/*
 * qb - C++ Actor Framework
 * Copyright (C) 2011-2026 isndev (cpp.actor). All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use it except in compliance with the License.
 * See the License for the specific terms.
 */

/**
 * @file integration/resilience/coro-cancel-and-connection-loss.cpp
 * @brief The two things a live Redis connection can have done TO it from outside.
 *
 * The rest of this suite drives commands and client-initiated `disconnect()`. Two shapes were
 * untested against a live server:
 *
 *   1. **A coroutine destroyed while parked on an in-flight (blocking) command.** The
 *      destroy-while-parked class this project has fixed at ten sites in qb-io. `redis_awaiter`
 *      guards it with a `shared_ptr<bool> valid_` cleared in its destructor.
 *
 *      Do NOT expect AddressSanitizer to be the oracle for that guard: qb's coroutine frames
 *      come from a pooled freelist (`CoroutineFrameAllocator` in `qb/io/async/coroutine/task.h`),
 *      so `h.destroy()` returns the block to the pool, not to the allocator. The memory stays
 *      mapped, ASan never sees a free, and an un-retracted completion reads a perfectly valid
 *      (recycled) frame. Measured: removing the guard and destroying ONE coroutine passes clean
 *      under ASan.
 *
 *      The pool is LIFO, which is what makes the guard load-bearing — and what makes the hazard
 *      observable. The next spawn of the same size class gets that exact block, so an
 *      un-retracted completion resumes a DIFFERENT, innocent coroutine, out of turn, with a
 *      reply that was never its own. This case arms that deliberately. Negative control: delete
 *      `if (!*valid) return;` from `redis_awaiter::await_suspend` in `redis.h` and the recycled
 *      coroutine resumes on the first coroutine's BLPOP reply.
 *
 *   2. **A server-side drop mid-transaction.** Every "connection loss" case in this suite is a
 *      client-initiated `disconnect()`. `CLIENT KILL` issued from a second connection is the
 *      real thing: the socket dies under a queued MULTI with no cooperation from the client.
 *      Negative control: delete the `(*entry.handler)(nullptr)` drain from
 *      `Redis::on(disconnected)` and the queued commands' awaiters never resolve.
 *
 * This file also closes three command-surface gaps the same fixtures make cheap: `BZPOPMIN`,
 * `BZPOPMAX` and `XREAD ... BLOCK` were registered as blocking verbs but never actually issued
 * by any test.
 *
 * Tier: integration (REQUIRES live redis). Skip-not-fail via the shared fixture. NOTE the
 * fixture FLUSHALLs — never point this at a Redis that matters.
 */
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <qb/system/parse.h>
#include <qbm/redis/redis.h>

#include "../../shared/redis_integration_fixture.h"

namespace {

using qb::redis::test::redis_test_uri;

/**
 * @brief Pump the loop until @p pred or the watchdog expires; returns pred's final value.
 *
 * Wall clock bounds it as a hang detector only, never as a timing assumption.
 */
template <typename Pred>
bool
pump_until(Pred &&pred, std::chrono::milliseconds budget = std::chrono::milliseconds(15000)) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (!pred()) {
        qb::io::async::run(EVRUN_NOWAIT);
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
    }
    return true;
}

/**
 * @brief `blocked_clients` from `INFO clients`, or -1 if it could not be read.
 *
 * The reply decodes to a qb::json STRING holding the raw INFO body (`# Clients\r\nkey:value...`),
 * not to an object, so this parses the line rather than indexing a field. Prefix matching alone
 * would accept `blocked_clients:10`, hence the explicit line slice.
 */
[[nodiscard]] long long
blocked_clients(qb::redis::tcp::client &c) {
    auto info = qb::io::async::run_sync(c.info("clients"));
    if (!info.ok())
        return -1;
    const std::string                 body = info.result().is_string() ? info.result().get<std::string>() : info.result().dump();
    static constexpr std::string_view kKey = "blocked_clients:";
    const auto                        pos  = body.find(kKey);
    if (pos == std::string::npos)
        return -1;
    const auto        start = pos + kKey.size();
    const auto        end   = body.find_first_of("\r\n", start);
    const std::string value = body.substr(start, end == std::string::npos ? std::string::npos : end - start);
    const auto        n     = qb::to_number<long long>(value);
    return n ? *n : -1;
}

/**
 * @brief Free coroutine (not a lambda) that parks on `BLPOP key 0` and records the outcome.
 *
 * Deliberately not an immediately-invoked lambda: `task`'s `initial_suspend` is `suspend_always`,
 * so a `spawn(lambda(){...}())` closure is already dead when the body runs — see
 * qb/scripts/check-spawn-dangling-closure.py. Both cases below need two spawns with an IDENTICAL
 * frame size class (that is the recycling), so they share this one function.
 */
qb::io::async::task<void>
park_on_blpop(qb::redis::tcp::client *redis, std::string key, bool *resumed, bool *ok, std::string *popped_from) {
    auto reply = co_await redis->blpop({key}, 0);
    *ok        = reply.ok();
    if (reply.ok() && reply.result().has_value())
        *popped_from = reply.result()->first;
    *resumed = true;
    co_return;
}

/**
 * @brief Integration fixture that survives losing its own connection.
 *
 * These cases drop the fixture's connection ON PURPOSE, and the shared `TearDown()` then issues a
 * `FLUSHALL` on it. `redis_try_connect()` restores an UNBOUNDED command timeout before returning,
 * so that FLUSHALL waits on a reply that can never arrive — the binary hangs in teardown with
 * every assertion already passed, and CTest reports a timeout instead of a result. (Measured, on
 * the first run of `ServerSideClientKillResolvesTheInFlightCommand`.)
 *
 * Two belts, because a reconnect can itself fail: put a usable handle back if there is one, and
 * bound the command deadline either way so cleanup can only ever fail fast.
 */
class RedisResilienceTest : public qb::redis::test::RedisIntegrationTest {
protected:
    void
    TearDown() override {
        if (!IsSkipped()) {
            if (!redis.is_connected()) {
                redis.disconnect();
                (void) qb::io::async::run_sync(redis.connect());
            }
            redis.set_command_timeout(std::chrono::seconds(5));
        }
        qb::redis::test::RedisIntegrationTest::TearDown();
    }
};

} // namespace

// ------------------------------------------------------------------------------------------
// 1. Coroutine destroyed mid-flight
// ------------------------------------------------------------------------------------------

/**
 * @brief A destroyed coroutine's late reply must not resume whoever inherited its frame.
 *
 * Timeline (all against the live server; `pusher` is a second connection):
 *
 *   - coroutine A parks on `BLPOP orphan_key 0` — blocks server-side, indefinitely;
 *   - `cancel_spawned()` destroys A's frame; `~redis_awaiter` retracts `valid_` and the block
 *     goes back to the coroutine frame pool;
 *   - coroutine B is spawned through the SAME function, so the same size class, so (LIFO
 *     freelist) the same block. It parks on `BLPOP heir_key 0`, queued behind A's BLPOP;
 *   - `pusher` LPUSHes `orphan_key`. The server answers A's BLPOP. That reply belongs to a
 *     coroutine that no longer exists and must be dropped. If it is not, it resumes B — which
 *     is parked on a completely different key that nobody has pushed;
 *   - `pusher` LPUSHes `heir_key`; B resumes with ITS OWN reply, from its own key.
 *
 * The frame-identity check is an instrument check: without recycling the hazard is not armed and
 * the early-resume assertion proves nothing, so it says so rather than passing quietly.
 */
TEST_F(RedisResilienceTest, CoroutineDestroyedWhileParkedOnBlockingCommand) {
    const std::string orphan_key = "qb:resilience:orphan";
    const std::string heir_key   = "qb:resilience:heir";

    bool        orphan_resumed = false, orphan_ok = false;
    std::string orphan_from;
    auto orphan = qb::io::async::coro_scheduler().spawn_tracked(park_on_blpop(&redis, orphan_key, &orphan_resumed, &orphan_ok, &orphan_from));
    ASSERT_TRUE(orphan) << "spawn_tracked returned an empty handle — nothing was parked";
    void *const orphan_frame = orphan.address();

    // Let the BLPOP reach the server. A second connection is the witness: the client is parked
    // exactly when the server reports a blocked client, which is positive evidence rather than a
    // sleep-and-hope. (INFO decodes to qb::json, and `blocked_clients` may land as a number or a
    // string depending on the decoder, so match the serialized form for both.)
    qb::redis::tcp::client pusher{redis_test_uri()};
    ASSERT_TRUE(qb::io::async::run_sync(pusher.connect()));
    ASSERT_TRUE(pump_until([&] { return blocked_clients(pusher) == 1; }))
        << "the BLPOP never blocked on the server; the coroutine was not parked on real work";

    // Destroy the parked frame, then recycle it under an innocent coroutine.
    qb::io::async::coro_scheduler().cancel_spawned(orphan);

    bool        heir_resumed = false, heir_ok = false;
    std::string heir_from;
    auto        heir = qb::io::async::coro_scheduler().spawn_tracked(park_on_blpop(&redis, heir_key, &heir_resumed, &heir_ok, &heir_from));
    ASSERT_TRUE(heir);
    EXPECT_EQ(heir.address(), orphan_frame)
        << "the coroutine frame pool did not hand the cancelled frame back to the next spawn, so the recycled-frame hazard is NOT "
           "armed and the early-resume check below cannot observe it";

    // Answer the ORPHANED BLPOP.
    ASSERT_TRUE(qb::io::async::run_sync(pusher.lpush(orphan_key, "for-the-dead-one")).ok());

    // Give the reply every chance to land and be (mis)dispatched.
    const auto checkpoint = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < checkpoint)
        qb::io::async::run(EVRUN_NOWAIT);

    EXPECT_FALSE(orphan_resumed) << "the destroyed coroutine was resumed";
    EXPECT_FALSE(heir_resumed) << "a reply belonging to a coroutine that no longer exists resumed the coroutine that inherited its "
                                  "frame — on a key nobody has pushed to";

    // Now answer the heir's own BLPOP. It must get ITS key, which is also the protocol-sync check.
    ASSERT_TRUE(qb::io::async::run_sync(pusher.lpush(heir_key, "for-the-living-one")).ok());
    ASSERT_TRUE(pump_until([&] { return heir_resumed; })) << "the surviving coroutine never resumed: swallowing the orphaned reply "
                                                             "desynced the reply queue";
    EXPECT_TRUE(heir_ok) << "the surviving coroutine resumed with a failure";
    EXPECT_EQ(heir_from, heir_key) << "the surviving coroutine was handed the ORPHANED reply's key: the reply queue is off by one";
    EXPECT_FALSE(orphan_resumed);

    // The connection must still work.
    auto ping = qb::io::async::run_sync(redis.ping());
    EXPECT_TRUE(ping.ok()) << "the client is unusable after the orphaned reply";

    pusher.disconnect();
}

// ------------------------------------------------------------------------------------------
// 2. Server-side drop mid-transaction
// ------------------------------------------------------------------------------------------

/// Kill @p victim_id from a fresh connection; returns false with a reason on failure.
[[nodiscard]] bool
kill_client(long long victim_id, std::string &why) {
    qb::redis::tcp::client killer{redis_test_uri()};
    if (!qb::io::async::run_sync(killer.connect())) {
        why = "the killer connection could not be opened";
        return false;
    }
    auto killer_id = qb::io::async::run_sync(killer.client_id());
    if (!killer_id.ok() || killer_id.result() == victim_id) {
        why = "the killer is not a distinct connection";
        return false;
    }
    auto killed = qb::io::async::run_sync(killer.client_kill("", victim_id));
    killer.disconnect();
    if (!killed.ok()) {
        why = "CLIENT KILL ID " + std::to_string(victim_id) + " failed: " + killed.error();
        return false;
    }
    if (killed.result() < 1) {
        why = "CLIENT KILL ID " + std::to_string(victim_id) + " matched no connection (killed " + std::to_string(killed.result()) + ")";
        return false;
    }
    return true;
}

/**
 * @brief `CLIENT KILL` from another connection must resolve the in-flight command, not hang it.
 *
 * A coroutine is parked on `BLPOP key 0` — blocked server-side with no timeout, so nothing but
 * the drop can wake it. A second connection kills the first by client id: the socket dies under
 * an in-flight command with no cooperation from the client at all, which is the path
 * `on(disconnected)` exists for and which `disconnect()` (synchronous, client-initiated) never
 * takes.
 *
 * A hang here is the actual hazard — a caller blocked on a `co_await` has no other way to learn
 * the socket is gone — so the assertion is bounded and reports that, not a timeout.
 */
TEST_F(RedisResilienceTest, ServerSideClientKillResolvesTheInFlightCommand) {
    const std::string key = "qb:resilience:killed";

    auto id_reply = qb::io::async::run_sync(redis.client_id());
    ASSERT_TRUE(id_reply.ok()) << id_reply.error();
    const long long victim_id = id_reply.result();
    ASSERT_GT(victim_id, 0);

    bool        resumed = false, ok = true;
    std::string from;
    auto        handle = qb::io::async::coro_scheduler().spawn_tracked(park_on_blpop(&redis, key, &resumed, &ok, &from));
    ASSERT_TRUE(handle);

    // Positive evidence that the command really is parked server-side before we pull the socket.
    qb::redis::tcp::client observer{redis_test_uri()};
    ASSERT_TRUE(qb::io::async::run_sync(observer.connect()));
    ASSERT_TRUE(pump_until([&] { return blocked_clients(observer) == 1; })) << "the BLPOP never blocked on the server";
    observer.disconnect();

    std::string why;
    ASSERT_TRUE(kill_client(victim_id, why)) << why;

    ASSERT_TRUE(pump_until([&] { return resumed; })) << "the coroutine parked on the killed connection never resumed: the caller has "
                                                        "no other way to learn the socket is gone, so this is an unbounded hang";
    EXPECT_FALSE(ok) << "a command on a killed connection reported success";
    ASSERT_TRUE(pump_until([&] { return !redis.is_connected(); })) << "the client still reports connected after the server dropped it";
}

/**
 * @brief A transaction queued when the server drops the connection must not be applied.
 *
 * A Redis connection cannot be both blocked and mid-`MULTI` — inside a transaction every command
 * is answered `+QUEUED` at once — so this is the transaction half of the same event, kept
 * separate rather than faked: `MULTI`, a queued `SET`, then `CLIENT KILL` before any `EXEC`.
 *
 * Two things must hold, and only the second is about the server: the write must be invisible to
 * a connection that never saw the transaction, and the CLIENT must not still believe it is inside
 * a MULTI afterwards (`on(disconnected)` calls `reset_transaction_state()`; without it the next
 * `multi()` on a reconnected handle is rejected as a nested MULTI).
 */
TEST_F(RedisResilienceTest, ServerSideClientKillDiscardsTheQueuedTransaction) {
    const std::string key = "qb:resilience:txn";

    auto id_reply = qb::io::async::run_sync(redis.client_id());
    ASSERT_TRUE(id_reply.ok()) << id_reply.error();
    const long long victim_id = id_reply.result();

    ASSERT_TRUE(qb::io::async::run_sync(redis.multi()).ok());
    ASSERT_TRUE(qb::io::async::run_sync(redis.set(key, "must-not-exist")).ok()) << "the SET should have been QUEUED";

    std::string why;
    ASSERT_TRUE(kill_client(victim_id, why)) << why;
    ASSERT_TRUE(pump_until([&] { return !redis.is_connected(); })) << "the client still reports connected after the server dropped it";

    // Never applied — asserted from a connection that never saw the transaction.
    qb::redis::tcp::client witness{redis_test_uri()};
    ASSERT_TRUE(qb::io::async::run_sync(witness.connect()));
    auto exists = qb::io::async::run_sync(witness.exists(key));
    ASSERT_TRUE(exists.ok()) << exists.error();
    EXPECT_EQ(exists.result(), 0) << "a transaction queued on a killed connection was applied anyway";
    witness.disconnect();

    // The client's own transaction state must have been reset by the drop.
    redis.disconnect();
    ASSERT_TRUE(qb::io::async::run_sync(redis.connect())) << "could not reconnect the killed handle";
    EXPECT_TRUE(qb::io::async::run_sync(redis.multi()).ok())
        << "the client still thinks it is inside the MULTI the server dropped: the drop did not reset the transaction state";
    (void) qb::io::async::run_sync(redis.discard());
}

// ------------------------------------------------------------------------------------------
// 3. Blocking verbs that were registered but never issued
// ------------------------------------------------------------------------------------------

/**
 * @brief `BZPOPMIN` / `BZPOPMAX` / `XREAD ... BLOCK` round-trip against the live server.
 *
 * All three are in `is_blocking_command()`'s set — which is what suppresses the client-side
 * command deadline for them — yet no test ever issued one, so neither the serialization nor the
 * reply decoding of any of them had ever run against a real Redis. Short BLOCK timeouts keep the
 * case fast while still taking the blocking code path (`_inflight_blocking` is incremented for
 * exactly these verbs).
 */
TEST_F(RedisResilienceTest, BlockingSortedSetAndStreamVerbsRoundTrip) {
    const std::string zkey = "qb:resilience:z";
    const std::string skey = "qb:resilience:s";

    ASSERT_TRUE(qb::io::async::run_sync(redis.zadd(zkey, {{1.0, "low"}, {9.0, "high"}})).ok());

    auto lo = qb::io::async::run_sync(redis.bzpopmin({zkey}, std::chrono::seconds(2)));
    ASSERT_TRUE(lo.ok()) << lo.error();
    ASSERT_TRUE(lo.result().has_value()) << "BZPOPMIN timed out on a non-empty sorted set";
    EXPECT_EQ(std::get<0>(*lo.result()), zkey);
    EXPECT_EQ(std::get<1>(*lo.result()), "low") << "BZPOPMIN returned the wrong end of the set";
    EXPECT_DOUBLE_EQ(std::get<2>(*lo.result()), 1.0);

    auto hi = qb::io::async::run_sync(redis.bzpopmax({zkey}, std::chrono::seconds(2)));
    ASSERT_TRUE(hi.ok()) << hi.error();
    ASSERT_TRUE(hi.result().has_value()) << "BZPOPMAX timed out on a non-empty sorted set";
    EXPECT_EQ(std::get<1>(*hi.result()), "high") << "BZPOPMAX returned the wrong end of the set";
    EXPECT_DOUBLE_EQ(std::get<2>(*hi.result()), 9.0);

    // Empty set + a short BLOCK must time out cleanly (a nil reply), not error and not hang.
    auto empty = qb::io::async::run_sync(redis.bzpopmin({zkey}, std::chrono::seconds(1)));
    EXPECT_TRUE(empty.ok()) << "a BZPOPMIN that times out must not be an error: " << empty.error();
    EXPECT_FALSE(empty.result().has_value()) << "BZPOPMIN on an empty set returned a value";

    // XREAD with BLOCK over an entry that already exists: returns immediately from id 0.
    // `block` is a millisecond count, and it is the argument that puts XREAD on the blocking
    // path (`is_blocking_command`), which is the half that had never been issued.
    ASSERT_TRUE(qb::io::async::run_sync(redis.xadd(skey, {{"field", "value"}})).ok());
    auto read = qb::io::async::run_sync(redis.xread(skey, "0", std::nullopt, 500));
    ASSERT_TRUE(read.ok()) << read.error();
    EXPECT_FALSE(read.result().empty()) << "XREAD ... BLOCK returned nothing for a stream that already has an entry";
}
