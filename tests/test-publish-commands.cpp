/*
 * qb - C++ Actor Framework
 * Copyright (C) 2011-2026 isndev (cpp.actor). All rights reserved.
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
#include <qb/io/async/coroutine.h>
#include "../redis.h"
#include "protocol_test_common.h"

// Redis Configuration
#define REDIS_URI {"tcp://localhost:6379"}

#define TEST_MESSAGE "Hello World"

using namespace qb::io;
using namespace std::chrono;

// ============================================================================
// Fixture: all tests run in both RESP2 and RESP3
// ============================================================================

class PublishProtocolModesTest : public ProtocolModesTestBase {
protected:
    qb::redis::tcp::client      publisher{REDIS_URI_PROTOCOL};
    qb::redis::tcp::cb_consumer consumer{REDIS_URI_PROTOCOL, [](auto &&) {}};

    void
    SetUp() override {
        ProtocolModesTestBase::SetUp();
        if (!qb::io::async::run_sync(publisher.connect()) || !qb::io::async::run_sync(consumer.connect())) {
            throw std::runtime_error("Unable to connect publisher/consumer");
        }
    }
};

INSTANTIATE_PROTOCOL_MODES(PublishProtocolModesTest);

// =============== BASIC PUBLISH TESTS ===============

TEST_P(PublishProtocolModesTest, CORO_PUBLISH_BASIC) {
    std::atomic<size_t> message_count{0};
    auto                ch = protocol_key("pub");

    qb::redis::tcp::cb_consumer consumer_with_cb{REDIS_URI_PROTOCOL, [&](auto &&msg) {
                                                     EXPECT_EQ(msg.payload, TEST_MESSAGE);
                                                     ++message_count;
                                                 }};
    ASSERT_TRUE(qb::io::async::run_sync(consumer_with_cb.connect()));

    bool completed = false;
    auto sub_task  = [&completed, &consumer_with_cb, ch]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CONSUMER(consumer_with_cb, completed);
        auto reply = co_await consumer_with_cb.subscribe(ch);
        if (!reply.ok() || !reply.result().channel.has_value() || *reply.result().channel != ch) {
            ADD_FAILURE() << "subscribe failed or channel mismatch";
            completed = true;
            co_return;
        }
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(sub_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);

    completed     = false;
    auto pub_task = [this, &completed, ch]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CLIENT(publisher, completed);
        auto reply = co_await publisher.publish(ch, TEST_MESSAGE);
        EXPECT_TRUE(reply.ok());
        EXPECT_EQ(reply.result(), 1);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(pub_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);

    completed      = false;
    auto wait_task = [&]() -> qb::io::async::task<void> {
        co_await qb::io::async::sleep(std::chrono::milliseconds(100));
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(wait_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);

    EXPECT_EQ(message_count, 1);

    qb::io::async::run_sync(consumer_with_cb.unsubscribe(ch));
}

// =============== PATTERN PUBLISH TESTS ===============

TEST_P(PublishProtocolModesTest, CORO_PUBLISH_PATTERN) {
    std::atomic<size_t> message_count{0};
    auto                pat = protocol_key("t") + "_*";
    auto                ch  = protocol_key("t") + "_channel";

    qb::redis::tcp::cb_consumer consumer_with_cb{REDIS_URI_PROTOCOL, [&](auto &&msg) {
                                                     EXPECT_EQ(msg.payload, TEST_MESSAGE);
                                                     ++message_count;
                                                 }};
    ASSERT_TRUE(qb::io::async::run_sync(consumer_with_cb.connect()));

    bool completed = false;
    auto sub_task  = [&completed, &consumer_with_cb, pat]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CONSUMER(consumer_with_cb, completed);
        auto reply = co_await consumer_with_cb.psubscribe(pat);
        if (!reply.ok() || !reply.result().channel.has_value()) {
            ADD_FAILURE() << "psubscribe failed";
            completed = true;
            co_return;
        }
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(sub_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);

    completed     = false;
    auto pub_task = [this, &completed, ch]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CLIENT(publisher, completed);
        auto reply = co_await publisher.publish(ch, TEST_MESSAGE);
        EXPECT_TRUE(reply.ok());
        EXPECT_EQ(reply.result(), 1);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(pub_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);

    completed      = false;
    auto wait_task = [&]() -> qb::io::async::task<void> {
        co_await qb::io::async::sleep(std::chrono::milliseconds(100));
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(wait_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);

    EXPECT_EQ(message_count, 1);

    qb::io::async::run_sync(consumer_with_cb.punsubscribe(pat));
}

// =============== MULTIPLE SUBSCRIBERS TESTS ===============

TEST_P(PublishProtocolModesTest, CORO_PUBLISH_MULTIPLE_SUBSCRIBERS) {
    std::atomic<size_t> message_count1{0};
    std::atomic<size_t> message_count2{0};
    auto                ch = protocol_key("multi");

    qb::redis::tcp::cb_consumer consumer1{REDIS_URI_PROTOCOL, [&](auto &&msg) {
                                              EXPECT_EQ(msg.payload, TEST_MESSAGE);
                                              ++message_count1;
                                          }};
    qb::redis::tcp::cb_consumer consumer2{REDIS_URI_PROTOCOL, [&](auto &&msg) {
                                              EXPECT_EQ(msg.payload, TEST_MESSAGE);
                                              ++message_count2;
                                          }};
    ASSERT_TRUE(qb::io::async::run_sync(consumer1.connect()));
    ASSERT_TRUE(qb::io::async::run_sync(consumer2.connect()));

    bool completed = false;
    auto sub_task  = [&completed, &consumer1, &consumer2, ch]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CONSUMER(consumer1, completed);
        PROTOCOL_ENSURE_RESP3_CONSUMER(consumer2, completed);
        auto r1 = co_await consumer1.subscribe(ch);
        auto r2 = co_await consumer2.subscribe(ch);
        if (!r1.ok() || !r1.result().channel.has_value() || !r2.ok() || !r2.result().channel.has_value()) {
            ADD_FAILURE() << "subscribe failed";
            completed = true;
            co_return;
        }
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(sub_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);

    completed     = false;
    auto pub_task = [this, &completed, ch]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CLIENT(publisher, completed);
        auto reply = co_await publisher.publish(ch, TEST_MESSAGE);
        EXPECT_TRUE(reply.ok());
        EXPECT_EQ(reply.result(), 2);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(pub_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);

    completed      = false;
    auto wait_task = [&]() -> qb::io::async::task<void> {
        co_await qb::io::async::sleep(std::chrono::milliseconds(100));
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(wait_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);

    EXPECT_EQ(message_count1, 1);
    EXPECT_EQ(message_count2, 1);

    qb::io::async::run_sync(consumer1.unsubscribe(ch));
    qb::io::async::run_sync(consumer2.unsubscribe(ch));
}

// =============== EDGE CASES ===============

TEST_P(PublishProtocolModesTest, CORO_PUBLISH_EMPTY_CHANNEL) {
    bool completed = false;
    auto pub_task  = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CLIENT(publisher, completed);
        auto reply = co_await publisher.publish("", TEST_MESSAGE);
        EXPECT_TRUE(reply.ok());
        EXPECT_EQ(reply.result(), 0);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(pub_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(PublishProtocolModesTest, CORO_PUBLISH_EMPTY_MESSAGE) {
    std::atomic<size_t> message_count{0};
    auto                ch = protocol_key("empty");

    qb::redis::tcp::cb_consumer consumer_with_cb{REDIS_URI_PROTOCOL, [&](auto &&msg) {
                                                     EXPECT_EQ(msg.payload, "");
                                                     ++message_count;
                                                 }};
    ASSERT_TRUE(qb::io::async::run_sync(consumer_with_cb.connect()));

    bool completed = false;
    auto sub_task  = [&completed, &consumer_with_cb, ch]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CONSUMER(consumer_with_cb, completed);
        auto reply = co_await consumer_with_cb.subscribe(ch);
        if (!reply.ok() || !reply.result().channel.has_value()) {
            ADD_FAILURE() << "subscribe failed";
            completed = true;
            co_return;
        }
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(sub_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);

    completed     = false;
    auto pub_task = [this, &completed, ch]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CLIENT(publisher, completed);
        auto reply = co_await publisher.publish(ch, "");
        EXPECT_TRUE(reply.ok());
        EXPECT_EQ(reply.result(), 1);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(pub_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);

    completed      = false;
    auto wait_task = [&]() -> qb::io::async::task<void> {
        co_await qb::io::async::sleep(std::chrono::milliseconds(100));
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(wait_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);

    EXPECT_EQ(message_count, 1);

    qb::io::async::run_sync(consumer_with_cb.unsubscribe(ch));
}

TEST_P(PublishProtocolModesTest, PUBLISH_INTEGER) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k = protocol_key("chan");
        auto r = co_await redis.publish(k, "msg");
        EXPECT_TRUE(r.ok()) << r.error();
        if (r.ok())
            EXPECT_GE(r.result(), 0);
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}
