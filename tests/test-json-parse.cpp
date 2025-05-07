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
#include <qb/json.h>
#include "../redis.h"

// Redis Configuration
#define REDIS_URI {"tcp://localhost:6379"}

using namespace qb::io;
using namespace std::chrono;

// Helper function to generate unique key prefixes
inline std::string
key_prefix(const std::string &key = "") {
    static int  counter = 0;
    std::string prefix  = "qb::redis::json-test:" + std::to_string(++counter);

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

// Test fixture for Redis JSON parsing
class RedisJsonTest : public ::testing::Test {
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

// Test parsing string values to JSON
TEST_F(RedisJsonTest, SYNC_JSON_PARSE_STRING) {
    std::string key = test_key("string");
    std::string json_string = "{\"name\":\"John\",\"age\":30,\"city\":\"New York\"}";
    
    // Store JSON string in Redis
    EXPECT_TRUE(redis.set(key, json_string));
    
    // Get the value and parse it as JSON
    auto result = redis.eval<qb::json>("return redis.call('GET', KEYS[1])", {key});
    
    // Verify the JSON structure
    EXPECT_TRUE(result.is_object());
    EXPECT_EQ(result["name"], "John");
    EXPECT_EQ(result["age"], 30);
    EXPECT_EQ(result["city"], "New York");
}

// Test parsing integer values to JSON
TEST_F(RedisJsonTest, SYNC_JSON_PARSE_INTEGER) {
    std::string key = test_key("integer");
    
    // Store integer in Redis
    EXPECT_TRUE(redis.set(key, "42"));
    
    // Get the value and parse it as JSON
    auto result = redis.eval<qb::json>("return tonumber(redis.call('GET', KEYS[1]))", {key});
    
    // Verify the JSON structure
    EXPECT_TRUE(result.is_number());
    EXPECT_EQ(result, 42);
}

// Test parsing array values to JSON
TEST_F(RedisJsonTest, SYNC_JSON_PARSE_ARRAY) {
    std::string key = test_key("array");
    
    // Add elements to a list
    redis.lpush(key, "item1", "item2", "item3");
    
    // Get the list as JSON array
    auto result = redis.eval<qb::json>("return redis.call('LRANGE', KEYS[1], 0, -1)", {key});
    
    // Verify the JSON structure
    EXPECT_TRUE(result.is_array());
    EXPECT_EQ(result.size(), 3);
    EXPECT_EQ(result[0], "item3"); // LPUSH pushes to the front
    EXPECT_EQ(result[1], "item2");
    EXPECT_EQ(result[2], "item1");
}

// Test parsing hash values to JSON
TEST_F(RedisJsonTest, SYNC_JSON_PARSE_HASH) {
    std::string key = test_key("hash");
    
    // Create a hash
    redis.hset(key, "name", "Alice");
    redis.hset(key, "age", "25");
    redis.hset(key, "city", "London");
    
    // Get the hash as JSON object
    auto result = redis.eval<qb::json>("return redis.call('HGETALL', KEYS[1])", {key});
    
    // Verify the JSON structure
    EXPECT_TRUE(result.is_object());
    EXPECT_EQ(result["name"], "Alice");
    // Numbers in string form get converted to numbers
    EXPECT_EQ(result["age"], 25);
    EXPECT_EQ(result["city"], "London");
}

// Test parsing complex nested structures to JSON
TEST_F(RedisJsonTest, SYNC_JSON_PARSE_COMPLEX) {
    // Use Lua to create a complex nested structure
    std::string script = R"(
        local result = {}
        result.string = "Hello World"
        result.number = 42
        result.boolean = true
        result.array = {"one", "two", "three"}
        result.object = {key1 = "value1", key2 = "value2"}
        result.nested = {
            array = {1, 2, 3},
            sub = {nested = "value"}
        }
        return cjson.encode(result)
    )";
    
    // Execute the script and get the result as JSON
    auto result = redis.eval<qb::json>(script);
    
    // Verify the JSON structure
    EXPECT_TRUE(result.is_object());
    EXPECT_EQ(result["string"], "Hello World");
    EXPECT_EQ(result["number"], 42);
    EXPECT_EQ(result["boolean"], true);
    
    EXPECT_TRUE(result["array"].is_array());
    EXPECT_EQ(result["array"].size(), 3);
    EXPECT_EQ(result["array"][0], "one");
    
    EXPECT_TRUE(result["object"].is_object());
    EXPECT_EQ(result["object"]["key1"], "value1");
    
    EXPECT_TRUE(result["nested"].is_object());
    EXPECT_TRUE(result["nested"]["array"].is_array());
    EXPECT_EQ(result["nested"]["sub"]["nested"], "value");
}

/*
 * ASYNCHRONOUS TESTS
 */

// Test async parsing string values to JSON
TEST_F(RedisJsonTest, ASYNC_JSON_PARSE_STRING) {
    std::string key = test_key("async_string");
    std::string json_string = "{\"name\":\"Jane\",\"age\":28,\"city\":\"Boston\"}";
    qb::json result;
    bool response_received = false;
    
    // Store JSON string in Redis
    redis.set(key, json_string);
    
    // Get the value and parse it as JSON asynchronously
    redis.eval<qb::json>(
        [&](auto &&reply) {
            result = reply.result();
            response_received = true;
        },
        "return redis.call('GET', KEYS[1])", 
        {key}
    );
    
    redis.await();
    
    // Verify the response was received and parsed correctly
    EXPECT_TRUE(response_received);
    EXPECT_TRUE(result.is_object());
    EXPECT_EQ(result["name"], "Jane");
    EXPECT_EQ(result["age"], 28);
    EXPECT_EQ(result["city"], "Boston");
}

// Test async parsing hash values to JSON
TEST_F(RedisJsonTest, ASYNC_JSON_PARSE_HASH) {
    std::string key = test_key("async_hash");
    qb::json result;
    bool response_received = false;
    
    // Create a hash
    redis.hset(key, "product", "Laptop");
    redis.hset(key, "price", "999.99");
    redis.hset(key, "available", "true");
    
    // Get the hash as JSON object asynchronously
    redis.eval<qb::json>(
        [&](auto &&reply) {
            result = reply.result();
            response_received = true;
        },
        "return redis.call('HGETALL', KEYS[1])", 
        {key}
    );
    
    redis.await();
    
    // Verify the response was received and parsed correctly
    EXPECT_TRUE(response_received);
    EXPECT_TRUE(result.is_object());
    EXPECT_EQ(result["product"], "Laptop");
    // Numbers and booleans in string form get converted to actual types
    EXPECT_EQ(result["price"], 999.99);
    EXPECT_EQ(result["available"], true);
}

// Main function
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
} 