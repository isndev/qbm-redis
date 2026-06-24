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

class ModuleProtocolModesTest : public ProtocolModesTestBase {};

INSTANTIATE_PROTOCOL_MODES(ModuleProtocolModesTest);

/*
 * COROUTINE TESTS
 */

// Test MODULE LIST command using coroutines
TEST_P(ModuleProtocolModesTest, CORO_MODULE_COMMANDS_LIST) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        try {
            auto reply = co_await redis.module_list();

            if (reply.ok()) {
                auto modules = reply.result();

                // Check that the result is an array (even if empty)
                EXPECT_TRUE(modules.is_array());

                // If modules are loaded, each entry should have a "name" field
                if (!modules.empty()) {
                    for (const auto &module : modules) {
                        EXPECT_TRUE(module.contains("name"));
                        EXPECT_TRUE(module["name"].is_string());
                    }
                }
            } else {
                // This might happen if the Redis server doesn't support modules
                std::string error = std::string(reply.error());
                EXPECT_TRUE(error.find("unknown command") != std::string::npos || error.find("module") != std::string::npos);
            }
        } catch (const std::exception &e) {
            // This might happen if the Redis server doesn't support module commands
            std::string error = e.what();
            EXPECT_TRUE(error.find("unknown command") != std::string::npos || error.find("module") != std::string::npos);
        }

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test MODULE LOAD command using coroutines
TEST_P(ModuleProtocolModesTest, CORO_MODULE_COMMANDS_LOAD) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        // Empty path must produce an error reply, never an exception
        auto reply = co_await redis.module_load("");
        EXPECT_FALSE(reply.ok());
        if (!reply.ok()) {
            std::string error{reply.error()};
            EXPECT_TRUE(error.find("wrong number") != std::string::npos || error.find("unknown command") != std::string::npos
                        || error.find("ERR") != std::string::npos || error.find("path") != std::string::npos);
        }

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test MODULE UNLOAD command using coroutines
TEST_P(ModuleProtocolModesTest, CORO_MODULE_COMMANDS_UNLOAD) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        // Unloading a non-existent module must produce an error reply
        auto reply = co_await redis.module_unload("nonexistent_module");
        EXPECT_FALSE(reply.ok());
        if (!reply.ok()) {
            std::string error{reply.error()};
            EXPECT_TRUE(error.find("module not loaded") != std::string::npos || error.find("unknown command") != std::string::npos
                        || error.find("ERR") != std::string::npos || error.find("No such module") != std::string::npos);
        }

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test MODULE HELP command using coroutines
TEST_P(ModuleProtocolModesTest, CORO_MODULE_COMMANDS_HELP) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        try {
            auto reply = co_await redis.module_help();

            if (reply.ok()) {
                auto help = reply.result();

                // Check that the result is a vector of strings
                EXPECT_FALSE(help.empty());

                // Each line of help should be a string
                for (const auto &line : help) {
                    EXPECT_FALSE(line.empty());
                }
            } else {
                // This might happen if the Redis server doesn't support module commands
                std::string error = std::string(reply.error());
                EXPECT_TRUE(error.find("unknown command") != std::string::npos || error.find("module") != std::string::npos);
            }
        } catch (const std::exception &e) {
            // This might happen if the Redis server doesn't support module commands
            std::string error = e.what();
            EXPECT_TRUE(error.find("unknown command") != std::string::npos || error.find("module") != std::string::npos);
        }

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

TEST_P(ModuleProtocolModesTest, MODULE_LIST_JSON) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        try {
            auto r = co_await redis.module_list();
            if (r.ok())
                {
                EXPECT_TRUE(r.result().is_array());
                }
        } catch (const std::exception &) {
            // MODULE not available on older Redis
        }
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

// Test async MODULE LIST command