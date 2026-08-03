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
 * @file pubsub-pipeline-desync.cpp
 * @brief The high-value, deterministic pub/sub FIFO-integrity tests.
 *
 * Migrated from `test-subscription-commands.cpp` (the two `PIPELINED_*` tests and the
 * throwing-message-callback containment test). These are the framework-specific,
 * deterministic core of the pub/sub suite — isolated here, away from the
 * timing-fragile delivery tests (spec §1/§2 split, dossier c11 rationale).
 *
 * What they prove:
 *   1. A single (P)SUBSCRIBE of N channels makes Redis emit N confirmation frames,
 *      but the caller registered exactly ONE handler. When two such commands are
 *      pipelined back-to-back (loop NOT drained in between), each handler must
 *      resolve exactly once, on its OWN command's final confirmation — the N
 *      confirmations of command #1 must not steal command #2's handler. Asserted via
 *      `await()` (deterministic drain, no sleep), `pending_reply_count() == 0`, and
 *      the exact `num` progression (3 then 4).
 *   2. Pipelined subscribe(3) then unsubscribe-all: the unsubscribe-all handler
 *      expects exactly the predicted confirmation count and resolves on the final
 *      `num == 0` frame, not a stray subscribe confirmation.
 *   3. A throwing message callback is contained in the MESSAGE delivery path and must
 *      not escalate to the dispatcher's outer catch (which would fail an unrelated
 *      pending command and desync the reply/command FIFO): the consumer survives and
 *      keeps serving commands.
 *
 * Defect fixed (spec §7.C): the throwing-callback test's fixed `sleep(100ms)` "let
 * the cb fire" gate is replaced by a delivery-driven wait on a pre-throw counter.
 */

#include <atomic>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <vector>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include "../../shared/pubsub_wait.h"
#include "../../shared/redis_integration_fixture.h"
#include <qbm/redis/redis.h>

using namespace qb::io;
using qb::redis::test::pubsub_wait_count;

namespace {
constexpr const char *kTestMessage = "Hello World";
} // namespace

// ============================================================================
// Fixture: a publisher on top of the base client (consumers are per-test).
// ============================================================================

class PubSubPipelineTest : public ProtocolModesTestBase {
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

INSTANTIATE_PROTOCOL_MODES(PubSubPipelineTest);

// =============== PIPELINED MULTI-CHANNEL: no handler cross-resolve ===============

TEST_P(PubSubPipelineTest, PipelinedSubscribeCommandsEachResolveOnceOnOwnFinalFrame) {
    qb::redis::tcp::cb_consumer c{REDIS_URI_PROTOCOL, [](auto &&) {}};
    ASSERT_TRUE(qb::io::async::run_sync(c.connect()));

    int         n1 = 0, n2 = 0;
    bool        ok1 = false, ok2 = false;
    long long   num1 = 0, num2 = 0;
    std::string ch1_last, ch2_last;

    const auto a  = protocol_key("pda");
    const auto b  = protocol_key("pdb");
    const auto cc = protocol_key("pdc");
    const auto d  = protocol_key("pdd");

    bool completed = false;
    auto task      = [&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CONSUMER(c, completed);
        // Two subscribe commands issued back-to-back WITHOUT draining the loop
        // between them.
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
    run_coro_test_until(completed);

    c.await(); // deterministically drain all 4 confirmation frames
    EXPECT_EQ(c.pending_reply_count(), 0u);

    EXPECT_EQ(n1, 1) << "first handler must fire exactly once";
    EXPECT_EQ(n2, 1) << "second handler must fire exactly once";
    EXPECT_TRUE(ok1);
    EXPECT_TRUE(ok2);
    EXPECT_EQ(ch1_last, cc) << "command #1 resolves on its OWN last channel, not channel b";
    EXPECT_EQ(ch2_last, d);
    EXPECT_EQ(num1, 3) << "3 channels active when the first command resolves";
    EXPECT_EQ(num2, 4) << "4 channels active when the second resolves";

    CO_IGNORE(qb::io::async::run_sync(c.unsubscribe("")));
}

// =============== PIPELINED SUBSCRIBE THEN UNSUBSCRIBE-ALL ===============

TEST_P(PubSubPipelineTest, PipelinedSubscribeThenUnsubscribeAllResolvesOnZeroFrame) {
    qb::redis::tcp::cb_consumer c{REDIS_URI_PROTOCOL, [](auto &&) {}};
    ASSERT_TRUE(qb::io::async::run_sync(c.connect()));

    int       n_sub = 0, n_unsub = 0;
    bool      ok_sub = false, ok_unsub = false;
    long long num_unsub = -1;

    const auto a  = protocol_key("pua");
    const auto b  = protocol_key("pub");
    const auto cc = protocol_key("puc");

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
    run_coro_test_until(completed);

    c.await();
    EXPECT_EQ(c.pending_reply_count(), 0u);

    EXPECT_EQ(n_sub, 1);
    EXPECT_EQ(n_unsub, 1);
    EXPECT_TRUE(ok_sub);
    EXPECT_TRUE(ok_unsub);
    EXPECT_EQ(num_unsub, 0) << "all channels gone after unsubscribe-all";
}

// =============== THROWING MESSAGE CALLBACK IS CONTAINED ===============
//
// A throwing pub/sub message callback must be contained in the MESSAGE delivery (its
// own try/catch) and never escalate to the dispatcher's outer catch — which would
// fail an unrelated pending command and desync the reply/command FIFO. This confirms
// the consumer survives a throwing message callback and keeps serving commands.
//
// The pre-throw counter lets us wait for the message to actually be delivered (and the
// throw to occur) with a delivery-driven watchdog instead of a fixed sleep.

TEST_P(PubSubPipelineTest, ThrowingMessageCallbackDoesNotBreakConsumer) {
    std::atomic<size_t>         delivered{0};
    qb::redis::tcp::cb_consumer thrower{REDIS_URI_PROTOCOL, [&](auto &&) {
                                            ++delivered; // observed BEFORE the throw
                                            throw std::runtime_error("boom in message cb");
                                        }};
    ASSERT_TRUE(qb::io::async::run_sync(thrower.connect()));

    const auto ch   = protocol_key("throwsub");
    bool       done = false;
    auto       sub  = [&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_CONSUMER(thrower, done);
        EXPECT_TRUE((co_await thrower.subscribe(ch)).ok());
        done = true;
    };
    qb::io::async::coro_scheduler().spawn(sub());
    run_coro_test_until(done);

    done     = false;
    auto pub = [&]() -> qb::io::async::task<void> {
        auto r = co_await publisher.publish(ch, kTestMessage);
        EXPECT_TRUE(r.ok());
        EXPECT_EQ(r.result(), 1);
        done = true;
    };
    qb::io::async::coro_scheduler().spawn(pub());
    run_coro_test_until(done);

    // Wait for the message to land (the callback runs and throws); the throw must be
    // contained, not crash the process or wedge the loop.
    ASSERT_TRUE(pubsub_wait_count(delivered, 1));
    EXPECT_EQ(delivered, 1u);

    // The consumer must still be healthy: a follow-up command resolves correctly.
    done          = false;
    bool unsub_ok = false;
    auto fin      = [&]() -> qb::io::async::task<void> {
        unsub_ok = (co_await thrower.unsubscribe(ch)).ok();
        done     = true;
    };
    qb::io::async::coro_scheduler().spawn(fin());
    run_coro_test_until(done);
    EXPECT_TRUE(unsub_ok) << "consumer must keep serving commands after a throwing callback";
}
