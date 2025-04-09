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

// Configuration Redis
#define REDIS_URI {"tcp://localhost:6379"}

using namespace qb::io;
using namespace std::chrono;

// Generates unique key prefixes to avoid collisions between tests
inline std::string
key_prefix(const std::string &key = "") {
    static int  counter = 0;
    std::string prefix  = "qb::redis::transaction-test:" + std::to_string(++counter);

    if (key.empty()) {
        return prefix;
    }

    return prefix + ":" + key;
}

// Generates a test key
inline std::string
test_key(const std::string &k) {
    return "{" + key_prefix() + "}::" + k;
}

// Verifies connection and cleans environment before tests
class RedisTest : public ::testing::Test {
protected:
    qb::redis::tcp::client redis{REDIS_URI};

    void
    SetUp() override {
        async::init();
        if (!redis.connect() || !redis.flushall())
            throw std::runtime_error("Impossible de se connecter à Redis");

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
 * TESTS SYNCHRONES
 */

// Test MULTI/EXEC
TEST_F(RedisTest, SYNC_TRANSACTION_COMMANDS_MULTI_EXEC) {
    std::string key1 = test_key("multi_exec1");
    std::string key2 = test_key("multi_exec2");

    // Start a transaction
    EXPECT_TRUE(redis.multi());
    EXPECT_TRUE(redis.is_in_multi());

    // Add commands to the transaction
    redis.set(key1, "value1");
    redis.set(key2, "value2");

    // Execute the transaction
    auto results = redis.exec<std::string>();
    EXPECT_EQ(results.size(), 2);
    EXPECT_EQ(results[0], "OK");
    EXPECT_EQ(results[1], "OK");
    EXPECT_FALSE(redis.is_in_multi());

    // Check the results
    auto value1 = redis.get(key1);
    auto value2 = redis.get(key2);
    EXPECT_TRUE(value1.has_value());
    EXPECT_TRUE(value2.has_value());
    EXPECT_EQ(*value1, "value1");
    EXPECT_EQ(*value2, "value2");
}

// Test DISCARD
TEST_F(RedisTest, SYNC_TRANSACTION_COMMANDS_DISCARD) {
    std::string key = test_key("discard");

    // Start a transaction
    EXPECT_TRUE(redis.multi());
    EXPECT_TRUE(redis.is_in_multi());

    // Add a command to the transaction
    redis.set(key, "value");

    // Check that the command is executed
    EXPECT_TRUE(redis.get(key).has_value());

    // Discard the transaction
    EXPECT_TRUE(redis.discard());
    EXPECT_FALSE(redis.is_in_multi());

    // Check that the command was not executed after discard
    EXPECT_FALSE(redis.get(key).has_value());

    // Verify we can start a new transaction
    EXPECT_TRUE(redis.multi());
    EXPECT_TRUE(redis.is_in_multi());
}

// Test WATCH/UNWATCH
TEST_F(RedisTest, SYNC_TRANSACTION_COMMANDS_WATCH_UNWATCH) {
    std::string key = test_key("watch");

    // Watch the key
    EXPECT_TRUE(redis.watch(key));

    // Modify the key in another client
    qb::redis::tcp::client other_client{REDIS_URI};
    other_client.connect();
    other_client.set(key, "modified");
    other_client.await();

    // Start a transaction
    EXPECT_TRUE(redis.multi());
    redis.set(key, "new_value");

    std::vector<std::string> results;
    // The transaction should fail because the key was modified
    EXPECT_THROW((results = redis.exec<std::string>()), std::runtime_error);
    EXPECT_TRUE(results.empty());

    // Check that the value hasn't changed
    auto value = redis.get(key);
    EXPECT_TRUE(value.has_value());
    EXPECT_EQ(*value, "modified");

    // Stop watching
    EXPECT_TRUE(redis.unwatch());
}

// Test WATCH with multiple keys
TEST_F(RedisTest, SYNC_TRANSACTION_COMMANDS_WATCH_MULTIPLE) {
    std::string key1 = test_key("watch1");
    std::string key2 = test_key("watch2");

    // Set initial values
    redis.set(key1, "initial1");
    redis.set(key2, "initial2");

    // Watch both keys
    EXPECT_TRUE(redis.watch({key1, key2}));

    // Modify one of the keys in another client
    qb::redis::tcp::client other_client{REDIS_URI};
    other_client.connect();
    other_client.set(key1, "modified1");
    other_client.await();

    // Start a transaction
    EXPECT_TRUE(redis.multi());
    redis.set(key1, "new_value1");
    redis.set(key2, "new_value2");

    std::vector<std::string> results;
    // The transaction should fail because a key was modified
    EXPECT_THROW((results = redis.exec<std::string>()), std::runtime_error);
    EXPECT_TRUE(results.empty());

    // Check that the values haven't changed
    auto value1 = redis.get(key1);
    auto value2 = redis.get(key2);
    EXPECT_TRUE(value1.has_value());
    EXPECT_TRUE(value2.has_value());
    EXPECT_EQ(*value1, "modified1");
    EXPECT_EQ(*value2, "initial2");
}

/*
 * TESTS ASYNCHRONES
 */

// Test asynchrone MULTI/EXEC
TEST_F(RedisTest, ASYNC_TRANSACTION_COMMANDS_MULTI_EXEC) {
    std::string              key1          = test_key("async_multi_exec1");
    std::string              key2          = test_key("async_multi_exec2");
    bool                     multi_success = false;
    std::vector<std::string> exec_results;

    // Start a transaction asynchronously
    redis.multi([&](auto &&reply) { multi_success = reply.ok(); });

    redis.await();
    EXPECT_TRUE(multi_success);
    EXPECT_TRUE(redis.is_in_multi());

    // Add commands to the transaction
    redis.set(key1, "value1");
    redis.set(key2, "value2");

    // Execute the transaction asynchronously
    redis.exec<std::string>([&](auto &&reply) { exec_results = reply.result(); });

    redis.await();
    EXPECT_EQ(exec_results.size(), 2);
    EXPECT_EQ(exec_results[0], "OK");
    EXPECT_EQ(exec_results[1], "OK");
    EXPECT_FALSE(redis.is_in_multi());

    // Check the results
    std::optional<std::string> value1, value2;
    redis.get([&](auto &&reply) { value1 = reply.result(); }, key1);

    redis.get([&](auto &&reply) { value2 = reply.result(); }, key2);

    redis.await();
    EXPECT_TRUE(value1.has_value());
    EXPECT_TRUE(value2.has_value());
    EXPECT_EQ(*value1, "value1");
    EXPECT_EQ(*value2, "value2");
}

// Test asynchrone DISCARD
TEST_F(RedisTest, ASYNC_TRANSACTION_COMMANDS_DISCARD) {
    std::string key             = test_key("async_discard");
    bool        multi_success   = false;
    bool        discard_success = false;

    // Start a transaction asynchronously
    redis.multi([&](auto &&reply) { multi_success = reply.ok(); });

    redis.await();
    EXPECT_TRUE(multi_success);
    EXPECT_TRUE(redis.is_in_multi());

    // Add a command to the transaction
    redis.set(key, "value");

    // Cancel the transaction asynchronously
    redis.discard([&](auto &&reply) { discard_success = reply.ok(); });

    redis.await();
    EXPECT_TRUE(discard_success);
    EXPECT_FALSE(redis.is_in_multi());

    // Check that the command was not executed
    std::optional<std::string> value;
    redis.get([&](auto &&reply) { value = reply.result(); }, key);

    redis.await();
    EXPECT_FALSE(value.has_value());
}

// Test asynchrone WATCH/UNWATCH
TEST_F(RedisTest, ASYNC_TRANSACTION_COMMANDS_WATCH_UNWATCH) {
    std::string              key           = test_key("async_watch");
    bool                     watch_success = false;
    bool                     multi_success = false;
    std::vector<std::string> exec_results;

    // Watch the key asynchronously
    redis.watch([&](auto &&reply) { watch_success = reply.ok(); }, key);

    redis.await();
    EXPECT_TRUE(watch_success);

    // Modify the key in another client
    qb::redis::tcp::client other_client{REDIS_URI};
    other_client.connect();
    other_client.set(key, "modified");
    other_client.await();

    // Start a transaction asynchronously
    redis.multi([&](auto &&reply) { multi_success = reply.ok(); });

    redis.await();
    EXPECT_TRUE(multi_success);

    redis.set(key, "new_value");

    // Execute the transaction asynchronously
    redis.exec<std::string>([&](auto &&reply) { exec_results = reply.result(); });

    redis.await();
    EXPECT_TRUE(exec_results.empty());

    // Check that the value hasn't changed
    std::optional<std::string> value;
    redis.get([&](auto &&reply) { value = reply.result(); }, key);

    redis.await();
    EXPECT_TRUE(value.has_value());
    EXPECT_EQ(*value, "modified");

    // Arrêter la surveillance de manière asynchrone
    bool unwatch_success = false;
    redis.unwatch([&](auto &&reply) { unwatch_success = reply.ok(); });

    redis.await();
    EXPECT_TRUE(unwatch_success);
}

// Test asynchrone WATCH avec plusieurs clés
TEST_F(RedisTest, ASYNC_TRANSACTION_COMMANDS_WATCH_MULTIPLE) {
    std::string              key1          = test_key("async_watch1");
    std::string              key2          = test_key("async_watch2");
    bool                     watch_success = false;
    bool                     multi_success = false;
    std::vector<std::string> exec_results;

    // Mettre des valeurs initiales
    redis.set(key1, "initial1");
    redis.set(key2, "initial2");

    // Surveiller les deux clés de manière asynchrone
    redis.watch([&](auto &&reply) { watch_success = reply.ok(); }, {key1, key2});

    redis.await();
    EXPECT_TRUE(watch_success);

    // Modifier une des clés dans un autre client
    qb::redis::tcp::client other_client{REDIS_URI};
    other_client.connect();
    other_client.set(key1, "modified1");
    other_client.await();

    // Démarrer une transaction de manière asynchrone
    redis.multi([&](auto &&reply) { multi_success = reply.ok(); });

    redis.await();
    EXPECT_TRUE(multi_success);

    redis.set(key1, "new_value1");
    redis.set(key2, "new_value2");

    // Exécuter la transaction de manière asynchrone
    redis.exec<std::string>([&](auto &&reply) { exec_results = reply.result(); });

    redis.await();
    EXPECT_TRUE(exec_results.empty());

    // Vérifier que les valeurs n'ont pas changé
    std::optional<std::string> value1, value2;
    redis.get([&](auto &&reply) { value1 = reply.result(); }, key1);

    redis.get([&](auto &&reply) { value2 = reply.result(); }, key2);

    redis.await();
    EXPECT_TRUE(value1.has_value());
    EXPECT_TRUE(value2.has_value());
    EXPECT_EQ(*value1, "modified1");
    EXPECT_EQ(*value2, "initial2");
}