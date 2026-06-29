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
 * @file integration/hyperloglog/hyperloglog-commands.cpp
 * @brief Live RESP2/RESP3 integration tests for the qbm-redis HyperLogLog command mixin.
 *
 * Restructured from `test-hyperloglog-commands.cpp`:
 *   - the 2 smoke dups (PFADD_PFCOUNT, PFMERGE_STATUS) are deleted — subsumed by CORO_* bodies;
 *   - the dead `#define REDIS_URI` macro and the 3 vestigial "Test async …" comment stubs are removed;
 *   - busy-spins → shared `run_coro_test_until` watchdog;
 *   - new WRONGTYPE negative: PFCOUNT on a plain string key must fail.
 */

#include <gtest/gtest.h>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include "../../shared/redis_integration_fixture.h"
#include "../redis.h"

using namespace qb::io;
using namespace qb::redis::test;

class HyperLogLogProtocolModesTest : public ProtocolModesTestBase {};

INSTANTIATE_PROTOCOL_MODES(HyperLogLogProtocolModesTest);

// PFADD reports 1 when the estimated cardinality changes, 0 when only duplicates are added.
TEST_P(HyperLogLogProtocolModesTest, PFADD) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto key = protocol_key("pfadd");

        auto reply1 = co_await redis.pfadd(key, "element1");
        EXPECT_TRUE(reply1.ok()) << reply1.error();
        EXPECT_TRUE(reply1.result()); // new register set

        auto reply2 = co_await redis.pfadd(key, "element2", "element3", "element4");
        EXPECT_TRUE(reply2.ok()) << reply2.error();
        EXPECT_TRUE(reply2.result());

        // Re-adding only existing elements does not change the estimate → 0.
        auto reply3 = co_await redis.pfadd(key, "element1", "element2");
        EXPECT_TRUE(reply3.ok()) << reply3.error();
        EXPECT_FALSE(reply3.result());

        CO_IGNORE(co_await redis.del(key));
        completed = true;
    });
    run_coro_test_until(completed);
}

// PFCOUNT on a single key and the union count over two keys with one overlapping element.
TEST_P(HyperLogLogProtocolModesTest, PFCOUNT) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto key1 = protocol_key("pfcount1");
        auto key2 = protocol_key("pfcount2");

        EXPECT_TRUE((co_await redis.pfadd(key1, "element1", "element2", "element3")).ok());
        EXPECT_TRUE((co_await redis.pfadd(key2, "element3", "element4", "element5")).ok());

        auto reply1 = co_await redis.pfcount(key1);
        EXPECT_TRUE(reply1.ok()) << reply1.error();
        EXPECT_EQ(reply1.result(), 3);

        // Union: {1,2,3} ∪ {3,4,5} = 5 distinct.
        auto reply2 = co_await redis.pfcount(key1, key2);
        EXPECT_TRUE(reply2.ok()) << reply2.error();
        EXPECT_EQ(reply2.result(), 5);

        CO_IGNORE(co_await redis.del(key1, key2));
        completed = true;
    });
    run_coro_test_until(completed);
}

// PFMERGE folds two source HLLs into a destination; the merged count equals the union.
TEST_P(HyperLogLogProtocolModesTest, PFMERGE) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto key1    = protocol_key("pfmerge1");
        auto key2    = protocol_key("pfmerge2");
        auto destkey = protocol_key("pfmerge_dest");

        EXPECT_TRUE((co_await redis.pfadd(key1, "element1", "element2", "element3")).ok());
        EXPECT_TRUE((co_await redis.pfadd(key2, "element3", "element4", "element5")).ok());

        auto reply = co_await redis.pfmerge(destkey, key1, key2);
        EXPECT_TRUE(reply.ok()) << reply.error();
        EXPECT_TRUE(reply.result().ok());

        auto count_reply = co_await redis.pfcount(destkey);
        EXPECT_TRUE(count_reply.ok()) << count_reply.error();
        EXPECT_EQ(count_reply.result(), 5);

        CO_IGNORE(co_await redis.del(key1, key2, destkey));
        completed = true;
    });
    run_coro_test_until(completed);
}

// WRONGTYPE: PFCOUNT against a plain (non-HLL) string key must surface a server error.
TEST_P(HyperLogLogProtocolModesTest, PFCOUNT_WRONGTYPE) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto key = protocol_key("plain-not-hll");

        // A plain string value is not a valid HyperLogLog representation.
        EXPECT_TRUE((co_await redis.set(key, "just-a-string")).ok());

        auto reply = co_await redis.pfcount(key);
        EXPECT_FALSE(reply.ok());
        EXPECT_FALSE(reply.error().empty());

        CO_IGNORE(co_await redis.del(key));
        completed = true;
    });
    run_coro_test_until(completed);
}
