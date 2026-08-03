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
 * @file pubsub-subscribe.cpp
 * @brief Integration tests for SUBSCRIBE / PSUBSCRIBE confirmation + management.
 *
 * Migrated from `test-subscription-commands.cpp` (subscribe/pattern/multi/management/
 * empty-edge cases). Subject under test: the `(P)SUBSCRIBE` / `(P)UNSUBSCRIBE`
 * confirmation shape (`reply.result().channel` / `.num`), multi-channel/pattern
 * delivery, subscription lifecycle, and client-side argument validation for the
 * empty-channel / empty-vector edges.
 *
 * Defects fixed (spec §7.C / §7.D / §3):
 *   - Every fixed `sleep(30-100ms)` delivery gate replaced by `pubsub_wait_*` pumps
 *     (flaky-timing — dominant defect).
 *   - `UNSUBSCRIBE_ALL_CHANNELS` / `PUNSUBSCRIBE_ALL_PATTERNS` now assert the
 *     resulting `num == 0` (were `.ok()`-only — under-constrained, §7.D), mirroring
 *     the pipelined variant.
 *   - The two `*_MANAGEMENT` cases (which restated the subscribe-confirmation shape
 *     already covered by the channel/pattern subscribe tests) are folded into those
 *     tests rather than duplicated (spec §3 subscription overlaps).
 *
 * The co_consumer.receive() pull-API tests live in pubsub-coconsumer-receive.cpp;
 * the pipelined-desync + throwing-callback tests live in pubsub-pipeline-desync.cpp.
 *
 * Coroutine convention: ASSERT_* (a bare `return;`) is ill-formed inside a coroutine;
 * use EXPECT_* and CORO_REQUIRE for an early `co_return` guard before a dependent deref.
 */

#include <algorithm>
#include <atomic>
#include <gtest/gtest.h>
#include <mutex>
#include <string>
#include <vector>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include "../../shared/pubsub_wait.h"
#include "../../shared/redis_integration_fixture.h"
#include <qbm/redis/redis.h>

using namespace qb::io;
using qb::redis::test::pubsub_wait_count;
using qb::redis::test::pubsub_wait_until;

/// Coroutine-safe early-return guard (ASSERT_* cannot be used inside a coroutine).
#define CORO_REQUIRE(cond, done_flag)                   \
    if (!(cond)) {                                      \
        ADD_FAILURE() << "CORO_REQUIRE failed: " #cond; \
        done_flag = true;                               \
        co_return;                                      \
    }

namespace {
constexpr const char *kTestMessage = "Hello World";
} // namespace

// ============================================================================
// Fixture: publisher + a reusable cb_consumer on top of the base client.
// ============================================================================

class PubSubSubscribeTest : public ProtocolModesTestBase {
protected:
    qb::redis::tcp::client      publisher{REDIS_URI_PROTOCOL};
    qb::redis::tcp::cb_consumer consumer{REDIS_URI_PROTOCOL, [](auto &&) {}};

    void
    SetUp() override {
        ProtocolModesTestBase::SetUp();
        if (IsSkipped())
            return;
        ASSERT_TRUE(qb::io::async::run_sync(publisher.connect()));
        ASSERT_TRUE(qb::io::async::run_sync(consumer.connect()));
    }
};

INSTANTIATE_PROTOCOL_MODES(PubSubSubscribeTest);

// =============== SINGLE CHANNEL: confirmation shape + lifecycle + delivery ===============

TEST_P(PubSubSubscribeTest, SubscribeConfirmsChannelDeliversAndUnsubscribesToZero) {
    std::atomic<size_t>      message_count{0};
    std::vector<std::string> payloads;
    std::mutex               mtx;
    const auto               ch = protocol_key("sub");

    qb::redis::tcp::cb_consumer cons{REDIS_URI_PROTOCOL, [&](auto &&msg) {
                                         std::lock_guard<std::mutex> lock(mtx);
                                         payloads.emplace_back(msg.payload);
                                         ++message_count;
                                     }};
    ASSERT_TRUE(qb::io::async::run_sync(cons.connect()));

    // subscribe -> confirmation carries the channel and a positive active count.
    bool done = false;
    auto sub  = [&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CONSUMER(cons, done);
        auto r = co_await cons.subscribe(ch);
        EXPECT_TRUE(r.ok()) << r.error();
        CORO_REQUIRE(r.ok() && r.result().channel.has_value(), done);
        EXPECT_EQ(*r.result().channel, ch);
        EXPECT_GT(r.result().num, 0);
        done = true;
    };
    qb::io::async::coro_scheduler().spawn(sub());
    run_coro_test_until(done);

    // publish -> deliver -> exact payload.
    done     = false;
    auto pub = [&]() -> qb::io::async::task<void> {
        auto r = co_await publisher.publish(ch, kTestMessage);
        EXPECT_TRUE(r.ok());
        EXPECT_EQ(r.result(), 1);
        done = true;
    };
    qb::io::async::coro_scheduler().spawn(pub());
    run_coro_test_until(done);

    ASSERT_TRUE(pubsub_wait_count(message_count, 1));
    EXPECT_EQ(message_count, 1u);
    {
        std::lock_guard<std::mutex> lock(mtx);
        ASSERT_EQ(payloads.size(), 1u);
        EXPECT_EQ(payloads[0], kTestMessage);
    }

    // unsubscribe -> confirmation carries the channel and num == 0.
    done       = false;
    auto unsub = [&]() -> qb::io::async::task<void> {
        auto r = co_await cons.unsubscribe(ch);
        EXPECT_TRUE(r.ok()) << r.error();
        CORO_REQUIRE(r.ok() && r.result().channel.has_value(), done);
        EXPECT_EQ(*r.result().channel, ch);
        EXPECT_EQ(r.result().num, 0);
        done = true;
    };
    qb::io::async::coro_scheduler().spawn(unsub());
    run_coro_test_until(done);
}

// =============== PATTERN: confirmation shape + lifecycle + delivery ===============

TEST_P(PubSubSubscribeTest, PsubscribeConfirmsPatternDeliversAndUnsubscribesToZero) {
    std::atomic<size_t> message_count{0};
    const auto          pat = protocol_key("tpat") + "*";
    const auto          ch1 = protocol_key("tpat") + "1";
    const auto          ch2 = protocol_key("tpat") + "2";

    qb::redis::tcp::cb_consumer cons{REDIS_URI_PROTOCOL, [&, pat](auto &&msg) {
                                         EXPECT_EQ(msg.payload, kTestMessage);
                                         EXPECT_EQ(msg.pattern, pat); // PMESSAGE carries the matching pattern
                                         ++message_count;
                                     }};
    ASSERT_TRUE(qb::io::async::run_sync(cons.connect()));

    bool done = false;
    auto sub  = [&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CONSUMER(cons, done);
        auto r = co_await cons.psubscribe(pat);
        EXPECT_TRUE(r.ok()) << r.error();
        CORO_REQUIRE(r.ok() && r.result().channel.has_value(), done);
        EXPECT_EQ(*r.result().channel, pat);
        EXPECT_GT(r.result().num, 0);
        done = true;
    };
    qb::io::async::coro_scheduler().spawn(sub());
    run_coro_test_until(done);

    done     = false;
    auto pub = [&]() -> qb::io::async::task<void> {
        EXPECT_TRUE((co_await publisher.publish(ch1, kTestMessage)).ok());
        EXPECT_TRUE((co_await publisher.publish(ch2, kTestMessage)).ok());
        done = true;
    };
    qb::io::async::coro_scheduler().spawn(pub());
    run_coro_test_until(done);

    ASSERT_TRUE(pubsub_wait_count(message_count, 2));
    EXPECT_EQ(message_count, 2u);

    done       = false;
    auto unsub = [&]() -> qb::io::async::task<void> {
        auto r = co_await cons.punsubscribe(pat);
        EXPECT_TRUE(r.ok()) << r.error();
        CORO_REQUIRE(r.ok() && r.result().channel.has_value(), done);
        EXPECT_EQ(*r.result().channel, pat);
        EXPECT_EQ(r.result().num, 0);
        done = true;
    };
    qb::io::async::coro_scheduler().spawn(unsub());
    run_coro_test_until(done);
}

// =============== MULTI-CHANNEL: all channels deliver, order-independent ===============

TEST_P(PubSubSubscribeTest, SubscribeMultipleChannelsDeliversEachOnce) {
    std::atomic<size_t>      message_count{0};
    std::vector<std::string> received_channels;
    std::mutex               mtx;

    qb::redis::tcp::cb_consumer cons{REDIS_URI_PROTOCOL, [&](auto &&msg) {
                                         std::lock_guard<std::mutex> lock(mtx);
                                         received_channels.emplace_back(msg.channel);
                                         ++message_count;
                                     }};
    ASSERT_TRUE(qb::io::async::run_sync(cons.connect()));

    const auto               ch_a     = protocol_key("mca");
    const auto               ch_b     = protocol_key("mcb");
    const auto               ch_c     = protocol_key("mcc");
    std::vector<std::string> channels = {ch_a, ch_b, ch_c};

    bool done = false;
    auto sub  = [&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CONSUMER(cons, done);
        auto r = co_await cons.subscribe(channels);
        EXPECT_TRUE(r.ok()) << r.error();
        EXPECT_EQ(r.result().num, 3) << "three channels active after the command";
        done = true;
    };
    qb::io::async::coro_scheduler().spawn(sub());
    run_coro_test_until(done);

    done     = false;
    auto pub = [&]() -> qb::io::async::task<void> {
        for (const auto &ch : channels)
            EXPECT_TRUE((co_await publisher.publish(ch, "msg_" + ch)).ok());
        done = true;
    };
    qb::io::async::coro_scheduler().spawn(pub());
    run_coro_test_until(done);

    ASSERT_TRUE(pubsub_wait_count(message_count, 3));
    EXPECT_EQ(message_count, 3u);
    {
        std::lock_guard<std::mutex> lock(mtx);
        std::sort(received_channels.begin(), received_channels.end());
        std::vector<std::string> expected = {ch_a, ch_b, ch_c};
        std::sort(expected.begin(), expected.end());
        EXPECT_EQ(received_channels, expected);
    }

    CO_IGNORE(qb::io::async::run_sync(cons.unsubscribe(channels)));
}

// =============== MULTI-PATTERN: both patterns deliver ===============

TEST_P(PubSubSubscribeTest, PsubscribeMultiplePatternsDeliverEach) {
    std::atomic<size_t> message_count{0};

    qb::redis::tcp::cb_consumer cons{REDIS_URI_PROTOCOL, [&](auto &&) { ++message_count; }};
    ASSERT_TRUE(qb::io::async::run_sync(cons.connect()));

    const auto               pat_a    = protocol_key("mpa") + "*";
    const auto               pat_b    = protocol_key("mpb") + "*";
    const auto               ch_a     = protocol_key("mpa") + "1";
    const auto               ch_b     = protocol_key("mpb") + "1";
    std::vector<std::string> patterns = {pat_a, pat_b};

    bool done = false;
    auto sub  = [&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CONSUMER(cons, done);
        auto r = co_await cons.psubscribe(patterns);
        EXPECT_TRUE(r.ok()) << r.error();
        EXPECT_EQ(r.result().num, 2) << "two patterns active after the command";
        done = true;
    };
    qb::io::async::coro_scheduler().spawn(sub());
    run_coro_test_until(done);

    done     = false;
    auto pub = [&]() -> qb::io::async::task<void> {
        EXPECT_TRUE((co_await publisher.publish(ch_a, "msg")).ok());
        EXPECT_TRUE((co_await publisher.publish(ch_b, "msg")).ok());
        done = true;
    };
    qb::io::async::coro_scheduler().spawn(pub());
    run_coro_test_until(done);

    ASSERT_TRUE(pubsub_wait_count(message_count, 2));
    EXPECT_EQ(message_count, 2u);

    CO_IGNORE(qb::io::async::run_sync(cons.punsubscribe(patterns)));
}

// =============== UNSUBSCRIBE-ALL (coro empty-string form) -> num == 0 ===============
//
// Spec §7.D: the source asserted only `.ok()`. After unsubscribe-all the final
// confirmation must report num == 0 (no channels remaining), mirroring the
// pipelined desync test.

TEST_P(PubSubSubscribeTest, UnsubscribeAllChannelsReportsZeroRemaining) {
    const auto ch1 = protocol_key("ch1");
    const auto ch2 = protocol_key("ch2");

    bool done = false;
    auto sub  = [&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CONSUMER(consumer, done);
        auto r = co_await consumer.subscribe(std::vector<std::string>{ch1, ch2});
        EXPECT_TRUE(r.ok()) << r.error();
        EXPECT_EQ(r.result().num, 2);
        done = true;
    };
    qb::io::async::coro_scheduler().spawn(sub());
    run_coro_test_until(done);

    done           = false;
    auto unsub_all = [&]() -> qb::io::async::task<void> {
        auto r = co_await consumer.unsubscribe(""); // empty == unsubscribe-all
        EXPECT_TRUE(r.ok()) << r.error();
        EXPECT_EQ(r.result().num, 0) << "no channels should remain after unsubscribe-all";
        done = true;
    };
    qb::io::async::coro_scheduler().spawn(unsub_all());
    run_coro_test_until(done);
}

TEST_P(PubSubSubscribeTest, PunsubscribeAllPatternsReportsZeroRemaining) {
    const auto p1 = protocol_key("p1") + "*";
    const auto p2 = protocol_key("p2") + "*";

    bool done = false;
    auto sub  = [&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CONSUMER(consumer, done);
        auto r = co_await consumer.psubscribe(std::vector<std::string>{p1, p2});
        EXPECT_TRUE(r.ok()) << r.error();
        EXPECT_EQ(r.result().num, 2);
        done = true;
    };
    qb::io::async::coro_scheduler().spawn(sub());
    run_coro_test_until(done);

    done            = false;
    auto punsub_all = [&]() -> qb::io::async::task<void> {
        auto r = co_await consumer.punsubscribe(""); // empty == punsubscribe-all
        EXPECT_TRUE(r.ok()) << r.error();
        EXPECT_EQ(r.result().num, 0) << "no patterns should remain after punsubscribe-all";
        done = true;
    };
    qb::io::async::coro_scheduler().spawn(punsub_all());
    run_coro_test_until(done);
}

// =============== EDGE: empty channel / pattern / vector are rejected client-side ===============

TEST_P(PubSubSubscribeTest, SubscribeEmptyChannelOrPatternIsRejected) {
    bool done = false;
    auto task = [&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CONSUMER(consumer, done);
        EXPECT_FALSE((co_await consumer.subscribe("")).ok());
        EXPECT_FALSE((co_await consumer.psubscribe("")).ok());
        done = true;
    };
    qb::io::async::coro_scheduler().spawn(task());
    run_coro_test_until(done);
}

TEST_P(PubSubSubscribeTest, SubscribeEmptyVectorIsRejected) {
    bool done = false;
    auto task = [&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CONSUMER(consumer, done);
        EXPECT_FALSE((co_await consumer.subscribe(std::vector<std::string>{})).ok());
        EXPECT_FALSE((co_await consumer.psubscribe(std::vector<std::string>{})).ok());
        done = true;
    };
    qb::io::async::coro_scheduler().spawn(task());
    run_coro_test_until(done);
}
