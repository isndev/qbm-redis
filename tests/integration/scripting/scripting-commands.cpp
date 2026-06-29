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
 * @file integration/scripting/scripting-commands.cpp
 * @brief Live Lua scripting surface for the redis client (EVAL / EVALSHA / SCRIPT *).
 *
 * Integration tier (`REQUIRES live`). Runs in both RESP2 and RESP3 via the shared
 * @ref qb::redis::test::ProtocolModesTestBase fixture.
 *
 * Migrated from test-scripting-commands.cpp. The three trailing thin smoke duplicates
 * (EVAL / EVAL_WITH_KEYS_ARGS / SCRIPT_LOAD_EVALSHA) were deleted (strict subsets of the
 * CORO_* bodies). All busy-spin loops were replaced by run_coro_test_until (watchdog).
 * KILL now asserts a concrete NOTBUSY outcome. Added: EVALSHA against an unknown SHA
 * (NOSCRIPT), read-only write rejection for evalRo/evalshaRo, and typed decode of a
 * nested table and the Lua error-table form.
 */

#include <gtest/gtest.h>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include "../../shared/redis_integration_fixture.h"
#include "../redis.h"

using namespace qb::io;
using namespace std::chrono;
using namespace qb::redis;

namespace {

using qb::redis::test::ProtocolModesTestBase;

class ScriptingTest : public ProtocolModesTestBase {};

INSTANTIATE_PROTOCOL_MODES(ScriptingTest);

// ---------------------------------------------------------------------------
// EVAL — write via script, verify side effect with an independent GET.
// ---------------------------------------------------------------------------
TEST_P(ScriptingTest, EVAL) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key    = protocol_key("eval");
        std::string script = "return redis.call('SET', KEYS[1], ARGV[1])";
        auto        reply  = co_await redis.eval<std::string>(script, {key}, {"test_value"});
        EXPECT_TRUE(reply.ok()) << reply.error();
        EXPECT_EQ(reply.result(), "OK");

        auto get_reply = co_await redis.get(key);
        EXPECT_TRUE(get_reply.ok());
        if (!(get_reply.result().has_value())) {
            ADD_FAILURE() << "precondition failed: get_reply.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*get_reply.result(), "test_value");
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

TEST_P(ScriptingTest, EVAL_MULTIPLE_KEYS) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key1   = protocol_key("eval1");
        std::string key2   = protocol_key("eval2");
        std::string script = R"(
            redis.call('SET', KEYS[1], ARGV[1])
            redis.call('SET', KEYS[2], ARGV[2])
            return "OK"
        )";
        auto        reply  = co_await redis.eval<std::string>(script, {key1, key2}, {"value1", "value2"});
        EXPECT_TRUE(reply.ok()) << reply.error();
        EXPECT_EQ(reply.result(), "OK");

        auto get1 = co_await redis.get(key1);
        auto get2 = co_await redis.get(key2);
        if (!(get1.result().has_value())) {
            ADD_FAILURE() << "precondition failed: get1.result().has_value()";
            co_return;
        }
        if (!(get2.result().has_value())) {
            ADD_FAILURE() << "precondition failed: get2.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*get1.result(), "value1");
        EXPECT_EQ(*get2.result(), "value2");
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// ---------------------------------------------------------------------------
// EVAL typed-decoder matrix: string / long long / bool / vector<long long>.
// ---------------------------------------------------------------------------
TEST_P(ScriptingTest, EVAL_RETURN_TYPES) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);

        auto num_reply = co_await redis.eval<long long>("return 42");
        EXPECT_TRUE(num_reply.ok()) << num_reply.error();
        EXPECT_EQ(num_reply.result(), 42);

        auto bool_reply = co_await redis.eval<bool>("return true");
        EXPECT_TRUE(bool_reply.ok()) << bool_reply.error();
        EXPECT_TRUE(bool_reply.result());

        auto false_reply = co_await redis.eval<bool>("return false");
        EXPECT_TRUE(false_reply.ok()) << false_reply.error();
        EXPECT_FALSE(false_reply.result());

        auto arr_reply = co_await redis.eval<std::vector<long long>>("return {1, 2, 3}");
        EXPECT_TRUE(arr_reply.ok()) << arr_reply.error();
        if (!(arr_reply.result().size() == 3u)) {
            ADD_FAILURE() << "precondition failed: arr_reply.result().size() == 3u";
            co_return;
        }
        EXPECT_EQ(arr_reply.result()[0], 1);
        EXPECT_EQ(arr_reply.result()[1], 2);
        EXPECT_EQ(arr_reply.result()[2], 3);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// ---------------------------------------------------------------------------
// EVALSHA — load, run by SHA, verify side effect.
// ---------------------------------------------------------------------------
TEST_P(ScriptingTest, EVALSHA) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key    = protocol_key("evalsha");
        std::string script = "return redis.call('SET', KEYS[1], ARGV[1])";

        auto load_reply = co_await redis.script_load(script);
        EXPECT_TRUE(load_reply.ok()) << load_reply.error();
        std::string sha = load_reply.result();
        if (!(sha.size() == 40u)) {
            ADD_FAILURE() << "SCRIPT LOAD must return a 40-char SHA1 hex digest";
            co_return;
        }

        auto reply = co_await redis.evalsha<std::string>(sha, {key}, {"test_value"});
        EXPECT_TRUE(reply.ok()) << reply.error();
        EXPECT_EQ(reply.result(), "OK");

        auto get_reply = co_await redis.get(key);
        if (!(get_reply.result().has_value())) {
            ADD_FAILURE() << "precondition failed: get_reply.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*get_reply.result(), "test_value");
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// ---------------------------------------------------------------------------
// EVALSHA against a SHA that was never loaded → NOSCRIPT error path. This is the
// most important failure mode of the load/cache flow and was previously untested.
// ---------------------------------------------------------------------------
TEST_P(ScriptingTest, EVALSHA_UNKNOWN_SHA_NOSCRIPT) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        // A syntactically valid but never-loaded 40-char SHA1.
        const std::string unknown_sha = "0000000000000000000000000000000000000000";
        auto              reply       = co_await redis.evalsha<std::string>(unknown_sha);
        EXPECT_FALSE(reply.ok());
        EXPECT_NE(reply.error().find("NOSCRIPT"), std::string::npos) << "actual error: " << reply.error();
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// ---------------------------------------------------------------------------
// SCRIPT EXISTS — true for a loaded SHA, false for garbage.
// ---------------------------------------------------------------------------
TEST_P(ScriptingTest, SCRIPT_EXISTS) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto load_reply = co_await redis.script_load("return redis.call('SET', KEYS[1], ARGV[1])");
        EXPECT_TRUE(load_reply.ok()) << load_reply.error();
        std::string sha = load_reply.result();
        if (!(sha.size() == 40u)) {
            ADD_FAILURE() << "precondition failed: sha.size() == 40u";
            co_return;
        }

        auto exists_reply = co_await redis.script_exists(sha);
        EXPECT_TRUE(exists_reply.ok()) << exists_reply.error();
        if (!(exists_reply.result().size() == 1u)) {
            ADD_FAILURE() << "precondition failed: exists_reply.result().size() == 1u";
            co_return;
        }
        EXPECT_TRUE(exists_reply.result()[0]);

        auto invalid_reply = co_await redis.script_exists("invalid_sha");
        EXPECT_TRUE(invalid_reply.ok()) << invalid_reply.error();
        if (!(invalid_reply.result().size() == 1u)) {
            ADD_FAILURE() << "precondition failed: invalid_reply.result().size() == 1u";
            co_return;
        }
        EXPECT_FALSE(invalid_reply.result()[0]);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// ---------------------------------------------------------------------------
// SCRIPT FLUSH — exists==true → flush → exists==false (real state transition).
// ---------------------------------------------------------------------------
TEST_P(ScriptingTest, SCRIPT_FLUSH) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto load_reply = co_await redis.script_load("return 1");
        EXPECT_TRUE(load_reply.ok()) << load_reply.error();
        std::string sha = load_reply.result();
        if (!(sha.size() == 40u)) {
            ADD_FAILURE() << "precondition failed: sha.size() == 40u";
            co_return;
        }

        auto exists1 = co_await redis.script_exists(sha);
        if (!(exists1.result().size() == 1u)) {
            ADD_FAILURE() << "precondition failed: exists1.result().size() == 1u";
            co_return;
        }
        EXPECT_TRUE(exists1.result()[0]);

        auto flush_reply = co_await redis.script_flush();
        EXPECT_TRUE(flush_reply.ok()) << flush_reply.error();

        auto exists2 = co_await redis.script_exists(sha);
        if (!(exists2.result().size() == 1u)) {
            ADD_FAILURE() << "precondition failed: exists2.result().size() == 1u";
            co_return;
        }
        EXPECT_FALSE(exists2.result()[0]);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// ---------------------------------------------------------------------------
// SCRIPT KILL — with no busy script the server returns NOTBUSY. Assert that
// concrete outcome (the old test swallowed every result and asserted nothing).
// ---------------------------------------------------------------------------
TEST_P(ScriptingTest, SCRIPT_KILL_NOTBUSY_WHEN_IDLE) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto kill_reply = co_await redis.script_kill();
        // No script is currently running on this fresh connection, so SCRIPT KILL
        // must fail with -NOTBUSY rather than report success.
        EXPECT_FALSE(kill_reply.ok());
        EXPECT_NE(kill_reply.error().find("NOTBUSY"), std::string::npos) << "actual error: " << kill_reply.error();
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// ---------------------------------------------------------------------------
// EVAL_RO — a read-only script executes; a write inside it is rejected. The
// rejection proves the RO enforcement (a plain GET would pass identically under
// non-RO EVAL, so the old RO tests never actually exercised the "RO" guarantee).
// ---------------------------------------------------------------------------
TEST_P(ScriptingTest, EVAL_RO_ALLOWS_READ_REJECTS_WRITE) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("eval_ro");
        EXPECT_TRUE((co_await redis.set(key, "existing")).ok());

        // Read path: allowed.
        auto read = co_await redis.evalRo<std::string>("return redis.call('GET', KEYS[1])", {key});
        EXPECT_TRUE(read.ok()) << read.error();
        EXPECT_EQ(read.result(), "existing");

        // Write path under EVAL_RO: must be rejected by the server.
        auto write = co_await redis.evalRo<std::string>("return redis.call('SET', KEYS[1], ARGV[1])", {key}, {"mutated"});
        EXPECT_FALSE(write.ok()) << "EVAL_RO must reject a write command, but it succeeded";

        // The value is unchanged.
        auto check = co_await redis.get(key);
        if (!(check.result().has_value())) {
            ADD_FAILURE() << "precondition failed: check.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*check.result(), "existing");
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

TEST_P(ScriptingTest, EVALSHA_RO_REJECTS_WRITE) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("evalsha_ro");
        EXPECT_TRUE((co_await redis.set(key, "value")).ok());

        // Read path via SHA: allowed.
        auto load_read = co_await redis.script_load("return redis.call('GET', KEYS[1])");
        if (!(load_read.result().size() == 40u)) {
            ADD_FAILURE() << "precondition failed: load_read.result().size() == 40u";
            co_return;
        }
        auto read = co_await redis.evalshaRo<std::string>(load_read.result(), {key});
        EXPECT_TRUE(read.ok()) << read.error();
        EXPECT_EQ(read.result(), "value");

        // Write path via SHA under RO: rejected.
        auto load_write = co_await redis.script_load("return redis.call('SET', KEYS[1], ARGV[1])");
        if (!(load_write.result().size() == 40u)) {
            ADD_FAILURE() << "precondition failed: load_write.result().size() == 40u";
            co_return;
        }
        auto write = co_await redis.evalshaRo<std::string>(load_write.result(), {key}, {"mutated"});
        EXPECT_FALSE(write.ok()) << "EVALSHA_RO must reject a write command, but it succeeded";

        auto check = co_await redis.get(key);
        if (!(check.result().has_value())) {
            ADD_FAILURE() << "precondition failed: check.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*check.result(), "value");
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// ---------------------------------------------------------------------------
// SCRIPT DEBUG — beyond .ok(): assert each documented mode is accepted, and that
// an invalid mode is rejected. (We do not enter an interactive debug session.)
// ---------------------------------------------------------------------------
TEST_P(ScriptingTest, SCRIPT_DEBUG_MODES) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        // SYNC and YES enable debugging; NO turns it back off. All three are valid.
        auto sync_reply = co_await redis.scriptDebug("SYNC");
        EXPECT_TRUE(sync_reply.ok()) << sync_reply.error();
        auto no_reply = co_await redis.scriptDebug("NO");
        EXPECT_TRUE(no_reply.ok()) << no_reply.error();

        // An unknown mode is a server-side error.
        auto bad_reply = co_await redis.scriptDebug("BOGUS");
        EXPECT_FALSE(bad_reply.ok());
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// ---------------------------------------------------------------------------
// EVAL error path — a Lua error(...) surfaces as a failed Reply with the message.
// ---------------------------------------------------------------------------
TEST_P(ScriptingTest, EVAL_LUA_ERROR) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key   = protocol_key("error");
        auto        reply = co_await redis.eval<std::string>("error('This is a test error')", {key});
        EXPECT_FALSE(reply.ok());
        EXPECT_NE(reply.error().find("This is a test error"), std::string::npos) << "actual error: " << reply.error();
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// ---------------------------------------------------------------------------
// EVAL returning an explicit Lua error-table {err = "..."} → failed Reply that
// carries the table's message (hardens the typed error decode path).
// ---------------------------------------------------------------------------
TEST_P(ScriptingTest, EVAL_ERROR_TABLE_DECODE) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto reply = co_await redis.eval<std::string>("return {err = 'CUSTOM custom error'}");
        EXPECT_FALSE(reply.ok());
        EXPECT_NE(reply.error().find("CUSTOM"), std::string::npos) << "actual error: " << reply.error();
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// ---------------------------------------------------------------------------
// EVAL returning a nested / heterogeneous table → decode element-by-element via
// the raw reply array (hardens the typed decoder against mixed shapes).
// ---------------------------------------------------------------------------
TEST_P(ScriptingTest, EVAL_NESTED_TABLE_DECODE) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        // Top-level array of: integer, status-string, nested array of two integers.
        auto reply = co_await redis.eval<std::string>("return {1, 'two', {3, 4}}");
        // Heterogeneous: a homogeneous typed parse would throw, so inspect the raw array.
        const auto &raw = reply.raw();
        if (!(raw != nullptr)) {
            ADD_FAILURE() << "precondition failed: raw != nullptr";
            co_return;
        }
        EXPECT_TRUE(raw->is_array());
        if (!(raw->as_array().size() == 3u)) {
            ADD_FAILURE() << "precondition failed: raw->as_array().size() == 3u";
            co_return;
        }
        EXPECT_TRUE(raw->as_array()[0]->is_integer());
        EXPECT_EQ(raw->as_array()[0]->as_integer().value, 1);
        EXPECT_TRUE(raw->as_array()[1]->is_string());
        EXPECT_EQ(raw->as_array()[1]->as_string_view(), "two");
        EXPECT_TRUE(raw->as_array()[2]->is_array());
        if (!(raw->as_array()[2]->as_array().size() == 2u)) {
            ADD_FAILURE() << "precondition failed: raw->as_array()[2]->as_array().size() == 2u";
            co_return;
        }
        EXPECT_EQ(raw->as_array()[2]->as_array()[0]->as_integer().value, 3);
        EXPECT_EQ(raw->as_array()[2]->as_array()[1]->as_integer().value, 4);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// ---------------------------------------------------------------------------
// Atomic swap inside a single script.
// ---------------------------------------------------------------------------
TEST_P(ScriptingTest, EVAL_ATOMIC_SWAP) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key1   = protocol_key("atomic1");
        std::string key2   = protocol_key("atomic2");
        std::string script = R"(
            local val1 = redis.call('GET', KEYS[1])
            local val2 = redis.call('GET', KEYS[2])
            if val1 == false or val2 == false then return false end
            redis.call('SET', KEYS[1], val2)
            redis.call('SET', KEYS[2], val1)
            return true
        )";
        EXPECT_TRUE((co_await redis.set(key1, "value1")).ok());
        EXPECT_TRUE((co_await redis.set(key2, "value2")).ok());

        auto reply = co_await redis.eval<bool>(script, {key1, key2});
        EXPECT_TRUE(reply.ok()) << reply.error();
        EXPECT_TRUE(reply.result());

        auto get1 = co_await redis.get(key1);
        auto get2 = co_await redis.get(key2);
        if (!(get1.result().has_value())) {
            ADD_FAILURE() << "precondition failed: get1.result().has_value()";
            co_return;
        }
        if (!(get2.result().has_value())) {
            ADD_FAILURE() << "precondition failed: get2.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*get1.result(), "value2");
        EXPECT_EQ(*get2.result(), "value1");
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// ---------------------------------------------------------------------------
// Compare-and-set: both the matching (update) and non-matching (no-op) branches.
// ---------------------------------------------------------------------------
TEST_P(ScriptingTest, EVAL_COMPARE_AND_SET) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key    = protocol_key("cas");
        std::string script = R"(
            local val = redis.call('GET', KEYS[1])
            if val == ARGV[1] then
                redis.call('SET', KEYS[1], ARGV[2])
                return 1
            end
            return 0
        )";
        EXPECT_TRUE((co_await redis.set(key, "initial")).ok());

        auto matched = co_await redis.eval<bool>(script, {key}, {"initial", "updated"});
        EXPECT_TRUE(matched.ok()) << matched.error();
        EXPECT_TRUE(matched.result());
        EXPECT_EQ(*(co_await redis.get(key)).result(), "updated");

        auto unmatched = co_await redis.eval<bool>(script, {key}, {"wrong", "not_applied"});
        EXPECT_TRUE(unmatched.ok()) << unmatched.error();
        EXPECT_FALSE(unmatched.result());
        EXPECT_EQ(*(co_await redis.get(key)).result(), "updated"); // unchanged
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

} // namespace
