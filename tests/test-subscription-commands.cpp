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

#include <algorithm>
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

class SubscriptionProtocolModesTest : public ProtocolModesTestBase {
protected:
    qb::redis::tcp::client      publisher{REDIS_URI_PROTOCOL};
    qb::redis::tcp::cb_consumer consumer{REDIS_URI_PROTOCOL, [](auto &&) {}};
    qb::redis::tcp::co_consumer co_consumer{REDIS_URI_PROTOCOL};

    void
    SetUp() override {
        ProtocolModesTestBase::SetUp();
        if (!qb::io::async::run_sync(publisher.connect()) || !qb::io::async::run_sync(consumer.connect())
            || !qb::io::async::run_sync(co_consumer.connect())) {
            throw std::runtime_error("Unable to connect publisher/consumer");
        }
    }
};

INSTANTIATE_PROTOCOL_MODES(SubscriptionProtocolModesTest);

// =============== CHANNEL SUBSCRIPTION TESTS ===============

TEST_P(SubscriptionProtocolModesTest, CORO_SUBSCRIPTION_CHANNEL) {
    std::atomic<size_t>      message_count{0};
    std::vector<std::string> messages;
    std::mutex               mutex;

    qb::redis::tcp::cb_consumer consumer_with_cb{REDIS_URI_PROTOCOL, [&](auto &&msg) {
                                                     std::lock_guard<std::mutex> lock(mutex);
                                                     messages.push_back(std::string(msg.payload));
                                                     ++message_count;
                                                 }};
    ASSERT_TRUE(qb::io::async::run_sync(consumer_with_cb.connect()));

    bool completed = false;
    auto test_task = [&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CONSUMER(consumer_with_cb, completed);
        auto ch    = protocol_key("sub");
        auto reply = co_await consumer_with_cb.subscribe(ch);
        EXPECT_TRUE(reply.ok());
        EXPECT_TRUE(reply.result().channel.has_value());
        EXPECT_EQ(*reply.result().channel, ch);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);

    completed       = false;
    auto ch         = protocol_key("sub");
    auto test_task2 = [&]() -> qb::io::async::task<void> {
        auto reply = co_await publisher.publish(ch, TEST_MESSAGE);
        EXPECT_TRUE(reply.ok());
        EXPECT_GT(reply.result(), 0);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task2());
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
    if (message_count > 0) {
        std::lock_guard<std::mutex> lock(mutex);
        EXPECT_EQ(messages[0], TEST_MESSAGE);
    }

    completed       = false;
    auto test_task3 = [&]() -> qb::io::async::task<void> {
        auto reply = co_await consumer_with_cb.unsubscribe(ch);
        EXPECT_TRUE(reply.ok());
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task3());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);
}

// A throwing pub/sub message callback must be CONTAINED in the MESSAGE/PMESSAGE
// delivery (its own try/catch), never escalate to the dispatcher's outer catch —
// which would fail an unrelated pending command and desync the reply/command FIFO.
// This confirms the consumer survives a throwing message callback and keeps serving
// commands. (A precise pending-command-corruption fail-before needs message/reply
// interleaving the harness cannot deterministically force; the fix is the same
// containment the other three handler-dispatch paths already use.)
TEST_P(SubscriptionProtocolModesTest, ThrowingMessageCallbackDoesNotBreakConsumer) {
    qb::redis::tcp::cb_consumer thrower{REDIS_URI_PROTOCOL, [](auto &&) { throw std::runtime_error("boom in message cb"); }};
    ASSERT_TRUE(qb::io::async::run_sync(thrower.connect()));

    auto ch   = protocol_key("throwsub");
    bool done = false;
    auto sub  = [&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CONSUMER(thrower, done);
        EXPECT_TRUE((co_await thrower.subscribe(ch)).ok());
        done = true;
    };
    qb::io::async::coro_scheduler().spawn(sub());
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);

    done     = false;
    auto pub = [&]() -> qb::io::async::task<void> {
        EXPECT_TRUE((co_await publisher.publish(ch, TEST_MESSAGE)).ok());
        co_await qb::io::async::sleep(std::chrono::milliseconds(100)); // let the cb fire (and throw)
        done = true;
    };
    qb::io::async::coro_scheduler().spawn(pub());
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);

    // The consumer must still be healthy: a follow-up command resolves correctly.
    done          = false;
    bool unsub_ok = false;
    auto fin      = [&]() -> qb::io::async::task<void> {
        unsub_ok = (co_await thrower.unsubscribe(ch)).ok();
        done     = true;
    };
    qb::io::async::coro_scheduler().spawn(fin());
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
    EXPECT_TRUE(unsub_ok);
}

// =============== CORO CONSUMER (receive() API) TESTS ===============

TEST_P(SubscriptionProtocolModesTest, CORO_CONSUMER_RECEIVE) {
    std::optional<std::string> received_payload;
    bool                       done = false;
    auto                       ch   = protocol_key("recv");

    auto recv_task = [this, &received_payload, &done, ch]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CONSUMER(co_consumer, done);
        auto reply = co_await co_consumer.subscribe(ch);
        if (!reply.ok()) {
            ADD_FAILURE() << "subscribe failed: " << reply.error();
            done = true;
            co_return;
        }
        auto msg = co_await co_consumer.receive();
        if (!msg.has_value()) {
            ADD_FAILURE() << "receive returned nullopt";
        } else {
            received_payload = std::string(msg->payload);
        }
        done = true;
    };

    auto pub_task = [this, ch]() -> qb::io::async::task<void> {
        co_await qb::io::async::sleep(std::chrono::milliseconds(50));
        auto reply = co_await publisher.publish(ch, TEST_MESSAGE);
        EXPECT_TRUE(reply.ok());
    };

    qb::io::async::coro_scheduler().spawn(recv_task());
    qb::io::async::coro_scheduler().spawn(pub_task());

    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);

    ASSERT_TRUE(received_payload.has_value());
    EXPECT_EQ(*received_payload, TEST_MESSAGE);

    auto unsub = qb::io::async::run_sync(co_consumer.unsubscribe(ch));
    EXPECT_TRUE(unsub.ok());
}

TEST_P(SubscriptionProtocolModesTest, CORO_CONSUMER_PSUBSCRIBE_RECEIVE) {
    std::optional<std::string> received_payload;
    std::optional<std::string> received_channel;
    std::optional<std::string> received_pattern;
    bool                       done       = false;
    auto                       pat        = protocol_key("tpat") + "*";
    auto                       ch_for_pub = protocol_key("tpat") + "1";

    auto recv_task = [this, &received_payload, &received_channel, &received_pattern, &done, pat]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CONSUMER(co_consumer, done);
        auto reply = co_await co_consumer.psubscribe(pat);
        if (!reply.ok()) {
            ADD_FAILURE() << "psubscribe failed: " << reply.error();
            done = true;
            co_return;
        }
        auto msg = co_await co_consumer.receive();
        if (!msg.has_value()) {
            ADD_FAILURE() << "receive returned nullopt";
        } else {
            received_payload = std::string(msg->payload);
            received_channel = std::string(msg->channel);
            received_pattern = std::string(msg->pattern);
        }
        done = true;
    };

    auto pub_task = [this, ch_for_pub]() -> qb::io::async::task<void> {
        co_await qb::io::async::sleep(std::chrono::milliseconds(50));
        auto reply = co_await publisher.publish(ch_for_pub, TEST_MESSAGE);
        EXPECT_TRUE(reply.ok());
    };

    qb::io::async::coro_scheduler().spawn(recv_task());
    qb::io::async::coro_scheduler().spawn(pub_task());

    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);

    ASSERT_TRUE(received_payload.has_value());
    EXPECT_EQ(*received_payload, TEST_MESSAGE);
    ASSERT_TRUE(received_channel.has_value());
    EXPECT_EQ(*received_channel, ch_for_pub);
    ASSERT_TRUE(received_pattern.has_value());
    EXPECT_EQ(*received_pattern, pat);

    auto punsub = qb::io::async::run_sync(co_consumer.punsubscribe(pat));
    EXPECT_TRUE(punsub.ok());
}

TEST_P(SubscriptionProtocolModesTest, CORO_CONSUMER_RECEIVE_MULTIPLE_MESSAGES) {
    std::vector<std::string> received;
    bool                     done = false;
    auto                     ch   = protocol_key("recv");

    auto recv_task = [this, &received, &done, ch]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CONSUMER(co_consumer, done);
        auto reply = co_await co_consumer.subscribe(ch);
        if (!reply.ok()) {
            ADD_FAILURE() << "subscribe failed";
            done = true;
            co_return;
        }
        for (int i = 0; i < 3; ++i) {
            auto msg = co_await co_consumer.receive();
            if (!msg.has_value())
                break;
            received.push_back(std::string(msg->payload));
        }
        done = true;
    };

    auto pub_task = [this, ch]() -> qb::io::async::task<void> {
        co_await qb::io::async::sleep(std::chrono::milliseconds(50));
        for (int i = 0; i < 3; ++i) {
            auto r = co_await publisher.publish(ch, "msg" + std::to_string(i));
            EXPECT_TRUE(r.ok());
            co_await qb::io::async::sleep(std::chrono::milliseconds(10));
        }
    };

    qb::io::async::coro_scheduler().spawn(recv_task());
    qb::io::async::coro_scheduler().spawn(pub_task());

    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);

    ASSERT_EQ(received.size(), 3u);
    EXPECT_EQ(received[0], "msg0");
    EXPECT_EQ(received[1], "msg1");
    EXPECT_EQ(received[2], "msg2");

    qb::io::async::run_sync(co_consumer.unsubscribe(ch));
}

// receive() returns nullopt when channel is closed (e.g. on disconnect).
// Manual disconnect() in this test harness does not propagate to the event loop
// before run_sync(receive()) blocks; skip to avoid hanging.
TEST_P(SubscriptionProtocolModesTest, DISABLED_CORO_CONSUMER_RECEIVE_NULLOPT_ON_DISCONNECT) {
    auto ch = protocol_key("disc");
    if (GetParam() == ProtocolMode::RESP3) {
        auto h = qb::io::async::run_sync(co_consumer.hello(3));
        ASSERT_TRUE(h.ok()) << h.error();
    }
    auto reply = qb::io::async::run_sync(co_consumer.subscribe(ch));
    ASSERT_TRUE(reply.ok());

    co_consumer.disconnect();
    for (int i = 0; i < 500; ++i) {
        qb::io::async::run(EVRUN_NOWAIT);
    }

    auto msg = qb::io::async::run_sync(co_consumer.receive());
    EXPECT_FALSE(msg.has_value()) << "receive() should return nullopt after disconnect";
}

// =============== PATTERN SUBSCRIPTION TESTS ===============

TEST_P(SubscriptionProtocolModesTest, CORO_SUBSCRIPTION_PATTERN) {
    std::atomic<size_t> message_count{0};
    auto                pat = protocol_key("tpat") + "*";
    auto                ch1 = protocol_key("tpat") + "1";
    auto                ch2 = protocol_key("tpat") + "2";

    qb::redis::tcp::cb_consumer consumer_with_cb{REDIS_URI_PROTOCOL, [&](auto &&msg) {
                                                     ++message_count;
                                                     EXPECT_EQ(msg.payload, TEST_MESSAGE);
                                                 }};
    ASSERT_TRUE(qb::io::async::run_sync(consumer_with_cb.connect()));

    bool completed = false;
    auto test_task = [&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CONSUMER(consumer_with_cb, completed);
        auto reply = co_await consumer_with_cb.psubscribe(pat);
        EXPECT_TRUE(reply.ok());
        EXPECT_TRUE(reply.result().channel.has_value());
        EXPECT_EQ(*reply.result().channel, pat);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);

    completed       = false;
    auto test_task2 = [this, &completed, ch1, ch2]() -> qb::io::async::task<void> {
        auto reply1 = co_await publisher.publish(ch1, TEST_MESSAGE);
        EXPECT_TRUE(reply1.ok());
        EXPECT_GT(reply1.result(), 0);
        auto reply2 = co_await publisher.publish(ch2, TEST_MESSAGE);
        EXPECT_TRUE(reply2.ok());
        EXPECT_GT(reply2.result(), 0);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task2());
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

    EXPECT_EQ(message_count, 2);

    completed       = false;
    auto test_task3 = [&]() -> qb::io::async::task<void> {
        auto reply = co_await consumer_with_cb.punsubscribe(pat);
        EXPECT_TRUE(reply.ok());
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task3());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);
}

// =============== MULTI-CHANNEL SUBSCRIPTION TESTS ===============

TEST_P(SubscriptionProtocolModesTest, CORO_SUBSCRIPTION_MULTI_CHANNEL) {
    std::atomic<size_t>      message_count{0};
    std::vector<std::string> received_channels;
    std::mutex               mutex;

    qb::redis::tcp::cb_consumer consumer_with_cb{REDIS_URI_PROTOCOL, [&](auto &&msg) {
                                                     std::lock_guard<std::mutex> lock(mutex);
                                                     received_channels.push_back(std::string(msg.channel));
                                                     ++message_count;
                                                 }};
    ASSERT_TRUE(qb::io::async::run_sync(consumer_with_cb.connect()));

    auto                     ch_a     = protocol_key("mca");
    auto                     ch_b     = protocol_key("mcb");
    auto                     ch_c     = protocol_key("mcc");
    std::vector<std::string> channels = {ch_a, ch_b, ch_c};

    bool completed = false;
    auto sub_task  = [&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CONSUMER(consumer_with_cb, completed);
        auto reply = co_await consumer_with_cb.subscribe(channels);
        EXPECT_TRUE(reply.ok());
        EXPECT_GE(reply.result().num, 1);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(sub_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);

    completed     = false;
    auto pub_task = [this, &completed, channels]() -> qb::io::async::task<void> {
        co_await qb::io::async::sleep(std::chrono::milliseconds(30));
        for (const auto &ch : channels) {
            auto r = co_await publisher.publish(ch, "msg_" + ch);
            EXPECT_TRUE(r.ok());
        }
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

    EXPECT_EQ(message_count, 3u);
    std::sort(received_channels.begin(), received_channels.end());
    std::vector<std::string> expected = {ch_a, ch_b, ch_c};
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(received_channels, expected);

    completed       = false;
    auto unsub_task = [&]() -> qb::io::async::task<void> {
        auto reply = co_await consumer_with_cb.unsubscribe(channels);
        EXPECT_TRUE(reply.ok());
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(unsub_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(SubscriptionProtocolModesTest, CORO_UNSUBSCRIBE_ALL_CHANNELS) {
    bool completed = false;
    auto ch1       = protocol_key("ch1");
    auto ch2       = protocol_key("ch2");

    auto sub_task = [this, &completed, ch1, ch2]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CONSUMER(consumer, completed);
        auto r = co_await consumer.subscribe(std::vector<std::string>{ch1, ch2});
        EXPECT_TRUE(r.ok());
        EXPECT_GE(r.result().num, 1);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(sub_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);

    completed           = false;
    auto unsub_all_task = [this, &completed]() -> qb::io::async::task<void> {
        auto r = co_await consumer.unsubscribe("");
        EXPECT_TRUE(r.ok());
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(unsub_all_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);
}

// Pipelined multi-channel subscribe: the N confirmations of one command must
// not steal the handler of the next pipelined command. Two subscribe commands
// are issued back-to-back WITHOUT draining the loop in between; each handler
// must resolve exactly once, on its own command's final confirmation.
TEST_P(SubscriptionProtocolModesTest, PIPELINED_MULTI_CHANNEL_NO_DESYNC) {
    qb::redis::tcp::cb_consumer c{REDIS_URI_PROTOCOL, [](auto &&) {}};
    ASSERT_TRUE(qb::io::async::run_sync(c.connect()));

    int         n1 = 0, n2 = 0;
    bool        ok1 = false, ok2 = false;
    long long   num1 = 0, num2 = 0;
    std::string ch1_last, ch2_last;

    auto a  = protocol_key("pda");
    auto b  = protocol_key("pdb");
    auto cc = protocol_key("pdc");
    auto d  = protocol_key("pdd");

    bool completed = false;
    auto task      = [&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CONSUMER(c, completed);
        c.subscribe(
            [&](auto &&r) {
                ++n1;
                ok1 = r.ok();
                if (r.ok()) {
                    num1 = r.result().num;
                    if (r.result().channel)
                        ch1_last = *r.result().channel;
                }
            },
            std::vector<std::string>{a, b, cc});
        c.subscribe(
            [&](auto &&r) {
                ++n2;
                ok2 = r.ok();
                if (r.ok()) {
                    num2 = r.result().num;
                    if (r.result().channel)
                        ch2_last = *r.result().channel;
                }
            },
            std::vector<std::string>{d});
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);

    c.await(); // drain all 4 confirmations
    EXPECT_EQ(c.pending_reply_count(), 0u);

    EXPECT_EQ(n1, 1);
    EXPECT_EQ(n2, 1);
    EXPECT_TRUE(ok1);
    EXPECT_TRUE(ok2);
    EXPECT_EQ(ch1_last, cc); // resolved on its own last channel, not channel b
    EXPECT_EQ(ch2_last, d);
    EXPECT_EQ(num1, 3); // 3 channels active when the first command resolves
    EXPECT_EQ(num2, 4); // 4 when the second resolves
}

// Pipelined subscribe(N) then unsubscribe-all: the unsubscribe-all handler must
// expect exactly the predicted number of confirmations (3 here), so it resolves
// on the final "num == 0" frame rather than on a stray subscribe confirmation.
TEST_P(SubscriptionProtocolModesTest, PIPELINED_SUBSCRIBE_THEN_UNSUBSCRIBE_ALL) {
    qb::redis::tcp::cb_consumer c{REDIS_URI_PROTOCOL, [](auto &&) {}};
    ASSERT_TRUE(qb::io::async::run_sync(c.connect()));

    int       n_sub = 0, n_unsub = 0;
    bool      ok_sub = false, ok_unsub = false;
    long long num_unsub = -1;

    auto a  = protocol_key("pua");
    auto b  = protocol_key("pub");
    auto cc = protocol_key("puc");

    bool completed = false;
    auto task      = [&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CONSUMER(c, completed);
        c.subscribe(
            [&](auto &&r) {
                ++n_sub;
                ok_sub = r.ok();
            },
            std::vector<std::string>{a, b, cc});
        c.unsubscribe(
            [&](auto &&r) {
                ++n_unsub;
                ok_unsub = r.ok();
                if (r.ok())
                    num_unsub = r.result().num;
            },
            std::string{""}); // unsubscribe from all channels
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);

    c.await();
    EXPECT_EQ(c.pending_reply_count(), 0u);

    EXPECT_EQ(n_sub, 1);
    EXPECT_EQ(n_unsub, 1);
    EXPECT_TRUE(ok_sub);
    EXPECT_TRUE(ok_unsub);
    EXPECT_EQ(num_unsub, 0); // all channels gone after unsubscribe-all
}

// =============== MULTI-PATTERN SUBSCRIPTION TESTS ===============

TEST_P(SubscriptionProtocolModesTest, CORO_SUBSCRIPTION_MULTI_PATTERN) {
    std::atomic<size_t> message_count{0};

    qb::redis::tcp::cb_consumer consumer_with_cb{REDIS_URI_PROTOCOL, [&](auto &&) { ++message_count; }};
    ASSERT_TRUE(qb::io::async::run_sync(consumer_with_cb.connect()));

    auto                     pat_a    = protocol_key("mpa") + "*";
    auto                     pat_b    = protocol_key("mpb") + "*";
    auto                     ch_a     = protocol_key("mpa") + "1";
    auto                     ch_b     = protocol_key("mpb") + "1";
    std::vector<std::string> patterns = {pat_a, pat_b};

    bool completed = false;
    auto sub_task  = [&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CONSUMER(consumer_with_cb, completed);
        auto reply = co_await consumer_with_cb.psubscribe(patterns);
        EXPECT_TRUE(reply.ok());
        EXPECT_GE(reply.result().num, 1);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(sub_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);

    completed     = false;
    auto pub_task = [this, &completed, ch_a, ch_b]() -> qb::io::async::task<void> {
        co_await qb::io::async::sleep(std::chrono::milliseconds(30));
        auto r1 = co_await publisher.publish(ch_a, "msg");
        auto r2 = co_await publisher.publish(ch_b, "msg");
        EXPECT_TRUE(r1.ok());
        EXPECT_TRUE(r2.ok());
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

    EXPECT_EQ(message_count, 2u);

    completed       = false;
    auto unsub_task = [&]() -> qb::io::async::task<void> {
        auto reply = co_await consumer_with_cb.punsubscribe(patterns);
        EXPECT_TRUE(reply.ok());
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(unsub_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(SubscriptionProtocolModesTest, CORO_PUNSUBSCRIBE_ALL_PATTERNS) {
    bool completed = false;
    auto p1        = protocol_key("p1") + "*";
    auto p2        = protocol_key("p2") + "*";

    auto sub_task = [this, &completed, p1, p2]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CONSUMER(consumer, completed);
        auto r = co_await consumer.psubscribe(std::vector<std::string>{p1, p2});
        EXPECT_TRUE(r.ok());
        EXPECT_GE(r.result().num, 1);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(sub_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);

    completed            = false;
    auto punsub_all_task = [this, &completed]() -> qb::io::async::task<void> {
        auto r = co_await consumer.punsubscribe("");
        EXPECT_TRUE(r.ok());
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(punsub_all_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);
}

// =============== SUBSCRIPTION MANAGEMENT TESTS ===============

TEST_P(SubscriptionProtocolModesTest, CORO_SUBSCRIPTION_MANAGEMENT) {
    auto ch = protocol_key("mgt");

    bool completed = false;
    auto test_task = [this, &completed, ch]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CONSUMER(consumer, completed);
        auto reply = co_await consumer.subscribe(ch);
        EXPECT_TRUE(reply.ok());
        EXPECT_TRUE(reply.result().channel.has_value());
        EXPECT_EQ(*reply.result().channel, ch);
        EXPECT_GT(reply.result().num, 0);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);

    completed       = false;
    auto test_task2 = [this, &completed, ch]() -> qb::io::async::task<void> {
        auto reply = co_await consumer.unsubscribe(ch);
        EXPECT_TRUE(reply.ok());
        EXPECT_TRUE(reply.result().channel.has_value());
        EXPECT_EQ(*reply.result().channel, ch);
        EXPECT_EQ(reply.result().num, 0);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task2());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(SubscriptionProtocolModesTest, CORO_PATTERN_SUBSCRIPTION_MANAGEMENT) {
    auto pat = protocol_key("pmgt") + "*";

    bool completed = false;
    auto test_task = [this, &completed, pat]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CONSUMER(consumer, completed);
        auto reply = co_await consumer.psubscribe(pat);
        EXPECT_TRUE(reply.ok());
        EXPECT_TRUE(reply.result().channel.has_value());
        EXPECT_EQ(*reply.result().channel, pat);
        EXPECT_GT(reply.result().num, 0);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);

    completed       = false;
    auto test_task2 = [this, &completed, pat]() -> qb::io::async::task<void> {
        auto reply = co_await consumer.punsubscribe(pat);
        EXPECT_TRUE(reply.ok());
        EXPECT_TRUE(reply.result().channel.has_value());
        EXPECT_EQ(*reply.result().channel, pat);
        EXPECT_EQ(reply.result().num, 0);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task2());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);
}

// =============== EDGE CASES: EMPTY CHANNEL / PATTERN / VECTOR ===============

TEST_P(SubscriptionProtocolModesTest, CORO_SUBSCRIPTION_EMPTY_CHANNEL) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CONSUMER(consumer, completed);
        auto reply = co_await consumer.subscribe("");
        EXPECT_FALSE(reply.ok());
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);

    completed       = false;
    auto test_task2 = [this, &completed]() -> qb::io::async::task<void> {
        auto reply = co_await consumer.psubscribe("");
        EXPECT_FALSE(reply.ok());
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task2());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(SubscriptionProtocolModesTest, CORO_SUBSCRIPTION_EMPTY_CHANNELS_VECTOR) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CONSUMER(consumer, completed);
        auto reply = co_await consumer.subscribe(std::vector<std::string>{});
        EXPECT_FALSE(reply.ok());
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(SubscriptionProtocolModesTest, CORO_SUBSCRIPTION_EMPTY_PATTERNS_VECTOR) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CONSUMER(consumer, completed);
        auto reply = co_await consumer.psubscribe(std::vector<std::string>{});
        EXPECT_FALSE(reply.ok());
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);
}
