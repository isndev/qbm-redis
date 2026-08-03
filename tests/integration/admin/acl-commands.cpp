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
 * @file integration/admin/acl-commands.cpp
 * @brief Live RESP2/RESP3 integration tests for the ACL surface of `qb::redis::tcp::client`.
 *
 * The legacy monolith wrapped every body in `try{...}catch{ EXPECT_TRUE(msg.find("ACL")) }`,
 * which turned any real wrapper failure into a green pass. That masking is removed: ACL
 * availability is probed ONCE in SetUp (acl_whoami) and the whole suite is `GTEST_SKIP`-ped on a
 * pre-Redis-6 server, so genuine failures now surface. DRYRUN is exercised on both an allowed and
 * a denied command against a purpose-built restricted user. GENPASS asserts the exact hex length.
 * The 2 trailing dups (ACL_WHOAMI_STRING / ACL_LIST_JSON) are deleted (subsets of WHOAMI/LIST).
 */

#include <cctype>
#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include "../../shared/redis_integration_fixture.h"
#include <qbm/redis/redis.h>

// ProtocolMode / ProtocolModesTestBase / macros from redis_integration_fixture.h (global re-export).

namespace {

// Fixture that additionally skips the whole suite when the server predates ACL (Redis < 6).
class AclTest : public ProtocolModesTestBase {
protected:
    void
    SetUp() override {
        ProtocolModesTestBase::SetUp();
        if (::testing::Test::IsSkipped())
            return; // daemon unreachable — base already skipped.
        // ACL WHOAMI exists since Redis 6.0; a non-ok reply means ACL is unavailable.
        auto probe = qb::io::async::run_sync(redis.acl_whoami());
        if (!probe.ok())
            GTEST_SKIP() << "ACL unavailable (pre-Redis-6 server): " << probe.error();
    }
};

INSTANTIATE_PROTOCOL_MODES(AclTest);

// ACL CAT lists categories (incl. string + keyspace) and ACL CAT <cat> lists commands.
TEST_P(AclTest, CatCategoriesAndCommands) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);

        auto cats = co_await redis.acl_cat();
        EXPECT_TRUE(cats.ok()) << cats.error();
        if (!(!(cats.result().empty()))) {
            ADD_FAILURE() << "precondition failed: !(cats.result().empty())";
            co_return;
        }

        bool has_string = false, has_keyspace = false;
        for (const auto &c : cats.result()) {
            if (c == "string")
                has_string = true;
            else if (c == "keyspace")
                has_keyspace = true;
        }
        EXPECT_TRUE(has_string);
        EXPECT_TRUE(has_keyspace);

        // ACL CAT string → array of string-category command names.
        auto cmds = co_await redis.command<qb::json>("ACL", "CAT", "string");
        EXPECT_TRUE(cmds.ok()) << cmds.error();
        EXPECT_TRUE(cmds.result().is_array());
        if (!(!(cmds.result().empty()))) {
            ADD_FAILURE() << "precondition failed: !(cmds.result().empty())";
            co_return;
        }
        bool found = false;
        for (const auto &cmd : cmds.result()) {
            const std::string name = cmd.get<std::string>();
            if (name == "incr" || name == "decr" || name == "getex" || name == "getrange" || name == "strlen") {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found);

        completed = true;
    });
    run_coro_test_until(completed);
}

// ACL GETUSER(default) → object with a flags array containing "on".
TEST_P(AclTest, GetuserDefaultIsActive) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);

        auto user = co_await redis.acl_getuser("default");
        EXPECT_TRUE(user.ok()) << user.error();
        EXPECT_TRUE(user.result().is_object());
        EXPECT_TRUE(user.result().contains("flags"));
        const auto &flags = user.result()["flags"];
        EXPECT_TRUE(flags.is_array());
        bool active = false;
        for (const auto &f : flags)
            if (f.get<std::string>() == "on")
                active = true;
        EXPECT_TRUE(active);

        completed = true;
    });
    run_coro_test_until(completed);
}

// ACL LIST contains the default-user rule; ACL USERS contains "default"; ACL WHOAMI == "default".
TEST_P(AclTest, ListUsersWhoami) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);

        auto list = co_await redis.acl_list();
        EXPECT_TRUE(list.ok()) << list.error();
        EXPECT_TRUE(list.result().is_array());
        if (!(!(list.result().empty()))) {
            ADD_FAILURE() << "precondition failed: !(list.result().empty())";
            co_return;
        }
        bool found_default = false;
        for (const auto &rule : list.result())
            if (rule.get<std::string>().find("user default") != std::string::npos)
                found_default = true;
        EXPECT_TRUE(found_default);

        auto users = co_await redis.acl_users();
        EXPECT_TRUE(users.ok()) << users.error();
        bool has_default = false;
        for (const auto &u : users.result())
            if (u == "default")
                has_default = true;
        EXPECT_TRUE(has_default);

        auto who = co_await redis.acl_whoami();
        EXPECT_TRUE(who.ok()) << who.error();
        EXPECT_EQ(who.result(), "default");

        completed = true;
    });
    run_coro_test_until(completed);
}

// ACL LOG returns an array; LOG(5) respects the count cap.
TEST_P(AclTest, LogArrayAndCount) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);

        auto all = co_await redis.acl_log();
        EXPECT_TRUE(all.ok()) << all.error();
        EXPECT_TRUE(all.result().is_array());

        auto limited = co_await redis.acl_log(5);
        EXPECT_TRUE(limited.ok()) << limited.error();
        EXPECT_TRUE(limited.result().is_array());
        EXPECT_LE(limited.result().size(), 5u);

        completed = true;
    });
    run_coro_test_until(completed);
}

// ACL HELP is a non-empty array mentioning ACL.
TEST_P(AclTest, HelpMentionsAcl) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);

        auto help = co_await redis.acl_help();
        EXPECT_TRUE(help.ok()) << help.error();
        if (!(!(help.result().empty()))) {
            ADD_FAILURE() << "precondition failed: !(help.result().empty())";
            co_return;
        }
        bool mentions = false;
        for (const auto &line : help.result())
            if (line.find("ACL") != std::string::npos)
                mentions = true;
        EXPECT_TRUE(mentions);

        completed = true;
    });
    run_coro_test_until(completed);
}

// ACL GENPASS returns hex of an exact length: default 256 bits → 64 hex chars; 128 bits → 32.
TEST_P(AclTest, GenpassExactHexLength) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);

        auto def = co_await redis.acl_genpass();
        EXPECT_TRUE(def.ok()) << def.error();
        EXPECT_EQ(def.result().size(), 64u); // 256 bits / 4 bits-per-hex-char
        for (char c : def.result())
            EXPECT_TRUE(std::isxdigit(static_cast<unsigned char>(c))) << def.result();

        auto p128 = co_await redis.acl_genpass(128);
        EXPECT_TRUE(p128.ok()) << p128.error();
        EXPECT_EQ(p128.result().size(), 32u); // 128 bits / 4
        for (char c : p128.result())
            EXPECT_TRUE(std::isxdigit(static_cast<unsigned char>(c))) << p128.result();

        completed = true;
    });
    run_coro_test_until(completed);
}

// ACL DRYRUN: positive flows. A restricted user that may only GET on its own key prefix:
//   - GET on an allowed key dry-runs to "OK";
//   - SET on the same key dry-runs to a non-OK denial message.
// The user is created in the test and deleted at the end (no daemon residue).
TEST_P(AclTest, DryrunAllowedAndDenied) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);

        const std::string user = protocol_key("acl_dryrun_user");

        // on, no password, allow GET only, allow keys under acl:* .
        auto setuser = co_await redis.acl_setuser(user, "on", "nopass", "+get", "~acl:*");
        EXPECT_TRUE(setuser.ok()) << setuser.error();

        // Allowed: GET acl:foo → "OK".
        auto allowed = co_await redis.acl_dryrun(user, "GET", {"acl:foo"});
        EXPECT_TRUE(allowed.ok()) << allowed.error();
        EXPECT_TRUE(allowed.result().is_string());
        EXPECT_EQ(allowed.result().get<std::string>(), "OK");

        // Denied: SET acl:foo bar → a denial message (not "OK").
        auto denied = co_await redis.acl_dryrun(user, "SET", {"acl:foo", "bar"});
        EXPECT_TRUE(denied.ok()) << denied.error();
        EXPECT_TRUE(denied.result().is_string());
        EXPECT_NE(denied.result().get<std::string>(), "OK");
        EXPECT_FALSE(denied.result().get<std::string>().empty());

        // Cleanup.
        auto del = co_await redis.acl_deluser(user);
        EXPECT_TRUE(del.ok()) << del.error();
        EXPECT_EQ(del.result(), 1); // exactly one user removed

        completed = true;
    });
    run_coro_test_until(completed);
}

} // namespace
