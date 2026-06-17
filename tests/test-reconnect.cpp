/*
 * qb - C++ Actor Framework
 * Copyright (C) 2011-2025 isndev (cpp.actor). All rights reserved.
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

#include <gtest/gtest.h>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include "../redis.h"
#include "protocol_test_common.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

#define REDIS_URI      {"tcp://localhost:6379"}
#define BAD_REDIS_URI  {"tcp://localhost:19999"}

using namespace qb::io;
using namespace std::chrono_literals;

// ============================================================================
// Test fixture: all tests run in both RESP2 and RESP3
// ============================================================================

class ReconnectProtocolModesTest : public ProtocolModesTestBase {};

INSTANTIATE_PROTOCOL_MODES(ReconnectProtocolModesTest);

// ============================================================================
// Helper: run event loop until predicate is true or timeout elapses
// ============================================================================
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

// Destroying the client while an auto-reconnect connect is in flight must not
// use-after-free the connector. The reconnect task is spawned (detached), so it
// outlives the client; its connect_awaiter completion lambda touches the client
// (`_client.setup_connection(_client._uri, ...)`) and must check the client's liveness
// (connector_alive()), not just the awaiter's own validity flag (which stays true on
// the still-alive reconnect coroutine frame).
//
// The UAF is only reachable when the connect SUCCEEDS (raw_io is open) while the client
// is dead — a failed/timed-out connect never touches _client. So we reconnect to a
// local listening socket that never accept()s: the kernel still completes the TCP
// handshake, so the connect succeeds and the completion lambda reaches setup_connection.
// We destroy the client after the reconnect starts but before the connect completes,
// then pump so the lambda fires against the freed client. With the fix it detects the
// dead client and exits cleanly; before the fix AddressSanitizer reports
// heap-use-after-free in setup_connection.
TEST(ReconnectLifetime, DestroyDuringInflightReconnectNoUAF) {
    qb::io::async::init();

    // Listening socket on an ephemeral loopback port; never accept() — the kernel
    // completes the 3-way handshake from its backlog, so a connect to it succeeds.
    const int lfd = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(lfd, 0);
    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = 0;
    ASSERT_EQ(::bind(lfd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)), 0);
    ASSERT_EQ(::listen(lfd, 16), 0);
    socklen_t alen = sizeof(addr);
    ASSERT_EQ(::getsockname(lfd, reinterpret_cast<sockaddr *>(&addr), &alen), 0);
    const int port = ntohs(addr.sin_port);

    auto client = std::make_unique<qb::redis::tcp::client>(qb::io::uri{"tcp://localhost:6379"});
    ASSERT_TRUE(qb::io::async::run_sync(client->connect()));

    client->set_uri(qb::io::uri{"tcp://127.0.0.1:" + std::to_string(port)});
    client->enable_auto_reconnect(qb::redis::RetryPolicy{}
                                      .with_max_attempts(3)
                                      .with_initial_delay(50ms)
                                      .with_connect_timeout(2s)
                                      .with_jitter(false));
    client->disconnect();

    // Reconnect started: the connect to the listener is registered and in flight (its
    // completion lambda runs on a later loop iteration, not this one).
    ASSERT_TRUE(run_until([&] { return client->is_reconnecting(); }, 2000ms));

    client.reset(); // destroy while the (about-to-succeed) connect is in flight

    // Pump so the connect completes and the lambda fires against the freed client.
    for (int i = 0; i < 500; ++i)
        qb::io::async::run(EVRUN_NOWAIT);

    ::close(lfd);
    SUCCEED();
}

// ============================================================================
// 1. AUTO-RECONNECT — success after manual disconnect
//    Connect → enable auto-reconnect → disconnect() → verify reconnect fires
//    → verify is_connected() returns true again
// ============================================================================
TEST_P(ReconnectProtocolModesTest, CORO_AUTO_RECONNECT_AFTER_MANUAL_DISCONNECT) {
    qb::redis::tcp::client client{qb::io::uri{REDIS_URI}};

    // ── initial connection ──────────────────────────────────────────────────
    bool initially_connected = false;
    auto setup = [&client, &initially_connected]() -> qb::io::async::task<void> {
        initially_connected = co_await client.connect();
        if (initially_connected && GetParam() == ProtocolMode::RESP3) {
            auto h = co_await client.hello(3);
            if (!h.ok()) initially_connected = false;
        }
    };
    qb::io::async::coro_scheduler().spawn(setup());
    ASSERT_TRUE(run_until([&] { return initially_connected; }));
    ASSERT_TRUE(client.is_connected());

    // ── enable auto-reconnect with fast backoff ─────────────────────────────
    client.enable_auto_reconnect(
        qb::redis::RetryPolicy{}
            .with_initial_delay(50ms)
            .with_max_delay(200ms)
            .with_connect_timeout(2s)
            .with_jitter(false));

    // ── force a disconnect ──────────────────────────────────────────────────
    client.disconnect();
    EXPECT_FALSE(client.is_connected());

    // ── wait for is_reconnecting() to flip on ──────────────────────────────
    ASSERT_TRUE(run_until([&] { return client.is_reconnecting(); }, 1000ms));

    // ── wait for full reconnect ─────────────────────────────────────────────
    ASSERT_TRUE(run_until([&] { return !client.is_reconnecting(); }, 5000ms));
    EXPECT_TRUE(client.is_connected());

    // ── RESP3: re-negotiate protocol on fresh connection (Redis defaults to RESP2)
    if (GetParam() == ProtocolMode::RESP3) {
        bool hello_done = false;
        bool hello_ok   = false;
        qb::io::async::coro_scheduler().spawn([&]() -> qb::io::async::task<void> {
            auto h = co_await client.hello(3);
            hello_ok   = h.ok();
            hello_done = true;
        });
        ASSERT_TRUE(run_until([&] { return hello_done; }, 2000ms))
            << "HELLO 3 after reconnect did not complete";
        ASSERT_TRUE(hello_ok) << "HELLO 3 failed after reconnect";
    }

    // ── verify the connection is usable ─────────────────────────────────────
    bool ping_ok   = false;
    bool ping_done = false;
    qb::io::async::coro_scheduler().spawn([&]() -> qb::io::async::task<void> {
        auto r = co_await client.ping();
        ping_ok   = r.ok();
        ping_done = true;
    });
    ASSERT_TRUE(run_until([&] { return ping_done; }, 2000ms));
    EXPECT_TRUE(ping_ok);
}

// ============================================================================
// 2. AUTO-RECONNECT — exhausts attempts on unreachable host
// ============================================================================
TEST_P(ReconnectProtocolModesTest, CORO_AUTO_RECONNECT_EXHAUSTS_ATTEMPTS) {
    // First connect to a good server so we can trigger a disconnect
    qb::redis::tcp::client client{qb::io::uri{REDIS_URI}};

    bool initially_connected = false;
    auto setup = [&client, &initially_connected]() -> qb::io::async::task<void> {
        initially_connected = co_await client.connect();
        if (initially_connected && GetParam() == ProtocolMode::RESP3) {
            auto h = co_await client.hello(3);
            if (!h.ok()) initially_connected = false;
        }
    };
    qb::io::async::coro_scheduler().spawn(setup());
    ASSERT_TRUE(run_until([&] { return initially_connected; }));

    // Switch the URI to a bad address so the reconnect will fail,
    // then enable auto-reconnect with only 2 attempts
    int  retry_count    = 0;

    client.enable_auto_reconnect(
        qb::redis::RetryPolicy{}
            .with_max_attempts(2)
            .with_initial_delay(50ms)
            .with_connect_timeout(300ms)
            .with_jitter(false)
            .with_on_retry([&](int n, auto /*wait*/) { retry_count = n; }));

    // Swap the URI to an unreachable host before disconnecting
    // (the reconnect will use whatever URI is stored on the client)
    client.set_uri(qb::io::uri{BAD_REDIS_URI});

    // Trigger the reconnect path
    client.disconnect();

    // Wait long enough for 2 failed attempts (0.3 s each + small delays)
    qb::io::async::run_for(3000ms);

    EXPECT_FALSE(client.is_connected());
    EXPECT_FALSE(client.is_reconnecting()); // finished (all attempts done)
    EXPECT_GE(retry_count, 1);             // observer was called at least once
}

// ============================================================================
// 3. DISABLE_AUTO_RECONNECT — no reconnect after calling disable
// ============================================================================
TEST_P(ReconnectProtocolModesTest, CORO_AUTO_RECONNECT_CAN_BE_DISABLED) {
    qb::redis::tcp::client client{qb::io::uri{REDIS_URI}};

    bool initially_connected = false;
    auto setup = [&client, &initially_connected]() -> qb::io::async::task<void> {
        initially_connected = co_await client.connect();
        if (initially_connected && GetParam() == ProtocolMode::RESP3) {
            auto h = co_await client.hello(3);
            if (!h.ok()) initially_connected = false;
        }
    };
    qb::io::async::coro_scheduler().spawn(setup());
    ASSERT_TRUE(run_until([&] { return initially_connected; }));

    client.enable_auto_reconnect();
    client.disable_auto_reconnect(); // disable immediately after

    client.disconnect();

    // Spin the event loop briefly — no reconnect should start
    qb::io::async::run_for(300ms);

    EXPECT_FALSE(client.is_connected());
    EXPECT_FALSE(client.is_reconnecting());
}

// ============================================================================
// 4. IS_RECONNECTING state is false before any connect attempt
// ============================================================================
TEST_P(ReconnectProtocolModesTest, CORO_IS_RECONNECTING_INITIALLY_FALSE) {
    qb::redis::tcp::client client{qb::io::uri{REDIS_URI}};
    EXPECT_FALSE(client.is_reconnecting());
    EXPECT_FALSE(client.is_connected());
}

// ============================================================================
// 5. Commands issued while reconnecting fail gracefully with _ok == false
// ============================================================================
TEST_P(ReconnectProtocolModesTest, CORO_COMMANDS_FAIL_GRACEFULLY_ON_DISCONNECT) {
    qb::redis::tcp::client client{qb::io::uri{REDIS_URI}};

    bool initially_connected = false;
    auto setup = [&client, &initially_connected]() -> qb::io::async::task<void> {
        initially_connected = co_await client.connect();
        if (initially_connected && GetParam() == ProtocolMode::RESP3) {
            auto h = co_await client.hello(3);
            if (!h.ok()) initially_connected = false;
        }
    };
    qb::io::async::coro_scheduler().spawn(setup());
    ASSERT_TRUE(run_until([&] { return initially_connected; }));

    // Issue a command BEFORE disconnecting to ensure it's in the reply queue
    bool callback_fired = false;
    bool reply_ok       = true; // will be set to false on disconnect
    client.ping([&](auto &&reply) {
        callback_fired = true;
        reply_ok       = reply.ok();
    });

    // Disconnect while the PING is in flight
    client.disconnect();
    client.await(); // drain the queue

    EXPECT_TRUE(callback_fired);
    EXPECT_FALSE(reply_ok); // command lost → _ok == false
}

TEST_P(ReconnectProtocolModesTest, RECONNECT_THEN_PING) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        redis.enable_auto_reconnect(
            qb::redis::RetryPolicy{}
                .with_initial_delay(50ms)
                .with_max_delay(200ms)
                .with_connect_timeout(2s)
                .with_jitter(false));
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
        if (r.ok()) EXPECT_EQ(r.result(), "PONG");
        done = true;
    });
    while (!done) qb::io::async::run(EVRUN_NOWAIT);
}

// ============================================================================
// 7. DISCONNECT WHILE A SLOW COMMAND IS IN FLIGHT — the pending command must
//    fail gracefully ("disconnected"), no crash, no desync, clean teardown.
//    (EVAL busy-loops server-side so the reply arrives well after disconnect.)
// ============================================================================
TEST_P(ReconnectProtocolModesTest, CORO_DISCONNECT_WITH_SLOW_COMMAND_IN_FLIGHT) {
    qb::redis::tcp::client client{qb::io::uri{REDIS_URI}};

    bool ready = false;
    qb::io::async::coro_scheduler().spawn([&]() -> qb::io::async::task<void> {
        ready = co_await client.connect();
        if (ready && GetParam() == ProtocolMode::RESP3) {
            auto h = co_await client.hello(3);
            if (!h.ok()) ready = false;
        }
    });
    ASSERT_TRUE(run_until([&] { return ready; }));

    bool done = false, ok = true;
    qb::io::async::coro_scheduler().spawn([&]() -> qb::io::async::task<void> {
        // Finite counter loop: redis.call('TIME') is frozen during script
        // execution, so a time-based loop would never advance.
        static const char *kBusy =
            "local i = 0 while i < 80000000 do i = i + 1 end return 1";
        auto r = co_await client.command<long long>("EVAL", kBusy, "0");
        ok   = r.ok();
        done = true;
    });
    run_until([] { return false; }, 50ms); // let the EVAL get sent
    client.disconnect();
    ASSERT_TRUE(run_until([&] { return done; }, 3000ms));
    EXPECT_FALSE(ok); // failed with "disconnected", not stuck, not crashed

    // Let the server finish its busy script before the next test connects.
    run_until([] { return false; }, 1200ms);
}

// ============================================================================
// 8. COMMAND TIMEOUT — a non-blocking command with no reply within the
//    deadline drops the connection and fails the pending command with
//    "command timed out"; auto-reconnect then restores the connection.
// ============================================================================
TEST_P(ReconnectProtocolModesTest, CORO_COMMAND_TIMEOUT_DROPS_CONNECTION) {
    qb::redis::tcp::client client{qb::io::uri{REDIS_URI}};

    bool ready = false;
    qb::io::async::coro_scheduler().spawn([&]() -> qb::io::async::task<void> {
        ready = co_await client.connect();
        if (ready && GetParam() == ProtocolMode::RESP3) {
            auto h = co_await client.hello(3);
            if (!h.ok()) ready = false;
        }
    });
    ASSERT_TRUE(run_until([&] { return ready; }));

    client.set_command_timeout(50ms); // short deadline; the server-side EVAL must not win the race
    client.enable_auto_reconnect(
        qb::redis::RetryPolicy{}
            .with_initial_delay(50ms)
            .with_max_delay(200ms)
            .with_connect_timeout(2s)
            .with_jitter(false));

    bool        done = false;
    bool        ok   = true;
    std::string err;
    qb::io::async::coro_scheduler().spawn([&]() -> qb::io::async::task<void> {
        // EVAL busy-loops server-side: no reply arrives before the client
        // deadline, so the connection must be dropped. EVAL is not a
        // blocking command, so the deadline applies.
        static const char *kBusy =
            "local i = 0 while i < 80000000 do i = i + 1 end return 1";
        auto r = co_await client.command<long long>("EVAL", kBusy, "0");
        ok   = r.ok();
        err  = r.error();
        done = true;
    });
    ASSERT_TRUE(run_until([&] { return done; }, 3000ms));

    EXPECT_FALSE(ok) << "command should have timed out";
    EXPECT_NE(err.find("timed out"), std::string::npos) << "got: " << err;

    // The deadline dropped the connection; auto-reconnect brings it back.
    EXPECT_TRUE(run_until([&] { return client.is_connected(); }, 5000ms))
        << "client should auto-reconnect after a command timeout";

    // Let the server finish its busy script before the next test connects.
    run_until([] { return false; }, 1200ms);
}

// ============================================================================
// 9. COMMAND TIMEOUT — blocking commands (BLPOP, …) are exempt: their
//    server-side timeout governs, so a short client deadline must NOT drop
//    them prematurely.
// ============================================================================
TEST_P(ReconnectProtocolModesTest, CORO_COMMAND_TIMEOUT_EXEMPTS_BLOCKING) {
    qb::redis::tcp::client client{qb::io::uri{REDIS_URI}};

    bool ready = false;
    qb::io::async::coro_scheduler().spawn([&]() -> qb::io::async::task<void> {
        ready = co_await client.connect();
        if (ready && GetParam() == ProtocolMode::RESP3) {
            auto h = co_await client.hello(3);
            if (!h.ok()) ready = false;
        }
    });
    ASSERT_TRUE(run_until([&] { return ready; }));

    client.set_command_timeout(300ms); // shorter than the 1s BLPOP below

    bool done = false, ok = false, has_value = true, still_connected = false;
    qb::io::async::coro_scheduler().spawn([&]() -> qb::io::async::task<void> {
        auto key = std::string("qbm_to_blpop_") +
                   (GetParam() == ProtocolMode::RESP3 ? "r3" : "r2");
        [[maybe_unused]] auto _del = co_await client.del(std::vector<std::string>{key});
        // BLPOP on an empty key with a 1s server timeout: must outlive the
        // 300ms client deadline and return nil rather than being dropped.
        auto r          = co_await client.blpop(std::vector<std::string>{key}, 1);
        ok              = r.ok();
        has_value       = r.result().has_value();
        still_connected = client.is_connected();
        done            = true;
    });
    ASSERT_TRUE(run_until([&] { return done; }, 4000ms));

    EXPECT_TRUE(ok) << "blocking command must not be timed out by the client";
    EXPECT_FALSE(has_value); // BLPOP returned nil (server-side timeout)
    EXPECT_TRUE(still_connected);
}

// ============================================================================
int
main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
