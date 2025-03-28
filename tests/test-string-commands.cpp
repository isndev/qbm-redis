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
#define REDIS_URI \
    { "tcp://localhost:6379" }

using namespace qb::io;
using namespace std::chrono;
using namespace qb::redis;

// Generates unique key prefixes to avoid collisions between tests
inline std::string
key_prefix(const std::string &key = "") {
    static int counter = 0;
    std::string prefix = "qb::redis::string-test:" + std::to_string(++counter);
    
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

// Test APPEND command
TEST_F(RedisTest, SYNC_STRING_COMMANDS_APPEND) {
    std::string key = test_key("append");
    
    // Test basic append
    EXPECT_EQ(redis.append(key, "Hello"), 5);
    EXPECT_EQ(redis.append(key, " World"), 11);
    
    // Verify the final value
    auto value = redis.get(key);
    EXPECT_TRUE(value.has_value());
    EXPECT_EQ(*value, "Hello World");
}

// Test DECR/DECRBY commands
TEST_F(RedisTest, SYNC_STRING_COMMANDS_DECR) {
    std::string key = test_key("decr");
    
    // Set initial value
    redis.set(key, "10");
    
    // Test DECR
    EXPECT_EQ(redis.decr(key), 9);
    EXPECT_EQ(redis.decr(key), 8);
    
    // Test DECRBY
    EXPECT_EQ(redis.decrby(key, 3), 5);
    EXPECT_EQ(redis.decrby(key, 2), 3);
    
    // Test with non-existent key
    std::string new_key = test_key("decr_new");
    EXPECT_EQ(redis.decr(new_key), -1);
    EXPECT_EQ(redis.decrby(new_key, 5), -6);
}

// Test GET/GETRANGE commands
TEST_F(RedisTest, SYNC_STRING_COMMANDS_GET) {
    std::string key = test_key("get");
    std::string value = "Hello World";
    
    // Set value
    redis.set(key, value);
    
    // Test GET
    auto result = redis.get(key);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, value);
    
    // Test GET with non-existent key
    auto empty = redis.get(test_key("nonexistent"));
    EXPECT_FALSE(empty.has_value());
    
    // Test GETRANGE
    EXPECT_EQ(redis.getrange(key, 0, 4), "Hello");
    EXPECT_EQ(redis.getrange(key, 6, 10), "World");
    EXPECT_EQ(redis.getrange(key, -5, -1), "World");
}

// Test GETSET command
TEST_F(RedisTest, SYNC_STRING_COMMANDS_GETSET) {
    std::string key = test_key("getset");
    
    // Test with non-existent key
    auto result = redis.getset(key, "new_value");
    EXPECT_FALSE(result.has_value());
    
    // Test with existing key
    redis.set(key, "old_value");
    result = redis.getset(key, "new_value");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, "old_value");
    
    // Verify new value
    auto current = redis.get(key);
    EXPECT_TRUE(current.has_value());
    EXPECT_EQ(*current, "new_value");
}

// Test INCR/INCRBY commands
TEST_F(RedisTest, SYNC_STRING_COMMANDS_INCR) {
    std::string key = test_key("incr");
    
    // Test INCR
    EXPECT_EQ(redis.incr(key), 1);
    EXPECT_EQ(redis.incr(key), 2);
    
    // Test INCRBY
    EXPECT_EQ(redis.incrby(key, 3), 5);
    EXPECT_EQ(redis.incrby(key, 2), 7);
    
    // Test with non-existent key
    std::string new_key = test_key("incr_new");
    EXPECT_EQ(redis.incr(new_key), 1);
    EXPECT_EQ(redis.incrby(new_key, 5), 6);
}

// Test INCRBYFLOAT command
TEST_F(RedisTest, SYNC_STRING_COMMANDS_INCRBYFLOAT) {
    std::string key = test_key("incrbyfloat");
    
    // Set initial value
    redis.set(key, "10.5");
    
    // Test increment
    EXPECT_DOUBLE_EQ(redis.incrbyfloat(key, 0.1), 10.6);
    EXPECT_DOUBLE_EQ(redis.incrbyfloat(key, 0.5), 11.1);
    
    // Test with non-existent key
    std::string new_key = test_key("incrbyfloat_new");
    EXPECT_DOUBLE_EQ(redis.incrbyfloat(new_key, 1.5), 1.5);
}

// Test MGET/MSET commands
TEST_F(RedisTest, SYNC_STRING_COMMANDS_MGET_MSET) {
    std::string key1 = test_key("mget1");
    std::string key2 = test_key("mget2");
    std::string key3 = test_key("mget3");
    
    // Test MSET
    EXPECT_TRUE(redis.mset({
        {key1, "value1"},
        {key2, "value2"},
        {key3, "value3"}
    }));
    
    // Test MGET
    auto results = redis.mget({key1, key2, key3, test_key("nonexistent")});
    EXPECT_EQ(results.size(), 4);
    EXPECT_EQ(results[0], "value1");
    EXPECT_EQ(results[1], "value2");
    EXPECT_EQ(results[2], "value3");
    EXPECT_FALSE(results[3].has_value());
}

// Test MSETNX command
TEST_F(RedisTest, SYNC_STRING_COMMANDS_MSETNX) {
    std::string key1 = test_key("msetnx1");
    std::string key2 = test_key("msetnx2");
    std::string key3 = test_key("msetnx3");
    
    // Test successful MSETNX
    EXPECT_TRUE(redis.msetnx({
        {key1, "value1"},
        {key2, "value2"}
    }));
    
    // Test failed MSETNX (key already exists)
    EXPECT_FALSE(redis.msetnx({
        {key1, "new_value1"},
        {key3, "value3"}
    }));
    
    // Verify values
    auto value1 = redis.get(key1);
    auto value2 = redis.get(key2);
    auto value3 = redis.get(key3);
    
    EXPECT_TRUE(value1.has_value());
    EXPECT_TRUE(value2.has_value());
    EXPECT_FALSE(value3.has_value());
    EXPECT_EQ(*value1, "value1");
    EXPECT_EQ(*value2, "value2");
}

// Test PSETEX command
TEST_F(RedisTest, SYNC_STRING_COMMANDS_PSETEX) {
    std::string key = test_key("psetex");
    
    // Test with milliseconds
    EXPECT_TRUE(redis.psetex(key, 1000, "value"));
    
    // Verify value exists
    auto value = redis.get(key);
    EXPECT_TRUE(value.has_value());
    EXPECT_EQ(*value, "value");
    
    // Wait for expiration
    std::this_thread::sleep_for(milliseconds(1100));
    
    // Verify value is gone
    value = redis.get(key);
    EXPECT_FALSE(value.has_value());
}

// Test SET command with various options
TEST_F(RedisTest, SYNC_STRING_COMMANDS_SET) {
    std::string key = test_key("set");
    
    // Test basic SET
    EXPECT_TRUE(redis.set(key, "value"));
    
    // Test SET with expiration
    EXPECT_TRUE(redis.set(key, "value2", 1));
    std::this_thread::sleep_for(seconds(2));
    auto value = redis.get(key);
    EXPECT_FALSE(value.has_value());
    
    // Test SET with NX option
    EXPECT_TRUE(redis.set(key, "value3", UpdateType::NOT_EXIST));
    EXPECT_THROW(redis.set(key, "value4", UpdateType::NOT_EXIST), std::runtime_error);
    
    // Test SET with XX option
    EXPECT_TRUE(redis.set(key, "value5", UpdateType::EXIST));
    EXPECT_THROW(redis.set(test_key("nonexistent"), "value6", UpdateType::EXIST), std::runtime_error);
}

// Test SETEX command
TEST_F(RedisTest, SYNC_STRING_COMMANDS_SETEX) {
    std::string key = test_key("setex");
    
    // Test SETEX
    EXPECT_TRUE(redis.setex(key, 1, "value"));
    
    // Verify value exists
    auto value = redis.get(key);
    EXPECT_TRUE(value.has_value());
    EXPECT_EQ(*value, "value");
    
    // Wait for expiration
    std::this_thread::sleep_for(seconds(2));
    
    // Verify value is gone
    value = redis.get(key);
    EXPECT_FALSE(value.has_value());
}

// Test SETNX command
TEST_F(RedisTest, SYNC_STRING_COMMANDS_SETNX) {
    std::string key = test_key("setnx");
    
    // Test SETNX
    EXPECT_TRUE(redis.setnx(key, "value1"));
    EXPECT_FALSE(redis.setnx(key, "value2"));
    
    // Verify value
    auto value = redis.get(key);
    EXPECT_TRUE(value.has_value());
    EXPECT_EQ(*value, "value1");
}

// Test SETRANGE command
TEST_F(RedisTest, SYNC_STRING_COMMANDS_SETRANGE) {
    std::string key = test_key("setrange");
    
    // Set initial value
    redis.set(key, "Hello World");
    
    // Test SETRANGE
    EXPECT_EQ(redis.setrange(key, 6, "Redis"), 11);
    
    // Verify result
    auto value = redis.get(key);
    EXPECT_TRUE(value.has_value());
    EXPECT_EQ(*value, "Hello Redis");
}

// Test STRLEN command
TEST_F(RedisTest, SYNC_STRING_COMMANDS_STRLEN) {
    std::string key = test_key("strlen");
    
    // Test with existing key
    redis.set(key, "Hello World");
    EXPECT_EQ(redis.strlen(key), 11);
    
    // Test with non-existent key
    EXPECT_EQ(redis.strlen(test_key("nonexistent")), 0);
}

/*
 * ASYNCHRONOUS TESTS
 */

// Test async APPEND command
TEST_F(RedisTest, ASYNC_STRING_COMMANDS_APPEND) {
    std::string key = test_key("async_append");
    bool append_completed = false;
    
    redis.append(
        [&](auto&& reply) {
            EXPECT_TRUE(reply.ok());
            EXPECT_EQ(reply.result(), 5);
            append_completed = true;
        },
        key,
        "Hello"
    );
    
    redis.await();
    EXPECT_TRUE(append_completed);
    
    // Verify the value
    auto value = redis.get(key);
    EXPECT_TRUE(value.has_value());
    EXPECT_EQ(*value, "Hello");
}

// Test async DECR/DECRBY commands
TEST_F(RedisTest, ASYNC_STRING_COMMANDS_DECR) {
    std::string key = test_key("async_decr");
    bool decr_completed = false;
    bool decrby_completed = false;
    
    // Set initial value
    redis.set(key, "10");
    
    redis.decr(
        [&](auto&& reply) {
            EXPECT_TRUE(reply.ok());
            EXPECT_EQ(reply.result(), 9);
            decr_completed = true;
        },
        key
    );
    
    redis.decrby(
        [&](auto&& reply) {
            EXPECT_TRUE(reply.ok());
            EXPECT_EQ(reply.result(), 6);
            decrby_completed = true;
        },
        key,
        3
    );
    
    redis.await();
    EXPECT_TRUE(decr_completed);
    EXPECT_TRUE(decrby_completed);
}

// Test async GET/GETRANGE commands
TEST_F(RedisTest, ASYNC_STRING_COMMANDS_GET) {
    std::string key = test_key("async_get");
    bool get_completed = false;
    bool getrange_completed = false;
    
    // Set value
    redis.set(key, "Hello World");
    
    redis.get(
        [&](auto&& reply) {
            EXPECT_TRUE(reply.ok());
            EXPECT_TRUE(reply.result().has_value());
            EXPECT_EQ(*reply.result(), "Hello World");
            get_completed = true;
        },
        key
    );
    
    redis.getrange(
        [&](auto&& reply) {
            EXPECT_TRUE(reply.ok());
            EXPECT_EQ(reply.result(), "Hello");
            getrange_completed = true;
        },
        key,
        0,
        4
    );
    
    redis.await();
    EXPECT_TRUE(get_completed);
    EXPECT_TRUE(getrange_completed);
}

// Test async GETSET command
TEST_F(RedisTest, ASYNC_STRING_COMMANDS_GETSET) {
    std::string key = test_key("async_getset");
    bool getset_completed = false;
    
    // Set initial value
    redis.set(key, "old_value");
    
    redis.getset(
        [&](auto&& reply) {
            EXPECT_TRUE(reply.ok());
            EXPECT_TRUE(reply.result().has_value());
            EXPECT_EQ(*reply.result(), "old_value");
            getset_completed = true;
        },
        key,
        "new_value"
    );
    
    redis.await();
    EXPECT_TRUE(getset_completed);
    
    // Verify new value
    auto value = redis.get(key);
    EXPECT_TRUE(value.has_value());
    EXPECT_EQ(*value, "new_value");
}

// Test async INCR/INCRBY commands
TEST_F(RedisTest, ASYNC_STRING_COMMANDS_INCR) {
    std::string key = test_key("async_incr");
    bool incr_completed = false;
    bool incrby_completed = false;
    
    redis.incr(
        [&](auto&& reply) {
            EXPECT_TRUE(reply.ok());
            EXPECT_EQ(reply.result(), 1);
            incr_completed = true;
        },
        key
    );
    
    redis.incrby(
        [&](auto&& reply) {
            EXPECT_TRUE(reply.ok());
            EXPECT_EQ(reply.result(), 4);
            incrby_completed = true;
        },
        key,
        3
    );
    
    redis.await();
    EXPECT_TRUE(incr_completed);
    EXPECT_TRUE(incrby_completed);
}

// Test async INCRBYFLOAT command
TEST_F(RedisTest, ASYNC_STRING_COMMANDS_INCRBYFLOAT) {
    std::string key = test_key("async_incrbyfloat");
    bool incrbyfloat_completed = false;
    
    // Set initial value
    redis.set(key, "10.5");
    
    redis.incrbyfloat(
        [&](auto&& reply) {
            EXPECT_TRUE(reply.ok());
            EXPECT_DOUBLE_EQ(reply.result(), 11.0);
            incrbyfloat_completed = true;
        },
        key,
        0.5
    );
    
    redis.await();
    EXPECT_TRUE(incrbyfloat_completed);
}

// Test async MGET/MSET commands
TEST_F(RedisTest, ASYNC_STRING_COMMANDS_MGET_MSET) {
    std::string key1 = test_key("async_mget1");
    std::string key2 = test_key("async_mget2");
    bool mset_completed = false;
    bool mget_completed = false;
    
    redis.mset(
        [&](auto&& reply) {
            EXPECT_TRUE(reply.ok());
            mset_completed = true;
        },
        {
            {key1, "value1"},
            {key2, "value2"}
        }
    );
    
    redis.await();
    EXPECT_TRUE(mset_completed);
    
    redis.mget(
        [&](auto&& reply) {
            EXPECT_TRUE(reply.ok());
            EXPECT_EQ(reply.result().size(), 2);
            EXPECT_EQ(reply.result()[0], "value1");
            EXPECT_EQ(reply.result()[1], "value2");
            mget_completed = true;
        },
        {key1, key2}
    );
    
    redis.await();
    EXPECT_TRUE(mget_completed);
}

// Test async MSETNX command
TEST_F(RedisTest, ASYNC_STRING_COMMANDS_MSETNX) {
    std::string key1 = test_key("async_msetnx1");
    std::string key2 = test_key("async_msetnx2");
    bool msetnx_completed = false;
    
    redis.msetnx(
        [&](auto&& reply) {
            EXPECT_TRUE(reply.ok());
            msetnx_completed = true;
        },
        {
            {key1, "value1"},
            {key2, "value2"}
        }
    );
    
    redis.await();
    EXPECT_TRUE(msetnx_completed);
    
    // Verify values
    auto value1 = redis.get(key1);
    auto value2 = redis.get(key2);
    EXPECT_TRUE(value1.has_value());
    EXPECT_TRUE(value2.has_value());
    EXPECT_EQ(*value1, "value1");
    EXPECT_EQ(*value2, "value2");
}

// Test async PSETEX command
TEST_F(RedisTest, ASYNC_STRING_COMMANDS_PSETEX) {
    std::string key = test_key("async_psetex");
    bool psetex_completed = false;
    
    redis.psetex(
        [&](auto&& reply) {
            EXPECT_TRUE(reply.ok());
            psetex_completed = true;
        },
        key,
        1000,
        "value"
    );
    
    redis.await();
    EXPECT_TRUE(psetex_completed);
    
    // Verify value exists
    auto value = redis.get(key);
    EXPECT_TRUE(value.has_value());
    EXPECT_EQ(*value, "value");
    
    // Wait for expiration
    std::this_thread::sleep_for(milliseconds(1100));
    
    // Verify value is gone
    value = redis.get(key);
    EXPECT_FALSE(value.has_value());
}

// Test async SET command
TEST_F(RedisTest, ASYNC_STRING_COMMANDS_SET) {
    std::string key = test_key("async_set");
    bool set_completed = false;
    
    redis.set(
        [&](auto&& reply) {
            EXPECT_TRUE(reply.ok());
            set_completed = true;
        },
        key,
        "value",
        1
    );
    
    redis.await();
    EXPECT_TRUE(set_completed);
    
    // Verify value exists
    auto value = redis.get(key);
    EXPECT_TRUE(value.has_value());
    EXPECT_EQ(*value, "value");
    
    // Wait for expiration
    std::this_thread::sleep_for(seconds(2));
    
    // Verify value is gone
    value = redis.get(key);
    EXPECT_FALSE(value.has_value());
}

// Test async SETEX command
TEST_F(RedisTest, ASYNC_STRING_COMMANDS_SETEX) {
    std::string key = test_key("async_setex");
    bool setex_completed = false;
    
    redis.setex(
        [&](auto&& reply) {
            EXPECT_TRUE(reply.ok());
            setex_completed = true;
        },
        key,
        1,
        "value"
    );
    
    redis.await();
    EXPECT_TRUE(setex_completed);
    
    // Verify value exists
    auto value = redis.get(key);
    EXPECT_TRUE(value.has_value());
    EXPECT_EQ(*value, "value");
    
    // Wait for expiration
    std::this_thread::sleep_for(seconds(2));
    
    // Verify value is gone
    value = redis.get(key);
    EXPECT_FALSE(value.has_value());
}

// Test async SETNX command
TEST_F(RedisTest, ASYNC_STRING_COMMANDS_SETNX) {
    std::string key = test_key("async_setnx");
    bool setnx_completed = false;
    
    redis.setnx(
        [&](auto&& reply) {
            EXPECT_TRUE(reply.ok());
            setnx_completed = true;
        },
        key,
        "value"
    );
    
    redis.await();
    EXPECT_TRUE(setnx_completed);
    
    // Verify value
    auto value = redis.get(key);
    EXPECT_TRUE(value.has_value());
    EXPECT_EQ(*value, "value");
}

// Test async SETRANGE command
TEST_F(RedisTest, ASYNC_STRING_COMMANDS_SETRANGE) {
    std::string key = test_key("async_setrange");
    bool setrange_completed = false;
    
    // Set initial value
    redis.set(key, "Hello World");
    
    redis.setrange(
        [&](auto&& reply) {
            EXPECT_TRUE(reply.ok());
            EXPECT_EQ(reply.result(), 11);
            setrange_completed = true;
        },
        key,
        6,
        "Redis"
    );
    
    redis.await();
    EXPECT_TRUE(setrange_completed);
    
    // Verify result
    auto value = redis.get(key);
    EXPECT_TRUE(value.has_value());
    EXPECT_EQ(*value, "Hello Redis");
}

// Test async STRLEN command
TEST_F(RedisTest, ASYNC_STRING_COMMANDS_STRLEN) {
    std::string key = test_key("async_strlen");
    bool strlen_completed = false;
    
    // Set value
    redis.set(key, "Hello World");
    
    redis.strlen(
        [&](auto&& reply) {
            EXPECT_TRUE(reply.ok());
            EXPECT_EQ(reply.result(), 11);
            strlen_completed = true;
        },
        key
    );
    
    redis.await();
    EXPECT_TRUE(strlen_completed);
}

// Test command chaining
TEST_F(RedisTest, ASYNC_STRING_COMMANDS_CHAINING) {
    std::string key = test_key("string_chaining");
    bool all_commands_completed = false;
    int command_count = 0;
    
    // Setup callback to track completion
    auto completion_callback = [&command_count, &all_commands_completed](auto&&) {
        if (++command_count == 3) {
            all_commands_completed = true;
        }
    };
    
    // Chain multiple commands
    redis.set(
        [&](auto&& reply) {
            EXPECT_TRUE(reply.ok());
            completion_callback(reply);
        },
        key,
        "value"
    );
    
    redis.append(
        [&](auto&& reply) {
            EXPECT_TRUE(reply.ok());
            EXPECT_EQ(reply.result(), 14);
            completion_callback(reply);
        },
        key,
        " appended"
    );
    
    redis.get(
        [&](auto&& reply) {
            EXPECT_TRUE(reply.ok());
            EXPECT_TRUE(reply.result().has_value());
            EXPECT_EQ(*reply.result(), "value appended");
            completion_callback(reply);
        },
        key
    );
    
    // Trigger the async operations and wait for completion
    redis.await();
    EXPECT_TRUE(all_commands_completed);
}

// Main function to run the tests
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
} 