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

#include <gtest/gtest.h>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <thread>
#include "../redis.h"
#include "protocol_test_common.h"

using namespace qb::io;
using namespace std::chrono;

// ============================================================================
// Fixture: all tests run in both RESP2 and RESP3
// ============================================================================

class SortedSetProtocolModesTest : public ProtocolModesTestBase {};

INSTANTIATE_PROTOCOL_MODES(SortedSetProtocolModesTest);

// Test basic ZADD, ZCARD, ZSCORE operations
TEST_P(SortedSetProtocolModesTest, CORO_SORTED_SET_COMMANDS_BASIC) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("basic");

        // ZADD test
        std::vector<qb::redis::score_member> members   = {{10.0, "member1"}, {20.0, "member2"}, {30.0, "member3"}};
        auto                                 add_reply = co_await redis.zadd(key, members);
        EXPECT_TRUE(add_reply.ok());
        EXPECT_EQ(add_reply.result(), 3);

        // ZCARD test
        auto card_reply = co_await redis.zcard(key);
        EXPECT_TRUE(card_reply.ok());
        EXPECT_EQ(card_reply.result(), 3);

        // ZSCORE test
        auto score_reply = co_await redis.zscore(key, "member2");
        EXPECT_TRUE(score_reply.ok());
        EXPECT_TRUE(score_reply.result().has_value());
        EXPECT_DOUBLE_EQ(*score_reply.result(), 20.0);

        // Non-existent member
        auto score_none = co_await redis.zscore(key, "nonexistent");
        EXPECT_TRUE(score_none.ok());
        EXPECT_FALSE(score_none.result().has_value());

        // Cleanup
        (void) co_await redis.del(key);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test ZRANK and ZREVRANK
TEST_P(SortedSetProtocolModesTest, CORO_SORTED_SET_COMMANDS_RANK) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("rank");

        // Setup sorted set
        std::vector<qb::redis::score_member> members = {{10.0, "a"}, {20.0, "b"}, {30.0, "c"}, {40.0, "d"}};
        (void) co_await redis.zadd(key, members);

        // ZRANK test (low to high: a=0, b=1, c=2, d=3)
        auto rank_b = co_await redis.zrank(key, "b");
        EXPECT_TRUE(rank_b.ok());
        EXPECT_TRUE(rank_b.result().has_value());
        EXPECT_EQ(*rank_b.result(), 1);

        auto rank_d = co_await redis.zrank(key, "d");
        EXPECT_TRUE(rank_d.ok());
        EXPECT_EQ(*rank_d.result(), 3);

        // ZREVRANK test (high to low: d=0, c=1, b=2, a=3)
        auto revrank_b = co_await redis.zrevrank(key, "b");
        EXPECT_TRUE(revrank_b.ok());
        EXPECT_TRUE(revrank_b.result().has_value());
        EXPECT_EQ(*revrank_b.result(), 2);

        auto revrank_d = co_await redis.zrevrank(key, "d");
        EXPECT_TRUE(revrank_d.ok());
        EXPECT_EQ(*revrank_d.result(), 0);

        // Non-existent member
        auto rank_none = co_await redis.zrank(key, "nonexistent");
        EXPECT_TRUE(rank_none.ok());
        EXPECT_FALSE(rank_none.result().has_value());

        // Cleanup
        (void) co_await redis.del(key);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test ZRANGE and ZREVRANGE
TEST_P(SortedSetProtocolModesTest, CORO_SORTED_SET_COMMANDS_RANGE) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("range");

        // Setup sorted set
        std::vector<qb::redis::score_member> members = {{10.0, "a"}, {20.0, "b"}, {30.0, "c"}, {40.0, "d"}, {50.0, "e"}};
        (void) co_await redis.zadd(key, members);

        // ZRANGE test
        auto range_reply = co_await redis.zrange(key, 1, 3);
        EXPECT_TRUE(range_reply.ok());
        EXPECT_EQ(range_reply.result().size(), 3);
        EXPECT_EQ(range_reply.result()[0].member, "b");
        EXPECT_EQ(range_reply.result()[1].member, "c");
        EXPECT_EQ(range_reply.result()[2].member, "d");

        // ZREVRANGE test
        auto revrange_reply = co_await redis.zrevrange(key, 0, 2);
        EXPECT_TRUE(revrange_reply.ok());
        EXPECT_EQ(revrange_reply.result().size(), 3);
        EXPECT_EQ(revrange_reply.result()[0].member, "e");
        EXPECT_EQ(revrange_reply.result()[1].member, "d");
        EXPECT_EQ(revrange_reply.result()[2].member, "c");

        // Cleanup
        (void) co_await redis.del(key);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test ZCOUNT and ZLEXCOUNT
TEST_P(SortedSetProtocolModesTest, CORO_SORTED_SET_COMMANDS_COUNT) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("count");

        // Setup sorted set with same scores for lex counting
        std::vector<qb::redis::score_member> members = {{0.0, "a"}, {0.0, "b"}, {0.0, "c"}, {0.0, "d"}, {0.0, "e"}};
        (void) co_await redis.zadd(key, members);

        // ZLEXCOUNT test
        qb::redis::lex_interval interval("b", "d", qb::redis::BoundType::CLOSED);
        auto                    lexcount_reply = co_await redis.zlexcount(key, interval);
        EXPECT_TRUE(lexcount_reply.ok());
        EXPECT_EQ(lexcount_reply.result(), 3); // b, c, d

        // Cleanup
        (void) co_await redis.del(key);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test ZPOPMAX and ZPOPMIN
TEST_P(SortedSetProtocolModesTest, CORO_SORTED_SET_COMMANDS_POP) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("pop");

        // Setup sorted set
        std::vector<qb::redis::score_member> members = {{10.0, "a"}, {20.0, "b"}, {30.0, "c"}, {40.0, "d"}, {50.0, "e"}};
        (void) co_await redis.zadd(key, members);

        // ZPOPMAX test
        auto popmax_reply = co_await redis.zpopmax(key, 2);
        EXPECT_TRUE(popmax_reply.ok());
        EXPECT_EQ(popmax_reply.result().size(), 2);
        EXPECT_EQ(popmax_reply.result()[0].member, "e"); // Highest score
        EXPECT_EQ(popmax_reply.result()[1].member, "d");

        // Verify remaining count
        auto card1 = co_await redis.zcard(key);
        EXPECT_EQ(card1.result(), 3);

        // ZPOPMIN test
        auto popmin_reply = co_await redis.zpopmin(key, 1);
        EXPECT_TRUE(popmin_reply.ok());
        EXPECT_EQ(popmin_reply.result().size(), 1);
        EXPECT_EQ(popmin_reply.result()[0].member, "a"); // Lowest score

        // Verify remaining count
        auto card2 = co_await redis.zcard(key);
        EXPECT_EQ(card2.result(), 2);

        // Cleanup
        (void) co_await redis.del(key);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test ZREM and ZREMRANGE
TEST_P(SortedSetProtocolModesTest, CORO_SORTED_SET_COMMANDS_REMOVE) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("remove");

        // Setup sorted set
        std::vector<qb::redis::score_member> members = {{10.0, "a"}, {20.0, "b"}, {30.0, "c"}, {40.0, "d"}, {50.0, "e"}};
        (void) co_await redis.zadd(key, members);

        // ZREM test
        auto rem_reply = co_await redis.zrem(key, {"b", "d"});
        EXPECT_TRUE(rem_reply.ok());
        EXPECT_EQ(rem_reply.result(), 2);

        auto card1 = co_await redis.zcard(key);
        EXPECT_EQ(card1.result(), 3);

        // ZREMRANGEBYRANK test
        auto remrank_reply = co_await redis.zremrangebyrank(key, 0, 1);
        EXPECT_TRUE(remrank_reply.ok());
        EXPECT_EQ(remrank_reply.result(), 2);

        auto card2 = co_await redis.zcard(key);
        EXPECT_EQ(card2.result(), 1);

        // Cleanup
        (void) co_await redis.del(key);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test ZINCRBY
TEST_P(SortedSetProtocolModesTest, CORO_SORTED_SET_COMMANDS_INCR) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("incr");

        // Setup sorted set
        std::vector<qb::redis::score_member> incr_members = {{10.0, "score"}};
        (void) co_await redis.zadd(key, incr_members);

        // ZINCRBY test
        auto incr1 = co_await redis.zincrby(key, 5.0, "score");
        EXPECT_TRUE(incr1.ok());
        EXPECT_DOUBLE_EQ(incr1.result(), 15.0);

        auto incr2 = co_await redis.zincrby(key, -3.0, "score");
        EXPECT_TRUE(incr2.ok());
        EXPECT_DOUBLE_EQ(incr2.result(), 12.0);

        // Verify
        auto score = co_await redis.zscore(key, "score");
        EXPECT_DOUBLE_EQ(*score.result(), 12.0);

        // Cleanup
        (void) co_await redis.del(key);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test set operations: ZUNIONSTORE, ZINTERSTORE
TEST_P(SortedSetProtocolModesTest, CORO_SORTED_SET_COMMANDS_UNION_INTER) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string set1       = protocol_key("set1");
        std::string set2       = protocol_key("set2");
        std::string union_dest = protocol_key("union");
        std::string inter_dest = protocol_key("inter");

        // Setup sets (named vectors: a braced-init-list passed inline through co_await
        // trips a GCC-14 internal compiler error in gimplify, so hoist them).
        std::vector<qb::redis::score_member> set1_members = {{1.0, "a"}, {2.0, "b"}, {3.0, "c"}};
        std::vector<qb::redis::score_member> set2_members = {{2.0, "b"}, {3.0, "c"}, {4.0, "d"}};
        (void) co_await redis.zadd(set1, set1_members);
        (void) co_await redis.zadd(set2, set2_members);

        // ZUNIONSTORE test
        auto union_reply = co_await redis.zunionstore(union_dest, {set1, set2});
        EXPECT_TRUE(union_reply.ok());
        EXPECT_EQ(union_reply.result(), 4); // a, b, c, d

        auto union_card = co_await redis.zcard(union_dest);
        EXPECT_EQ(union_card.result(), 4);

        // ZINTERSTORE test
        auto inter_reply = co_await redis.zinterstore(inter_dest, {set1, set2});
        EXPECT_TRUE(inter_reply.ok());
        EXPECT_EQ(inter_reply.result(), 2); // b, c (common members)

        auto inter_card = co_await redis.zcard(inter_dest);
        EXPECT_EQ(inter_card.result(), 2);

        // Cleanup
        (void) co_await redis.del(set1, set2, union_dest, inter_dest);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test ZRANGEBYSCORE and ZREVRANGEBYSCORE
TEST_P(SortedSetProtocolModesTest, CORO_SORTED_SET_COMMANDS_RANGE_BY_SCORE) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("range-by-score");

        // Setup sorted set
        std::vector<qb::redis::score_member> members = {{10.0, "a"}, {20.0, "b"}, {30.0, "c"}, {40.0, "d"}, {50.0, "e"}};
        (void) co_await redis.zadd(key, members);

        // ZRANGEBYSCORE test
        qb::redis::score_interval interval(20.0, 40.0, qb::redis::BoundType::CLOSED);
        auto                      range_reply = co_await redis.zrangebyscore(key, interval);
        EXPECT_TRUE(range_reply.ok());
        EXPECT_EQ(range_reply.result().size(), 3); // b, c, d

        // ZREVRANGEBYSCORE test
        auto revrange_reply = co_await redis.zrevrangebyscore(key, interval);
        EXPECT_TRUE(revrange_reply.ok());
        EXPECT_EQ(revrange_reply.result().size(), 3);
        EXPECT_EQ(revrange_reply.result()[0].member, "d"); // Reversed order
        EXPECT_EQ(revrange_reply.result()[1].member, "c");
        EXPECT_EQ(revrange_reply.result()[2].member, "b");

        // Cleanup
        (void) co_await redis.del(key);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test ZSCAN
TEST_P(SortedSetProtocolModesTest, CORO_SORTED_SET_COMMANDS_SCAN) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("scan");

        // Setup sorted set
        std::vector<qb::redis::score_member> members = {{1.0, "m1"}, {2.0, "m2"}, {3.0, "m3"}, {4.0, "m4"}, {5.0, "m5"}};
        (void) co_await redis.zadd(key, members);

        // ZSCAN test
        auto scan_reply = co_await redis.zscan(key, 0, "*", 10);
        EXPECT_TRUE(scan_reply.ok());
        EXPECT_EQ(scan_reply.result().items.size(), 5);

        // Cleanup
        (void) co_await redis.del(key);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test ZRANGEBYLEX, ZREVRANGEBYLEX, ZREMRANGEBYLEX
TEST_P(SortedSetProtocolModesTest, CORO_SORTED_SET_COMMANDS_LEX_RANGE) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("lexrange");

        // Add members with score 0 (lexicographic ordering)
        std::vector<qb::redis::score_member> members = {{0, "a"}, {0, "b"}, {0, "c"}, {0, "d"}, {0, "e"}, {0, "f"}, {0, "g"}};
        (void) co_await redis.zadd(key, members);

        // ZRANGEBYLEX: [b, (e] → b, c, d
        qb::redis::lex_interval lex_interval("b", "e", qb::redis::BoundType::LEFT_OPEN);
        auto                    range_reply = co_await redis.zrangebylex(key, lex_interval);
        EXPECT_TRUE(range_reply.ok());
        EXPECT_EQ(range_reply.result().size(), 3u);
        EXPECT_EQ(range_reply.result()[0], "c");
        EXPECT_EQ(range_reply.result()[1], "d");
        EXPECT_EQ(range_reply.result()[2], "e");

        // ZREVRANGEBYLEX: (e, b] → e, d, c (reversed)
        auto rev_reply = co_await redis.zrevrangebylex(key, lex_interval);
        EXPECT_TRUE(rev_reply.ok());
        EXPECT_EQ(rev_reply.result().size(), 3u);
        EXPECT_EQ(rev_reply.result()[0], "e");
        EXPECT_EQ(rev_reply.result()[1], "d");
        EXPECT_EQ(rev_reply.result()[2], "c");

        // ZRANGEBYLEX with CLOSED bounds: [b, e] → b, c, d, e
        qb::redis::lex_interval closed_interval("b", "e", qb::redis::BoundType::CLOSED);
        auto                    closed_reply = co_await redis.zrangebylex(key, closed_interval);
        EXPECT_TRUE(closed_reply.ok());
        EXPECT_EQ(closed_reply.result().size(), 4u);

        // ZREMRANGEBYLEX: remove [c, e]
        qb::redis::lex_interval remove_interval("c", "e", qb::redis::BoundType::CLOSED);
        auto                    rem_reply = co_await redis.zremrangebylex(key, remove_interval);
        EXPECT_TRUE(rem_reply.ok());
        EXPECT_EQ(rem_reply.result(), 3); // c, d, e removed

        // Verify remaining: a, b, f, g
        auto count_reply = co_await redis.zcard(key);
        EXPECT_EQ(count_reply.result(), 4);

        (void) co_await redis.del(key);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test ZDIFF, ZDIFFSTORE
TEST_P(SortedSetProtocolModesTest, CORO_SORTED_SET_COMMANDS_ZDIFF) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string set1 = protocol_key("zdiff1");
        std::string set2 = protocol_key("zdiff2");
        std::string dest = protocol_key("zdiff_dest");

        std::vector<qb::redis::score_member> zdiff_set1_members = {{1.0, "a"}, {2.0, "b"}, {3.0, "c"}};
        std::vector<qb::redis::score_member> zdiff_set2_members = {{2.0, "b"}, {3.0, "c"}, {4.0, "d"}};
        (void) co_await redis.zadd(set1, zdiff_set1_members);
        (void) co_await redis.zadd(set2, zdiff_set2_members);

        // ZDIFF: first set minus others = elements in set1 not in set2 = {a}
        auto diff_r = co_await redis.zdiff({set1, set2});
        EXPECT_TRUE(diff_r.ok());
        EXPECT_EQ(diff_r.result().size(), 1u);
        EXPECT_EQ(diff_r.result()[0], "a");

        // ZDIFF WITHSCORES
        auto diff_scores = co_await redis.zdiffWithScores({set1, set2});
        EXPECT_TRUE(diff_scores.ok());
        EXPECT_EQ(diff_scores.result().size(), 1u);
        EXPECT_EQ(diff_scores.result()[0].member, "a");
        EXPECT_DOUBLE_EQ(diff_scores.result()[0].score, 1.0);

        // ZDIFFSTORE
        auto store_r = co_await redis.zdiffstore(dest, {set1, set2});
        EXPECT_TRUE(store_r.ok());
        EXPECT_EQ(store_r.result(), 1);

        auto card_r = co_await redis.zcard(dest);
        EXPECT_EQ(card_r.result(), 1);

        (void) co_await redis.del(set1, set2, dest);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);
}

// Test ZINTER, ZINTERCARD
TEST_P(SortedSetProtocolModesTest, CORO_SORTED_SET_COMMANDS_ZINTER) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string set1 = protocol_key("zinter1");
        std::string set2 = protocol_key("zinter2");

        std::vector<qb::redis::score_member> zinter_set1_members = {{1.0, "a"}, {2.0, "b"}, {3.0, "c"}};
        std::vector<qb::redis::score_member> zinter_set2_members = {{2.0, "b"}, {3.0, "c"}, {4.0, "d"}};
        (void) co_await redis.zadd(set1, zinter_set1_members);
        (void) co_await redis.zadd(set2, zinter_set2_members);

        // ZINTER
        auto inter_r = co_await redis.zinter({set1, set2});
        EXPECT_TRUE(inter_r.ok());
        EXPECT_EQ(inter_r.result().size(), 2u);

        // ZINTER WITHSCORES
        auto inter_scores = co_await redis.zinterWithScores({set1, set2});
        EXPECT_TRUE(inter_scores.ok());
        EXPECT_EQ(inter_scores.result().size(), 2u);

        // ZINTERCARD
        auto card_r = co_await redis.zintercard({set1, set2});
        EXPECT_TRUE(card_r.ok());
        EXPECT_EQ(card_r.result(), 2);

        (void) co_await redis.del(set1, set2);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);
}

// Test ZMPOP, ZMSCORE, ZRANDMEMBER, ZRANGESTORE
TEST_P(SortedSetProtocolModesTest, CORO_SORTED_SET_COMMANDS_ZMPOP_ZMSCORE_ZRANDMEMBER) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("zmpop");
        std::string src = protocol_key("zrangestore_src");
        std::string dst = protocol_key("zrangestore_dst");

        std::vector<qb::redis::score_member> zmpop_members = {{1.0, "a"}, {2.0, "b"}, {3.0, "c"}};
        (void) co_await redis.zadd(key, zmpop_members);

        // ZMPOP MIN
        auto pop_r = co_await redis.zmpop({key}, "MIN", 1);
        EXPECT_TRUE(pop_r.ok());
        EXPECT_TRUE(pop_r.result().has_value());
        EXPECT_EQ(pop_r.result()->first, key);
        EXPECT_EQ(pop_r.result()->second.size(), 1u);
        EXPECT_EQ(pop_r.result()->second[0].member, "a");

        // ZMSCORE
        std::vector<qb::redis::score_member> zmscore_members = {{1.0, "a"}, {2.0, "b"}, {3.0, "c"}};
        (void) co_await redis.zadd(key, zmscore_members);
        auto mscore_r = co_await redis.zmscore(key, {"a", "b", "nonexistent"});
        EXPECT_TRUE(mscore_r.ok());
        EXPECT_EQ(mscore_r.result().size(), 3u);
        EXPECT_TRUE(mscore_r.result()[0].has_value());
        EXPECT_DOUBLE_EQ(*mscore_r.result()[0], 1.0);
        EXPECT_TRUE(mscore_r.result()[1].has_value());
        EXPECT_DOUBLE_EQ(*mscore_r.result()[1], 2.0);
        EXPECT_FALSE(mscore_r.result()[2].has_value());

        // ZRANDMEMBER
        auto rand_r = co_await redis.zrandmember(key);
        EXPECT_TRUE(rand_r.ok());
        EXPECT_TRUE(rand_r.result().has_value());

        auto rand_count = co_await redis.zrandmemberCount(key, 2);
        EXPECT_TRUE(rand_count.ok());
        EXPECT_EQ(rand_count.result().size(), 2u);

        // ZRANDMEMBER WITHSCORES
        auto rand_scores = co_await redis.zrandmemberWithScores(key, 2);
        EXPECT_TRUE(rand_scores.ok());
        EXPECT_EQ(rand_scores.result().size(), 2u);
        for (const auto &sm : rand_scores.result()) {
            EXPECT_FALSE(sm.member.empty());
        }

        // ZRANGESTORE
        std::vector<qb::redis::score_member> rangestore_src_members = {{1.0, "a"}, {2.0, "b"}, {3.0, "c"}, {4.0, "d"}};
        (void) co_await redis.zadd(src, rangestore_src_members);
        auto rangestore_r = co_await redis.zrangestore(dst, src, "1", "3", {"BYSCORE"});
        EXPECT_TRUE(rangestore_r.ok());
        EXPECT_EQ(rangestore_r.result(), 3);

        auto dst_card = co_await redis.zcard(dst);
        EXPECT_EQ(dst_card.result(), 3);

        // BZMPOP with timeout 1 and non-empty key (returns immediately)
        std::vector<qb::redis::score_member> bzmpop_members = {{1.0, "x"}};
        (void) co_await redis.zadd(key, bzmpop_members);
        auto bzmpop_r = co_await redis.bzmpop({key}, 1, "MIN", 1);
        EXPECT_TRUE(bzmpop_r.ok());
        EXPECT_TRUE(bzmpop_r.result().has_value());
        EXPECT_EQ(bzmpop_r.result()->second.size(), 1u);

        (void) co_await redis.del(key, src, dst);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);
}

// Test ZCOUNT and ZREMRANGEBYSCORE
TEST_P(SortedSetProtocolModesTest, CORO_SORTED_SET_COMMANDS_COUNT_AND_REMRANGEBYSCORE) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("zcount");

        std::vector<qb::redis::score_member> members = {{1.0, "one"}, {2.0, "two"}, {3.0, "three"}, {4.0, "four"}, {5.0, "five"}};
        (void) co_await redis.zadd(key, members);

        // ZCOUNT with closed interval [2, 4] → 3 members
        qb::redis::score_interval interval(2.0, 4.0, qb::redis::BoundType::CLOSED);
        auto                      count_reply = co_await redis.zcount(key, interval);
        EXPECT_TRUE(count_reply.ok());
        EXPECT_EQ(count_reply.result(), 3);

        // ZCOUNT with open lower bound (2, 4] → 2 members
        qb::redis::score_interval open_interval(2.0, 4.0, qb::redis::BoundType::LEFT_OPEN);
        auto                      open_count = co_await redis.zcount(key, open_interval);
        EXPECT_TRUE(open_count.ok());
        EXPECT_EQ(open_count.result(), 2);

        // ZREMRANGEBYSCORE: remove [1, 2]
        qb::redis::score_interval remove_interval(1.0, 2.0, qb::redis::BoundType::CLOSED);
        auto                      rem_reply = co_await redis.zremrangebyscore(key, remove_interval);
        EXPECT_TRUE(rem_reply.ok());
        EXPECT_EQ(rem_reply.result(), 2); // one, two removed

        // Verify 3 remain: three, four, five
        auto card_reply = co_await redis.zcard(key);
        EXPECT_EQ(card_reply.result(), 3);

        (void) co_await redis.del(key);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

TEST_P(SortedSetProtocolModesTest, ZADD_ZRANGE) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto                                 k = protocol_key("zset");
        std::vector<qb::redis::score_member> members{{1.0, "a"}, {2.0, "b"}, {3.0, "c"}};
        auto                                 add_r = co_await redis.zadd(k, members);
        EXPECT_TRUE(add_r.ok()) << add_r.error();
        if (add_r.ok())
            {
            EXPECT_EQ(add_r.result(), 3);
            }
        auto range_r = co_await redis.zrange(k, 0, -1);
        EXPECT_TRUE(range_r.ok()) << range_r.error();
        if (range_r.ok()) {
            const auto &sm = range_r.result();
            EXPECT_EQ(sm.size(), 3u);
            EXPECT_EQ(sm[0].member, "a");
            EXPECT_EQ(sm[1].member, "b");
            EXPECT_EQ(sm[2].member, "c");
        }
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(SortedSetProtocolModesTest, ZSCORE_ZINCRBY_DOUBLE) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k = protocol_key("zscore");
        std::vector<qb::redis::score_member> zscore_members = {{1.0, "m"}};
        (void) co_await redis.zadd(k, zscore_members);
        auto score_r = co_await redis.zscore(k, "m");
        EXPECT_TRUE(score_r.ok()) << score_r.error();
        if (score_r.ok() && score_r.result())
            {
            EXPECT_DOUBLE_EQ(*score_r.result(), 1.0);
            }
        auto incr_r = co_await redis.zincrby(k, 0.5, "m");
        EXPECT_TRUE(incr_r.ok()) << incr_r.error();
        if (incr_r.ok())
            {
            EXPECT_DOUBLE_EQ(incr_r.result(), 1.5);
            }
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(SortedSetProtocolModesTest, ZRANK_ZCARD) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k = protocol_key("zrank");
        std::vector<qb::redis::score_member> zrank_members = {{1.0, "a"}, {2.0, "b"}, {3.0, "c"}};
        (void) co_await redis.zadd(k, zrank_members);
        auto rank_r = co_await redis.zrank(k, "b");
        EXPECT_TRUE(rank_r.ok()) << rank_r.error();
        if (rank_r.ok() && rank_r.result())
            {
            EXPECT_EQ(*rank_r.result(), 1);
            }
        auto card_r = co_await redis.zcard(k);
        EXPECT_TRUE(card_r.ok()) << card_r.error();
        if (card_r.ok())
            {
            EXPECT_EQ(card_r.result(), 3);
            }
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(SortedSetProtocolModesTest, ZDIFF_ZMSCORE) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k1 = protocol_key("zdiff1");
        auto k2 = protocol_key("zdiff2");
        std::vector<qb::redis::score_member> zdiff_k1_members = {{1.0, "a"}, {2.0, "b"}};
        std::vector<qb::redis::score_member> zdiff_k2_members = {{2.0, "b"}};
        (void) co_await redis.zadd(k1, zdiff_k1_members);
        (void) co_await redis.zadd(k2, zdiff_k2_members);
        auto diff_r = co_await redis.zdiff({k1, k2});
        EXPECT_TRUE(diff_r.ok()) << diff_r.error();
        if (diff_r.ok())
            {
            EXPECT_EQ(diff_r.result().size(), 1u);
            }
        auto mscore_r = co_await redis.zmscore(k1, {"a", "b"});
        EXPECT_TRUE(mscore_r.ok()) << mscore_r.error();
        if (mscore_r.ok())
            {
            EXPECT_EQ(mscore_r.result().size(), 2u);
            }
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(SortedSetProtocolModesTest, ZREM_INTEGER) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k = protocol_key("zrem");
        std::vector<qb::redis::score_member> zrem_members = {{1.0, "a"}, {2.0, "b"}, {3.0, "c"}};
        (void) co_await redis.zadd(k, zrem_members);
        auto r = co_await redis.zrem(k, std::vector<std::string>{"b"});
        EXPECT_TRUE(r.ok()) << r.error();
        if (r.ok())
            {
            EXPECT_EQ(r.result(), 1);
            }
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

// Main function to run the tests
int
main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
