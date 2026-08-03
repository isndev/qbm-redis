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
 * @file integration/hash/hash-commands.cpp
 * @brief Live-redis integration tests for the hash command family (HSET/HGET/HDEL/HEXISTS,
 *        HMSET/HMGET/HGETALL, HINCRBY/HINCRBYFLOAT, HKEYS/HVALS, HSETNX, HSTRLEN, HSCAN),
 *        exercised in both RESP2 and RESP3.
 *
 * Restructured from the legacy `test-hash-commands.cpp`:
 *  - deleted 5 legacy smoke dups (HSET_HGET / HGETALL_MAP / HMGET_HEXISTS / HINCRBY_INTEGER /
 *    HDEL_INTEGER) — strict subsets of the CORO_* bodies kept here;
 *  - removed the file-local `main()` (links the shared gtest-main);
 *  - HSCAN strengthened to assert the actual field/value payload, iterate the cursor to
 *    completion, exercise a MATCH pattern, and walk a large hash across multiple cursor steps;
 *  - ADDED HINCRBY / HINCRBYFLOAT non-numeric-field error cases.
 */

#include <gtest/gtest.h>
#include <set>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include "../../shared/redis_integration_fixture.h"
#include <qbm/redis/redis.h>

using namespace qb::redis::test;

namespace {

class HashProtocolModesTest : public ProtocolModesTestBase {};

INSTANTIATE_PROTOCOL_MODES(HashProtocolModesTest);

// HSET / HGET / HEXISTS / HLEN / HDEL — full single-field lifecycle.
TEST_P(HashProtocolModesTest, HSET_HGET_LIFECYCLE) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("basic");

        auto s1 = co_await redis.hset(key, "field1", "value1");
        EXPECT_TRUE(s1.ok()) << s1.error();
        EXPECT_EQ(s1.result(), 1);
        auto s2 = co_await redis.hset(key, "field2", "value2");
        EXPECT_TRUE(s2.ok());
        EXPECT_EQ(s2.result(), 1);

        auto g1 = co_await redis.hget(key, "field1");
        if (!(g1.ok() && g1.result().has_value())) {
            ADD_FAILURE() << "precondition failed: g1.ok() && g1.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*g1.result(), "value1");
        auto g2 = co_await redis.hget(key, "field2");
        if (!(g2.ok() && g2.result().has_value())) {
            ADD_FAILURE() << "precondition failed: g2.ok() && g2.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*g2.result(), "value2");
        auto g3 = co_await redis.hget(key, "field3");
        EXPECT_TRUE(g3.ok());
        EXPECT_FALSE(g3.result().has_value());

        auto e1 = co_await redis.hexists(key, "field1");
        EXPECT_TRUE(e1.ok());
        EXPECT_TRUE(e1.result());
        auto e2 = co_await redis.hexists(key, "field3");
        EXPECT_TRUE(e2.ok());
        EXPECT_FALSE(e2.result());

        auto len = co_await redis.hlen(key);
        EXPECT_TRUE(len.ok());
        EXPECT_EQ(len.result(), 2);

        auto del = co_await redis.hdel(key, "field1");
        EXPECT_TRUE(del.ok());
        EXPECT_EQ(del.result(), 1);

        auto after = co_await redis.hget(key, "field1");
        EXPECT_TRUE(after.ok());
        EXPECT_FALSE(after.result().has_value());

        auto len2 = co_await redis.hlen(key);
        EXPECT_TRUE(len2.ok());
        EXPECT_EQ(len2.result(), 1);

        completed = true;
    });
    run_coro_test_until(completed);
}

// HMSET / HMGET / HGETALL.
TEST_P(HashProtocolModesTest, HMSET_HMGET_HGETALL) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("hmset");

        auto hmset = co_await redis.hmset(key, "field1", "value1", "field2", "value2", "field3", "value3");
        EXPECT_TRUE(hmset.ok()) << hmset.error();
        EXPECT_TRUE(hmset.result().ok());

        auto hmget = co_await redis.hmget(key, "field1", "field2", "field3", "field4");
        EXPECT_TRUE(hmget.ok());
        if (!(hmget.result().size() == 4u)) {
            ADD_FAILURE() << "precondition failed: hmget.result().size() == 4u";
            co_return;
        }
        if (!(hmget.result()[0].has_value())) {
            ADD_FAILURE() << "precondition failed: hmget.result()[0].has_value()";
            co_return;
        }
        EXPECT_EQ(*hmget.result()[0], "value1");
        if (!(hmget.result()[1].has_value())) {
            ADD_FAILURE() << "precondition failed: hmget.result()[1].has_value()";
            co_return;
        }
        EXPECT_EQ(*hmget.result()[1], "value2");
        if (!(hmget.result()[2].has_value())) {
            ADD_FAILURE() << "precondition failed: hmget.result()[2].has_value()";
            co_return;
        }
        EXPECT_EQ(*hmget.result()[2], "value3");
        EXPECT_FALSE(hmget.result()[3].has_value());

        auto all = co_await redis.hgetall(key);
        EXPECT_TRUE(all.ok());
        if (!(all.result().size() == 3u)) {
            ADD_FAILURE() << "precondition failed: all.result().size() == 3u";
            co_return;
        }
        EXPECT_EQ(all.result().at("field1"), "value1");
        EXPECT_EQ(all.result().at("field2"), "value2");
        EXPECT_EQ(all.result().at("field3"), "value3");

        completed = true;
    });
    run_coro_test_until(completed);
}

// HINCRBY / HINCRBYFLOAT happy paths plus value read-back.
TEST_P(HashProtocolModesTest, INCR) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("incr");

        auto i1 = co_await redis.hincrby(key, "counter", 1);
        EXPECT_TRUE(i1.ok()) << i1.error();
        EXPECT_EQ(i1.result(), 1);
        auto i2 = co_await redis.hincrby(key, "counter", 10);
        EXPECT_TRUE(i2.ok());
        EXPECT_EQ(i2.result(), 11);
        auto i3 = co_await redis.hincrby(key, "counter", -5);
        EXPECT_TRUE(i3.ok());
        EXPECT_EQ(i3.result(), 6);

        auto f1 = co_await redis.hincrbyfloat(key, "float", 10.5);
        EXPECT_TRUE(f1.ok());
        EXPECT_FLOAT_EQ(f1.result(), 10.5);
        auto f2 = co_await redis.hincrbyfloat(key, "float", 0.5);
        EXPECT_TRUE(f2.ok());
        EXPECT_FLOAT_EQ(f2.result(), 11.0);
        auto f3 = co_await redis.hincrbyfloat(key, "float", -1.5);
        EXPECT_TRUE(f3.ok());
        EXPECT_FLOAT_EQ(f3.result(), 9.5);

        auto counter = co_await redis.hget(key, "counter");
        if (!(counter.ok() && counter.result().has_value())) {
            ADD_FAILURE() << "precondition failed: counter.ok() && counter.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*counter.result(), "6");
        auto fl = co_await redis.hget(key, "float");
        if (!(fl.ok() && fl.result().has_value())) {
            ADD_FAILURE() << "precondition failed: fl.ok() && fl.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*fl.result(), "9.5");

        completed = true;
    });
    run_coro_test_until(completed);
}

// HINCRBY / HINCRBYFLOAT against a non-numeric field → error reply (not silent zero).
TEST_P(HashProtocolModesTest, INCR_NON_NUMERIC_ERRORS) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("incr_err");

        auto seed = co_await redis.hset(key, "text", "not-a-number");
        EXPECT_TRUE(seed.ok()) << seed.error();

        auto bad_int = co_await redis.hincrby(key, "text", 1);
        EXPECT_FALSE(bad_int.ok());
        EXPECT_FALSE(bad_int.error().empty());

        auto bad_float = co_await redis.hincrbyfloat(key, "text", 1.0);
        EXPECT_FALSE(bad_float.ok());
        EXPECT_FALSE(bad_float.error().empty());

        // Field value must be untouched after the failed increments.
        auto unchanged = co_await redis.hget(key, "text");
        if (!(unchanged.ok() && unchanged.result().has_value())) {
            ADD_FAILURE() << "precondition failed: unchanged.ok() && unchanged.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*unchanged.result(), "not-a-number");

        completed = true;
    });
    run_coro_test_until(completed);
}

// HSETNX — sets only when the field is absent.
TEST_P(HashProtocolModesTest, HSETNX) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("hsetnx");

        auto first = co_await redis.hsetnx(key, "field1", "value1");
        EXPECT_TRUE(first.ok()) << first.error();
        EXPECT_TRUE(first.result());

        auto v1 = co_await redis.hget(key, "field1");
        if (!(v1.ok() && v1.result().has_value())) {
            ADD_FAILURE() << "precondition failed: v1.ok() && v1.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*v1.result(), "value1");

        auto second = co_await redis.hsetnx(key, "field1", "new-value");
        EXPECT_TRUE(second.ok());
        EXPECT_FALSE(second.result());

        auto v2 = co_await redis.hget(key, "field1");
        if (!(v2.ok() && v2.result().has_value())) {
            ADD_FAILURE() << "precondition failed: v2.ok() && v2.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*v2.result(), "value1"); // unchanged

        completed = true;
    });
    run_coro_test_until(completed);
}

// HKEYS / HVALS — full key/value sets (order-independent).
TEST_P(HashProtocolModesTest, KEYS_VALUES) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("keys-values");

        auto seed = co_await redis.hmset(key, "field1", "value1", "field2", "value2", "field3", "value3");
        EXPECT_TRUE(seed.ok()) << seed.error();

        auto keys = co_await redis.hkeys(key);
        EXPECT_TRUE(keys.ok());
        std::set<std::string> key_set(keys.result().begin(), keys.result().end());
        EXPECT_EQ(key_set, (std::set<std::string>{"field1", "field2", "field3"}));

        auto vals = co_await redis.hvals(key);
        EXPECT_TRUE(vals.ok());
        std::set<std::string> val_set(vals.result().begin(), vals.result().end());
        EXPECT_EQ(val_set, (std::set<std::string>{"value1", "value2", "value3"}));

        completed = true;
    });
    run_coro_test_until(completed);
}

// HSTRLEN — byte length of field values, 0 for absent.
TEST_P(HashProtocolModesTest, STRLEN) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("strlen");

        auto seed = co_await redis.hmset(key, "field1", "hello", "field2", "world!");
        EXPECT_TRUE(seed.ok()) << seed.error();

        auto l1 = co_await redis.hstrlen(key, "field1");
        EXPECT_TRUE(l1.ok());
        EXPECT_EQ(l1.result(), 5);
        auto l2 = co_await redis.hstrlen(key, "field2");
        EXPECT_TRUE(l2.ok());
        EXPECT_EQ(l2.result(), 6);
        auto l3 = co_await redis.hstrlen(key, "nonexistent");
        EXPECT_TRUE(l3.ok());
        EXPECT_EQ(l3.result(), 0);

        completed = true;
    });
    run_coro_test_until(completed);
}

// HSCAN — small hash: a single step returns cursor 0 (complete) with every field/value.
TEST_P(HashProtocolModesTest, SCAN_ASSERTS_FIELDS_AND_VALUES) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("scan");

        auto seed = co_await redis.hmset(key, "field1", "value1", "field2", "value2", "field3", "value3");
        EXPECT_TRUE(seed.ok()) << seed.error();

        auto scan = co_await redis.hscan(key, 0, "*", 100);
        EXPECT_TRUE(scan.ok()) << scan.error();
        EXPECT_EQ(scan.result().cursor, 0u); // complete in one step for a tiny hash
        const auto &items = scan.result().items;
        if (!(items.size() == 3u)) {
            ADD_FAILURE() << "precondition failed: items.size() == 3u";
            co_return;
        }
        EXPECT_EQ(items.at("field1"), "value1");
        EXPECT_EQ(items.at("field2"), "value2");
        EXPECT_EQ(items.at("field3"), "value3");

        completed = true;
    });
    run_coro_test_until(completed);
}

// HSCAN MATCH — only matching fields are returned.
TEST_P(HashProtocolModesTest, SCAN_MATCH_PATTERN) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("scan_match");

        auto seed = co_await redis.hmset(key, "user:1", "a", "user:2", "b", "post:1", "c", "post:2", "d");
        EXPECT_TRUE(seed.ok()) << seed.error();

        // Drive the cursor to completion accumulating only the user:* fields.
        qb::unordered_map<std::string, std::string> collected;
        size_t                                      cursor = 0;
        do {
            auto step = co_await redis.hscan(key, static_cast<long long>(cursor), "user:*", 100);
            EXPECT_TRUE(step.ok()) << step.error();
            for (const auto &kv : step.result().items)
                collected[kv.first] = kv.second;
            cursor = step.result().cursor;
        } while (cursor != 0);

        if (!(collected.size() == 2u)) {
            ADD_FAILURE() << "precondition failed: collected.size() == 2u";
            co_return;
        }
        EXPECT_EQ(collected.at("user:1"), "a");
        EXPECT_EQ(collected.at("user:2"), "b");
        EXPECT_EQ(collected.count("post:1"), 0u);

        completed = true;
    });
    run_coro_test_until(completed);
}

// HSCAN large hash — must span multiple cursor steps and recover the full set with no
// duplicates or drops.
TEST_P(HashProtocolModesTest, SCAN_LARGE_HASH_MULTI_CURSOR) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("scan_large");

        // Populate well above hash-max-listpack-entries (default 128, but commonly
        // raised to 512 in production configs) so the hash is stored as a hashtable.
        // Listpack-encoded hashes ignore COUNT and return every field in a single
        // HSCAN step (cursor 0); only a hashtable honors COUNT and yields a non-zero
        // cursor, which is what proves the multi-step traversal below.
        constexpr int kCount = 1000;
        for (int i = 0; i < kCount; ++i) {
            auto s = co_await redis.hset(key, "f" + std::to_string(i), "v" + std::to_string(i));
            EXPECT_TRUE(s.ok()) << s.error();
        }

        qb::unordered_map<std::string, std::string> collected;
        size_t                                      cursor = 0;
        int                                         steps  = 0;
        do {
            auto step = co_await redis.hscan(key, static_cast<long long>(cursor), "*", 32);
            EXPECT_TRUE(step.ok()) << step.error();
            for (const auto &kv : step.result().items)
                collected[kv.first] = kv.second;
            cursor = step.result().cursor;
            ++steps;
            EXPECT_LT(steps, 1000) << "HSCAN cursor failed to converge";
        } while (cursor != 0);

        EXPECT_GT(steps, 1) << "expected the large hash to require multiple cursor steps";
        if (!(collected.size() == static_cast<size_t>(kCount))) {
            ADD_FAILURE() << "precondition failed: collected.size() == static_cast<size_t>(kCount)";
            co_return;
        }
        EXPECT_EQ(collected.at("f0"), "v0");
        EXPECT_EQ(collected.at("f499"), "v499");

        completed = true;
    });
    run_coro_test_until(completed, std::chrono::seconds(60));
}

} // namespace
