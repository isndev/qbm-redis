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
 * @file integration/admin/function-commands.cpp
 * @brief Live RESP2/RESP3 integration tests for the FUNCTION surface of `qb::redis::tcp::client`.
 *
 * The legacy monolith was the worst tolerance offender: every body was `if(ok){weak}else{
 * find("function")}` + a catch re-applying the same match, so a server WITH and WITHOUT FUNCTION
 * both passed. Here FUNCTION (Redis >= 7.0) is probed ONCE in SetUp and the suite is
 * `GTEST_SKIP`-ped when absent, then the happy path is exercised end-to-end:
 *   load a real Lua library → FCALL returns the expected value → FCALL_RO on a no-writes function
 *   → FUNCTION LIST shows the library → FUNCTION STATS reports the LUA engine → FUNCTION DELETE →
 *   FCALL now fails with "Function not found".
 * The negative-only verbs (LOAD invalid, DELETE missing, KILL when idle, RESTORE invalid) keep
 * their deterministic error assertions. Deleted: FUNCTION_LIST_JSON dup + the dangling
 * "// Test async" comment; the identical FCALL/FCALL_RO error bodies are merged into the flow.
 */

#include <gtest/gtest.h>
#include <string>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include "../redis.h"
#include "../../shared/redis_integration_fixture.h"

// ProtocolMode / ProtocolModesTestBase / macros from redis_integration_fixture.h (global re-export).

namespace {

class FunctionTest : public ProtocolModesTestBase {
protected:
    void
    SetUp() override {
        ProtocolModesTestBase::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        // FUNCTION exists since Redis 7.0; a non-ok FUNCTION LIST means it is unavailable.
        auto probe = qb::io::async::run_sync(redis.function_list());
        if (!probe.ok())
            GTEST_SKIP() << "FUNCTION unavailable (pre-Redis-7 server): " << probe.error();
        // Start from a clean function namespace.
        (void) qb::io::async::run_sync(redis.function_flush());
    }
};

INSTANTIATE_PROTOCOL_MODES(FunctionTest);

// A Lua library with one normal and one no-writes function, used by the positive flow.
constexpr const char *kLibCode =
    "#!lua name=qbtestlib\n"
    "redis.register_function('qbtest_echo', function(keys, args) return args[1] end)\n"
    "redis.register_function{function_name='qbtest_roecho', "
    "callback=function(keys, args) return args[1] end, flags={'no-writes'}}\n";

// =============== POSITIVE END-TO-END FLOW ===============

TEST_P(FunctionTest, LoadFcallListStatsDelete) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);

        // LOAD returns the library name.
        auto load = co_await redis.function_load(kLibCode);
        EXPECT_TRUE(load.ok()) << load.error();
        EXPECT_EQ(std::string(load.result()), "qbtestlib");

        // FCALL the normal function: echoes its single arg.
        auto called = co_await redis.fcall<std::string>("qbtest_echo", {}, {"hello"});
        EXPECT_TRUE(called.ok()) << called.error();
        EXPECT_EQ(called.result(), "hello");

        // FCALL_RO the no-writes function: echoes too, but is allowed on a read-only path.
        auto called_ro = co_await redis.fcallRo<std::string>("qbtest_roecho", {}, {"world"});
        EXPECT_TRUE(called_ro.ok()) << called_ro.error();
        EXPECT_EQ(called_ro.result(), "world");

        // FUNCTION LIST shows the loaded library by name.
        auto list = co_await redis.function_list();
        EXPECT_TRUE(list.ok()) << list.error();
        EXPECT_NE(list.result().dump().find("qbtestlib"), std::string::npos) << list.result().dump();

        // FUNCTION STATS reports the LUA engine.
        auto stats = co_await redis.function_stats();
        EXPECT_TRUE(stats.ok()) << stats.error();
        EXPECT_TRUE(stats.result().is_object());
        EXPECT_TRUE(stats.result().contains("engines"));
        EXPECT_NE(stats.result()["engines"].dump().find("LUA"), std::string::npos)
            << stats.result()["engines"].dump();

        // DELETE the library, then FCALL must now fail with "Function not found".
        auto del = co_await redis.function_delete("qbtestlib");
        EXPECT_TRUE(del.ok()) << del.error();

        auto gone = co_await redis.fcall<std::string>("qbtest_echo", {}, {"x"});
        EXPECT_FALSE(gone.ok());
        if (!gone.ok())
            EXPECT_NE(std::string(gone.error()).find("Function not found"), std::string::npos)
                << gone.error();

        completed = true;
    });
    run_coro_test_until(completed);
}

// FUNCTION DUMP returns the serialized payload; after FLUSH the namespace is empty and LIST is [].
TEST_P(FunctionTest, DumpAndFlush) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);

        auto load = co_await redis.function_load(kLibCode);
        EXPECT_TRUE(load.ok()) << load.error();

        auto dump = co_await redis.function_dump();
        EXPECT_TRUE(dump.ok()) << dump.error();
        EXPECT_TRUE(dump.result().is_string() || dump.result().is_binary());

        auto flush = co_await redis.function_flush();
        EXPECT_TRUE(flush.ok()) << flush.error();
        EXPECT_EQ(std::string(flush.result()), "OK");

        auto list = co_await redis.function_list();
        EXPECT_TRUE(list.ok()) << list.error();
        EXPECT_TRUE(list.result().is_array());
        EXPECT_TRUE(list.result().empty());

        completed = true;
    });
    run_coro_test_until(completed);
}

// =============== DETERMINISTIC NEGATIVE PATHS ===============

TEST_P(FunctionTest, LoadInvalidCodeFails) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);

        auto reply = co_await redis.function_load("invalid function code");
        EXPECT_FALSE(reply.ok());
        if (!reply.ok()) {
            const std::string err{reply.error()};
            EXPECT_TRUE(err.find("Missing library metadata") != std::string::npos
                        || err.find("syntax error") != std::string::npos
                        || err.find("ERR") != std::string::npos)
                << err;
        }
        completed = true;
    });
    run_coro_test_until(completed);
}

TEST_P(FunctionTest, DeleteMissingLibraryFails) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);

        auto reply = co_await redis.function_delete("nonexistent_library");
        EXPECT_FALSE(reply.ok());
        if (!reply.ok())
            EXPECT_NE(std::string(reply.error()).find("Library not found"), std::string::npos)
                << reply.error();
        completed = true;
    });
    run_coro_test_until(completed);
}

TEST_P(FunctionTest, KillWhenIdleFails) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);

        auto reply = co_await redis.function_kill();
        EXPECT_FALSE(reply.ok());
        if (!reply.ok())
            EXPECT_NE(std::string(reply.error()).find("NOTBUSY"), std::string::npos) << reply.error();
        completed = true;
    });
    run_coro_test_until(completed);
}

TEST_P(FunctionTest, RestoreInvalidPayloadFails) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);

        auto reply = co_await redis.function_restore("invalid_dump_data");
        EXPECT_FALSE(reply.ok());
        if (!reply.ok()) {
            const std::string err{reply.error()};
            EXPECT_TRUE(err.find("payload version") != std::string::npos
                        || err.find("invalid payload") != std::string::npos
                        || err.find("ERR") != std::string::npos)
                << err;
        }
        completed = true;
    });
    run_coro_test_until(completed);
}

TEST_P(FunctionTest, HelpMentionsFunction) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);

        auto help = co_await redis.function_help();
        EXPECT_TRUE(help.ok()) << help.error();
        if (!(!(help.result().empty()))) { ADD_FAILURE() << "precondition failed: !(help.result().empty())"; co_return; }
        bool mentions = false;
        for (const auto &line : help.result())
            if (line.find("FUNCTION") != std::string::npos || line.find("FCALL") != std::string::npos)
                mentions = true;
        EXPECT_TRUE(mentions);
        completed = true;
    });
    run_coro_test_until(completed);
}

} // namespace
