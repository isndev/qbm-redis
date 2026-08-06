/*
 * qb - C++ Actor Framework
 * Copyright (C) 2011-2026 isndev (cpp.actor). All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 *         limitations under the License.
 */

/**
 * @file integration/connection/reconnect-resilience.cpp
 * @brief Live auto-reconnect + command-timeout resilience for qb::redis::tcp::client,
 *        RESP2 and RESP3.
 *
 * Integration tier — needs a live redis (env `REDIS_URI`, default tcp://localhost:6379).
 * Tagged `slow` (the server-stall cases hold the single-threaded server busy for a fraction of
 * a second via a Lua EVAL busy-loop — see `server_busy_loop_script` below). The lifetime/UAF case
 * (`DestroyDuringInflightReconnectNoUAF`) was promoted to the system tier
 * (system/connection/reconnect-lifetime-uaf.cpp) since it only needs a loopback listener, not a
 * live daemon.
 *
 * Determinism vs the legacy test-reconnect.cpp:
 *   - `EXHAUSTS_ATTEMPTS` now asserts the observer fired EXACTLY twice (== 2), not >= 1.
 *   - The server stall is a bounded CPU busy-loop inside a single Lua `EVAL`, NOT `DEBUG SLEEP`:
 *     DEBUG is gated behind the immutable `enable-debug-command` config (off by default, cannot be
 *     toggled at runtime), so `DEBUG SLEEP` returns an immediate "DEBUG command not allowed" error
 *     on a stock server — no stall, no race to win. The EVAL loop fully occupies the
 *     single-threaded server for a finite window whose LENGTH IS MEASURED, not assumed: the
 *     iteration count is calibrated against the server under test (`calibrated_stall_iterations`)
 *     so the stall lands near `kStallTarget` on any host instead of scaling with CPU speed. EVAL is
 *     not in the client's blocking-command exemption set, so the client-side command deadline
 *     genuinely applies. See the block comment above `kStallTarget` for why the bound cannot come
 *     from inside the script.
 *   - The fixed `run_until([]{return false;}, 1200ms)` post-test recovery sleeps were removed: the
 *     EVAL loop is finite so the server frees itself when it returns, and the shared fixture's
 *     bounded connect-retry already tolerates a transiently-busy server for the next test.
 */

#include <algorithm>
#include <chrono>
#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include "../../shared/redis_integration_fixture.h"
#include <qbm/redis/redis.h>

using namespace qb::io;
using namespace std::chrono_literals;
using namespace qb::redis::test;

namespace {

// Pump the loop until `pred()` holds or `timeout` elapses; returns whether it held.
template <typename Pred>
bool
run_until(Pred &&pred, std::chrono::milliseconds timeout = 5000ms) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!pred()) {
        qb::io::async::run(EVRUN_NOWAIT);
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
    }
    return true;
}

// A server-side stall that does NOT require DEBUG.
//
// `DEBUG SLEEP` is the obvious deterministic stall, but it is gated behind the immutable
// `enable-debug-command` config (off by default, cannot be toggled at runtime), so it
// returns an immediate "DEBUG command not allowed" error on a stock server — no stall, no
// timeout. Instead we run a bounded CPU busy-loop inside a Lua EVAL: the single-threaded
// server is fully occupied for the duration of the loop and cannot reply (or accept the
// reconnect) until it finishes. EVAL is NOT in the client's blocking-command exemption set
// (only B*/WAIT*/XREAD* are), so the client-side command deadline genuinely applies.
//
// The stall has to sit inside a window: long enough to blow past the sub-second client deadlines
// below (~50ms, plus the 50ms send-flush settle), short enough to stay well under the server's
// `busy-reply-threshold` (5000ms by default), past which the server answers `-BUSY` to every other
// client — including the NEXT test's connect, which then times out and `GTEST_SKIP`s while ctest
// still reports this binary `Passed`. A stall that overruns is therefore silent.
//
// A fixed iteration count cannot hold that window, because it is a WORK budget and its wall time
// scales with the host. Measured, same box (macOS, Redis 8.10.0), same 2e8 count:
//
//     idle                 601 ms      (matches the sizing note this replaced)
//     20 CPU spinners   52 820 ms      10.5x OVER the 5000 ms threshold
//
// and calibrated, under that same load: a 1977 ms probe chose 4.0e6 iterations and stalled
// 1188 ms — still under the threshold. Halving the fixed count to survive the loaded case would
// put it under the client deadline on the idle one, which is why a constant cannot work.
//
// The bias is deliberate: a stall that comes out too SHORT fails test 8 loudly (the command
// succeeds instead of timing out), while one that comes out too long is the silent mode above.
//
// Bounding it from INSIDE the script does not work either, and this is the part worth writing
// down: Redis freezes a script's view of the clock. `redis.call('TIME')` returns the value cached
// when the script started and only advances once the busy-script watchdog fires at
// `busy-reply-threshold` — so a loop written to stop after 300ms of TIME measured 5033ms here, i.e.
// exactly the overrun it was meant to prevent. The check cannot come from inside the script.
//
// So it comes from outside: time a small probe EVAL against THIS server once per process, and size
// the real stall from the measured rate. That makes the stall a DURATION on every host.
constexpr auto      kStallTarget     = 400ms;      ///< 8x the 50ms deadline, 12x under the 5s threshold
constexpr long long kProbeIterations = 20000000LL; ///< 2e7 — ~70ms here, still sub-second on a 10x slower box

inline std::string
busy_loop_script(long long const iterations) {
    return "local x=0 for i=1," + std::to_string(iterations) + " do x=x+1 end return x";
}

/// Iterations that take ~`kStallTarget` on the server under test. Measured once, on first use.
[[nodiscard]] inline long long
calibrated_stall_iterations() {
    static const long long value = []() -> long long {
        // Fallback if the probe cannot run: the smallest count that still beats the deadlines on a
        // fast host. Erring short only weakens the stall; erring long is what wedges the server.
        constexpr long long kFallback = 20000000LL;

        // A bare connect, NOT `redis_try_connect` — that helper issues a FLUSHALL, and this runs
        // in the middle of a test rather than in SetUp.
        qb::redis::tcp::client probe{qb::io::uri{redis_test_uri()}};
        if (!qb::io::async::run_sync(probe.connect()))
            return kFallback; // daemon down — every test here is about to GTEST_SKIP anyway

        const auto start   = std::chrono::steady_clock::now();
        const auto reply   = qb::io::async::run_sync(probe.command<long long>("EVAL", busy_loop_script(kProbeIterations), "0"));
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start);
        probe.disconnect();

        if (!reply.ok() || elapsed.count() <= 0)
            return kFallback;

        const auto target = std::chrono::duration_cast<std::chrono::microseconds>(kStallTarget);
        const auto scaled = static_cast<long long>(static_cast<double>(kProbeIterations) * static_cast<double>(target.count())
                                                   / static_cast<double>(elapsed.count()));
        // Sanity band around the probe, so a wildly mis-measured probe (a descheduled process, a
        // clock jump) cannot produce either a no-op stall or a multi-second one.
        return std::clamp(scaled, kProbeIterations / 8, kProbeIterations * 100);
    }();
    return value;
}

inline std::string
server_busy_loop_script() {
    return busy_loop_script(calibrated_stall_iterations());
}

class ReconnectProtocolModesTest : public ProtocolModesTestBase {};

} // namespace

INSTANTIATE_PROTOCOL_MODES(ReconnectProtocolModesTest);

// ============================================================================
// 1. Auto-reconnect succeeds after a manual disconnect.
// ============================================================================
TEST_P(ReconnectProtocolModesTest, AUTO_RECONNECT_AFTER_MANUAL_DISCONNECT) {
    qb::redis::tcp::client client{qb::io::uri{redis_test_uri()}};

    bool initially_connected = false;
    auto setup               = [&client, &initially_connected]() -> qb::io::async::task<void> {
        initially_connected = co_await client.connect();
        if (initially_connected && GetParam() == ProtocolMode::RESP3) {
            auto h = co_await client.hello(3);
            if (!h.ok())
                initially_connected = false;
        }
    };
    qb::io::async::coro_scheduler().spawn(setup());
    ASSERT_TRUE(run_until([&] { return initially_connected; }));
    ASSERT_TRUE(client.is_connected());

    client.enable_auto_reconnect(
        qb::redis::RetryPolicy{}.with_initial_delay(50ms).with_max_delay(200ms).with_connect_timeout(2s).with_jitter(false));

    client.disconnect();
    EXPECT_FALSE(client.is_connected());

    ASSERT_TRUE(run_until([&] { return client.is_reconnecting(); }, 1000ms));
    ASSERT_TRUE(run_until([&] { return !client.is_reconnecting(); }, 5000ms));
    EXPECT_TRUE(client.is_connected());

    if (GetParam() == ProtocolMode::RESP3) {
        bool hello_done = false;
        bool hello_ok   = false;
        qb::io::async::coro_scheduler().spawn([&]() -> qb::io::async::task<void> {
            auto h     = co_await client.hello(3);
            hello_ok   = h.ok();
            hello_done = true;
        });
        ASSERT_TRUE(run_until([&] { return hello_done; }, 2000ms)) << "HELLO 3 after reconnect did not complete";
        ASSERT_TRUE(hello_ok) << "HELLO 3 failed after reconnect";
    }

    bool ping_ok   = false;
    bool ping_done = false;
    qb::io::async::coro_scheduler().spawn([&]() -> qb::io::async::task<void> {
        auto r    = co_await client.ping();
        ping_ok   = r.ok();
        ping_done = true;
    });
    ASSERT_TRUE(run_until([&] { return ping_done; }, 2000ms));
    EXPECT_TRUE(ping_ok);
}

// ============================================================================
// 2. Auto-reconnect exhausts attempts on an unreachable host.
//    Observer fires once per failed attempt except the last → exactly 2 for 2 attempts.
// ============================================================================
TEST_P(ReconnectProtocolModesTest, AUTO_RECONNECT_EXHAUSTS_ATTEMPTS) {
    qb::redis::tcp::client client{qb::io::uri{redis_test_uri()}};

    bool initially_connected = false;
    auto setup               = [&client, &initially_connected]() -> qb::io::async::task<void> {
        initially_connected = co_await client.connect();
        if (initially_connected && GetParam() == ProtocolMode::RESP3) {
            auto h = co_await client.hello(3);
            if (!h.ok())
                initially_connected = false;
        }
    };
    qb::io::async::coro_scheduler().spawn(setup());
    ASSERT_TRUE(run_until([&] { return initially_connected; }));

    int retry_count = 0;
    client.enable_auto_reconnect(qb::redis::RetryPolicy{}
                                     .with_max_attempts(3)
                                     .with_initial_delay(50ms)
                                     .with_max_delay(50ms)
                                     .with_connect_timeout(300ms)
                                     .with_jitter(false)
                                     .with_on_retry([&](int /*attempt*/, qb::duration /*wait*/) { ++retry_count; }));

    // Point the client at an unreachable host so every reconnect attempt fails fast.
    client.set_uri(qb::io::uri{"tcp://localhost:19999"});
    client.disconnect();

    // disconnect() defers teardown to the io watcher's own callback, so _is_reconnecting is
    // NOT set synchronously — on(disconnected) flips it true on a later loop turn. We must
    // therefore first pump until reconnection has STARTED; otherwise `!is_reconnecting()`
    // is trivially true the instant after disconnect() and the wait below returns before a
    // single attempt runs (on_retry would never fire → retry_count stuck at 0).
    ASSERT_TRUE(run_until([&] { return client.is_reconnecting(); }, 1000ms)) << "auto-reconnect never started";
    // Then wait until reconnection has finished (all attempts exhausted).
    ASSERT_TRUE(run_until([&] { return !client.is_reconnecting(); }, 5000ms));
    EXPECT_FALSE(client.is_connected());
    // on_retry fires once after each failed attempt except the last: 3 attempts → exactly 2.
    EXPECT_EQ(retry_count, 2);
}

// ============================================================================
// 3. disable_auto_reconnect → no reconnect after disconnect.
// ============================================================================
TEST_P(ReconnectProtocolModesTest, AUTO_RECONNECT_CAN_BE_DISABLED) {
    qb::redis::tcp::client client{qb::io::uri{redis_test_uri()}};

    bool initially_connected = false;
    auto setup               = [&client, &initially_connected]() -> qb::io::async::task<void> {
        initially_connected = co_await client.connect();
        if (initially_connected && GetParam() == ProtocolMode::RESP3) {
            auto h = co_await client.hello(3);
            if (!h.ok())
                initially_connected = false;
        }
    };
    qb::io::async::coro_scheduler().spawn(setup());
    ASSERT_TRUE(run_until([&] { return initially_connected; }));

    client.enable_auto_reconnect();
    client.disable_auto_reconnect();
    client.disconnect();

    // Brief pump: no reconnect should ever start. (Bounded, not a guarantee-by-sleep —
    // is_reconnecting must remain false the whole window.)
    bool started = false;
    (void) run_until([&] { return (started = client.is_reconnecting()); }, 300ms);
    EXPECT_FALSE(started);
    EXPECT_FALSE(client.is_connected());
    EXPECT_FALSE(client.is_reconnecting());
}

// ============================================================================
// 4. is_reconnecting() is false before any connect attempt.
// ============================================================================
TEST_P(ReconnectProtocolModesTest, IS_RECONNECTING_INITIALLY_FALSE) {
    qb::redis::tcp::client client{qb::io::uri{redis_test_uri()}};
    EXPECT_FALSE(client.is_reconnecting());
    EXPECT_FALSE(client.is_connected());
}

// ============================================================================
// 5. Commands in flight when the connection drops fail gracefully (_ok == false).
// ============================================================================
TEST_P(ReconnectProtocolModesTest, COMMANDS_FAIL_GRACEFULLY_ON_DISCONNECT) {
    qb::redis::tcp::client client{qb::io::uri{redis_test_uri()}};

    bool initially_connected = false;
    auto setup               = [&client, &initially_connected]() -> qb::io::async::task<void> {
        initially_connected = co_await client.connect();
        if (initially_connected && GetParam() == ProtocolMode::RESP3) {
            auto h = co_await client.hello(3);
            if (!h.ok())
                initially_connected = false;
        }
    };
    qb::io::async::coro_scheduler().spawn(setup());
    ASSERT_TRUE(run_until([&] { return initially_connected; }));

    bool callback_fired = false;
    bool reply_ok       = true;
    client.ping([&](auto &&reply) {
        callback_fired = true;
        reply_ok       = reply.ok();
    });

    client.disconnect();
    client.await(); // drain the queue

    EXPECT_TRUE(callback_fired);
    EXPECT_FALSE(reply_ok);
}

// ============================================================================
// 6. Reconnect on the shared fixture client, then PING works.
// ============================================================================
TEST_P(ReconnectProtocolModesTest, RECONNECT_THEN_PING) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        redis.enable_auto_reconnect(
            qb::redis::RetryPolicy{}.with_initial_delay(50ms).with_max_delay(200ms).with_connect_timeout(2s).with_jitter(false));
        redis.disconnect();
        for (int i = 0; i < 100 && (redis.is_reconnecting() || !redis.is_connected()); ++i) {
            co_await qb::io::async::sleep(50ms);
        }
        if (!redis.is_connected()) {
            ADD_FAILURE() << "Reconnect did not complete";
            done = true;
            co_return;
        }
        if (GetParam() == ProtocolMode::RESP3) {
            auto h = co_await redis.hello(3);
            EXPECT_TRUE(h.ok()) << h.error();
        }
        auto r = co_await redis.ping();
        EXPECT_TRUE(r.ok()) << r.error();
        EXPECT_EQ(r.result(), "PONG");
        done = true;
    });
    run_coro_test_until(done);
}

// ============================================================================
// 7. Disconnect while a slow command is in flight: the pending command must fail
//    gracefully ("disconnected"), no crash, no desync. A Lua EVAL CPU busy-loop
//    (NOT DEBUG SLEEP — disabled by default) occupies the single-threaded server
//    for a fixed window so its reply cannot return before the disconnect.
// ============================================================================
TEST_P(ReconnectProtocolModesTest, DISCONNECT_WITH_SLOW_COMMAND_IN_FLIGHT) {
    qb::redis::tcp::client client{qb::io::uri{redis_test_uri()}};

    bool ready = false;
    qb::io::async::coro_scheduler().spawn([&]() -> qb::io::async::task<void> {
        ready = co_await client.connect();
        if (ready && GetParam() == ProtocolMode::RESP3) {
            auto h = co_await client.hello(3);
            if (!h.ok())
                ready = false;
        }
    });
    ASSERT_TRUE(run_until([&] { return ready; }));

    // Built HERE, not inside the coroutine: the first call calibrates, and calibration drives the
    // loop through `run_sync`, which `ensure_not_inside_ready_drain()` forbids from inside a
    // coroutine already running under `run_ready()`.
    const std::string stall = server_busy_loop_script();

    bool done = false, ok = true;
    qb::io::async::coro_scheduler().spawn([&, stall]() -> qb::io::async::task<void> {
        // A long EVAL busy-loop occupies the server for a measured window; its reply cannot
        // return before we disconnect, so the pending command must complete with !ok().
        auto r = co_await client.command<long long>("EVAL", stall, "0");
        ok     = r.ok();
        done   = true;
    });
    // Bounded send-flush settle: pump the loop briefly so the queued EVAL bytes are written to
    // the socket BEFORE we disconnect — otherwise the disconnect could race ahead of the send and
    // we'd be testing "command never left the client" instead of "in-flight command is dropped".
    // This is NOT one of the post-test recovery sleeps the docstring removed; the client exposes no
    // public "bytes flushed" signal to convert this into a deterministic predicate, and 50ms is a
    // generous ceiling for flushing a single small EVAL over loopback.
    (void) run_until([] { return false; }, 50ms);
    client.disconnect();
    // The disconnect should fail the pending command promptly (not wait out the full stall).
    ASSERT_TRUE(run_until([&] { return done; }, 3000ms));
    EXPECT_FALSE(ok) << "in-flight command should fail with 'disconnected', not stick";
}

// ============================================================================
// 8. Command timeout: a non-blocking command with no reply within the deadline drops
//    the connection and fails the pending command with "timed out"; auto-reconnect then
//    restores the connection. A Lua EVAL CPU busy-loop (NOT DEBUG SLEEP — disabled by
//    default) provides the deterministic no-reply window.
// ============================================================================
TEST_P(ReconnectProtocolModesTest, COMMAND_TIMEOUT_DROPS_CONNECTION) {
    qb::redis::tcp::client client{qb::io::uri{redis_test_uri()}};

    bool ready = false;
    qb::io::async::coro_scheduler().spawn([&]() -> qb::io::async::task<void> {
        ready = co_await client.connect();
        if (ready && GetParam() == ProtocolMode::RESP3) {
            auto h = co_await client.hello(3);
            if (!h.ok())
                ready = false;
        }
    });
    ASSERT_TRUE(run_until([&] { return ready; }));

    client.set_command_timeout(50ms); // far shorter than the server stall below
    client.enable_auto_reconnect(
        qb::redis::RetryPolicy{}.with_initial_delay(50ms).with_max_delay(200ms).with_connect_timeout(2s).with_jitter(false));

    // Built before the spawn (calibration uses run_sync — see the note in test 7). It runs on its
    // own short-lived client, so the 50ms deadline set above does not apply to the probe.
    const std::string stall = server_busy_loop_script();

    bool        done = false;
    bool        ok   = true;
    std::string err;
    qb::io::async::coro_scheduler().spawn([&, stall]() -> qb::io::async::task<void> {
        // A long EVAL busy-loop is not a blocking command, so the 50ms client deadline
        // applies: the server is busy and no reply arrives in time → the connection is
        // dropped and the command times out.
        auto r = co_await client.command<long long>("EVAL", stall, "0");
        ok     = r.ok();
        err    = r.error();
        done   = true;
    });
    ASSERT_TRUE(run_until([&] { return done; }, 3000ms));

    EXPECT_FALSE(ok) << "command should have timed out";
    EXPECT_NE(err.find("timed out"), std::string::npos) << "got: " << err;

    EXPECT_TRUE(run_until([&] { return client.is_connected(); }, 5000ms)) << "client should auto-reconnect after a command timeout";
}

// ============================================================================
// 9. Command timeout exempts blocking commands: a short client deadline must NOT drop a
//    BLPOP whose server-side timeout governs.
// ============================================================================
TEST_P(ReconnectProtocolModesTest, COMMAND_TIMEOUT_EXEMPTS_BLOCKING) {
    qb::redis::tcp::client client{qb::io::uri{redis_test_uri()}};

    bool ready = false;
    qb::io::async::coro_scheduler().spawn([&]() -> qb::io::async::task<void> {
        ready = co_await client.connect();
        if (ready && GetParam() == ProtocolMode::RESP3) {
            auto h = co_await client.hello(3);
            if (!h.ok())
                ready = false;
        }
    });
    ASSERT_TRUE(run_until([&] { return ready; }));

    client.set_command_timeout(300ms); // shorter than the 1s BLPOP below

    bool done = false, ok = false, has_value = true, still_connected = false;
    qb::io::async::coro_scheduler().spawn([&]() -> qb::io::async::task<void> {
        auto                  key  = std::string("qbm_to_blpop_") + (GetParam() == ProtocolMode::RESP3 ? "r3" : "r2");
        [[maybe_unused]] auto _del = co_await client.del(std::vector<std::string>{key});
        auto                  r    = co_await client.blpop(std::vector<std::string>{key}, 1);
        ok                         = r.ok();
        has_value                  = r.result().has_value();
        still_connected            = client.is_connected();
        done                       = true;
    });
    ASSERT_TRUE(run_until([&] { return done; }, 4000ms));

    EXPECT_TRUE(ok) << "blocking command must not be timed out by the client";
    EXPECT_FALSE(has_value); // BLPOP returned nil (server-side timeout)
    EXPECT_TRUE(still_connected);
}
