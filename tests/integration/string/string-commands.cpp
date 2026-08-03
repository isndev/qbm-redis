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
 * @file integration/string/string-commands.cpp
 * @brief String-family commands (APPEND/INCR/DECR/GET/SET/MGET/SETEX/...), RESP2 + RESP3.
 *
 * Integration tier — needs a live redis (env `REDIS_URI`, default tcp://localhost:6379).
 * Migrated from test-string-commands.cpp: the 11 lowercase short-form dups (SET_GET,
 * GET_MISSING_KEY, MSET_MGET, APPEND, INCR_DECR, INCRBYFLOAT_DOUBLE, STRLEN_INTEGER,
 * SETEX_STATUS, GETRANGE_STRING, SUBSTR_STRING, DECR_INTEGER) were deleted (strictly
 * subsumed by the CORO_* cases). `if (r.ok())`-guarded value checks were promoted to hard
 * ASSERTs; SETEX/PSETEX now read the TTL back (no sleep); SET NX/XX assert the nil-vs-OK
 * outcome (the status, not just the protocol-level ok()). Busy-spins → run_coro_test_until.
 */

#include <gtest/gtest.h>
#include <string>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include "../../shared/redis_integration_fixture.h"
#include <qbm/redis/redis.h>

using namespace qb::io;
using namespace std::chrono;
using namespace qb::redis;
using namespace qb::redis::test;

class StringProtocolModesTest : public ProtocolModesTestBase {};

INSTANTIATE_PROTOCOL_MODES(StringProtocolModesTest);

TEST_P(StringProtocolModesTest, APPEND) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("append");

        auto reply1 = co_await redis.append(key, "Hello");
        EXPECT_TRUE(reply1.ok()) << reply1.error();
        EXPECT_EQ(reply1.result(), 5);

        auto reply2 = co_await redis.append(key, " World");
        EXPECT_TRUE(reply2.ok()) << reply2.error();
        EXPECT_EQ(reply2.result(), 11);

        auto reply3 = co_await redis.get(key);
        EXPECT_TRUE(reply3.ok()) << reply3.error();
        if (!(reply3.result().has_value())) {
            ADD_FAILURE() << "precondition failed: reply3.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*reply3.result(), "Hello World");

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

TEST_P(StringProtocolModesTest, DECR) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("decr");
        CO_IGNORE(co_await redis.set(key, "10"));

        auto reply1 = co_await redis.decr(key);
        EXPECT_TRUE(reply1.ok()) << reply1.error();
        EXPECT_EQ(reply1.result(), 9);

        auto reply2 = co_await redis.decr(key);
        EXPECT_TRUE(reply2.ok()) << reply2.error();
        EXPECT_EQ(reply2.result(), 8);

        auto reply3 = co_await redis.decrby(key, 3);
        EXPECT_TRUE(reply3.ok()) << reply3.error();
        EXPECT_EQ(reply3.result(), 5);

        auto reply4 = co_await redis.decrby(key, 2);
        EXPECT_TRUE(reply4.ok()) << reply4.error();
        EXPECT_EQ(reply4.result(), 3);

        const std::string new_key = protocol_key("decr_new");
        auto              reply5  = co_await redis.decr(new_key);
        EXPECT_TRUE(reply5.ok()) << reply5.error();
        EXPECT_EQ(reply5.result(), -1);

        auto reply6 = co_await redis.decrby(new_key, 5);
        EXPECT_TRUE(reply6.ok()) << reply6.error();
        EXPECT_EQ(reply6.result(), -6);

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

TEST_P(StringProtocolModesTest, GET_AND_GETRANGE) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key   = protocol_key("get");
        const std::string value = "Hello World";
        CO_IGNORE(co_await redis.set(key, value));

        auto reply1 = co_await redis.get(key);
        EXPECT_TRUE(reply1.ok()) << reply1.error();
        if (!(reply1.result().has_value())) {
            ADD_FAILURE() << "precondition failed: reply1.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*reply1.result(), value);

        auto reply2 = co_await redis.get(protocol_key("nonexistent"));
        EXPECT_TRUE(reply2.ok()) << reply2.error();
        EXPECT_FALSE(reply2.result().has_value());

        auto reply3 = co_await redis.getrange(key, 0, 4);
        EXPECT_TRUE(reply3.ok()) << reply3.error();
        EXPECT_EQ(reply3.result(), "Hello");

        auto reply4 = co_await redis.getrange(key, 6, 10);
        EXPECT_TRUE(reply4.ok()) << reply4.error();
        EXPECT_EQ(reply4.result(), "World");

        auto reply5 = co_await redis.getrange(key, -5, -1);
        EXPECT_TRUE(reply5.ok()) << reply5.error();
        EXPECT_EQ(reply5.result(), "World");

        // SUBSTR is the deprecated alias for GETRANGE.
        auto reply6 = co_await redis.substr(key, 0, 4);
        EXPECT_TRUE(reply6.ok()) << reply6.error();
        EXPECT_EQ(reply6.result(), "Hello");

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

TEST_P(StringProtocolModesTest, GETSET) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("getset");

        auto reply1 = co_await redis.getset(key, "new_value");
        EXPECT_TRUE(reply1.ok()) << reply1.error();
        EXPECT_FALSE(reply1.result().has_value());

        CO_IGNORE(co_await redis.set(key, "old_value"));
        auto reply2 = co_await redis.getset(key, "new_value");
        EXPECT_TRUE(reply2.ok()) << reply2.error();
        if (!(reply2.result().has_value())) {
            ADD_FAILURE() << "precondition failed: reply2.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*reply2.result(), "old_value");

        auto reply3 = co_await redis.get(key);
        EXPECT_TRUE(reply3.ok()) << reply3.error();
        if (!(reply3.result().has_value())) {
            ADD_FAILURE() << "precondition failed: reply3.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*reply3.result(), "new_value");

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

TEST_P(StringProtocolModesTest, INCR) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("incr");

        auto reply1 = co_await redis.incr(key);
        EXPECT_TRUE(reply1.ok()) << reply1.error();
        EXPECT_EQ(reply1.result(), 1);

        auto reply2 = co_await redis.incr(key);
        EXPECT_TRUE(reply2.ok()) << reply2.error();
        EXPECT_EQ(reply2.result(), 2);

        auto reply3 = co_await redis.incrby(key, 3);
        EXPECT_TRUE(reply3.ok()) << reply3.error();
        EXPECT_EQ(reply3.result(), 5);

        auto reply4 = co_await redis.incrby(key, 2);
        EXPECT_TRUE(reply4.ok()) << reply4.error();
        EXPECT_EQ(reply4.result(), 7);

        const std::string new_key = protocol_key("incr_new");
        auto              reply5  = co_await redis.incr(new_key);
        EXPECT_TRUE(reply5.ok()) << reply5.error();
        EXPECT_EQ(reply5.result(), 1);

        auto reply6 = co_await redis.incrby(new_key, 5);
        EXPECT_TRUE(reply6.ok()) << reply6.error();
        EXPECT_EQ(reply6.result(), 6);

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

TEST_P(StringProtocolModesTest, INCRBYFLOAT) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("incrbyfloat");
        CO_IGNORE(co_await redis.set(key, "10.5"));

        auto reply1 = co_await redis.incrbyfloat(key, 0.1);
        EXPECT_TRUE(reply1.ok()) << reply1.error();
        EXPECT_DOUBLE_EQ(reply1.result(), 10.6);

        auto reply2 = co_await redis.incrbyfloat(key, 0.5);
        EXPECT_TRUE(reply2.ok()) << reply2.error();
        EXPECT_DOUBLE_EQ(reply2.result(), 11.1);

        const std::string new_key = protocol_key("incrbyfloat_new");
        auto              reply3  = co_await redis.incrbyfloat(new_key, 1.5);
        EXPECT_TRUE(reply3.ok()) << reply3.error();
        EXPECT_DOUBLE_EQ(reply3.result(), 1.5);

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

TEST_P(StringProtocolModesTest, MGET_MSET) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key1 = protocol_key("mget1");
        const std::string key2 = protocol_key("mget2");
        const std::string key3 = protocol_key("mget3");

        auto reply1 = co_await redis.mset({{key1, "value1"}, {key2, "value2"}, {key3, "value3"}});
        EXPECT_TRUE(reply1.ok()) << reply1.error();
        EXPECT_EQ(reply1.result(), "OK");

        auto reply2 = co_await redis.mget({key1, key2, key3, protocol_key("nonexistent")});
        EXPECT_TRUE(reply2.ok()) << reply2.error();
        if (!(reply2.result().size() == 4u)) {
            ADD_FAILURE() << "precondition failed: reply2.result().size() == 4u";
            co_return;
        }
        if (!(reply2.result()[0].has_value())) {
            ADD_FAILURE() << "precondition failed: reply2.result()[0].has_value()";
            co_return;
        }
        if (!(reply2.result()[1].has_value())) {
            ADD_FAILURE() << "precondition failed: reply2.result()[1].has_value()";
            co_return;
        }
        if (!(reply2.result()[2].has_value())) {
            ADD_FAILURE() << "precondition failed: reply2.result()[2].has_value()";
            co_return;
        }
        EXPECT_EQ(*reply2.result()[0], "value1");
        EXPECT_EQ(*reply2.result()[1], "value2");
        EXPECT_EQ(*reply2.result()[2], "value3");
        EXPECT_FALSE(reply2.result()[3].has_value());

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

TEST_P(StringProtocolModesTest, MSETNX) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key1 = protocol_key("msetnx1");
        const std::string key2 = protocol_key("msetnx2");
        const std::string key3 = protocol_key("msetnx3");

        auto reply1 = co_await redis.msetnx({{key1, "value1"}, {key2, "value2"}});
        EXPECT_TRUE(reply1.ok()) << reply1.error();
        EXPECT_TRUE(reply1.result());

        // Fails because key1 already exists; no key in the batch is written.
        auto reply2 = co_await redis.msetnx({{key1, "new_value1"}, {key3, "value3"}});
        EXPECT_TRUE(reply2.ok()) << reply2.error();
        EXPECT_FALSE(reply2.result());

        auto reply3 = co_await redis.get(key1);
        auto reply4 = co_await redis.get(key2);
        auto reply5 = co_await redis.get(key3);
        EXPECT_TRUE(reply3.ok() && reply4.ok() && reply5.ok());
        if (!(reply3.result().has_value())) {
            ADD_FAILURE() << "precondition failed: reply3.result().has_value()";
            co_return;
        }
        if (!(reply4.result().has_value())) {
            ADD_FAILURE() << "precondition failed: reply4.result().has_value()";
            co_return;
        }
        EXPECT_FALSE(reply5.result().has_value());
        EXPECT_EQ(*reply3.result(), "value1");
        EXPECT_EQ(*reply4.result(), "value2");

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// SETEX sets a value with a second-granularity TTL; read the TTL back (no sleep) to prove
// the expiry was actually applied, not just that the command returned OK.
TEST_P(StringProtocolModesTest, SETEX) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("setex");

        auto reply1 = co_await redis.setex(key, 100, "value");
        EXPECT_TRUE(reply1.ok()) << reply1.error();
        EXPECT_TRUE(reply1.result().ok());

        auto reply2 = co_await redis.get(key);
        EXPECT_TRUE(reply2.ok()) << reply2.error();
        if (!(reply2.result().has_value())) {
            ADD_FAILURE() << "precondition failed: reply2.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*reply2.result(), "value");

        auto ttl = co_await redis.ttl(key);
        EXPECT_TRUE(ttl.ok()) << ttl.error();
        EXPECT_GT(ttl.result(), 90); // set to 100s; allow for command latency
        EXPECT_LE(ttl.result(), 100);

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// PSETEX sets a millisecond-granularity TTL; read PTTL back.
TEST_P(StringProtocolModesTest, PSETEX) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("psetex");

        auto reply1 = co_await redis.psetex(key, 100000, "value");
        EXPECT_TRUE(reply1.ok()) << reply1.error();
        EXPECT_TRUE(reply1.result().ok());

        auto reply2 = co_await redis.get(key);
        EXPECT_TRUE(reply2.ok()) << reply2.error();
        if (!(reply2.result().has_value())) {
            ADD_FAILURE() << "precondition failed: reply2.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*reply2.result(), "value");

        auto pttl = co_await redis.pttl(key);
        EXPECT_TRUE(pttl.ok()) << pttl.error();
        EXPECT_GT(pttl.result(), 90000);
        EXPECT_LE(pttl.result(), 100000);

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// SET with NX/XX must assert the nil-vs-OK outcome: a failed conditional SET returns a nil
// status (status{} → status.ok()==false), a successful one returns +OK.
TEST_P(StringProtocolModesTest, SET_WITH_CONDITIONS) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("set");

        // Plain SET succeeds.
        auto basic = co_await redis.set(key, "value");
        EXPECT_TRUE(basic.ok()) << basic.error();
        EXPECT_TRUE(basic.result().ok());

        // SET with PX TTL succeeds; read PTTL to confirm.
        auto with_ttl = co_await redis.set(key, "value2", 100000LL);
        EXPECT_TRUE(with_ttl.ok()) << with_ttl.error();
        EXPECT_TRUE(with_ttl.result().ok());
        auto pttl = co_await redis.pttl(key);
        EXPECT_TRUE(pttl.ok());
        EXPECT_GT(pttl.result(), 0);

        // NX on a fresh key → OK.
        const std::string nx_key = protocol_key("set_nx");
        auto              nx_ok  = co_await redis.set(nx_key, "v", UpdateType::NOT_EXIST);
        EXPECT_TRUE(nx_ok.ok()) << nx_ok.error();
        EXPECT_TRUE(nx_ok.result().ok()) << "NX on a non-existent key must set (+OK)";

        // NX on an existing key → nil (status not OK), value unchanged.
        auto nx_fail = co_await redis.set(nx_key, "v2", UpdateType::NOT_EXIST);
        EXPECT_TRUE(nx_fail.ok()) << nx_fail.error();
        EXPECT_FALSE(nx_fail.result().ok()) << "NX on an existing key must return nil, not OK";
        auto after_nx = co_await redis.get(nx_key);
        if (!(after_nx.ok() && after_nx.result().has_value())) {
            ADD_FAILURE() << "precondition failed: after_nx.ok() && after_nx.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*after_nx.result(), "v") << "failed NX must not overwrite the value";

        // XX on the existing key → OK and overwrites.
        auto xx_ok = co_await redis.set(nx_key, "v3", UpdateType::EXIST);
        EXPECT_TRUE(xx_ok.ok()) << xx_ok.error();
        EXPECT_TRUE(xx_ok.result().ok()) << "XX on an existing key must set (+OK)";
        auto after_xx = co_await redis.get(nx_key);
        if (!(after_xx.ok() && after_xx.result().has_value())) {
            ADD_FAILURE() << "precondition failed: after_xx.ok() && after_xx.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*after_xx.result(), "v3");

        // XX on a missing key → nil (status not OK), key stays absent.
        const std::string xx_missing = protocol_key("set_xx_missing");
        auto              xx_fail    = co_await redis.set(xx_missing, "v", UpdateType::EXIST);
        EXPECT_TRUE(xx_fail.ok()) << xx_fail.error();
        EXPECT_FALSE(xx_fail.result().ok()) << "XX on a missing key must return nil, not OK";
        auto absent = co_await redis.get(xx_missing);
        EXPECT_TRUE(absent.ok());
        EXPECT_FALSE(absent.result().has_value());

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

TEST_P(StringProtocolModesTest, SETNX) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("setnx");

        auto reply1 = co_await redis.setnx(key, "value1");
        EXPECT_TRUE(reply1.ok()) << reply1.error();
        EXPECT_TRUE(reply1.result());

        auto reply2 = co_await redis.setnx(key, "value2");
        EXPECT_TRUE(reply2.ok()) << reply2.error();
        EXPECT_FALSE(reply2.result());

        auto reply3 = co_await redis.get(key);
        EXPECT_TRUE(reply3.ok()) << reply3.error();
        if (!(reply3.result().has_value())) {
            ADD_FAILURE() << "precondition failed: reply3.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*reply3.result(), "value1");

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

TEST_P(StringProtocolModesTest, SETRANGE) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("setrange");
        CO_IGNORE(co_await redis.set(key, "Hello World"));

        auto reply1 = co_await redis.setrange(key, 6, "Redis");
        EXPECT_TRUE(reply1.ok()) << reply1.error();
        EXPECT_EQ(reply1.result(), 11);

        auto reply2 = co_await redis.get(key);
        EXPECT_TRUE(reply2.ok()) << reply2.error();
        if (!(reply2.result().has_value())) {
            ADD_FAILURE() << "precondition failed: reply2.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*reply2.result(), "Hello Redis");

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

TEST_P(StringProtocolModesTest, STRLEN) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("strlen");

        CO_IGNORE(co_await redis.set(key, "Hello World"));
        auto reply1 = co_await redis.strlen(key);
        EXPECT_TRUE(reply1.ok()) << reply1.error();
        EXPECT_EQ(reply1.result(), 11);

        auto reply2 = co_await redis.strlen(protocol_key("nonexistent"));
        EXPECT_TRUE(reply2.ok()) << reply2.error();
        EXPECT_EQ(reply2.result(), 0);

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

TEST_P(StringProtocolModesTest, GETDEL) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("getdel");

        auto reply1 = co_await redis.getdel(protocol_key("nonexistent_getdel"));
        EXPECT_TRUE(reply1.ok()) << reply1.error();
        EXPECT_FALSE(reply1.result().has_value());

        CO_IGNORE(co_await redis.set(key, "to_delete"));
        auto reply2 = co_await redis.getdel(key);
        EXPECT_TRUE(reply2.ok()) << reply2.error();
        if (!(reply2.result().has_value())) {
            ADD_FAILURE() << "precondition failed: reply2.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*reply2.result(), "to_delete");

        auto ex_reply = co_await redis.exists(key);
        EXPECT_TRUE(ex_reply.ok()) << ex_reply.error();
        EXPECT_EQ(ex_reply.result(), 0);

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

TEST_P(StringProtocolModesTest, GETEX) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("getex");
        CO_IGNORE(co_await redis.set(key, "value"));

        auto reply1 = co_await redis.getex(key, 5000LL);
        EXPECT_TRUE(reply1.ok()) << reply1.error();
        if (!(reply1.result().has_value())) {
            ADD_FAILURE() << "precondition failed: reply1.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*reply1.result(), "value");

        auto pttl_reply = co_await redis.pttl(key);
        EXPECT_TRUE(pttl_reply.ok()) << pttl_reply.error();
        EXPECT_GT(pttl_reply.result(), 0);

        auto reply2 = co_await redis.getex(key, std::chrono::milliseconds{10000});
        EXPECT_TRUE(reply2.ok()) << reply2.error();
        if (!(reply2.result().has_value())) {
            ADD_FAILURE() << "precondition failed: reply2.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*reply2.result(), "value");

        auto reply3 = co_await redis.getex(protocol_key("nonexistent_getex"), 1000LL);
        EXPECT_TRUE(reply3.ok()) << reply3.error();
        EXPECT_FALSE(reply3.result().has_value());

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

TEST_P(StringProtocolModesTest, LCS) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key1 = protocol_key("lcs1");
        const std::string key2 = protocol_key("lcs2");
        CO_IGNORE(co_await redis.set(key1, "ohmytext"));
        CO_IGNORE(co_await redis.set(key2, "mynewtext"));

        auto reply = co_await redis.lcs(key1, key2);
        EXPECT_TRUE(reply.ok()) << reply.error();
        EXPECT_EQ(reply.result(), "mytext"); // longest common subsequence

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}
