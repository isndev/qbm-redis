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
#include "../redis.h"
#include "protocol_test_common.h"

using namespace qb::io;
using namespace std::chrono;
using namespace qb::redis;

// ============================================================================
// Fixture: all tests run in both RESP2 and RESP3
// ============================================================================

class StringProtocolModesTest : public ProtocolModesTestBase {};

INSTANTIATE_PROTOCOL_MODES(StringProtocolModesTest);

/*
 * COROUTINE TESTS
 */

// Test APPEND command (coroutine version)
TEST_P(StringProtocolModesTest, CORO_STRING_COMMANDS_APPEND) {
    bool completed = false;

    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("append");

        // Test basic append with co_await
        auto reply1 = co_await redis.append(key, "Hello");
        EXPECT_TRUE(reply1.ok());
        EXPECT_EQ(reply1.result(), 5);

        auto reply2 = co_await redis.append(key, " World");
        EXPECT_TRUE(reply2.ok());
        EXPECT_EQ(reply2.result(), 11);

        // Verify the final value
        auto reply3 = co_await redis.get(key);
        EXPECT_TRUE(reply3.ok());
        EXPECT_TRUE(reply3.result().has_value());
        EXPECT_EQ(*reply3.result(), "Hello World");

        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test DECR/DECRBY commands (coroutine version)
TEST_P(StringProtocolModesTest, CORO_STRING_COMMANDS_DECR) {
    bool completed = false;

    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("decr");

        // Set initial value
        (void) co_await redis.set(key, "10");

        // Test DECR with co_await
        auto reply1 = co_await redis.decr(key);
        EXPECT_TRUE(reply1.ok());
        EXPECT_EQ(reply1.result(), 9);

        auto reply2 = co_await redis.decr(key);
        EXPECT_TRUE(reply2.ok());
        EXPECT_EQ(reply2.result(), 8);

        // Test DECRBY
        auto reply3 = co_await redis.decrby(key, 3);
        EXPECT_TRUE(reply3.ok());
        EXPECT_EQ(reply3.result(), 5);

        auto reply4 = co_await redis.decrby(key, 2);
        EXPECT_TRUE(reply4.ok());
        EXPECT_EQ(reply4.result(), 3);

        // Test with non-existent key
        std::string new_key = protocol_key("decr_new");
        auto        reply5  = co_await redis.decr(new_key);
        EXPECT_TRUE(reply5.ok());
        EXPECT_EQ(reply5.result(), -1);

        auto reply6 = co_await redis.decrby(new_key, 5);
        EXPECT_TRUE(reply6.ok());
        EXPECT_EQ(reply6.result(), -6);

        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test GET/GETRANGE commands (coroutine version)
TEST_P(StringProtocolModesTest, CORO_STRING_COMMANDS_GET) {
    bool completed = false;

    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key   = protocol_key("get");
        std::string value = "Hello World";

        // Set value
        (void) co_await redis.set(key, value);

        // Test GET
        auto reply1 = co_await redis.get(key);
        EXPECT_TRUE(reply1.ok());
        EXPECT_TRUE(reply1.result().has_value());
        EXPECT_EQ(*reply1.result(), value);

        // Test GET with non-existent key
        auto reply2 = co_await redis.get(protocol_key("nonexistent"));
        EXPECT_TRUE(reply2.ok());
        EXPECT_FALSE(reply2.result().has_value());

        // Test GETRANGE
        auto reply3 = co_await redis.getrange(key, 0, 4);
        EXPECT_TRUE(reply3.ok());
        EXPECT_EQ(reply3.result(), "Hello");

        auto reply4 = co_await redis.getrange(key, 6, 10);
        EXPECT_TRUE(reply4.ok());
        EXPECT_EQ(reply4.result(), "World");

        auto reply5 = co_await redis.getrange(key, -5, -1);
        EXPECT_TRUE(reply5.ok());
        EXPECT_EQ(reply5.result(), "World");

        // Test SUBSTR (deprecated alias for GETRANGE)
        auto reply6 = co_await redis.substr(key, 0, 4);
        EXPECT_TRUE(reply6.ok());
        EXPECT_EQ(reply6.result(), "Hello");

        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test GETSET command (coroutine version)
TEST_P(StringProtocolModesTest, CORO_STRING_COMMANDS_GETSET) {
    bool completed = false;

    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("getset");

        // Test with non-existent key
        auto reply1 = co_await redis.getset(key, "new_value");
        EXPECT_TRUE(reply1.ok());
        EXPECT_FALSE(reply1.result().has_value());

        // Test with existing key
        (void) co_await redis.set(key, "old_value");
        auto reply2 = co_await redis.getset(key, "new_value");
        EXPECT_TRUE(reply2.ok());
        EXPECT_TRUE(reply2.result().has_value());
        EXPECT_EQ(*reply2.result(), "old_value");

        // Verify new value
        auto reply3 = co_await redis.get(key);
        EXPECT_TRUE(reply3.ok());
        EXPECT_TRUE(reply3.result().has_value());
        EXPECT_EQ(*reply3.result(), "new_value");

        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test INCR/INCRBY commands (coroutine version)
TEST_P(StringProtocolModesTest, CORO_STRING_COMMANDS_INCR) {
    bool completed = false;

    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("incr");

        // Test INCR
        auto reply1 = co_await redis.incr(key);
        EXPECT_TRUE(reply1.ok());
        EXPECT_EQ(reply1.result(), 1);

        auto reply2 = co_await redis.incr(key);
        EXPECT_TRUE(reply2.ok());
        EXPECT_EQ(reply2.result(), 2);

        // Test INCRBY
        auto reply3 = co_await redis.incrby(key, 3);
        EXPECT_TRUE(reply3.ok());
        EXPECT_EQ(reply3.result(), 5);

        auto reply4 = co_await redis.incrby(key, 2);
        EXPECT_TRUE(reply4.ok());
        EXPECT_EQ(reply4.result(), 7);

        // Test with non-existent key
        std::string new_key = protocol_key("incr_new");
        auto        reply5  = co_await redis.incr(new_key);
        EXPECT_TRUE(reply5.ok());
        EXPECT_EQ(reply5.result(), 1);

        auto reply6 = co_await redis.incrby(new_key, 5);
        EXPECT_TRUE(reply6.ok());
        EXPECT_EQ(reply6.result(), 6);

        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test INCRBYFLOAT command (coroutine version)
TEST_P(StringProtocolModesTest, CORO_STRING_COMMANDS_INCRBYFLOAT) {
    bool completed = false;

    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("incrbyfloat");

        // Set initial value
        (void) co_await redis.set(key, "10.5");

        // Test increment
        auto reply1 = co_await redis.incrbyfloat(key, 0.1);
        EXPECT_TRUE(reply1.ok());
        EXPECT_DOUBLE_EQ(reply1.result(), 10.6);

        auto reply2 = co_await redis.incrbyfloat(key, 0.5);
        EXPECT_TRUE(reply2.ok());
        EXPECT_DOUBLE_EQ(reply2.result(), 11.1);

        // Test with non-existent key
        std::string new_key = protocol_key("incrbyfloat_new");
        auto        reply3  = co_await redis.incrbyfloat(new_key, 1.5);
        EXPECT_TRUE(reply3.ok());
        EXPECT_DOUBLE_EQ(reply3.result(), 1.5);

        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test MGET/MSET commands (coroutine version)
TEST_P(StringProtocolModesTest, CORO_STRING_COMMANDS_MGET_MSET) {
    bool completed = false;

    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key1 = protocol_key("mget1");
        std::string key2 = protocol_key("mget2");
        std::string key3 = protocol_key("mget3");

        // Test MSET
        auto reply1 = co_await redis.mset({{key1, "value1"}, {key2, "value2"}, {key3, "value3"}});
        EXPECT_TRUE(reply1.ok());
        EXPECT_EQ(reply1.result(), "OK");

        // Test MGET
        auto reply2 = co_await redis.mget({key1, key2, key3, protocol_key("nonexistent")});
        EXPECT_TRUE(reply2.ok());
        EXPECT_EQ(reply2.result().size(), 4);
        EXPECT_EQ(reply2.result()[0], "value1");
        EXPECT_EQ(reply2.result()[1], "value2");
        EXPECT_EQ(reply2.result()[2], "value3");
        EXPECT_FALSE(reply2.result()[3].has_value());

        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test MSETNX command (coroutine version)
TEST_P(StringProtocolModesTest, CORO_STRING_COMMANDS_MSETNX) {
    bool completed = false;

    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key1 = protocol_key("msetnx1");
        std::string key2 = protocol_key("msetnx2");
        std::string key3 = protocol_key("msetnx3");

        // Test successful MSETNX
        auto reply1 = co_await redis.msetnx({{key1, "value1"}, {key2, "value2"}});
        EXPECT_TRUE(reply1.ok());
        EXPECT_EQ(reply1.result(), true);

        // Test failed MSETNX (key already exists)
        auto reply2 = co_await redis.msetnx({{key1, "new_value1"}, {key3, "value3"}});
        EXPECT_TRUE(reply2.ok());
        EXPECT_EQ(reply2.result(), false);

        // Verify values
        auto reply3 = co_await redis.get(key1);
        auto reply4 = co_await redis.get(key2);
        auto reply5 = co_await redis.get(key3);

        EXPECT_TRUE(reply3.ok());
        EXPECT_TRUE(reply4.ok());
        EXPECT_TRUE(reply5.ok());
        EXPECT_TRUE(reply3.result().has_value());
        EXPECT_TRUE(reply4.result().has_value());
        EXPECT_FALSE(reply5.result().has_value());
        EXPECT_EQ(*reply3.result(), "value1");
        EXPECT_EQ(*reply4.result(), "value2");

        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test PSETEX command (coroutine version)
TEST_P(StringProtocolModesTest, CORO_STRING_COMMANDS_PSETEX) {
    bool completed = false;

    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("psetex");

        // Test with milliseconds
        auto reply1 = co_await redis.psetex(key, 1000, "value");
        EXPECT_TRUE(reply1.ok());

        // Verify value exists
        auto reply2 = co_await redis.get(key);
        EXPECT_TRUE(reply2.ok());
        EXPECT_TRUE(reply2.result().has_value());
        EXPECT_EQ(*reply2.result(), "value");

        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test SET command with various options (coroutine version)
TEST_P(StringProtocolModesTest, CORO_STRING_COMMANDS_SET) {
    bool completed = false;

    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("set");

        // Test basic SET
        auto reply1 = co_await redis.set(key, "value");
        EXPECT_TRUE(reply1.ok());

        // Test SET with expiration
        auto reply2 = co_await redis.set(key, "value2", 1000);
        EXPECT_TRUE(reply2.ok());

        // Test SET with NX option
        std::string key2   = protocol_key("set_nx");
        auto        reply3 = co_await redis.set(key2, "value3", UpdateType::NOT_EXIST);
        EXPECT_TRUE(reply3.ok());

        // Test SET with XX option
        auto reply4 = co_await redis.set(key, "value5", UpdateType::EXIST);
        EXPECT_TRUE(reply4.ok());

        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test SETEX command (coroutine version)
TEST_P(StringProtocolModesTest, CORO_STRING_COMMANDS_SETEX) {
    bool completed = false;

    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("setex");

        // Test SETEX
        auto reply1 = co_await redis.setex(key, 1, "value");
        EXPECT_TRUE(reply1.ok());

        // Verify value exists
        auto reply2 = co_await redis.get(key);
        EXPECT_TRUE(reply2.ok());
        EXPECT_TRUE(reply2.result().has_value());
        EXPECT_EQ(*reply2.result(), "value");

        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test SETNX command (coroutine version)
TEST_P(StringProtocolModesTest, CORO_STRING_COMMANDS_SETNX) {
    bool completed = false;

    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("setnx");

        // Test SETNX
        auto reply1 = co_await redis.setnx(key, "value1");
        EXPECT_TRUE(reply1.ok());
        EXPECT_EQ(reply1.result(), true);

        auto reply2 = co_await redis.setnx(key, "value2");
        EXPECT_TRUE(reply2.ok());
        EXPECT_EQ(reply2.result(), false);

        // Verify value
        auto reply3 = co_await redis.get(key);
        EXPECT_TRUE(reply3.ok());
        EXPECT_TRUE(reply3.result().has_value());
        EXPECT_EQ(*reply3.result(), "value1");

        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test SETRANGE command (coroutine version)
TEST_P(StringProtocolModesTest, CORO_STRING_COMMANDS_SETRANGE) {
    bool completed = false;

    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("setrange");

        // Set initial value
        (void) co_await redis.set(key, "Hello World");

        // Test SETRANGE
        auto reply1 = co_await redis.setrange(key, 6, "Redis");
        EXPECT_TRUE(reply1.ok());
        EXPECT_EQ(reply1.result(), 11);

        // Verify result
        auto reply2 = co_await redis.get(key);
        EXPECT_TRUE(reply2.ok());
        EXPECT_TRUE(reply2.result().has_value());
        EXPECT_EQ(*reply2.result(), "Hello Redis");

        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test STRLEN command (coroutine version)
TEST_P(StringProtocolModesTest, CORO_STRING_COMMANDS_STRLEN) {
    bool completed = false;

    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("strlen");

        // Test with existing key
        (void) co_await redis.set(key, "Hello World");
        auto reply1 = co_await redis.strlen(key);
        EXPECT_TRUE(reply1.ok());
        EXPECT_EQ(reply1.result(), 11);

        // Test with non-existent key
        auto reply2 = co_await redis.strlen(protocol_key("nonexistent"));
        EXPECT_TRUE(reply2.ok());
        EXPECT_EQ(reply2.result(), 0);

        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test GETDEL command (coroutine version)
TEST_P(StringProtocolModesTest, CORO_STRING_COMMANDS_GETDEL) {
    bool completed = false;

    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("getdel");

        // GETDEL on non-existent key returns nullopt
        auto reply1 = co_await redis.getdel(protocol_key("nonexistent_getdel"));
        EXPECT_TRUE(reply1.ok());
        EXPECT_FALSE(reply1.result().has_value());

        // Set and then GETDEL
        (void) co_await redis.set(key, "to_delete");
        auto reply2 = co_await redis.getdel(key);
        EXPECT_TRUE(reply2.ok());
        EXPECT_TRUE(reply2.result().has_value());
        EXPECT_EQ(*reply2.result(), "to_delete");

        // Key should no longer exist
        auto ex_reply = co_await redis.exists(key);
        EXPECT_EQ(ex_reply.result(), 0);

        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test GETEX command (coroutine version)
TEST_P(StringProtocolModesTest, CORO_STRING_COMMANDS_GETEX) {
    bool completed = false;

    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("getex");

        (void) co_await redis.set(key, "value");

        // GETEX with TTL (milliseconds integer)
        auto reply1 = co_await redis.getex(key, 5000LL);
        EXPECT_TRUE(reply1.ok());
        EXPECT_TRUE(reply1.result().has_value());
        EXPECT_EQ(*reply1.result(), "value");

        // Verify TTL was set
        auto pttl_reply = co_await redis.pttl(key);
        EXPECT_TRUE(pttl_reply.ok());
        EXPECT_GT(pttl_reply.result(), 0);

        // GETEX with chrono overload
        auto reply2 = co_await redis.getex(key, std::chrono::milliseconds{10000});
        EXPECT_TRUE(reply2.ok());
        EXPECT_TRUE(reply2.result().has_value());
        EXPECT_EQ(*reply2.result(), "value");

        // GETEX on non-existent key returns nullopt
        auto reply3 = co_await redis.getex(protocol_key("nonexistent_getex"), 1000LL);
        EXPECT_TRUE(reply3.ok());
        EXPECT_FALSE(reply3.result().has_value());

        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test LCS command (coroutine version)
TEST_P(StringProtocolModesTest, CORO_STRING_COMMANDS_LCS) {
    bool completed = false;

    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key1 = protocol_key("lcs1");
        std::string key2 = protocol_key("lcs2");

        (void) co_await redis.set(key1, "ohmytext");
        (void) co_await redis.set(key2, "mynewtext");

        // LCS basic: longest common substring
        auto reply = co_await redis.lcs(key1, key2);
        EXPECT_TRUE(reply.ok());
        // "mytext" is the LCS
        EXPECT_EQ(reply.result(), "mytext");

        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

TEST_P(StringProtocolModesTest, SET_GET) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k = protocol_key("set_get");
        (void) co_await redis.set(k, "val");
        auto r = co_await redis.get(k);
        EXPECT_TRUE(r.ok()) << r.error();
        EXPECT_TRUE(r.result().has_value());
        if (r.result())
            {
            EXPECT_EQ(*r.result(), "val");
            }
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(StringProtocolModesTest, GET_MISSING_KEY) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto r = co_await redis.get(protocol_key("nonexistent"));
        EXPECT_TRUE(r.ok()) << r.error();
        EXPECT_FALSE(r.result().has_value());
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(StringProtocolModesTest, MSET_MGET) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k1 = protocol_key("mset1");
        auto k2 = protocol_key("mset2");
        (void) co_await redis.mset({{k1, "a"}, {k2, "b"}});
        auto r = co_await redis.mget({k1, k2});
        EXPECT_TRUE(r.ok()) << r.error();
        auto v = r.result();
        EXPECT_EQ(v.size(), 2u);
        if (v.size() >= 2) {
            EXPECT_TRUE(v[0].has_value());
            EXPECT_TRUE(v[1].has_value());
            if (v[0])
                {
                EXPECT_EQ(*v[0], "a");
                }
            if (v[1])
                {
                EXPECT_EQ(*v[1], "b");
                }
        }
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(StringProtocolModesTest, APPEND) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k = protocol_key("append");
        (void) co_await redis.append(k, "Hello");
        auto r = co_await redis.append(k, " World");
        EXPECT_TRUE(r.ok()) << r.error();
        EXPECT_EQ(r.result(), 11);
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(StringProtocolModesTest, INCR_DECR) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k = protocol_key("incr_decr");
        (void) co_await redis.set(k, "10");
        auto r1 = co_await redis.incr(k);
        EXPECT_TRUE(r1.ok()) << r1.error();
        EXPECT_EQ(r1.result(), 11);
        auto r2 = co_await redis.decr(k);
        EXPECT_TRUE(r2.ok()) << r2.error();
        EXPECT_EQ(r2.result(), 10);
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(StringProtocolModesTest, INCRBYFLOAT_DOUBLE) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k = protocol_key("incrbyfloat");
        (void) co_await redis.set(k, "1.5");
        auto r = co_await redis.incrbyfloat(k, 0.5);
        EXPECT_TRUE(r.ok()) << r.error();
        EXPECT_DOUBLE_EQ(r.result(), 2.0);
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(StringProtocolModesTest, STRLEN_INTEGER) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k = protocol_key("strlen");
        (void) co_await redis.set(k, "hello");
        auto r = co_await redis.strlen(k);
        EXPECT_TRUE(r.ok()) << r.error();
        if (r.ok())
            {
            EXPECT_EQ(r.result(), 5);
            }
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(StringProtocolModesTest, SETEX_STATUS) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k = protocol_key("setex");
        auto r = co_await redis.setex(k, 60, "val");
        EXPECT_TRUE(r.ok()) << r.error();
        if (r.ok())
            {
            EXPECT_TRUE(r.result().ok());
            }
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(StringProtocolModesTest, GETRANGE_STRING) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k = protocol_key("getrange");
        (void) co_await redis.set(k, "hello");
        auto r = co_await redis.getrange(k, 0, 2);
        EXPECT_TRUE(r.ok()) << r.error();
        if (r.ok())
            {
            EXPECT_EQ(r.result(), "hel");
            }
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(StringProtocolModesTest, SUBSTR_STRING) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k = protocol_key("substr");
        (void) co_await redis.set(k, "HelloWorld");
        auto r = co_await redis.substr(k, 0, 4);
        EXPECT_TRUE(r.ok()) << r.error();
        if (r.ok())
            {
            EXPECT_EQ(r.result(), "Hello");
            }
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(StringProtocolModesTest, DECR_INTEGER) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([&]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k = protocol_key("decr");
        (void) co_await redis.set(k, "10");
        auto r = co_await redis.decr(k);
        EXPECT_TRUE(r.ok()) << r.error();
        if (r.ok())
            {
            EXPECT_EQ(r.result(), 9);
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