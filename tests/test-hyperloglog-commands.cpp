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
#include <thread>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include "../redis.h"
#include "protocol_test_common.h"

// Redis Configuration
#define REDIS_URI {"tcp://localhost:6379"}

using namespace qb::io;
using namespace std::chrono;

// ============================================================================
// Fixture: all tests run in both RESP2 and RESP3
// ============================================================================

class HyperLogLogProtocolModesTest : public ProtocolModesTestBase {};

INSTANTIATE_PROTOCOL_MODES(HyperLogLogProtocolModesTest);

/*
 * COROUTINE TESTS
 */

// Test PFADD with coroutines
TEST_P(HyperLogLogProtocolModesTest, CORO_HYPERLOGLOG_PFADD) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto key = protocol_key("pfadd");

        // Test adding single element
        auto reply1 = co_await redis.pfadd(key, "element1");
        EXPECT_TRUE(reply1.ok());
        EXPECT_TRUE(reply1.result());

        // Test adding multiple elements
        auto reply2 = co_await redis.pfadd(key, "element2", "element3", "element4");
        EXPECT_TRUE(reply2.ok());
        EXPECT_TRUE(reply2.result());

        // Test adding duplicate elements (should not affect cardinality)
        auto reply3 = co_await redis.pfadd(key, "element1", "element2");
        EXPECT_TRUE(reply3.ok());
        EXPECT_FALSE(reply3.result());

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test PFCOUNT with coroutines
TEST_P(HyperLogLogProtocolModesTest, CORO_HYPERLOGLOG_PFCOUNT) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto key1 = protocol_key("pfcount1");
        auto key2 = protocol_key("pfcount2");

        // Add elements to first HyperLogLog
        (void) co_await redis.pfadd(key1, "element1", "element2", "element3");

        // Add elements to second HyperLogLog
        (void) co_await redis.pfadd(key2, "element3", "element4", "element5");

        // Test counting single HyperLogLog
        auto reply1 = co_await redis.pfcount(key1);
        EXPECT_TRUE(reply1.ok());
        EXPECT_EQ(reply1.result(), 3);

        // Test counting multiple HyperLogLogs (union)
        auto reply2 = co_await redis.pfcount(key1, key2);
        EXPECT_TRUE(reply2.ok());
        EXPECT_EQ(reply2.result(), 5);

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test PFMERGE with coroutines
TEST_P(HyperLogLogProtocolModesTest, CORO_HYPERLOGLOG_PFMERGE) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto key1    = protocol_key("pfmerge1");
        auto key2    = protocol_key("pfmerge2");
        auto destkey = protocol_key("pfmerge_dest");

        // Add elements to source HyperLogLogs
        (void) co_await redis.pfadd(key1, "element1", "element2", "element3");
        (void) co_await redis.pfadd(key2, "element3", "element4", "element5");

        // Merge HyperLogLogs
        auto reply = co_await redis.pfmerge(destkey, key1, key2);
        EXPECT_TRUE(reply.ok());
        EXPECT_TRUE(reply.result().ok());

        // Verify merged result
        auto count_reply = co_await redis.pfcount(destkey);
        EXPECT_TRUE(count_reply.ok());
        EXPECT_EQ(count_reply.result(), 5);

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

TEST_P(HyperLogLogProtocolModesTest, PFADD_PFCOUNT) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(done);
        auto k = protocol_key("hll");
        (void) co_await redis.pfadd(k, "a", "b", "c");
        auto count_r = co_await redis.pfcount(k);
        EXPECT_TRUE(count_r.ok()) << count_r.error();
        if (count_r.ok()) {
            EXPECT_EQ(count_r.result(), 3);
        }
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(HyperLogLogProtocolModesTest, PFMERGE_STATUS) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(done);
        auto k1   = protocol_key("pfmerge1");
        auto k2   = protocol_key("pfmerge2");
        auto dest = protocol_key("pfmerge_dest");
        (void) co_await redis.pfadd(k1, "a", "b");
        (void) co_await redis.pfadd(k2, "c", "d");
        auto r = co_await redis.pfmerge(dest, k1, k2);
        EXPECT_TRUE(r.ok()) << r.error();
        if (r.ok()) {
            EXPECT_TRUE(r.result().ok());
        }
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

// Test async PFADD
// Test async PFCOUNT
// Test async PFMERGE
// Main function to run the tests
int
main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
