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

class HashProtocolModesTest : public ProtocolModesTestBase {};

INSTANTIATE_PROTOCOL_MODES(HashProtocolModesTest);

/*
 * COROUTINE TESTS
 */

// Test basic HSET and HGET operations
TEST_P(HashProtocolModesTest, CORO_HASH_COMMANDS_HSET_HGET) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("basic");

        // HSET test
        auto reply1 = co_await redis.hset(key, "field1", "value1");
        EXPECT_TRUE(reply1.ok());
        EXPECT_EQ(reply1.result(), 1);

        auto reply2 = co_await redis.hset(key, "field2", "value2");
        EXPECT_TRUE(reply2.ok());
        EXPECT_EQ(reply2.result(), 1);

        // HGET test
        auto result1_reply = co_await redis.hget(key, "field1");
        EXPECT_TRUE(result1_reply.ok());
        EXPECT_TRUE(result1_reply.result().has_value());
        EXPECT_EQ(*result1_reply.result(), "value1");

        auto result2_reply = co_await redis.hget(key, "field2");
        EXPECT_TRUE(result2_reply.ok());
        EXPECT_TRUE(result2_reply.result().has_value());
        EXPECT_EQ(*result2_reply.result(), "value2");

        auto result3_reply = co_await redis.hget(key, "field3");
        EXPECT_TRUE(result3_reply.ok());
        EXPECT_FALSE(result3_reply.result().has_value());

        // HEXISTS test
        auto exists1_reply = co_await redis.hexists(key, "field1");
        EXPECT_TRUE(exists1_reply.ok());
        EXPECT_TRUE(exists1_reply.result());

        auto exists2_reply = co_await redis.hexists(key, "field3");
        EXPECT_TRUE(exists2_reply.ok());
        EXPECT_FALSE(exists2_reply.result());

        // HLEN test
        auto len_reply = co_await redis.hlen(key);
        EXPECT_TRUE(len_reply.ok());
        EXPECT_EQ(len_reply.result(), 2);

        // HDEL test
        auto del_reply = co_await redis.hdel(key, "field1");
        EXPECT_TRUE(del_reply.ok());
        EXPECT_EQ(del_reply.result(), 1);

        auto hget_after_del = co_await redis.hget(key, "field1");
        EXPECT_TRUE(hget_after_del.ok());
        EXPECT_FALSE(hget_after_del.result().has_value());

        auto len_after_del = co_await redis.hlen(key);
        EXPECT_TRUE(len_after_del.ok());
        EXPECT_EQ(len_after_del.result(), 1);

        // Cleanup
        (void) co_await redis.del(key);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test HMSET and HMGET operations
TEST_P(HashProtocolModesTest, CORO_HASH_COMMANDS_HMSET_HMGET) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("hmset");

        // HMSET test
        auto hmset_reply = co_await redis.hmset(key, "field1", "value1", "field2", "value2", "field3", "value3");
        EXPECT_TRUE(hmset_reply.ok());
        EXPECT_TRUE(hmset_reply.result().ok());

        // HMGET test
        auto values_reply = co_await redis.hmget(key, "field1", "field2", "field3", "field4");
        EXPECT_TRUE(values_reply.ok());
        EXPECT_EQ(values_reply.result().size(), 4);
        EXPECT_TRUE(values_reply.result()[0].has_value());
        EXPECT_EQ(*values_reply.result()[0], "value1");
        EXPECT_TRUE(values_reply.result()[1].has_value());
        EXPECT_EQ(*values_reply.result()[1], "value2");
        EXPECT_TRUE(values_reply.result()[2].has_value());
        EXPECT_EQ(*values_reply.result()[2], "value3");
        EXPECT_FALSE(values_reply.result()[3].has_value());

        // HGETALL test
        auto all_values_reply = co_await redis.hgetall(key);
        EXPECT_TRUE(all_values_reply.ok());
        EXPECT_EQ(all_values_reply.result().size(), 3);
        EXPECT_EQ(all_values_reply.result()["field1"], "value1");
        EXPECT_EQ(all_values_reply.result()["field2"], "value2");
        EXPECT_EQ(all_values_reply.result()["field3"], "value3");

        // Cleanup
        (void) co_await redis.del(key);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test increment operations
TEST_P(HashProtocolModesTest, CORO_HASH_COMMANDS_INCR) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("incr");

        // HINCRBY test
        auto incr1_reply = co_await redis.hincrby(key, "counter", 1);
        EXPECT_TRUE(incr1_reply.ok());
        EXPECT_EQ(incr1_reply.result(), 1);

        auto incr2_reply = co_await redis.hincrby(key, "counter", 10);
        EXPECT_TRUE(incr2_reply.ok());
        EXPECT_EQ(incr2_reply.result(), 11);

        auto incr3_reply = co_await redis.hincrby(key, "counter", -5);
        EXPECT_TRUE(incr3_reply.ok());
        EXPECT_EQ(incr3_reply.result(), 6);

        // HINCRBYFLOAT test
        auto float1_reply = co_await redis.hincrbyfloat(key, "float", 10.5);
        EXPECT_TRUE(float1_reply.ok());
        EXPECT_FLOAT_EQ(float1_reply.result(), 10.5);

        auto float2_reply = co_await redis.hincrbyfloat(key, "float", 0.5);
        EXPECT_TRUE(float2_reply.ok());
        EXPECT_FLOAT_EQ(float2_reply.result(), 11.0);

        auto float3_reply = co_await redis.hincrbyfloat(key, "float", -1.5);
        EXPECT_TRUE(float3_reply.ok());
        EXPECT_FLOAT_EQ(float3_reply.result(), 9.5);

        // Verify values
        auto counter_reply = co_await redis.hget(key, "counter");
        EXPECT_TRUE(counter_reply.ok());
        EXPECT_EQ(*counter_reply.result(), "6");

        auto float_reply = co_await redis.hget(key, "float");
        EXPECT_TRUE(float_reply.ok());
        EXPECT_EQ(*float_reply.result(), "9.5");

        // Cleanup
        (void) co_await redis.del(key);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test HSETNX operation
TEST_P(HashProtocolModesTest, CORO_HASH_COMMANDS_HSETNX) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("hsetnx");

        // HSETNX on new field
        auto setnx1_reply = co_await redis.hsetnx(key, "field1", "value1");
        EXPECT_TRUE(setnx1_reply.ok());
        EXPECT_TRUE(setnx1_reply.result());

        auto hget1_reply = co_await redis.hget(key, "field1");
        EXPECT_TRUE(hget1_reply.ok());
        EXPECT_EQ(*hget1_reply.result(), "value1");

        // HSETNX on existing field
        auto setnx2_reply = co_await redis.hsetnx(key, "field1", "new-value");
        EXPECT_TRUE(setnx2_reply.ok());
        EXPECT_FALSE(setnx2_reply.result());

        auto hget2_reply = co_await redis.hget(key, "field1");
        EXPECT_TRUE(hget2_reply.ok());
        EXPECT_EQ(*hget2_reply.result(), "value1"); // Value should not change

        // Cleanup
        (void) co_await redis.del(key);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test keys and values operations
TEST_P(HashProtocolModesTest, CORO_HASH_COMMANDS_KEYS_VALUES) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("keys-values");

        // Setup hash
        (void) co_await redis.hmset(key, "field1", "value1", "field2", "value2", "field3", "value3");

        // HKEYS test
        auto keys_reply = co_await redis.hkeys(key);
        EXPECT_TRUE(keys_reply.ok());
        EXPECT_EQ(keys_reply.result().size(), 3);
        EXPECT_TRUE(std::find(keys_reply.result().begin(), keys_reply.result().end(), "field1") != keys_reply.result().end());
        EXPECT_TRUE(std::find(keys_reply.result().begin(), keys_reply.result().end(), "field2") != keys_reply.result().end());
        EXPECT_TRUE(std::find(keys_reply.result().begin(), keys_reply.result().end(), "field3") != keys_reply.result().end());

        // HVALS test
        auto values_reply = co_await redis.hvals(key);
        EXPECT_TRUE(values_reply.ok());
        EXPECT_EQ(values_reply.result().size(), 3);
        EXPECT_TRUE(std::find(values_reply.result().begin(), values_reply.result().end(), "value1") != values_reply.result().end());
        EXPECT_TRUE(std::find(values_reply.result().begin(), values_reply.result().end(), "value2") != values_reply.result().end());
        EXPECT_TRUE(std::find(values_reply.result().begin(), values_reply.result().end(), "value3") != values_reply.result().end());

        // Cleanup
        (void) co_await redis.del(key);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test HSTRLEN operation
TEST_P(HashProtocolModesTest, CORO_HASH_COMMANDS_STRLEN) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("strlen");

        // Setup hash
        (void) co_await redis.hset(key, "field1", "hello");
        (void) co_await redis.hset(key, "field2", "world!");

        // HSTRLEN test
        auto len1_reply = co_await redis.hstrlen(key, "field1");
        EXPECT_TRUE(len1_reply.ok());
        EXPECT_EQ(len1_reply.result(), 5);

        auto len2_reply = co_await redis.hstrlen(key, "field2");
        EXPECT_TRUE(len2_reply.ok());
        EXPECT_EQ(len2_reply.result(), 6);

        auto len3_reply = co_await redis.hstrlen(key, "nonexistent");
        EXPECT_TRUE(len3_reply.ok());
        EXPECT_EQ(len3_reply.result(), 0);

        // Cleanup
        (void) co_await redis.del(key);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test HSCAN operation
TEST_P(HashProtocolModesTest, CORO_HASH_COMMANDS_SCAN) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("scan");

        // Setup hash with multiple fields
        (void) co_await redis.hmset(key, "field1", "value1", "field2", "value2", "field3", "value3");

        // HSCAN test
        auto scan_reply = co_await redis.hscan(key, 0, "*", 10);
        EXPECT_TRUE(scan_reply.ok());
        EXPECT_EQ(scan_reply.result().items.size(), 3);

        // Cleanup
        (void) co_await redis.del(key);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test multiple hash operations in sequence
TEST_P(HashProtocolModesTest, CORO_HASH_COMMANDS_SEQUENCE) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("sequence");

        // Set multiple fields
        (void) co_await redis.hset(key, "name", "John");
        (void) co_await redis.hset(key, "age", "30");
        (void) co_await redis.hset(key, "city", "NYC");

        // Verify with HGETALL
        auto all_reply = co_await redis.hgetall(key);
        EXPECT_TRUE(all_reply.ok());
        EXPECT_EQ(all_reply.result()["name"], "John");
        EXPECT_EQ(all_reply.result()["age"], "30");
        EXPECT_EQ(all_reply.result()["city"], "NYC");

        // Update age
        (void) co_await redis.hincrby(key, "age", 1);

        auto age_reply = co_await redis.hget(key, "age");
        EXPECT_TRUE(age_reply.ok());
        EXPECT_EQ(*age_reply.result(), "31");

        // Delete a field
        (void) co_await redis.hdel(key, "city");

        auto len_reply = co_await redis.hlen(key);
        EXPECT_TRUE(len_reply.ok());
        EXPECT_EQ(len_reply.result(), 2);

        // Cleanup
        (void) co_await redis.del(key);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

TEST_P(HashProtocolModesTest, HSET_HGET) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k = protocol_key("hash");
        (void) co_await redis.hset(k, "field", "value");
        auto r = co_await redis.hget(k, "field");
        EXPECT_TRUE(r.ok()) << r.error();
        if (r.ok() && r.result())
            {
            EXPECT_EQ(*r.result(), "value");
            }
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(HashProtocolModesTest, HGETALL_MAP) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k = protocol_key("hgetall");
        (void) co_await redis.hset(k, "a", "1");
        (void) co_await redis.hset(k, "b", "2");
        auto r = co_await redis.hgetall(k);
        EXPECT_TRUE(r.ok()) << r.error();
        if (r.ok()) {
            const auto &m = r.result();
            EXPECT_EQ(m.size(), 2u);
            EXPECT_EQ(m.at("a"), "1");
            EXPECT_EQ(m.at("b"), "2");
        }
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(HashProtocolModesTest, HMGET_HEXISTS) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k = protocol_key("hmget");
        (void) co_await redis.hset(k, "f1", "v1");
        (void) co_await redis.hset(k, "f2", "v2");
        auto hmget_r = co_await redis.hmget(k, "f1", "f2", "f3");
        EXPECT_TRUE(hmget_r.ok()) << hmget_r.error();
        if (hmget_r.ok()) {
            const auto &v = hmget_r.result();
            EXPECT_EQ(v.size(), 3u);
            EXPECT_TRUE(v[0].has_value() && *v[0] == "v1");
            EXPECT_TRUE(v[1].has_value() && *v[1] == "v2");
            EXPECT_FALSE(v[2].has_value());
        }
        auto hexists_r = co_await redis.hexists(k, "f1");
        EXPECT_TRUE(hexists_r.ok()) << hexists_r.error();
        if (hexists_r.ok())
            {
            EXPECT_TRUE(hexists_r.result());
            }
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(HashProtocolModesTest, HINCRBY_INTEGER) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k = protocol_key("hincrby");
        (void) co_await redis.hset(k, "n", "5");
        auto r = co_await redis.hincrby(k, "n", 3);
        EXPECT_TRUE(r.ok()) << r.error();
        if (r.ok())
            {
            EXPECT_EQ(r.result(), 8);
            }
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(HashProtocolModesTest, HDEL_INTEGER) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k = protocol_key("hdel");
        (void) co_await redis.hset(k, "a", "1");
        (void) co_await redis.hset(k, "b", "2");
        auto r = co_await redis.hdel(k, "a");
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
