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
#include <iostream>

using namespace qb::io;
using namespace std::chrono;
using namespace qb::redis;

// ============================================================================
// Fixture: all tests run in both RESP2 and RESP3
// ============================================================================

class AclProtocolModesTest : public ProtocolModesTestBase {};

INSTANTIATE_PROTOCOL_MODES(AclProtocolModesTest);

/*
 * COROUTINE TESTS
 */

// Test ACL CAT command using coroutines
TEST_P(AclProtocolModesTest, CORO_ACL_COMMANDS_CAT) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        try {
            // Test basic ACL CAT
            auto reply = co_await redis.acl_cat();
            EXPECT_TRUE(reply.ok());

            auto categories = reply.result();
            EXPECT_FALSE(categories.empty());

            // Verify common categories are present
            bool found_string_category = false;
            bool found_key_category = false;

            for (const auto& cat : categories) {
                if (cat == "string")
                    found_string_category = true;
                if (cat == "keyspace")
                    found_key_category = true;
            }

            EXPECT_TRUE(found_string_category);
            EXPECT_TRUE(found_key_category);

            // ACL CAT <category> returns an array of command name strings
            auto commands_reply = co_await redis.command<qb::json>("ACL", "CAT", "string");
            if (commands_reply.ok()) {
                auto commands = commands_reply.result();
                EXPECT_TRUE(commands.is_array());
                EXPECT_FALSE(commands.empty());

                bool found_string_command = false;
                for (const auto &cmd : commands) {
                    std::string cmd_name = cmd.get<std::string>();
                    if (cmd_name == "incr" || cmd_name == "decr" || cmd_name == "getex" ||
                        cmd_name == "getrange" || cmd_name == "strlen") {
                        found_string_command = true;
                        break;
                    }
                }
                EXPECT_TRUE(found_string_command);
            }
        } catch (const std::exception& e) {
            // This might happen if the Redis server doesn't support ACL commands
            std::string error = e.what();
            EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                       error.find("ACL") != std::string::npos);
        }

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test ACL GETUSER command using coroutines
TEST_P(AclProtocolModesTest, CORO_ACL_COMMANDS_GETUSER) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        try {
            auto reply = co_await redis.acl_getuser("default");
            EXPECT_TRUE(reply.ok());

            auto user_info = reply.result();
            EXPECT_TRUE(user_info.is_object());

            // Verify the default user has flags
            EXPECT_TRUE(user_info.contains("flags"));
            auto flags = user_info["flags"];
            EXPECT_TRUE(flags.is_array());

            // Check if the default user is active (on)
            bool is_active = false;
            for (const auto& flag : flags) {
                if (flag.template get<std::string>() == "on")
                    is_active = true;
            }
            EXPECT_TRUE(is_active);
        } catch (const std::exception& e) {
            std::string error = e.what();
            EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                       error.find("ACL") != std::string::npos);
        }

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test ACL LIST command using coroutines
TEST_P(AclProtocolModesTest, CORO_ACL_COMMANDS_LIST) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        try {
            auto reply = co_await redis.acl_list();
            EXPECT_TRUE(reply.ok());

            auto acl_rules = reply.result();
            EXPECT_TRUE(acl_rules.is_array());
            EXPECT_FALSE(acl_rules.empty());

            // Verify that the default user is listed
            bool found_default_user = false;
            for (const auto& rule : acl_rules) {
                std::string rule_str = rule.template get<std::string>();
                if (rule_str.find("user default") != std::string::npos)
                    found_default_user = true;
            }
            EXPECT_TRUE(found_default_user);
        } catch (const std::exception& e) {
            std::string error = e.what();
            EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                       error.find("ACL") != std::string::npos);
        }

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test ACL DRYRUN command using coroutines
// ACL DRYRUN returns: "OK" (simple string) on success, or bulk string error message on denial.
// The reply is parsed as qb::json which can be string or object depending on Redis version.
TEST_P(AclProtocolModesTest, CORO_ACL_COMMANDS_DRYRUN) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        try {
            auto reply = co_await redis.acl_dryrun("default", "GET", {"nonexistent_key"});
            EXPECT_TRUE(reply.ok());
            // DRYRUN returns "OK" (string) or error message (string); accept string or object
            EXPECT_TRUE(reply.result().is_string() || reply.result().is_object());
        } catch (const std::exception& e) {
            std::string error = e.what();
            EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                       error.find("ACL") != std::string::npos);
        }
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) qb::io::async::run(EVRUN_NOWAIT);
}

// Test ACL LOG command using coroutines
TEST_P(AclProtocolModesTest, CORO_ACL_COMMANDS_LOG) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        try {
            // Get ACL logs without count limitation
            auto reply1 = co_await redis.acl_log();
            EXPECT_TRUE(reply1.ok());
            EXPECT_TRUE(reply1.result().is_array());

            // Get ACL logs with count limitation
            auto reply2 = co_await redis.acl_log(5);
            EXPECT_TRUE(reply2.ok());
            auto limited_logs = reply2.result();
            EXPECT_TRUE(limited_logs.is_array());

            if (!limited_logs.empty()) {
                EXPECT_LE(limited_logs.size(), 5);
            }
        } catch (const std::exception& e) {
            std::string error = e.what();
            EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                       error.find("ACL") != std::string::npos);
        }

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test ACL USERS command using coroutines
TEST_P(AclProtocolModesTest, CORO_ACL_COMMANDS_USERS) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        try {
            auto reply = co_await redis.acl_users();
            EXPECT_TRUE(reply.ok());

            auto users = reply.result();
            EXPECT_FALSE(users.empty());

            // Verify that the default user exists
            bool found_default_user = false;
            for (const auto& user_str : users) {
                if (user_str == "default")
                    found_default_user = true;
            }
            EXPECT_TRUE(found_default_user);
        } catch (const std::exception& e) {
            std::string error = e.what();
            EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                       error.find("ACL") != std::string::npos);
        }

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test ACL WHOAMI command using coroutines
TEST_P(AclProtocolModesTest, CORO_ACL_COMMANDS_WHOAMI) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        try {
            auto reply = co_await redis.acl_whoami();
            EXPECT_TRUE(reply.ok());

            auto current_user = reply.result();
            EXPECT_EQ(current_user, "default");
        } catch (const std::exception& e) {
            std::string error = e.what();
            EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                       error.find("ACL") != std::string::npos);
        }

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test ACL HELP command using coroutines
TEST_P(AclProtocolModesTest, CORO_ACL_COMMANDS_HELP) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        try {
            auto reply = co_await redis.acl_help();
            EXPECT_TRUE(reply.ok());

            auto help = reply.result();
            EXPECT_FALSE(help.empty());

            // Verify that the help contains useful information
            bool found_help_entry = false;
            for (const auto& line_str : help) {
                if (line_str.find("ACL") != std::string::npos)
                    found_help_entry = true;
            }
            EXPECT_TRUE(found_help_entry);
        } catch (const std::exception& e) {
            std::string error = e.what();
            EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                       error.find("ACL") != std::string::npos);
        }

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test ACL GENPASS command using coroutines
TEST_P(AclProtocolModesTest, CORO_ACL_COMMANDS_GENPASS) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        try {
            // Generate a password
            auto reply1 = co_await redis.acl_genpass();
            EXPECT_TRUE(reply1.ok());

            auto password = reply1.result();
            EXPECT_FALSE(password.empty());
            EXPECT_GT(password.length(), 8);

            // Test with custom bits
            auto reply2 = co_await redis.acl_genpass(128);
            EXPECT_TRUE(reply2.ok());
            EXPECT_FALSE(reply2.result().empty());
        } catch (const std::exception& e) {
            std::string error = e.what();
            EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                       error.find("ACL") != std::string::npos);
        }

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

TEST_P(AclProtocolModesTest, ACL_WHOAMI_STRING) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        try {
            auto r = co_await redis.acl_whoami();
            EXPECT_TRUE(r.ok()) << r.error();
            if (r.ok()) EXPECT_FALSE(r.result().empty());
        } catch (const std::exception&) {
            // ACL not available on older Redis
        }
        done = true;
    }());
    while (!done) qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(AclProtocolModesTest, ACL_LIST_JSON) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        try {
            auto r = co_await redis.acl_list();
            if (r.ok()) EXPECT_TRUE(r.result().is_array());
        } catch (const std::exception&) {
            // ACL not available on older Redis
        }
        done = true;
    }());
    while (!done) qb::io::async::run(EVRUN_NOWAIT);
}

// Test async ACL CAT command