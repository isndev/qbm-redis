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

class TransactionProtocolModesTest : public ProtocolModesTestBase {};

INSTANTIATE_PROTOCOL_MODES(TransactionProtocolModesTest);

/*
 * COROUTINE TESTS
 */

// Test MULTI/EXEC with coroutines
TEST_P(TransactionProtocolModesTest, CORO_TRANSACTION_COMMANDS_MULTI_EXEC) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key1 = protocol_key("coro_multi_exec1");
        std::string key2 = protocol_key("coro_multi_exec2");

        // Start a transaction
        auto multi_reply = co_await redis.multi();
        EXPECT_TRUE(multi_reply.ok());
        EXPECT_TRUE(redis.is_in_multi());

        // Add commands to the transaction (using callback version inside transaction)
        (void)co_await redis.set(key1, "value1");
        (void)co_await redis.set(key2, "value2");

        // Execute the transaction
        auto exec_reply = co_await redis.exec<std::string>();
        EXPECT_TRUE(exec_reply.ok());
        auto results = exec_reply.result();
        EXPECT_EQ(results.size(), 2);
        EXPECT_EQ(results[0], "OK");
        EXPECT_EQ(results[1], "OK");
        EXPECT_FALSE(redis.is_in_multi());

        // Check the results
        auto value1_reply = co_await redis.get(key1);
        auto value2_reply = co_await redis.get(key2);
        EXPECT_TRUE(value1_reply.ok());
        EXPECT_TRUE(value2_reply.ok());
        EXPECT_TRUE(value1_reply.result().has_value());
        EXPECT_TRUE(value2_reply.result().has_value());
        EXPECT_EQ(*value1_reply.result(), "value1");
        EXPECT_EQ(*value2_reply.result(), "value2");

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// Test DISCARD with coroutines
TEST_P(TransactionProtocolModesTest, CORO_TRANSACTION_COMMANDS_DISCARD) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("coro_discard");

        // Start a transaction
        auto multi_reply = co_await redis.multi();
        EXPECT_TRUE(multi_reply.ok());
        EXPECT_TRUE(redis.is_in_multi());

        // Add a command to the transaction
        (void)co_await redis.set(key, "value");

        // Discard the transaction
        auto discard_reply = co_await redis.discard();
        EXPECT_TRUE(discard_reply.ok());
        EXPECT_FALSE(redis.is_in_multi());

        // Check that the command was not executed
        auto value_reply = co_await redis.get(key);
        EXPECT_TRUE(value_reply.ok());
        EXPECT_FALSE(value_reply.result().has_value());

        // Verify we can start a new transaction
        auto multi2_reply = co_await redis.multi();
        EXPECT_TRUE(multi2_reply.ok());
        EXPECT_TRUE(redis.is_in_multi());

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// Test WATCH/UNWATCH with coroutines
TEST_P(TransactionProtocolModesTest, CORO_TRANSACTION_COMMANDS_WATCH_UNWATCH) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("coro_watch");

        // Watch the key
        auto watch_reply = co_await redis.watch(key);
        EXPECT_TRUE(watch_reply.ok());

        // Modify the key in another client
        qb::redis::tcp::client other_client{REDIS_URI_PROTOCOL};
        co_await other_client.connect();
        bool set_ok = true;
        other_client.set([&set_ok](auto &&r) { set_ok = r.ok(); }, key, "modified");
        other_client.await();
        EXPECT_TRUE(set_ok);

        // Start a transaction
        auto multi_reply = co_await redis.multi();
        EXPECT_TRUE(multi_reply.ok());
        
        (void)co_await redis.set(key, "new_value");

        // Execute the transaction - should fail because key was modified
        // Redis returns nil when EXEC is aborted due to WATCH → ok() == false
        auto exec_reply = co_await redis.exec<std::string>();
        EXPECT_FALSE(exec_reply.ok());

        // Check that the value hasn't changed
        auto value_reply = co_await redis.get(key);
        EXPECT_TRUE(value_reply.ok());
        EXPECT_TRUE(value_reply.result().has_value());
        EXPECT_EQ(*value_reply.result(), "modified");

        // Stop watching
        auto unwatch_reply = co_await redis.unwatch();
        EXPECT_TRUE(unwatch_reply.ok());

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// Test WATCH with multiple keys using coroutines
TEST_P(TransactionProtocolModesTest, CORO_TRANSACTION_COMMANDS_WATCH_MULTIPLE) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key1 = protocol_key("coro_watch1");
        std::string key2 = protocol_key("coro_watch2");

        // Set initial values
        (void)co_await redis.set(key1, "initial1");
        (void)co_await redis.set(key2, "initial2");

        // Watch both keys
        auto watch_reply = co_await redis.watch({key1, key2});
        EXPECT_TRUE(watch_reply.ok());

        // Modify one of the keys in another client
        qb::redis::tcp::client other_client{REDIS_URI_PROTOCOL};
        co_await other_client.connect();
        bool set_ok = true;
        other_client.set([&set_ok](auto &&r) { set_ok = r.ok(); }, key1, "modified1");
        other_client.await();
        EXPECT_TRUE(set_ok);

        // Start a transaction
        auto multi_reply = co_await redis.multi();
        EXPECT_TRUE(multi_reply.ok());
        
        (void)co_await redis.set(key1, "new_value1");
        (void)co_await redis.set(key2, "new_value2");

        // Execute the transaction - should fail because key was modified
        // Redis returns nil when EXEC is aborted due to WATCH → ok() == false
        auto exec_reply = co_await redis.exec<std::string>();
        EXPECT_FALSE(exec_reply.ok());

        // Check that the values haven't changed
        auto value1_reply = co_await redis.get(key1);
        auto value2_reply = co_await redis.get(key2);
        EXPECT_TRUE(value1_reply.ok());
        EXPECT_TRUE(value2_reply.ok());
        EXPECT_TRUE(value1_reply.result().has_value());
        EXPECT_TRUE(value2_reply.result().has_value());
        EXPECT_EQ(*value1_reply.result(), "modified1");
        EXPECT_EQ(*value2_reply.result(), "initial2");

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// Test successful transaction after WATCH with no modifications
TEST_P(TransactionProtocolModesTest, CORO_TRANSACTION_COMMANDS_WATCH_SUCCESS) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key1 = protocol_key("coro_watch_success1");
        std::string key2 = protocol_key("coro_watch_success2");

        // Set initial values
        (void)co_await redis.set(key1, "initial1");
        (void)co_await redis.set(key2, "initial2");

        // Watch both keys
        auto watch_reply = co_await redis.watch({key1, key2});
        EXPECT_TRUE(watch_reply.ok());

        // Start a transaction (no modifications from other clients)
        auto multi_reply = co_await redis.multi();
        EXPECT_TRUE(multi_reply.ok());
        
        (void)co_await redis.set(key1, "updated1");
        (void)co_await redis.set(key2, "updated2");

        // Execute the transaction - should succeed
        auto exec_reply = co_await redis.exec<std::string>();
        EXPECT_TRUE(exec_reply.ok());
        auto results = exec_reply.result();
        EXPECT_EQ(results.size(), 2);
        EXPECT_EQ(results[0], "OK");
        EXPECT_EQ(results[1], "OK");

        // Check that the values were updated
        auto value1_reply = co_await redis.get(key1);
        auto value2_reply = co_await redis.get(key2);
        EXPECT_TRUE(value1_reply.ok());
        EXPECT_TRUE(value2_reply.ok());
        EXPECT_TRUE(value1_reply.result().has_value());
        EXPECT_TRUE(value2_reply.result().has_value());
        EXPECT_EQ(*value1_reply.result(), "updated1");
        EXPECT_EQ(*value2_reply.result(), "updated2");

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// Test WATCH with empty key returns failed Reply (validation)
TEST_P(TransactionProtocolModesTest, CORO_TRANSACTION_COMMANDS_WATCH_EMPTY_KEY) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);

        auto watch_reply = co_await redis.watch("");
        EXPECT_FALSE(watch_reply.ok());
        EXPECT_FALSE(watch_reply.error().empty());
        EXPECT_TRUE(watch_reply.error().find("empty") != std::string::npos);

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// Test WATCH with empty key list returns failed Reply (validation)
TEST_P(TransactionProtocolModesTest, CORO_TRANSACTION_COMMANDS_WATCH_EMPTY_KEYS) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);

        auto watch_reply = co_await redis.watch(std::vector<std::string>{});
        EXPECT_FALSE(watch_reply.ok());
        EXPECT_FALSE(watch_reply.error().empty());
        EXPECT_TRUE(watch_reply.error().find("empty") != std::string::npos);

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// Test UNWATCH followed by transaction
TEST_P(TransactionProtocolModesTest, CORO_TRANSACTION_COMMANDS_UNWATCH_THEN_EXEC) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("coro_unwatch_exec");

        // Set initial value
        (void)co_await redis.set(key, "initial");

        // Watch the key
        auto watch_reply = co_await redis.watch(key);
        EXPECT_TRUE(watch_reply.ok());

        // Unwatch before transaction
        auto unwatch_reply = co_await redis.unwatch();
        EXPECT_TRUE(unwatch_reply.ok());

        // Modify the key in another client - should not affect transaction since we unwatched
        qb::redis::tcp::client other_client{REDIS_URI_PROTOCOL};
        co_await other_client.connect();
        bool set_ok = true;
        other_client.set([&set_ok](auto &&r) { set_ok = r.ok(); }, key, "modified");
        other_client.await();
        EXPECT_TRUE(set_ok);

        // Start a transaction
        auto multi_reply = co_await redis.multi();
        EXPECT_TRUE(multi_reply.ok());
        
        (void)co_await redis.set(key, "new_value");

        // Execute the transaction - should succeed because we unwatched
        auto exec_reply = co_await redis.exec<std::string>();
        EXPECT_TRUE(exec_reply.ok());
        auto results = exec_reply.result();
        EXPECT_EQ(results.size(), 1);
        EXPECT_EQ(results[0], "OK");

        // Check that the value was updated by our transaction
        auto value_reply = co_await redis.get(key);
        EXPECT_TRUE(value_reply.ok());
        EXPECT_TRUE(value_reply.result().has_value());
        EXPECT_EQ(*value_reply.result(), "new_value");

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

TEST_P(TransactionProtocolModesTest, MULTI_EXEC) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k = protocol_key("tx");
        auto multi_r = co_await redis.multi();
        EXPECT_TRUE(multi_r.ok()) << multi_r.error();
        (void)co_await redis.set(k, "x");
        (void)co_await redis.set(std::string(k) + ":2", "y");
        auto exec_r = co_await redis.exec<std::string>();
        EXPECT_TRUE(exec_r.ok()) << exec_r.error();
        if (exec_r.ok()) {
            const auto& results = exec_r.result();
            EXPECT_EQ(results.size(), 2u);
            EXPECT_EQ(results[0], "OK");
            EXPECT_EQ(results[1], "OK");
        }
        done = true;
    });
    run_coro_test_until(done);
}

// Main function to run the tests
int
main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
