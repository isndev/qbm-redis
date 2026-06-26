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
 * @file integration/set/set-commands.cpp
 * @brief Live-redis integration tests for the set command family (SADD/SCARD/SMEMBERS,
 *        SISMEMBER/SMISMEMBER, SREM, SPOP/SRANDMEMBER, SMOVE, SDIFF/SINTER/SUNION + store
 *        variants, SINTERCARD, SSCAN), exercised in both RESP2 and RESP3.
 *
 * Restructured from the legacy `test-set-commands.cpp`:
 *  - deleted 5 trailing smoke dups (SADD_SMEMBERS / SISMEMBER_BOOLEAN / SINTER /
 *    SREM_INTEGER / SCARD_INTEGER) — strict subsets of the CORO_* bodies kept here;
 *  - REMOVED the file-local `main()` that used to be the whole suite's entry point (links the
 *    shared gtest-main now);
 *  - SPOP / SRANDMEMBER strengthened to assert returned members belong to the seeded set;
 *  - SSCAN made cursor-aware with an added large-set multi-batch walk;
 *  - ADDED SPOP/SRANDMEMBER count>cardinality edges.
 */

#include <gtest/gtest.h>
#include <set>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include "../redis.h"
#include "../../shared/redis_integration_fixture.h"

using namespace qb::redis::test;

namespace {

class SetProtocolModesTest : public ProtocolModesTestBase {};

INSTANTIATE_PROTOCOL_MODES(SetProtocolModesTest);

// SADD / SCARD / SMEMBERS — adds, dedup, and full membership.
TEST_P(SetProtocolModesTest, BASIC) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("basic");

        auto a1 = co_await redis.sadd(key, "member1");
        EXPECT_TRUE(a1.ok()) << a1.error();
        EXPECT_EQ(a1.result(), 1);
        auto a2 = co_await redis.sadd(key, "member2", "member3");
        EXPECT_TRUE(a2.ok());
        EXPECT_EQ(a2.result(), 2);
        auto dup = co_await redis.sadd(key, "member1");
        EXPECT_TRUE(dup.ok());
        EXPECT_EQ(dup.result(), 0); // already present

        auto card = co_await redis.scard(key);
        EXPECT_TRUE(card.ok());
        EXPECT_EQ(card.result(), 3);

        auto members = co_await redis.smembers(key);
        EXPECT_TRUE(members.ok());
        std::set<std::string> got(members.result().begin(), members.result().end());
        EXPECT_EQ(got, (std::set<std::string>{"member1", "member2", "member3"}));

        completed = true;
    });
    run_coro_test_until(completed);
}

// SISMEMBER / SMISMEMBER.
TEST_P(SetProtocolModesTest, ISMEMBER) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("ismember");

        auto seed = co_await redis.sadd(key, "member1", "member2", "member3");
        EXPECT_TRUE(seed.ok()) << seed.error();

        auto in = co_await redis.sismember(key, "member1");
        EXPECT_TRUE(in.ok());
        EXPECT_TRUE(in.result());
        auto out = co_await redis.sismember(key, "nonexistent");
        EXPECT_TRUE(out.ok());
        EXPECT_FALSE(out.result());

        auto multi = co_await redis.smismember(key, "member1", "member2", "nonexistent");
        EXPECT_TRUE(multi.ok());
        if (!(multi.result().size() == 3u)) { ADD_FAILURE() << "precondition failed: multi.result().size() == 3u"; co_return; }
        EXPECT_TRUE(multi.result()[0]);
        EXPECT_TRUE(multi.result()[1]);
        EXPECT_FALSE(multi.result()[2]);

        completed = true;
    });
    run_coro_test_until(completed);
}

// SREM — removes existing, no-op on absent.
TEST_P(SetProtocolModesTest, REMOVE) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("remove");

        auto seed = co_await redis.sadd(key, "member1", "member2", "member3", "member4");
        EXPECT_TRUE(seed.ok()) << seed.error();

        auto rem = co_await redis.srem(key, "member1", "member2");
        EXPECT_TRUE(rem.ok());
        EXPECT_EQ(rem.result(), 2);

        auto card = co_await redis.scard(key);
        EXPECT_TRUE(card.ok());
        EXPECT_EQ(card.result(), 2);

        auto rem_miss = co_await redis.srem(key, "nonexistent");
        EXPECT_TRUE(rem_miss.ok());
        EXPECT_EQ(rem_miss.result(), 0);

        completed = true;
    });
    run_coro_test_until(completed);
}

// SPOP — popped members are real set members; cardinality drops accordingly; count>cardinality
// drains the whole set.
TEST_P(SetProtocolModesTest, POP) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("pop");

        const std::set<std::string> seeded{"member1", "member2", "member3", "member4", "member5"};
        auto seed = co_await redis.sadd(key, "member1", "member2", "member3", "member4", "member5");
        EXPECT_TRUE(seed.ok()) << seed.error();

        auto one = co_await redis.spop(key);
        if (!(one.ok() && one.result().has_value())) { ADD_FAILURE() << "precondition failed: one.ok() && one.result().has_value()"; co_return; }
        EXPECT_EQ(seeded.count(*one.result()), 1u) << "SPOP returned a non-member: " << *one.result();

        auto card1 = co_await redis.scard(key);
        EXPECT_TRUE(card1.ok());
        EXPECT_EQ(card1.result(), 4);

        auto two = co_await redis.spop(key, 2);
        EXPECT_TRUE(two.ok());
        if (!(two.result().size() == 2u)) { ADD_FAILURE() << "precondition failed: two.result().size() == 2u"; co_return; }
        for (const auto &m : two.result())
            EXPECT_EQ(seeded.count(m), 1u) << "SPOP(count) returned a non-member: " << m;
        // No duplicates within a single SPOP batch.
        EXPECT_NE(two.result()[0], two.result()[1]);

        auto card2 = co_await redis.scard(key);
        EXPECT_TRUE(card2.ok());
        EXPECT_EQ(card2.result(), 2);

        // count larger than remaining cardinality drains the set (returns exactly what's left).
        auto rest = co_await redis.spop(key, 10);
        EXPECT_TRUE(rest.ok());
        EXPECT_EQ(rest.result().size(), 2u);
        auto empty_card = co_await redis.scard(key);
        EXPECT_TRUE(empty_card.ok());
        EXPECT_EQ(empty_card.result(), 0);

        completed = true;
    });
    run_coro_test_until(completed);
}

// SRANDMEMBER — non-destructive; returned members are real; count>cardinality (positive) caps
// at cardinality, negative count allows repeats up to |count|.
TEST_P(SetProtocolModesTest, RANDMEMBER) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("randmember");

        const std::set<std::string> seeded{"member1", "member2", "member3", "member4", "member5"};
        auto seed = co_await redis.sadd(key, "member1", "member2", "member3", "member4", "member5");
        EXPECT_TRUE(seed.ok()) << seed.error();

        auto one = co_await redis.srandmember(key);
        if (!(one.ok() && one.result().has_value())) { ADD_FAILURE() << "precondition failed: one.ok() && one.result().has_value()"; co_return; }
        EXPECT_EQ(seeded.count(*one.result()), 1u) << "SRANDMEMBER returned a non-member: " << *one.result();

        auto card = co_await redis.scard(key);
        EXPECT_TRUE(card.ok());
        EXPECT_EQ(card.result(), 5); // non-destructive

        auto three = co_await redis.srandmember(key, 3);
        EXPECT_TRUE(three.ok());
        if (!(three.result().size() == 3u)) { ADD_FAILURE() << "precondition failed: three.result().size() == 3u"; co_return; }
        std::set<std::string> distinct(three.result().begin(), three.result().end());
        EXPECT_EQ(distinct.size(), 3u) << "positive-count SRANDMEMBER must not repeat";
        for (const auto &m : three.result())
            EXPECT_EQ(seeded.count(m), 1u);

        // Positive count > cardinality → capped at the whole set, distinct.
        auto over = co_await redis.srandmember(key, 100);
        EXPECT_TRUE(over.ok());
        EXPECT_EQ(over.result().size(), 5u);

        // Negative count → exactly |count| entries, repeats permitted, all real members.
        auto neg = co_await redis.srandmember(key, -8);
        EXPECT_TRUE(neg.ok());
        EXPECT_EQ(neg.result().size(), 8u);
        for (const auto &m : neg.result())
            EXPECT_EQ(seeded.count(m), 1u);

        completed = true;
    });
    run_coro_test_until(completed);
}

// SMOVE — moves a member between sets; no-op on absent source member.
TEST_P(SetProtocolModesTest, MOVE) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string source = protocol_key("source");
        const std::string dest   = protocol_key("dest");

        auto s1 = co_await redis.sadd(source, "member1", "member2", "member3");
        EXPECT_TRUE(s1.ok()) << s1.error();
        auto s2 = co_await redis.sadd(dest, "member4");
        EXPECT_TRUE(s2.ok());

        auto moved = co_await redis.smove(source, dest, "member1");
        EXPECT_TRUE(moved.ok());
        EXPECT_TRUE(moved.result());

        auto src_card = co_await redis.scard(source);
        auto dst_card = co_await redis.scard(dest);
        EXPECT_TRUE(src_card.ok() && dst_card.ok());
        EXPECT_EQ(src_card.result(), 2);
        EXPECT_EQ(dst_card.result(), 2);
        auto dest_has = co_await redis.sismember(dest, "member1");
        EXPECT_TRUE(dest_has.ok());
        EXPECT_TRUE(dest_has.result());

        auto miss = co_await redis.smove(source, dest, "nonexistent");
        EXPECT_TRUE(miss.ok());
        EXPECT_FALSE(miss.result());

        completed = true;
    });
    run_coro_test_until(completed);
}

// SDIFF / SINTER / SUNION / SINTERCARD over two seeded sets.
TEST_P(SetProtocolModesTest, OPERATIONS) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string set1 = protocol_key("set1");
        const std::string set2 = protocol_key("set2");

        EXPECT_TRUE((co_await redis.sadd(set1, "a", "b", "c", "d")).ok());
        EXPECT_TRUE((co_await redis.sadd(set2, "b", "c", "e", "f")).ok());

        auto diff = co_await redis.sdiff({set1, set2});
        EXPECT_TRUE(diff.ok());
        EXPECT_EQ(std::set<std::string>(diff.result().begin(), diff.result().end()),
                  (std::set<std::string>{"a", "d"}));

        auto inter = co_await redis.sinter({set1, set2});
        EXPECT_TRUE(inter.ok());
        EXPECT_EQ(std::set<std::string>(inter.result().begin(), inter.result().end()),
                  (std::set<std::string>{"b", "c"}));

        auto uni = co_await redis.sunion({set1, set2});
        EXPECT_TRUE(uni.ok());
        EXPECT_EQ(std::set<std::string>(uni.result().begin(), uni.result().end()),
                  (std::set<std::string>{"a", "b", "c", "d", "e", "f"}));

        auto intercard = co_await redis.sintercard({set1, set2});
        EXPECT_TRUE(intercard.ok());
        EXPECT_EQ(intercard.result(), 2);

        completed = true;
    });
    run_coro_test_until(completed);
}

// SDIFFSTORE / SINTERSTORE / SUNIONSTORE — store cardinalities and resulting membership.
TEST_P(SetProtocolModesTest, STORE) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string set1  = protocol_key("set1");
        const std::string set2  = protocol_key("set2");
        const std::string dest1 = protocol_key("dest1");
        const std::string dest2 = protocol_key("dest2");
        const std::string dest3 = protocol_key("dest3");

        EXPECT_TRUE((co_await redis.sadd(set1, "a", "b", "c", "d")).ok());
        EXPECT_TRUE((co_await redis.sadd(set2, "b", "c", "e", "f")).ok());

        auto diff = co_await redis.sdiffstore(dest1, {set1, set2});
        EXPECT_TRUE(diff.ok());
        EXPECT_EQ(diff.result(), 2);
        auto diff_members = co_await redis.smembers(dest1);
        EXPECT_TRUE(diff_members.ok());
        EXPECT_EQ(std::set<std::string>(diff_members.result().begin(), diff_members.result().end()),
                  (std::set<std::string>{"a", "d"}));

        auto inter = co_await redis.sinterstore(dest2, {set1, set2});
        EXPECT_TRUE(inter.ok());
        EXPECT_EQ(inter.result(), 2);

        auto uni = co_await redis.sunionstore(dest3, {set1, set2});
        EXPECT_TRUE(uni.ok());
        EXPECT_EQ(uni.result(), 6);

        completed = true;
    });
    run_coro_test_until(completed);
}

// SINTERCARD with limit.
TEST_P(SetProtocolModesTest, SINTERCARD_WITH_LIMIT) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string set1 = protocol_key("sintercard_limit1");
        const std::string set2 = protocol_key("sintercard_limit2");

        EXPECT_TRUE((co_await redis.sadd(set1, "a", "b", "c", "d", "e")).ok());
        EXPECT_TRUE((co_await redis.sadd(set2, "a", "b", "c", "f", "g")).ok());

        auto full = co_await redis.sintercard({set1, set2});
        EXPECT_TRUE(full.ok());
        EXPECT_EQ(full.result(), 3);

        auto two = co_await redis.sintercard({set1, set2}, 2LL);
        EXPECT_TRUE(two.ok());
        EXPECT_EQ(two.result(), 2);

        auto one = co_await redis.sintercard({set1, set2}, 1LL);
        EXPECT_TRUE(one.ok());
        EXPECT_EQ(one.result(), 1);

        auto big = co_await redis.sintercard({set1, set2}, 100LL);
        EXPECT_TRUE(big.ok());
        EXPECT_EQ(big.result(), 3);

        completed = true;
    });
    run_coro_test_until(completed);
}

// SSCAN small set — single step, cursor 0, full membership.
TEST_P(SetProtocolModesTest, SCAN_SINGLE_STEP) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("scan");

        auto seed = co_await redis.sadd(key, "member1", "member2", "member3", "member4", "member5");
        EXPECT_TRUE(seed.ok()) << seed.error();

        auto scan = co_await redis.sscan(key, 0, "*", 100);
        EXPECT_TRUE(scan.ok()) << scan.error();
        EXPECT_EQ(scan.result().cursor, 0u);
        std::set<std::string> got(scan.result().items.begin(), scan.result().items.end());
        EXPECT_EQ(got, (std::set<std::string>{"member1", "member2", "member3", "member4", "member5"}));

        completed = true;
    });
    run_coro_test_until(completed);
}

// SSCAN large set — multiple cursor steps recover the full set without drops/dups.
TEST_P(SetProtocolModesTest, SCAN_LARGE_SET_MULTI_BATCH) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("scan_large");

        constexpr int kCount = 500;
        std::set<std::string> expected;
        for (int i = 0; i < kCount; ++i) {
            const std::string m = "m" + std::to_string(i);
            expected.insert(m);
            auto s = co_await redis.sadd(key, m);
            EXPECT_TRUE(s.ok()) << s.error();
        }

        std::set<std::string> collected;
        size_t cursor = 0;
        int    steps  = 0;
        do {
            auto step = co_await redis.sscan(key, static_cast<long long>(cursor), "*", 32);
            EXPECT_TRUE(step.ok()) << step.error();
            collected.insert(step.result().items.begin(), step.result().items.end());
            cursor = step.result().cursor;
            ++steps;
            EXPECT_LT(steps, 1000) << "SSCAN cursor failed to converge";
        } while (cursor != 0);

        EXPECT_GT(steps, 1) << "expected the large set to require multiple cursor steps";
        EXPECT_EQ(collected, expected);

        completed = true;
    });
    run_coro_test_until(completed, std::chrono::seconds(60));
}

} // namespace
