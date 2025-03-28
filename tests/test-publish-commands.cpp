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

#define TEST_CHANNEL "test_channel"
#define TEST_MESSAGE "Hello World"

using namespace qb::io;
using namespace std::chrono;

// Verifies connection and cleans the environment before tests
class RedisPublishTest : public ::testing::Test {
protected:
    qb::redis::tcp::client redis{REDIS_URI};
    qb::redis::tcp::client publisher{REDIS_URI};
    qb::redis::tcp::cb_consumer consumer{REDIS_URI, [](auto&&) {}};

    void SetUp() override {
        async::init();
        if (!redis.connect() || !redis.flushall() || !publisher.connect() || !consumer.connect())
            throw std::runtime_error("Unable to connect to Redis");

        // Wait for the connection to be established
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

// =============== BASIC PUBLISH TESTS ===============

TEST(Redis, SYNC_PUBLISH_BASIC) {
    async::init();
    
    qb::redis::tcp::client publisher{REDIS_URI};
    std::atomic<size_t> message_count{0};
    
    // Create consumer with message callback
    qb::redis::tcp::cb_consumer consumer{REDIS_URI, [&](auto&& msg) {
        EXPECT_EQ(msg.message, TEST_MESSAGE);
        ++message_count;
    }};
    
    // Connect publisher and consumer
    ASSERT_TRUE(publisher.connect());
    ASSERT_TRUE(consumer.connect());
    
    // Subscribe to test channel
    auto subscribe_result = consumer.subscribe(TEST_CHANNEL);
    ASSERT_TRUE(subscribe_result.channel.has_value());
    ASSERT_EQ(*subscribe_result.channel, TEST_CHANNEL);
    
    // Test publishing with no subscribers (should return 0)
    EXPECT_EQ(publisher.publish(TEST_CHANNEL, TEST_MESSAGE), 1);
    
    // Allow time for message processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    qb::io::async::run(EVRUN_NOWAIT);
    
    // Verify message was received
    EXPECT_EQ(message_count, 1);
    
    // Clean up
    consumer.unsubscribe(TEST_CHANNEL);
}

// =============== PATTERN PUBLISH TESTS ===============

TEST(Redis, SYNC_PUBLISH_PATTERN) {
    async::init();
    
    qb::redis::tcp::client publisher{REDIS_URI};
    std::atomic<size_t> message_count{0};
    
    // Create consumer with message callback
    qb::redis::tcp::cb_consumer consumer{REDIS_URI, [&](auto&& msg) {
        EXPECT_EQ(msg.message, TEST_MESSAGE);
        ++message_count;
    }};
    
    // Connect publisher and consumer
    ASSERT_TRUE(publisher.connect());
    ASSERT_TRUE(consumer.connect());
    
    // Subscribe to test pattern
    auto subscribe_result = consumer.psubscribe("test_*");
    ASSERT_TRUE(subscribe_result.channel.has_value());
    
    // Publish to a channel matching the pattern
    EXPECT_EQ(publisher.publish("test_channel", TEST_MESSAGE), 1);
    
    // Allow time for message processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    qb::io::async::run(EVRUN_NOWAIT);
    
    // Verify message was received
    EXPECT_EQ(message_count, 1);
    
    // Clean up
    consumer.punsubscribe("test_*");
}

// =============== MULTIPLE SUBSCRIBERS TESTS ===============

TEST(Redis, SYNC_PUBLISH_MULTIPLE_SUBSCRIBERS) {
    async::init();
    
    qb::redis::tcp::client publisher{REDIS_URI};
    std::atomic<size_t> message_count1{0};
    std::atomic<size_t> message_count2{0};
    
    // Create two consumers with message callbacks
    qb::redis::tcp::cb_consumer consumer1{REDIS_URI, [&](auto&& msg) {
        EXPECT_EQ(msg.message, TEST_MESSAGE);
        ++message_count1;
    }};
    
    qb::redis::tcp::cb_consumer consumer2{REDIS_URI, [&](auto&& msg) {
        EXPECT_EQ(msg.message, TEST_MESSAGE);
        ++message_count2;
    }};
    
    // Connect publisher and consumers
    ASSERT_TRUE(publisher.connect());
    ASSERT_TRUE(consumer1.connect());
    ASSERT_TRUE(consumer2.connect());
    
    // Subscribe both consumers to test channel
    auto subscribe_result1 = consumer1.subscribe(TEST_CHANNEL);
    auto subscribe_result2 = consumer2.subscribe(TEST_CHANNEL);
    
    ASSERT_TRUE(subscribe_result1.channel.has_value());
    ASSERT_TRUE(subscribe_result2.channel.has_value());
    
    // Publish message (should be received by both subscribers)
    EXPECT_EQ(publisher.publish(TEST_CHANNEL, TEST_MESSAGE), 2);
    
    // Allow time for message processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    qb::io::async::run(EVRUN_NOWAIT);
    
    // Verify messages were received by both consumers
    EXPECT_EQ(message_count1, 1);
    EXPECT_EQ(message_count2, 1);
    
    // Clean up
    consumer1.unsubscribe(TEST_CHANNEL);
    consumer2.unsubscribe(TEST_CHANNEL);
}

// =============== EDGE CASES ===============

TEST(Redis, SYNC_PUBLISH_EMPTY_CHANNEL) {
    async::init();
    
    qb::redis::tcp::client publisher{REDIS_URI};
    ASSERT_TRUE(publisher.connect());
    
    // Publishing to an empty channel should return 0 (no subscribers)
    EXPECT_EQ(publisher.publish("", TEST_MESSAGE), 0);
}

TEST(Redis, SYNC_PUBLISH_EMPTY_MESSAGE) {
    async::init();
    
    qb::redis::tcp::client publisher{REDIS_URI};
    std::atomic<size_t> message_count{0};
    
    // Create consumer with message callback
    qb::redis::tcp::cb_consumer consumer{REDIS_URI, [&](auto&& msg) {
        EXPECT_EQ(msg.message, "");
        ++message_count;
    }};
    
    // Connect publisher and consumer
    ASSERT_TRUE(publisher.connect());
    ASSERT_TRUE(consumer.connect());
    
    // Subscribe to test channel
    auto subscribe_result = consumer.subscribe(TEST_CHANNEL);
    ASSERT_TRUE(subscribe_result.channel.has_value());
    
    // Publishing an empty message should work
    EXPECT_EQ(publisher.publish(TEST_CHANNEL, ""), 1);
    
    // Allow time for message processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    qb::io::async::run(EVRUN_NOWAIT);
    
    // Verify empty message was received
    EXPECT_EQ(message_count, 1);
    
    // Clean up
    consumer.unsubscribe(TEST_CHANNEL);
}

/*
 * ASYNCHRONOUS TESTS
 */

// =============== BASIC PUBLISH TESTS ===============

TEST(Redis, ASYNC_PUBLISH_BASIC) {
    async::init();
    
    qb::redis::tcp::client publisher{REDIS_URI};
    std::atomic<size_t> message_count{0};
    
    // Create consumer with message callback
    qb::redis::tcp::cb_consumer consumer{REDIS_URI, [&](auto&& msg) {
        EXPECT_EQ(msg.message, TEST_MESSAGE);
        ++message_count;
    }};
    
    // Connect publisher and consumer
    ASSERT_TRUE(publisher.connect());
    ASSERT_TRUE(consumer.connect());
    
    // Subscribe to test channel
    auto subscribe_status = false;
    consumer.subscribe([&subscribe_status](auto&& reply) {
        EXPECT_TRUE(reply.ok());
        EXPECT_TRUE(reply.result().channel.has_value());
        EXPECT_EQ(*reply.result().channel, TEST_CHANNEL);
        subscribe_status = true;
    }, TEST_CHANNEL);
    
    while (!subscribe_status) {
        qb::io::async::run(EVRUN_ONCE);
    }
    
    // Test publishing with no subscribers (should return 0)
    bool publish_complete = false;
    publisher.publish([&](auto&& reply) {
        EXPECT_TRUE(reply.ok());
        EXPECT_EQ(reply.result(), 1);
        publish_complete = true;
    }, TEST_CHANNEL, TEST_MESSAGE);
    
    while (!publish_complete) {
        qb::io::async::run(EVRUN_ONCE);
    }
    
    // Allow time for message processing
    for (int i = 0; i < 10 && message_count < 1; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        qb::io::async::run(EVRUN_NOWAIT);
    }
    
    // Verify message was received
    EXPECT_EQ(message_count, 1);
    
    // Clean up
    consumer.unsubscribe(TEST_CHANNEL);
}

// =============== PATTERN PUBLISH TESTS ===============

TEST(Redis, ASYNC_PUBLISH_PATTERN) {
    async::init();
    
    qb::redis::tcp::client publisher{REDIS_URI};
    std::atomic<size_t> message_count{0};
    
    // Create consumer with message callback
    qb::redis::tcp::cb_consumer consumer{REDIS_URI, [&](auto&& msg) {
        EXPECT_EQ(msg.message, TEST_MESSAGE);
        ++message_count;
    }};
    
    // Connect publisher and consumer
    ASSERT_TRUE(publisher.connect());
    ASSERT_TRUE(consumer.connect());
    
    // Subscribe to test pattern
    auto subscribe_status = false;
    consumer.psubscribe([&subscribe_status](auto&& reply) {
        EXPECT_TRUE(reply.ok());
        EXPECT_TRUE(reply.result().channel.has_value());
        subscribe_status = true;
    }, "test_*");
    
    while (!subscribe_status) {
        qb::io::async::run(EVRUN_ONCE);
    }
    
    // Publish to a channel matching the pattern
    bool publish_complete = false;
    publisher.publish([&](auto&& reply) {
        EXPECT_TRUE(reply.ok());
        EXPECT_EQ(reply.result(), 1);
        publish_complete = true;
    }, "test_channel", TEST_MESSAGE);
    
    while (!publish_complete) {
        qb::io::async::run(EVRUN_ONCE);
    }
    
    // Allow time for message processing
    for (int i = 0; i < 10 && message_count < 1; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        qb::io::async::run(EVRUN_NOWAIT);
    }
    
    // Verify message was received
    EXPECT_EQ(message_count, 1);
    
    // Clean up
    consumer.punsubscribe("test_*");
}

// =============== EDGE CASES ===============

TEST(Redis, ASYNC_PUBLISH_EMPTY_CHANNEL) {
    async::init();
    
    qb::redis::tcp::client publisher{REDIS_URI};
    ASSERT_TRUE(publisher.connect());
    
    // Publishing to an empty channel should return 0 (no subscribers)
    bool publish_complete = false;
    publisher.publish([&](auto&& reply) {
        EXPECT_TRUE(reply.ok());
        EXPECT_EQ(reply.result(), 0);
        publish_complete = true;
    }, "", TEST_MESSAGE);
    
    while (!publish_complete) {
        qb::io::async::run(EVRUN_ONCE);
    }
}

TEST(Redis, ASYNC_PUBLISH_EMPTY_MESSAGE) {
    async::init();
    
    qb::redis::tcp::client publisher{REDIS_URI};
    std::atomic<size_t> message_count{0};
    
    // Create consumer with message callback
    qb::redis::tcp::cb_consumer consumer{REDIS_URI, [&](auto&& msg) {
        EXPECT_EQ(msg.message, "");
        ++message_count;
    }};
    
    // Connect publisher and consumer
    ASSERT_TRUE(publisher.connect());
    ASSERT_TRUE(consumer.connect());
    
    // Subscribe to test channel
    auto subscribe_status = false;
    consumer.subscribe([&subscribe_status](auto&& reply) {
        EXPECT_TRUE(reply.ok());
        EXPECT_TRUE(reply.result().channel.has_value());
        EXPECT_EQ(*reply.result().channel, TEST_CHANNEL);
        subscribe_status = true;
    }, TEST_CHANNEL);
    
    while (!subscribe_status) {
        qb::io::async::run(EVRUN_ONCE);
    }
    
    // Publishing an empty message should work
    bool publish_complete = false;
    publisher.publish([&](auto&& reply) {
        EXPECT_TRUE(reply.ok());
        EXPECT_EQ(reply.result(), 1);
        publish_complete = true;
    }, TEST_CHANNEL, "");
    
    while (!publish_complete) {
        qb::io::async::run(EVRUN_ONCE);
    }
    
    // Allow time for message processing
    for (int i = 0; i < 10 && message_count < 1; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        qb::io::async::run(EVRUN_NOWAIT);
    }
    
    // Verify empty message was received
    EXPECT_EQ(message_count, 1);
    
    // Clean up
    consumer.unsubscribe(TEST_CHANNEL);
} 