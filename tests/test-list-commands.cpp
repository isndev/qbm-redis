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
    std::string prefix = "qb::redis::list-test:" + std::to_string(++counter);
    
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

// Test basic LPUSH, RPUSH, LLEN operations
TEST_F(RedisTest, SYNC_LIST_COMMANDS_PUSH) {
    std::string key = test_key("basic");
    
    // Test LPUSH (left push)
    EXPECT_EQ(redis.lpush(key, "item1"), 1);
    EXPECT_EQ(redis.lpush(key, "item2"), 2);
    EXPECT_EQ(redis.lpush(key, "item3"), 3);
    
    // Test LLEN (list length)
    EXPECT_EQ(redis.llen(key), 3);
    
    // Test RPUSH (right push)
    EXPECT_EQ(redis.rpush(key, "item4"), 4);
    EXPECT_EQ(redis.rpush(key, "item5"), 5);
    
    // Verify length again
    EXPECT_EQ(redis.llen(key), 5);
    
    // Cleanup
    redis.del(key);
}

// Test LPOP, RPOP operations
TEST_F(RedisTest, SYNC_LIST_COMMANDS_POP) {
    std::string key = test_key("pop");
    
    // Setup list
    redis.rpush(key, "item1", "item2", "item3", "item4", "item5");
    
    // Test LPOP (pop from left)
    auto left_item = redis.lpop(key);
    EXPECT_TRUE(left_item.has_value());
    EXPECT_EQ(*left_item, "item1");
    
    // Test RPOP (pop from right)
    auto right_item = redis.rpop(key);
    EXPECT_TRUE(right_item.has_value());
    EXPECT_EQ(*right_item, "item5");
    
    // Verify length after pops
    EXPECT_EQ(redis.llen(key), 3);
    
    // Test multiple LPOP
    auto left_items = redis.lpop(key, 2);
    EXPECT_EQ(left_items.size(), 2);
    EXPECT_EQ(left_items[0], "item2");
    EXPECT_EQ(left_items[1], "item3");
    
    // Verify length again
    EXPECT_EQ(redis.llen(key), 1);
    
    // Test LPOP on empty list after removing last item
    redis.lpop(key);
    auto empty_pop = redis.lpop(key);
    EXPECT_FALSE(empty_pop.has_value());
    
    // Cleanup
    redis.del(key);
}

// Test LRANGE operation
TEST_F(RedisTest, SYNC_LIST_COMMANDS_RANGE) {
    std::string key = test_key("range");
    
    // Setup list
    redis.rpush(key, "item1", "item2", "item3", "item4", "item5");
    
    // Test LRANGE
    auto all_items = redis.lrange(key, 0, -1);
    EXPECT_EQ(all_items.size(), 5);
    EXPECT_EQ(all_items[0], "item1");
    EXPECT_EQ(all_items[4], "item5");
    
    // Test partial range
    auto subset = redis.lrange(key, 1, 3);
    EXPECT_EQ(subset.size(), 3);
    EXPECT_EQ(subset[0], "item2");
    EXPECT_EQ(subset[2], "item4");
    
    // Test negative indices
    auto last_two = redis.lrange(key, -2, -1);
    EXPECT_EQ(last_two.size(), 2);
    EXPECT_EQ(last_two[0], "item4");
    EXPECT_EQ(last_two[1], "item5");
    
    // Cleanup
    redis.del(key);
}

// Test LSET, LINDEX operations
TEST_F(RedisTest, SYNC_LIST_COMMANDS_INDEX) {
    std::string key = test_key("index");
    
    // Setup list
    redis.rpush(key, "item1", "item2", "item3", "item4", "item5");
    
    // Test LINDEX
    auto item = redis.lindex(key, 2);
    EXPECT_TRUE(item.has_value());
    EXPECT_EQ(*item, "item3");
    
    // Test negative index
    auto last_item = redis.lindex(key, -1);
    EXPECT_TRUE(last_item.has_value());
    EXPECT_EQ(*last_item, "item5");
    
    // Test LSET
    EXPECT_TRUE(redis.lset(key, 1, "replaced"));
    
    // Verify the change
    auto modified_item = redis.lindex(key, 1);
    EXPECT_TRUE(modified_item.has_value());
    EXPECT_EQ(*modified_item, "replaced");
    
    // Test invalid index
    EXPECT_THROW(redis.lset(key, 10, "invalid"), std::exception);
    
    // Cleanup
    redis.del(key);
}

// Test LTRIM operation
TEST_F(RedisTest, SYNC_LIST_COMMANDS_TRIM) {
    std::string key = test_key("trim");
    
    // Setup list
    redis.rpush(key, "item1", "item2", "item3", "item4", "item5");
    
    // Test LTRIM
    EXPECT_TRUE(redis.ltrim(key, 1, 3));
    
    // Verify the result
    auto items = redis.lrange(key, 0, -1);
    EXPECT_EQ(items.size(), 3);
    EXPECT_EQ(items[0], "item2");
    EXPECT_EQ(items[2], "item4");
    
    // Cleanup
    redis.del(key);
}

// Test LREM operation
TEST_F(RedisTest, SYNC_LIST_COMMANDS_REMOVE) {
    std::string key = test_key("remove");
    
    // Setup list with duplicates
    redis.rpush(key, "item1", "item2", "item3", "item2", "item4", "item2", "item5");
    
    // Test LREM (remove 2 occurrences of "item2")
    EXPECT_EQ(redis.lrem(key, 2, "item2"), 2);
    
    // Verify the result
    auto items = redis.lrange(key, 0, -1);
    EXPECT_EQ(items.size(), 5);
    EXPECT_EQ(std::count(items.begin(), items.end(), "item2"), 1);
    
    // Test LREM with negative count (remove from right)
    EXPECT_EQ(redis.lrem(key, -1, "item2"), 1);
    
    // Verify all "item2" are gone
    items = redis.lrange(key, 0, -1);
    EXPECT_EQ(std::count(items.begin(), items.end(), "item2"), 0);
    
    // Cleanup
    redis.del(key);
}

// Test blocking operations (BLPOP, BRPOP)
TEST_F(RedisTest, SYNC_LIST_COMMANDS_BLOCKING) {
    std::string key1 = test_key("blocking1");
    std::string key2 = test_key("blocking2");
    
    // Setup list for key1
    redis.rpush(key1, "item1");
    
    // Test BLPOP with timeout
    auto result = redis.blpop({key1, key2}, 1);
    EXPECT_TRUE(result.has_value());
    if (result.has_value()) {
        EXPECT_EQ(result->key, key1);
        EXPECT_EQ(result->element, "item1");
    }
    
    // Test BLPOP on empty lists with short timeout
    auto empty_result = redis.blpop({key1, key2}, 1);
    EXPECT_FALSE(empty_result.has_value());
    
    // Test BRPOP
    redis.rpush(key2, "item2");
    auto rpop_result = redis.brpop({key1, key2}, 1);
    EXPECT_TRUE(rpop_result.has_value());
    if (rpop_result.has_value()) {
        EXPECT_EQ(rpop_result->key, key2);
        EXPECT_EQ(rpop_result->element, "item2");
    }
    
    // Cleanup
    redis.del(key1, key2);
}

/*
 * ASYNCHRONOUS TESTS
 */

// Test basic push operations asynchronously
TEST_F(RedisTest, ASYNC_LIST_COMMANDS_PUSH) {
    std::string key = test_key("async-push");
    bool lpush_called = false;
    bool rpush_called = false;
    bool llen_called = false;
    
    // Test LPUSH async
    redis.lpush(
        [&lpush_called](auto&& reply) {
            EXPECT_TRUE(reply.ok);
            EXPECT_EQ(reply.result, 1);
            lpush_called = true;
        },
        key,
        "item1"
    );
    
    redis.await();
    EXPECT_TRUE(lpush_called);
    
    // Test RPUSH async
    redis.rpush(
        [&rpush_called](auto&& reply) {
            EXPECT_TRUE(reply.ok);
            EXPECT_EQ(reply.result, 2);
            rpush_called = true;
        },
        key,
        "item2"
    );
    
    redis.await();
    EXPECT_TRUE(rpush_called);
    
    // Test LLEN async
    redis.llen(
        [&llen_called](auto&& reply) {
            EXPECT_TRUE(reply.ok);
            EXPECT_EQ(reply.result, 2);
            llen_called = true;
        },
        key
    );
    
    redis.await();
    EXPECT_TRUE(llen_called);
    
    // Cleanup
    redis.del(key);
}

// Test pop operations asynchronously
TEST_F(RedisTest, ASYNC_LIST_COMMANDS_POP) {
    std::string key = test_key("async-pop");
    bool setup_called = false;
    bool lpop_called = false;
    bool rpop_called = false;
    
    // Setup list asynchronously
    redis.rpush(
        [&setup_called](auto&& reply) {
            EXPECT_TRUE(reply.ok);
            EXPECT_EQ(reply.result, 3);
            setup_called = true;
        },
        key,
        "item1", "item2", "item3"
    );
    
    redis.await();
    EXPECT_TRUE(setup_called);
    
    // Test LPOP async
    redis.lpop(
        [&lpop_called](auto&& reply) {
            EXPECT_TRUE(reply.ok);
            EXPECT_TRUE(reply.result.has_value());
            EXPECT_EQ(*reply.result, "item1");
            lpop_called = true;
        },
        key
    );
    
    redis.await();
    EXPECT_TRUE(lpop_called);
    
    // Test RPOP async
    redis.rpop(
        [&rpop_called](auto&& reply) {
            EXPECT_TRUE(reply.ok);
            EXPECT_TRUE(reply.result.has_value());
            EXPECT_EQ(*reply.result, "item3");
            rpop_called = true;
        },
        key
    );
    
    redis.await();
    EXPECT_TRUE(rpop_called);
    
    // Cleanup
    redis.del(key);
}

// Test range operations asynchronously
TEST_F(RedisTest, ASYNC_LIST_COMMANDS_RANGE) {
    std::string key = test_key("async-range");
    bool setup_called = false;
    bool lrange_called = false;
    
    // Setup list asynchronously
    redis.rpush(
        [&setup_called](auto&& reply) {
            EXPECT_TRUE(reply.ok);
            EXPECT_EQ(reply.result, 5);
            setup_called = true;
        },
        key,
        "item1", "item2", "item3", "item4", "item5"
    );
    
    redis.await();
    EXPECT_TRUE(setup_called);
    
    // Test LRANGE async
    redis.lrange(
        [&lrange_called](auto&& reply) {
            EXPECT_TRUE(reply.ok);
            EXPECT_EQ(reply.result.size(), 3);
            EXPECT_EQ(reply.result[0], "item2");
            EXPECT_EQ(reply.result[2], "item4");
            lrange_called = true;
        },
        key,
        1, 3
    );
    
    redis.await();
    EXPECT_TRUE(lrange_called);
    
    // Cleanup
    redis.del(key);
}

// Test command chaining instead of explicit pipeline
TEST_F(RedisTest, ASYNC_LIST_COMMANDS_CHAINING) {
    std::string key = test_key("list-chaining");
    bool all_commands_completed = false;
    int command_count = 0;
    
    // Setup callback to track completion
    auto completion_callback = [&command_count, &all_commands_completed](auto&&) {
        if (++command_count == 3) {
            all_commands_completed = true;
        }
    };
    
    // Chain multiple commands (they will be buffered and sent together)
    redis.lpush(
        [&](auto&& reply) {
            EXPECT_TRUE(reply.ok);
            EXPECT_EQ(reply.result, 1);
            completion_callback(reply);
        },
        key,
        "item1"
    );
    
    redis.lpush(
        [&](auto&& reply) {
            EXPECT_TRUE(reply.ok);
            EXPECT_EQ(reply.result, 2);
            completion_callback(reply);
        },
        key,
        "item2"
    );
    
    redis.rpush(
        [&](auto&& reply) {
            EXPECT_TRUE(reply.ok);
            EXPECT_EQ(reply.result, 3);
            completion_callback(reply);
        },
        key,
        "item3"
    );
    
    // Trigger the async operations and wait for completion
    redis.await();
    EXPECT_TRUE(all_commands_completed);
    
    // Verify the results with synchronous call
    auto items = redis.lrange(key, 0, -1);
    EXPECT_EQ(items.size(), 3);
    EXPECT_EQ(items[0], "item2");
    EXPECT_EQ(items[1], "item1");
    EXPECT_EQ(items[2], "item3");
    
    // Cleanup
    redis.del(key);
}

// Main function to run the tests
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
} 