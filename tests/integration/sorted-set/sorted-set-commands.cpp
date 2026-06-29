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
 * @file integration/sorted-set/sorted-set-commands.cpp
 * @brief Live RESP2/RESP3 integration tests for the qbm-redis sorted-set (ZSET) command mixin.
 *
 * Restructured from the legacy `test-sorted-set-commands.cpp`:
 *   - the 5 plain smoke tails (ZADD_ZRANGE, ZSCORE_ZINCRBY_DOUBLE, ZRANK_ZCARD,
 *     ZDIFF_ZMSCORE, ZREM_INTEGER) are deleted — strict subsets of the CORO_* bodies;
 *   - every un-watchdogged `while (!done) run(EVRUN_NOWAIT)` busy-spin (this was the most
 *     hang-prone file) is replaced by the shared `run_coro_test_until` watchdog;
 *   - `zrandmemberWithScores` now asserts each returned score matches the seeded value;
 *   - new cases: ZADD NX/XX/CH semantics, ZRANGEBYSCORE LIMIT offset/count, a true
 *     blocking-timeout BZMPOP on an empty key, and explicit WITHSCORES assertions on
 *     zrange/zrevrange (the wrapper always requests WITHSCORES → every entry has a score).
 */

#include <gtest/gtest.h>
#include <map>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include "../../shared/redis_integration_fixture.h"
#include "../redis.h"

using namespace qb::io;
using namespace qb::redis::test;

class SortedSetProtocolModesTest : public ProtocolModesTestBase {};

INSTANTIATE_PROTOCOL_MODES(SortedSetProtocolModesTest);

// ZADD / ZCARD / ZSCORE — membership, cardinality, score readback.
TEST_P(SortedSetProtocolModesTest, BASIC) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("basic");

        std::vector<qb::redis::score_member> members   = {{10.0, "member1"}, {20.0, "member2"}, {30.0, "member3"}};
        auto                                 add_reply = co_await redis.zadd(key, members);
        EXPECT_TRUE(add_reply.ok()) << add_reply.error();
        EXPECT_EQ(add_reply.result(), 3);

        auto card_reply = co_await redis.zcard(key);
        EXPECT_TRUE(card_reply.ok()) << card_reply.error();
        EXPECT_EQ(card_reply.result(), 3);

        auto score_reply = co_await redis.zscore(key, "member2");
        EXPECT_TRUE(score_reply.ok()) << score_reply.error();
        if (!(score_reply.result().has_value())) {
            ADD_FAILURE() << "precondition failed: score_reply.result().has_value()";
            co_return;
        }
        EXPECT_DOUBLE_EQ(*score_reply.result(), 20.0);

        auto score_none = co_await redis.zscore(key, "nonexistent");
        EXPECT_TRUE(score_none.ok()) << score_none.error();
        EXPECT_FALSE(score_none.result().has_value());

        CO_IGNORE(co_await redis.del(key));
        completed = true;
    });
    run_coro_test_until(completed);
}

// ZADD NX (NOT_EXIST) / XX (EXIST) / CH (count changed) modes.
TEST_P(SortedSetProtocolModesTest, ADD_NX_XX_CH) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("nx-xx-ch");

        // Seed a:1
        std::vector<qb::redis::score_member> seed = {{1.0, "a"}};
        EXPECT_TRUE((co_await redis.zadd(key, seed)).ok());

        // NX: only ADD new members. a already exists → not updated; b is new → added(1).
        std::vector<qb::redis::score_member> nx_members = {{99.0, "a"}, {2.0, "b"}};
        auto                                 nx         = co_await redis.zadd(key, nx_members, qb::redis::UpdateType::NOT_EXIST);
        EXPECT_TRUE(nx.ok()) << nx.error();
        EXPECT_EQ(nx.result(), 1); // only b added
        auto a_after_nx = co_await redis.zscore(key, "a");
        if (!(a_after_nx.result().has_value())) {
            ADD_FAILURE() << "precondition failed: a_after_nx.result().has_value()";
            co_return;
        }
        EXPECT_DOUBLE_EQ(*a_after_nx.result(), 1.0); // unchanged by NX

        // XX: only UPDATE existing members. a exists → updated; c is new → ignored.
        // Default reply counts only *added* members, so XX-update reports 0 added.
        std::vector<qb::redis::score_member> xx_members = {{5.0, "a"}, {3.0, "c"}};
        auto                                 xx         = co_await redis.zadd(key, xx_members, qb::redis::UpdateType::EXIST);
        EXPECT_TRUE(xx.ok()) << xx.error();
        EXPECT_EQ(xx.result(), 0); // nothing *added* (a updated, c skipped)
        auto a_after_xx = co_await redis.zscore(key, "a");
        if (!(a_after_xx.result().has_value())) {
            ADD_FAILURE() << "precondition failed: a_after_xx.result().has_value()";
            co_return;
        }
        EXPECT_DOUBLE_EQ(*a_after_xx.result(), 5.0);                          // updated by XX
        EXPECT_FALSE((co_await redis.zscore(key, "c")).result().has_value()); // c never created

        // CH (changed=true): report members whose score changed OR were added.
        // a: 5→7 (changed), b: 2→2 (unchanged), d: new (added) → CH counts a + d = 2.
        std::vector<qb::redis::score_member> ch_members = {{7.0, "a"}, {2.0, "b"}, {4.0, "d"}};
        auto                                 ch         = co_await redis.zadd(key, ch_members, qb::redis::UpdateType::ALWAYS, true);
        EXPECT_TRUE(ch.ok()) << ch.error();
        EXPECT_EQ(ch.result(), 2);

        CO_IGNORE(co_await redis.del(key));
        completed = true;
    });
    run_coro_test_until(completed);
}

// ZRANK / ZREVRANK — exact ordinals both directions.
TEST_P(SortedSetProtocolModesTest, RANK) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("rank");

        std::vector<qb::redis::score_member> members = {{10.0, "a"}, {20.0, "b"}, {30.0, "c"}, {40.0, "d"}};
        EXPECT_TRUE((co_await redis.zadd(key, members)).ok());

        auto rank_b = co_await redis.zrank(key, "b");
        EXPECT_TRUE(rank_b.ok());
        if (!(rank_b.result().has_value())) {
            ADD_FAILURE() << "precondition failed: rank_b.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*rank_b.result(), 1);

        auto rank_d = co_await redis.zrank(key, "d");
        if (!(rank_d.result().has_value())) {
            ADD_FAILURE() << "precondition failed: rank_d.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*rank_d.result(), 3);

        auto revrank_b = co_await redis.zrevrank(key, "b");
        if (!(revrank_b.result().has_value())) {
            ADD_FAILURE() << "precondition failed: revrank_b.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*revrank_b.result(), 2);

        auto revrank_d = co_await redis.zrevrank(key, "d");
        if (!(revrank_d.result().has_value())) {
            ADD_FAILURE() << "precondition failed: revrank_d.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*revrank_d.result(), 0);

        auto rank_none = co_await redis.zrank(key, "nonexistent");
        EXPECT_TRUE(rank_none.ok());
        EXPECT_FALSE(rank_none.result().has_value());

        CO_IGNORE(co_await redis.del(key));
        completed = true;
    });
    run_coro_test_until(completed);
}

// ZRANGE / ZREVRANGE — index ranges WITH scores (the wrapper always requests WITHSCORES).
TEST_P(SortedSetProtocolModesTest, RANGE_WITHSCORES) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("range");

        std::vector<qb::redis::score_member> members = {{10.0, "a"}, {20.0, "b"}, {30.0, "c"}, {40.0, "d"}, {50.0, "e"}};
        EXPECT_TRUE((co_await redis.zadd(key, members)).ok());

        auto range_reply = co_await redis.zrange(key, 1, 3);
        EXPECT_TRUE(range_reply.ok()) << range_reply.error();
        if (!(range_reply.result().size() == 3u)) {
            ADD_FAILURE() << "precondition failed: range_reply.result().size() == 3u";
            co_return;
        }
        EXPECT_EQ(range_reply.result()[0].member, "b");
        EXPECT_DOUBLE_EQ(range_reply.result()[0].score, 20.0);
        EXPECT_EQ(range_reply.result()[1].member, "c");
        EXPECT_DOUBLE_EQ(range_reply.result()[1].score, 30.0);
        EXPECT_EQ(range_reply.result()[2].member, "d");
        EXPECT_DOUBLE_EQ(range_reply.result()[2].score, 40.0);

        auto revrange_reply = co_await redis.zrevrange(key, 0, 2);
        EXPECT_TRUE(revrange_reply.ok()) << revrange_reply.error();
        if (!(revrange_reply.result().size() == 3u)) {
            ADD_FAILURE() << "precondition failed: revrange_reply.result().size() == 3u";
            co_return;
        }
        EXPECT_EQ(revrange_reply.result()[0].member, "e");
        EXPECT_DOUBLE_EQ(revrange_reply.result()[0].score, 50.0);
        EXPECT_EQ(revrange_reply.result()[1].member, "d");
        EXPECT_DOUBLE_EQ(revrange_reply.result()[1].score, 40.0);
        EXPECT_EQ(revrange_reply.result()[2].member, "c");
        EXPECT_DOUBLE_EQ(revrange_reply.result()[2].score, 30.0);

        CO_IGNORE(co_await redis.del(key));
        completed = true;
    });
    run_coro_test_until(completed);
}

// ZLEXCOUNT over an equal-score set.
TEST_P(SortedSetProtocolModesTest, LEXCOUNT) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("count");

        std::vector<qb::redis::score_member> members = {{0.0, "a"}, {0.0, "b"}, {0.0, "c"}, {0.0, "d"}, {0.0, "e"}};
        EXPECT_TRUE((co_await redis.zadd(key, members)).ok());

        qb::redis::lex_interval interval("b", "d", qb::redis::BoundType::CLOSED);
        auto                    lexcount_reply = co_await redis.zlexcount(key, interval);
        EXPECT_TRUE(lexcount_reply.ok()) << lexcount_reply.error();
        EXPECT_EQ(lexcount_reply.result(), 3); // b, c, d

        CO_IGNORE(co_await redis.del(key));
        completed = true;
    });
    run_coro_test_until(completed);
}

// ZPOPMAX / ZPOPMIN — pops in score order, cardinality shrinks.
TEST_P(SortedSetProtocolModesTest, POP) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("pop");

        std::vector<qb::redis::score_member> members = {{10.0, "a"}, {20.0, "b"}, {30.0, "c"}, {40.0, "d"}, {50.0, "e"}};
        EXPECT_TRUE((co_await redis.zadd(key, members)).ok());

        auto popmax_reply = co_await redis.zpopmax(key, 2);
        EXPECT_TRUE(popmax_reply.ok()) << popmax_reply.error();
        if (!(popmax_reply.result().size() == 2u)) {
            ADD_FAILURE() << "precondition failed: popmax_reply.result().size() == 2u";
            co_return;
        }
        EXPECT_EQ(popmax_reply.result()[0].member, "e");
        EXPECT_DOUBLE_EQ(popmax_reply.result()[0].score, 50.0);
        EXPECT_EQ(popmax_reply.result()[1].member, "d");

        EXPECT_EQ((co_await redis.zcard(key)).result(), 3);

        auto popmin_reply = co_await redis.zpopmin(key, 1);
        EXPECT_TRUE(popmin_reply.ok()) << popmin_reply.error();
        if (!(popmin_reply.result().size() == 1u)) {
            ADD_FAILURE() << "precondition failed: popmin_reply.result().size() == 1u";
            co_return;
        }
        EXPECT_EQ(popmin_reply.result()[0].member, "a");
        EXPECT_DOUBLE_EQ(popmin_reply.result()[0].score, 10.0);

        EXPECT_EQ((co_await redis.zcard(key)).result(), 2);

        CO_IGNORE(co_await redis.del(key));
        completed = true;
    });
    run_coro_test_until(completed);
}

// ZREM / ZREMRANGEBYRANK.
TEST_P(SortedSetProtocolModesTest, REMOVE) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("remove");

        std::vector<qb::redis::score_member> members = {{10.0, "a"}, {20.0, "b"}, {30.0, "c"}, {40.0, "d"}, {50.0, "e"}};
        EXPECT_TRUE((co_await redis.zadd(key, members)).ok());

        auto rem_reply = co_await redis.zrem(key, std::vector<std::string>{"b", "d"});
        EXPECT_TRUE(rem_reply.ok()) << rem_reply.error();
        EXPECT_EQ(rem_reply.result(), 2);
        EXPECT_EQ((co_await redis.zcard(key)).result(), 3);

        auto remrank_reply = co_await redis.zremrangebyrank(key, 0, 1);
        EXPECT_TRUE(remrank_reply.ok()) << remrank_reply.error();
        EXPECT_EQ(remrank_reply.result(), 2);
        EXPECT_EQ((co_await redis.zcard(key)).result(), 1);

        CO_IGNORE(co_await redis.del(key));
        completed = true;
    });
    run_coro_test_until(completed);
}

// ZINCRBY — increments and decrements accumulate.
TEST_P(SortedSetProtocolModesTest, INCR) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("incr");

        std::vector<qb::redis::score_member> incr_members = {{10.0, "score"}};
        EXPECT_TRUE((co_await redis.zadd(key, incr_members)).ok());

        auto incr1 = co_await redis.zincrby(key, 5.0, "score");
        EXPECT_TRUE(incr1.ok()) << incr1.error();
        EXPECT_DOUBLE_EQ(incr1.result(), 15.0);

        auto incr2 = co_await redis.zincrby(key, -3.0, "score");
        EXPECT_TRUE(incr2.ok()) << incr2.error();
        EXPECT_DOUBLE_EQ(incr2.result(), 12.0);

        EXPECT_DOUBLE_EQ(*(co_await redis.zscore(key, "score")).result(), 12.0);

        CO_IGNORE(co_await redis.del(key));
        completed = true;
    });
    run_coro_test_until(completed);
}

// ZUNIONSTORE / ZINTERSTORE.
TEST_P(SortedSetProtocolModesTest, UNION_INTER) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string set1       = protocol_key("set1");
        std::string set2       = protocol_key("set2");
        std::string union_dest = protocol_key("union");
        std::string inter_dest = protocol_key("inter");

        // Named vectors: a braced-init-list passed inline through co_await trips a GCC-14
        // internal compiler error in gimplify, so hoist them.
        std::vector<qb::redis::score_member> set1_members = {{1.0, "a"}, {2.0, "b"}, {3.0, "c"}};
        std::vector<qb::redis::score_member> set2_members = {{2.0, "b"}, {3.0, "c"}, {4.0, "d"}};
        EXPECT_TRUE((co_await redis.zadd(set1, set1_members)).ok());
        EXPECT_TRUE((co_await redis.zadd(set2, set2_members)).ok());

        auto union_reply = co_await redis.zunionstore(union_dest, {set1, set2});
        EXPECT_TRUE(union_reply.ok()) << union_reply.error();
        EXPECT_EQ(union_reply.result(), 4); // a, b, c, d
        EXPECT_EQ((co_await redis.zcard(union_dest)).result(), 4);

        auto inter_reply = co_await redis.zinterstore(inter_dest, {set1, set2});
        EXPECT_TRUE(inter_reply.ok()) << inter_reply.error();
        EXPECT_EQ(inter_reply.result(), 2); // b, c
        EXPECT_EQ((co_await redis.zcard(inter_dest)).result(), 2);

        CO_IGNORE(co_await redis.del(set1, set2, union_dest, inter_dest));
        completed = true;
    });
    run_coro_test_until(completed);
}

// ZRANGEBYSCORE / ZREVRANGEBYSCORE — closed interval, with reverse order.
TEST_P(SortedSetProtocolModesTest, RANGE_BY_SCORE) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("range-by-score");

        std::vector<qb::redis::score_member> members = {{10.0, "a"}, {20.0, "b"}, {30.0, "c"}, {40.0, "d"}, {50.0, "e"}};
        EXPECT_TRUE((co_await redis.zadd(key, members)).ok());

        qb::redis::score_interval interval(20.0, 40.0, qb::redis::BoundType::CLOSED);
        auto                      range_reply = co_await redis.zrangebyscore(key, interval);
        EXPECT_TRUE(range_reply.ok()) << range_reply.error();
        if (!(range_reply.result().size() == 3u)) {
            ADD_FAILURE() << "precondition failed: range_reply.result().size() == 3u";
            co_return;
        }
        EXPECT_EQ(range_reply.result()[0].member, "b");
        EXPECT_EQ(range_reply.result()[2].member, "d");

        auto revrange_reply = co_await redis.zrevrangebyscore(key, interval);
        EXPECT_TRUE(revrange_reply.ok()) << revrange_reply.error();
        if (!(revrange_reply.result().size() == 3u)) {
            ADD_FAILURE() << "precondition failed: revrange_reply.result().size() == 3u";
            co_return;
        }
        EXPECT_EQ(revrange_reply.result()[0].member, "d");
        EXPECT_EQ(revrange_reply.result()[1].member, "c");
        EXPECT_EQ(revrange_reply.result()[2].member, "b");

        CO_IGNORE(co_await redis.del(key));
        completed = true;
    });
    run_coro_test_until(completed);
}

// ZRANGEBYSCORE with a LIMIT offset/count window.
TEST_P(SortedSetProtocolModesTest, RANGE_BY_SCORE_LIMIT) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("range-by-score-limit");

        std::vector<qb::redis::score_member> members = {{1.0, "a"}, {2.0, "b"}, {3.0, "c"}, {4.0, "d"}, {5.0, "e"}};
        EXPECT_TRUE((co_await redis.zadd(key, members)).ok());

        // Full score window [1,5] but LIMIT offset=1 count=2 → skip a, take b, c.
        qb::redis::score_interval interval(1.0, 5.0, qb::redis::BoundType::CLOSED);
        qb::redis::LimitOptions   limit{/*offset*/ 1, /*count*/ 2};
        auto                      r = co_await redis.zrangebyscore(key, interval, limit);
        EXPECT_TRUE(r.ok()) << r.error();
        if (!(r.result().size() == 2u)) {
            ADD_FAILURE() << "precondition failed: r.result().size() == 2u";
            co_return;
        }
        EXPECT_EQ(r.result()[0].member, "b");
        EXPECT_DOUBLE_EQ(r.result()[0].score, 2.0);
        EXPECT_EQ(r.result()[1].member, "c");
        EXPECT_DOUBLE_EQ(r.result()[1].score, 3.0);

        CO_IGNORE(co_await redis.del(key));
        completed = true;
    });
    run_coro_test_until(completed);
}

// ZSCAN — full set returned within a single cursor pass for a small set.
TEST_P(SortedSetProtocolModesTest, SCAN) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("scan");

        std::vector<qb::redis::score_member> members = {{1.0, "m1"}, {2.0, "m2"}, {3.0, "m3"}, {4.0, "m4"}, {5.0, "m5"}};
        EXPECT_TRUE((co_await redis.zadd(key, members)).ok());

        auto scan_reply = co_await redis.zscan(key, 0, "*", 10);
        EXPECT_TRUE(scan_reply.ok()) << scan_reply.error();
        EXPECT_EQ(scan_reply.result().items.size(), 5u);

        CO_IGNORE(co_await redis.del(key));
        completed = true;
    });
    run_coro_test_until(completed);
}

// ZRANGEBYLEX / ZREVRANGEBYLEX / ZREMRANGEBYLEX over an equal-score set.
TEST_P(SortedSetProtocolModesTest, LEX_RANGE) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("lexrange");

        std::vector<qb::redis::score_member> members = {{0, "a"}, {0, "b"}, {0, "c"}, {0, "d"}, {0, "e"}, {0, "f"}, {0, "g"}};
        EXPECT_TRUE((co_await redis.zadd(key, members)).ok());

        // (b, e] → c, d, e
        qb::redis::lex_interval lex_interval("b", "e", qb::redis::BoundType::LEFT_OPEN);
        auto                    range_reply = co_await redis.zrangebylex(key, lex_interval);
        EXPECT_TRUE(range_reply.ok()) << range_reply.error();
        if (!(range_reply.result().size() == 3u)) {
            ADD_FAILURE() << "precondition failed: range_reply.result().size() == 3u";
            co_return;
        }
        EXPECT_EQ(range_reply.result()[0], "c");
        EXPECT_EQ(range_reply.result()[1], "d");
        EXPECT_EQ(range_reply.result()[2], "e");

        auto rev_reply = co_await redis.zrevrangebylex(key, lex_interval);
        EXPECT_TRUE(rev_reply.ok()) << rev_reply.error();
        if (!(rev_reply.result().size() == 3u)) {
            ADD_FAILURE() << "precondition failed: rev_reply.result().size() == 3u";
            co_return;
        }
        EXPECT_EQ(rev_reply.result()[0], "e");
        EXPECT_EQ(rev_reply.result()[1], "d");
        EXPECT_EQ(rev_reply.result()[2], "c");

        qb::redis::lex_interval closed_interval("b", "e", qb::redis::BoundType::CLOSED);
        auto                    closed_reply = co_await redis.zrangebylex(key, closed_interval);
        EXPECT_TRUE(closed_reply.ok()) << closed_reply.error();
        EXPECT_EQ(closed_reply.result().size(), 4u); // b, c, d, e

        qb::redis::lex_interval remove_interval("c", "e", qb::redis::BoundType::CLOSED);
        auto                    rem_reply = co_await redis.zremrangebylex(key, remove_interval);
        EXPECT_TRUE(rem_reply.ok()) << rem_reply.error();
        EXPECT_EQ(rem_reply.result(), 3); // c, d, e removed

        EXPECT_EQ((co_await redis.zcard(key)).result(), 4); // a, b, f, g

        CO_IGNORE(co_await redis.del(key));
        completed = true;
    });
    run_coro_test_until(completed);
}

// ZDIFF / ZDIFFWITHSCORES / ZDIFFSTORE.
TEST_P(SortedSetProtocolModesTest, ZDIFF) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string set1 = protocol_key("zdiff1");
        std::string set2 = protocol_key("zdiff2");
        std::string dest = protocol_key("zdiff_dest");

        std::vector<qb::redis::score_member> s1 = {{1.0, "a"}, {2.0, "b"}, {3.0, "c"}};
        std::vector<qb::redis::score_member> s2 = {{2.0, "b"}, {3.0, "c"}, {4.0, "d"}};
        EXPECT_TRUE((co_await redis.zadd(set1, s1)).ok());
        EXPECT_TRUE((co_await redis.zadd(set2, s2)).ok());

        auto diff_r = co_await redis.zdiff({set1, set2});
        EXPECT_TRUE(diff_r.ok()) << diff_r.error();
        if (!(diff_r.result().size() == 1u)) {
            ADD_FAILURE() << "precondition failed: diff_r.result().size() == 1u";
            co_return;
        }
        EXPECT_EQ(diff_r.result()[0], "a");

        auto diff_scores = co_await redis.zdiffWithScores({set1, set2});
        EXPECT_TRUE(diff_scores.ok()) << diff_scores.error();
        if (!(diff_scores.result().size() == 1u)) {
            ADD_FAILURE() << "precondition failed: diff_scores.result().size() == 1u";
            co_return;
        }
        EXPECT_EQ(diff_scores.result()[0].member, "a");
        EXPECT_DOUBLE_EQ(diff_scores.result()[0].score, 1.0);

        auto store_r = co_await redis.zdiffstore(dest, {set1, set2});
        EXPECT_TRUE(store_r.ok()) << store_r.error();
        EXPECT_EQ(store_r.result(), 1);
        EXPECT_EQ((co_await redis.zcard(dest)).result(), 1);

        CO_IGNORE(co_await redis.del(set1, set2, dest));
        completed = true;
    });
    run_coro_test_until(completed);
}

// ZINTER / ZINTERWITHSCORES / ZINTERCARD.
TEST_P(SortedSetProtocolModesTest, ZINTER) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string set1 = protocol_key("zinter1");
        std::string set2 = protocol_key("zinter2");

        std::vector<qb::redis::score_member> s1 = {{1.0, "a"}, {2.0, "b"}, {3.0, "c"}};
        std::vector<qb::redis::score_member> s2 = {{2.0, "b"}, {3.0, "c"}, {4.0, "d"}};
        EXPECT_TRUE((co_await redis.zadd(set1, s1)).ok());
        EXPECT_TRUE((co_await redis.zadd(set2, s2)).ok());

        auto inter_r = co_await redis.zinter({set1, set2});
        EXPECT_TRUE(inter_r.ok()) << inter_r.error();
        if (!(inter_r.result().size() == 2u)) {
            ADD_FAILURE() << "precondition failed: inter_r.result().size() == 2u";
            co_return;
        }
        // ZINTER returns common members in ascending aggregated-score order: b, c.
        EXPECT_EQ(inter_r.result()[0], "b");
        EXPECT_EQ(inter_r.result()[1], "c");

        auto inter_scores = co_await redis.zinterWithScores({set1, set2});
        EXPECT_TRUE(inter_scores.ok()) << inter_scores.error();
        if (!(inter_scores.result().size() == 2u)) {
            ADD_FAILURE() << "precondition failed: inter_scores.result().size() == 2u";
            co_return;
        }
        EXPECT_EQ(inter_scores.result()[0].member, "b");
        EXPECT_DOUBLE_EQ(inter_scores.result()[0].score, 4.0); // 2 + 2 (SUM)
        EXPECT_EQ(inter_scores.result()[1].member, "c");
        EXPECT_DOUBLE_EQ(inter_scores.result()[1].score, 6.0); // 3 + 3

        auto card_r = co_await redis.zintercard({set1, set2});
        EXPECT_TRUE(card_r.ok()) << card_r.error();
        EXPECT_EQ(card_r.result(), 2);

        CO_IGNORE(co_await redis.del(set1, set2));
        completed = true;
    });
    run_coro_test_until(completed);
}

// ZMPOP / ZMSCORE / ZRANDMEMBER / ZRANGESTORE / non-blocking BZMPOP fast path.
TEST_P(SortedSetProtocolModesTest, ZMPOP_ZMSCORE_ZRANDMEMBER) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("zmpop");
        std::string src = protocol_key("zrangestore_src");
        std::string dst = protocol_key("zrangestore_dst");

        std::vector<qb::redis::score_member> zmpop_members = {{1.0, "a"}, {2.0, "b"}, {3.0, "c"}};
        EXPECT_TRUE((co_await redis.zadd(key, zmpop_members)).ok());

        auto pop_r = co_await redis.zmpop({key}, "MIN", 1);
        EXPECT_TRUE(pop_r.ok()) << pop_r.error();
        if (!(pop_r.result().has_value())) {
            ADD_FAILURE() << "precondition failed: pop_r.result().has_value()";
            co_return;
        }
        EXPECT_EQ(pop_r.result()->first, key);
        if (!(pop_r.result()->second.size() == 1u)) {
            ADD_FAILURE() << "precondition failed: pop_r.result()->second.size() == 1u";
            co_return;
        }
        EXPECT_EQ(pop_r.result()->second[0].member, "a");
        EXPECT_DOUBLE_EQ(pop_r.result()->second[0].score, 1.0);

        // Seed a known score map so ZRANDMEMBER WITHSCORES can be checked against it.
        std::vector<qb::redis::score_member> reseed = {{1.0, "a"}, {2.0, "b"}, {3.0, "c"}};
        EXPECT_TRUE((co_await redis.zadd(key, reseed)).ok());
        const std::map<std::string, double> seeded_scores = {{"a", 1.0}, {"b", 2.0}, {"c", 3.0}};

        auto mscore_r = co_await redis.zmscore(key, {"a", "b", "nonexistent"});
        EXPECT_TRUE(mscore_r.ok()) << mscore_r.error();
        if (!(mscore_r.result().size() == 3u)) {
            ADD_FAILURE() << "precondition failed: mscore_r.result().size() == 3u";
            co_return;
        }
        if (!(mscore_r.result()[0].has_value())) {
            ADD_FAILURE() << "precondition failed: mscore_r.result()[0].has_value()";
            co_return;
        }
        EXPECT_DOUBLE_EQ(*mscore_r.result()[0], 1.0);
        if (!(mscore_r.result()[1].has_value())) {
            ADD_FAILURE() << "precondition failed: mscore_r.result()[1].has_value()";
            co_return;
        }
        EXPECT_DOUBLE_EQ(*mscore_r.result()[1], 2.0);
        EXPECT_FALSE(mscore_r.result()[2].has_value());

        auto rand_r = co_await redis.zrandmember(key);
        EXPECT_TRUE(rand_r.ok()) << rand_r.error();
        if (!(rand_r.result().has_value())) {
            ADD_FAILURE() << "precondition failed: rand_r.result().has_value()";
            co_return;
        }
        EXPECT_TRUE(seeded_scores.count(*rand_r.result()) == 1);

        auto rand_count = co_await redis.zrandmemberCount(key, 2);
        EXPECT_TRUE(rand_count.ok()) << rand_count.error();
        EXPECT_EQ(rand_count.result().size(), 2u);

        // ZRANDMEMBER WITHSCORES: each returned score must match the seeded value.
        auto rand_scores = co_await redis.zrandmemberWithScores(key, 2);
        EXPECT_TRUE(rand_scores.ok()) << rand_scores.error();
        if (!(rand_scores.result().size() == 2u)) {
            ADD_FAILURE() << "precondition failed: rand_scores.result().size() == 2u";
            co_return;
        }
        for (const auto &sm : rand_scores.result()) {
            EXPECT_TRUE(seeded_scores.count(sm.member) == 1) << "unexpected member " << sm.member;
            EXPECT_DOUBLE_EQ(sm.score, seeded_scores.at(sm.member));
        }

        std::vector<qb::redis::score_member> rangestore_src = {{1.0, "a"}, {2.0, "b"}, {3.0, "c"}, {4.0, "d"}};
        EXPECT_TRUE((co_await redis.zadd(src, rangestore_src)).ok());
        auto rangestore_r = co_await redis.zrangestore(dst, src, "1", "3", {"BYSCORE"});
        EXPECT_TRUE(rangestore_r.ok()) << rangestore_r.error();
        EXPECT_EQ(rangestore_r.result(), 3);
        EXPECT_EQ((co_await redis.zcard(dst)).result(), 3);

        // BZMPOP on a NON-empty key returns immediately (fast path).
        std::vector<qb::redis::score_member> bzmpop_members = {{1.0, "x"}};
        EXPECT_TRUE((co_await redis.zadd(key, bzmpop_members)).ok());
        auto bzmpop_r = co_await redis.bzmpop({key}, 1, "MIN", 1);
        EXPECT_TRUE(bzmpop_r.ok()) << bzmpop_r.error();
        if (!(bzmpop_r.result().has_value())) {
            ADD_FAILURE() << "precondition failed: bzmpop_r.result().has_value()";
            co_return;
        }
        EXPECT_EQ(bzmpop_r.result()->second.size(), 1u);

        CO_IGNORE(co_await redis.del(key, src, dst));
        completed = true;
    });
    run_coro_test_until(completed);
}

// BZMPOP true blocking timeout: on a never-populated key it blocks for the timeout then
// returns an empty (nullopt) result without hanging the loop.
TEST_P(SortedSetProtocolModesTest, BZMPOP_BLOCKING_TIMEOUT) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("bzmpop-empty");
        CO_IGNORE(co_await redis.del(key)); // ensure absent

        const auto start = std::chrono::steady_clock::now();
        // timeout=1s on an empty key → server blocks the full second, then nil.
        auto       r       = co_await redis.bzmpop({key}, 1, "MIN", 1);
        const auto elapsed = std::chrono::steady_clock::now() - start;
        EXPECT_TRUE(r.ok()) << r.error();
        EXPECT_FALSE(r.result().has_value());
        // It genuinely blocked (not an instant nil) — at least ~900ms elapsed.
        EXPECT_GE(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 900);

        completed = true;
    });
    run_coro_test_until(completed, std::chrono::seconds(10));
}

// ZCOUNT (closed + open bound) and ZREMRANGEBYSCORE.
TEST_P(SortedSetProtocolModesTest, COUNT_AND_REMRANGEBYSCORE) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("zcount");

        std::vector<qb::redis::score_member> members = {{1.0, "one"}, {2.0, "two"}, {3.0, "three"}, {4.0, "four"}, {5.0, "five"}};
        EXPECT_TRUE((co_await redis.zadd(key, members)).ok());

        qb::redis::score_interval interval(2.0, 4.0, qb::redis::BoundType::CLOSED);
        auto                      count_reply = co_await redis.zcount(key, interval);
        EXPECT_TRUE(count_reply.ok()) << count_reply.error();
        EXPECT_EQ(count_reply.result(), 3); // [2,4]

        qb::redis::score_interval open_interval(2.0, 4.0, qb::redis::BoundType::LEFT_OPEN);
        auto                      open_count = co_await redis.zcount(key, open_interval);
        EXPECT_TRUE(open_count.ok()) << open_count.error();
        EXPECT_EQ(open_count.result(), 2); // (2,4]

        qb::redis::score_interval remove_interval(1.0, 2.0, qb::redis::BoundType::CLOSED);
        auto                      rem_reply = co_await redis.zremrangebyscore(key, remove_interval);
        EXPECT_TRUE(rem_reply.ok()) << rem_reply.error();
        EXPECT_EQ(rem_reply.result(), 2); // one, two removed

        EXPECT_EQ((co_await redis.zcard(key)).result(), 3);

        CO_IGNORE(co_await redis.del(key));
        completed = true;
    });
    run_coro_test_until(completed);
}
