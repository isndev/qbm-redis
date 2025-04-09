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
#include <thread>
#include "../redis.h"

// Redis Configuration
#define REDIS_URI {"tcp://localhost:6379"}

using namespace qb::io;
using namespace std::chrono;

// Test fixture for Redis Connection commands
class RedisConnectionTest : public ::testing::Test {
protected:
    qb::redis::tcp::client redis{REDIS_URI};

    void
    SetUp() override {
        async::init();
        if (!redis.connect() || !redis.flushall())
            throw std::runtime_error("Failed to connect to Redis");

        // Wait for connection to be established
        redis.await();
        TearDown();
    }

    void
    TearDown() override {
        // Cleanup after tests
        redis.flushall();
        redis.await();
    }
};

/*
 * SYNCHRONOUS TESTS
 */

// Test AUTH with password only
TEST_F(RedisConnectionTest, DISABLED_SYNC_CONNECTION_COMMANDS_AUTH_PASSWORD) {
    // Note: This test assumes Redis is running without authentication
    // In a real environment, you would need to provide the correct password
    EXPECT_TRUE(redis.auth(""));
}

// Test AUTH with username and password
TEST_F(RedisConnectionTest, SYNC_CONNECTION_COMMANDS_AUTH_USERNAME_PASSWORD) {
    // Note: This test assumes Redis is running without authentication
    // In a real environment, you would need to provide the correct username and password
    EXPECT_TRUE(redis.auth("default", ""));
}

// Test ECHO
TEST_F(RedisConnectionTest, SYNC_CONNECTION_COMMANDS_ECHO) {
    std::string message = "Hello Redis!";
    EXPECT_EQ(redis.echo(message), message);
}

// Test PING without message
TEST_F(RedisConnectionTest, SYNC_CONNECTION_COMMANDS_PING) {
    EXPECT_EQ(redis.ping(), "PONG");
}

// Test PING with message
TEST_F(RedisConnectionTest, SYNC_CONNECTION_COMMANDS_PING_WITH_MESSAGE) {
    std::string message = "Hello";
    EXPECT_EQ(redis.ping(message), message);
}

// Test SELECT
TEST_F(RedisConnectionTest, SYNC_CONNECTION_COMMANDS_SELECT) {
    // Test selecting different databases
    EXPECT_TRUE(redis.select(0));
    EXPECT_TRUE(redis.select(1));
    EXPECT_TRUE(redis.select(0)); // Switch back to default database
}

// Test SWAPDB
TEST_F(RedisConnectionTest, SYNC_CONNECTION_COMMANDS_SWAPDB) {
    // Test swapping databases
    EXPECT_TRUE(redis.swapdb(0, 1));
    EXPECT_TRUE(redis.swapdb(0, 1)); // Swap back
}

// Test QUIT
TEST_F(RedisConnectionTest, DISABLED_SYNC_CONNECTION_COMMANDS_QUIT) {
    // Note: This test should be run last as it closes the connection
    EXPECT_TRUE(redis.quit());
}

/*
 * ASYNCHRONOUS TESTS
 */

// Test async AUTH with password only
TEST_F(RedisConnectionTest, DISABLED_ASYNC_CONNECTION_COMMANDS_AUTH_PASSWORD) {
    bool auth_result = false;

    redis.auth([&](auto &&reply) { auth_result = reply.ok(); }, "");

    redis.await();
    EXPECT_TRUE(auth_result);
}

// Test async AUTH with username and password
TEST_F(RedisConnectionTest, ASYNC_CONNECTION_COMMANDS_AUTH_USERNAME_PASSWORD) {
    bool auth_result = false;

    redis.auth([&](auto &&reply) { auth_result = reply.ok(); }, "default", "");

    redis.await();
    EXPECT_TRUE(auth_result);
}

// Test async ECHO
TEST_F(RedisConnectionTest, ASYNC_CONNECTION_COMMANDS_ECHO) {
    std::string message = "Hello Redis!";
    std::string echo_result;

    redis.echo([&](auto &&reply) { echo_result = reply.result(); }, message);

    redis.await();
    EXPECT_EQ(echo_result, message);
}

// Test async PING without message
TEST_F(RedisConnectionTest, ASYNC_CONNECTION_COMMANDS_PING) {
    std::string ping_result;

    redis.ping([&](auto &&reply) { ping_result = reply.result(); });

    redis.await();
    EXPECT_EQ(ping_result, "PONG");
}

// Test async PING with message
TEST_F(RedisConnectionTest, ASYNC_CONNECTION_COMMANDS_PING_WITH_MESSAGE) {
    std::string message = "Hello";
    std::string ping_result;

    redis.ping([&](auto &&reply) { ping_result = reply.result(); }, message);

    redis.await();
    EXPECT_EQ(ping_result, message);
}

// Test async SELECT
TEST_F(RedisConnectionTest, ASYNC_CONNECTION_COMMANDS_SELECT) {
    bool select_result = false;

    redis.select([&](auto &&reply) { select_result = reply.ok(); }, 1);

    redis.await();
    EXPECT_TRUE(select_result);

    // Switch back to default database
    redis.select([&](auto &&reply) { select_result = reply.ok(); }, 0);

    redis.await();
    EXPECT_TRUE(select_result);
}

// Test async SWAPDB
TEST_F(RedisConnectionTest, ASYNC_CONNECTION_COMMANDS_SWAPDB) {
    bool swap_result = false;

    redis.swapdb([&](auto &&reply) { swap_result = reply.ok(); }, 0, 1);

    redis.await();
    EXPECT_TRUE(swap_result);

    // Swap back
    redis.swapdb([&](auto &&reply) { swap_result = reply.ok(); }, 0, 1);

    redis.await();
    EXPECT_TRUE(swap_result);
}

// Test async QUIT
TEST_F(RedisConnectionTest, DISABLED_ASYNC_CONNECTION_COMMANDS_QUIT) {
    bool quit_result = false;

    redis.quit([&](auto &&reply) { quit_result = reply.ok(); });

    redis.await();
    EXPECT_TRUE(quit_result);
}