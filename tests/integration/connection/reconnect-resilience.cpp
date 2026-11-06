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
 * Tagged `slow` (the command-timeout case holds the single-threaded server busy for a fraction of
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
 *     iteration count is calibrated against the server under test (`calibrated_stall`) so the
 *     stall lands near `kStallTarget` on any host instead of scaling with CPU speed. EVAL is
 *     not in the client's blocking-command exemption set, so the client-side command deadline
 *     genuinely applies. See the block comment above `kStallTarget` for why the bound cannot come
 *     from inside the script.
 *   - The fixed `run_until([]{return false;}, 1200ms)` post-test recovery sleeps were removed: the
 *     EVAL loop is finite so the server frees itself when it returns, and the shared fixture's
 *     bounded connect-retry already tolerates a transiently-busy server for the next test.
 *   - The in-flight disconnect case holds its reply instead of delaying it: a BLPOP on a key nobody
 *     pushes, seen blocked by the server before the disconnect (Huly QB-1272).
 */

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <gtest/gtest.h>
#include <memory>
#include <optional>
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
// The stall has to sit inside a window: long enough to blow past the client deadline below (50ms),
// short enough to stay well under the server's `busy-reply-threshold` (5000ms by default), past
// which the server answers `-BUSY` to every other
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
// succeeds instead of timing out, and the test prints how long it lasted and what sized it), while
// one that comes out too long is the silent mode above.
//
// Bounding it from INSIDE the script does not work either, and this is the part worth writing
// down: Redis freezes a script's view of the clock. `redis.call('TIME')` returns the value cached
// when the script started and only advances once the busy-script watchdog fires at
// `busy-reply-threshold` — so a loop written to stop after 300ms of TIME measured 5033ms here, i.e.
// exactly the overrun it was meant to prevent. The check cannot come from inside the script.
//
// So it comes from outside: time a small probe EVAL against THIS server once per process, and size
// the real stall from the measured rate. That makes the stall a DURATION on every host.
//
// The probe is timed on the TEST's clock, so a host that deschedules this process can only make it
// read SLOWER than the server ran, never faster -- and a slow read sizes the stall short. One such
// read, on a host loaded by parallel builds, sized the stall under the 50ms settle test 7 then used,
// and the reply came first (Huly QB-1272). So the fastest of `kProbes` runs sizes it, and the stall
// never runs fewer than `kFloorIterations`: the band's floor was an eighth of a probe, ~9ms here.
// The floor is work, not time: on a host that runs it in under 50ms, a test process starved through
// all five probes still sizes a stall under the deadline -- test 8 then fails, loudly, with both
// numbers.
constexpr auto      kStallTarget     = 400ms;      ///< 8x the 50ms deadline, 12x under the 5s threshold
constexpr long long kProbeIterations = 5000000LL;  ///< 5e6 — ~17ms here, so five of them hold the server ~85ms
constexpr int       kProbes          = 5;          ///< the fastest of five sizes the stall
constexpr long long kFloorIterations = 20000000LL; ///< 2e7 — ~70ms here: the fewest a stall ever runs

inline std::string
busy_loop_script(long long const iterations) {
    return "local x=0 for i=1," + std::to_string(iterations) + " do x=x+1 end return x";
}

/// The stall: iterations that take ~`kStallTarget` on the server under test, and the fastest probe
/// that sized them (zero when the calibration fell back: no probe ran, or one failed). Measured
/// once, on first use.
struct stall_calibration {
    long long                 iterations;
    std::chrono::microseconds fastest_probe;
};

[[nodiscard]] inline stall_calibration const &
calibrated_stall() {
    static const stall_calibration value = []() -> stall_calibration {
        // Fallback if the probe cannot run: the smallest count that still beats the deadline on a
        // fast host. Erring short only weakens the stall; erring long is what wedges the server.
        constexpr long long kFallback = kFloorIterations;

        // A bare connect, NOT `redis_try_connect` — that helper issues a FLUSHALL, and this runs
        // in the middle of a test rather than in SetUp.
        qb::redis::tcp::client probe{qb::io::uri{redis_test_uri()}};
        if (!qb::io::async::run_sync(probe.connect()))
            return {kFallback, {}}; // daemon down — every test here is about to GTEST_SKIP anyway

        auto fastest = std::chrono::microseconds::max();
        for (int i = 0; i < kProbes; ++i) {
            const auto start   = std::chrono::steady_clock::now();
            const auto reply   = qb::io::async::run_sync(probe.command<long long>("EVAL", busy_loop_script(kProbeIterations), "0"));
            const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start);
            if (!reply.ok() || elapsed.count() <= 0) {
                probe.disconnect();
                return {kFallback, {}};
            }
            fastest = std::min(fastest, elapsed);
        }
        probe.disconnect();

        const auto target = std::chrono::duration_cast<std::chrono::microseconds>(kStallTarget);
        const auto scaled = static_cast<long long>(static_cast<double>(kProbeIterations) * static_cast<double>(target.count())
                                                   / static_cast<double>(fastest.count()));
        // `steady_clock` does not jump and a probe never reads faster than the server ran, so `scaled`
        // stays at or under the target at the probe's rate; the ceiling (5e8, ~1.7s here) bounds only
        // a host far faster than this one. What can still overrun is a server that slows down after
        // the probes. The floor is `kFloorIterations`.
        return {std::clamp(scaled, kFloorIterations, kProbeIterations * 100), fastest};
    }();
    return value;
}

inline std::string
server_busy_loop_script() {
    return busy_loop_script(calibrated_stall().iterations);
}

/// CLIENT KILL from another connection: a loss the client did not ask for -- the peer's, which an armed
/// reconnection run recovers from -- unlike its own disconnect(), which is terminal (Huly QB-126a).
qb::io::async::task<bool>
kill_from_outside(long long id) {
    qb::redis::tcp::client executioner{qb::io::uri{redis_test_uri()}};
    if (!co_await executioner.connect())
        co_return false;
    auto killed = co_await executioner.client_kill("", id);
    co_return killed.ok() && killed.result() == 1;
}

/// The connection's protocol as the server reports it: HELLO with no version changes nothing.
long long
reported_proto(qb::redis::tcp::client &client) {
    auto r = qb::io::async::run_sync(client.command<qb::json>("HELLO"));
    if (!r.ok())
        return -1;
    const auto &j = r.result();
    if (j.is_object() && j.contains("proto"))
        return j["proto"].get<long long>();
    if (j.is_array())
        for (std::size_t i = 0; i + 1 < j.size(); i += 2)
            if (j[i].is_string() && j[i].get<std::string>() == "proto")
                return j[i + 1].get<long long>();
    return -1;
}

/// Whether a CLIENT LIST line shows its connection blocked in a BLPOP: flag `b`, `cmd=blpop`.
[[nodiscard]] bool
blocked_in_blpop(std::string const &line) {
    const auto value = [&line](std::string const &field) -> std::string {
        const auto at = line.find(" " + field + "=");
        if (at == std::string::npos)
            return {};
        const auto from = at + field.size() + 2;
        return line.substr(from, line.find_first_of(" \r\n", from) - from);
    };
    return value("flags").find('b') != std::string::npos && value("cmd") == "blpop";
}

class ReconnectProtocolModesTest : public ProtocolModesTestBase {};

} // namespace

INSTANTIATE_PROTOCOL_MODES(ReconnectProtocolModesTest);

// ============================================================================
// 1. A connection the server kills is recovered by the armed run, and the handshake -- RESP3 here --
//    is replayed on the new connection by itself: no poll, no hello(3) by hand (Huly QB-126a).
// ============================================================================
TEST_P(ReconnectProtocolModesTest, AUTO_RECONNECT_AFTER_THE_SERVER_KILLS_THE_CONNECTION) {
    qb::redis::tcp::client client{qb::io::uri{redis_test_uri()}};
    const long long        proto = GetParam() == ProtocolMode::RESP3 ? 3 : 2;
    client.with_resp3(proto == 3);
    int connected = 0;
    client.on_connected([&connected] { ++connected; });
    ASSERT_TRUE(qb::io::async::run_sync(client.connect()));
    ASSERT_EQ(connected, 1);
    EXPECT_EQ(reported_proto(client), proto);

    client.enable_auto_reconnect(
        qb::redis::RetryPolicy{}.with_initial_delay(50ms).with_max_delay(200ms).with_connect_timeout(2s).with_jitter(false));
    const auto id = qb::io::async::run_sync(client.client_id());
    ASSERT_TRUE(id.ok()) << id.error();
    ASSERT_TRUE(qb::io::async::run_sync(kill_from_outside(id.result())));

    ASSERT_TRUE(run_until([&] { return connected == 2; }, 5000ms)) << "the armed run did not bring the connection back";
    EXPECT_TRUE(client.is_connected());
    EXPECT_EQ(reported_proto(client), proto) << "the handshake must replay the protocol on the new connection";
    auto ping = qb::io::async::run_sync(client.ping());
    EXPECT_TRUE(ping.ok()) << ping.error();
}

// ============================================================================
// 2. Auto-reconnect exhausts attempts on an unreachable host: the client is down, retries_exhausted.
//    Observer fires once per failed attempt except the last → exactly 2 for 3 attempts.
// ============================================================================
TEST_P(ReconnectProtocolModesTest, AUTO_RECONNECT_EXHAUSTS_ATTEMPTS) {
    qb::redis::tcp::client client{qb::io::uri{redis_test_uri()}};
    client.with_resp3(GetParam() == ProtocolMode::RESP3);
    ASSERT_TRUE(qb::io::async::run_sync(client.connect()));

    int                                retry_count = 0;
    std::optional<qb::io::down_reason> down;
    client.on_down([&down](qb::io::down_reason r) { down = r; });
    client.enable_auto_reconnect(qb::redis::RetryPolicy{}
                                     .with_max_attempts(3)
                                     .with_initial_delay(50ms)
                                     .with_max_delay(50ms)
                                     .with_connect_timeout(300ms)
                                     .with_jitter(false)
                                     .with_on_retry([&](int /*attempt*/, qb::duration /*wait*/) { ++retry_count; }));

    // Point the client at an unreachable host so every reconnect attempt fails fast, then lose the
    // connection the way a run recovers from.
    const auto id = qb::io::async::run_sync(client.client_id());
    ASSERT_TRUE(id.ok()) << id.error();
    client.set_uri(qb::io::uri{"tcp://localhost:19999"});
    ASSERT_TRUE(qb::io::async::run_sync(kill_from_outside(id.result())));

    ASSERT_TRUE(run_until([&] { return down.has_value(); }, 10000ms)) << "the run never ended";
    EXPECT_EQ(*down, qb::io::down_reason::retries_exhausted);
    EXPECT_EQ(client.state(), qb::io::connection_state::down);
    EXPECT_FALSE(client.is_connected());
    EXPECT_FALSE(client.is_reconnecting());
    // on_retry fires once after each failed attempt except the last: 3 attempts → exactly 2.
    EXPECT_EQ(retry_count, 2);
}

// ============================================================================
// 3. No run without a policy, and none after an explicit disconnect() even with one (R1).
// ============================================================================
TEST_P(ReconnectProtocolModesTest, AUTO_RECONNECT_CAN_BE_DISABLED) {
    qb::redis::tcp::client client{qb::io::uri{redis_test_uri()}};
    ASSERT_TRUE(qb::io::async::run_sync(client.connect()));

    client.enable_auto_reconnect();
    client.disable_auto_reconnect();
    const auto id = qb::io::async::run_sync(client.client_id());
    ASSERT_TRUE(id.ok()) << id.error();
    ASSERT_TRUE(qb::io::async::run_sync(kill_from_outside(id.result())));

    ASSERT_TRUE(run_until([&] { return client.state() == qb::io::connection_state::down; }, 3000ms));
    EXPECT_EQ(client.down_reason(), qb::io::down_reason::peer);
    EXPECT_FALSE(client.is_reconnecting());
}

TEST_P(ReconnectProtocolModesTest, EXPLICIT_DISCONNECT_IS_TERMINAL_WITH_A_POLICY_ARMED) {
    qb::redis::tcp::client client{qb::io::uri{redis_test_uri()}};
    ASSERT_TRUE(qb::io::async::run_sync(client.connect()));
    client.enable_auto_reconnect(qb::redis::RetryPolicy{}.with_initial_delay(1ms).with_jitter(false));

    client.disconnect();
    EXPECT_EQ(client.state(), qb::io::connection_state::down);
    EXPECT_EQ(client.down_reason(), qb::io::down_reason::user);
    // Bounded, not a guarantee-by-sleep: no run may start the whole window.
    bool started = false;
    (void) run_until([&] { return (started = client.is_reconnecting() || client.is_connected()); }, 300ms);
    EXPECT_FALSE(started) << "an explicit disconnect() must start no run";
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
        bool connected = co_await client.connect();
        if (connected && GetParam() == ProtocolMode::RESP3) {
            auto h    = co_await client.hello(3);
            connected = h.ok();
        }
        initially_connected = connected; // set LAST: the whole setup has finished when the test sees it
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
// 6. On the shared fixture client, a command submitted while the run is in progress is held, then answered
//    once the killed connection is back with its handshake. It is submitted from on_disconnected, where the
//    client reads `reconnecting` by contract: the run's first attempt is immediate, so a RESP2 client with
//    nothing to handshake is ready again a loop pass or two after the loss -- before run_sync() has seen the
//    kill's own reply. A poll for is_reconnecting() after the kill found that run already over (Huly QB-1090,
//    with QB-1103's prompt pump): the hooks and the counters say what happened, the state only where it is.
// ============================================================================
TEST_P(ReconnectProtocolModesTest, RECONNECT_THEN_PING) {
    struct observed {
        int                                     lost      = 0;
        int                                     connected = 0;
        std::optional<qb::io::connection_state> state_at_loss;
        std::uint64_t                           held_at_loss = 0;
        std::optional<bool>                     ping_ok;
        std::string                             ping;
    };
    // Shared with the hooks and the PING's callback, which may still run if an assertion below returns early.
    const auto seen = std::make_shared<observed>();
    redis.with_resp3(GetParam() == ProtocolMode::RESP3); // the next handshake applies it
    redis.enable_auto_reconnect(
        qb::redis::RetryPolicy{}.with_initial_delay(50ms).with_max_delay(200ms).with_connect_timeout(2s).with_jitter(false));
    redis.on_connected([seen] { ++seen->connected; });
    redis.on_disconnected([this, seen](qb::io::down_reason) {
        ++seen->lost;
        seen->state_at_loss = redis.state();
        seen->held_at_loss  = redis.stats().held;
        redis.ping([seen](qb::redis::Reply<std::string> &&reply) {
            seen->ping_ok = reply.ok();
            seen->ping    = reply.ok() ? reply.result() : reply.error();
        });
    });
    const auto id = qb::io::async::run_sync(redis.client_id());
    ASSERT_TRUE(id.ok()) << id.error();
    const auto before = redis.stats();
    ASSERT_TRUE(qb::io::async::run_sync(kill_from_outside(id.result())));
    ASSERT_TRUE(run_until([&] { return seen->ping_ok.has_value(); }, 5000ms))
        << "the PING submitted at the loss was never answered (losses seen: " << seen->lost << ")";

    EXPECT_EQ(seen->lost, 1) << "the client sees the kill, once";
    EXPECT_EQ(qb::io::connection_state_name(seen->state_at_loss.value_or(qb::io::connection_state::idle)), "reconnecting")
        << "a policy is armed: the loss starts a run";
    EXPECT_EQ(redis.stats().held, seen->held_at_loss + 1) << "submitted while the run is in progress: held";
    EXPECT_TRUE(*seen->ping_ok) << seen->ping;
    EXPECT_EQ(seen->ping, "PONG");
    EXPECT_EQ(seen->connected, 1) << "one reconnection";
    EXPECT_EQ(redis.stats().reconnects, before.reconnects + 1);
    EXPECT_TRUE(redis.is_connected());
}

// ============================================================================
// 7. Disconnect while a command is in flight: the pending command must fail
//    gracefully ("disconnected"), no crash, no desync. The reply is HELD, not
//    delayed: a BLPOP on a key nobody pushes has no answer before its server-side
//    timeout (30s), and a second connection sees the server hold it blocked before
//    the disconnect -- in flight by construction, on any host. A calibrated EVAL
//    stall raced a 50ms send-flush settle here, and a probe slowed by a loaded host
//    sized the stall under the settle: the reply came first (Huly QB-1272).
// ============================================================================
TEST_P(ReconnectProtocolModesTest, DISCONNECT_WITH_SLOW_COMMAND_IN_FLIGHT) {
    qb::redis::tcp::client client{qb::io::uri{redis_test_uri()}};

    bool ready = false;
    qb::io::async::coro_scheduler().spawn([&]() -> qb::io::async::task<void> {
        bool connected = co_await client.connect();
        if (connected && GetParam() == ProtocolMode::RESP3) {
            auto h    = co_await client.hello(3);
            connected = h.ok();
        }
        ready = connected; // set LAST: the whole setup has finished when the test sees it
    });
    ASSERT_TRUE(run_until([&] { return ready; }));
    const auto id = qb::io::async::run_sync(client.client_id());
    ASSERT_TRUE(id.ok()) << id.error();

    struct outcome {
        bool        done = false;
        bool        ok   = true;
        std::string error;
    };
    // Shared with the coroutine, which outlives this scope if an assertion below returns early: past
    // its first suspension it touches only `seen`. A BLPOP creates no key, so nothing is left behind.
    const auto        seen = std::make_shared<outcome>();
    const std::string key  = protocol_key("qbm_inflight_blpop");
    qb::io::async::coro_scheduler().spawn([&client, seen, key]() -> qb::io::async::task<void> {
        auto r      = co_await client.blpop(std::vector<std::string>{key}, 30);
        seen->ok    = r.ok();
        seen->error = r.ok() ? std::string{} : r.error();
        seen->done  = true;
    });
    // In flight, not queued: the server holds the BLPOP blocked on this connection. A 50ms settle
    // stood for that and could not prove it.
    qb::redis::tcp::client observer{qb::io::uri{redis_test_uri()}};
    ASSERT_TRUE(qb::io::async::run_sync(observer.connect()));
    observer.set_command_timeout(500ms); // a sibling's stall on the shared server cannot hold one poll past the window
    bool        held = false;
    std::string last;
    const auto  until = std::chrono::steady_clock::now() + 3s;
    while (!held && !seen->done && std::chrono::steady_clock::now() < until) {
        const auto line = qb::io::async::run_sync(observer.command<std::string>("CLIENT", "LIST", "ID", std::to_string(id.result())));
        last            = line.ok() ? line.result() : line.error();
        held            = line.ok() && blocked_in_blpop(line.result());
    }
    observer.disconnect();
    ASSERT_FALSE(seen->done) << "the BLPOP was answered before the disconnect (ok=" << seen->ok << "): " << seen->error;
    ASSERT_TRUE(held) << "the server never held the BLPOP blocked; last CLIENT LIST: " << last;

    client.disconnect();
    // The disconnect fails the pending command at once: no reply can come.
    ASSERT_TRUE(run_until([&] { return seen->done; }, 3000ms));
    EXPECT_FALSE(seen->ok) << "in-flight command should fail with 'disconnected', not stick";
    EXPECT_EQ(seen->error, "disconnected");
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
        bool connected = co_await client.connect();
        if (connected && GetParam() == ProtocolMode::RESP3) {
            auto h    = co_await client.hello(3);
            connected = h.ok();
        }
        ready = connected; // set LAST: the whole setup has finished when the test sees it
    });
    ASSERT_TRUE(run_until([&] { return ready; }));

    client.set_command_timeout(50ms); // far shorter than the server stall below
    client.enable_auto_reconnect(
        qb::redis::RetryPolicy{}.with_initial_delay(50ms).with_max_delay(200ms).with_connect_timeout(2s).with_jitter(false));

    // Built HERE, not inside the coroutine: the first call calibrates, and calibration drives the
    // loop through `run_sync`, which `ensure_not_inside_ready_drain()` forbids from inside a
    // coroutine already running under `run_ready()`. It runs on its own short-lived client, so the
    // 50ms deadline set above does not apply to the probes.
    const std::string stall       = server_busy_loop_script();
    const auto       &calibration = calibrated_stall();

    bool                                  done = false;
    bool                                  ok   = true;
    std::string                           err;
    std::chrono::steady_clock::time_point sent{}, answered{};
    qb::io::async::coro_scheduler().spawn([&, stall]() -> qb::io::async::task<void> {
        // A long EVAL busy-loop is not a blocking command, so the 50ms client deadline
        // applies: the server is busy and no reply arrives in time → the connection is
        // dropped and the command times out.
        sent     = std::chrono::steady_clock::now();
        auto r   = co_await client.command<long long>("EVAL", stall, "0");
        answered = std::chrono::steady_clock::now();
        ok       = r.ok();
        err      = r.error();
        done     = true;
    });
    ASSERT_TRUE(run_until([&] { return done; }, 3000ms));

    // Read after the fact, on the test's clock: a stall shorter than the deadline lets the command
    // succeed, and the failure says how long it lasted and what sized it (Huly QB-1272).
    const auto took = std::chrono::duration_cast<std::chrono::milliseconds>(answered - sent);
    if (ok && took < 50ms) {
        ADD_FAILURE() << "the EVAL was answered in " << took.count() << "ms, inside the 50ms deadline: the stall of " << calibration.iterations
                      << " iterations, sized from a fastest probe of " << calibration.fastest_probe.count() << "us, was shorter";
    } else if (ok) {
        ADD_FAILURE() << "the EVAL was answered after " << took.count()
                      << "ms, past the 50ms deadline, which never tripped: the client's command deadline, or this process "
                         "descheduled past it";
    }
    EXPECT_NE(err.find("timed out"), std::string::npos) << "got: " << err << " after " << took.count() << "ms";

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
        bool connected = co_await client.connect();
        if (connected && GetParam() == ProtocolMode::RESP3) {
            auto h    = co_await client.hello(3);
            connected = h.ok();
        }
        ready = connected; // set LAST: the whole setup has finished when the test sees it
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
