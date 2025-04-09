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

// Helper function to generate unique key prefixes
inline std::string
key_prefix(const std::string &key = "") {
    static int  counter = 0;
    std::string prefix  = "qb::redis::key-test:" + std::to_string(++counter);

    if (key.empty()) {
        return prefix;
    }

    return prefix + ":" + key;
}

// Helper function to generate test keys
inline std::string
test_key(const std::string &k) {
    return "{" + key_prefix() + "}::" + k;
}

// Test fixture class
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

// Test DEL command
TEST_F(RedisTest, SYNC_KEY_COMMANDS_DEL) {
    std::string key1 = test_key("del1");
    std::string key2 = test_key("del2");
    std::string key3 = test_key("del3");

    // Set some test keys
    redis.set(key1, "value1");
    redis.set(key2, "value2");
    redis.set(key3, "value3");

    // Test deleting single key
    EXPECT_EQ(redis.del(key1), 1);
    EXPECT_FALSE(redis.exists(key1));

    // Test deleting multiple keys
    EXPECT_EQ(redis.del(key2, key3), 2);
    EXPECT_FALSE(redis.exists(key2));
    EXPECT_FALSE(redis.exists(key3));

    // Test deleting non-existent key
    EXPECT_EQ(redis.del("non_existent_key"), 0);
}

// Test DUMP command
TEST_F(RedisTest, SYNC_KEY_COMMANDS_DUMP) {
    std::string key   = test_key("dump");
    std::string value = "test_value";

    // Set a test key
    redis.set(key, value);

    // Dump the key
    auto dump_result = redis.dump(key);
    EXPECT_TRUE(dump_result.has_value());

    // Restore the key with a different name
    std::string new_key = test_key("restored");
    EXPECT_TRUE(redis.restore(new_key, *dump_result, 0));

    // Verify the restored value
    auto restored_value = redis.get(new_key);
    EXPECT_TRUE(restored_value.has_value());
    EXPECT_EQ(*restored_value, value);
}

// Test EXISTS command
TEST_F(RedisTest, SYNC_KEY_COMMANDS_EXISTS) {
    std::string key1 = test_key("exists1");
    std::string key2 = test_key("exists2");
    std::string key3 = test_key("exists3");

    // Set some test keys
    redis.set(key1, "value1");
    redis.set(key2, "value2");

    // Test checking single key
    EXPECT_TRUE(redis.exists(key1));
    EXPECT_FALSE(redis.exists(key3));

    // Test checking multiple keys
    EXPECT_EQ(redis.exists(key1, key2, key3), 2);
}

// Test KEYS command
TEST_F(RedisTest, SYNC_KEY_COMMANDS_KEYS) {
    std::string pattern = test_key("keys*");
    std::string key1    = pattern + "1";
    std::string key2    = pattern + "2";
    std::string key3    = test_key("other");

    // Set test keys
    redis.set(key1, "value1");
    redis.set(key2, "value2");
    redis.set(key3, "value3");

    // Test keys pattern matching
    auto keys = redis.keys(pattern);
    EXPECT_EQ(keys.size(), 2);
    EXPECT_TRUE(std::find(keys.begin(), keys.end(), key1) != keys.end());
    EXPECT_TRUE(std::find(keys.begin(), keys.end(), key2) != keys.end());
}

// Test RANDOMKEY command
TEST_F(RedisTest, SYNC_KEY_COMMANDS_RANDOMKEY) {
    // Test with empty database
    EXPECT_FALSE(redis.randomkey().has_value());

    // Set some test keys
    std::string key1 = test_key("random1");
    std::string key2 = test_key("random2");
    std::string key3 = test_key("random3");

    redis.set(key1, "value1");
    redis.set(key2, "value2");
    redis.set(key3, "value3");

    // Test getting random key
    auto random_key = redis.randomkey();
    EXPECT_TRUE(random_key.has_value());
    EXPECT_TRUE(*random_key == key1 || *random_key == key2 || *random_key == key3);
}

// Test SCAN command
TEST_F(RedisTest, SYNC_KEY_COMMANDS_SCAN) {
    std::string              pattern = test_key("scan*");
    std::vector<std::string> keys;

    // Set multiple test keys
    for (int i = 0; i < 10; ++i) {
        redis.set(pattern + std::to_string(i), "value" + std::to_string(i));
    }

    // Test scanning keys
    long long cursor = 0;
    do {
        auto scan_result = redis.scan(cursor, pattern, 5);
        cursor           = scan_result.cursor;
        keys.insert(keys.end(), scan_result.items.begin(), scan_result.items.end());
    } while (cursor != 0);

    EXPECT_EQ(keys.size(), 10);
}

// Test TOUCH command
TEST_F(RedisTest, SYNC_KEY_COMMANDS_TOUCH) {
    std::string key1 = test_key("touch1");
    std::string key2 = test_key("touch2");
    std::string key3 = test_key("touch3");

    // Set some test keys
    redis.set(key1, "value1");
    redis.set(key2, "value2");

    // Test touching keys
    EXPECT_EQ(redis.touch(key1, key2, key3), 2);
}

// Test TYPE command
TEST_F(RedisTest, SYNC_KEY_COMMANDS_TYPE) {
    std::string string_key = test_key("type_string");
    std::string list_key   = test_key("type_list");
    std::string set_key    = test_key("type_set");
    std::string hash_key   = test_key("type_hash");
    std::string zset_key   = test_key("type_zset");

    // Set different types of keys
    redis.set(string_key, "value");
    redis.lpush(list_key, "value");
    redis.sadd(set_key, "value");
    redis.hset(hash_key, "field", "value");
    redis.zadd(zset_key, {{1.0, "value"}});

    // Test type checking
    EXPECT_EQ(redis.type(string_key), "string");
    EXPECT_EQ(redis.type(list_key), "list");
    EXPECT_EQ(redis.type(set_key), "set");
    EXPECT_EQ(redis.type(hash_key), "hash");
    EXPECT_EQ(redis.type(zset_key), "zset");
    EXPECT_EQ(redis.type("non_existent_key"), "none");
}

// Test UNLINK command
TEST_F(RedisTest, SYNC_KEY_COMMANDS_UNLINK) {
    std::string key1 = test_key("unlink1");
    std::string key2 = test_key("unlink2");
    std::string key3 = test_key("unlink3");

    // Set test keys
    redis.set(key1, "value1");
    redis.set(key2, "value2");

    // Test unlinking keys
    EXPECT_EQ(redis.unlink(key1, key2, key3), 2);
    EXPECT_FALSE(redis.exists(key1));
    EXPECT_FALSE(redis.exists(key2));
    EXPECT_FALSE(redis.exists(key3));
}

/*
 * ASYNCHRONOUS TESTS
 */

// Test async DEL command
TEST_F(RedisTest, ASYNC_KEY_COMMANDS_DEL) {
    std::string key1 = test_key("async_del1");
    std::string key2 = test_key("async_del2");
    std::string key3 = test_key("async_del3");

    // Set test keys
    redis.set(key1, "value1");
    redis.set(key2, "value2");
    redis.set(key3, "value3");

    // Test async delete
    long long deleted_count = 0;
    redis.del([&](auto &&reply) { deleted_count = reply.result(); }, key1, key2, key3);

    redis.await();

    EXPECT_EQ(deleted_count, 3);
    EXPECT_FALSE(redis.exists(key1));
    EXPECT_FALSE(redis.exists(key2));
    EXPECT_FALSE(redis.exists(key3));
}

// Test async DUMP command
TEST_F(RedisTest, ASYNC_KEY_COMMANDS_DUMP) {
    std::string key   = test_key("async_dump");
    std::string value = "test_value";

    // Set test key
    redis.set(key, value);

    // Test async dump
    std::optional<std::string> dump_result;
    redis.dump([&](auto &&reply) { dump_result = reply.result(); }, key);

    redis.await();

    EXPECT_TRUE(dump_result.has_value());

    // Restore the key
    std::string new_key         = test_key("async_restored");
    bool        restore_success = false;
    redis.restore([&](auto &&reply) { restore_success = reply.ok(); }, new_key,
                  *dump_result, 0);

    redis.await();

    EXPECT_TRUE(restore_success);

    // Verify restored value
    auto restored_value = redis.get(new_key);
    EXPECT_TRUE(restored_value.has_value());
    EXPECT_EQ(*restored_value, value);
}

// Test async EXISTS command
TEST_F(RedisTest, ASYNC_KEY_COMMANDS_EXISTS) {
    std::string key1 = test_key("async_exists1");
    std::string key2 = test_key("async_exists2");
    std::string key3 = test_key("async_exists3");

    // Set test keys
    redis.set(key1, "value1");
    redis.set(key2, "value2");

    // Test async exists
    long long existing_count = 0;
    redis.exists([&](auto &&reply) { existing_count = reply.result(); }, key1, key2,
                 key3);

    redis.await();

    EXPECT_EQ(existing_count, 2);
}

// Test async KEYS command
TEST_F(RedisTest, ASYNC_KEY_COMMANDS_KEYS) {
    std::string pattern = test_key("async_keys*");
    std::string key1    = pattern + "1";
    std::string key2    = pattern + "2";
    std::string key3    = test_key("async_other");

    // Set test keys
    redis.set(key1, "value1");
    redis.set(key2, "value2");
    redis.set(key3, "value3");

    // Test async keys
    std::vector<std::string> keys;
    redis.keys([&](auto &&reply) { keys = reply.result(); }, pattern);

    redis.await();

    EXPECT_EQ(keys.size(), 2);
    EXPECT_TRUE(std::find(keys.begin(), keys.end(), key1) != keys.end());
    EXPECT_TRUE(std::find(keys.begin(), keys.end(), key2) != keys.end());
}

// Test async RANDOMKEY command
TEST_F(RedisTest, ASYNC_KEY_COMMANDS_RANDOMKEY) {
    std::string key1 = test_key("async_random1");
    std::string key2 = test_key("async_random2");
    std::string key3 = test_key("async_random3");

    // Set test keys
    redis.set(key1, "value1");
    redis.set(key2, "value2");
    redis.set(key3, "value3");

    // Test async randomkey
    std::optional<std::string> random_key;
    redis.randomkey([&](auto &&reply) { random_key = reply.result(); });

    redis.await();

    EXPECT_TRUE(random_key.has_value());
    EXPECT_TRUE(*random_key == key1 || *random_key == key2 || *random_key == key3);
}

// Test async SCAN command
TEST_F(RedisTest, ASYNC_KEY_COMMANDS_SCAN) {
    std::string              pattern = test_key("async_scan*");
    std::vector<std::string> keys;

    // Set test keys
    for (int i = 0; i < 10; ++i) {
        redis.set(pattern + std::to_string(i), "value" + std::to_string(i));
    }

    // Test async scan
    long long cursor = 0;
    do {
        bool scan_complete = false;
        redis.scan(
            [&](auto &&reply) {
                cursor = reply.result().cursor;
                keys.insert(keys.end(), reply.result().items.begin(),
                            reply.result().items.end());
                scan_complete = true;
            },
            cursor, pattern, 5);

        while (!scan_complete) {
            redis.await();
        }
    } while (cursor != 0);

    EXPECT_EQ(keys.size(), 10);
}

// Test async TOUCH command
TEST_F(RedisTest, ASYNC_KEY_COMMANDS_TOUCH) {
    std::string key1 = test_key("async_touch1");
    std::string key2 = test_key("async_touch2");
    std::string key3 = test_key("async_touch3");

    // Set test keys
    redis.set(key1, "value1");
    redis.set(key2, "value2");

    // Test async touch
    long long touched_count = 0;
    redis.touch([&](auto &&reply) { touched_count = reply.result(); }, key1, key2, key3);

    redis.await();

    EXPECT_EQ(touched_count, 2);
}

// Test async TYPE command
TEST_F(RedisTest, ASYNC_KEY_COMMANDS_TYPE) {
    std::string key = test_key("async_type");

    // Set test key
    redis.set(key, "value");

    // Test async type
    std::string type;
    redis.type([&](auto &&reply) { type = reply.result(); }, key);

    redis.await();

    EXPECT_EQ(type, "string");
}

// Test async UNLINK command
TEST_F(RedisTest, ASYNC_KEY_COMMANDS_UNLINK) {
    std::string key1 = test_key("async_unlink1");
    std::string key2 = test_key("async_unlink2");
    std::string key3 = test_key("async_unlink3");

    // Set test keys
    redis.set(key1, "value1");
    redis.set(key2, "value2");

    // Test async unlink
    long long unlinked_count = 0;
    redis.unlink([&](auto &&reply) { unlinked_count = reply.result(); }, key1, key2,
                 key3);

    redis.await();

    EXPECT_EQ(unlinked_count, 2);
    EXPECT_FALSE(redis.exists(key1));
    EXPECT_FALSE(redis.exists(key2));
    EXPECT_FALSE(redis.exists(key3));
}