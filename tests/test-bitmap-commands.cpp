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
    std::string prefix  = "qb::redis::bitmap-test:" + std::to_string(++counter);

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

// Test BITCOUNT
TEST_F(RedisTest, SYNC_BITMAP_COMMANDS_BITCOUNT) {
    std::string key = test_key("bitcount");

    // Test with an empty string
    EXPECT_EQ(redis.bitcount(key), 0);

    // Test with a string containing bits
    redis.set(key, std::string("\xFF\x00\xFF", 3));
    EXPECT_EQ(redis.bitcount(key), 16);

    // Test with a specific range
    EXPECT_EQ(redis.bitcount(key, 0, 0), 8); // First byte
    EXPECT_EQ(redis.bitcount(key, 1, 1), 0); // Second byte
    EXPECT_EQ(redis.bitcount(key, 2, 2), 8); // Third byte
}

// Test BITFIELD
TEST_F(RedisTest, SYNC_BITMAP_COMMANDS_BITFIELD) {
    std::string key = test_key("bitfield");

    // Test SET and GET
    std::vector<std::string> operations = {
        "SET", "u4", "0", "100", // Set 4-bit unsigned integer at offset 0 to 100
        "GET", "u4", "0"         // Get 4-bit unsigned integer at offset 0
    };

    auto results = redis.bitfield(key, operations);
    EXPECT_EQ(results.size(), 2);
    EXPECT_EQ(results[0], 0); // SET returns the old value
    EXPECT_EQ(results[1], 4); // GET returns the new value
}

// Test BITOP
TEST_F(RedisTest, SYNC_BITMAP_COMMANDS_BITOP) {
    std::string key1    = test_key("bitop1");
    std::string key2    = test_key("bitop2");
    std::string destkey = test_key("bitop_dest");

    // Prepare test strings
    redis.set(key1, std::string("\xFF\x00\xFF", 3));
    redis.set(key2, "\x0F\xF0"); // 00001111 11110000

    // Test AND
    EXPECT_EQ(redis.bitop("AND", destkey, {key1, key2}), 2);
    auto result = redis.get(destkey);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, std::string("\x0F\x00", 2)); // 00001111 00000000

    // Test OR
    EXPECT_EQ(redis.bitop("OR", destkey, {key1, key2}), 2);
    result = redis.get(destkey);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, "\xFF\xF0"); // 11111111 11110000

    // Test XOR
    EXPECT_EQ(redis.bitop("XOR", destkey, {key1, key2}), 2);
    result = redis.get(destkey);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, "\xF0\xF0"); // 11110000 11110000

    // Test NOT
    EXPECT_EQ(redis.bitop("NOT", destkey, {key1}), 2);
    result = redis.get(destkey);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, std::string("\x00\xFF", 2)); // 00000000 11111111
}

// Test BITPOS
TEST_F(RedisTest, SYNC_BITMAP_COMMANDS_BITPOS) {
    std::string key = test_key("bitpos");

    // Test with an empty string
    EXPECT_EQ(redis.bitpos(key, true), -1);

    // Test with a string containing bits
    redis.set(key, std::string("\xFF\x00\xFF", 3));

    // Find the first bit set to 1
    EXPECT_EQ(redis.bitpos(key, true), 0);

    // Find the first bit set to 0
    EXPECT_EQ(redis.bitpos(key, false), 8);

    // Test with a specific range
    EXPECT_EQ(redis.bitpos(key, true, 0, 0), -1); // First byte
    EXPECT_EQ(redis.bitpos(key, true, 1, 1), 8);  // Second byte
    EXPECT_EQ(redis.bitpos(key, true, 2, 2), -1); // Third byte
}

// Test GETBIT and SETBIT
TEST_F(RedisTest, SYNC_BITMAP_COMMANDS_GETBIT_SETBIT) {
    std::string key = test_key("getbit_setbit");

    // Test with a non-existent key
    // Test avec une clé non existante
    EXPECT_FALSE(redis.getbit(key, 0));

    // Test SETBIT
    EXPECT_FALSE(redis.setbit(key, 7, true)); // Premier bit à 1
    EXPECT_TRUE(redis.setbit(key, 7, false)); // Retourne l'ancienne valeur
    EXPECT_FALSE(redis.setbit(key, 7, true)); // Retourne la nouvelle valeur

    // Test GETBIT
    EXPECT_FALSE(redis.getbit(key, 0));
    EXPECT_TRUE(redis.getbit(key, 7));
    EXPECT_FALSE(redis.getbit(key, 8)); // Bit hors limites
}

/*
 * TESTS ASYNCHRONES
 */

// Test asynchrone BITCOUNT
TEST_F(RedisTest, ASYNC_BITMAP_COMMANDS_BITCOUNT) {
    std::string key   = test_key("async_bitcount");
    long long   count = 0;

    redis.set(key, std::string("\xff\x00\xff", 3)); // 11111111 00000000 11111111

    redis.bitcount([&](auto &&reply) { count = reply.result(); }, key);

    redis.await();

    EXPECT_EQ(count, 16);
}

// Test asynchrone BITFIELD
TEST_F(RedisTest, ASYNC_BITMAP_COMMANDS_BITFIELD) {
    std::string                           key = test_key("async_bitfield");
    std::vector<std::optional<long long>> results;

    std::vector<std::string> operations = {"SET", "u4", "0", "100", "GET", "u4", "0"};

    redis.bitfield([&](auto &&reply) { results = reply.result(); }, key, operations);

    redis.await();

    EXPECT_EQ(results.size(), 2);
    EXPECT_EQ(results[0], 0);
    EXPECT_EQ(results[1], 4);
}

// Test asynchrone BITOP
TEST_F(RedisTest, ASYNC_BITMAP_COMMANDS_BITOP) {
    std::string key1          = test_key("async_bitop1");
    std::string key2          = test_key("async_bitop2");
    std::string destkey       = test_key("async_bitop_dest");
    long long   result_length = 0;

    redis.set(key1, std::string("\xff\x00", 2));
    redis.set(key2, "\x0f\xf0");

    redis.bitop([&](auto &&reply) { result_length = reply.result(); }, "AND", destkey,
                {key1, key2});

    redis.await();

    EXPECT_EQ(result_length, 2);

    std::optional<std::string> result;
    redis.get([&](auto &&reply) { result = reply.result(); }, destkey);

    redis.await();

    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, std::string("\x0f\x00", 2));
}

// Test asynchrone BITPOS
TEST_F(RedisTest, ASYNC_BITMAP_COMMANDS_BITPOS) {
    std::string key = test_key("async_bitpos");
    long long   pos = 0;

    redis.set(key, std::string("\x00\xff\x00", 2));

    redis.bitpos([&](auto &&reply) { pos = reply.result(); }, key, true);

    redis.await();

    EXPECT_EQ(pos, 8);
}

// Test asynchrone GETBIT et SETBIT
TEST_F(RedisTest, ASYNC_BITMAP_COMMANDS_GETBIT_SETBIT) {
    std::string key           = test_key("async_getbit_setbit");
    bool        getbit_result = false;
    bool        setbit_result = false;

    redis.setbit([&](auto &&reply) { setbit_result = reply.result() == 0; }, key, 7,
                 true);

    redis.getbit([&](auto &&reply) { getbit_result = reply.result() == 1; }, key, 7);

    redis.await();

    EXPECT_TRUE(setbit_result);
    EXPECT_TRUE(getbit_result);
}