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
#define REDIS_URI \
    { "tcp://localhost:6379" }

using namespace qb::io;
using namespace std::chrono;

// Helper function to generate unique key prefixes
inline std::string
key_prefix(const std::string &key = "") {
    static int counter = 0;
    std::string prefix = "qb::redis::hyperloglog-test:" + std::to_string(++counter);
    
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

// Test fixture for Redis HyperLogLog commands
class RedisHyperLogLogTest : public ::testing::Test {
protected:
    qb::redis::tcp::client redis{REDIS_URI};
    
    void SetUp() override {
        async::init();
        if (!redis.connect() || !redis.flushall())
            throw std::runtime_error("Failed to connect to Redis");

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

// Test PFADD
TEST_F(RedisHyperLogLogTest, SYNC_HYPERLOGLOG_PFADD) {
    std::string key = test_key("pfadd");
    
    // Test adding single element
    EXPECT_TRUE(redis.pfadd(key, "element1"));
    
    // Test adding multiple elements
    EXPECT_TRUE(redis.pfadd(key, "element2", "element3", "element4"));
    
    // Test adding duplicate elements (should not affect cardinality)
    EXPECT_FALSE(redis.pfadd(key, "element1", "element2"));
}

// Test PFCOUNT
TEST_F(RedisHyperLogLogTest, SYNC_HYPERLOGLOG_PFCOUNT) {
    std::string key1 = test_key("pfcount1");
    std::string key2 = test_key("pfcount2");
    
    // Add elements to first HyperLogLog
    redis.pfadd(key1, "element1", "element2", "element3");
    
    // Add elements to second HyperLogLog
    redis.pfadd(key2, "element3", "element4", "element5");
    
    // Test counting single HyperLogLog
    EXPECT_EQ(redis.pfcount(key1), 3);
    
    // Test counting multiple HyperLogLogs (union)
    EXPECT_EQ(redis.pfcount(key1, key2), 5);
}

// Test PFMERGE
TEST_F(RedisHyperLogLogTest, SYNC_HYPERLOGLOG_PFMERGE) {
    std::string key1 = test_key("pfmerge1");
    std::string key2 = test_key("pfmerge2");
    std::string destkey = test_key("pfmerge_dest");
    
    // Add elements to source HyperLogLogs
    redis.pfadd(key1, "element1", "element2", "element3");
    redis.pfadd(key2, "element3", "element4", "element5");
    
    // Merge HyperLogLogs
    EXPECT_TRUE(redis.pfmerge(destkey, key1, key2));
    
    // Verify merged result
    EXPECT_EQ(redis.pfcount(destkey), 5);
}

/*
 * ASYNCHRONOUS TESTS
 */

// Test async PFADD
TEST_F(RedisHyperLogLogTest, ASYNC_HYPERLOGLOG_PFADD) {
    std::string key = test_key("async_pfadd");
    bool result = false;
    
    redis.pfadd([&](auto &&reply) {
        result = reply.ok();
    }, key, "element1", "element2", "element3");
    
    redis.await();
    EXPECT_TRUE(result);
}

// Test async PFCOUNT
TEST_F(RedisHyperLogLogTest, ASYNC_HYPERLOGLOG_PFCOUNT) {
    std::string key1 = test_key("async_pfcount1");
    std::string key2 = test_key("async_pfcount2");
    long long count = 0;
    
    // Add elements to HyperLogLogs
    redis.pfadd(key1, "element1", "element2", "element3");
    redis.pfadd(key2, "element3", "element4", "element5");
    
    redis.pfcount([&](auto &&reply) {
        count = reply.result();
    }, key1, key2);
    
    redis.await();
    EXPECT_EQ(count, 5);
}

// Test async PFMERGE
TEST_F(RedisHyperLogLogTest, ASYNC_HYPERLOGLOG_PFMERGE) {
    std::string key1 = test_key("async_pfmerge1");
    std::string key2 = test_key("async_pfmerge2");
    std::string destkey = test_key("async_pfmerge_dest");
    bool result = false;
    
    // Add elements to source HyperLogLogs
    redis.pfadd(key1, "element1", "element2", "element3");
    redis.pfadd(key2, "element3", "element4", "element5");
    
    redis.pfmerge([&](auto &&reply) {
        result = reply.ok();
    }, destkey, key1, key2);
    
    redis.await();
    EXPECT_TRUE(result);
    
    // Verify merged result
    long long count = 0;
    redis.pfcount([&](auto &&reply) {
        count = reply.result();
    }, destkey);
    
    redis.await();
    EXPECT_EQ(count, 5);
} 