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
 * @file integration/connection/connection-commands.cpp
 * @brief Connection-level verbs (AUTH/ECHO/HELLO/PING/RESET/SELECT/SWAPDB) + the
 *        connect_with_retry / RetryPolicy ladder, RESP2 and RESP3.
 *
 * Integration tier — needs a live redis (env `REDIS_URI`, default tcp://localhost:6379).
 * Migrated from test-connection-commands.cpp: the 6 short-form verb dups (PING /
 * PING_WITH_MESSAGE / ECHO / SELECT / SWAPDB / HELLO_RESP3_MAP) were deleted (strictly
 * subsumed by the CORO_* cases); the `run_for(5s)` magic budgets on the retry-exhausted /
 * observer tests were replaced with a watchdog-bounded `run_coro_test_until`; a real
 * auth-failure case (WRONGPASS / NOAUTH) was added.
 */

#include <string>
#include <gtest/gtest.h>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include "../redis.h"
#include "../../shared/redis_integration_fixture.h"

using namespace qb::io;
using namespace std::chrono;
using namespace std::chrono_literals;
using namespace qb::redis::test;

class ConnectionProtocolModesTest : public ProtocolModesTestBase {};

INSTANTIATE_PROTOCOL_MODES(ConnectionProtocolModesTest);

// ============================================================================
// Verbs
// ============================================================================

// AUTH happy path: against an unauthenticated server, AUTH default/"" succeeds.
TEST_P(ConnectionProtocolModesTest, AUTH_EMPTY_PASSWORD_OK) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto reply = co_await redis.auth("default", "");
        EXPECT_TRUE(reply.ok()) << reply.error();
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// AUTH failure: invalid credentials must surface a Redis error, NOT a green pass.
//
// Ground-truth (Redis 8.x, confirmed via redis-cli):
//   - The built-in `default` user is created with the `nopass` flag, which means it
//     accepts ANY password. So two-arg `AUTH default <anything>` returns +OK — that is
//     real, correct Redis behaviour, NOT a bug. Asserting it fails would contradict the
//     server.
//   - The unambiguous "bad credentials" path that fails on every standalone server,
//     regardless of requirepass/ACL config, is AUTH for a username that does not exist:
//     `AUTH <nonexistent-user> <pw>` → "WRONGPASS invalid username-password pair or user
//     is disabled.". That is what we assert here.
TEST_P(ConnectionProtocolModesTest, AUTH_WRONG_PASSWORD_FAILS) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto reply = co_await redis.auth("definitely-not-a-real-user", "definitely-not-the-password");
        EXPECT_FALSE(reply.ok()) << "AUTH for a nonexistent user must fail, got OK";
        const std::string err(reply.error());
        EXPECT_FALSE(err.empty());
        EXPECT_TRUE(err.find("WRONGPASS") != std::string::npos ||
                    err.find("NOAUTH") != std::string::npos ||
                    err.find("ERR") != std::string::npos)
            << "unexpected AUTH error text: " << err;
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

TEST_P(ConnectionProtocolModesTest, ECHO) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string message = "Hello Redis!";
        auto              reply   = co_await redis.echo(message);
        EXPECT_TRUE(reply.ok()) << reply.error();
        EXPECT_EQ(reply.result(), message);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

TEST_P(ConnectionProtocolModesTest, PING) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto reply = co_await redis.ping();
        EXPECT_TRUE(reply.ok()) << reply.error();
        EXPECT_EQ(reply.result(), "PONG");
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

TEST_P(ConnectionProtocolModesTest, PING_WITH_MESSAGE) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string message = "Hello";
        auto              reply   = co_await redis.ping(message);
        EXPECT_TRUE(reply.ok()) << reply.error();
        EXPECT_EQ(reply.result(), message);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

TEST_P(ConnectionProtocolModesTest, ERROR_REPLY) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto reply = co_await redis.command<std::string>("INVALIDCMD");
        EXPECT_FALSE(reply.ok()) << "expected an error for an invalid command";
        EXPECT_FALSE(std::string(reply.error()).empty());
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// HELLO 3 (RESP3 handshake) returns a map with server info.
TEST_P(ConnectionProtocolModesTest, HELLO_RESP3_MAP) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto reply = co_await redis.hello(3);
        EXPECT_TRUE(reply.ok()) << "HELLO 3 failed: " << reply.error();
        if (reply.ok()) {
            const auto &info = reply.result();
            EXPECT_TRUE(info.is_object()) << "HELLO 3 reply must be a RESP3 map";
            EXPECT_TRUE(info.contains("server")) << "HELLO map must contain a server field";
            EXPECT_TRUE(info.contains("proto")) << "HELLO map must report the negotiated proto";
        }
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// PING continues to work after the protocol switch.
TEST_P(ConnectionProtocolModesTest, HELLO_THEN_PING) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto hello = co_await redis.hello(3);
        EXPECT_TRUE(hello.ok()) << "HELLO 3 failed: " << hello.error();
        if (!hello.ok()) {
            completed = true;
            co_return;
        }
        auto ping = co_await redis.ping();
        EXPECT_TRUE(ping.ok()) << "PING after HELLO 3 failed: " << ping.error();
        EXPECT_EQ(ping.result(), "PONG");
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// RESET resets server-side connection state but does NOT close the connection;
// verify PING still works afterwards.
TEST_P(ConnectionProtocolModesTest, RESET) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto reply = co_await redis.reset();
        EXPECT_TRUE(reply.ok()) << "RESET failed: " << reply.error();
        if (!reply.ok()) {
            completed = true;
            co_return;
        }
        auto ping = co_await redis.ping();
        EXPECT_TRUE(ping.ok()) << "PING after RESET failed: " << ping.error();
        EXPECT_EQ(ping.result(), "PONG");
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

TEST_P(ConnectionProtocolModesTest, SELECT) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto r0 = co_await redis.select(0);
        EXPECT_TRUE(r0.ok()) << r0.error();
        EXPECT_TRUE(r0.result().ok());
        auto r1 = co_await redis.select(1);
        EXPECT_TRUE(r1.ok()) << r1.error();
        EXPECT_TRUE(r1.result().ok());
        auto r2 = co_await redis.select(0); // restore db 0 for the next test
        EXPECT_TRUE(r2.ok()) << r2.error();
        EXPECT_TRUE(r2.result().ok());
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

TEST_P(ConnectionProtocolModesTest, SWAPDB) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto r1 = co_await redis.swapdb(0, 1);
        EXPECT_TRUE(r1.ok()) << r1.error();
        EXPECT_TRUE(r1.result().ok());
        auto r2 = co_await redis.swapdb(0, 1); // swap back
        EXPECT_TRUE(r2.ok()) << r2.error();
        EXPECT_TRUE(r2.result().ok());
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// ============================================================================
// connect_with_retry / RetryPolicy ladder
// ============================================================================

// Immediate success when the server is up.
TEST_P(ConnectionProtocolModesTest, RETRY_IMMEDIATE_SUCCESS) {
    qb::redis::tcp::client client{qb::io::uri{redis_test_uri()}};
    bool                   completed = false;
    bool                   connected = false;
    auto                   test_task = [&]() -> qb::io::async::task<void> {
        connected = co_await client.connect_with_retry(
            qb::redis::RetryPolicy{}.with_max_attempts(5).with_initial_delay(10ms).with_connect_timeout(2s));
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
    EXPECT_TRUE(connected);
}

// Exhausts all attempts when the host is unreachable. ECONNREFUSED on a dead port returns
// almost instantly, so the whole ladder (3 attempts × ≤500 ms + 2 backoffs) finishes well
// inside the watchdog. No fixed run_for budget.
TEST_P(ConnectionProtocolModesTest, RETRY_EXHAUSTED) {
    qb::redis::tcp::client client{qb::io::uri{"tcp://127.0.0.1:19379"}};
    bool                   completed = false;
    bool                   connected = true; // intentionally wrong; expect false
    auto                   test_task = [&]() -> qb::io::async::task<void> {
        connected = co_await client.connect_with_retry(qb::redis::RetryPolicy{}
                                                           .with_max_attempts(3)
                                                           .with_initial_delay(20ms)
                                                           .with_max_delay(50ms)
                                                           .with_jitter(false)
                                                           .with_connect_timeout(500ms));
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed, 10s);
    EXPECT_TRUE(completed);
    EXPECT_FALSE(connected);
}

// on_retry is invoked once after each failed attempt except the last:
// 3 attempts → exactly 2 on_retry calls (deterministic, jitter off).
TEST_P(ConnectionProtocolModesTest, RETRY_OBSERVER) {
    qb::redis::tcp::client client{qb::io::uri{"tcp://127.0.0.1:19380"}};
    bool                   completed   = false;
    int                    retry_count = 0;
    auto                   test_task   = [&]() -> qb::io::async::task<void> {
        (void) co_await client.connect_with_retry(
            qb::redis::RetryPolicy{}
                .with_max_attempts(3)
                .with_initial_delay(10ms)
                .with_jitter(false)
                .with_connect_timeout(200ms)
                .with_on_retry([&](int /*attempt*/, qb::duration /*wait*/) { ++retry_count; }));
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed, 10s);
    EXPECT_TRUE(completed);
    EXPECT_EQ(retry_count, 2);
}

// connect_with_retry with the URI overload.
TEST_P(ConnectionProtocolModesTest, RETRY_WITH_URI) {
    qb::redis::tcp::client client;
    bool                   completed = false;
    bool                   connected = false;
    auto                   test_task = [&]() -> qb::io::async::task<void> {
        connected = co_await client.connect_with_retry(
            qb::io::uri{redis_test_uri()},
            qb::redis::RetryPolicy{}.with_max_attempts(3).with_initial_delay(10ms));
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
    EXPECT_TRUE(connected);
}

// connect() with a configurable timeout.
TEST_P(ConnectionProtocolModesTest, CUSTOM_TIMEOUT) {
    qb::redis::tcp::client client{qb::io::uri{redis_test_uri()}};
    bool                   completed = false;
    bool                   connected = false;
    auto                   test_task = [&]() -> qb::io::async::task<void> {
        connected = co_await client.connect(1s);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
    EXPECT_TRUE(connected);
}
