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
#include <thread>
#include "../redis.h"
#include "protocol_test_common.h"

using namespace qb::io;
using namespace std::chrono;

// ============================================================================
// Fixture: all tests run in both RESP2 and RESP3
// ============================================================================

class ConnectionProtocolModesTest : public ProtocolModesTestBase {};

INSTANTIATE_PROTOCOL_MODES(ConnectionProtocolModesTest);

/*
 * COROUTINE TESTS
 */

// Test AUTH with username and password using coroutines
TEST_P(ConnectionProtocolModesTest, CORO_CONNECTION_COMMANDS_AUTH_USERNAME_PASSWORD) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        // Note: This test assumes Redis is running without authentication
        // In a real environment, you would need to provide the correct username and password
        auto reply = co_await redis.auth("default", "");
        EXPECT_TRUE(reply.ok());

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test ECHO using coroutines
TEST_P(ConnectionProtocolModesTest, CORO_CONNECTION_COMMANDS_ECHO) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string message = "Hello Redis!";

        auto reply = co_await redis.echo(message);
        EXPECT_TRUE(reply.ok());
        EXPECT_EQ(reply.result(), message);

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test HELLO 3 (RESP3 handshake) - verifies live RESP3 protocol
TEST_P(ConnectionProtocolModesTest, CORO_CONNECTION_RESP3_HELLO) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto reply = co_await redis.hello(3);
        EXPECT_TRUE(reply.ok()) << "HELLO 3 failed: " << reply.error();
        if (reply.ok()) {
            const auto& info = reply.result();
            // RESP3 HELLO returns a map with server info
            EXPECT_TRUE(info.is_object());
            EXPECT_TRUE(info.contains("server") || info.contains("version"))
                << "HELLO reply should contain server info";
        }
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test PING after HELLO 3 (RESP3 mode) - verifies commands work after protocol switch
TEST_P(ConnectionProtocolModesTest, CORO_CONNECTION_RESP3_HELLO_THEN_PING) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto hello_reply = co_await redis.hello(3);
        EXPECT_TRUE(hello_reply.ok()) << "HELLO 3 failed: " << hello_reply.error();
        if (!hello_reply.ok()) {
            completed = true;
            co_return;
        }
        auto ping_reply = co_await redis.ping();
        EXPECT_TRUE(ping_reply.ok()) << "PING after HELLO 3 failed: " << ping_reply.error();
        if (ping_reply.ok()) EXPECT_EQ(ping_reply.result(), "PONG");
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test RESET using coroutines
// Note: RESET resets server-side connection state but does NOT close the connection.
// The connection stays open; we verify PING still works after RESET.
TEST_P(ConnectionProtocolModesTest, CORO_CONNECTION_COMMANDS_RESET) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto reply = co_await redis.reset();
        EXPECT_TRUE(reply.ok()) << "RESET failed: " << reply.error();
        if (!reply.ok()) {
            completed = true;
            co_return;
        }
        // Connection stays open after RESET; verify we can still PING
        auto ping_r = co_await redis.ping();
        EXPECT_TRUE(ping_r.ok()) << "PING after RESET failed: " << ping_r.error();
        if (ping_r.ok()) EXPECT_EQ(ping_r.result(), "PONG");
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) qb::io::async::run(EVRUN_NOWAIT);
}

// Test PING without message using coroutines
TEST_P(ConnectionProtocolModesTest, CORO_CONNECTION_COMMANDS_PING) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto reply = co_await redis.ping();
        EXPECT_TRUE(reply.ok());
        EXPECT_EQ(reply.result(), "PONG");

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test PING with message using coroutines
TEST_P(ConnectionProtocolModesTest, CORO_CONNECTION_COMMANDS_PING_WITH_MESSAGE) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string message = "Hello";

        auto reply = co_await redis.ping(message);
        EXPECT_TRUE(reply.ok());
        EXPECT_EQ(reply.result(), message);

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test SELECT using coroutines
TEST_P(ConnectionProtocolModesTest, CORO_CONNECTION_COMMANDS_SELECT) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        // Test selecting different databases
        auto reply1 = co_await redis.select(0);
        EXPECT_TRUE(reply1.ok());
        EXPECT_TRUE(reply1.result().ok());

        auto reply2 = co_await redis.select(1);
        EXPECT_TRUE(reply2.ok());
        EXPECT_TRUE(reply2.result().ok());

        auto reply3 = co_await redis.select(0);
        EXPECT_TRUE(reply3.ok());
        EXPECT_TRUE(reply3.result().ok());

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test SWAPDB using coroutines
TEST_P(ConnectionProtocolModesTest, CORO_CONNECTION_COMMANDS_SWAPDB) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        // Test swapping databases
        auto reply1 = co_await redis.swapdb(0, 1);
        EXPECT_TRUE(reply1.ok());
        EXPECT_TRUE(reply1.result().ok());

        auto reply2 = co_await redis.swapdb(0, 1);
        EXPECT_TRUE(reply2.ok());
        EXPECT_TRUE(reply2.result().ok());

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}


TEST_P(ConnectionProtocolModesTest, PING) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto r = co_await redis.ping();
        EXPECT_TRUE(r.ok()) << r.error();
        if (r.ok()) EXPECT_EQ(r.result(), "PONG");
        done = true;
    }());
    while (!done) qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(ConnectionProtocolModesTest, PING_WITH_MESSAGE) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto r = co_await redis.ping("hello");
        EXPECT_TRUE(r.ok()) << r.error();
        if (r.ok()) EXPECT_EQ(r.result(), "hello");
        done = true;
    }());
    while (!done) qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(ConnectionProtocolModesTest, ECHO) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto r = co_await redis.echo("hello");
        EXPECT_TRUE(r.ok()) << r.error();
        if (r.ok()) EXPECT_EQ(r.result(), "hello");
        done = true;
    }());
    while (!done) qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(ConnectionProtocolModesTest, HELLO_RESP3_MAP) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        auto r = co_await redis.hello(GetParam() == ProtocolMode::RESP3 ? 3 : 2);
        EXPECT_TRUE(r.ok()) << r.error();
        if (r.ok()) {
            const auto& info = r.result();
            EXPECT_TRUE(info.is_object() || info.is_array())
                << "HELLO reply must be map (RESP3) or array (RESP2)";
            if (info.is_object()) {
                EXPECT_TRUE(info.contains("server") || info.contains("version"))
                    << "HELLO map should contain server info";
            }
        }
        done = true;
    }());
    while (!done) qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(ConnectionProtocolModesTest, SELECT) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto r1 = co_await redis.select(0);
        EXPECT_TRUE(r1.ok()) << r1.error();
        if (r1.ok()) EXPECT_TRUE(r1.result().ok());
        auto r2 = co_await redis.select(1);
        EXPECT_TRUE(r2.ok()) << r2.error();
        if (r2.ok()) EXPECT_TRUE(r2.result().ok());
        (void)co_await redis.select(0);
        done = true;
    }());
    while (!done) qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(ConnectionProtocolModesTest, ERROR_REPLY) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto r = co_await redis.command<std::string>("INVALIDCMD");
        EXPECT_FALSE(r.ok()) << "Expected error for invalid command";
        if (!r.ok()) EXPECT_FALSE(r.error().empty());
        done = true;
    }());
    while (!done) qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(ConnectionProtocolModesTest, SWAPDB) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto r = co_await redis.swapdb(0, 1);
        EXPECT_TRUE(r.ok()) << r.error();
        if (r.ok()) EXPECT_TRUE(r.result().ok());
        (void)co_await redis.swapdb(0, 1);
        done = true;
    }());
    while (!done) qb::io::async::run(EVRUN_NOWAIT);
}

// ============================================================================
// RETRY POLICY TESTS
// ============================================================================

// Test: connect_with_retry succeeds on the first attempt when Redis is up
TEST_P(ConnectionProtocolModesTest, CORO_CONNECTION_RETRY_IMMEDIATE_SUCCESS) {
    qb::redis::tcp::client client{qb::io::uri{"tcp://localhost:6379"}};
    bool completed = false;
    bool connected = false;

    auto test_task = [&]() -> qb::io::async::task<void> {
        connected = co_await client.connect_with_retry(
            qb::redis::RetryPolicy{}
                .with_max_attempts(5)
                .with_initial_delay(std::chrono::milliseconds{10})
                .with_connect_timeout(2.0));
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }

    EXPECT_TRUE(connected);
}

// Test: connect_with_retry exhausts all attempts when host is unreachable
TEST_P(ConnectionProtocolModesTest, CORO_CONNECTION_RETRY_EXHAUSTED) {
    // Use a port with no listener; ECONNREFUSED arrives almost instantly
    qb::redis::tcp::client client{qb::io::uri{"tcp://127.0.0.1:19379"}};
    bool completed = false;
    bool connected = true;  // intentionally wrong; expect false

    auto test_task = [&]() -> qb::io::async::task<void> {
        connected = co_await client.connect_with_retry(
            qb::redis::RetryPolicy{}
                .with_max_attempts(3)
                .with_initial_delay(std::chrono::milliseconds{20})
                .with_max_delay(std::chrono::milliseconds{50})
                .with_jitter(false)       // deterministic timing
                .with_connect_timeout(0.5));
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    // Give it enough wall-clock time: 3 attempts × 0.5 s + 2 × 50 ms = ~1.6 s
    qb::io::async::run_for(std::chrono::seconds{5});

    EXPECT_TRUE(completed);
    EXPECT_FALSE(connected);
}

// Test: on_retry callback is invoked for each failed attempt
TEST_P(ConnectionProtocolModesTest, CORO_CONNECTION_RETRY_OBSERVER) {
    qb::redis::tcp::client client{qb::io::uri{"tcp://127.0.0.1:19380"}};
    bool  completed    = false;
    int   retry_count  = 0;

    auto test_task = [&]() -> qb::io::async::task<void> {
        co_await client.connect_with_retry(
            qb::redis::RetryPolicy{}
                .with_max_attempts(3)
                .with_initial_delay(std::chrono::milliseconds{10})
                .with_jitter(false)
                .with_connect_timeout(0.2)
                .with_on_retry([&](int /*attempt*/, std::chrono::milliseconds /*wait*/) {
                    ++retry_count;
                }));
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    qb::io::async::run_for(std::chrono::seconds{5});

    EXPECT_TRUE(completed);
    // on_retry is called after each failed attempt except the last
    // (3 attempts → 2 on_retry calls)
    EXPECT_EQ(retry_count, 2);
}

// Test: connect_with_retry with URI overload
TEST_P(ConnectionProtocolModesTest, CORO_CONNECTION_RETRY_WITH_URI) {
    qb::redis::tcp::client client;
    bool completed = false;
    bool connected = false;

    auto test_task = [&]() -> qb::io::async::task<void> {
        connected = co_await client.connect_with_retry(
            qb::io::uri{"tcp://localhost:6379"},
            qb::redis::RetryPolicy{}
                .with_max_attempts(3)
                .with_initial_delay(std::chrono::milliseconds{10}));
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }

    EXPECT_TRUE(connected);
}

// Test: connect() with configurable timeout
TEST_P(ConnectionProtocolModesTest, CORO_CONNECTION_CUSTOM_TIMEOUT) {
    qb::redis::tcp::client client{qb::io::uri{"tcp://localhost:6379"}};
    bool completed = false;
    bool connected = false;

    auto test_task = [&]() -> qb::io::async::task<void> {
        connected = co_await client.connect(1.0);  // 1 second timeout
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }

    EXPECT_TRUE(connected);
}

// Main function to run the tests
int
main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
