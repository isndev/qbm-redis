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
#include <qb/io/async/coroutine.h>
#include <thread>
#include "../redis.h"
#include "protocol_test_common.h"

using namespace qb::io;
using namespace std::chrono;

// ============================================================================
// Fixture: all tests run in both RESP2 and RESP3
// ============================================================================

class SetProtocolModesTest : public ProtocolModesTestBase {};

INSTANTIATE_PROTOCOL_MODES(SetProtocolModesTest);

/*
 * COROUTINE TESTS
 */

// Test basic SADD, SCARD, SMEMBERS operations
TEST_P(SetProtocolModesTest, CORO_SET_COMMANDS_BASIC) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("basic");

        // SADD test
        auto add_reply1 = co_await redis.sadd(key, "member1");
        EXPECT_TRUE(add_reply1.ok());
        EXPECT_EQ(add_reply1.result(), 1);

        auto add_reply2 = co_await redis.sadd(key, "member2", "member3");
        EXPECT_TRUE(add_reply2.ok());
        EXPECT_EQ(add_reply2.result(), 2);

        // Duplicate add
        auto add_reply3 = co_await redis.sadd(key, "member1");
        EXPECT_TRUE(add_reply3.ok());
        EXPECT_EQ(add_reply3.result(), 0); // Already exists

        // SCARD test
        auto card_reply = co_await redis.scard(key);
        EXPECT_TRUE(card_reply.ok());
        EXPECT_EQ(card_reply.result(), 3);

        // SMEMBERS test
        auto members_reply = co_await redis.smembers(key);
        EXPECT_TRUE(members_reply.ok());
        EXPECT_EQ(members_reply.result().size(), 3);

        // Cleanup
        (void)co_await redis.del(key);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test SISMEMBER and SMISMEMBER
TEST_P(SetProtocolModesTest, CORO_SET_COMMANDS_ISMEMBER) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("ismember");

        // Setup set
        (void)co_await redis.sadd(key, "member1", "member2", "member3");

        // SISMEMBER test
        auto ismem1_reply = co_await redis.sismember(key, "member1");
        EXPECT_TRUE(ismem1_reply.ok());
        EXPECT_TRUE(ismem1_reply.result());

        auto ismem2_reply = co_await redis.sismember(key, "nonexistent");
        EXPECT_TRUE(ismem2_reply.ok());
        EXPECT_FALSE(ismem2_reply.result());

        // SMISMEMBER test
        auto mismem_reply = co_await redis.smismember(key, "member1", "member2", "nonexistent");
        EXPECT_TRUE(mismem_reply.ok());
        EXPECT_EQ(mismem_reply.result().size(), 3);
        EXPECT_TRUE(mismem_reply.result()[0]);
        EXPECT_TRUE(mismem_reply.result()[1]);
        EXPECT_FALSE(mismem_reply.result()[2]);

        // Cleanup
        (void)co_await redis.del(key);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test SREM operation
TEST_P(SetProtocolModesTest, CORO_SET_COMMANDS_REMOVE) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("remove");

        // Setup set
        (void)co_await redis.sadd(key, "member1", "member2", "member3", "member4");

        // SREM test
        auto rem_reply = co_await redis.srem(key, "member1", "member2");
        EXPECT_TRUE(rem_reply.ok());
        EXPECT_EQ(rem_reply.result(), 2);

        // Verify
        auto card_reply = co_await redis.scard(key);
        EXPECT_TRUE(card_reply.ok());
        EXPECT_EQ(card_reply.result(), 2);

        // Remove non-existent
        auto rem2_reply = co_await redis.srem(key, "nonexistent");
        EXPECT_TRUE(rem2_reply.ok());
        EXPECT_EQ(rem2_reply.result(), 0);

        // Cleanup
        (void)co_await redis.del(key);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test SPOP operation
TEST_P(SetProtocolModesTest, CORO_SET_COMMANDS_POP) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("pop");

        // Setup set
        (void)co_await redis.sadd(key, "member1", "member2", "member3", "member4", "member5");

        // SPOP single
        auto pop1_reply = co_await redis.spop(key);
        EXPECT_TRUE(pop1_reply.ok());
        EXPECT_TRUE(pop1_reply.result().has_value());

        auto card1_reply = co_await redis.scard(key);
        EXPECT_TRUE(card1_reply.ok());
        EXPECT_EQ(card1_reply.result(), 4);

        // SPOP multiple
        auto pop2_reply = co_await redis.spop(key, 2);
        EXPECT_TRUE(pop2_reply.ok());
        EXPECT_EQ(pop2_reply.result().size(), 2);

        auto card2_reply = co_await redis.scard(key);
        EXPECT_TRUE(card2_reply.ok());
        EXPECT_EQ(card2_reply.result(), 2);

        // Cleanup
        (void)co_await redis.del(key);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test SRANDMEMBER operation
TEST_P(SetProtocolModesTest, CORO_SET_COMMANDS_RANDMEMBER) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("randmember");

        // Setup set
        (void)co_await redis.sadd(key, "member1", "member2", "member3", "member4", "member5");

        // SRANDMEMBER single
        auto rand1_reply = co_await redis.srandmember(key);
        EXPECT_TRUE(rand1_reply.ok());
        EXPECT_TRUE(rand1_reply.result().has_value());

        // Verify set size unchanged
        auto card1_reply = co_await redis.scard(key);
        EXPECT_TRUE(card1_reply.ok());
        EXPECT_EQ(card1_reply.result(), 5);

        // SRANDMEMBER multiple
        auto rand2_reply = co_await redis.srandmember(key, 3);
        EXPECT_TRUE(rand2_reply.ok());
        EXPECT_EQ(rand2_reply.result().size(), 3);

        // Cleanup
        (void)co_await redis.del(key);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test SMOVE operation
TEST_P(SetProtocolModesTest, CORO_SET_COMMANDS_MOVE) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string source = protocol_key("source");
        std::string dest = protocol_key("dest");

        // Setup sets
        (void)co_await redis.sadd(source, "member1", "member2", "member3");
        (void)co_await redis.sadd(dest, "member4");

        // SMOVE test
        auto move1_reply = co_await redis.smove(source, dest, "member1");
        EXPECT_TRUE(move1_reply.ok());
        EXPECT_TRUE(move1_reply.result());

        // Verify
        auto source_card_reply = co_await redis.scard(source);
        auto dest_card_reply = co_await redis.scard(dest);
        EXPECT_TRUE(source_card_reply.ok());
        EXPECT_TRUE(dest_card_reply.ok());
        EXPECT_EQ(source_card_reply.result(), 2);
        EXPECT_EQ(dest_card_reply.result(), 2);

        // Move non-existent
        auto move2_reply = co_await redis.smove(source, dest, "nonexistent");
        EXPECT_TRUE(move2_reply.ok());
        EXPECT_FALSE(move2_reply.result());

        // Cleanup
        (void)co_await redis.del(source, dest);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test set operations: SDIFF, SINTER, SUNION
TEST_P(SetProtocolModesTest, CORO_SET_COMMANDS_OPERATIONS) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string set1 = protocol_key("set1");
        std::string set2 = protocol_key("set2");

        // Setup sets
        (void)co_await redis.sadd(set1, "a", "b", "c", "d");
        (void)co_await redis.sadd(set2, "b", "c", "e", "f");

        // SDIFF test
        auto diff_reply = co_await redis.sdiff({set1, set2});
        EXPECT_TRUE(diff_reply.ok());
        EXPECT_EQ(diff_reply.result().size(), 2); // a, d

        // SINTER test
        auto inter_reply = co_await redis.sinter({set1, set2});
        EXPECT_TRUE(inter_reply.ok());
        EXPECT_EQ(inter_reply.result().size(), 2); // b, c

        // SUNION test
        auto union_reply = co_await redis.sunion({set1, set2});
        EXPECT_TRUE(union_reply.ok());
        EXPECT_EQ(union_reply.result().size(), 6); // a, b, c, d, e, f

        // SINTERCARD test
        auto intercard_reply = co_await redis.sintercard({set1, set2});
        EXPECT_TRUE(intercard_reply.ok());
        EXPECT_EQ(intercard_reply.result(), 2);

        // Cleanup
        (void)co_await redis.del(set1, set2);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test set store operations: SDIFFSTORE, SINTERSTORE, SUNIONSTORE
TEST_P(SetProtocolModesTest, CORO_SET_COMMANDS_STORE) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string set1 = protocol_key("set1");
        std::string set2 = protocol_key("set2");
        std::string dest1 = protocol_key("dest1");
        std::string dest2 = protocol_key("dest2");
        std::string dest3 = protocol_key("dest3");

        // Setup sets
        (void)co_await redis.sadd(set1, "a", "b", "c", "d");
        (void)co_await redis.sadd(set2, "b", "c", "e", "f");

        // SDIFFSTORE test
        auto diffstore_reply = co_await redis.sdiffstore(dest1, {set1, set2});
        EXPECT_TRUE(diffstore_reply.ok());
        EXPECT_EQ(diffstore_reply.result(), 2); // a, d

        // SINTERSTORE test
        auto interstore_reply = co_await redis.sinterstore(dest2, {set1, set2});
        EXPECT_TRUE(interstore_reply.ok());
        EXPECT_EQ(interstore_reply.result(), 2); // b, c

        // SUNIONSTORE test
        auto unionstore_reply = co_await redis.sunionstore(dest3, {set1, set2});
        EXPECT_TRUE(unionstore_reply.ok());
        EXPECT_EQ(unionstore_reply.result(), 6); // a, b, c, d, e, f

        // Cleanup
        (void)co_await redis.del(set1, set2, dest1, dest2, dest3);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test SSCAN operation
TEST_P(SetProtocolModesTest, CORO_SET_COMMANDS_SCAN) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("scan");

        // Setup set with multiple members
        (void)co_await redis.sadd(key, "member1", "member2", "member3", "member4", "member5");

        // SSCAN test
        auto scan_reply = co_await redis.sscan(key, 0, "*", 10);
        EXPECT_TRUE(scan_reply.ok());
        EXPECT_EQ(scan_reply.result().items.size(), 5);

        // Cleanup
        (void)co_await redis.del(key);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test SINTERCARD with limit parameter
TEST_P(SetProtocolModesTest, CORO_SET_COMMANDS_SINTERCARD_WITH_LIMIT) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string set1 = protocol_key("sintercard_limit1");
        std::string set2 = protocol_key("sintercard_limit2");

        // set1: {a, b, c, d, e}, set2: {a, b, c, f, g}
        // intersection: {a, b, c}
        (void)co_await redis.sadd(set1, "a", "b", "c", "d", "e");
        (void)co_await redis.sadd(set2, "a", "b", "c", "f", "g");

        // Without limit: all 3 common members
        auto full_reply = co_await redis.sintercard({set1, set2});
        EXPECT_TRUE(full_reply.ok());
        EXPECT_EQ(full_reply.result(), 3);

        // With limit=2: stops counting at 2
        auto limited_reply = co_await redis.sintercard({set1, set2}, 2LL);
        EXPECT_TRUE(limited_reply.ok());
        EXPECT_EQ(limited_reply.result(), 2);

        // With limit=1: stops counting at 1
        auto one_reply = co_await redis.sintercard({set1, set2}, 1LL);
        EXPECT_TRUE(one_reply.ok());
        EXPECT_EQ(one_reply.result(), 1);

        // With limit greater than actual count: returns actual count
        auto big_limit = co_await redis.sintercard({set1, set2}, 100LL);
        EXPECT_TRUE(big_limit.ok());
        EXPECT_EQ(big_limit.result(), 3);

        (void)co_await redis.del(set1, set2);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

TEST_P(SetProtocolModesTest, SADD_SMEMBERS) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k = protocol_key("set");
        auto add_r = co_await redis.sadd(k, "x", "y", "z");
        EXPECT_TRUE(add_r.ok()) << add_r.error();
        if (add_r.ok()) EXPECT_EQ(add_r.result(), 3);
        auto members_r = co_await redis.smembers(k);
        EXPECT_TRUE(members_r.ok()) << members_r.error();
        if (members_r.ok()) EXPECT_EQ(members_r.result().size(), 3u);
        done = true;
    });
    while (!done) qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(SetProtocolModesTest, SISMEMBER_BOOLEAN) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k = protocol_key("sismember");
        (void)co_await redis.sadd(k, "a");
        auto r = co_await redis.sismember(k, "a");
        EXPECT_TRUE(r.ok()) << r.error();
        if (r.ok()) EXPECT_TRUE(r.result());
        auto r2 = co_await redis.sismember(k, "b");
        EXPECT_TRUE(r2.ok());
        if (r2.ok()) EXPECT_FALSE(r2.result());
        done = true;
    });
    while (!done) qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(SetProtocolModesTest, SINTER) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k1 = protocol_key("sinter1");
        auto k2 = protocol_key("sinter2");
        (void)co_await redis.sadd(k1, "a", "b", "c");
        (void)co_await redis.sadd(k2, "b", "c", "d");
        auto r = co_await redis.sinter({k1, k2});
        EXPECT_TRUE(r.ok()) << r.error();
        if (r.ok()) EXPECT_EQ(r.result().size(), 2u);
        done = true;
    });
    while (!done) qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(SetProtocolModesTest, SREM_INTEGER) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k = protocol_key("srem");
        (void)co_await redis.sadd(k, "a", "b", "c");
        auto r = co_await redis.srem(k, "a");
        EXPECT_TRUE(r.ok()) << r.error();
        if (r.ok()) EXPECT_EQ(r.result(), 1);
        done = true;
    });
    while (!done) qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(SetProtocolModesTest, SCARD_INTEGER) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k = protocol_key("scard");
        (void)co_await redis.sadd(k, "x", "y", "z");
        auto r = co_await redis.scard(k);
        EXPECT_TRUE(r.ok()) << r.error();
        if (r.ok()) EXPECT_EQ(r.result(), 3);
        done = true;
    });
    while (!done) qb::io::async::run(EVRUN_NOWAIT);
}

// Main function to run the tests
int
main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
