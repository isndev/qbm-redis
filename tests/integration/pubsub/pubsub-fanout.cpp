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
 * @file pubsub-fanout.cpp
 * @brief Integration tests for the PUBLISH fan-out path (publisher -> N subscribers).
 *
 * Migrated from `test-publish-commands.cpp`. Subject under test: `tcp::client::publish`
 * receiver-count semantics and end-to-end message delivery to one / many / pattern
 * subscribers, plus the PUBSUB introspection commands (CHANNELS / NUMSUB).
 *
 * Defects fixed vs the source (spec §7.C / §3):
 *   - Every fixed `co_await sleep(100ms)` delivery gate replaced by `pubsub_wait_*`
 *     predicate pumps (flaky-timing — the dominant defect of this cluster).
 *   - `PUBLISH_INTEGER` deleted (weaker `>=0` duplicate of the exact receiver-count
 *     assertion in PublishDeliversToSingleSubscriber).
 * Added (spec §1/§2):
 *   - unsubscribe-then-publish: after UNSUBSCRIBE the callback must no longer fire.
 *   - PUBSUB CHANNELS / NUMSUB introspection (raw `command<qb::json>` — no dedicated
 *     wrapper exists in publish_commands.h).
 *   - binary payload with an embedded NUL byte survives the round-trip intact.
 *
 * Coroutine convention: gtest fatal `ASSERT_*` expands to a bare `return;` which is
 * ill-formed inside a coroutine body. Inside `task<void>` lambdas we use `EXPECT_*`,
 * and `CORO_REQUIRE` for an early `co_return` guard before any dependent deref.
 */

#include <atomic>
#include <gtest/gtest.h>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include "../redis.h"
#include "../../shared/redis_integration_fixture.h"
#include "../../shared/pubsub_wait.h"

using namespace qb::io;
using qb::redis::test::pubsub_drain_for;
using qb::redis::test::pubsub_wait_count;
using qb::redis::test::pubsub_wait_until;

/// Coroutine-safe early-return guard (ASSERT_* cannot be used inside a coroutine).
#define CORO_REQUIRE(cond, done_flag)                                  \
    if (!(cond)) {                                                     \
        ADD_FAILURE() << "CORO_REQUIRE failed: " #cond;               \
        done_flag = true;                                             \
        co_return;                                                    \
    }

namespace {
constexpr const char *kTestMessage = "Hello World";
} // namespace

// ============================================================================
// Fixture: a dedicated publisher connection on top of the base `redis` client.
// (cb_consumer subscribers are created per-test so each owns its own callback.)
// ============================================================================

class PubSubFanoutTest : public ProtocolModesTestBase {
protected:
    qb::redis::tcp::client publisher{REDIS_URI_PROTOCOL};

    void
    SetUp() override {
        ProtocolModesTestBase::SetUp();
        if (IsSkipped())
            return;
        ASSERT_TRUE(qb::io::async::run_sync(publisher.connect()))
            << "publisher connection failed";
    }
};

INSTANTIATE_PROTOCOL_MODES(PubSubFanoutTest);

// =============== BASIC FAN-OUT (1 subscriber) ===============

TEST_P(PubSubFanoutTest, PublishDeliversToSingleSubscriber) {
    std::atomic<size_t>      message_count{0};
    std::vector<std::string> payloads;
    std::mutex               mtx;
    const auto               ch = protocol_key("pub");

    qb::redis::tcp::cb_consumer consumer{REDIS_URI_PROTOCOL, [&](auto &&msg) {
                                             std::lock_guard<std::mutex> lock(mtx);
                                             payloads.emplace_back(msg.payload);
                                             ++message_count;
                                         }};
    ASSERT_TRUE(qb::io::async::run_sync(consumer.connect()));

    bool done = false;
    auto sub  = [&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CONSUMER(consumer, done);
        auto r = co_await consumer.subscribe(ch);
        EXPECT_TRUE(r.ok()) << r.error();
        CORO_REQUIRE(r.ok() && r.result().channel.has_value(), done);
        EXPECT_EQ(*r.result().channel, ch);
        done = true;
    };
    qb::io::async::coro_scheduler().spawn(sub());
    run_coro_test_until(done);

    done     = false;
    auto pub = [&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CLIENT(publisher, done);
        auto r = co_await publisher.publish(ch, kTestMessage);
        EXPECT_TRUE(r.ok()) << r.error();
        EXPECT_EQ(r.result(), 1) << "exactly one subscriber should have received it";
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

    CO_IGNORE(qb::io::async::run_sync(consumer.unsubscribe(ch)));
}

// =============== PATTERN FAN-OUT ===============

TEST_P(PubSubFanoutTest, PublishDeliversToPatternSubscriber) {
    std::atomic<size_t> message_count{0};
    const auto          pat = protocol_key("t") + "_*";
    const auto          ch  = protocol_key("t") + "_channel";

    qb::redis::tcp::cb_consumer consumer{REDIS_URI_PROTOCOL, [&, ch](auto &&msg) {
                                             EXPECT_EQ(msg.payload, kTestMessage);
                                             EXPECT_EQ(msg.channel, ch);
                                             ++message_count;
                                         }};
    ASSERT_TRUE(qb::io::async::run_sync(consumer.connect()));

    bool done = false;
    auto sub  = [&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CONSUMER(consumer, done);
        auto r = co_await consumer.psubscribe(pat);
        EXPECT_TRUE(r.ok()) << r.error();
        CORO_REQUIRE(r.ok() && r.result().channel.has_value(), done);
        EXPECT_EQ(*r.result().channel, pat);
        done = true;
    };
    qb::io::async::coro_scheduler().spawn(sub());
    run_coro_test_until(done);

    done     = false;
    auto pub = [&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CLIENT(publisher, done);
        auto r = co_await publisher.publish(ch, kTestMessage);
        EXPECT_TRUE(r.ok()) << r.error();
        EXPECT_EQ(r.result(), 1);
        done = true;
    };
    qb::io::async::coro_scheduler().spawn(pub());
    run_coro_test_until(done);

    ASSERT_TRUE(pubsub_wait_count(message_count, 1));
    EXPECT_EQ(message_count, 1u);

    CO_IGNORE(qb::io::async::run_sync(consumer.punsubscribe(pat)));
}

// =============== FAN-OUT TO MULTIPLE SUBSCRIBERS ===============

TEST_P(PubSubFanoutTest, PublishDeliversToAllSubscribersAndCountsThem) {
    std::atomic<size_t> count1{0};
    std::atomic<size_t> count2{0};
    const auto          ch = protocol_key("multi");

    qb::redis::tcp::cb_consumer consumer1{REDIS_URI_PROTOCOL, [&](auto &&msg) {
                                              EXPECT_EQ(msg.payload, kTestMessage);
                                              ++count1;
                                          }};
    qb::redis::tcp::cb_consumer consumer2{REDIS_URI_PROTOCOL, [&](auto &&msg) {
                                              EXPECT_EQ(msg.payload, kTestMessage);
                                              ++count2;
                                          }};
    ASSERT_TRUE(qb::io::async::run_sync(consumer1.connect()));
    ASSERT_TRUE(qb::io::async::run_sync(consumer2.connect()));

    bool done = false;
    auto sub  = [&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CONSUMER(consumer1, done);
        PROTOCOL_ENSURE_RESP3_CONSUMER(consumer2, done);
        EXPECT_TRUE((co_await consumer1.subscribe(ch)).ok());
        EXPECT_TRUE((co_await consumer2.subscribe(ch)).ok());
        done = true;
    };
    qb::io::async::coro_scheduler().spawn(sub());
    run_coro_test_until(done);

    done     = false;
    auto pub = [&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CLIENT(publisher, done);
        auto r = co_await publisher.publish(ch, kTestMessage);
        EXPECT_TRUE(r.ok()) << r.error();
        EXPECT_EQ(r.result(), 2) << "PUBLISH must report both subscribers";
        done = true;
    };
    qb::io::async::coro_scheduler().spawn(pub());
    run_coro_test_until(done);

    ASSERT_TRUE(pubsub_wait_until([&] { return count1 >= 1 && count2 >= 1; }));
    EXPECT_EQ(count1, 1u);
    EXPECT_EQ(count2, 1u);

    CO_IGNORE(qb::io::async::run_sync(consumer1.unsubscribe(ch)));
    CO_IGNORE(qb::io::async::run_sync(consumer2.unsubscribe(ch)));
}

// =============== UNSUBSCRIBE THEN PUBLISH: CALLBACK MUST NOT FIRE ===============
//
// Added per spec: after a clean UNSUBSCRIBE, a later PUBLISH to the same channel
// must (a) report zero receivers and (b) never invoke the consumer's callback.
// Deterministic because we drain the loop for a settle window and assert the
// counter stayed put — a stray late delivery WOULD be observed and fail this.

TEST_P(PubSubFanoutTest, PublishAfterUnsubscribeDoesNotFireCallback) {
    std::atomic<size_t> message_count{0};
    const auto          ch = protocol_key("unsub");

    qb::redis::tcp::cb_consumer consumer{REDIS_URI_PROTOCOL,
                                         [&](auto &&) { ++message_count; }};
    ASSERT_TRUE(qb::io::async::run_sync(consumer.connect()));

    bool done = false;
    auto sub  = [&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CONSUMER(consumer, done);
        EXPECT_TRUE((co_await consumer.subscribe(ch)).ok());
        done = true;
    };
    qb::io::async::coro_scheduler().spawn(sub());
    run_coro_test_until(done);

    done      = false;
    auto pub1 = [&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CLIENT(publisher, done);
        auto r = co_await publisher.publish(ch, kTestMessage);
        EXPECT_TRUE(r.ok());
        EXPECT_EQ(r.result(), 1);
        done = true;
    };
    qb::io::async::coro_scheduler().spawn(pub1());
    run_coro_test_until(done);
    ASSERT_TRUE(pubsub_wait_count(message_count, 1));
    EXPECT_EQ(message_count, 1u);

    // Unsubscribe; the confirmation must report zero channels remaining.
    done       = false;
    auto unsub = [&]() -> qb::io::async::task<void> {
        auto r = co_await consumer.unsubscribe(ch);
        EXPECT_TRUE(r.ok()) << r.error();
        EXPECT_EQ(r.result().num, 0);
        done = true;
    };
    qb::io::async::coro_scheduler().spawn(unsub());
    run_coro_test_until(done);

    // Publish again: zero receivers, and the callback must NOT advance.
    done      = false;
    auto pub2 = [&]() -> qb::io::async::task<void> {
        auto r = co_await publisher.publish(ch, kTestMessage);
        EXPECT_TRUE(r.ok());
        EXPECT_EQ(r.result(), 0) << "no subscribers should remain after unsubscribe";
        done = true;
    };
    qb::io::async::coro_scheduler().spawn(pub2());
    run_coro_test_until(done);

    pubsub_drain_for(); // keep the loop live so a stray delivery would be caught
    EXPECT_EQ(message_count, 1u) << "callback fired after unsubscribe";
}

// =============== PUBSUB INTROSPECTION: CHANNELS / NUMSUB ===============
//
// No dedicated wrapper exists for the PUBSUB sub-command family; drive it through
// the raw `command<qb::json>` escape hatch on the publisher connection. Two
// subscribers on the same channel let us assert both the channel listing and the
// per-channel subscriber count exactly.

TEST_P(PubSubFanoutTest, PubsubChannelsAndNumsubReportSubscribers) {
    const auto ch = protocol_key("introspect");

    qb::redis::tcp::cb_consumer consumer1{REDIS_URI_PROTOCOL, [](auto &&) {}};
    qb::redis::tcp::cb_consumer consumer2{REDIS_URI_PROTOCOL, [](auto &&) {}};
    ASSERT_TRUE(qb::io::async::run_sync(consumer1.connect()));
    ASSERT_TRUE(qb::io::async::run_sync(consumer2.connect()));

    bool done = false;
    auto sub  = [&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CONSUMER(consumer1, done);
        PROTOCOL_ENSURE_RESP3_CONSUMER(consumer2, done);
        EXPECT_TRUE((co_await consumer1.subscribe(ch)).ok());
        EXPECT_TRUE((co_await consumer2.subscribe(ch)).ok());
        done = true;
    };
    qb::io::async::coro_scheduler().spawn(sub());
    run_coro_test_until(done);

    done                = false;
    bool      channel_listed = false;
    long long numsub         = -1;
    auto introspect          = [&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CLIENT(publisher, done);

        // PUBSUB CHANNELS <pattern> -> array of active channel names matching pattern.
        auto chans = co_await publisher.command<qb::json>("PUBSUB", "CHANNELS", ch);
        EXPECT_TRUE(chans.ok()) << chans.error();
        CORO_REQUIRE(chans.ok() && chans.result().is_array(), done);
        for (const auto &c : chans.result()) {
            if (c.is_string() && c.template get<std::string>() == ch)
                channel_listed = true;
        }

        // PUBSUB NUMSUB <channel> -> RESP2: flat [channel, count]; RESP3 may decode
        // as a {channel: count} map. Handle both shapes.
        auto ns = co_await publisher.command<qb::json>("PUBSUB", "NUMSUB", ch);
        EXPECT_TRUE(ns.ok()) << ns.error();
        CORO_REQUIRE(ns.ok(), done);
        if (ns.result().is_object()) {
            CORO_REQUIRE(ns.result().contains(ch), done);
            numsub = ns.result()[ch].template get<long long>();
        } else {
            CORO_REQUIRE(ns.result().is_array() && ns.result().size() == 2u, done);
            EXPECT_EQ(ns.result()[0].template get<std::string>(), ch);
            numsub = ns.result()[1].template get<long long>();
        }
        done = true;
    };
    qb::io::async::coro_scheduler().spawn(introspect());
    run_coro_test_until(done);

    EXPECT_TRUE(channel_listed) << "subscribed channel absent from PUBSUB CHANNELS";
    EXPECT_EQ(numsub, 2) << "two subscribers expected on the channel";

    CO_IGNORE(qb::io::async::run_sync(consumer1.unsubscribe(ch)));
    CO_IGNORE(qb::io::async::run_sync(consumer2.unsubscribe(ch)));
}

// =============== EDGE: NO SUBSCRIBERS ===============

TEST_P(PubSubFanoutTest, PublishToChannelWithNoSubscribersReturnsZero) {
    bool done = false;
    auto pub  = [&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CLIENT(publisher, done);
        auto r = co_await publisher.publish(protocol_key("nobody"), kTestMessage);
        EXPECT_TRUE(r.ok()) << r.error();
        EXPECT_EQ(r.result(), 0);
        done = true;
    };
    qb::io::async::coro_scheduler().spawn(pub());
    run_coro_test_until(done);
}

// =============== EDGE: EMPTY PAYLOAD ===============

TEST_P(PubSubFanoutTest, PublishEmptyPayloadDeliversEmptyString) {
    std::atomic<size_t>        message_count{0};
    std::optional<std::string> received;
    std::mutex                 mtx;
    const auto                 ch = protocol_key("empty");

    qb::redis::tcp::cb_consumer consumer{REDIS_URI_PROTOCOL, [&](auto &&msg) {
                                             std::lock_guard<std::mutex> lock(mtx);
                                             received = std::string(msg.payload);
                                             ++message_count;
                                         }};
    ASSERT_TRUE(qb::io::async::run_sync(consumer.connect()));

    bool done = false;
    auto sub  = [&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CONSUMER(consumer, done);
        EXPECT_TRUE((co_await consumer.subscribe(ch)).ok());
        done = true;
    };
    qb::io::async::coro_scheduler().spawn(sub());
    run_coro_test_until(done);

    done     = false;
    auto pub = [&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CLIENT(publisher, done);
        auto r = co_await publisher.publish(ch, "");
        EXPECT_TRUE(r.ok()) << r.error();
        EXPECT_EQ(r.result(), 1);
        done = true;
    };
    qb::io::async::coro_scheduler().spawn(pub());
    run_coro_test_until(done);

    ASSERT_TRUE(pubsub_wait_count(message_count, 1));
    {
        std::lock_guard<std::mutex> lock(mtx);
        ASSERT_TRUE(received.has_value());
        EXPECT_EQ(*received, "");
    }

    CO_IGNORE(qb::io::async::run_sync(consumer.unsubscribe(ch)));
}

// =============== EDGE: BINARY PAYLOAD WITH EMBEDDED NUL ===============
//
// Added per spec: RESP bulk strings are length-prefixed and binary-safe; a payload
// containing an embedded '\0' (and other non-printable bytes) must survive the
// publish -> deliver round-trip byte-for-byte, not be truncated at the NUL.

TEST_P(PubSubFanoutTest, PublishBinaryPayloadWithNulRoundTrips) {
    const std::string binary = std::string("a\0b\x01\x02\xff" "c", 7); // embeds NUL at [1]
    ASSERT_EQ(binary.size(), 7u);
    ASSERT_EQ(binary[1], '\0');

    std::atomic<size_t> message_count{0};
    std::string         received;
    std::mutex          mtx;
    const auto          ch = protocol_key("binary");

    qb::redis::tcp::cb_consumer consumer{REDIS_URI_PROTOCOL, [&](auto &&msg) {
                                             std::lock_guard<std::mutex> lock(mtx);
                                             received.assign(msg.payload.data(), msg.payload.size());
                                             ++message_count;
                                         }};
    ASSERT_TRUE(qb::io::async::run_sync(consumer.connect()));

    bool done = false;
    auto sub  = [&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CONSUMER(consumer, done);
        EXPECT_TRUE((co_await consumer.subscribe(ch)).ok());
        done = true;
    };
    qb::io::async::coro_scheduler().spawn(sub());
    run_coro_test_until(done);

    done     = false;
    auto pub = [&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CLIENT(publisher, done);
        auto r = co_await publisher.publish(ch, binary);
        EXPECT_TRUE(r.ok()) << r.error();
        EXPECT_EQ(r.result(), 1);
        done = true;
    };
    qb::io::async::coro_scheduler().spawn(pub());
    run_coro_test_until(done);

    ASSERT_TRUE(pubsub_wait_count(message_count, 1));
    {
        std::lock_guard<std::mutex> lock(mtx);
        ASSERT_EQ(received.size(), binary.size()) << "binary payload was truncated";
        EXPECT_EQ(received, binary);
    }

    CO_IGNORE(qb::io::async::run_sync(consumer.unsubscribe(ch)));
}
