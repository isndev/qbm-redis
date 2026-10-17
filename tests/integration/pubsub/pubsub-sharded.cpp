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
 * @file pubsub-sharded.cpp
 * @brief Sharded pub/sub against a live server (Huly QB-127): SSUBSCRIBE, SPUBLISH, SUNSUBSCRIBE.
 *
 * Redis 7+ (the server under test answers SSUBSCRIBE; a server that does not is skipped, with the reason).
 * What is proved, in RESP2 and RESP3:
 *   - SSUBSCRIBE confirms with the channel and the count of shard subscriptions; SPUBLISH reaches the
 *     subscriber, which receives an `smessage` as a message (channel and payload), and answers 1;
 *   - SUNSUBSCRIBE of the channel confirms with a count of 0; an unsubscribe-all of the shard channels
 *     resolves once every shard channel confirmed, and the reply FIFO stays aligned (a PING after it is
 *     answered as a PING).
 */

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

#define CORO_REQUIRE(cond, done_flag)                   \
    if (!(cond)) {                                      \
        ADD_FAILURE() << "CORO_REQUIRE failed: " #cond; \
        done_flag = true;                               \
        co_return;                                      \
    }

class PubSubShardedTest : public ProtocolModesTestBase {
protected:
    qb::redis::tcp::client publisher{REDIS_URI_PROTOCOL};

    void
    SetUp() override {
        ProtocolModesTestBase::SetUp();
        if (IsSkipped())
            return;
        ASSERT_TRUE(qb::io::async::run_sync(publisher.connect()));
    }
};

INSTANTIATE_PROTOCOL_MODES(PubSubShardedTest);

TEST_P(PubSubShardedTest, SsubscribeConfirmsSpublishDeliversAndSunsubscribeEndsAtZero) {
    std::atomic<size_t>      received{0};
    std::vector<std::string> seen;
    std::mutex               mtx;
    const auto               ch = protocol_key("shard");

    qb::redis::tcp::cb_consumer cons{REDIS_URI_PROTOCOL, [&](qb::redis::message &&msg) {
                                         std::lock_guard<std::mutex> lock(mtx);
                                         seen.push_back(msg.channel + "=" + msg.payload);
                                         ++received;
                                     }};
    ASSERT_TRUE(qb::io::async::run_sync(cons.connect()));

    bool done    = false;
    bool skipped = false;
    auto sub     = [&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CONSUMER(cons, done);
        auto r = co_await cons.ssubscribe(ch);
        if (!r.ok() && r.error().find("unknown command") != std::string::npos) {
            skipped = true; // a server before Redis 7
            done    = true;
            co_return;
        }
        EXPECT_TRUE(r.ok()) << r.error();
        CORO_REQUIRE(r.ok() && r.result().channel.has_value(), done);
        EXPECT_EQ(*r.result().channel, ch);
        EXPECT_EQ(r.result().num, 1);
        done = true;
    };
    qb::io::async::coro_scheduler().spawn(sub());
    run_coro_test_until(done);
    if (skipped)
        GTEST_SKIP() << "the server does not know SSUBSCRIBE (sharded pub/sub is Redis 7+)";

    done     = false;
    auto pub = [&]() -> qb::io::async::task<void> {
        auto r = co_await publisher.spublish(ch, "hello");
        EXPECT_TRUE(r.ok()) << r.error();
        EXPECT_EQ(r.result(), 1) << "the one shard subscriber received it";
        done = true;
    };
    qb::io::async::coro_scheduler().spawn(pub());
    run_coro_test_until(done);
    ASSERT_TRUE(pubsub_wait_count(received, 1));
    {
        std::lock_guard<std::mutex> lock(mtx);
        ASSERT_EQ(seen.size(), 1u);
        EXPECT_EQ(seen[0], ch + "=hello") << "an smessage arrives as a message";
    }

    done       = false;
    auto unsub = [&]() -> qb::io::async::task<void> {
        auto r = co_await cons.sunsubscribe(ch);
        EXPECT_TRUE(r.ok()) << r.error();
        CORO_REQUIRE(r.ok() && r.result().channel.has_value(), done);
        EXPECT_EQ(*r.result().channel, ch);
        EXPECT_EQ(r.result().num, 0);
        done = true;
    };
    qb::io::async::coro_scheduler().spawn(unsub());
    run_coro_test_until(done);
}

TEST_P(PubSubShardedTest, SunsubscribeAllResolvesOnTheLastShardAndKeepsTheFifoAligned) {
    qb::redis::tcp::cb_consumer cons{REDIS_URI_PROTOCOL, [](qb::redis::message &&) {}};
    ASSERT_TRUE(qb::io::async::run_sync(cons.connect()));
    const auto a = protocol_key("shard-a");
    const auto b = protocol_key("shard-b");

    bool done    = false;
    bool skipped = false;
    auto flow    = [&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CONSUMER(cons, done);
        auto ra = co_await cons.ssubscribe(a);
        if (!ra.ok() && ra.error().find("unknown command") != std::string::npos) {
            skipped = true;
            done    = true;
            co_return;
        }
        CORO_REQUIRE(ra.ok(), done);
        auto rb = co_await cons.ssubscribe(b); // a separate call: on a cluster the two may sit on two slots
        CORO_REQUIRE(rb.ok(), done);
        EXPECT_EQ(rb.result().num, 2);
        auto all = co_await cons.sunsubscribe();
        EXPECT_TRUE(all.ok()) << all.error();
        EXPECT_EQ(all.result().num, 0) << "resolved on the last shard channel's confirmation";
        EXPECT_EQ(cons.pending_reply_count(), 0u);
        done = true;
    };
    qb::io::async::coro_scheduler().spawn(flow());
    run_coro_test_until(done);
    if (skipped)
        GTEST_SKIP() << "the server does not know SSUBSCRIBE (sharded pub/sub is Redis 7+)";

    done      = false;
    auto ping = [&]() -> qb::io::async::task<void> {
        auto r = co_await cons.ping();
        EXPECT_TRUE(r.ok()) << r.error() << " -- the FIFO is no longer aligned";
        done = true;
    };
    qb::io::async::coro_scheduler().spawn(ping());
    run_coro_test_until(done);
}
