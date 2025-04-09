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
#include "../redis.h"

// Redis Configuration
#define REDIS_URI {"tcp://localhost:6379"}

using namespace qb::io;
using namespace std::chrono;
using namespace qb::redis;

// Generates unique key prefixes to avoid collisions between tests
inline std::string
key_prefix(const std::string &key = "") {
    static int  counter = 0;
    std::string prefix  = "qb::redis::scripting-test:" + std::to_string(++counter);

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

// Checks connection and cleans environment before tests
class RedisTest : public ::testing::Test {
protected:
    qb::redis::tcp::client redis{REDIS_URI};

    void
    SetUp() override {
        async::init();
        if (!redis.connect() || !redis.flushall())
            throw std::runtime_error("Unable to connect to Redis");

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

// Test basic EVAL operation
TEST_F(RedisTest, SYNC_SCRIPTING_COMMANDS_EVAL) {
    std::string              key    = test_key("eval");
    std::string              script = "return redis.call('SET', KEYS[1], ARGV[1])";
    std::vector<std::string> keys   = {key};
    std::vector<std::string> args   = {"test_value"};

    // Execute script
    auto result = redis.eval<std::string>(script, keys, args);
    EXPECT_EQ(result, "OK");

    // Verify the key was set
    auto value = redis.get(key);
    EXPECT_TRUE(value.has_value());
    EXPECT_EQ(*value, "test_value");
}

// Test EVAL with multiple keys and arguments
TEST_F(RedisTest, SYNC_SCRIPTING_COMMANDS_EVAL_MULTIPLE) {
    std::string              key1   = test_key("eval1");
    std::string              key2   = test_key("eval2");
    std::string              script = R"(
        redis.call('SET', KEYS[1], ARGV[1])
        redis.call('SET', KEYS[2], ARGV[2])
        return "OK"
    )";
    std::vector<std::string> keys   = {key1, key2};
    std::vector<std::string> args   = {"value1", "value2"};

    // Execute script
    auto result = redis.eval<std::string>(script, keys, args);
    EXPECT_EQ(result, "OK");

    // Verify both keys were set
    auto value1 = redis.get(key1);
    auto value2 = redis.get(key2);
    EXPECT_TRUE(value1.has_value());
    EXPECT_TRUE(value2.has_value());
    EXPECT_EQ(*value1, "value1");
    EXPECT_EQ(*value2, "value2");
}

// Test EVALSHA operation
TEST_F(RedisTest, SYNC_SCRIPTING_COMMANDS_EVALSHA) {
    std::string key    = test_key("evalsha");
    std::string script = "return redis.call('SET', KEYS[1], ARGV[1])";

    // First load the script
    std::string sha = redis.script_load(script);
    EXPECT_FALSE(sha.empty());

    // Execute script using SHA
    std::vector<std::string> keys   = {key};
    std::vector<std::string> args   = {"test_value"};
    auto                     result = redis.evalsha<std::string>(sha, keys, args);
    EXPECT_EQ(result, "OK");

    // Verify the key was set
    auto value = redis.get(key);
    EXPECT_TRUE(value.has_value());
    EXPECT_EQ(*value, "test_value");
}

// Test SCRIPT EXISTS
TEST_F(RedisTest, SYNC_SCRIPTING_COMMANDS_EXISTS) {
    std::string script = "return redis.call('SET', KEYS[1], ARGV[1])";

    // Load a script
    std::string sha = redis.script_load(script);
    EXPECT_FALSE(sha.empty());

    // Check if script exists
    auto exists = redis.script_exists(sha);
    EXPECT_EQ(exists.size(), 1);
    EXPECT_TRUE(exists[0]);

    // Check non-existent script
    exists = redis.script_exists("invalid_sha");
    EXPECT_EQ(exists.size(), 1);
    EXPECT_FALSE(exists[0]);
}

// Test SCRIPT FLUSH
TEST_F(RedisTest, SYNC_SCRIPTING_COMMANDS_FLUSH) {
    std::string script = "return redis.call('SET', KEYS[1], ARGV[1])";

    // Load a script
    std::string sha = redis.script_load(script);
    EXPECT_FALSE(sha.empty());

    // Verify script exists
    auto exists = redis.script_exists(sha);
    EXPECT_EQ(exists.size(), 1);
    EXPECT_TRUE(exists[0]);

    // Flush scripts
    bool flushed = redis.script_flush();
    EXPECT_TRUE(flushed);

    // Verify script no longer exists
    exists = redis.script_exists(sha);
    EXPECT_EQ(exists.size(), 1);
    EXPECT_FALSE(exists[0]);
}

// Test SCRIPT KILL
TEST_F(RedisTest, SYNC_SCRIPTING_COMMANDS_KILL) {
    // Note: This test might fail if no script is currently running
    // It's more of a smoke test to verify the command works
    try {
        bool killed = redis.script_kill();
        (void) killed; // hide warning
    } catch (...) {
    }
    // We don't assert the result as it depends on the Redis server state
}

/*
 * ASYNCHRONOUS TESTS
 */

// Test async EVAL operation
TEST_F(RedisTest, ASYNC_SCRIPTING_COMMANDS_EVAL) {
    std::string              key    = test_key("async_eval");
    std::string              script = "return redis.call('SET', KEYS[1], ARGV[1])";
    std::vector<std::string> keys   = {key};
    std::vector<std::string> args   = {"test_value"};
    bool                     eval_completed = false;

    // Execute script asynchronously
    redis.eval<std::string>(
        [&](auto &&reply) {
            EXPECT_TRUE(reply.ok());
            EXPECT_EQ(reply.result(), "OK");
            eval_completed = true;
        },
        script, std::move(keys), std::move(args));

    redis.await();
    EXPECT_TRUE(eval_completed);

    // Verify the key was set
    auto value = redis.get(key);
    EXPECT_TRUE(value.has_value());
    EXPECT_EQ(*value, "test_value");
}

// Test async EVALSHA operation
TEST_F(RedisTest, ASYNC_SCRIPTING_COMMANDS_EVALSHA) {
    std::string key            = test_key("async_evalsha");
    std::string script         = "return redis.call('SET', KEYS[1], ARGV[1])";
    bool        load_completed = false;
    bool        eval_completed = false;
    std::string script_sha;

    // Load script asynchronously
    redis.script_load(
        [&](auto &&reply) {
            EXPECT_TRUE(reply.ok());
            script_sha     = reply.result();
            load_completed = true;
        },
        script);

    redis.await();
    EXPECT_TRUE(load_completed);
    EXPECT_FALSE(script_sha.empty());

    // Execute script using SHA asynchronously
    std::vector<std::string> keys = {key};
    std::vector<std::string> args = {"test_value"};
    redis.evalsha<std::string>(
        [&](auto &&reply) {
            EXPECT_TRUE(reply.ok());
            EXPECT_EQ(reply.result(), "OK");
            eval_completed = true;
        },
        script_sha, std::move(keys), std::move(args));

    redis.await();
    EXPECT_TRUE(eval_completed);

    // Verify the key was set
    auto value = redis.get(key);
    EXPECT_TRUE(value.has_value());
    EXPECT_EQ(*value, "test_value");
}

// Test async SCRIPT EXISTS
TEST_F(RedisTest, ASYNC_SCRIPTING_COMMANDS_EXISTS) {
    std::string script           = "return redis.call('SET', KEYS[1], ARGV[1])";
    bool        load_completed   = false;
    bool        exists_completed = false;
    std::string script_sha;

    // Load script asynchronously
    redis.script_load(
        [&](auto &&reply) {
            EXPECT_TRUE(reply.ok());
            script_sha     = reply.result();
            load_completed = true;
        },
        script);

    redis.await();
    EXPECT_TRUE(load_completed);
    EXPECT_FALSE(script_sha.empty());

    // Check if script exists asynchronously
    redis.script_exists(
        [&](auto &&reply) {
            EXPECT_TRUE(reply.ok());
            EXPECT_EQ(reply.result().size(), 1);
            EXPECT_TRUE(reply.result()[0]);
            exists_completed = true;
        },
        script_sha);

    redis.await();
    EXPECT_TRUE(exists_completed);
}

// Test async SCRIPT FLUSH
TEST_F(RedisTest, ASYNC_SCRIPTING_COMMANDS_FLUSH) {
    std::string script           = "return redis.call('SET', KEYS[1], ARGV[1])";
    bool        load_completed   = false;
    bool        flush_completed  = false;
    bool        verify_completed = false;
    std::string script_sha;

    // Load script asynchronously
    redis.script_load(
        [&](auto &&reply) {
            EXPECT_TRUE(reply.ok());
            script_sha     = reply.result();
            load_completed = true;
        },
        script);

    redis.await();
    EXPECT_TRUE(load_completed);
    EXPECT_FALSE(script_sha.empty());

    // Flush scripts asynchronously
    redis.script_flush([&](auto &&reply) {
        EXPECT_TRUE(reply.ok());
        flush_completed = true;
    });

    redis.await();
    EXPECT_TRUE(flush_completed);

    // Verify script no longer exists asynchronously
    redis.script_exists(
        [&](auto &&reply) {
            EXPECT_TRUE(reply.ok());
            EXPECT_EQ(reply.result().size(), 1);
            EXPECT_FALSE(reply.result()[0]);
            verify_completed = true;
        },
        script_sha);

    redis.await();
    EXPECT_TRUE(verify_completed);
}

// Test async SCRIPT KILL
TEST_F(RedisTest, ASYNC_SCRIPTING_COMMANDS_KILL) {
    bool kill_completed = false;

    // Kill script asynchronously
    redis.script_kill([&](auto &&reply) {
        // We don't assert the result as it depends on the Redis server state
        kill_completed = true;
    });

    redis.await();
    EXPECT_TRUE(kill_completed);
}

// Test command chaining
TEST_F(RedisTest, ASYNC_SCRIPTING_COMMANDS_CHAINING) {
    std::string key                    = test_key("scripting_chaining");
    std::string script                 = "return redis.call('SET', KEYS[1], ARGV[1])";
    bool        all_commands_completed = false;
    int         command_count          = 0;

    // Setup callback to track completion
    auto completion_callback = [&command_count, &all_commands_completed](auto &&) {
        if (++command_count == 3) {
            all_commands_completed = true;
        }
    };

    // Chain multiple commands
    redis.script_load(
        [&](auto &&reply) {
            EXPECT_TRUE(reply.ok());
            completion_callback(reply);
        },
        script);

    redis.script_exists(
        [&](auto &&reply) {
            EXPECT_TRUE(reply.ok());
            completion_callback(reply);
        },
        "some_sha");

    redis.script_flush([&](auto &&reply) {
        EXPECT_TRUE(reply.ok());
        completion_callback(reply);
    });

    // Trigger the async operations and wait for completion
    redis.await();
    EXPECT_TRUE(all_commands_completed);
}

// Test async SCRIPT LOAD
TEST_F(RedisTest, ASYNC_SCRIPTING_COMMANDS_LOAD) {
    std::string script         = "return redis.call('SET', KEYS[1], ARGV[1])";
    bool        load_completed = false;
    std::string script_sha;

    // Load script asynchronously
    redis.script_load(
        [&](auto &&reply) {
            EXPECT_TRUE(reply.ok());
            script_sha     = reply.result();
            load_completed = true;
        },
        script);

    redis.await();
    EXPECT_TRUE(load_completed);
    EXPECT_FALSE(script_sha.empty());

    // Verify the script exists
    auto exists = redis.script_exists(script_sha);
    EXPECT_EQ(exists.size(), 1);
    EXPECT_TRUE(exists[0]);
}

// Test script with complex operations
TEST_F(RedisTest, SYNC_SCRIPTING_COMMANDS_COMPLEX) {
    std::string key1   = test_key("complex1");
    std::string key2   = test_key("complex2");
    std::string script = R"(
        local val1 = redis.call('GET', KEYS[1])
        local val2 = redis.call('GET', KEYS[2])
        if val1 == nil then val1 = '0' end
        if val2 == nil then val2 = '0' end
        local sum = tonumber(val1) + tonumber(val2)
        redis.call('SET', KEYS[1], tostring(sum))
        redis.call('SET', KEYS[2], tostring(sum))
        return sum
    )";

    // Set initial values
    redis.set(key1, "10");
    redis.set(key2, "20");

    // Execute complex script
    std::vector<std::string> keys   = {key1, key2};
    auto                     result = redis.eval<long long>(script, keys);
    EXPECT_EQ(result, 30);

    // Verify both keys were updated
    auto value1 = redis.get(key1);
    auto value2 = redis.get(key2);
    EXPECT_TRUE(value1.has_value());
    EXPECT_TRUE(value2.has_value());
    EXPECT_EQ(*value1, "30");
    EXPECT_EQ(*value2, "30");
}

// Test script with error handling
TEST_F(RedisTest, SYNC_SCRIPTING_COMMANDS_ERROR) {
    std::string              key    = test_key("error");
    std::string              script = "error('This is a test error')";
    std::vector<std::string> keys   = {key};

    // Execute script that should throw an error
    EXPECT_THROW(redis.eval<std::string>(script, keys), std::runtime_error);
}

// Test script with multiple return types
TEST_F(RedisTest, SYNC_SCRIPTING_COMMANDS_MULTIPLE_TYPES) {
    std::string key = test_key("types");

    // Premier script qui définit une clé et renvoie OK
    std::string set_script = R"(
        return redis.call('SET', KEYS[1], ARGV[1])
    )";

    std::vector<std::string> keys = {key};
    std::vector<std::string> args = {"test_value"};

    // Execute script and verify it returns OK
    auto set_result = redis.eval<std::string>(set_script, keys, args);
    EXPECT_EQ(set_result, "OK");

    // Verify the key was set
    auto value = redis.get(key);
    EXPECT_TRUE(value.has_value());
    EXPECT_EQ(*value, "test_value");

    // Second script that returns a number
    std::string number_script = "return 42";
    auto        number_result = redis.eval<long long>(number_script);
    EXPECT_EQ(number_result, 42);

    // Third script that returns a boolean
    std::string bool_script = "return true";
    auto        bool_result = redis.eval<bool>(bool_script);
    EXPECT_TRUE(bool_result);

    // Fourth script that returns an array
    std::string array_script = "return {1, 2, 3}";
    auto        array_result = redis.eval<std::vector<long long>>(array_script);
    EXPECT_EQ(array_result.size(), 3);
    EXPECT_EQ(array_result[0], 1);
    EXPECT_EQ(array_result[1], 2);
    EXPECT_EQ(array_result[2], 3);
}

// Test script with atomic operations
TEST_F(RedisTest, SYNC_SCRIPTING_COMMANDS_ATOMIC) {
    std::string key1   = test_key("atomic1");
    std::string key2   = test_key("atomic2");
    std::string script = R"(
        local val1 = redis.call('GET', KEYS[1])
        local val2 = redis.call('GET', KEYS[2])
        if val1 == nil or val2 == nil then
            return false
        end
        redis.call('SET', KEYS[1], val2)
        redis.call('SET', KEYS[2], val1)
        return true
    )";

    // Set initial values
    redis.set(key1, "value1");
    redis.set(key2, "value2");

    // Execute atomic swap script
    std::vector<std::string> keys   = {key1, key2};
    auto                     result = redis.eval<bool>(script, keys);
    EXPECT_TRUE(result);

    // Verify values were swapped
    auto value1 = redis.get(key1);
    auto value2 = redis.get(key2);
    EXPECT_TRUE(value1.has_value());
    EXPECT_TRUE(value2.has_value());
    EXPECT_EQ(*value1, "value2");
    EXPECT_EQ(*value2, "value1");
}

// Test script with conditional operations
TEST_F(RedisTest, SYNC_SCRIPTING_COMMANDS_CONDITIONAL) {
    std::string key    = test_key("conditional");
    std::string script = R"(
        local val = redis.call('GET', KEYS[1])
        if val == ARGV[1] then
            redis.call('SET', KEYS[1], ARGV[2])
            return 1
        end
        return 0
    )";

    // Set initial value
    redis.set(key, "initial");

    // Test matching condition
    std::vector<std::string> keys   = {key};
    std::vector<std::string> args   = {"initial", "updated"};
    auto                     result = redis.eval<bool>(script, keys, args);
    EXPECT_TRUE(result);

    // Verify value was updated
    auto value = redis.get(key);
    EXPECT_TRUE(value.has_value());
    EXPECT_EQ(*value, "updated");

    // Test non-matching condition
    args   = {"wrong", "not_updated"};
    result = redis.eval<bool>(script, keys, args);
    EXPECT_FALSE(result);

    // Verify value wasn't changed
    value = redis.get(key);
    EXPECT_TRUE(value.has_value());
    EXPECT_EQ(*value, "updated");
}

// Main function to run the tests
int
main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}