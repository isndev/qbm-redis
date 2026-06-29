/*
 * qb - C++ Actor Framework
 * Copyright (C) 2011-2026 isndev (cpp.actor). All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

/**
 * @file integration/connection/pipeline.cpp
 * @brief Callback pipelining, await() draining, the RedisPipeline builder, RESP2/RESP3,
 *        and the noexcept-boundary containment guarantee.
 *
 * Integration tier — needs a live redis (env `REDIS_URI`, default tcp://localhost:6379).
 * Migrated from test-pipeline.cpp: the `guard++ < 50000` disconnect-drain busy-spin was
 * replaced with a watchdog-bounded pump; a middle-command-error case was added to prove
 * an error in the *middle* of a pipeline does not desync subsequent reply parsing; and
 * `RedisPipelineFlushIsAwaitOnClient` now asserts `pending_reply_count() == 0` after flush.
 */

#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include "../../shared/redis_integration_fixture.h"
#include "../redis.h"

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

using namespace qb::io;
using namespace qb::redis::test;

namespace {

[[nodiscard]] std::string
unique_prefix() {
#if defined(_WIN32)
    const int pid = static_cast<int>(_getpid());
#else
    const int pid = static_cast<int>(getpid());
#endif
    return std::string("qbm:pipe:") + std::to_string(pid) + ":";
}

// Pump the loop until `redis.pending_reply_count() == 0` or a watchdog fires. Replaces the
// fixed `guard++ < 50000` bound so a real hang fails loudly with a diagnostic instead of
// silently looping a CPU-speed-dependent number of times.
void
drain_until_empty(qb::redis::tcp::client &redis, qb::duration timeout = std::chrono::seconds(10)) {
    bool timed_out = false;
    auto watchdog  = qb::io::async::scoped_callback([&timed_out]() noexcept { timed_out = true; }, timeout);
    (void) watchdog;
    while (redis.pending_reply_count() > 0 && !timed_out) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
    if (redis.pending_reply_count() > 0) {
        ADD_FAILURE() << "pending replies did not drain within watchdog";
    }
}

// Non-parameterized callback-pipelining fixture; reuses the shared skip-not-throw connect.
class PipelineCallbackTest : public RedisIntegrationTest {};

} // namespace

TEST_F(PipelineCallbackTest, AwaitWithNoPendingReturnsImmediately) {
    EXPECT_EQ(redis.pending_reply_count(), 0u);
    redis.await();
    EXPECT_EQ(redis.pending_reply_count(), 0u);
}

TEST_F(PipelineCallbackTest, PendingReplyCountDrainsWithAwait) {
    const std::string key = unique_prefix() + "k1";
    std::atomic<int>  callbacks{0};

    EXPECT_EQ(redis.pending_reply_count(), 0u);

    redis.set(
        [&](qb::redis::Reply<qb::redis::status> &&r) {
            EXPECT_TRUE(r.ok());
            ++callbacks;
        },
        key, "v1");
    EXPECT_EQ(redis.pending_reply_count(), 1u);

    redis.get(
        [&](qb::redis::Reply<std::optional<std::string>> &&r) {
            EXPECT_TRUE(r.ok());
            ASSERT_TRUE(r.result().has_value());
            EXPECT_EQ(*r.result(), "v1");
            ++callbacks;
        },
        key);
    EXPECT_EQ(redis.pending_reply_count(), 2u);

    redis.await();
    EXPECT_EQ(redis.pending_reply_count(), 0u);
    EXPECT_EQ(callbacks.load(), 2);
}

TEST_F(PipelineCallbackTest, CallbacksRunInSendOrder) {
    std::vector<int> order;
    int              step = 0;

    redis.ping([&](qb::redis::Reply<std::string> &&r) {
        EXPECT_TRUE(r.ok());
        order.push_back(++step);
    });
    redis.ping([&](qb::redis::Reply<std::string> &&r) {
        EXPECT_TRUE(r.ok());
        order.push_back(++step);
    });
    redis.ping([&](qb::redis::Reply<std::string> &&r) {
        EXPECT_TRUE(r.ok());
        order.push_back(++step);
    });

    redis.await();
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
}

// A reply callback that throws a non-std exception must not cross the noexcept onMessage()
// boundary (which would std::terminate the whole process). The dispatcher's per-handler
// catch only covers std::exception; the onMessage backstop catches the rest. Before the fix
// this aborts the binary; after it the throw is swallowed and the connection keeps working.
TEST_F(PipelineCallbackTest, NonStdExceptionInCallbackDoesNotTerminate) {
    bool first_ran = false;
    redis.ping([&](qb::redis::Reply<std::string> &&) {
        first_ran = true;
        throw 42; // non-std-exception type — bypasses every catch(const std::exception&)
    });
    redis.await(); // dispatches the reply; the throw must be contained, not terminate
    EXPECT_TRUE(first_ran);

    bool second_ok = false;
    redis.ping([&](qb::redis::Reply<std::string> &&r) { second_ok = r.ok(); });
    redis.await();
    EXPECT_TRUE(second_ok);
}

TEST_F(PipelineCallbackTest, DeepPipelineIncrSequence) {
    const std::string      key = unique_prefix() + "ctr";
    std::vector<long long> values;
    constexpr int          kN = 20;

    for (int i = 0; i < kN; ++i) {
        redis.incr(
            [&values](qb::redis::Reply<long long> &&r) {
                ASSERT_TRUE(r.ok());
                values.push_back(r.result());
            },
            key);
    }
    redis.await();
    ASSERT_EQ(values.size(), static_cast<size_t>(kN));
    for (int i = 0; i < kN; ++i) {
        EXPECT_EQ(values[static_cast<size_t>(i)], static_cast<long long>(i + 1)) << "i=" << i;
    }
}

TEST_F(PipelineCallbackTest, TwoWavesSeparatedByAwait) {
    const std::string k1 = unique_prefix() + "w1";
    const std::string k2 = unique_prefix() + "w2";
    std::atomic<int>  phase1{0};
    std::atomic<int>  phase2{0};

    redis.set(
        [&](qb::redis::Reply<qb::redis::status> &&r) {
            EXPECT_TRUE(r.ok());
            ++phase1;
        },
        k1, "a");
    redis.await();
    EXPECT_EQ(phase1.load(), 1);
    EXPECT_EQ(redis.pending_reply_count(), 0u);

    redis.set(
        [&](qb::redis::Reply<qb::redis::status> &&r) {
            EXPECT_TRUE(r.ok());
            ++phase2;
        },
        k2, "b");
    redis.get(
        [&](qb::redis::Reply<std::optional<std::string>> &&r) {
            EXPECT_TRUE(r.ok());
            ASSERT_TRUE(r.result().has_value());
            EXPECT_EQ(*r.result(), "b");
            ++phase2;
        },
        k2);
    redis.await();
    EXPECT_EQ(phase2.load(), 2);
}

TEST_F(PipelineCallbackTest, EchoViaLowLevelCommand) {
    // A payload with an embedded NUL must round-trip byte-exact through the binary-safe
    // bulk-string codec (NUL is not a terminator on the wire).
    const std::string nul_payload = unique_prefix() + std::string("hello") + std::string(1, '\0') + std::string("world");
    std::string       out;
    redis.command<std::string>(
        [&out](qb::redis::Reply<std::string> &&r) {
            ASSERT_TRUE(r.ok());
            out = r.result();
        },
        "ECHO", nul_payload);
    redis.await();
    EXPECT_EQ(out, nul_payload);
}

TEST_F(PipelineCallbackTest, SecondCommandRedisErrorWrongType) {
    const std::string key = unique_prefix() + "wt";
    std::vector<bool> oks;

    redis.set([&oks](qb::redis::Reply<qb::redis::status> &&r) { oks.push_back(r.ok()); }, key, "string_value");
    redis.lpush([&oks](qb::redis::Reply<long long> &&r) { oks.push_back(r.ok()); }, key, "list");

    redis.await();
    ASSERT_EQ(oks.size(), 2u);
    EXPECT_TRUE(oks[0]);
    EXPECT_FALSE(oks[1]);
}

// An error in the MIDDLE of a pipeline must not desync the parser for the commands that
// follow it: cmd[0] SET ok, cmd[1] LPUSH-on-string is WRONGTYPE, cmd[2] GET must still
// parse correctly and return the original value. (Before reply-FIFO correctness this kind
// of mid-stream error shifted every subsequent reply by one.)
TEST_F(PipelineCallbackTest, MiddleCommandErrorDoesNotDesync) {
    const std::string key = unique_prefix() + "mid";
    std::vector<bool> oks;
    std::string       tail_value;
    bool              tail_has_value = false;

    redis.set([&oks](qb::redis::Reply<qb::redis::status> &&r) { oks.push_back(r.ok()); }, key, "payload");
    redis.lpush([&oks](qb::redis::Reply<long long> &&r) { oks.push_back(r.ok()); }, key, "x"); // WRONGTYPE
    redis.get(
        [&](qb::redis::Reply<std::optional<std::string>> &&r) {
            oks.push_back(r.ok());
            tail_has_value = r.ok() && r.result().has_value();
            if (tail_has_value)
                tail_value = *r.result();
        },
        key);

    redis.await();
    ASSERT_EQ(oks.size(), 3u);
    EXPECT_TRUE(oks[0]) << "leading SET should succeed";
    EXPECT_FALSE(oks[1]) << "middle LPUSH on a string key must be WRONGTYPE";
    EXPECT_TRUE(oks[2]) << "trailing GET must parse fine despite the middle error";
    ASSERT_TRUE(tail_has_value) << "GET after a mid-pipeline error returned no value (desync)";
    EXPECT_EQ(tail_value, "payload");
    EXPECT_EQ(redis.pending_reply_count(), 0u);
}

TEST_F(PipelineCallbackTest, CoroutineAfterCallbackAwait) {
    bool              completed = false;
    const std::string key       = unique_prefix() + "mixcoro";

    redis.set([](qb::redis::Reply<qb::redis::status> &&r) { EXPECT_TRUE(r.ok()); }, key, "z");
    redis.await();

    auto task = [this, &completed, key]() -> qb::io::async::task<void> {
        auto g = co_await redis.get(key);
        EXPECT_TRUE(g.ok());
        if (!(g.result().has_value())) {
            ADD_FAILURE() << "precondition failed: g.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*g.result(), "z");
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(task());
    run_coro_test_until(completed);
}

TEST_F(PipelineCallbackTest, DisconnectDeliversFailureToPendingCallbacks) {
    std::atomic<int> invocations{0};
    redis.ping([&](qb::redis::Reply<std::string> &&r) {
        ++invocations;
        EXPECT_FALSE(r.ok());
        EXPECT_EQ(r.error(), "disconnected");
    });
    EXPECT_EQ(redis.pending_reply_count(), 1u);
    redis.disconnect();

    drain_until_empty(redis);
    ASSERT_EQ(redis.pending_reply_count(), 0u) << "pending replies not cleared after disconnect";
    EXPECT_EQ(invocations.load(), 1);

    // TearDown runs FLUSHALL; reconnect after the explicit disconnect.
    ASSERT_TRUE(qb::io::async::run_sync(redis.connect()));
    ASSERT_TRUE(qb::io::async::run_sync(redis.flushall()).ok());
}

TEST_F(PipelineCallbackTest, RedisPipelineChainedCommandAndFlush) {
    const std::string key = unique_prefix() + "raw";
    std::string       got;

    qb::redis::tcp::pipeline pipe{redis};
    EXPECT_EQ(pipe.pending_reply_count(), 0u);

    pipe.command<qb::redis::status>([](qb::redis::Reply<qb::redis::status> &&r) { EXPECT_TRUE(r.ok()); }, "SET", key, "hello")
        .command<std::optional<std::string>>(
            [&got](qb::redis::Reply<std::optional<std::string>> &&r) {
                EXPECT_TRUE(r.ok());
                ASSERT_TRUE(r.result().has_value());
                got = *r.result();
            },
            "GET", key);

    EXPECT_EQ(pipe.pending_reply_count(), 2u);
    pipe.flush();
    EXPECT_EQ(redis.pending_reply_count(), 0u);
    EXPECT_EQ(got, "hello");
}

TEST_F(PipelineCallbackTest, RedisPipelineFlushIsAwaitOnClient) {
    const std::string key = unique_prefix() + "mix";
    std::atomic<int>  n{0};

    qb::redis::tcp::pipeline pipe{redis};
    pipe.client().set(
        [&](qb::redis::Reply<qb::redis::status> &&r) {
            EXPECT_TRUE(r.ok());
            ++n;
        },
        key, "x");
    pipe.client().get(
        [&](qb::redis::Reply<std::optional<std::string>> &&r) {
            EXPECT_TRUE(r.ok());
            ASSERT_TRUE(r.result().has_value());
            EXPECT_EQ(*r.result(), "x");
            ++n;
        },
        key);

    EXPECT_EQ(pipe.pending_reply_count(), 2u);
    pipe.flush();
    EXPECT_EQ(n.load(), 2);
    EXPECT_EQ(pipe.pending_reply_count(), 0u);
    EXPECT_EQ(redis.pending_reply_count(), 0u);
}

// ---------------------------------------------------------------------------
// RESP2 + RESP3 (HELLO) — same pipelining invariants after protocol negotiation
// ---------------------------------------------------------------------------

class PipelineProtocolModesTest : public ProtocolModesTestBase {};

INSTANTIATE_PROTOCOL_MODES(PipelineProtocolModesTest);

TEST_P(PipelineProtocolModesTest, CallbackPipelineSetGetAfterNegotiation) {
    bool completed = false;
    auto task      = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(task());
    run_coro_test_until(completed);

    const std::string key = protocol_key("pl_setget");
    std::atomic<int>  n{0};

    redis.set(
        [&n](qb::redis::Reply<qb::redis::status> &&r) {
            EXPECT_TRUE(r.ok());
            ++n;
        },
        key, "42");
    redis.get(
        [&n](qb::redis::Reply<std::optional<std::string>> &&r) {
            EXPECT_TRUE(r.ok());
            ASSERT_TRUE(r.result().has_value());
            EXPECT_EQ(*r.result(), "42");
            ++n;
        },
        key);
    redis.await();
    EXPECT_EQ(n.load(), 2);
    EXPECT_EQ(redis.pending_reply_count(), 0u);
}

TEST_P(PipelineProtocolModesTest, CoroutineThenCallbackPipelineInSameConnection) {
    bool completed = false;
    bool ping_ok   = false;
    auto task      = [this, &completed, &ping_ok]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto p = co_await redis.ping();
        EXPECT_TRUE(p.ok());
        ping_ok   = p.ok();
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(task());
    run_coro_test_until(completed);
    ASSERT_TRUE(ping_ok);

    const std::string key = protocol_key("pl_coro_cb");
    std::atomic<int>  cb{0};
    redis.set(
        [&cb](qb::redis::Reply<qb::redis::status> &&r) {
            EXPECT_TRUE(r.ok());
            ++cb;
        },
        key, "v");
    redis.del(
        [&cb](qb::redis::Reply<long long> &&r) {
            EXPECT_TRUE(r.ok());
            EXPECT_EQ(r.result(), 1);
            ++cb;
        },
        key);
    redis.await();
    EXPECT_EQ(cb.load(), 2);
}
