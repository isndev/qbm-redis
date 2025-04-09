/*
 * qb - C++ Actor Framework
 * Copyright (C) 2011-2025 isndev (cpp.actor). All rights reserved.
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
#include "../redis.h"

// Configuration Redis
#define REDIS_URI {"tcp://localhost:6379"}

using namespace qb::io;
using namespace std::chrono;

// Generates unique key prefixes to avoid collisions between tests
inline std::string
key_prefix(const std::string &key = "") {
    static int  counter = 0;
    std::string prefix  = "qb::redis::sorted-set-test:" + std::to_string(++counter);

    if (key.empty()) {
        return prefix;
    }

    return prefix + ":" + key;
}

// Generates a test key
inline std::string
test_key(const std::string &k) {
    return "{" + key_prefix() + "}::" + k;
}

// Verifies connection and cleans environment before tests
class RedisTest : public ::testing::Test {
protected:
    qb::redis::tcp::client redis{REDIS_URI};

    void
    SetUp() override {
        async::init();
        if (!redis.connect() || !redis.flushall())
            throw std::runtime_error("Impossible de se connecter à Redis");

        // Wait for connection to be established
        redis.await();
        TearDown();
    }

    void
    TearDown() override {
        // Cleanup after tests
        redis.flushall();
        redis.await();
    }
};

/*
 * TESTS SYNCHRONES
 */

// Test ZADD/ZCARD
TEST_F(RedisTest, SYNC_SORTED_SET_COMMANDS_ZADD_ZCARD) {
    std::string key = test_key("zadd_zcard");

    // Add members
    EXPECT_EQ(redis.zadd(key, {{1.0, "member1"}, {2.0, "member2"}, {3.0, "member3"}}),
              3);

    // Verify the number of members
    EXPECT_EQ(redis.zcard(key), 3);

    // Add existing members
    EXPECT_EQ(redis.zadd(key, {{1.0, "member1"}, {2.0, "member2"}}), 0);

    // Verify that the number hasn't changed
    EXPECT_EQ(redis.zcard(key), 3);
}

// Test ZADD with options
TEST_F(RedisTest, SYNC_SORTED_SET_COMMANDS_ZADD_OPTIONS) {
    std::string key = test_key("zadd_options");

    // Add members with the NX option (does not exist)
    EXPECT_EQ(redis.zadd(key, {{1.0, "member1"}, {2.0, "member2"}},
                         qb::redis::UpdateType::NOT_EXIST),
              2);

    // Try to add the same members with NX
    EXPECT_EQ(redis.zadd(key, {{1.0, "member1"}, {2.0, "member2"}},
                         qb::redis::UpdateType::NOT_EXIST),
              0);

    // Add with the XX option (already exists)
    EXPECT_EQ(redis.zadd(key, {{3.0, "member1"}, {4.0, "member2"}},
                         qb::redis::UpdateType::EXIST),
              0);

    // Verify the updated scores
    EXPECT_EQ(redis.zscore(key, "member1"), 3.0);
    EXPECT_EQ(redis.zscore(key, "member2"), 4.0);

    // Test with the CH option (counts members added or updated)
    EXPECT_EQ(redis.zadd(key, {{5.0, "member1"}, {6.0, "member2"}},
                         qb::redis::UpdateType::EXIST, true),
              2);

    // Verify the updated scores again
    EXPECT_EQ(redis.zscore(key, "member1"), 5.0);
    EXPECT_EQ(redis.zscore(key, "member2"), 6.0);
}

// Test ZADD with the CHANGED option
TEST_F(RedisTest, SYNC_SORTED_SET_COMMANDS_ZADD_CHANGED) {
    std::string key = test_key("zadd_changed");

    // Add members
    EXPECT_EQ(redis.zadd(key, {{1.0, "member1"}, {2.0, "member2"}},
                         qb::redis::UpdateType::ALWAYS, true),
              2);

    // Modify an existing member
    EXPECT_EQ(redis.zadd(key, {{3.0, "member1"}}, qb::redis::UpdateType::ALWAYS, true),
              1);

    // Verify the updated score
    EXPECT_EQ(redis.zscore(key, "member1"), 3.0);
}

// Test ZINCRBY
TEST_F(RedisTest, SYNC_SORTED_SET_COMMANDS_ZINCRBY) {
    std::string key = test_key("zincrby");

    // Increment a member that does not exist
    EXPECT_DOUBLE_EQ(redis.zincrby(key, 1.0, "member1"), 1.0);

    // Increment the same member
    EXPECT_DOUBLE_EQ(redis.zincrby(key, 2.0, "member1"), 3.0);

    // Increment with a negative number
    EXPECT_DOUBLE_EQ(redis.zincrby(key, -1.0, "member1"), 2.0);

    // Verify the final score
    auto score = redis.zscore(key, "member1");
    EXPECT_TRUE(score.has_value());
    EXPECT_DOUBLE_EQ(*score, 2.0);
}

// Test ZRANGE/ZREVRANGE
TEST_F(RedisTest, SYNC_SORTED_SET_COMMANDS_RANGE) {
    std::string key = test_key("range");

    // Add members with their scores
    redis.zadd(key, {{1.0, "member1"},
                     {2.0, "member2"},
                     {3.0, "member3"},
                     {4.0, "member4"},
                     {5.0, "member5"}});

    // Test ZRANGE
    auto range = redis.zrange(key, 0, -1);
    EXPECT_EQ(range.size(), 5);
    EXPECT_EQ(range[0].member, "member1");
    EXPECT_EQ(range[4].member, "member5");

    // Test ZRANGE with negative indices
    range = redis.zrange(key, -2, -1);
    EXPECT_EQ(range.size(), 2);
    EXPECT_EQ(range[0].member, "member4");
    EXPECT_EQ(range[1].member, "member5");

    // Test ZREVRANGE
    auto revrange = redis.zrevrange(key, 0, -1);
    EXPECT_EQ(revrange.size(), 5);
    EXPECT_EQ(revrange[0].member, "member5");
    EXPECT_EQ(revrange[4].member, "member1");

    // Test ZREVRANGE with negative indices
    revrange = redis.zrevrange(key, -2, -1);
    EXPECT_EQ(revrange.size(), 2);
    EXPECT_EQ(revrange[0].member, "member2");
    EXPECT_EQ(revrange[1].member, "member1");
}

// Test ZRANGEBYSCORE/ZREVRANGEBYSCORE
TEST_F(RedisTest, SYNC_SORTED_SET_COMMANDS_RANGEBYSCORE) {
    std::string key = test_key("rangebyscore");

    // Add members with their scores
    redis.zadd(key, {{1.0, "member1"},
                     {2.0, "member2"},
                     {3.0, "member3"},
                     {4.0, "member4"},
                     {5.0, "member5"}});

    // Test ZRANGEBYSCORE
    auto range = redis.zrangebyscore(
        key, qb::redis::BoundedInterval<double>(2.0, 4.0, qb::redis::BoundType::CLOSED));
    EXPECT_EQ(range.size(), 3);
    EXPECT_EQ(range[0].member, "member2");
    EXPECT_EQ(range[2].member, "member4");

    // Test ZRANGEBYSCORE with limit
    qb::redis::LimitOptions limit;
    limit.offset = 1;
    limit.count  = 2;
    range        = redis.zrangebyscore(
        key, qb::redis::BoundedInterval<double>(1.0, 5.0, qb::redis::BoundType::CLOSED),
        limit);
    EXPECT_EQ(range.size(), 2);
    EXPECT_EQ(range[0].member, "member2");
    EXPECT_EQ(range[1].member, "member3");

    // Test ZREVRANGEBYSCORE
    auto revrange = redis.zrevrangebyscore(
        key, qb::redis::BoundedInterval<double>(2.0, 4.0, qb::redis::BoundType::CLOSED));
    EXPECT_EQ(revrange.size(), 3);
    EXPECT_EQ(revrange[0].member, "member4");
    EXPECT_EQ(revrange[2].member, "member2");

    // Test ZREVRANGEBYSCORE with limit
    revrange = redis.zrevrangebyscore(
        key, qb::redis::BoundedInterval<double>(1.0, 5.0, qb::redis::BoundType::CLOSED),
        limit);
    EXPECT_EQ(revrange.size(), 2);
    EXPECT_EQ(revrange[0].member, "member4");
    EXPECT_EQ(revrange[1].member, "member3");
}

// Test ZRANGEBYLEX/ZREVRANGEBYLEX
TEST_F(RedisTest, SYNC_SORTED_SET_COMMANDS_RANGEBYLEX) {
    std::string key = test_key("rangebylex");

    // Add members with the same score (for lexicographical sorting)
    redis.zadd(key, {{0.0, "a"}, {0.0, "b"}, {0.0, "c"}, {0.0, "d"}, {0.0, "e"}});

    // Test ZRANGEBYLEX
    auto range = redis.zrangebylex(key, qb::redis::BoundedInterval<std::string>(
                                            "b", "d", qb::redis::BoundType::CLOSED));
    EXPECT_EQ(range.size(), 3);
    EXPECT_EQ(range[0], "b");
    EXPECT_EQ(range[2], "d");

    // Test ZRANGEBYLEX with limit
    qb::redis::LimitOptions limit;
    limit.offset = 1;
    limit.count  = 2;
    range        = redis.zrangebylex(
        key,
        qb::redis::BoundedInterval<std::string>("a", "e", qb::redis::BoundType::CLOSED),
        limit);
    EXPECT_EQ(range.size(), 2);
    EXPECT_EQ(range[0], "b");
    EXPECT_EQ(range[1], "c");

    // Test ZREVRANGEBYLEX
    auto revrange = redis.zrevrangebylex(
        key,
        qb::redis::BoundedInterval<std::string>("b", "d", qb::redis::BoundType::CLOSED));
    EXPECT_EQ(revrange.size(), 3);
    EXPECT_EQ(revrange[0], "d");
    EXPECT_EQ(revrange[2], "b");

    // Test ZREVRANGEBYLEX with limit
    revrange = redis.zrevrangebylex(
        key,
        qb::redis::BoundedInterval<std::string>("a", "e", qb::redis::BoundType::CLOSED),
        limit);
    EXPECT_EQ(revrange.size(), 2);
    EXPECT_EQ(revrange[0], "d");
    EXPECT_EQ(revrange[1], "c");
}

// Test ZRANK/ZREVRANK
TEST_F(RedisTest, SYNC_SORTED_SET_COMMANDS_RANK) {
    std::string key = test_key("rank");

    // Add members with their scores
    redis.zadd(key, {{1.0, "member1"},
                     {2.0, "member2"},
                     {3.0, "member3"},
                     {4.0, "member4"},
                     {5.0, "member5"}});

    // Test ZRANK
    auto rank = redis.zrank(key, "member3");
    EXPECT_TRUE(rank.has_value());
    EXPECT_EQ(*rank, 2);

    // Test ZRANK with a non-existent member
    rank = redis.zrank(key, "nonexistent");
    EXPECT_FALSE(rank.has_value());

    // Test ZREVRANK
    auto revrank = redis.zrevrank(key, "member3");
    EXPECT_TRUE(revrank.has_value());
    EXPECT_EQ(*revrank, 2);

    // Test ZREVRANK with a non-existent member
    revrank = redis.zrevrank(key, "nonexistent");
    EXPECT_FALSE(revrank.has_value());
}

// Test ZSCORE
TEST_F(RedisTest, SYNC_SORTED_SET_COMMANDS_ZSCORE) {
    std::string key = test_key("zscore");

    // Add a member with its score
    redis.zadd(key, {{1.5, "member1"}});

    // Test ZSCORE
    auto score = redis.zscore(key, "member1");
    EXPECT_TRUE(score.has_value());
    EXPECT_DOUBLE_EQ(*score, 1.5);

    // Test ZSCORE with a non-existent member
    score = redis.zscore(key, "nonexistent");
    EXPECT_FALSE(score.has_value());
}

// Test ZREM
TEST_F(RedisTest, SYNC_SORTED_SET_COMMANDS_ZREM) {
    std::string key = test_key("zrem");

    // Add members with their scores
    redis.zadd(key, {{1.0, "member1"}, {2.0, "member2"}, {3.0, "member3"}});

    // Remove members
    EXPECT_EQ(redis.zrem(key, {"member1", "member2"}), 2);

    // Verify that the members have been removed
    EXPECT_EQ(redis.zcard(key), 1);
    EXPECT_TRUE(redis.zscore(key, "member3").has_value());
    EXPECT_FALSE(redis.zscore(key, "member1").has_value());
    EXPECT_FALSE(redis.zscore(key, "member2").has_value());
}

// Test ZREMRANGEBYRANK
TEST_F(RedisTest, SYNC_SORTED_SET_COMMANDS_ZREMRANGEBYRANK) {
    std::string key = test_key("zremrangebyrank");

    // Add members with their scores
    redis.zadd(key, {{1.0, "member1"},
                     {2.0, "member2"},
                     {3.0, "member3"},
                     {4.0, "member4"},
                     {5.0, "member5"}});

    // Remove members in the range [1, 3]
    EXPECT_EQ(redis.zremrangebyrank(key, 1, 3), 3);

    // Verify that the members have been removed
    EXPECT_EQ(redis.zcard(key), 2);
    EXPECT_TRUE(redis.zscore(key, "member1").has_value());
    EXPECT_TRUE(redis.zscore(key, "member5").has_value());
    EXPECT_FALSE(redis.zscore(key, "member2").has_value());
    EXPECT_FALSE(redis.zscore(key, "member3").has_value());
    EXPECT_FALSE(redis.zscore(key, "member4").has_value());
}

// Test ZREMRANGEBYSCORE
TEST_F(RedisTest, SYNC_SORTED_SET_COMMANDS_ZREMRANGEBYSCORE) {
    std::string key = test_key("zremrangebyscore");

    // Add members with their scores
    redis.zadd(key, {{1.0, "member1"},
                     {2.0, "member2"},
                     {3.0, "member3"},
                     {4.0, "member4"},
                     {5.0, "member5"}});

    // Remove members with scores in the range [2, 4]
    EXPECT_EQ(redis.zremrangebyscore(key, qb::redis::BoundedInterval<double>(
                                              2.0, 4.0, qb::redis::BoundType::CLOSED)),
              3);

    // Verify that the members have been removed
    EXPECT_EQ(redis.zcard(key), 2);
    EXPECT_TRUE(redis.zscore(key, "member1").has_value());
    EXPECT_TRUE(redis.zscore(key, "member5").has_value());
    EXPECT_FALSE(redis.zscore(key, "member2").has_value());
    EXPECT_FALSE(redis.zscore(key, "member3").has_value());
    EXPECT_FALSE(redis.zscore(key, "member4").has_value());
}

// Test ZREMRANGEBYLEX
TEST_F(RedisTest, SYNC_SORTED_SET_COMMANDS_ZREMRANGEBYLEX) {
    std::string key = test_key("zremrangebylex");

    // Add members with the same score (for lexicographical sorting)
    redis.zadd(key, {{0.0, "a"}, {0.0, "b"}, {0.0, "c"}, {0.0, "d"}, {0.0, "e"}});

    // Remove members in the lexicographical range [b, d]
    EXPECT_EQ(redis.zremrangebylex(key, qb::redis::BoundedInterval<std::string>(
                                            "b", "d", qb::redis::BoundType::CLOSED)),
              3);

    // Verify that the members have been removed
    EXPECT_EQ(redis.zcard(key), 2);
    EXPECT_TRUE(redis.zscore(key, "a").has_value());
    EXPECT_TRUE(redis.zscore(key, "e").has_value());
    EXPECT_FALSE(redis.zscore(key, "b").has_value());
    EXPECT_FALSE(redis.zscore(key, "c").has_value());
    EXPECT_FALSE(redis.zscore(key, "d").has_value());
}

// Test ZUNIONSTORE/ZINTERSTORE
TEST_F(RedisTest, SYNC_SORTED_SET_COMMANDS_UNION_INTER) {
    std::string key1 = test_key("union1");
    std::string key2 = test_key("union2");
    std::string dest = test_key("union_dest");

    // Create sorted sets
    redis.zadd(key1, {{1.0, "a"}, {2.0, "b"}, {3.0, "c"}});
    redis.zadd(key2, {{2.0, "b"}, {3.0, "c"}, {4.0, "d"}});

    // Test ZUNIONSTORE
    EXPECT_EQ(redis.zunionstore(dest, {key1, key2}), 4);
    auto union_result = redis.zrange(dest, 0, -1);
    EXPECT_EQ(union_result.size(), 4);

    // Verify all members are present (regardless of order)
    std::set<std::string> union_members;
    for (const auto &item : union_result) {
        union_members.insert(item.member);
    }
    EXPECT_TRUE(union_members.count("a") > 0);
    EXPECT_TRUE(union_members.count("b") > 0);
    EXPECT_TRUE(union_members.count("c") > 0);
    EXPECT_TRUE(union_members.count("d") > 0);

    // Verify scores individually
    for (const auto &item : union_result) {
        if (item.member == "a")
            EXPECT_DOUBLE_EQ(item.score, 1.0);
        else if (item.member == "b")
            EXPECT_DOUBLE_EQ(item.score, 4.0);
        else if (item.member == "c")
            EXPECT_DOUBLE_EQ(item.score, 6.0);
        else if (item.member == "d")
            EXPECT_DOUBLE_EQ(item.score, 4.0);
    }

    // Test ZUNIONSTORE with weights
    EXPECT_EQ(redis.zunionstore(dest, {key1, key2}, {2.0, 1.0}), 4);
    union_result = redis.zrange(dest, 0, -1);
    EXPECT_EQ(union_result.size(), 4);

    // Verify scores individually with weights
    for (const auto &item : union_result) {
        if (item.member == "a")
            EXPECT_DOUBLE_EQ(item.score, 2.0); // 1.0 * 2.0
        else if (item.member == "b")
            EXPECT_DOUBLE_EQ(item.score, 6.0); // 2.0 * 2.0 + 2.0 * 1.0
        else if (item.member == "c")
            EXPECT_DOUBLE_EQ(item.score, 9.0); // 3.0 * 2.0 + 3.0 * 1.0
        else if (item.member == "d")
            EXPECT_DOUBLE_EQ(item.score, 4.0); // 4.0 * 1.0
    }

    // Test ZINTERSTORE
    EXPECT_EQ(redis.zinterstore(dest, {key1, key2}), 2);
    auto inter_result = redis.zrange(dest, 0, -1);
    EXPECT_EQ(inter_result.size(), 2);

    // Verify the correct members are present
    std::set<std::string> inter_members;
    for (const auto &item : inter_result) {
        inter_members.insert(item.member);
    }
    EXPECT_TRUE(inter_members.count("b") > 0);
    EXPECT_TRUE(inter_members.count("c") > 0);

    // Verify scores individually
    for (const auto &item : inter_result) {
        if (item.member == "b")
            EXPECT_DOUBLE_EQ(item.score, 4.0); // 2.0 + 2.0
        else if (item.member == "c")
            EXPECT_DOUBLE_EQ(item.score, 6.0); // 3.0 + 3.0
    }

    // Test ZINTERSTORE with weights
    EXPECT_EQ(redis.zinterstore(dest, {key1, key2}, {2.0, 1.0}), 2);
    inter_result = redis.zrange(dest, 0, -1);
    EXPECT_EQ(inter_result.size(), 2);

    // Verify scores individually with weights
    for (const auto &item : inter_result) {
        if (item.member == "b")
            EXPECT_DOUBLE_EQ(item.score, 6.0); // 2.0 * 2.0 + 2.0 * 1.0
        else if (item.member == "c")
            EXPECT_DOUBLE_EQ(item.score, 9.0); // 3.0 * 2.0 + 3.0 * 1.0
    }
}

// Test ZPOPMAX/ZPOPMIN
TEST_F(RedisTest, SYNC_SORTED_SET_COMMANDS_POP) {
    std::string key = test_key("pop");

    // Add members with their scores
    redis.zadd(key, {{1.0, "member1"},
                     {2.0, "member2"},
                     {3.0, "member3"},
                     {4.0, "member4"},
                     {5.0, "member5"}});

    // Test ZPOPMAX
    auto popmax = redis.zpopmax(key);
    EXPECT_EQ(popmax.size(), 1);
    EXPECT_EQ(popmax[0].member, "member5");
    EXPECT_DOUBLE_EQ(popmax[0].score, 5.0);

    // Test ZPOPMAX with count
    popmax = redis.zpopmax(key, 2);
    EXPECT_EQ(popmax.size(), 2);
    EXPECT_EQ(popmax[0].member, "member4");
    EXPECT_EQ(popmax[1].member, "member3");

    // Test ZPOPMIN
    auto popmin = redis.zpopmin(key);
    EXPECT_EQ(popmin.size(), 1);
    EXPECT_EQ(popmin[0].member, "member1");
    EXPECT_DOUBLE_EQ(popmin[0].score, 1.0);

    // Test ZPOPMIN with count
    popmin = redis.zpopmin(key, 2);
    EXPECT_EQ(popmin.size(), 1);
    EXPECT_EQ(popmin[0].member, "member2");
}

// Test BZPOPMAX/BZPOPMIN
TEST_F(RedisTest, SYNC_SORTED_SET_COMMANDS_BLOCKING_POP) {
    std::string key1 = test_key("blocking1");
    std::string key2 = test_key("blocking2");

    // Add a member to key1
    redis.zadd(key1, {{1.0, "member1"}});

    // Test BZPOPMAX
    auto bpopmax = redis.bzpopmax({key1, key2}, 1);
    EXPECT_TRUE(bpopmax.has_value());
    EXPECT_EQ(std::get<0>(*bpopmax), key1);
    EXPECT_EQ(std::get<1>(*bpopmax), "member1");
    EXPECT_DOUBLE_EQ(std::get<2>(*bpopmax), 1.0);

    // Test BZPOPMAX with timeout
    bpopmax = redis.bzpopmax({key1, key2}, 1);
    EXPECT_FALSE(bpopmax.has_value());

    // Add a member to key2
    redis.zadd(key2, {{2.0, "member2"}});

    // Test BZPOPMIN
    auto bpopmin = redis.bzpopmin({key1, key2}, 1);
    EXPECT_TRUE(bpopmin.has_value());
    EXPECT_EQ(std::get<0>(*bpopmin), key2);
    EXPECT_EQ(std::get<1>(*bpopmin), "member2");
    EXPECT_DOUBLE_EQ(std::get<2>(*bpopmin), 2.0);

    // Test BZPOPMIN with timeout
    bpopmin = redis.bzpopmin({key1, key2}, 1);
    EXPECT_FALSE(bpopmin.has_value());
}

// Test ZSCAN
TEST_F(RedisTest, SYNC_SORTED_SET_COMMANDS_SCAN) {
    std::string key = test_key("scan");

    // Add members with their scores
    redis.zadd(key, {{1.0, "member1"},
                     {2.0, "member2"},
                     {3.0, "member3"},
                     {4.0, "member4"},
                     {5.0, "member5"}});

    // Scan members
    std::map<std::string, double> res;
    long long                     cursor = 0;
    while (true) {
        auto scan = redis.zscan(key, cursor, "member*", 2);
        cursor    = scan.cursor;
        std::move(scan.items.begin(), scan.items.end(), std::inserter(res, res.end()));
        if (cursor == 0) {
            break;
        }
    }

    // Verify the results
    EXPECT_EQ(res.size(), 5);
    EXPECT_DOUBLE_EQ(res["member1"], 1.0);
    EXPECT_DOUBLE_EQ(res["member2"], 2.0);
    EXPECT_DOUBLE_EQ(res["member3"], 3.0);
    EXPECT_DOUBLE_EQ(res["member4"], 4.0);
    EXPECT_DOUBLE_EQ(res["member5"], 5.0);
}

// Test ZLEXCOUNT
TEST_F(RedisTest, SYNC_SORTED_SET_COMMANDS_ZLEXCOUNT) {
    std::string key = test_key("zlexcount");

    // Add members with the same score (for lexicographical sorting)
    redis.zadd(key, {{0.0, "a"}, {0.0, "b"}, {0.0, "c"}, {0.0, "d"}, {0.0, "e"}});

    // Test ZLEXCOUNT
    EXPECT_EQ(redis.zlexcount(key, qb::redis::BoundedInterval<std::string>(
                                       "b", "d", qb::redis::BoundType::CLOSED)),
              3);
    EXPECT_EQ(redis.zlexcount(key, qb::redis::BoundedInterval<std::string>(
                                       "a", "e", qb::redis::BoundType::CLOSED)),
              5);

    // Test with open intervals
    EXPECT_EQ(redis.zlexcount(key, qb::redis::BoundedInterval<std::string>(
                                       "a", "e", qb::redis::BoundType::OPEN)),
              3);
}

/*
 * TESTS ASYNCHRONES
 */

// Test asynchronous ZADD/ZCARD
TEST_F(RedisTest, ASYNC_SORTED_SET_COMMANDS_ZADD_ZCARD) {
    std::string key          = test_key("async_zadd_zcard");
    long long   zadd_result  = 0;
    long long   zcard_result = 0;

    // Add members asynchronously
    redis.zadd([&](auto &&reply) { zadd_result = reply.result(); }, key,
               {{1.0, "member1"}, {2.0, "member2"}, {3.0, "member3"}});

    redis.await();
    EXPECT_EQ(zadd_result, 3);

    // Verify the number of members asynchronously
    redis.zcard([&](auto &&reply) { zcard_result = reply.result(); }, key);

    redis.await();
    EXPECT_EQ(zcard_result, 3);
}

// Test asynchronous ZADD with options
TEST_F(RedisTest, ASYNC_SORTED_SET_COMMANDS_ZADD_OPTIONS) {
    std::string           key         = test_key("async_zadd_options");
    long long             zadd_result = 0;
    std::optional<double> score_result;

    // Add members with the NX option
    redis.zadd([&](auto &&reply) { zadd_result = reply.result(); }, key,
               {{1.0, "member1"}, {2.0, "member2"}}, qb::redis::UpdateType::NOT_EXIST);

    redis.await();
    EXPECT_EQ(zadd_result, 2);

    // Verify the score asynchronously
    redis.zscore([&](auto &&reply) { score_result = reply.result(); }, key, "member1");

    redis.await();
    EXPECT_EQ(score_result, 1.0);

    // Test with the XX option (already exists)
    // Without the CH option, should return 0 because elements are not new
    redis.zadd([&](auto &&reply) { zadd_result = reply.result(); }, key,
               {{3.0, "member1"}, {4.0, "member2"}}, qb::redis::UpdateType::EXIST);

    redis.await();
    EXPECT_EQ(zadd_result, 0);

    // Verify that scores have been updated
    redis.zscore([&](auto &&reply) { score_result = reply.result(); }, key, "member1");

    redis.await();
    EXPECT_EQ(score_result, 3.0);

    // Test with the XX option + CH (counts members added or updated)
    redis.zadd([&](auto &&reply) { zadd_result = reply.result(); }, key,
               {{5.0, "member1"}, {6.0, "member2"}}, qb::redis::UpdateType::EXIST, true);

    redis.await();
    EXPECT_EQ(zadd_result, 2);

    // Verify that scores have been updated
    redis.zscore([&](auto &&reply) { score_result = reply.result(); }, key, "member1");

    redis.await();
    EXPECT_EQ(score_result, 5.0);
}

// Test asynchronous ZINCRBY
TEST_F(RedisTest, ASYNC_SORTED_SET_COMMANDS_ZINCRBY) {
    std::string key            = test_key("async_zincrby");
    double      zincrby_result = 0.0;

    // Increment a member asynchronously
    redis.zincrby([&](auto &&reply) { zincrby_result = reply.result(); }, key, 1.0,
                  "member1");

    redis.await();
    EXPECT_DOUBLE_EQ(zincrby_result, 1.0);

    // Increment the same member asynchronously
    redis.zincrby([&](auto &&reply) { zincrby_result = reply.result(); }, key, 2.0,
                  "member1");

    redis.await();
    EXPECT_DOUBLE_EQ(zincrby_result, 3.0);
}

// Test asynchronous ZRANGE/ZREVRANGE
TEST_F(RedisTest, ASYNC_SORTED_SET_COMMANDS_RANGE) {
    std::string                          key = test_key("async_range");
    std::vector<qb::redis::score_member> range_result;
    std::vector<qb::redis::score_member> revrange_result;

    // Add members
    redis.zadd(key, {{1.0, "member1"},
                     {2.0, "member2"},
                     {3.0, "member3"},
                     {4.0, "member4"},
                     {5.0, "member5"}});

    // Test ZRANGE asynchronously
    redis.zrange([&](auto &&reply) { range_result = reply.result(); }, key, 0, -1);

    redis.await();
    EXPECT_EQ(range_result.size(), 5);
    EXPECT_EQ(range_result[0].member, "member1");
    EXPECT_EQ(range_result[4].member, "member5");

    // Test ZREVRANGE asynchronously
    redis.zrevrange([&](auto &&reply) { revrange_result = reply.result(); }, key, 0, -1);

    redis.await();
    EXPECT_EQ(revrange_result.size(), 5);
    EXPECT_EQ(revrange_result[0].member, "member5");
    EXPECT_EQ(revrange_result[4].member, "member1");
}

// Test asynchronous ZRANGEBYSCORE/ZREVRANGEBYSCORE
TEST_F(RedisTest, ASYNC_SORTED_SET_COMMANDS_RANGEBYSCORE) {
    std::string                          key = test_key("async_rangebyscore");
    std::vector<qb::redis::score_member> range_result;
    std::vector<qb::redis::score_member> revrange_result;

    // Add members
    redis.zadd(key, {{1.0, "member1"},
                     {2.0, "member2"},
                     {3.0, "member3"},
                     {4.0, "member4"},
                     {5.0, "member5"}});

    // Test ZRANGEBYSCORE asynchronously
    redis.zrangebyscore(
        [&](auto &&reply) { range_result = reply.result(); }, key,
        qb::redis::BoundedInterval<double>(2.0, 4.0, qb::redis::BoundType::CLOSED));

    redis.await();
    EXPECT_EQ(range_result.size(), 3);
    EXPECT_EQ(range_result[0].member, "member2");
    EXPECT_EQ(range_result[2].member, "member4");

    // Test ZREVRANGEBYSCORE asynchronously
    redis.zrevrangebyscore(
        [&](auto &&reply) { revrange_result = reply.result(); }, key,
        qb::redis::BoundedInterval<double>(2.0, 4.0, qb::redis::BoundType::CLOSED));

    redis.await();
    EXPECT_EQ(revrange_result.size(), 3);
    EXPECT_EQ(revrange_result[0].member, "member4");
    EXPECT_EQ(revrange_result[2].member, "member2");
}

// Test asynchronous ZRANK/ZREVRANK
TEST_F(RedisTest, ASYNC_SORTED_SET_COMMANDS_RANK) {
    std::string              key = test_key("async_rank");
    std::optional<long long> rank_result;
    std::optional<long long> revrank_result;

    // Add members
    redis.zadd(key, {{1.0, "member1"}, {2.0, "member2"}, {3.0, "member3"}});

    // Test ZRANK asynchronously
    redis.zrank([&](auto &&reply) { rank_result = reply.result(); }, key, "member2");

    redis.await();
    EXPECT_TRUE(rank_result.has_value());
    EXPECT_EQ(*rank_result, 1);

    // Test ZREVRANK asynchronously
    redis.zrevrank([&](auto &&reply) { revrank_result = reply.result(); }, key,
                   "member2");

    redis.await();
    EXPECT_TRUE(revrank_result.has_value());
    EXPECT_EQ(*revrank_result, 1);
}

// Test asynchronous ZSCORE
TEST_F(RedisTest, ASYNC_SORTED_SET_COMMANDS_ZSCORE) {
    std::string           key = test_key("async_zscore");
    std::optional<double> score_result;

    // Add a member
    redis.zadd(key, {{1.5, "member1"}});

    // Test ZSCORE asynchronously
    redis.zscore([&](auto &&reply) { score_result = reply.result(); }, key, "member1");

    redis.await();
    EXPECT_TRUE(score_result.has_value());
    EXPECT_DOUBLE_EQ(*score_result, 1.5);
}

// Test asynchronous ZREM
TEST_F(RedisTest, ASYNC_SORTED_SET_COMMANDS_ZREM) {
    std::string key         = test_key("async_zrem");
    long long   zrem_result = 0;

    // Add members
    redis.zadd(key, {{1.0, "member1"}, {2.0, "member2"}, {3.0, "member3"}});

    // Remove members asynchronously
    redis.zrem([&](auto &&reply) { zrem_result = reply.result(); }, key,
               {"member1", "member2"});

    redis.await();
    EXPECT_EQ(zrem_result, 2);
}

// Test asynchronous ZPOPMAX/ZPOPMIN
TEST_F(RedisTest, ASYNC_SORTED_SET_COMMANDS_POP) {
    std::string                          key = test_key("async_pop");
    std::vector<qb::redis::score_member> popmax_result;
    std::vector<qb::redis::score_member> popmin_result;

    // Add members
    redis.zadd(key, {{1.0, "member1"},
                     {2.0, "member2"},
                     {3.0, "member3"},
                     {4.0, "member4"},
                     {5.0, "member5"}});

    // Test ZPOPMAX asynchronously
    redis.zpopmax([&](auto &&reply) { popmax_result = reply.result(); }, key, 2);

    redis.await();
    EXPECT_EQ(popmax_result.size(), 2);
    EXPECT_EQ(popmax_result[0].member, "member5");
    EXPECT_EQ(popmax_result[1].member, "member4");

    // Test ZPOPMIN asynchronously
    redis.zpopmin([&](auto &&reply) { popmin_result = reply.result(); }, key, 2);

    redis.await();
    EXPECT_EQ(popmin_result.size(), 2);
    EXPECT_EQ(popmin_result[0].member, "member1");
    EXPECT_EQ(popmin_result[1].member, "member2");
}

// Test asynchronous BZPOPMAX/BZPOPMIN
TEST_F(RedisTest, ASYNC_SORTED_SET_COMMANDS_BLOCKING_POP) {
    std::string key1 = test_key("async_blocking1");
    std::string key2 = test_key("async_blocking2");
    std::optional<std::tuple<std::string, std::string, double>> bpopmax_result;
    std::optional<std::tuple<std::string, std::string, double>> bpopmin_result;

    // Add a member to key1
    redis.zadd(key1, {{1.0, "member1"}});

    // Test BZPOPMAX asynchronously
    redis.bzpopmax([&](auto &&reply) { bpopmax_result = reply.result(); }, {key1, key2},
                   1);

    redis.await();
    EXPECT_TRUE(bpopmax_result.has_value());
    EXPECT_EQ(std::get<0>(*bpopmax_result), key1);
    EXPECT_EQ(std::get<1>(*bpopmax_result), "member1");
    EXPECT_DOUBLE_EQ(std::get<2>(*bpopmax_result), 1.0);

    // Add a member to key2
    redis.zadd(key2, {{2.0, "member2"}});

    // Test BZPOPMIN asynchronously
    redis.bzpopmin([&](auto &&reply) { bpopmin_result = reply.result(); }, {key1, key2},
                   1);

    redis.await();
    EXPECT_TRUE(bpopmin_result.has_value());
    EXPECT_EQ(std::get<0>(*bpopmin_result), key2);
    EXPECT_EQ(std::get<1>(*bpopmin_result), "member2");
    EXPECT_DOUBLE_EQ(std::get<2>(*bpopmin_result), 2.0);
}

// Test asynchronous ZSCAN
TEST_F(RedisTest, ASYNC_SORTED_SET_COMMANDS_SCAN) {
    std::string                   key = test_key("async_scan");
    std::map<std::string, double> scan_result;
    bool                          scan_completed = false;

    // Add members
    redis.zadd(key, {{1.0, "member1"},
                     {2.0, "member2"},
                     {3.0, "member3"},
                     {4.0, "member4"},
                     {5.0, "member5"}});

    // Scan members asynchronously
    // Scanner les membres de manière asynchrone
    redis.zscan(
        [&](auto &&reply) {
            std::move(reply.result().items.begin(), reply.result().items.end(),
                      std::inserter(scan_result, scan_result.end()));
            scan_completed = true;
        },
        key, 0, "member*", 2);

    redis.await();
    EXPECT_TRUE(scan_completed);
    EXPECT_GE(scan_result.size(), 2); // With count=2, we should only have 2 elements
}

// Test asynchronous ZRANGEBYLEX/ZREVRANGEBYLEX
TEST_F(RedisTest, ASYNC_SORTED_SET_COMMANDS_RANGEBYLEX) {
    std::string              key = test_key("async_rangebylex");
    std::vector<std::string> range_result;
    std::vector<std::string> revrange_result;

    // Add members with the same score (for lexicographical sorting)
    redis.zadd(key, {{0.0, "a"}, {0.0, "b"}, {0.0, "c"}, {0.0, "d"}, {0.0, "e"}});

    // Test ZRANGEBYLEX asynchronously
    redis.zrangebylex(
        [&](auto &&reply) { range_result = reply.result(); }, key,
        qb::redis::BoundedInterval<std::string>("b", "d", qb::redis::BoundType::CLOSED));

    redis.await();
    EXPECT_EQ(range_result.size(), 3);
    EXPECT_EQ(range_result[0], "b");
    EXPECT_EQ(range_result[2], "d");

    // Test ZRANGEBYLEX with limit asynchronously
    qb::redis::LimitOptions limit;
    limit.offset = 1;
    limit.count  = 2;
    redis.zrangebylex(
        [&](auto &&reply) { range_result = reply.result(); }, key,
        qb::redis::BoundedInterval<std::string>("a", "e", qb::redis::BoundType::CLOSED),
        limit);

    redis.await();
    EXPECT_EQ(range_result.size(), 2);
    EXPECT_EQ(range_result[0], "b");
    EXPECT_EQ(range_result[1], "c");

    // Test ZREVRANGEBYLEX asynchronously
    redis.zrevrangebylex(
        [&](auto &&reply) { revrange_result = reply.result(); }, key,
        qb::redis::BoundedInterval<std::string>("b", "d", qb::redis::BoundType::CLOSED));

    redis.await();
    EXPECT_EQ(revrange_result.size(), 3);
    EXPECT_EQ(revrange_result[0], "d");
    EXPECT_EQ(revrange_result[2], "b");

    // Test ZREVRANGEBYLEX with limit asynchronously
    redis.zrevrangebylex(
        [&](auto &&reply) { revrange_result = reply.result(); }, key,
        qb::redis::BoundedInterval<std::string>("a", "e", qb::redis::BoundType::CLOSED),
        limit);

    redis.await();
    EXPECT_EQ(revrange_result.size(), 2);
    EXPECT_EQ(revrange_result[0], "d");
    EXPECT_EQ(revrange_result[1], "c");
}

// Test asynchronous ZLEXCOUNT
TEST_F(RedisTest, ASYNC_SORTED_SET_COMMANDS_ZLEXCOUNT) {
    std::string key          = test_key("async_zlexcount");
    long long   count_result = 0;

    // Add members with the same score (for lexicographical sorting)
    redis.zadd(key, {{0.0, "a"}, {0.0, "b"}, {0.0, "c"}, {0.0, "d"}, {0.0, "e"}});

    // Test ZLEXCOUNT asynchronously
    redis.zlexcount(
        [&](auto &&reply) { count_result = reply.result(); }, key,
        qb::redis::BoundedInterval<std::string>("b", "d", qb::redis::BoundType::CLOSED));

    redis.await();
    EXPECT_EQ(count_result, 3);
}

// Test asynchronous ZREMRANGEBYLEX
TEST_F(RedisTest, ASYNC_SORTED_SET_COMMANDS_ZREMRANGEBYLEX) {
    std::string key        = test_key("async_zremrangebylex");
    long long   rem_result = 0;

    // Add members with the same score (for lexicographical sorting)
    redis.zadd(key, {{0.0, "a"}, {0.0, "b"}, {0.0, "c"}, {0.0, "d"}, {0.0, "e"}});

    // Test ZREMRANGEBYLEX asynchronously
    redis.zremrangebylex(
        [&](auto &&reply) { rem_result = reply.result(); }, key,
        qb::redis::BoundedInterval<std::string>("b", "d", qb::redis::BoundType::CLOSED));

    redis.await();
    EXPECT_EQ(rem_result, 3);

    // Verify remaining members
    auto members = redis.zrange(key, 0, -1);
    EXPECT_EQ(members.size(), 2);
    EXPECT_EQ(members[0].member, "a");
    EXPECT_EQ(members[1].member, "e");
}

// Test asynchronous ZREMRANGEBYSCORE
TEST_F(RedisTest, ASYNC_SORTED_SET_COMMANDS_ZREMRANGEBYSCORE) {
    std::string key        = test_key("async_zremrangebyscore");
    long long   rem_result = 0;

    // Add members with their scores
    redis.zadd(key, {{1.0, "member1"},
                     {2.0, "member2"},
                     {3.0, "member3"},
                     {4.0, "member4"},
                     {5.0, "member5"}});

    // Test ZREMRANGEBYSCORE asynchronously
    redis.zremrangebyscore(
        [&](auto &&reply) { rem_result = reply.result(); }, key,
        qb::redis::BoundedInterval<double>(2.0, 4.0, qb::redis::BoundType::CLOSED));

    redis.await();
    EXPECT_EQ(rem_result, 3);

    // Verify remaining members
    auto members = redis.zrange(key, 0, -1);
    EXPECT_EQ(members.size(), 2);
    EXPECT_EQ(members[0].member, "member1");
    EXPECT_EQ(members[1].member, "member5");
}

// Test asynchronous ZREMRANGEBYRANK
TEST_F(RedisTest, ASYNC_SORTED_SET_COMMANDS_ZREMRANGEBYRANK) {
    std::string key        = test_key("async_zremrangebyrank");
    long long   rem_result = 0;

    // Add members with their scores
    redis.zadd(key, {{1.0, "member1"},
                     {2.0, "member2"},
                     {3.0, "member3"},
                     {4.0, "member4"},
                     {5.0, "member5"}});

    // Test ZREMRANGEBYRANK asynchronously
    redis.zremrangebyrank([&](auto &&reply) { rem_result = reply.result(); }, key, 1, 3);

    redis.await();
    EXPECT_EQ(rem_result, 3);

    // Verify remaining members
    auto members = redis.zrange(key, 0, -1);
    EXPECT_EQ(members.size(), 2);
    EXPECT_EQ(members[0].member, "member1");
    EXPECT_EQ(members[1].member, "member5");
}

// Test asynchronous ZUNIONSTORE/ZINTERSTORE
TEST_F(RedisTest, ASYNC_SORTED_SET_COMMANDS_UNION_INTER) {
    std::string key1         = test_key("async_union1");
    std::string key2         = test_key("async_union2");
    std::string dest1        = test_key("async_union_dest");
    std::string dest2        = test_key("async_inter_dest");
    long long   union_result = 0;
    long long   inter_result = 0;

    // Create sorted sets
    redis.zadd(key1, {{1.0, "a"}, {2.0, "b"}, {3.0, "c"}});
    redis.zadd(key2, {{2.0, "b"}, {3.0, "c"}, {4.0, "d"}});

    // Test ZUNIONSTORE asynchronously
    redis.zunionstore([&](auto &&reply) { union_result = reply.result(); }, dest1,
                      {key1, key2}, {2.0, 1.0}, qb::redis::Aggregation::SUM);

    redis.await();
    EXPECT_EQ(union_result, 4);

    // Verify union result
    auto union_members = redis.zrange(dest1, 0, -1);
    EXPECT_EQ(union_members.size(), 4);

    // Verify all members are present (regardless of order)
    std::set<std::string> union_members_set;
    for (const auto &item : union_members) {
        union_members_set.insert(item.member);
    }
    EXPECT_TRUE(union_members_set.count("a") > 0);
    EXPECT_TRUE(union_members_set.count("b") > 0);
    EXPECT_TRUE(union_members_set.count("c") > 0);
    EXPECT_TRUE(union_members_set.count("d") > 0);

    // Verify scores individually with weights
    for (const auto &item : union_members) {
        if (item.member == "a")
            EXPECT_DOUBLE_EQ(item.score, 2.0); // 1.0 * 2.0
        else if (item.member == "b")
            EXPECT_DOUBLE_EQ(item.score, 6.0); // 2.0 * 2.0 + 2.0 * 1.0
        else if (item.member == "c")
            EXPECT_DOUBLE_EQ(item.score, 9.0); // 3.0 * 2.0 + 3.0 * 1.0
        else if (item.member == "d")
            EXPECT_DOUBLE_EQ(item.score, 4.0); // 4.0 * 1.0
    }

    // Test ZINTERSTORE asynchronously
    redis.zinterstore([&](auto &&reply) { inter_result = reply.result(); }, dest2,
                      {key1, key2}, {2.0, 1.0}, qb::redis::Aggregation::SUM);

    redis.await();
    EXPECT_EQ(inter_result, 2);

    // Verify intersection result
    auto inter_members = redis.zrange(dest2, 0, -1);
    EXPECT_EQ(inter_members.size(), 2);

    // Verify the correct members are present
    std::set<std::string> inter_members_set;
    for (const auto &item : inter_members) {
        inter_members_set.insert(item.member);
    }
    EXPECT_TRUE(inter_members_set.count("b") > 0);
    EXPECT_TRUE(inter_members_set.count("c") > 0);

    // Verify scores individually with weights
    for (const auto &item : inter_members) {
        if (item.member == "b")
            EXPECT_DOUBLE_EQ(item.score, 6.0); // 2.0 * 2.0 + 2.0 * 1.0
        else if (item.member == "c")
            EXPECT_DOUBLE_EQ(item.score, 9.0); // 3.0 * 2.0 + 3.0 * 1.0
    }
}

// Test ZSCAN with auto-iteration
TEST_F(RedisTest, SYNC_SORTED_SET_COMMANDS_SCAN_AUTO) {
    std::string key = test_key("scan_auto");

    // Add members with their scores
    redis.zadd(key, {{1.0, "member1"},
                     {2.0, "member2"},
                     {3.0, "member3"},
                     {4.0, "member4"},
                     {5.0, "member5"}});

    // Test auto-scanning with callback
    bool scan_called = false;
    redis.zscan(
        [&](auto &&reply) {
            scan_called  = true;
            auto &result = reply.result();
            EXPECT_EQ(result.items.size(), 5);
            EXPECT_DOUBLE_EQ(result.items["member1"], 1.0);
            EXPECT_DOUBLE_EQ(result.items["member2"], 2.0);
            EXPECT_DOUBLE_EQ(result.items["member3"], 3.0);
            EXPECT_DOUBLE_EQ(result.items["member4"], 4.0);
            EXPECT_DOUBLE_EQ(result.items["member5"], 5.0);
        },
        key);

    redis.await();
    EXPECT_TRUE(scan_called);

    // Test auto-scanning with pattern
    scan_called = false;
    redis.zscan(
        [&](auto &&reply) {
            scan_called  = true;
            auto &result = reply.result();
            EXPECT_EQ(result.items.size(), 3);
            EXPECT_DOUBLE_EQ(result.items["member1"], 1.0);
            EXPECT_DOUBLE_EQ(result.items["member2"], 2.0);
            EXPECT_DOUBLE_EQ(result.items["member3"], 3.0);
        },
        key, "member[1-3]");

    redis.await();
    EXPECT_TRUE(scan_called);
}