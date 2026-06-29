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
 * @file integration/list/list-commands.cpp
 * @brief Live-redis integration tests for the list command family (LPUSH/RPUSH/LPOP/RPOP,
 *        LRANGE/LINDEX/LSET/LTRIM/LREM/LINSERT/LPOS, LMOVE/RPOPLPUSH, blocking variants),
 *        exercised in both RESP2 and RESP3.
 *
 * Restructured from the legacy `test-list-commands.cpp`:
 *  - deleted the dead `TestListCommands : list_commands<TestListCommands>` CRTP shim (stub
 *    `command<>` returns, asserted nothing, never instantiated);
 *  - deleted 6 terse smoke dups (LPUSH_LRANGE / RPUSH_LLEN_LINDEX / LPOP_OPTIONAL /
 *    RPOP_OPTIONAL / LREM_INTEGER / LMPOP_LMOVE) — strict subsets of the CORO_* bodies kept;
 *  - removed the file-local `main()` (links the shared gtest-main);
 *  - replaced un-watchdogged busy-spins with `run_coro_test_until`;
 *  - ADDED a TRUE blocking test (BLPOP parked before the key exists, woken by a push from a
 *    second client), plus LSET/LINDEX out-of-range and LINSERT pivot-not-found edges.
 */

#include <algorithm>
#include <gtest/gtest.h>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include "../../shared/redis_integration_fixture.h"
#include "../redis.h"

using namespace qb::redis;
using namespace qb::redis::test;

namespace {

class ListProtocolModesTest : public ProtocolModesTestBase {};

INSTANTIATE_PROTOCOL_MODES(ListProtocolModesTest);

// LPUSH / RPUSH / LLEN — element counts after left/right pushes.
TEST_P(ListProtocolModesTest, PUSH) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("basic");

        auto l1 = co_await redis.lpush(key, "item1");
        EXPECT_TRUE(l1.ok()) << l1.error();
        EXPECT_EQ(l1.result(), 1);
        auto l2 = co_await redis.lpush(key, "item2");
        EXPECT_TRUE(l2.ok()) << l2.error();
        EXPECT_EQ(l2.result(), 2);
        auto l3 = co_await redis.lpush(key, "item3");
        EXPECT_TRUE(l3.ok()) << l3.error();
        EXPECT_EQ(l3.result(), 3);

        auto len1 = co_await redis.llen(key);
        EXPECT_TRUE(len1.ok()) << len1.error();
        EXPECT_EQ(len1.result(), 3);

        auto r4 = co_await redis.rpush(key, "item4");
        EXPECT_TRUE(r4.ok()) << r4.error();
        EXPECT_EQ(r4.result(), 4);
        auto r5 = co_await redis.rpush(key, "item5");
        EXPECT_TRUE(r5.ok()) << r5.error();
        EXPECT_EQ(r5.result(), 5);

        auto len2 = co_await redis.llen(key);
        EXPECT_TRUE(len2.ok()) << len2.error();
        EXPECT_EQ(len2.result(), 5);

        // After three LPUSH then two RPUSH: item3,item2,item1,item4,item5.
        auto all = co_await redis.lrange(key, 0, -1);
        EXPECT_TRUE(all.ok()) << all.error();
        EXPECT_EQ(all.result(), (std::vector<std::string>{"item3", "item2", "item1", "item4", "item5"}));

        completed = true;
    });
    run_coro_test_until(completed);
}

// LPOP / RPOP, single + multi-count + empty.
TEST_P(ListProtocolModesTest, POP) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("pop");

        auto seed = co_await redis.rpush(key, "item1", "item2", "item3", "item4", "item5");
        EXPECT_TRUE(seed.ok()) << seed.error();
        EXPECT_EQ(seed.result(), 5);

        auto left = co_await redis.lpop(key);
        if (!(left.ok() && left.result().has_value())) {
            ADD_FAILURE() << "precondition failed: left.ok() && left.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*left.result(), "item1");

        auto right = co_await redis.rpop(key);
        if (!(right.ok() && right.result().has_value())) {
            ADD_FAILURE() << "precondition failed: right.ok() && right.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*right.result(), "item5");

        auto len = co_await redis.llen(key);
        EXPECT_TRUE(len.ok());
        EXPECT_EQ(len.result(), 3);

        auto left2 = co_await redis.lpop(key, 2);
        EXPECT_TRUE(left2.ok());
        if (!(left2.result().size() == 2u)) {
            ADD_FAILURE() << "precondition failed: left2.result().size() == 2u";
            co_return;
        }
        EXPECT_EQ(left2.result()[0], "item2");
        EXPECT_EQ(left2.result()[1], "item3");

        auto len2 = co_await redis.llen(key);
        EXPECT_TRUE(len2.ok());
        EXPECT_EQ(len2.result(), 1);

        (void) co_await redis.lpop(key);
        auto empty = co_await redis.lpop(key);
        EXPECT_TRUE(empty.ok());
        EXPECT_FALSE(empty.result().has_value());

        completed = true;
    });
    run_coro_test_until(completed);
}

// LRANGE — full, partial and negative-index slices.
TEST_P(ListProtocolModesTest, RANGE) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("range");

        auto seed = co_await redis.rpush(key, "item1", "item2", "item3", "item4", "item5");
        EXPECT_TRUE(seed.ok()) << seed.error();

        auto all = co_await redis.lrange(key, 0, -1);
        EXPECT_TRUE(all.ok());
        EXPECT_EQ(all.result(), (std::vector<std::string>{"item1", "item2", "item3", "item4", "item5"}));

        auto mid = co_await redis.lrange(key, 1, 3);
        EXPECT_TRUE(mid.ok());
        EXPECT_EQ(mid.result(), (std::vector<std::string>{"item2", "item3", "item4"}));

        auto last2 = co_await redis.lrange(key, -2, -1);
        EXPECT_TRUE(last2.ok());
        EXPECT_EQ(last2.result(), (std::vector<std::string>{"item4", "item5"}));

        completed = true;
    });
    run_coro_test_until(completed);
}

// LINDEX / LSET happy-path plus out-of-range edges.
TEST_P(ListProtocolModesTest, INDEX_AND_SET) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("index");

        auto seed = co_await redis.rpush(key, "item1", "item2", "item3", "item4", "item5");
        EXPECT_TRUE(seed.ok()) << seed.error();

        auto at2 = co_await redis.lindex(key, 2);
        if (!(at2.ok() && at2.result().has_value())) {
            ADD_FAILURE() << "precondition failed: at2.ok() && at2.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*at2.result(), "item3");

        auto last = co_await redis.lindex(key, -1);
        if (!(last.ok() && last.result().has_value())) {
            ADD_FAILURE() << "precondition failed: last.ok() && last.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*last.result(), "item5");

        // LINDEX out of range → nil (not an error).
        auto oor = co_await redis.lindex(key, 99);
        EXPECT_TRUE(oor.ok()) << oor.error();
        EXPECT_FALSE(oor.result().has_value());

        auto set_r = co_await redis.lset(key, 1, "replaced");
        EXPECT_TRUE(set_r.ok()) << set_r.error();
        EXPECT_TRUE(set_r.result().ok());

        auto modified = co_await redis.lindex(key, 1);
        if (!(modified.ok() && modified.result().has_value())) {
            ADD_FAILURE() << "precondition failed: modified.ok() && modified.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*modified.result(), "replaced");

        // LSET out of range → error reply.
        auto set_oor = co_await redis.lset(key, 99, "nope");
        EXPECT_FALSE(set_oor.ok());
        EXPECT_FALSE(set_oor.error().empty());

        completed = true;
    });
    run_coro_test_until(completed);
}

// LTRIM — retains the requested inclusive window.
TEST_P(ListProtocolModesTest, TRIM) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("trim");

        auto seed = co_await redis.rpush(key, "item1", "item2", "item3", "item4", "item5");
        EXPECT_TRUE(seed.ok()) << seed.error();

        auto trim = co_await redis.ltrim(key, 1, 3);
        EXPECT_TRUE(trim.ok()) << trim.error();
        EXPECT_TRUE(trim.result().ok());

        auto remaining = co_await redis.lrange(key, 0, -1);
        EXPECT_TRUE(remaining.ok());
        EXPECT_EQ(remaining.result(), (std::vector<std::string>{"item2", "item3", "item4"}));

        completed = true;
    });
    run_coro_test_until(completed);
}

// LREM — positive count (front), negative count (back).
TEST_P(ListProtocolModesTest, REMOVE) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("remove");

        auto seed = co_await redis.rpush(key, "item1", "item2", "item3", "item2", "item4", "item2", "item5");
        EXPECT_TRUE(seed.ok()) << seed.error();

        auto rem = co_await redis.lrem(key, 2, "item2");
        EXPECT_TRUE(rem.ok()) << rem.error();
        EXPECT_EQ(rem.result(), 2);

        auto after = co_await redis.lrange(key, 0, -1);
        EXPECT_TRUE(after.ok());
        EXPECT_EQ(after.result().size(), 5u);
        EXPECT_EQ(std::count(after.result().begin(), after.result().end(), std::string("item2")), 1);

        auto rem2 = co_await redis.lrem(key, -1, "item2");
        EXPECT_TRUE(rem2.ok());
        EXPECT_EQ(rem2.result(), 1);

        auto after2 = co_await redis.lrange(key, 0, -1);
        EXPECT_TRUE(after2.ok());
        EXPECT_EQ(std::count(after2.result().begin(), after2.result().end(), std::string("item2")), 0);

        completed = true;
    });
    run_coro_test_until(completed);
}

// LINSERT before/after a pivot, plus pivot-not-found (→ -1).
TEST_P(ListProtocolModesTest, INSERT) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("insert");

        auto seed = co_await redis.rpush(key, "item1", "item3");
        EXPECT_TRUE(seed.ok()) << seed.error();

        auto before = co_await redis.linsert(key, InsertPosition::BEFORE, "item3", "item2");
        EXPECT_TRUE(before.ok()) << before.error();
        EXPECT_EQ(before.result(), 3);

        auto order = co_await redis.lrange(key, 0, -1);
        EXPECT_TRUE(order.ok());
        EXPECT_EQ(order.result(), (std::vector<std::string>{"item1", "item2", "item3"}));

        auto after = co_await redis.linsert(key, InsertPosition::AFTER, "item3", "item4");
        EXPECT_TRUE(after.ok()) << after.error();
        EXPECT_EQ(after.result(), 4);

        auto order2 = co_await redis.lrange(key, 0, -1);
        EXPECT_TRUE(order2.ok());
        EXPECT_EQ(order2.result(), (std::vector<std::string>{"item1", "item2", "item3", "item4"}));

        // Pivot not present → Redis returns -1 (no insert).
        auto missing = co_await redis.linsert(key, InsertPosition::BEFORE, "nonexistent", "x");
        EXPECT_TRUE(missing.ok()) << missing.error();
        EXPECT_EQ(missing.result(), -1);

        auto unchanged = co_await redis.llen(key);
        EXPECT_TRUE(unchanged.ok());
        EXPECT_EQ(unchanged.result(), 4);

        completed = true;
    });
    run_coro_test_until(completed);
}

// LPOS — basic, rank, count, maxlen, and not-found (empty).
TEST_P(ListProtocolModesTest, POS) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("pos");

        auto seed = co_await redis.rpush(key, "item1", "item2", "item3", "item2", "item4", "item2", "item5");
        EXPECT_TRUE(seed.ok()) << seed.error();

        auto all = co_await redis.lpos(key, "item2");
        EXPECT_TRUE(all.ok());
        EXPECT_EQ(all.result(), (std::vector<long long>{1, 3, 5}));

        auto rank2 = co_await redis.lpos(key, "item2", 2);
        EXPECT_TRUE(rank2.ok());
        if (!(rank2.result().size() == 2u)) {
            ADD_FAILURE() << "precondition failed: rank2.result().size() == 2u";
            co_return;
        }
        EXPECT_EQ(rank2.result()[0], 3);

        auto count2 = co_await redis.lpos(key, "item2", std::nullopt, 2);
        EXPECT_TRUE(count2.ok());
        EXPECT_EQ(count2.result(), (std::vector<long long>{1, 3}));

        auto maxlen4 = co_await redis.lpos(key, "item2", std::nullopt, std::nullopt, 4);
        EXPECT_TRUE(maxlen4.ok());
        EXPECT_EQ(maxlen4.result(), (std::vector<long long>{1, 3}));

        auto none = co_await redis.lpos(key, "nonexistent");
        EXPECT_TRUE(none.ok());
        EXPECT_TRUE(none.result().empty());

        completed = true;
    });
    run_coro_test_until(completed);
}

// BLPOP / BRPOP on already-populated lists (immediate return) + empty timeout.
TEST_P(ListProtocolModesTest, BLOCKING_IMMEDIATE) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key1 = protocol_key("blocking1");
        const std::string key2 = protocol_key("blocking2");

        auto seed = co_await redis.rpush(key1, "item1");
        EXPECT_TRUE(seed.ok()) << seed.error();

        auto blpop = co_await redis.blpop({key1, key2}, 1);
        if (!(blpop.ok() && blpop.result().has_value())) {
            ADD_FAILURE() << "precondition failed: blpop.ok() && blpop.result().has_value()";
            co_return;
        }
        EXPECT_EQ(blpop.result()->first, key1);
        EXPECT_EQ(blpop.result()->second, "item1");

        // Both empty now → blocks for the timeout, then nil.
        auto empty = co_await redis.blpop({key1, key2}, 1);
        EXPECT_TRUE(empty.ok());
        EXPECT_FALSE(empty.result().has_value());

        auto seed2 = co_await redis.rpush(key2, "item2");
        EXPECT_TRUE(seed2.ok());
        auto brpop = co_await redis.brpop({key1, key2}, 1);
        if (!(brpop.ok() && brpop.result().has_value())) {
            ADD_FAILURE() << "precondition failed: brpop.ok() && brpop.result().has_value()";
            co_return;
        }
        EXPECT_EQ(brpop.result()->first, key2);
        EXPECT_EQ(brpop.result()->second, "item2");

        completed = true;
    });
    run_coro_test_until(completed);
}

// TRUE blocking: BLPOP parks on a not-yet-existing key, then a second client RPUSHes and the
// blocked pop wakes with that value. Proves the await actually blocks server-side rather than
// returning nil immediately.
TEST_P(ListProtocolModesTest, BLPOP_BLOCKS_THEN_WOKEN_BY_SECOND_CLIENT) {
    qb::redis::tcp::client pusher{redis_test_uri()};
    ASSERT_TRUE(redis_try_connect(pusher)) << kDaemonUnreachableSentinel;

    const std::string key = protocol_key("blpop_wake");

    bool        popped  = false;
    bool        wake_ok = false;
    std::string value;

    // Consumer: park on BLPOP before the key has any element.
    // NB: pass the lambda to spawn() WITHOUT the trailing () — the lambda-safe overload
    // moves the closure into the spawned coroutine frame so its by-value capture `key`
    // stays alive. Writing `spawn(lambda())` would destroy the temporary closure at the
    // end of the call expression, leaving the frame reading freed `key` bytes (ASan-blind
    // stack corruption — observed as garbage compares before this fix).
    qb::io::async::coro_scheduler().spawn([this, key, &popped, &wake_ok, &value]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(popped);
        (void) co_await redis.del(key);
        auto r  = co_await redis.blpop({key}, 5);
        wake_ok = r.ok();
        if (r.ok() && r.result().has_value()) {
            EXPECT_EQ(r.result()->first, key);
            value = r.result()->second;
        }
        popped = true;
    });

    // Producer: after a short delay (so the consumer is definitely parked), RPUSH from the 2nd
    // client. The delay is a producer-side stagger, not a delivery-assert sleep.
    qb::io::async::coro_scheduler().spawn([&pusher, key]() -> qb::io::async::task<void> {
        co_await qb::io::async::sleep(std::chrono::milliseconds(150));
        (void) co_await pusher.rpush(key, "woke-up");
        co_return;
    });

    run_coro_test_until(popped, std::chrono::seconds(10));
    EXPECT_TRUE(wake_ok);
    EXPECT_EQ(value, "woke-up") << "BLPOP did not wake with the pushed value (likely returned nil immediately)";

    (void) qb::io::async::run_sync(pusher.del(key));
}

// LMOVE — right→left then left→right between two keys.
TEST_P(ListProtocolModesTest, MOVE) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string source = protocol_key("source");
        const std::string dest   = protocol_key("dest");

        auto seed = co_await redis.rpush(source, "item1", "item2", "item3");
        EXPECT_TRUE(seed.ok()) << seed.error();

        auto m1 = co_await redis.lmove(source, dest, ListPosition::RIGHT, ListPosition::LEFT);
        if (!(m1.ok() && m1.result().has_value())) {
            ADD_FAILURE() << "precondition failed: m1.ok() && m1.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*m1.result(), "item3");

        auto src_after = co_await redis.lrange(source, 0, -1);
        auto dst_after = co_await redis.lrange(dest, 0, -1);
        EXPECT_TRUE(src_after.ok() && dst_after.ok());
        EXPECT_EQ(src_after.result(), (std::vector<std::string>{"item1", "item2"}));
        EXPECT_EQ(dst_after.result(), (std::vector<std::string>{"item3"}));

        auto m2 = co_await redis.lmove(source, dest, ListPosition::LEFT, ListPosition::RIGHT);
        if (!(m2.ok() && m2.result().has_value())) {
            ADD_FAILURE() << "precondition failed: m2.ok() && m2.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*m2.result(), "item1");

        auto dst_final = co_await redis.lrange(dest, 0, -1);
        EXPECT_TRUE(dst_final.ok());
        EXPECT_EQ(dst_final.result(), (std::vector<std::string>{"item3", "item1"}));

        completed = true;
    });
    run_coro_test_until(completed);
}

// LPUSHX / RPUSHX — only push to an existing list.
TEST_P(ListProtocolModesTest, PUSHX) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string existing = protocol_key("pushx-existing");
        const std::string fresh    = protocol_key("pushx-new");

        auto seed = co_await redis.rpush(existing, "item1");
        EXPECT_TRUE(seed.ok()) << seed.error();

        auto lx = co_await redis.lpushx(existing, "item0");
        EXPECT_TRUE(lx.ok());
        EXPECT_EQ(lx.result(), 2);

        auto lx_miss = co_await redis.lpushx(fresh, "item1");
        EXPECT_TRUE(lx_miss.ok());
        EXPECT_EQ(lx_miss.result(), 0);

        auto rx = co_await redis.rpushx(existing, "item2");
        EXPECT_TRUE(rx.ok());
        EXPECT_EQ(rx.result(), 3);

        auto rx_miss = co_await redis.rpushx(fresh, "item1");
        EXPECT_TRUE(rx_miss.ok());
        EXPECT_EQ(rx_miss.result(), 0);

        completed = true;
    });
    run_coro_test_until(completed);
}

// RPOPLPUSH — pop tail of source, push to head of dest.
TEST_P(ListProtocolModesTest, RPOPLPUSH) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string source = protocol_key("rpoplpush-source");
        const std::string dest   = protocol_key("rpoplpush-dest");

        auto seed = co_await redis.rpush(source, "item1", "item2", "item3");
        EXPECT_TRUE(seed.ok()) << seed.error();

        auto moved = co_await redis.rpoplpush(source, dest);
        if (!(moved.ok() && moved.result().has_value())) {
            ADD_FAILURE() << "precondition failed: moved.ok() && moved.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*moved.result(), "item3");

        auto src_after = co_await redis.lrange(source, 0, -1);
        EXPECT_TRUE(src_after.ok());
        EXPECT_EQ(src_after.result(), (std::vector<std::string>{"item1", "item2"}));

        auto dst_after = co_await redis.lrange(dest, 0, -1);
        EXPECT_TRUE(dst_after.ok());
        EXPECT_EQ(dst_after.result(), (std::vector<std::string>{"item3"}));

        completed = true;
    });
    run_coro_test_until(completed);
}

// BLMPOP — blocking multi-key pop with data already present (returns immediately).
TEST_P(ListProtocolModesTest, BLMPOP) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key1 = protocol_key("blmpop1");
        const std::string key2 = protocol_key("blmpop2");
        (void) co_await redis.del(key1, key2);
        auto seed = co_await redis.rpush(key2, "x", "y", "z");
        EXPECT_TRUE(seed.ok()) << seed.error();

        auto r = co_await redis.blmpop({key1, key2}, ListPosition::RIGHT, 1, 2);
        if (!(r.ok() && r.result().has_value())) {
            ADD_FAILURE() << "precondition failed: r.ok() && r.result().has_value()";
            co_return;
        }
        EXPECT_EQ(r.result()->first, key2);
        if (!(r.result()->second.size() == 2u)) {
            ADD_FAILURE() << "precondition failed: r.result()->second.size() == 2u";
            co_return;
        }
        EXPECT_EQ(r.result()->second[0], "z");
        EXPECT_EQ(r.result()->second[1], "y");

        completed = true;
    });
    run_coro_test_until(completed);
}

// LMPOP — non-blocking multi-key pop.
TEST_P(ListProtocolModesTest, LMPOP) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key1 = protocol_key("lmpop1");
        const std::string key2 = protocol_key("lmpop2");
        (void) co_await redis.del(key1, key2);
        auto seed = co_await redis.rpush(key2, "a", "b", "c");
        EXPECT_TRUE(seed.ok()) << seed.error();

        auto r = co_await redis.lmpop({key1, key2}, ListPosition::LEFT, 2);
        if (!(r.ok() && r.result().has_value())) {
            ADD_FAILURE() << "precondition failed: r.ok() && r.result().has_value()";
            co_return;
        }
        EXPECT_EQ(r.result()->first, key2);
        if (!(r.result()->second.size() == 2u)) {
            ADD_FAILURE() << "precondition failed: r.result()->second.size() == 2u";
            co_return;
        }
        EXPECT_EQ(r.result()->second[0], "a");
        EXPECT_EQ(r.result()->second[1], "b");

        completed = true;
    });
    run_coro_test_until(completed);
}

// BLMOVE — blocking LMOVE with data present (returns immediately).
TEST_P(ListProtocolModesTest, BLMOVE) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string src = protocol_key("blmove_src");
        const std::string dst = protocol_key("blmove_dst");
        (void) co_await redis.del(src, dst);
        auto seed = co_await redis.rpush(src, "x");
        EXPECT_TRUE(seed.ok()) << seed.error();

        auto r = co_await redis.blmove(src, dst, ListPosition::RIGHT, ListPosition::LEFT, 1);
        if (!(r.ok() && r.result().has_value())) {
            ADD_FAILURE() << "precondition failed: r.ok() && r.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*r.result(), "x");

        auto dst_after = co_await redis.lrange(dst, 0, -1);
        EXPECT_TRUE(dst_after.ok());
        EXPECT_EQ(dst_after.result(), (std::vector<std::string>{"x"}));

        completed = true;
    });
    run_coro_test_until(completed);
}

// TRUE blocking: BRPOPLPUSH parks on an empty source, woken by a push from a second client; the
// popped value lands at the head of dest.
TEST_P(ListProtocolModesTest, BRPOPLPUSH_BLOCKS_THEN_WOKEN_BY_SECOND_CLIENT) {
    qb::redis::tcp::client pusher{redis_test_uri()};
    ASSERT_TRUE(redis_try_connect(pusher)) << kDaemonUnreachableSentinel;

    const std::string src = protocol_key("brpoplpush_wake_src");
    const std::string dst = protocol_key("brpoplpush_wake_dst");

    bool        done    = false;
    bool        wake_ok = false;
    std::string value;

    // Pass lambdas to spawn() WITHOUT the trailing () (lambda-safe overload): the by-value
    // captures `src`/`dst` live inside the closure, which must be owned by the coroutine
    // frame. `spawn(lambda())` would free the closure immediately, dangling those strings.
    qb::io::async::coro_scheduler().spawn([this, src, dst, &done, &wake_ok, &value]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(done);
        (void) co_await redis.del(src, dst);
        auto r  = co_await redis.brpoplpush(src, dst, 5);
        wake_ok = r.ok();
        if (r.ok() && r.result().has_value())
            value = *r.result();
        done = true;
    });

    qb::io::async::coro_scheduler().spawn([&pusher, src]() -> qb::io::async::task<void> {
        co_await qb::io::async::sleep(std::chrono::milliseconds(150));
        (void) co_await pusher.rpush(src, "transit");
        co_return;
    });

    run_coro_test_until(done, std::chrono::seconds(10));
    EXPECT_TRUE(wake_ok);
    EXPECT_EQ(value, "transit") << "BRPOPLPUSH did not block-then-wake with the pushed value";

    // The transited element must now be the head of dest.
    auto dst_after = qb::io::async::run_sync(redis.lrange(dst, 0, -1));
    ASSERT_TRUE(dst_after.ok());
    EXPECT_EQ(dst_after.result(), (std::vector<std::string>{"transit"}));

    (void) qb::io::async::run_sync(pusher.del(src, dst));
}

} // namespace
