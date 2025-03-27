/*
 * qb - C++ Actor Framework
 * Copyright (C) 2011-2021 isndev (www.qbaf.io). All rights reserved.
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
#define REDIS_URI \
    { "tcp://localhost:6379" }

using namespace qb::io;
using namespace std::chrono;

// Generates unique key prefixes to avoid collisions between tests
inline std::string
key_prefix(const std::string &key = "") {
    static int counter = 0;
    std::string prefix = "qb::redis::hash-test:" + std::to_string(++counter);
    
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
    
    void SetUp() override {
        async::init();
        if (!redis.connect() || !redis.flushall())
            throw std::runtime_error("Unable to connect to Redis");
        
        // Wait for connection to be established
        redis.await();
        TearDown();
    }
    
    void TearDown() override {
        // Cleanup after tests
        redis.flushall();
        redis.await();
    }
};

/*
 * SYNCHRONOUS TESTS
 */

// Test basic HSET and HGET operations
TEST_F(RedisTest, SYNC_HASH_COMMANDS_HSET_HGET) {
    // Simple HSET test
    std::string key = test_key("basic");
    
    EXPECT_EQ(redis.hset(key, "field1", "value1"), 1);
    EXPECT_EQ(redis.hset(key, "field2", "value2"), 1);
    
    // HGET test
    auto result1 = redis.hget(key, "field1");
    auto result2 = redis.hget(key, "field2");
    auto result3 = redis.hget(key, "field3");
    
    EXPECT_TRUE(result1.has_value());
    EXPECT_EQ(*result1, "value1");
    EXPECT_TRUE(result2.has_value());
    EXPECT_EQ(*result2, "value2");
    EXPECT_FALSE(result3.has_value());
    
    // HEXISTS test
    EXPECT_TRUE(redis.hexists(key, "field1"));
    EXPECT_FALSE(redis.hexists(key, "field3"));
    
    // HLEN test
    EXPECT_EQ(redis.hlen(key), 2);
    
    // HDEL test
    EXPECT_EQ(redis.hdel(key, "field1"), 1);
    EXPECT_FALSE(redis.hget(key, "field1").has_value());
    EXPECT_EQ(redis.hlen(key), 1);
    
    // Cleanup
    redis.del(key);
}

// Test HMSET and HMGET operations
TEST_F(RedisTest, SYNC_HASH_COMMANDS_HMSET_HMGET) {
    std::string key = test_key("hmset");
    
    // Test HMSET
    EXPECT_TRUE(redis.hmset(key, "field1", "value1", "field2", "value2", "field3", "value3"));
    
    // Test HMGET
    auto values = redis.hmget(key, "field1", "field2", "field3", "field4");
    EXPECT_EQ(values.size(), 4);
    EXPECT_TRUE(values[0].has_value());
    EXPECT_EQ(*values[0], "value1");
    EXPECT_TRUE(values[1].has_value());
    EXPECT_EQ(*values[1], "value2");
    EXPECT_TRUE(values[2].has_value());
    EXPECT_EQ(*values[2], "value3");
    EXPECT_FALSE(values[3].has_value());
    
    // Test HGETALL
    auto all_values = redis.hgetall(key);
    EXPECT_EQ(all_values.size(), 3);
    EXPECT_EQ(all_values["field1"], "value1");
    EXPECT_EQ(all_values["field2"], "value2");
    EXPECT_EQ(all_values["field3"], "value3");
    
    // Cleanup
    redis.del(key);
}

// Test increment operations
TEST_F(RedisTest, SYNC_HASH_COMMANDS_INCR) {
    std::string key = test_key("incr");
    
    // Test HINCRBY
    EXPECT_EQ(redis.hincrby(key, "counter", 1), 1);
    EXPECT_EQ(redis.hincrby(key, "counter", 10), 11);
    EXPECT_EQ(redis.hincrby(key, "counter", -5), 6);
    
    // Test HINCRBYFLOAT
    EXPECT_FLOAT_EQ(redis.hincrbyfloat(key, "float", 10.5), 10.5);
    EXPECT_FLOAT_EQ(redis.hincrbyfloat(key, "float", 0.5), 11.0);
    EXPECT_FLOAT_EQ(redis.hincrbyfloat(key, "float", -1.5), 9.5);
    
    // Verify values
    EXPECT_EQ(*redis.hget(key, "counter"), "6");
    
    auto float_val = redis.hget(key, "float");
    EXPECT_TRUE(float_val.has_value());
    EXPECT_EQ(*float_val, "9.5");
    
    // Cleanup
    redis.del(key);
}

// Test HSETNX operation
TEST_F(RedisTest, SYNC_HASH_COMMANDS_HSETNX) {
    std::string key = test_key("hsetnx");
    
    // Test HSETNX on new field
    EXPECT_TRUE(redis.hsetnx(key, "field1", "value1"));
    EXPECT_EQ(*redis.hget(key, "field1"), "value1");
    
    // Test HSETNX on existing field
    EXPECT_FALSE(redis.hsetnx(key, "field1", "new-value"));
    EXPECT_EQ(*redis.hget(key, "field1"), "value1"); // Value should not change
    
    // Cleanup
    redis.del(key);
}

// Test keys and values operations
TEST_F(RedisTest, SYNC_HASH_COMMANDS_KEYS_VALUES) {
    std::string key = test_key("keys-values");
    
    // Setup hash
    redis.hmset(key, "field1", "value1", "field2", "value2", "field3", "value3");
    
    // Test HKEYS
    auto keys = redis.hkeys(key);
    EXPECT_EQ(keys.size(), 3);
    EXPECT_TRUE(std::find(keys.begin(), keys.end(), "field1") != keys.end());
    EXPECT_TRUE(std::find(keys.begin(), keys.end(), "field2") != keys.end());
    EXPECT_TRUE(std::find(keys.begin(), keys.end(), "field3") != keys.end());
    
    // Test HVALS
    auto values = redis.hvals(key);
    EXPECT_EQ(values.size(), 3);
    EXPECT_TRUE(std::find(values.begin(), values.end(), "value1") != values.end());
    EXPECT_TRUE(std::find(values.begin(), values.end(), "value2") != values.end());
    EXPECT_TRUE(std::find(values.begin(), values.end(), "value3") != values.end());
    
    // Test HSTRLEN
    EXPECT_EQ(redis.hstrlen(key, "field1"), 6); // Length of "value1"
    EXPECT_EQ(redis.hstrlen(key, "nonexistent"), 0);
    
    // Cleanup
    redis.del(key);
}

// Test HSCAN operation
TEST_F(RedisTest, SYNC_HASH_COMMANDS_HSCAN) {
    std::string key = test_key("hscan");
    
    // Setup large hash
    for (int i = 0; i < 20; ++i) {
        redis.hset(key, "field:" + std::to_string(i), "value:" + std::to_string(i));
    }
    
    // Test HSCAN with pattern
    auto scan_result = redis.hscan(key, 0, "field:1*");
    EXPECT_TRUE(scan_result.items.size() > 0); // Should find at least "field:1", "field:10"-"field:19"
    
    // Count all fields with cursor-based iteration
    std::size_t cursor = 0;
    std::size_t total_fields = 0;
    do {
        auto result = redis.hscan(key, cursor);
        cursor = result.cursor;
        total_fields += result.items.size() / 2; // Each field-value pair is two items
    } while (cursor != 0);
    
    EXPECT_EQ(total_fields, 20);
    
    // Cleanup
    redis.del(key);
}

/*
 * ASYNCHRONOUS TESTS
 */

// Test basic HSET and HGET operations asynchronously
TEST_F(RedisTest, ASYNC_HASH_COMMANDS_HSET_HGET) {
    std::string key = test_key("async-basic");
    bool hset_called = false;
    bool hget_called = false;
    bool hexists_called = false;
    bool hdel_called = false;
    
    // Test HSET async
    redis.hset(
        [&hset_called](auto&& reply) {
            EXPECT_TRUE(reply.ok);
            EXPECT_EQ(reply.result, 1); // Created a new field
            hset_called = true;
        },
        key, 
        "field1", 
        "value1"
    );
    
    redis.await();
    EXPECT_TRUE(hset_called);
    
    // Test HGET async
    redis.hget(
        [&hget_called](auto&& reply) {
            EXPECT_TRUE(reply.ok);
            EXPECT_TRUE(reply.result.has_value());
            EXPECT_EQ(*reply.result, "value1");
            hget_called = true;
        },
        key,
        "field1"
    );
    
    redis.await();
    EXPECT_TRUE(hget_called);
    
    // Test HEXISTS async
    redis.hexists(
        [&hexists_called](auto&& reply) {
            EXPECT_TRUE(reply.ok);
            hexists_called = true;
        },
        key,
        "field1"
    );
    
    redis.await();
    EXPECT_TRUE(hexists_called);
    
    // Test HDEL async
    redis.hdel(
        [&hdel_called](auto&& reply) {
            EXPECT_TRUE(reply.ok);
            EXPECT_EQ(reply.result, 1);
            hdel_called = true;
        },
        key,
        "field1"
    );
    
    redis.await();
    EXPECT_TRUE(hdel_called);
    
    // Cleanup
    redis.del(key);
}

// Test HMSET and HMGET operations asynchronously
TEST_F(RedisTest, ASYNC_HASH_COMMANDS_HMSET_HMGET) {
    std::string key = test_key("async-hmset");
    bool hmset_called = false;
    bool hmget_called = false;
    bool hgetall_called = false;
    
    // Test HMSET async
    redis.hmset(
        [&hmset_called](auto&& reply) {
            EXPECT_TRUE(reply.ok);
            hmset_called = true;
        },
        key,
        "field1", "value1",
        "field2", "value2",
        "field3", "value3"
    );
    
    redis.await();
    EXPECT_TRUE(hmset_called);
    
    // Test HMGET async
    redis.hmget(
        [&hmget_called](auto&& reply) {
            EXPECT_TRUE(reply.ok);
            EXPECT_EQ(reply.result.size(), 3);
            EXPECT_TRUE(reply.result[0].has_value());
            EXPECT_EQ(*reply.result[0], "value1");
            EXPECT_TRUE(reply.result[1].has_value());
            EXPECT_EQ(*reply.result[1], "value2");
            EXPECT_TRUE(reply.result[2].has_value());
            EXPECT_EQ(*reply.result[2], "value3");
            hmget_called = true;
        },
        key,
        "field1", "field2", "field3"
    );
    
    redis.await();
    EXPECT_TRUE(hmget_called);
    
    // Test HGETALL async
    redis.hgetall(
        [&hgetall_called](auto&& reply) {
            EXPECT_TRUE(reply.ok);
            EXPECT_EQ(reply.result.size(), 3);
            EXPECT_EQ(reply.result["field1"], "value1");
            EXPECT_EQ(reply.result["field2"], "value2");
            EXPECT_EQ(reply.result["field3"], "value3");
            hgetall_called = true;
        },
        key
    );
    
    redis.await();
    EXPECT_TRUE(hgetall_called);
    
    // Cleanup
    redis.del(key);
}

// Test increment operations asynchronously
TEST_F(RedisTest, ASYNC_HASH_COMMANDS_INCR) {
    std::string key = test_key("async-incr");
    bool hincrby_called = false;
    bool hincrbyfloat_called = false;
    
    // Test HINCRBY async
    redis.hincrby(
        [&hincrby_called](auto&& reply) {
            EXPECT_TRUE(reply.ok);
            EXPECT_EQ(reply.result, 1);
            hincrby_called = true;
        },
        key,
        "counter",
        1
    );
    
    redis.await();
    EXPECT_TRUE(hincrby_called);
    
    // Test HINCRBYFLOAT async
    redis.hincrbyfloat(
        [&hincrbyfloat_called](auto&& reply) {
            EXPECT_TRUE(reply.ok);
            EXPECT_FLOAT_EQ(reply.result, 10.5);
            hincrbyfloat_called = true;
        },
        key,
        "float",
        10.5
    );
    
    redis.await();
    EXPECT_TRUE(hincrbyfloat_called);
    
    // Cleanup
    redis.del(key);
}

// Test keys and values operations asynchronously
TEST_F(RedisTest, ASYNC_HASH_COMMANDS_KEYS_VALUES) {
    std::string key = test_key("async-keys-values");
    bool hmset_called = false;
    bool hkeys_called = false;
    bool hvals_called = false;
    bool hstrlen_called = false;
    
    // Setup hash
    redis.hmset(
        [&hmset_called](auto&& reply) {
            EXPECT_TRUE(reply.ok);
            hmset_called = true;
        },
        key,
        "field1", "value1",
        "field2", "value2",
        "field3", "value3"
    );
    
    redis.await();
    EXPECT_TRUE(hmset_called);
    
    // Test HKEYS async
    redis.hkeys(
        [&hkeys_called](auto&& reply) {
            EXPECT_TRUE(reply.ok);
            EXPECT_EQ(reply.result.size(), 3);
            EXPECT_TRUE(std::find(reply.result.begin(), reply.result.end(), "field1") != reply.result.end());
            EXPECT_TRUE(std::find(reply.result.begin(), reply.result.end(), "field2") != reply.result.end());
            EXPECT_TRUE(std::find(reply.result.begin(), reply.result.end(), "field3") != reply.result.end());
            hkeys_called = true;
        },
        key
    );
    
    redis.await();
    EXPECT_TRUE(hkeys_called);
    
    // Test HVALS async
    redis.hvals(
        [&hvals_called](auto&& reply) {
            EXPECT_TRUE(reply.ok);
            EXPECT_EQ(reply.result.size(), 3);
            EXPECT_TRUE(std::find(reply.result.begin(), reply.result.end(), "value1") != reply.result.end());
            EXPECT_TRUE(std::find(reply.result.begin(), reply.result.end(), "value2") != reply.result.end());
            EXPECT_TRUE(std::find(reply.result.begin(), reply.result.end(), "value3") != reply.result.end());
            hvals_called = true;
        },
        key
    );
    
    redis.await();
    EXPECT_TRUE(hvals_called);
    
    // Test HSTRLEN async
    redis.hstrlen(
        [&hstrlen_called](auto&& reply) {
            EXPECT_TRUE(reply.ok);
            EXPECT_EQ(reply.result, 6); // Length of "value1"
            hstrlen_called = true;
        },
        key,
        "field1"
    );
    
    redis.await();
    EXPECT_TRUE(hstrlen_called);
    
    // Cleanup
    redis.del(key);
}

// Test command chaining instead of explicit pipeline
TEST_F(RedisTest, ASYNC_HASH_COMMANDS_CHAINING) {
    std::string key = test_key("hash-chaining");
    bool all_commands_completed = false;
    int command_count = 0;
    
    // Setup callback to track completion
    auto completion_callback = [&command_count, &all_commands_completed](auto&&) {
        if (++command_count == 3) {
            all_commands_completed = true;
        }
    };
    
    // Chain multiple commands (they will be buffered and sent together)
    redis.hset(
        [&](auto&& reply) {
            EXPECT_TRUE(reply.ok);
            EXPECT_EQ(reply.result, 1);
            completion_callback(reply);
        },
        key,
        "field1",
        "value1"
    );
    
    redis.hset(
        [&](auto&& reply) {
            EXPECT_TRUE(reply.ok);
            EXPECT_EQ(reply.result, 1);
            completion_callback(reply);
        },
        key,
        "field2",
        "value2"
    );
    
    redis.hincrby(
        [&](auto&& reply) {
            EXPECT_TRUE(reply.ok);
            EXPECT_EQ(reply.result, 5);
            completion_callback(reply);
        },
        key,
        "counter",
        5
    );
    
    // Trigger the async operations and wait for completion
    redis.await();
    EXPECT_TRUE(all_commands_completed);
    
    // Verify the results with synchronous calls
    auto hgetall_result = redis.hgetall(key);
    EXPECT_EQ(hgetall_result.size(), 3);
    EXPECT_EQ(hgetall_result["field1"], "value1");
    EXPECT_EQ(hgetall_result["field2"], "value2");
    EXPECT_EQ(hgetall_result["counter"], "5");
    
    // Cleanup
    redis.del(key);
}

// Main function to run the tests
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
} 