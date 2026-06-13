/*
 * qb - C++ Actor Framework
 * Copyright (C) 2011-2025 isndev (cpp.actor). All rights reserved.
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

class FunctionProtocolModesTest : public ProtocolModesTestBase {};

INSTANTIATE_PROTOCOL_MODES(FunctionProtocolModesTest);

/*
 * COROUTINE TESTS
 */

// Test FUNCTION LIST command using coroutines
TEST_P(FunctionProtocolModesTest, CORO_FUNCTION_COMMANDS_LIST) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        try {
            auto reply = co_await redis.function_list();

            if (reply.ok()) {
                auto functions = reply.result();
                EXPECT_TRUE(functions.is_array());

                if (!functions.empty()) {
                    for (const auto& function : functions) {
                        EXPECT_TRUE(function.contains("name"));
                        EXPECT_TRUE(function["name"].is_string());
                    }
                }
            } else {
                std::string error = std::string(reply.error());
                EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                           error.find("function") != std::string::npos);
            }
        } catch (const std::exception& e) {
            std::string error = e.what();
            EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                       error.find("function") != std::string::npos);
        }

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test FUNCTION LOAD command using coroutines
TEST_P(FunctionProtocolModesTest, CORO_FUNCTION_COMMANDS_LOAD) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto reply = co_await redis.function_load("invalid function code");
        // Invalid code must produce an error reply, never an exception
        EXPECT_FALSE(reply.ok());
        if (!reply.ok()) {
            std::string error{reply.error()};
            EXPECT_TRUE(error.find("syntax error") != std::string::npos ||
                        error.find("unknown command") != std::string::npos ||
                        error.find("ERR") != std::string::npos);
        }

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test FUNCTION DELETE command using coroutines
TEST_P(FunctionProtocolModesTest, CORO_FUNCTION_COMMANDS_DELETE) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto reply = co_await redis.function_delete("nonexistent_function");
        // Deleting a non-existent function must produce an error reply
        EXPECT_FALSE(reply.ok());
        if (!reply.ok()) {
            std::string error{reply.error()};
            EXPECT_TRUE(error.find("function not found") != std::string::npos ||
                        error.find("unknown command") != std::string::npos ||
                        error.find("ERR") != std::string::npos ||
                        error.find("Library not found") != std::string::npos);
        }

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test FUNCTION FLUSH command using coroutines
TEST_P(FunctionProtocolModesTest, CORO_FUNCTION_COMMANDS_FLUSH) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        try {
            auto reply = co_await redis.function_flush();

            if (reply.ok()) {
                auto result = reply.result();
                EXPECT_EQ(result, "OK");
            } else {
                std::string error = std::string(reply.error());
                EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                           error.find("function") != std::string::npos);
            }
        } catch (const std::exception& e) {
            std::string error = e.what();
            EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                       error.find("function") != std::string::npos);
        }

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test FCALL and FCALL_RO (require Redis with functions loaded)
TEST_P(FunctionProtocolModesTest, CORO_FUNCTION_COMMANDS_FCALL) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto reply = co_await redis.fcall<qb::json>("nonexistent::func", {}, {});
        if (reply.ok()) {
            EXPECT_TRUE(reply.result().is_object() || reply.result().is_array());
        } else {
            std::string err{reply.error()};
            EXPECT_TRUE(err.find("unknown command") != std::string::npos ||
                       err.find("function") != std::string::npos ||
                       err.find("not found") != std::string::npos ||
                       err.find("ERR") != std::string::npos);
        }
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(FunctionProtocolModesTest, CORO_FUNCTION_COMMANDS_FCALL_RO) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto reply = co_await redis.fcallRo<qb::json>("nonexistent::func", {}, {});
        if (reply.ok()) {
            EXPECT_TRUE(reply.result().is_object() || reply.result().is_array());
        } else {
            std::string err{reply.error()};
            EXPECT_TRUE(err.find("unknown command") != std::string::npos ||
                       err.find("function") != std::string::npos ||
                       err.find("not found") != std::string::npos ||
                       err.find("ERR") != std::string::npos);
        }
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) qb::io::async::run(EVRUN_NOWAIT);
}

// Test FUNCTION KILL command using coroutines
TEST_P(FunctionProtocolModesTest, CORO_FUNCTION_COMMANDS_KILL) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto reply = co_await redis.function_kill();
        // Killing when no function is running must produce an error reply
        EXPECT_FALSE(reply.ok());
        if (!reply.ok()) {
            std::string error{reply.error()};
            EXPECT_TRUE(error.find("No scripts") != std::string::npos ||
                        error.find("unknown command") != std::string::npos ||
                        error.find("ERR") != std::string::npos ||
                        error.find("NOTBUSY") != std::string::npos);
        }

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test FUNCTION STATS command using coroutines
TEST_P(FunctionProtocolModesTest, CORO_FUNCTION_COMMANDS_STATS) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        try {
            auto reply = co_await redis.function_stats();

            if (reply.ok()) {
                auto stats = reply.result();
                EXPECT_TRUE(stats.is_object());
                EXPECT_TRUE(stats.contains("running_scripts") || stats.contains("engines"));
            } else {
                std::string error = std::string(reply.error());
                EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                           error.find("function") != std::string::npos);
            }
        } catch (const std::exception& e) {
            std::string error = e.what();
            EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                       error.find("function") != std::string::npos);
        }

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test FUNCTION DUMP command using coroutines
TEST_P(FunctionProtocolModesTest, CORO_FUNCTION_COMMANDS_DUMP) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        try {
            auto reply = co_await redis.function_dump();

            if (reply.ok()) {
                auto dump = reply.result();
                EXPECT_TRUE(dump.is_string() || dump.is_binary());
            } else {
                std::string error = std::string(reply.error());
                EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                           error.find("function") != std::string::npos);
            }
        } catch (const std::exception& e) {
            std::string error = e.what();
            EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                       error.find("function") != std::string::npos);
        }

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test FUNCTION RESTORE command using coroutines
TEST_P(FunctionProtocolModesTest, CORO_FUNCTION_COMMANDS_RESTORE) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto reply = co_await redis.function_restore("invalid_dump_data");
        // Invalid dump data must produce an error reply
        EXPECT_FALSE(reply.ok());
        if (!reply.ok()) {
            std::string error{reply.error()};
            EXPECT_TRUE(error.find("invalid payload") != std::string::npos ||
                        error.find("unknown command") != std::string::npos ||
                        error.find("ERR") != std::string::npos ||
                        error.find("payload version") != std::string::npos);
        }

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test FUNCTION HELP command using coroutines
TEST_P(FunctionProtocolModesTest, CORO_FUNCTION_COMMANDS_HELP) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        try {
            auto reply = co_await redis.function_help();

            if (reply.ok()) {
                auto help = reply.result();
                EXPECT_FALSE(help.empty());

                for (const auto& line : help) {
                    EXPECT_FALSE(line.empty());
                }
            } else {
                std::string error = std::string(reply.error());
                EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                           error.find("function") != std::string::npos);
            }
        } catch (const std::exception& e) {
            std::string error = e.what();
            EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                       error.find("function") != std::string::npos);
        }

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

TEST_P(FunctionProtocolModesTest, FUNCTION_LIST_JSON) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        try {
            auto r = co_await redis.function_list();
            if (r.ok()) EXPECT_TRUE(r.result().is_array());
        } catch (const std::exception&) {
            // FUNCTION not available on older Redis
        }
        done = true;
    });
    while (!done) qb::io::async::run(EVRUN_NOWAIT);
}

// Test async FUNCTION LIST command