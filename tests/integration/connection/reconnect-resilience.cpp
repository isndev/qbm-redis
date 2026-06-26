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
 * Tagged `slow` (the DEBUG-SLEEP stall cases hold the single-threaded server busy for a
 * couple of seconds). The lifetime/UAF case (`DestroyDuringInflightReconnectNoUAF`) was
 * promoted to the system tier (system/connection/reconnect-lifetime-uaf.cpp) since it only
 * needs a loopback listener, not a live daemon.
 *
 * Determinism fixes vs the legacy test-reconnect.cpp:
 *   - `EXHAUSTS_ATTEMPTS` now asserts the observer fired EXACTLY twice (== 2), not >= 1.
 *   - the 80M-iteration EVAL busy-loops (CPU-speed-dependent) were replaced with
 *     `DEBUG SLEEP <n>`, which freezes the server for a deterministic wall-clock window so
 *     a short client deadline / disconnect wins the race reproducibly.
 *   - the fixed `run_until([]{return false;}, 1200ms)` post-test recovery sleeps were
 *     removed — the server frees itself when DEBUG SLEEP returns, and the shared fixture's
 *     bounded connect-retry already tolerates a transiently-busy server for the next test.
 */

#include <chrono>
#include <string>
#include <vector>
#include <gtest/gtest.h>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include "../redis.h"
#include "../../shared/redis_integration_fixture.h"

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
// kStallIterations is sized so the loop reliably exceeds the sub-second client deadlines
// below (~50ms) with wide margin even on a fast CPU, while staying comfortably under the
// 5s auto-reconnect window even on a slow CPU (measured ~0.6s for 2e8 on this box; the
// loop is finite, so it can never wedge the server indefinitely).
constexpr long long kStallIterations = 200000000LL; // 2e8

inline std::string
server_busy_loop_script() {
    return "local x=0 for i=1," + std::to_string(kStallIterations) + " do x=x+1 end return x";
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
    ASSERT_TRUE(run_until([&] { return client.is_reconnecting(); }, 1000ms))
        << "auto-reconnect never started";
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
//    gracefully ("disconnected"), no crash, no desync. DEBUG SLEEP freezes the
//    server deterministically so the reply arrives well after the disconnect.
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

    bool done = false, ok = true;
    qb::io::async::coro_scheduler().spawn([&]() -> qb::io::async::task<void> {
        // A long EVAL busy-loop occupies the server for a fixed window; its reply cannot
        // return before we disconnect, so the pending command must complete with !ok().
        auto r = co_await client.command<long long>("EVAL", server_busy_loop_script(), "0");
        ok     = r.ok();
        done   = true;
    });
    (void) run_until([] { return false; }, 50ms); // let the EVAL get sent
    client.disconnect();
    // The disconnect should fail the pending command promptly (not wait out the full stall).
    ASSERT_TRUE(run_until([&] { return done; }, 3000ms));
    EXPECT_FALSE(ok) << "in-flight command should fail with 'disconnected', not stick";
}

// ============================================================================
// 8. Command timeout: a non-blocking command with no reply within the deadline drops
//    the connection and fails the pending command with "timed out"; auto-reconnect then
//    restores the connection. DEBUG SLEEP provides the deterministic no-reply window.
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

    bool        done = false;
    bool        ok   = true;
    std::string err;
    qb::io::async::coro_scheduler().spawn([&]() -> qb::io::async::task<void> {
        // A long EVAL busy-loop is not a blocking command, so the 50ms client deadline
        // applies: the server is busy and no reply arrives in time → the connection is
        // dropped and the command times out.
        auto r = co_await client.command<long long>("EVAL", server_busy_loop_script(), "0");
        ok     = r.ok();
        err    = r.error();
        done   = true;
    });
    ASSERT_TRUE(run_until([&] { return done; }, 3000ms));

    EXPECT_FALSE(ok) << "command should have timed out";
    EXPECT_NE(err.find("timed out"), std::string::npos) << "got: " << err;

    EXPECT_TRUE(run_until([&] { return client.is_connected(); }, 5000ms))
        << "client should auto-reconnect after a command timeout";
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
