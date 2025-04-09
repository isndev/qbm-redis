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

#define TEST_CHANNEL "test_channel"
#define TEST_PATTERN "test_pattern*"
#define TEST_MESSAGE "Hello World"

using namespace qb::io;
using namespace std::chrono;

// Verifies connection and cleans the environment before tests
class RedisSubscriptionTest : public ::testing::Test {
protected:
    qb::redis::tcp::client redis{REDIS_URI};
    qb::redis::tcp::client publisher{REDIS_URI};

    void
    SetUp() override {
        async::init();
        if (!redis.connect() || !redis.flushall() || !publisher.connect())
            throw std::runtime_error("Unable to connect to Redis");

        // Wait for the connection to be established
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

// =============== CHANNEL SUBSCRIPTION TESTS ===============

// This test uses cb_consumer for subscription testing
TEST(Redis, SYNC_SUBSCRIPTION_CHANNEL) {
    async::init();

    qb::redis::tcp::client   publisher{REDIS_URI};
    std::atomic<size_t>      message_count{0};
    std::vector<std::string> messages;
    std::mutex               mutex;

    // Create consumer with message callback
    qb::redis::tcp::cb_consumer consumer{REDIS_URI, [&](auto &&msg) {
                                             std::lock_guard<std::mutex> lock(mutex);
                                             messages.push_back(
                                                 std::string(msg.message));
                                             ++message_count;
                                         }};

    // Connect publisher and consumer
    ASSERT_TRUE(publisher.connect());
    ASSERT_TRUE(consumer.connect());

    // Subscribe to test channel
    auto subscribe_result = consumer.subscribe(TEST_CHANNEL);
    ASSERT_TRUE(subscribe_result.channel.has_value());
    ASSERT_EQ(*subscribe_result.channel, TEST_CHANNEL);

    // Publish a message and verify it was received
    EXPECT_GT(publisher.publish(TEST_CHANNEL, TEST_MESSAGE), 0);

    // Allow time for message processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    qb::io::async::run(EVRUN_NOWAIT);

    // Verify message was received
    EXPECT_EQ(message_count, 1);

    if (message_count > 0) {
        std::lock_guard<std::mutex> lock(mutex);
        EXPECT_EQ(messages[0], TEST_MESSAGE);
    }

    // Clean up
    consumer.unsubscribe(TEST_CHANNEL);
}

// =============== PATTERN SUBSCRIPTION TESTS ===============

// This test uses cb_consumer for pattern subscription testing
TEST(Redis, SYNC_SUBSCRIPTION_PATTERN) {
    async::init();

    qb::redis::tcp::client publisher{REDIS_URI};
    std::atomic<size_t>    message_count{0};

    // Create consumer with message callback
    qb::redis::tcp::cb_consumer consumer{REDIS_URI, [&](auto &&msg) {
                                             ++message_count;
                                             EXPECT_EQ(msg.message, TEST_MESSAGE);
                                         }};

    // Connect publisher and consumer
    ASSERT_TRUE(publisher.connect());
    ASSERT_TRUE(consumer.connect());

    // Subscribe to test pattern
    auto subscribe_result = consumer.psubscribe(TEST_PATTERN);
    ASSERT_TRUE(subscribe_result.channel.has_value());
    ASSERT_EQ(*subscribe_result.channel, TEST_PATTERN);

    // Publish messages to different channels matching the pattern
    EXPECT_GT(publisher.publish("test_pattern1", TEST_MESSAGE), 0);
    EXPECT_GT(publisher.publish("test_pattern2", TEST_MESSAGE), 0);

    // Allow time for message processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    qb::io::async::run(EVRUN_NOWAIT);

    // Verify messages were received
    EXPECT_EQ(message_count, 2);

    // Clean up
    consumer.punsubscribe(TEST_PATTERN);
}

// =============== SUBSCRIPTION MANAGEMENT TESTS ===============

TEST(Redis, SYNC_SUBSCRIPTION_MANAGEMENT) {
    async::init();

    // Create a consumer for testing subscription management
    qb::redis::tcp::cb_consumer consumer{REDIS_URI, [](auto &&) {}};
    ASSERT_TRUE(consumer.connect());

    // Test subscribe and unsubscribe
    auto subscribe_result = consumer.subscribe(TEST_CHANNEL);
    ASSERT_TRUE(subscribe_result.channel.has_value());
    ASSERT_EQ(*subscribe_result.channel, TEST_CHANNEL);
    ASSERT_GT(subscribe_result.num, 0);

    auto unsubscribe_result = consumer.unsubscribe(TEST_CHANNEL);
    ASSERT_TRUE(unsubscribe_result.channel.has_value());
    ASSERT_EQ(*unsubscribe_result.channel, TEST_CHANNEL);
    ASSERT_EQ(unsubscribe_result.num, 0);

    // Test vector version
    std::vector<std::string> channels = {TEST_CHANNEL, "another_channel"};
    for (const auto &channel : channels) {
        consumer.subscribe(channel);
    }

    // Unsubscribe from each channel individually
    for (const auto &channel : channels) {
        auto result = consumer.unsubscribe(channel);
        ASSERT_TRUE(result.channel.has_value());
        ASSERT_EQ(*result.channel, channel);
    }
}

TEST(Redis, SYNC_PATTERN_SUBSCRIPTION_MANAGEMENT) {
    async::init();

    // Create a consumer for testing pattern subscription management
    qb::redis::tcp::cb_consumer consumer{REDIS_URI, [](auto &&) {}};
    ASSERT_TRUE(consumer.connect());

    // Test psubscribe and punsubscribe
    auto psubscribe_result = consumer.psubscribe(TEST_PATTERN);
    ASSERT_TRUE(psubscribe_result.channel.has_value());
    ASSERT_EQ(*psubscribe_result.channel, TEST_PATTERN);
    ASSERT_GT(psubscribe_result.num, 0);

    auto punsubscribe_result = consumer.punsubscribe(TEST_PATTERN);
    ASSERT_TRUE(punsubscribe_result.channel.has_value());
    ASSERT_EQ(*punsubscribe_result.channel, TEST_PATTERN);
    ASSERT_EQ(punsubscribe_result.num, 0);

    // Test vector version
    std::vector<std::string> patterns = {TEST_PATTERN, "another_pattern*"};
    for (const auto &pattern : patterns) {
        consumer.psubscribe(pattern);
    }

    // Unsubscribe from each pattern individually
    for (const auto &pattern : patterns) {
        auto result = consumer.punsubscribe(pattern);
        ASSERT_TRUE(result.channel.has_value());
        ASSERT_EQ(*result.channel, pattern);
    }
}

// Test subscription with empty channel name
TEST(Redis, SYNC_SUBSCRIPTION_EMPTY_CHANNEL) {
    async::init();

    // Create a consumer for testing
    qb::redis::tcp::cb_consumer consumer{REDIS_URI, [](auto &&) {}};
    ASSERT_TRUE(consumer.connect());

    auto result = consumer.subscribe("");
    ASSERT_FALSE(result.channel.has_value());
    ASSERT_EQ(result.num, 0);

    result = consumer.psubscribe("");
    ASSERT_FALSE(result.channel.has_value());
    ASSERT_EQ(result.num, 0);
}

/*
 * ASYNCHRONOUS TESTS
 */

// =============== CHANNEL SUBSCRIPTION TESTS ===============

TEST(Redis, ASYNC_SUBSCRIPTION_CHANNEL) {
    async::init();

    qb::redis::tcp::client publisher{REDIS_URI};
    std::atomic<bool>      message_received{false};

    // Create consumer with message callback
    qb::redis::tcp::cb_consumer consumer{REDIS_URI, [&](auto &&msg) {
                                             EXPECT_EQ(msg.message, TEST_MESSAGE);
                                             message_received = true;
                                         }};

    // Connect publisher and consumer
    ASSERT_TRUE(publisher.connect());
    ASSERT_TRUE(consumer.connect());

    auto subscribe_status = false;
    consumer.subscribe(
        [&subscribe_status](auto &&reply) {
            EXPECT_TRUE(reply.ok());
            EXPECT_TRUE(reply.result().channel.has_value());
            EXPECT_EQ(*reply.result().channel, TEST_CHANNEL);
            subscribe_status = true;
        },
        TEST_CHANNEL);

    while (!subscribe_status) {
        qb::io::async::run(EVRUN_ONCE);
    }

    // Publish a message
    publisher.publish(TEST_CHANNEL, TEST_MESSAGE);

    // Allow time for message processing
    for (int i = 0; i < 10 && !message_received; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        qb::io::async::run(EVRUN_NOWAIT);
    }

    EXPECT_TRUE(message_received);
}

// =============== PATTERN SUBSCRIPTION TESTS ===============

TEST(Redis, ASYNC_SUBSCRIPTION_PATTERN) {
    async::init();

    qb::redis::tcp::client publisher{REDIS_URI};
    std::atomic<size_t>    message_count{0};

    // Create consumer with message callback
    qb::redis::tcp::cb_consumer consumer{REDIS_URI, [&](auto &&msg) {
                                             EXPECT_EQ(msg.message, TEST_MESSAGE);
                                             ++message_count;
                                         }};

    // Connect publisher and consumer
    ASSERT_TRUE(publisher.connect());
    ASSERT_TRUE(consumer.connect());

    auto subscribe_status = false;
    consumer.psubscribe(
        [&subscribe_status](auto &&reply) {
            EXPECT_TRUE(reply.ok());
            EXPECT_TRUE(reply.result().channel.has_value());
            EXPECT_EQ(*reply.result().channel, TEST_PATTERN);
            subscribe_status = true;
        },
        TEST_PATTERN);

    while (!subscribe_status) {
        qb::io::async::run(EVRUN_ONCE);
    }

    // Publish messages to different channels matching the pattern
    publisher.publish("test_pattern1", TEST_MESSAGE);
    publisher.publish("test_pattern2", TEST_MESSAGE);

    // Allow time for message processing
    for (int i = 0; i < 10 && message_count < 2; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        qb::io::async::run(EVRUN_NOWAIT);
    }

    EXPECT_EQ(message_count, 2);
}

// =============== SUBSCRIPTION MANAGEMENT TESTS ===============

TEST(Redis, ASYNC_SUBSCRIPTION_MANAGEMENT) {
    async::init();

    // Create a consumer for testing subscription management
    qb::redis::tcp::cb_consumer consumer{REDIS_URI, [](auto &&) {}};
    ASSERT_TRUE(consumer.connect());

    bool subscribe_complete   = false;
    bool unsubscribe_complete = false;

    // Test async subscribe and unsubscribe
    consumer.subscribe(
        [&](auto &&reply) {
            ASSERT_TRUE(reply.ok());
            ASSERT_TRUE(reply.result().channel.has_value());
            ASSERT_EQ(*reply.result().channel, TEST_CHANNEL);
            ASSERT_GT(reply.result().num, 0);
            subscribe_complete = true;
        },
        TEST_CHANNEL);

    while (!subscribe_complete) {
        qb::io::async::run(EVRUN_ONCE);
    }

    consumer.unsubscribe(
        [&](auto &&reply) {
            ASSERT_TRUE(reply.ok());
            ASSERT_TRUE(reply.result().channel.has_value());
            ASSERT_EQ(*reply.result().channel, TEST_CHANNEL);
            ASSERT_EQ(reply.result().num, 0);
            unsubscribe_complete = true;
        },
        TEST_CHANNEL);

    while (!unsubscribe_complete) {
        qb::io::async::run(EVRUN_ONCE);
    }

    // Test vector version
    subscribe_complete   = false;
    unsubscribe_complete = false;

    std::vector<std::string> channels = {TEST_CHANNEL, "another_channel"};

    // Subscribe to multiple channels sequentially
    for (const auto &channel : channels) {
        bool channel_subscribe_complete = false;
        consumer.subscribe(
            [&](auto &&reply) {
                ASSERT_TRUE(reply.ok());
                channel_subscribe_complete = true;
            },
            channel);

        while (!channel_subscribe_complete) {
            qb::io::async::run(EVRUN_ONCE);
        }
    }

    // Unsubscribe from each channel individually
    for (const auto &channel : channels) {
        auto result = consumer.unsubscribe(channel);
        ASSERT_TRUE(result.channel.has_value());
        ASSERT_EQ(*result.channel, channel);
    }
}

TEST(Redis, ASYNC_PATTERN_SUBSCRIPTION_MANAGEMENT) {
    async::init();

    // Create a consumer for testing pattern subscription management
    qb::redis::tcp::cb_consumer consumer{REDIS_URI, [](auto &&) {}};
    ASSERT_TRUE(consumer.connect());

    bool subscribe_complete   = false;
    bool unsubscribe_complete = false;

    // Test async psubscribe and punsubscribe
    consumer.psubscribe(
        [&](auto &&reply) {
            ASSERT_TRUE(reply.ok());
            ASSERT_TRUE(reply.result().channel.has_value());
            ASSERT_EQ(*reply.result().channel, TEST_PATTERN);
            ASSERT_GT(reply.result().num, 0);
            subscribe_complete = true;
        },
        TEST_PATTERN);

    while (!subscribe_complete) {
        qb::io::async::run(EVRUN_ONCE);
    }

    consumer.punsubscribe(
        [&](auto &&reply) {
            ASSERT_TRUE(reply.ok());
            ASSERT_TRUE(reply.result().channel.has_value());
            ASSERT_EQ(*reply.result().channel, TEST_PATTERN);
            ASSERT_EQ(reply.result().num, 0);
            unsubscribe_complete = true;
        },
        TEST_PATTERN);

    while (!unsubscribe_complete) {
        qb::io::async::run(EVRUN_ONCE);
    }

    // Test vector version
    subscribe_complete   = false;
    unsubscribe_complete = false;

    std::vector<std::string> patterns = {TEST_PATTERN, "another_pattern*"};

    // Subscribe to multiple patterns sequentially
    for (const auto &pattern : patterns) {
        bool pattern_subscribe_complete = false;
        consumer.psubscribe(
            [&](auto &&reply) {
                ASSERT_TRUE(reply.ok());
                pattern_subscribe_complete = true;
            },
            pattern);

        while (!pattern_subscribe_complete) {
            qb::io::async::run(EVRUN_ONCE);
        }
    }

    // Unsubscribe from each pattern individually
    for (const auto &pattern : patterns) {
        auto result = consumer.punsubscribe(pattern);
        ASSERT_TRUE(result.channel.has_value());
        ASSERT_EQ(*result.channel, pattern);
    }
}

// Test async subscription with empty channel name
TEST(Redis, ASYNC_SUBSCRIPTION_EMPTY_CHANNEL) {
    async::init();

    // Create a consumer for testing
    qb::redis::tcp::cb_consumer consumer{REDIS_URI, [](auto &&) {}};
    ASSERT_TRUE(consumer.connect());

    bool test_complete = false;

    consumer.subscribe(
        [&](auto &&reply) {
            ASSERT_FALSE(reply.ok());
            test_complete = true;
        },
        "");

    while (!test_complete) {
        qb::io::async::run(EVRUN_ONCE);
    }

    test_complete = false;
    consumer.psubscribe(
        [&](auto &&reply) {
            ASSERT_FALSE(reply.ok());
            test_complete = true;
        },
        "");

    while (!test_complete) {
        qb::io::async::run(EVRUN_ONCE);
    }
}