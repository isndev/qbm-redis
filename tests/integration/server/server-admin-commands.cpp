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
 * @file integration/server/server-admin-commands.cpp
 * @brief Live RESP2/RESP3 integration tests for the server *admin* surface of
 *        `qb::redis::tcp::client`: CLIENT / CONFIG / COMMAND / DEBUG.
 *
 * Effect-verified throughout (setname→getname, config_set→config_get readback,
 * setinfo→client_info), not `.ok()`-only. Introspection verbs (INFO/TIME/ROLE/SLOWLOG/
 * MEMORY) live in the sibling `server-introspection.cpp`. The 5 terse trailing smoke
 * tests (DBSIZE/TIME/CLIENT_ID_INTEGER/INFO_JSON/FLUSHDB_FLUSHALL) of the legacy monolith
 * are deleted: they are strict subsets of the CORO_* bodies (here and in introspection).
 */

#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include "../redis.h"
#include "../../shared/redis_integration_fixture.h"

// ProtocolMode / ProtocolModesTestBase / INSTANTIATE_PROTOCOL_MODES / run_coro_test_until /
// CO_IGNORE / PROTOCOL_ENSURE_RESP3_VAR come from redis_integration_fixture.h (legacy-compatible
// global re-export).

namespace {

class ServerAdminTest : public ProtocolModesTestBase {};

INSTANTIATE_PROTOCOL_MODES(ServerAdminTest);

// =============== CLIENT MANAGEMENT ===============

// setname must be observable through getname (effect verification, not .ok()-only).
TEST_P(ServerAdminTest, ClientSetnameGetnameRoundTrip) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);

        auto setname = co_await redis.client_setname("qb_admin_client");
        EXPECT_TRUE(setname.ok()) << setname.error();

        auto getname = co_await redis.client_getname();
        EXPECT_TRUE(getname.ok()) << getname.error();
        if (!(getname.result().has_value())) { ADD_FAILURE() << "precondition failed: getname.result().has_value()"; co_return; }
        EXPECT_EQ(*getname.result(), "qb_admin_client");

        // client_id is a positive monotonic connection id.
        auto id = co_await redis.client_id();
        EXPECT_TRUE(id.ok()) << id.error();
        EXPECT_GT(id.result(), 0);

        completed = true;
    });
    run_coro_test_until(completed);
}

// client_info must reflect the lib-name/lib-ver set via client_setinfo.
TEST_P(ServerAdminTest, ClientSetinfoReflectedInClientInfo) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);

        auto setname = co_await redis.client_setinfo("lib-name", "qb-redis-admintest");
        EXPECT_TRUE(setname.ok()) << setname.error();
        auto setver = co_await redis.client_setinfo("lib-ver", "9.9.9");
        EXPECT_TRUE(setver.ok()) << setver.error();

        auto info = co_await redis.client_info();
        EXPECT_TRUE(info.ok()) << info.error();
        const std::string &line = info.result();
        EXPECT_FALSE(line.empty());
        // CLIENT INFO echoes lib-name=/lib-ver= fields for the current connection.
        EXPECT_NE(line.find("lib-name=qb-redis-admintest"), std::string::npos) << line;
        EXPECT_NE(line.find("lib-ver=9.9.9"), std::string::npos) << line;

        completed = true;
    });
    run_coro_test_until(completed);
}

// CLIENT toggle/pause verbs: each is exercised for a real OK; pause is immediately lifted so
// TearDown's flushall is not parked behind the pause window (Redis 8 holds commands while paused).
TEST_P(ServerAdminTest, ClientToggleAndPauseVerbs) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);

        auto pause = co_await redis.client_pause(50, "WRITE");
        EXPECT_TRUE(pause.ok()) << pause.error();
        auto unpause = co_await redis.client_unpause();
        EXPECT_TRUE(unpause.ok()) << unpause.error();

        auto track_on = co_await redis.client_tracking(true);
        EXPECT_TRUE(track_on.ok()) << track_on.error();
        auto track_off = co_await redis.client_tracking(false);
        EXPECT_TRUE(track_off.ok()) << track_off.error();

        auto noevict = co_await redis.client_no_evict(false);
        EXPECT_TRUE(noevict.ok()) << noevict.error();
        auto notouch = co_await redis.client_no_touch(false);
        EXPECT_TRUE(notouch.ok()) << notouch.error();
        auto reply_on = co_await redis.client_reply("ON");
        EXPECT_TRUE(reply_on.ok()) << reply_on.error();

        // CLIENT GETREDIR is -1 (no redirection) when tracking is off; just assert it is valid.
        auto getredir = co_await redis.client_getredir();
        EXPECT_TRUE(getredir.ok()) << getredir.error();
        EXPECT_GE(getredir.result(), -1);

        // client_list returns the raw textual CLIENT LIST as a json string; it must mention
        // this very connection (addr= field present at least once).
        auto list = co_await redis.client_list();
        EXPECT_TRUE(list.ok()) << list.error();
        EXPECT_TRUE(list.result().is_string());
        EXPECT_NE(list.result().get<std::string>().find("addr="), std::string::npos);

        completed = true;
    });
    run_coro_test_until(completed);
}

// =============== CONFIGURATION ===============

// config_set must be observable through config_get (round-trip on a safe, restorable param).
TEST_P(ServerAdminTest, ConfigSetGetRoundTrip) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);

        // Snapshot maxmemory so we can restore it (never leave the daemon mutated).
        auto before = co_await redis.config_get("maxmemory");
        EXPECT_TRUE(before.ok()) << before.error();
        if (!(!(before.result().empty()))) { ADD_FAILURE() << "precondition failed: !(before.result().empty())"; co_return; }
        EXPECT_EQ(before.result()[0].first, "maxmemory");
        const std::string original = before.result()[0].second;

        // Set to a small concrete value and read it back exactly.
        auto set_r = co_await redis.config_set("maxmemory", "104857600"); // 100 MiB
        EXPECT_TRUE(set_r.ok()) << set_r.error();

        auto after = co_await redis.config_get("maxmemory");
        EXPECT_TRUE(after.ok()) << after.error();
        if (!(!(after.result().empty()))) { ADD_FAILURE() << "precondition failed: !(after.result().empty())"; co_return; }
        EXPECT_EQ(after.result()[0].second, "104857600");

        // Restore.
        auto restore = co_await redis.config_set("maxmemory", original);
        EXPECT_TRUE(restore.ok()) << restore.error();

        // Pattern get returns at least the matched param.
        auto pattern = co_await redis.config_get("maxmemory*");
        EXPECT_TRUE(pattern.ok()) << pattern.error();
        EXPECT_FALSE(pattern.result().empty());

        // config_resetstat is a plain OK.
        auto reset = co_await redis.config_resetstat();
        EXPECT_TRUE(reset.ok()) << reset.error();

        completed = true;
    });
    run_coro_test_until(completed);
}

// CONFIG REWRITE depends on the server being started with a config file; it is allowed to fail
// with the well-known "without a config file" error, but the error TEXT is asserted (label-gated
// rather than swallowed), so a changed/unknown error fails the test.
TEST_P(ServerAdminTest, ConfigRewriteKnownOutcome) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);

        auto rewrite = co_await redis.config_rewrite();
        if (!rewrite.ok()) {
            const std::string err{rewrite.error()};
            EXPECT_NE(err.find("config file"), std::string::npos) << err;
        }
        completed = true;
    });
    run_coro_test_until(completed);
}

// =============== COMMAND INTROSPECTION ===============

// COMMAND with explicit names returns metadata for exactly those commands.
TEST_P(ServerAdminTest, CommandInfoForNamedCommands) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);

        std::vector<std::string> names = {"get", "set"};
        auto                     info  = co_await redis.command(names);
        EXPECT_TRUE(info.ok()) << info.error();
        const auto &j = info.result();
        if (!(!(j.empty()))) { ADD_FAILURE() << "precondition failed: !(j.empty())"; co_return; }

        bool has_get = false, has_set = false;
        if (j.is_object()) {
            has_get = j.contains("get");
            has_set = j.contains("set");
        } else {
            EXPECT_TRUE(j.is_array());
            for (const auto &cmd : j) {
                std::string name;
                if (cmd.is_array() && cmd.size() > 0 && cmd[0].is_string())
                    name = cmd[0].get<std::string>();
                else if (cmd.is_object() && cmd.contains("name"))
                    name = cmd["name"].get<std::string>();
                if (name == "get")
                    has_get = true;
                else if (name == "set")
                    has_set = true;
            }
        }
        EXPECT_TRUE(has_get);
        EXPECT_TRUE(has_set);

        completed = true;
    });
    run_coro_test_until(completed);
}

// COMMAND (all) + COMMAND COUNT must be consistent: count > 0 and the full list contains ping.
TEST_P(ServerAdminTest, CommandAllAndCount) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);

        auto all = co_await redis.command();
        EXPECT_TRUE(all.ok()) << all.error();
        const auto &j = all.result();
        if (!(!(j.empty()))) { ADD_FAILURE() << "precondition failed: !(j.empty())"; co_return; }
        EXPECT_GT(j.size(), 10u);

        bool has_ping = false;
        if (j.is_object()) {
            has_ping = j.contains("ping");
        } else {
            EXPECT_TRUE(j.is_array());
            for (const auto &cmd : j) {
                std::string name;
                if (cmd.is_array() && cmd.size() > 0 && cmd[0].is_string())
                    name = cmd[0].get<std::string>();
                else if (cmd.is_object() && cmd.contains("name"))
                    name = cmd["name"].get<std::string>();
                if (name == "ping") {
                    has_ping = true;
                    break;
                }
            }
        }
        EXPECT_TRUE(has_ping);

        auto count = co_await redis.command_count();
        EXPECT_TRUE(count.ok()) << count.error();
        EXPECT_GT(count.result(), 0);

        completed = true;
    });
    run_coro_test_until(completed);
}

// COMMAND GETKEYS / GETKEYSANDFLAGS / DOCS / LIST.
TEST_P(ServerAdminTest, CommandGetkeysDocsList) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);

        // SET <key> <value> → the only key is <key>, at position 0.
        auto keys = co_await redis.command_getkeys("set", {"admin:k", "v"});
        EXPECT_TRUE(keys.ok()) << keys.error();
        if (!(keys.result().size() == 1u)) { ADD_FAILURE() << "precondition failed: keys.result().size() == 1u"; co_return; }
        EXPECT_EQ(keys.result()[0], "admin:k");

        auto kflags = co_await redis.command_getkeysandflags("GET", {"admin:k"});
        EXPECT_TRUE(kflags.ok()) << kflags.error();
        EXPECT_TRUE(kflags.result().is_array());

        auto docs = co_await redis.command_docs({"GET", "SET"});
        EXPECT_TRUE(docs.ok()) << docs.error();
        EXPECT_TRUE(docs.result().is_array() || docs.result().is_object());

        auto list = co_await redis.command_list();
        EXPECT_TRUE(list.ok()) << list.error();
        EXPECT_FALSE(list.result().empty());

        completed = true;
    });
    run_coro_test_until(completed);
}

// =============== DEBUG ===============
// Re-enabled from the legacy DISABLED_ block, gated behind the `destructive` label (DEBUG OBJECT
// touches server internals; DEBUG SLEEP freezes the single-threaded server briefly).

TEST_P(ServerAdminTest, DebugObjectAndSleep) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);

        const std::string key = protocol_key("debug_target");
        auto              set = co_await redis.set(key, "value");
        EXPECT_TRUE(set.ok()) << set.error();

        auto dbg = co_await redis.debug_object(key);
        // DEBUG may be disabled via `enable-debug-command`; tolerate that specific outcome but
        // assert the diagnostic shape when it succeeds.
        if (dbg.ok()) {
            const std::string &line = dbg.result();
            EXPECT_FALSE(line.empty());
            EXPECT_TRUE(line.find("encoding") != std::string::npos
                        || line.find("refcount") != std::string::npos
                        || line.find("serializedlength") != std::string::npos)
                << line;
        } else {
            const std::string err{dbg.error()};
            EXPECT_NE(err.find("DEBUG"), std::string::npos) << err;
        }

        auto sleep_r = co_await redis.debug_sleep(std::chrono::milliseconds(5));
        if (!sleep_r.ok()) {
            const std::string err{sleep_r.error()};
            EXPECT_NE(err.find("DEBUG"), std::string::npos) << err;
        }

        completed = true;
    });
    run_coro_test_until(completed);
}

} // namespace
