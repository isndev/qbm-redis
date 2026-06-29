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
 * @file integration/transaction/transaction-multi-exec.cpp
 * @brief Live MULTI/EXEC/DISCARD/WATCH transaction semantics for the redis client.
 *
 * Integration tier (`REQUIRES live`). Every test runs in both RESP2 and RESP3 via
 * the shared @ref qb::redis::test::ProtocolModesTestBase fixture. Covers the
 * optimistic-locking surface (WATCH abort / success / unwatch-release) with a real
 * second concurrent writer, asserts the per-command QUEUED acknowledgements inside a
 * MULTI block, and the protocol-level edge cases EXEC-without-MULTI, nested MULTI and
 * an exec-time partial failure.
 *
 * Migrated from test-transaction-commands.cpp. The MULTI_EXEC smoke duplicate was
 * dropped (strict subset of MULTI_EXEC below); the two WATCH-empty argument-validation
 * cases were carved to unit/commands/transaction-args-validation.cpp (daemon-free).
 */

#include <gtest/gtest.h>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include "../../shared/redis_integration_fixture.h"
#include "../redis.h"

using namespace qb::io;
using namespace std::chrono;

namespace {

using qb::redis::test::ProtocolModesTestBase;

class TransactionTest : public ProtocolModesTestBase {};

INSTANTIATE_PROTOCOL_MODES(TransactionTest);

// ---------------------------------------------------------------------------
// MULTI / EXEC happy path — assert per-command QUEUED acks + EXEC vector.
// ---------------------------------------------------------------------------
TEST_P(TransactionTest, MULTI_EXEC) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key1 = protocol_key("multi_exec1");
        std::string key2 = protocol_key("multi_exec2");

        auto multi_reply = co_await redis.multi();
        EXPECT_TRUE(multi_reply.ok()) << multi_reply.error();
        EXPECT_TRUE(redis.is_in_multi());

        // Each command issued inside MULTI is acknowledged with a simple "+QUEUED",
        // not actually executed. Assert the ack (the old test discarded these).
        auto q1 = co_await redis.set(key1, "value1");
        EXPECT_TRUE(q1.ok()) << q1.error();
        EXPECT_EQ(q1.result().str(), "QUEUED");
        auto q2 = co_await redis.set(key2, "value2");
        EXPECT_TRUE(q2.ok()) << q2.error();
        EXPECT_EQ(q2.result().str(), "QUEUED");

        // EXEC runs the queued commands atomically and returns their replies in order.
        auto exec_reply = co_await redis.exec<std::string>();
        EXPECT_TRUE(exec_reply.ok()) << exec_reply.error();
        if (!(exec_reply.result().size() == 2u)) {
            ADD_FAILURE() << "precondition failed: exec_reply.result().size() == 2u";
            co_return;
        }
        EXPECT_EQ(exec_reply.result()[0], "OK");
        EXPECT_EQ(exec_reply.result()[1], "OK");
        EXPECT_FALSE(redis.is_in_multi());

        // Side effects are visible only after EXEC.
        auto value1_reply = co_await redis.get(key1);
        auto value2_reply = co_await redis.get(key2);
        EXPECT_TRUE(value1_reply.ok());
        EXPECT_TRUE(value2_reply.ok());
        if (!(value1_reply.result().has_value())) {
            ADD_FAILURE() << "precondition failed: value1_reply.result().has_value()";
            co_return;
        }
        if (!(value2_reply.result().has_value())) {
            ADD_FAILURE() << "precondition failed: value2_reply.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*value1_reply.result(), "value1");
        EXPECT_EQ(*value2_reply.result(), "value2");

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// ---------------------------------------------------------------------------
// DISCARD — queued command must not apply; transaction state reusable after.
// ---------------------------------------------------------------------------
TEST_P(TransactionTest, DISCARD) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("discard");

        auto multi_reply = co_await redis.multi();
        EXPECT_TRUE(multi_reply.ok()) << multi_reply.error();
        EXPECT_TRUE(redis.is_in_multi());

        auto queued = co_await redis.set(key, "value");
        EXPECT_TRUE(queued.ok()) << queued.error();
        EXPECT_EQ(queued.result().str(), "QUEUED");

        auto discard_reply = co_await redis.discard();
        EXPECT_TRUE(discard_reply.ok()) << discard_reply.error();
        EXPECT_FALSE(redis.is_in_multi());

        // The queued SET was thrown away.
        auto value_reply = co_await redis.get(key);
        EXPECT_TRUE(value_reply.ok());
        EXPECT_FALSE(value_reply.result().has_value());

        // State is reusable: a new MULTI can be opened.
        auto multi2_reply = co_await redis.multi();
        EXPECT_TRUE(multi2_reply.ok()) << multi2_reply.error();
        EXPECT_TRUE(redis.is_in_multi());
        auto discard2 = co_await redis.discard();
        EXPECT_TRUE(discard2.ok()) << discard2.error();

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// ---------------------------------------------------------------------------
// WATCH + concurrent writer → EXEC aborts (optimistic lock lost).
// ---------------------------------------------------------------------------
TEST_P(TransactionTest, WATCH_ABORTS_ON_CONCURRENT_WRITE) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("watch_abort");

        auto watch_reply = co_await redis.watch(key);
        EXPECT_TRUE(watch_reply.ok()) << watch_reply.error();

        // A second, independent connection mutates the watched key.
        qb::redis::tcp::client other_client{qb::redis::test::redis_test_uri()};
        EXPECT_TRUE(co_await other_client.connect());
        auto set_reply = co_await other_client.set(key, "modified");
        EXPECT_TRUE(set_reply.ok()) << set_reply.error();

        auto multi_reply = co_await redis.multi();
        EXPECT_TRUE(multi_reply.ok()) << multi_reply.error();
        auto queued = co_await redis.set(key, "new_value");
        EXPECT_EQ(queued.result().str(), "QUEUED");

        // EXEC returns nil (aborted) when a watched key changed → ok() == false.
        auto exec_reply = co_await redis.exec<std::string>();
        EXPECT_FALSE(exec_reply.ok());
        EXPECT_FALSE(redis.is_in_multi());

        // The concurrent writer's value won.
        auto value_reply = co_await redis.get(key);
        EXPECT_TRUE(value_reply.ok());
        if (!(value_reply.result().has_value())) {
            ADD_FAILURE() << "precondition failed: value_reply.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*value_reply.result(), "modified");

        auto unwatch_reply = co_await redis.unwatch();
        EXPECT_TRUE(unwatch_reply.ok()) << unwatch_reply.error();

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// ---------------------------------------------------------------------------
// WATCH multiple keys; one mutated → EXEC aborts, both keys keep prior values.
// ---------------------------------------------------------------------------
TEST_P(TransactionTest, WATCH_MULTIPLE_ABORTS) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key1 = protocol_key("watch_multi1");
        std::string key2 = protocol_key("watch_multi2");

        EXPECT_TRUE((co_await redis.set(key1, "initial1")).ok());
        EXPECT_TRUE((co_await redis.set(key2, "initial2")).ok());

        auto watch_reply = co_await redis.watch({key1, key2});
        EXPECT_TRUE(watch_reply.ok()) << watch_reply.error();

        qb::redis::tcp::client other_client{qb::redis::test::redis_test_uri()};
        EXPECT_TRUE(co_await other_client.connect());
        auto set_reply = co_await other_client.set(key1, "modified1");
        EXPECT_TRUE(set_reply.ok()) << set_reply.error();

        auto multi_reply = co_await redis.multi();
        EXPECT_TRUE(multi_reply.ok()) << multi_reply.error();
        EXPECT_EQ((co_await redis.set(key1, "new_value1")).result().str(), "QUEUED");
        EXPECT_EQ((co_await redis.set(key2, "new_value2")).result().str(), "QUEUED");

        auto exec_reply = co_await redis.exec<std::string>();
        EXPECT_FALSE(exec_reply.ok());

        auto value1_reply = co_await redis.get(key1);
        auto value2_reply = co_await redis.get(key2);
        if (!(value1_reply.result().has_value())) {
            ADD_FAILURE() << "precondition failed: value1_reply.result().has_value()";
            co_return;
        }
        if (!(value2_reply.result().has_value())) {
            ADD_FAILURE() << "precondition failed: value2_reply.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*value1_reply.result(), "modified1"); // other client won
        EXPECT_EQ(*value2_reply.result(), "initial2");  // untouched, pre-MULTI value

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// ---------------------------------------------------------------------------
// WATCH with no concurrent write → EXEC commits.
// ---------------------------------------------------------------------------
TEST_P(TransactionTest, WATCH_SUCCESS) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key1 = protocol_key("watch_ok1");
        std::string key2 = protocol_key("watch_ok2");

        EXPECT_TRUE((co_await redis.set(key1, "initial1")).ok());
        EXPECT_TRUE((co_await redis.set(key2, "initial2")).ok());

        auto watch_reply = co_await redis.watch({key1, key2});
        EXPECT_TRUE(watch_reply.ok()) << watch_reply.error();

        auto multi_reply = co_await redis.multi();
        EXPECT_TRUE(multi_reply.ok()) << multi_reply.error();
        EXPECT_EQ((co_await redis.set(key1, "updated1")).result().str(), "QUEUED");
        EXPECT_EQ((co_await redis.set(key2, "updated2")).result().str(), "QUEUED");

        auto exec_reply = co_await redis.exec<std::string>();
        EXPECT_TRUE(exec_reply.ok()) << exec_reply.error();
        if (!(exec_reply.result().size() == 2u)) {
            ADD_FAILURE() << "precondition failed: exec_reply.result().size() == 2u";
            co_return;
        }
        EXPECT_EQ(exec_reply.result()[0], "OK");
        EXPECT_EQ(exec_reply.result()[1], "OK");

        auto value1_reply = co_await redis.get(key1);
        auto value2_reply = co_await redis.get(key2);
        if (!(value1_reply.result().has_value())) {
            ADD_FAILURE() << "precondition failed: value1_reply.result().has_value()";
            co_return;
        }
        if (!(value2_reply.result().has_value())) {
            ADD_FAILURE() << "precondition failed: value2_reply.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*value1_reply.result(), "updated1");
        EXPECT_EQ(*value2_reply.result(), "updated2");

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// ---------------------------------------------------------------------------
// UNWATCH releases the optimistic lock → later EXEC commits despite a
// concurrent write to the formerly-watched key.
// ---------------------------------------------------------------------------
TEST_P(TransactionTest, UNWATCH_THEN_EXEC) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("unwatch_exec");

        EXPECT_TRUE((co_await redis.set(key, "initial")).ok());

        auto watch_reply = co_await redis.watch(key);
        EXPECT_TRUE(watch_reply.ok()) << watch_reply.error();
        auto unwatch_reply = co_await redis.unwatch();
        EXPECT_TRUE(unwatch_reply.ok()) << unwatch_reply.error();

        // Mutation after UNWATCH must NOT abort the transaction.
        qb::redis::tcp::client other_client{qb::redis::test::redis_test_uri()};
        EXPECT_TRUE(co_await other_client.connect());
        auto set_reply = co_await other_client.set(key, "modified");
        EXPECT_TRUE(set_reply.ok()) << set_reply.error();

        auto multi_reply = co_await redis.multi();
        EXPECT_TRUE(multi_reply.ok()) << multi_reply.error();
        EXPECT_EQ((co_await redis.set(key, "new_value")).result().str(), "QUEUED");

        auto exec_reply = co_await redis.exec<std::string>();
        EXPECT_TRUE(exec_reply.ok()) << exec_reply.error();
        if (!(exec_reply.result().size() == 1u)) {
            ADD_FAILURE() << "precondition failed: exec_reply.result().size() == 1u";
            co_return;
        }
        EXPECT_EQ(exec_reply.result()[0], "OK");

        auto value_reply = co_await redis.get(key);
        if (!(value_reply.result().has_value())) {
            ADD_FAILURE() << "precondition failed: value_reply.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*value_reply.result(), "new_value");

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// ---------------------------------------------------------------------------
// Protocol edge cases (added):
//   - EXEC without a preceding MULTI is a server error.
//   - MULTI nested inside MULTI is rejected (the queued nested-MULTI ack errors).
//   - A command that errors at EXEC time fails just that entry while the others
//     still apply (Redis has no rollback on exec-time errors).
// ---------------------------------------------------------------------------
TEST_P(TransactionTest, EXEC_WITHOUT_MULTI_ERRORS) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        EXPECT_FALSE(redis.is_in_multi());

        auto exec_reply = co_await redis.exec<std::string>();
        EXPECT_FALSE(exec_reply.ok());
        EXPECT_NE(exec_reply.error().find("EXEC without MULTI"), std::string::npos) << "actual error: " << exec_reply.error();
        EXPECT_FALSE(redis.is_in_multi());

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

TEST_P(TransactionTest, NESTED_MULTI_REJECTED) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("nested_multi");

        auto multi_reply = co_await redis.multi();
        EXPECT_TRUE(multi_reply.ok()) << multi_reply.error();
        EXPECT_TRUE(redis.is_in_multi());

        // MULTI inside MULTI: server replies with an error, not "+QUEUED".
        auto nested = co_await redis.multi();
        EXPECT_FALSE(nested.ok());
        EXPECT_NE(nested.error().find("MULTI calls can not be nested"), std::string::npos) << "actual error: " << nested.error();

        // The first transaction is still open server-side: the rejected nested
        // MULTI does NOT abort it (confirmed via redis-cli — the queued command
        // still executes through EXEC). The framework's client-side hint must agree:
        // a failed MULTI no longer clobbers in_multi_ (transaction_commands.h only
        // sets the hint true on a successful MULTI), so the hint stays true through
        // the rejected nested call.
        EXPECT_TRUE(redis.is_in_multi());

        // Authoritative server-observable survival proof: the next command is QUEUED
        // (not executed inline) and EXEC then applies it.
        EXPECT_EQ((co_await redis.set(key, "v")).result().str(), "QUEUED");
        auto exec_reply = co_await redis.exec<std::string>();
        EXPECT_TRUE(exec_reply.ok()) << exec_reply.error();
        if (!(exec_reply.result().size() == 1u)) {
            ADD_FAILURE() << "precondition failed: exec_reply.result().size() == 1u";
            co_return;
        }
        EXPECT_EQ(exec_reply.result()[0], "OK");

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

TEST_P(TransactionTest, EXEC_TIME_ERROR_PARTIAL_FAILURE) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string str_key = protocol_key("partial_str");
        std::string ok_key  = protocol_key("partial_ok");

        // Seed a string value; INCR on it errors at EXEC time (WRONGTYPE-class).
        EXPECT_TRUE((co_await redis.set(str_key, "not_a_number")).ok());

        auto multi_reply = co_await redis.multi();
        EXPECT_TRUE(multi_reply.ok()) << multi_reply.error();

        // All three queue fine (syntactically valid) — the error only manifests at EXEC.
        // The two SETs are typed <status> so their "+QUEUED" ack parses cleanly; INCR is
        // typed <long long>, so its "+QUEUED" ack cannot be represented by the typed
        // parser (it is asserted at the wire level by the EXEC array below), hence we only
        // queue it here.
        EXPECT_EQ((co_await redis.set(ok_key, "first")).result().str(), "QUEUED");
        (void) co_await redis.incr(str_key);
        EXPECT_EQ((co_await redis.set(ok_key, "second")).result().str(), "QUEUED");

        // EXEC succeeds at the protocol level: the whole block runs and the server
        // returns a 3-element array. The failing INCR does NOT roll back the
        // surrounding SETs (Redis has no transactional rollback for exec-time errors).
        // Typed parsing of the homogeneous vector would throw on the error element, so
        // we inspect the raw EXEC array directly to see per-entry outcomes.
        auto        exec_reply = co_await redis.exec<std::string>();
        const auto &raw        = exec_reply.raw();
        if (!(raw != nullptr)) {
            ADD_FAILURE() << "precondition failed: raw != nullptr";
            co_return;
        }
        EXPECT_TRUE(raw->is_array());
        if (!(raw->as_array().size() == 3u)) {
            ADD_FAILURE() << "precondition failed: raw->as_array().size() == 3u";
            co_return;
        }
        // entry 0: SET ok_key "first" → +OK ; entry 1: INCR on string → -ERR ; entry 2: SET → +OK
        EXPECT_TRUE(raw->as_array()[0]->is_string());
        EXPECT_TRUE(raw->as_array()[1]->is_error()) << "middle command should have errored at exec time";
        EXPECT_TRUE(raw->as_array()[2]->is_string());

        // The non-failing commands still applied: last SET wins.
        auto value_reply = co_await redis.get(ok_key);
        if (!(value_reply.result().has_value())) {
            ADD_FAILURE() << "precondition failed: value_reply.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*value_reply.result(), "second");

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

} // namespace
