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
    std::string prefix  = "qb::redis::set-test:" + std::to_string(++counter);

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
            throw std::runtime_error("Unable to connect to Redis");

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

// Test SADD/SCARD
TEST_F(RedisTest, SYNC_SET_COMMANDS_SADD_SCARD) {
    std::string key = test_key("sadd_scard");

    // Ajouter des membres
    EXPECT_EQ(redis.sadd(key, "member1", "member2", "member3"), 3);

    // Verify the number of members
    EXPECT_EQ(redis.scard(key), 3);

    // Ajouter des membres existants
    EXPECT_EQ(redis.sadd(key, "member1", "member2"), 0);

    // Verify that the number hasn't changed
    EXPECT_EQ(redis.scard(key), 3);
}

// Test SDIFF/SDIFFSTORE
TEST_F(RedisTest, SYNC_SET_COMMANDS_SDIFF_SDIFFSTORE) {
    std::string key1 = test_key("sdiff1");
    std::string key2 = test_key("sdiff2");
    std::string dest = test_key("sdiff_dest");

    // Create the sets
    redis.sadd(key1, "a", "b", "c");
    redis.sadd(key2, "c", "d", "e");

    // Calculate the difference
    auto diff = redis.sdiff({key1, key2});
    EXPECT_EQ(diff.size(), 2);
    EXPECT_TRUE(std::find(diff.begin(), diff.end(), "a") != diff.end());
    EXPECT_TRUE(std::find(diff.begin(), diff.end(), "b") != diff.end());

    // Store the difference
    EXPECT_EQ(redis.sdiffstore(dest, {key1, key2}), 2);

    // Verify the stored result
    auto stored = redis.smembers(dest);
    EXPECT_EQ(stored.size(), 2);
    EXPECT_TRUE(stored.find("a") != stored.end());
    EXPECT_TRUE(stored.find("b") != stored.end());
}

// Test SINTER/SINTERSTORE/SINTERCARD
TEST_F(RedisTest, SYNC_SET_COMMANDS_SINTER) {
    std::string key1 = test_key("sinter1");
    std::string key2 = test_key("sinter2");
    std::string dest = test_key("sinter_dest");

    // Create the sets
    redis.sadd(key1, "a", "b", "c");
    redis.sadd(key2, "b", "c", "d");

    // Calculate the intersection
    auto inter = redis.sinter({key1, key2});
    EXPECT_EQ(inter.size(), 2);
    EXPECT_TRUE(std::find(inter.begin(), inter.end(), "b") != inter.end());
    EXPECT_TRUE(std::find(inter.begin(), inter.end(), "c") != inter.end());

    // Store the intersection
    EXPECT_EQ(redis.sinterstore(dest, {key1, key2}), 2);

    // Verify the stored result
    auto stored = redis.smembers(dest);
    EXPECT_EQ(stored.size(), 2);
    EXPECT_TRUE(stored.find("b") != stored.end());
    EXPECT_TRUE(stored.find("c") != stored.end());

    // Verify the cardinality of the intersection
    EXPECT_EQ(redis.sintercard({key1, key2}), 2);
    EXPECT_EQ(redis.sintercard({key1, key2}, 1), 1);
}

// Test SISMEMBER/SMISMEMBER
TEST_F(RedisTest, SYNC_SET_COMMANDS_SISMEMBER) {
    std::string key = test_key("sismember");

    // Ajouter des membres
    redis.sadd(key, "member1", "member2", "member3");

    // Verify membership
    EXPECT_TRUE(redis.sismember(key, "member1"));
    EXPECT_FALSE(redis.sismember(key, "member4"));

    // Verify multiple membership
    auto results = redis.smismember(key, "member1", "member2", "member4");
    EXPECT_EQ(results.size(), 3);
    EXPECT_TRUE(results[0]);
    EXPECT_TRUE(results[1]);
    EXPECT_FALSE(results[2]);
}

// Test SMEMBERS
TEST_F(RedisTest, SYNC_SET_COMMANDS_SMEMBERS) {
    std::string key = test_key("smembers");

    // Ajouter des membres
    redis.sadd(key, "member1", "member2", "member3");

    // Retrieve all members
    auto members = redis.smembers(key);
    EXPECT_EQ(members.size(), 3);
    EXPECT_TRUE(members.find("member1") != members.end());
    EXPECT_TRUE(members.find("member2") != members.end());
    EXPECT_TRUE(members.find("member3") != members.end());
}

// Test SMOVE
TEST_F(RedisTest, SYNC_SET_COMMANDS_SMOVE) {
    std::string source = test_key("smove_source");
    std::string dest   = test_key("smove_dest");

    // Add a member to the source
    redis.sadd(source, "member1");

    // Move the member
    EXPECT_TRUE(redis.smove(source, dest, "member1"));

    // Verify that the member has been moved
    EXPECT_FALSE(redis.sismember(source, "member1"));
    EXPECT_TRUE(redis.sismember(dest, "member1"));
}

// Test SPOP
TEST_F(RedisTest, SYNC_SET_COMMANDS_SPOP) {
    std::string key = test_key("spop");

    // Ajouter des membres
    redis.sadd(key, "member1", "member2", "member3");

    // Pop a member
    auto popped = redis.spop(key);
    EXPECT_TRUE(popped.has_value());
    EXPECT_EQ(redis.scard(key), 2);

    // Pop multiple members
    auto popped_many = redis.spop(key, 2);
    EXPECT_EQ(popped_many.size(), 2);
    EXPECT_EQ(redis.scard(key), 0);
}

// Test SRANDMEMBER
TEST_F(RedisTest, SYNC_SET_COMMANDS_SRANDMEMBER) {
    std::string key = test_key("srandmember");

    // Ajouter des membres
    redis.sadd(key, "member1", "member2", "member3");

    // Get a random member
    auto member = redis.srandmember(key);
    EXPECT_TRUE(member.has_value());

    // Get multiple random members
    auto members = redis.srandmember(key, 2);
    EXPECT_EQ(members.size(), 2);
}

// Test SREM
TEST_F(RedisTest, SYNC_SET_COMMANDS_SREM) {
    std::string key = test_key("srem");

    // Ajouter des membres
    redis.sadd(key, "member1", "member2", "member3");

    // Verify that the members have been removed
    EXPECT_EQ(redis.srem(key, "member1", "member2"), 2);

    EXPECT_EQ(redis.scard(key), 1);
    EXPECT_TRUE(redis.sismember(key, "member3"));
}

// Test SSCAN
TEST_F(RedisTest, SYNC_SET_COMMANDS_SSCAN) {
    std::string key = test_key("sscan");

    // Ajouter des membres
    redis.sadd(key, "member1", "member2", "member3", "member4", "member5");

    // Scan members
    std::unordered_set<std::string> res;
    long long                       cursor = 0;
    while (true) {
        auto scan = redis.sscan(key, cursor, "member*", 2);
        cursor    = scan.cursor;
        std::move(scan.items.begin(), scan.items.end(), std::inserter(res, res.end()));
        if (cursor == 0) {
            break;
        }
    }
    EXPECT_EQ(res.size(), 5);
    EXPECT_TRUE(res.find("member1") != res.end());
    EXPECT_TRUE(res.find("member2") != res.end());
    EXPECT_TRUE(res.find("member3") != res.end());
    EXPECT_TRUE(res.find("member4") != res.end());
    EXPECT_TRUE(res.find("member5") != res.end());

    // Scan all members
    res.clear();
    cursor = 0;
    while (true) {
        auto scan = redis.sscan(key, cursor, "*", 2);
        cursor    = scan.cursor;
        std::move(scan.items.begin(), scan.items.end(), std::inserter(res, res.end()));
        if (cursor == 0) {
            break;
        }
    }
    EXPECT_EQ(res.size(), 5);
}

// Test SUNION/SUNIONSTORE
TEST_F(RedisTest, SYNC_SET_COMMANDS_SUNION) {
    std::string key1 = test_key("sunion1");
    std::string key2 = test_key("sunion2");
    std::string dest = test_key("sunion_dest");

    // Create the sets
    redis.sadd(key1, "a", "b", "c");
    redis.sadd(key2, "c", "d", "e");

    // Calculate the union
    auto union_result = redis.sunion({key1, key2});
    EXPECT_EQ(union_result.size(), 5);
    EXPECT_TRUE(std::find(union_result.begin(), union_result.end(), "a") !=
                union_result.end());
    EXPECT_TRUE(std::find(union_result.begin(), union_result.end(), "b") !=
                union_result.end());
    EXPECT_TRUE(std::find(union_result.begin(), union_result.end(), "c") !=
                union_result.end());
    EXPECT_TRUE(std::find(union_result.begin(), union_result.end(), "d") !=
                union_result.end());
    EXPECT_TRUE(std::find(union_result.begin(), union_result.end(), "e") !=
                union_result.end());

    // Store the union
    EXPECT_EQ(redis.sunionstore(dest, {key1, key2}), 5);

    // Verify the stored result
    auto stored = redis.smembers(dest);
    EXPECT_EQ(stored.size(), 5);
    EXPECT_TRUE(stored.find("a") != stored.end());
    EXPECT_TRUE(stored.find("b") != stored.end());
    EXPECT_TRUE(stored.find("c") != stored.end());
    EXPECT_TRUE(stored.find("d") != stored.end());
    EXPECT_TRUE(stored.find("e") != stored.end());
}

/*
 * TESTS ASYNCHRONES
 */

// Test asynchrone SADD/SCARD
TEST_F(RedisTest, ASYNC_SET_COMMANDS_SADD_SCARD) {
    std::string key          = test_key("async_sadd_scard");
    long long   sadd_result  = 0;
    long long   scard_result = 0;

    // Add members asynchronously
    redis.sadd([&](auto &&reply) { sadd_result = reply.result(); }, key, "member1",
               "member2", "member3");

    redis.await();
    EXPECT_EQ(sadd_result, 3);

    // Verify the number of members asynchronously
    redis.scard([&](auto &&reply) { scard_result = reply.result(); }, key);

    redis.await();
    EXPECT_EQ(scard_result, 3);
}

// Test asynchrone SDIFF/SDIFFSTORE
TEST_F(RedisTest, ASYNC_SET_COMMANDS_SDIFF_SDIFFSTORE) {
    std::string              key1 = test_key("async_sdiff1");
    std::string              key2 = test_key("async_sdiff2");
    std::string              dest = test_key("async_sdiff_dest");
    std::vector<std::string> diff_result;
    long long                diffstore_result = 0;

    // Create the sets
    redis.sadd(key1, "a", "b", "c");
    redis.sadd(key2, "c", "d", "e");

    // Calculate the difference asynchronously
    redis.sdiff([&](auto &&reply) { diff_result = reply.result(); }, {key1, key2});

    redis.await();
    EXPECT_EQ(diff_result.size(), 2);
    EXPECT_TRUE(std::find(diff_result.begin(), diff_result.end(), "a") !=
                diff_result.end());
    EXPECT_TRUE(std::find(diff_result.begin(), diff_result.end(), "b") !=
                diff_result.end());

    // Store the difference asynchronously
    redis.sdiffstore([&](auto &&reply) { diffstore_result = reply.result(); }, dest,
                     {key1, key2});

    redis.await();
    EXPECT_EQ(diffstore_result, 2);
}

// Test asynchrone SINTER/SINTERSTORE/SINTERCARD
TEST_F(RedisTest, ASYNC_SET_COMMANDS_SINTER) {
    std::string              key1 = test_key("async_sinter1");
    std::string              key2 = test_key("async_sinter2");
    std::string              dest = test_key("async_sinter_dest");
    std::vector<std::string> inter_result;
    long long                interstore_result = 0;
    long long                intercard_result  = 0;

    // Create the sets
    redis.sadd(key1, "a", "b", "c");
    redis.sadd(key2, "b", "c", "d");

    // Calculate the intersection asynchronously
    redis.sinter([&](auto &&reply) { inter_result = reply.result(); }, {key1, key2});

    redis.await();
    EXPECT_EQ(inter_result.size(), 2);
    EXPECT_TRUE(std::find(inter_result.begin(), inter_result.end(), "b") !=
                inter_result.end());
    EXPECT_TRUE(std::find(inter_result.begin(), inter_result.end(), "c") !=
                inter_result.end());

    // Store the intersection asynchronously
    redis.sinterstore([&](auto &&reply) { interstore_result = reply.result(); }, dest,
                      {key1, key2});

    redis.await();
    EXPECT_EQ(interstore_result, 2);

    // Verify the cardinality of the intersection asynchronously
    redis.sintercard([&](auto &&reply) { intercard_result = reply.result(); },
                     {key1, key2});

    redis.await();
    EXPECT_EQ(intercard_result, 2);
}

// Test asynchrone SISMEMBER/SMISMEMBER
TEST_F(RedisTest, ASYNC_SET_COMMANDS_SISMEMBER) {
    std::string       key             = test_key("async_sismember");
    bool              ismember_result = false;
    std::vector<bool> mismember_result;

    // Ajouter des membres
    redis.sadd(key, "member1", "member2", "member3");

    // Verify membership asynchronously
    redis.sismember([&](auto &&reply) { ismember_result = reply.ok(); }, key, "member1");

    redis.await();
    EXPECT_TRUE(ismember_result);

    // Verify multiple membership asynchronously
    redis.smismember([&](auto &&reply) { mismember_result = reply.result(); }, key,
                     "member1", "member2", "member4");

    redis.await();
    EXPECT_EQ(mismember_result.size(), 3);
    EXPECT_TRUE(mismember_result[0]);
    EXPECT_TRUE(mismember_result[1]);
    EXPECT_FALSE(mismember_result[2]);
}

// Test asynchrone SMEMBERS
TEST_F(RedisTest, ASYNC_SET_COMMANDS_SMEMBERS) {
    std::string                    key = test_key("async_smembers");
    qb::unordered_set<std::string> members_result;

    // Ajouter des membres
    redis.sadd(key, "member1", "member2", "member3");

    // Retrieve all members asynchronously
    redis.smembers([&](auto &&reply) { members_result = reply.result(); }, key);

    redis.await();
    EXPECT_EQ(members_result.size(), 3);
    EXPECT_TRUE(members_result.find("member1") != members_result.end());
    EXPECT_TRUE(members_result.find("member2") != members_result.end());
    EXPECT_TRUE(members_result.find("member3") != members_result.end());
}

// Test asynchrone SMOVE
TEST_F(RedisTest, ASYNC_SET_COMMANDS_SMOVE) {
    std::string source       = test_key("async_smove_source");
    std::string dest         = test_key("async_smove_dest");
    bool        smove_result = false;

    // Add a member to the source
    redis.sadd(source, "member1");

    // Move the member asynchronously
    redis.smove([&](auto &&reply) { smove_result = reply.ok(); }, source, dest,
                "member1");

    redis.await();
    EXPECT_TRUE(smove_result);
}

// Test asynchrone SPOP
TEST_F(RedisTest, ASYNC_SET_COMMANDS_SPOP) {
    std::string                key = test_key("async_spop");
    std::optional<std::string> pop_result;
    std::vector<std::string>   pop_many_result;

    // Ajouter des membres
    redis.sadd(key, "member1", "member2", "member3");

    // Pop a member asynchronously
    redis.spop([&](auto &&reply) { pop_result = reply.result(); }, key);

    redis.await();
    EXPECT_TRUE(pop_result.has_value());

    // Pop multiple members asynchronously
    redis.spop([&](auto &&reply) { pop_many_result = reply.result(); }, key, 2);

    redis.await();
    EXPECT_EQ(pop_many_result.size(), 2);
}

// Test asynchrone SRANDMEMBER
TEST_F(RedisTest, ASYNC_SET_COMMANDS_SRANDMEMBER) {
    std::string                key = test_key("async_srandmember");
    std::optional<std::string> rand_result;
    std::vector<std::string>   rand_many_result;

    // Ajouter des membres
    redis.sadd(key, "member1", "member2", "member3");

    // Get a random member asynchronously
    redis.srandmember([&](auto &&reply) { rand_result = reply.result(); }, key);

    redis.await();
    EXPECT_TRUE(rand_result.has_value());

    // Get multiple random members asynchronously
    redis.srandmember([&](auto &&reply) { rand_many_result = reply.result(); }, key, 2);

    redis.await();
    EXPECT_EQ(rand_many_result.size(), 2);
}

// Test asynchrone SREM
TEST_F(RedisTest, ASYNC_SET_COMMANDS_SREM) {
    std::string key         = test_key("async_srem");
    long long   srem_result = 0;

    // Ajouter des membres
    redis.sadd(key, "member1", "member2", "member3");

    // Remove members asynchronously
    redis.srem([&](auto &&reply) { srem_result = reply.result(); }, key, "member1",
               "member2");

    redis.await();
    EXPECT_EQ(srem_result, 2);
}

// Test asynchrone SSCAN
TEST_F(RedisTest, ASYNC_SET_COMMANDS_SSCAN) {
    std::string                     key = test_key("async_sscan");
    std::unordered_set<std::string> scan_result;
    bool                            scan_completed = false;

    // Ajouter des membres
    redis.sadd(key, "member1", "member2", "member3", "member4", "member5");

    // Scan members asynchronously
    redis.sscan(
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

// Test asynchrone SUNION/SUNIONSTORE
TEST_F(RedisTest, ASYNC_SET_COMMANDS_SUNION) {
    std::string              key1 = test_key("async_sunion1");
    std::string              key2 = test_key("async_sunion2");
    std::string              dest = test_key("async_sunion_dest");
    std::vector<std::string> union_result;
    long long                unionstore_result = 0;

    // Create the sets
    redis.sadd(key1, "a", "b", "c");
    redis.sadd(key2, "c", "d", "e");

    // Calculate the union asynchronously
    redis.sunion([&](auto &&reply) { union_result = reply.result(); }, {key1, key2});

    redis.await();
    EXPECT_EQ(union_result.size(), 5);
    EXPECT_TRUE(std::find(union_result.begin(), union_result.end(), "a") !=
                union_result.end());
    EXPECT_TRUE(std::find(union_result.begin(), union_result.end(), "b") !=
                union_result.end());
    EXPECT_TRUE(std::find(union_result.begin(), union_result.end(), "c") !=
                union_result.end());
    EXPECT_TRUE(std::find(union_result.begin(), union_result.end(), "d") !=
                union_result.end());
    EXPECT_TRUE(std::find(union_result.begin(), union_result.end(), "e") !=
                union_result.end());

    // Store the union asynchronously
    redis.sunionstore([&](auto &&reply) { unionstore_result = reply.result(); }, dest,
                      {key1, key2});

    redis.await();
    EXPECT_EQ(unionstore_result, 5);
}