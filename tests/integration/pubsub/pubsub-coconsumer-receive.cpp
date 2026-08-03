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

/**
 * @file pubsub-coconsumer-receive.cpp
 * @brief Integration tests for the coroutine pull consumer `co_consumer.receive()`.
 *
 * Migrated from `test-subscription-commands.cpp` (the `co_consumer.receive()` cases).
 * Subject under test: the `tcp::co_consumer` pull API — `subscribe`/`psubscribe`
 * then `co_await receive()` yielding `message{pattern, channel, payload}` for plain
 * and pattern subscriptions, in order, across N messages.
 *
 * Defects fixed (spec §7.C):
 *   - The publisher's fixed `sleep(50ms)` pre-delay is removed: `receive()` parks on
 *     an internal channel (it does not busy-spin), so the receiver task is spawned
 *     first and simply suspends until delivery; `run_coro_test_until` pumps the loop
 *     to completion with a watchdog. No wall-clock race remains.
 */

#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <vector>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include "../../shared/pubsub_wait.h"
#include "../../shared/redis_integration_fixture.h"
#include <qbm/redis/redis.h>

using namespace qb::io;

namespace {
constexpr const char *kTestMessage = "Hello World";
} // namespace

// ============================================================================
// Fixture: publisher + a coroutine pull consumer.
// ============================================================================

class PubSubCoConsumerTest : public ProtocolModesTestBase {
protected:
    qb::redis::tcp::client      publisher{REDIS_URI_PROTOCOL};
    qb::redis::tcp::co_consumer co_consumer{REDIS_URI_PROTOCOL};

    void
    SetUp() override {
        ProtocolModesTestBase::SetUp();
        if (IsSkipped())
            return;
        ASSERT_TRUE(qb::io::async::run_sync(publisher.connect()));
        ASSERT_TRUE(qb::io::async::run_sync(co_consumer.connect()));
    }
};

INSTANTIATE_PROTOCOL_MODES(PubSubCoConsumerTest);

// =============== receive() yields one channel message ===============

TEST_P(PubSubCoConsumerTest, ReceiveYieldsSubscribedChannelMessage) {
    std::optional<std::string> received_payload;
    std::optional<std::string> received_channel;
    bool                       done = false;
    const auto                 ch   = protocol_key("recv");

    // Ordering is load-bearing: pub/sub has NO buffering, so a PUBLISH issued before the
    // server has registered our SUBSCRIBE is silently dropped — the receiver would then
    // park on receive() forever (the original RESP2 hang) and, once the test returned, the
    // still-parked coroutine would be resumed by ~RedisCoroConsumer's channel close into a
    // freed stack frame (the observed SEGV). We therefore (1) confirm the subscription
    // synchronously FIRST, (2) only then publish, so the message is guaranteed to land and
    // receive() completes — leaving no orphaned coroutine referencing this stack.
    if (GetParam() == ProtocolMode::RESP3) {
        ASSERT_TRUE(qb::io::async::run_sync(co_consumer.hello(3)).ok());
    }
    ASSERT_TRUE(qb::io::async::run_sync(co_consumer.subscribe(ch)).ok());

    auto recv_task = [&]() -> qb::io::async::task<void> {
        auto msg = co_await co_consumer.receive();
        if (!msg.has_value()) {
            ADD_FAILURE() << "receive() returned nullopt";
        } else {
            received_payload = std::string(msg->payload);
            received_channel = std::string(msg->channel);
        }
        done = true;
    };
    qb::io::async::coro_scheduler().spawn(recv_task());

    // Subscription is confirmed; publishing now is guaranteed to be delivered.
    ASSERT_TRUE(qb::io::async::run_sync(publisher.publish(ch, kTestMessage)).ok());

    run_coro_test_until(done);

    ASSERT_TRUE(received_payload.has_value());
    EXPECT_EQ(*received_payload, kTestMessage);
    ASSERT_TRUE(received_channel.has_value());
    EXPECT_EQ(*received_channel, ch);

    EXPECT_TRUE(qb::io::async::run_sync(co_consumer.unsubscribe(ch)).ok());
}

// =============== receive() yields a pattern message (channel + pattern set) ===============

TEST_P(PubSubCoConsumerTest, ReceiveYieldsPatternMessageWithChannelAndPattern) {
    std::optional<std::string> received_payload;
    std::optional<std::string> received_channel;
    std::optional<std::string> received_pattern;
    bool                       done       = false;
    const auto                 pat        = protocol_key("tpat") + "*";
    const auto                 ch_for_pub = protocol_key("tpat") + "1";

    // Confirm the pattern subscription synchronously BEFORE publishing (see the channel
    // test for why the ordering matters: avoids the lost-message hang + orphan-coroutine
    // SEGV).
    if (GetParam() == ProtocolMode::RESP3) {
        ASSERT_TRUE(qb::io::async::run_sync(co_consumer.hello(3)).ok());
    }
    ASSERT_TRUE(qb::io::async::run_sync(co_consumer.psubscribe(pat)).ok());

    auto recv_task = [&]() -> qb::io::async::task<void> {
        auto msg = co_await co_consumer.receive();
        if (!msg.has_value()) {
            ADD_FAILURE() << "receive() returned nullopt";
        } else {
            received_payload = std::string(msg->payload);
            received_channel = std::string(msg->channel);
            received_pattern = std::string(msg->pattern);
        }
        done = true;
    };
    qb::io::async::coro_scheduler().spawn(recv_task());

    ASSERT_TRUE(qb::io::async::run_sync(publisher.publish(ch_for_pub, kTestMessage)).ok());

    run_coro_test_until(done);

    ASSERT_TRUE(received_payload.has_value());
    EXPECT_EQ(*received_payload, kTestMessage);
    ASSERT_TRUE(received_channel.has_value());
    EXPECT_EQ(*received_channel, ch_for_pub);
    ASSERT_TRUE(received_pattern.has_value());
    EXPECT_EQ(*received_pattern, pat);

    EXPECT_TRUE(qb::io::async::run_sync(co_consumer.punsubscribe(pat)).ok());
}

// =============== receive() in a loop yields N messages in order ===============

TEST_P(PubSubCoConsumerTest, ReceiveLoopYieldsAllMessagesInOrder) {
    std::vector<std::string> received;
    bool                     done = false;
    const auto               ch   = protocol_key("recvmany");

    // Confirm the subscription synchronously BEFORE publishing the burst (ordering avoids
    // the lost-message hang + orphan-coroutine SEGV — see the channel test).
    if (GetParam() == ProtocolMode::RESP3) {
        ASSERT_TRUE(qb::io::async::run_sync(co_consumer.hello(3)).ok());
    }
    ASSERT_TRUE(qb::io::async::run_sync(co_consumer.subscribe(ch)).ok());

    auto recv_task = [&]() -> qb::io::async::task<void> {
        for (int i = 0; i < 3; ++i) {
            auto msg = co_await co_consumer.receive();
            if (!msg.has_value())
                break;
            received.emplace_back(msg->payload);
        }
        done = true;
    };
    qb::io::async::coro_scheduler().spawn(recv_task());

    // Publish the burst now that the receiver is subscribed and parked.
    for (int i = 0; i < 3; ++i)
        ASSERT_TRUE(qb::io::async::run_sync(publisher.publish(ch, "msg" + std::to_string(i))).ok());

    run_coro_test_until(done);

    ASSERT_EQ(received.size(), 3u);
    EXPECT_EQ(received[0], "msg0");
    EXPECT_EQ(received[1], "msg1");
    EXPECT_EQ(received[2], "msg2");

    EXPECT_TRUE(qb::io::async::run_sync(co_consumer.unsubscribe(ch)).ok());
}
