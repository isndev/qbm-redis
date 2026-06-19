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
#include "../redis.h"
#include "protocol_test_common.h"

using namespace qb::io;
using namespace std::chrono;
using namespace qb::redis;

// ============================================================================
// Fixture: all tests run in both RESP2 and RESP3
// ============================================================================

class ScriptingProtocolModesTest : public ProtocolModesTestBase {};

INSTANTIATE_PROTOCOL_MODES(ScriptingProtocolModesTest);

/*
 * COROUTINE TESTS
 */

TEST_P(ScriptingProtocolModesTest, CORO_SCRIPTING_COMMANDS_EVAL) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string              key        = protocol_key("eval");
        std::string              script     = "return redis.call('SET', KEYS[1], ARGV[1])";
        std::vector<std::string> keys       = {key};
        std::vector<std::string> args       = {"test_value"};
        auto                     eval_reply = co_await redis.eval<std::string>(script, keys, args);
        EXPECT_TRUE(eval_reply.ok());
        EXPECT_EQ(eval_reply.result(), "OK");
        auto get_reply = co_await redis.get(key);
        EXPECT_TRUE(get_reply.ok());
        EXPECT_TRUE(get_reply.result().has_value());
        EXPECT_EQ(*get_reply.result(), "test_value");
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

TEST_P(ScriptingProtocolModesTest, CORO_SCRIPTING_COMMANDS_EVAL_MULTIPLE) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string              key1       = protocol_key("eval1");
        std::string              key2       = protocol_key("eval2");
        std::string              script     = R"(
        redis.call('SET', KEYS[1], ARGV[1])
        redis.call('SET', KEYS[2], ARGV[2])
        return "OK"
    )";
        std::vector<std::string> keys       = {key1, key2};
        std::vector<std::string> args       = {"value1", "value2"};
        auto                     eval_reply = co_await redis.eval<std::string>(script, keys, args);
        EXPECT_TRUE(eval_reply.ok());
        EXPECT_EQ(eval_reply.result(), "OK");
        auto get1 = co_await redis.get(key1);
        auto get2 = co_await redis.get(key2);
        EXPECT_TRUE(get1.ok() && get2.ok());
        EXPECT_TRUE(get1.result().has_value() && get2.result().has_value());
        EXPECT_EQ(*get1.result(), "value1");
        EXPECT_EQ(*get2.result(), "value2");
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

TEST_P(ScriptingProtocolModesTest, CORO_SCRIPTING_COMMANDS_EVALSHA) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key        = protocol_key("evalsha");
        std::string script     = "return redis.call('SET', KEYS[1], ARGV[1])";
        auto        load_reply = co_await redis.script_load(script);
        EXPECT_TRUE(load_reply.ok());
        std::string sha = load_reply.result();
        EXPECT_FALSE(sha.empty());
        std::vector<std::string> keys          = {key};
        std::vector<std::string> args          = {"test_value"};
        auto                     evalsha_reply = co_await redis.evalsha<std::string>(sha, keys, args);
        EXPECT_TRUE(evalsha_reply.ok());
        EXPECT_EQ(evalsha_reply.result(), "OK");
        auto get_reply = co_await redis.get(key);
        EXPECT_TRUE(get_reply.ok());
        EXPECT_TRUE(get_reply.result().has_value());
        EXPECT_EQ(*get_reply.result(), "test_value");
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

TEST_P(ScriptingProtocolModesTest, CORO_SCRIPTING_COMMANDS_EXISTS) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string script     = "return redis.call('SET', KEYS[1], ARGV[1])";
        auto        load_reply = co_await redis.script_load(script);
        EXPECT_TRUE(load_reply.ok());
        std::string sha = load_reply.result();
        EXPECT_FALSE(sha.empty());
        auto exists_reply = co_await redis.script_exists(sha);
        EXPECT_TRUE(exists_reply.ok());
        EXPECT_EQ(exists_reply.result().size(), 1);
        EXPECT_TRUE(exists_reply.result()[0]);
        auto invalid_reply = co_await redis.script_exists("invalid_sha");
        EXPECT_TRUE(invalid_reply.ok());
        EXPECT_EQ(invalid_reply.result().size(), 1);
        EXPECT_FALSE(invalid_reply.result()[0]);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

TEST_P(ScriptingProtocolModesTest, CORO_SCRIPTING_COMMANDS_FLUSH) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string script     = "return redis.call('SET', KEYS[1], ARGV[1])";
        auto        load_reply = co_await redis.script_load(script);
        EXPECT_TRUE(load_reply.ok());
        std::string sha = load_reply.result();
        EXPECT_FALSE(sha.empty());
        auto exists1_reply = co_await redis.script_exists(sha);
        EXPECT_TRUE(exists1_reply.ok());
        EXPECT_EQ(exists1_reply.result().size(), 1);
        EXPECT_TRUE(exists1_reply.result()[0]);
        auto flush_reply = co_await redis.script_flush();
        EXPECT_TRUE(flush_reply.ok());
        auto exists2_reply = co_await redis.script_exists(sha);
        EXPECT_TRUE(exists2_reply.ok());
        EXPECT_EQ(exists2_reply.result().size(), 1);
        EXPECT_FALSE(exists2_reply.result()[0]);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

TEST_P(ScriptingProtocolModesTest, CORO_SCRIPTING_COMMANDS_KILL) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        try {
            auto kill_reply = co_await redis.script_kill();
            (void) kill_reply;
        } catch (...) {
        }
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test EVAL_RO (read-only EVAL - script cannot perform writes)
TEST_P(ScriptingProtocolModesTest, CORO_SCRIPTING_COMMANDS_EVAL_RO) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("eval_ro");
        (void) co_await redis.set(key, "existing");
        // Read-only script: GET only, no writes
        std::string              script = "return redis.call('GET', KEYS[1])";
        std::vector<std::string> keys   = {key};
        auto                     reply  = co_await redis.evalRo<std::string>(script, keys);
        EXPECT_TRUE(reply.ok());
        EXPECT_EQ(reply.result(), "existing");
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);
}

// Test EVALSHA_RO (read-only EVALSHA)
TEST_P(ScriptingProtocolModesTest, CORO_SCRIPTING_COMMANDS_EVALSHA_RO) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("evalsha_ro");
        (void) co_await redis.set(key, "value");
        std::string script = "return redis.call('GET', KEYS[1])";
        auto        load_r = co_await redis.script_load(script);
        EXPECT_TRUE(load_r.ok());
        std::vector<std::string> keys  = {key};
        auto                     reply = co_await redis.evalshaRo<std::string>(load_r.result(), keys);
        EXPECT_TRUE(reply.ok());
        EXPECT_EQ(reply.result(), "value");
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);
}

// Test SCRIPT DEBUG (set debug mode)
TEST_P(ScriptingProtocolModesTest, CORO_SCRIPTING_COMMANDS_SCRIPT_DEBUG) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto reply = co_await redis.scriptDebug("NO");
        EXPECT_TRUE(reply.ok());
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);
}

// Test async EVAL operation
TEST_P(ScriptingProtocolModesTest, CORO_SCRIPTING_COMMANDS_ERROR) {
    bool completed = false;
    bool got_error = false;
    auto test_task = [this, &completed, &got_error]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string              key        = protocol_key("error");
        std::string              script     = "error('This is a test error')";
        std::vector<std::string> keys       = {key};
        auto                     eval_reply = co_await redis.eval<std::string>(script, keys);
        got_error                           = !eval_reply.ok();
        completed                           = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
    EXPECT_TRUE(got_error);
}

TEST_P(ScriptingProtocolModesTest, CORO_SCRIPTING_COMMANDS_MULTIPLE_TYPES) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string              key        = protocol_key("types");
        std::string              set_script = R"(
        return redis.call('SET', KEYS[1], ARGV[1])
    )";
        std::vector<std::string> keys       = {key};
        std::vector<std::string> args       = {"test_value"};
        auto                     set_reply  = co_await redis.eval<std::string>(set_script, keys, args);
        EXPECT_TRUE(set_reply.ok());
        EXPECT_EQ(set_reply.result(), "OK");
        auto get_reply = co_await redis.get(key);
        EXPECT_TRUE(get_reply.ok());
        EXPECT_TRUE(get_reply.result().has_value());
        EXPECT_EQ(*get_reply.result(), "test_value");
        auto num_reply = co_await redis.eval<long long>("return 42");
        EXPECT_TRUE(num_reply.ok());
        EXPECT_EQ(num_reply.result(), 42);
        auto bool_reply = co_await redis.eval<bool>("return true");
        EXPECT_TRUE(bool_reply.ok());
        EXPECT_TRUE(bool_reply.result());
        auto arr_reply = co_await redis.eval<std::vector<long long>>("return {1, 2, 3}");
        EXPECT_TRUE(arr_reply.ok());
        EXPECT_EQ(arr_reply.result().size(), 3);
        EXPECT_EQ(arr_reply.result()[0], 1);
        EXPECT_EQ(arr_reply.result()[1], 2);
        EXPECT_EQ(arr_reply.result()[2], 3);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

TEST_P(ScriptingProtocolModesTest, CORO_SCRIPTING_COMMANDS_ATOMIC) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key1   = protocol_key("atomic1");
        std::string key2   = protocol_key("atomic2");
        std::string script = R"(
        local val1 = redis.call('GET', KEYS[1])
        local val2 = redis.call('GET', KEYS[2])
        if val1 == nil or val2 == nil then
            return false
        end
        redis.call('SET', KEYS[1], val2)
        redis.call('SET', KEYS[2], val1)
        return true
    )";
        (void) co_await redis.set(key1, "value1");
        (void) co_await redis.set(key2, "value2");
        std::vector<std::string> keys       = {key1, key2};
        auto                     eval_reply = co_await redis.eval<bool>(script, keys);
        EXPECT_TRUE(eval_reply.ok());
        EXPECT_TRUE(eval_reply.result());
        auto get1 = co_await redis.get(key1);
        auto get2 = co_await redis.get(key2);
        EXPECT_TRUE(get1.ok() && get2.ok());
        EXPECT_TRUE(get1.result().has_value() && get2.result().has_value());
        EXPECT_EQ(*get1.result(), "value2");
        EXPECT_EQ(*get2.result(), "value1");
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

TEST_P(ScriptingProtocolModesTest, CORO_SCRIPTING_COMMANDS_CONDITIONAL) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key    = protocol_key("conditional");
        std::string script = R"(
        local val = redis.call('GET', KEYS[1])
        if val == ARGV[1] then
            redis.call('SET', KEYS[1], ARGV[2])
            return 1
        end
        return 0
    )";
        (void) co_await redis.set(key, "initial");
        std::vector<std::string> keys     = {key};
        std::vector<std::string> args     = {"initial", "updated"};
        auto                     r1_reply = co_await redis.eval<bool>(script, keys, args);
        EXPECT_TRUE(r1_reply.ok());
        EXPECT_TRUE(r1_reply.result());
        auto get1 = co_await redis.get(key);
        EXPECT_TRUE(get1.ok());
        EXPECT_TRUE(get1.result().has_value());
        EXPECT_EQ(*get1.result(), "updated");
        args          = {"wrong", "not_updated"};
        auto r2_reply = co_await redis.eval<bool>(script, keys, args);
        EXPECT_TRUE(r2_reply.ok());
        EXPECT_FALSE(r2_reply.result());
        auto get2 = co_await redis.get(key);
        EXPECT_TRUE(get2.ok());
        EXPECT_TRUE(get2.result().has_value());
        EXPECT_EQ(*get2.result(), "updated");
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

TEST_P(ScriptingProtocolModesTest, EVAL) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto r = co_await redis.eval<long long>("return 42");
        EXPECT_TRUE(r.ok()) << r.error();
        if (r.ok())
            EXPECT_EQ(r.result(), 42);
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(ScriptingProtocolModesTest, EVAL_WITH_KEYS_ARGS) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto        k      = protocol_key("eval_key");
        std::string script = "return redis.call('SET', KEYS[1], ARGV[1])";
        auto        r      = co_await redis.eval<std::string>(script, {k}, {"value"});
        EXPECT_TRUE(r.ok()) << r.error();
        if (r.ok())
            EXPECT_EQ(r.result(), "OK");
        auto get_r = co_await redis.get(k);
        EXPECT_TRUE(get_r.ok() && get_r.result() && *get_r.result() == "value");
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(ScriptingProtocolModesTest, SCRIPT_LOAD_EVALSHA) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        std::string script = "return 123";
        auto        load_r = co_await redis.script_load(script);
        EXPECT_TRUE(load_r.ok()) << load_r.error();
        if (!load_r.ok()) {
            done = true;
            co_return;
        }
        auto sha       = load_r.result();
        auto evalsha_r = co_await redis.evalsha<long long>(sha);
        EXPECT_TRUE(evalsha_r.ok()) << evalsha_r.error();
        if (evalsha_r.ok())
            EXPECT_EQ(evalsha_r.result(), 123);
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