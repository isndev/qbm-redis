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
 *        `qb::redis::tcp::client`: CLIENT / CONFIG / COMMAND / DEBUG / MONITOR.
 *
 * Effect-verified throughout (setname→getname, config_set→config_get readback,
 * setinfo→client_info), not `.ok()`-only. Introspection verbs (INFO/TIME/ROLE/SLOWLOG/
 * MEMORY) live in the sibling `server-introspection.cpp`. The 5 terse trailing smoke
 * tests (DBSIZE/TIME/CLIENT_ID_INTEGER/INFO_JSON/FLUSHDB_FLUSHALL) of the legacy monolith
 * are deleted: they are strict subsets of the CORO_* bodies (here and in introspection).
 */

#include <gtest/gtest.h>
#include <chrono>
#include <string>
#include <vector>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include "../../shared/redis_integration_fixture.h"
#include <qbm/redis/redis.h>

// ProtocolMode / ProtocolModesTestBase / INSTANTIATE_PROTOCOL_MODES / run_coro_test_until /
// CO_IGNORE / PROTOCOL_ENSURE_RESP3_VAR come from redis_integration_fixture.h (legacy-compatible
// global re-export).

namespace {

class ServerAdminTest : public ProtocolModesTestBase {};

INSTANTIATE_PROTOCOL_MODES(ServerAdminTest);

TEST_F(RedisIntegrationTest, MonitorStreamsTwoRealCommandsAndResetReleasesConnection) {
    qb::redis::tcp::client observer{qb::redis::test::redis_test_uri()};
    ASSERT_TRUE(qb::io::async::run_sync(observer.connect()));

    std::vector<std::string> lines;
    int                      terminal = 0;
    observer.monitor([&](qb::redis::Reply<std::string> &&r) {
        if (r)
            lines.push_back(r.result());
        else
            ++terminal;
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (lines.empty() && std::chrono::steady_clock::now() < deadline)
        qb::io::async::run(EVRUN_NOWAIT);
    ASSERT_EQ(lines, (std::vector<std::string>{"OK"}));

    const std::string key = "qb651-monitor-stream";
    auto              set = qb::io::async::run_sync(redis.set(key, "value"));
    ASSERT_TRUE(set.ok()) << set.error();
    auto get = qb::io::async::run_sync(redis.get(key));
    ASSERT_TRUE(get.ok()) << get.error();
    ASSERT_EQ(get.value_or(""), "value");
    while (lines.size() < 3 && std::chrono::steady_clock::now() < deadline)
        qb::io::async::run(EVRUN_NOWAIT);
    ASSERT_GE(lines.size(), 3u);
    EXPECT_NE(lines[1].find("\"SET\" \"" + key + "\""), std::string::npos) << lines[1];
    EXPECT_NE(lines[2].find("\"GET\" \"" + key + "\""), std::string::npos) << lines[2];
    EXPECT_EQ(observer.pending_reply_count(), 0u);

    auto reset = qb::io::async::run_sync(observer.reset());
    ASSERT_TRUE(reset.ok()) << reset.error();
    EXPECT_EQ(std::string(reset.result()), "RESET");
    EXPECT_EQ(terminal, 1);
    auto ping = qb::io::async::run_sync(observer.ping());
    ASSERT_TRUE(ping.ok()) << ping.error();
    EXPECT_EQ(ping.result(), "PONG");
    observer.disconnect();
    EXPECT_EQ(terminal, 1);
}

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
        if (!(getname.result().has_value())) {
            ADD_FAILURE() << "precondition failed: getname.result().has_value()";
            co_return;
        }
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

// The typed Reply<status> API cannot await a server reply for OFF or SKIP:
// Redis emits none. Reject locally before either mode reaches the connection,
// then prove a following command still receives its own reply.
TEST_P(ServerAdminTest, ClientReplyNoResponseModesRejectWithoutDesync) {
    for (const std::string mode : {"OFF", "sKiP"}) {
        SCOPED_TRACE(mode);
        qb::redis::tcp::client candidate{qb::redis::test::redis_test_uri()};
        ASSERT_TRUE(qb::io::async::run_sync(candidate.connect()));
        if (GetParam() == ProtocolMode::RESP3) {
            auto hello = qb::io::async::run_sync(candidate.hello(3));
            ASSERT_TRUE(hello.ok()) << hello.error();
        }

        int         calls = 0;
        std::string error;
        candidate.client_reply(
            [&](auto &&reply) {
                ++calls;
                if (!reply.ok())
                    error = reply.error();
            },
            mode);
        if (calls != 1) {
            candidate.disconnect(); // the base implementation queued a reply Redis cannot send
            ADD_FAILURE() << "CLIENT REPLY " << mode << " did not reject synchronously";
            continue;
        }
        EXPECT_NE(error.find("no reply"), std::string::npos) << error;
        EXPECT_EQ(candidate.pending_reply_count(), 0u);
        auto echo = qb::io::async::run_sync(candidate.echo("still-aligned"));
        ASSERT_TRUE(echo.ok()) << echo.error();
        EXPECT_EQ(echo.result(), "still-aligned");

        auto awaited = qb::io::async::run_sync(candidate.client_reply(mode));
        EXPECT_FALSE(awaited.ok());
        EXPECT_NE(awaited.error().find("no reply"), std::string::npos) << awaited.error();
        EXPECT_EQ(candidate.pending_reply_count(), 0u);
        auto next = qb::io::async::run_sync(candidate.echo("coroutine-aligned"));
        ASSERT_TRUE(next.ok()) << next.error();
        EXPECT_EQ(next.result(), "coroutine-aligned");
        candidate.disconnect();
    }
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
        if (!(!(before.result().empty()))) {
            ADD_FAILURE() << "precondition failed: !(before.result().empty())";
            co_return;
        }
        EXPECT_EQ(before.result()[0].first, "maxmemory");
        const std::string original = before.result()[0].second;

        // Set to a small concrete value and read it back exactly.
        auto set_r = co_await redis.config_set("maxmemory", "104857600"); // 100 MiB
        EXPECT_TRUE(set_r.ok()) << set_r.error();

        auto after = co_await redis.config_get("maxmemory");
        EXPECT_TRUE(after.ok()) << after.error();
        if (!(!(after.result().empty()))) {
            ADD_FAILURE() << "precondition failed: !(after.result().empty())";
            co_return;
        }
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
        if (!(!(j.empty()))) {
            ADD_FAILURE() << "precondition failed: !(j.empty())";
            co_return;
        }

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
        if (!(!(j.empty()))) {
            ADD_FAILURE() << "precondition failed: !(j.empty())";
            co_return;
        }
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
        if (!(keys.result().size() == 1u)) {
            ADD_FAILURE() << "precondition failed: keys.result().size() == 1u";
            co_return;
        }
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
// Runs by default with NO extra label. On a stock server DEBUG is disabled by default
// (`enable-debug-command no`, confirmed via `redis-cli CONFIG GET enable-debug-command` on
// Redis 8.8), so DEBUG OBJECT / DEBUG SLEEP return an immediate "DEBUG command not allowed" error
// — the test tolerates that outcome and only asserts the diagnostic shape when DEBUG happens to be
// enabled. It is therefore non-destructive on a stock daemon and needs no `destructive` label (and
// CTest labels are per-binary, so labeling here would wrongly exclude the whole admin binary).

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
            EXPECT_TRUE(line.find("encoding") != std::string::npos || line.find("refcount") != std::string::npos
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

// CLIENT UNBLOCK answers :1 when it unblocked a client and :0 when the ID names no blocked client
// (Huly QB-652). Unblocked as a timeout, the parked BLPOP returns nil; with ERROR it fails with
// the server's UNBLOCKED reason. A second connection parks; the test unblocks it from `redis`.
TEST_P(ServerAdminTest, ClientUnblockReportsWhetherABlockedClientWasUnblocked) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        qb::redis::tcp::client blocked{REDIS_URI_PROTOCOL};
        if (!co_await blocked.connect()) {
            ADD_FAILURE() << "the second connection did not connect";
            completed = true;
            co_return;
        }
        PROTOCOL_ENSURE_RESP3_CLIENT(blocked, completed);
        auto id = co_await blocked.client_id();
        if (!id.ok()) {
            ADD_FAILURE() << id.error();
            completed = true;
            co_return;
        }

        auto idle = co_await redis.client_unblock(id.result());
        EXPECT_TRUE(idle.ok()) << idle.error();
        EXPECT_FALSE(idle.result()) << "a client that is not blocked is not unblocked";

        const std::string key = protocol_key("unblock_list");
        for (const bool with_error : {false, true}) {
            SCOPED_TRACE(with_error);
            bool                                                                 popped = false;
            qb::redis::Reply<std::optional<std::pair<std::string, std::string>>> pop;
            blocked.blpop(
                [&](qb::redis::Reply<std::optional<std::pair<std::string, std::string>>> &&reply) {
                    pop    = std::move(reply);
                    popped = true;
                },
                std::vector<std::string>{key}, 0LL);
            // Until the BLPOP reaches the server the answer is :0; once it is parked, :1.
            bool unblocked = false;
            for (int attempt = 0; attempt < 5000 && !unblocked; ++attempt) {
                auto r = co_await redis.client_unblock(id.result(), with_error);
                if (!r.ok()) {
                    ADD_FAILURE() << r.error();
                    break;
                }
                unblocked = r.result();
            }
            if (!unblocked) {
                ADD_FAILURE() << "the parked BLPOP was never unblocked";
                blocked.disconnect(); // fails the parked BLPOP instead of leaving it parked
                break;
            }
            CO_IGNORE(co_await blocked.ping()); // FIFO: the BLPOP's reply precedes this one
            EXPECT_TRUE(popped);
            if (with_error) {
                EXPECT_FALSE(pop.ok());
                EXPECT_NE(pop.error().find("UNBLOCKED"), std::string::npos) << pop.error();
            } else {
                EXPECT_TRUE(pop.ok()) << pop.error();
                EXPECT_FALSE(pop.result().has_value()) << "unblocked as a timeout: nil";
            }
        }

        completed = true;
    });
    run_coro_test_until(completed);
}

// COMMAND INFO is one positional array per command name -- name, arity, flags, first key, last
// key, step, then ACL categories (6.0), tips, key specifications and subcommands (7.0) -- and a
// nil for a name the server does not know (Huly QB-653). command_info() names the positions,
// keeps the native types and the nesting, and keeps the nil as null.
TEST_P(ServerAdminTest, CommandInfoNamesThePositionalMetadata) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const auto holds_element = [](const qb::json &array, const char *element) {
            for (const auto &e : array)
                if (e == element)
                    return true;
            return false;
        };

        auto info = co_await redis.command_info({"get", "qb-no-such-command", "config"});
        EXPECT_TRUE(info.ok()) << info.error();
        const qb::json &docs = info.result();
        if (!docs.is_array() || docs.size() != 3u) {
            ADD_FAILURE() << "one entry per requested name: " << docs.dump();
            completed = true;
            co_return;
        }

        const qb::json &get = docs[0];
        EXPECT_EQ(get.value("name", ""), "get");
        EXPECT_EQ(get.value("arity", 0), 2);
        EXPECT_EQ(get.value("first_key", 0), 1);
        EXPECT_EQ(get.value("last_key", 0), 1);
        EXPECT_EQ(get.value("step", 0), 1);
        EXPECT_TRUE(get.contains("flags") && holds_element(get["flags"], "readonly")) << get.dump();
        if (get.contains("acl_categories")) { // Redis 6.0+
            EXPECT_TRUE(holds_element(get["acl_categories"], "@read")) << get.dump();
        }
        if (get.contains("key_specifications")) { // Redis 7.0+
            const qb::json specs        = get["key_specifications"];
            const qb::json spec         = specs.is_array() && !specs.empty() ? specs[0] : qb::json::object();
            const qb::json begin_search = spec.value("begin_search", qb::json::object());
            EXPECT_EQ(begin_search.value("type", ""), "index") << get.dump();
            EXPECT_EQ(begin_search.value("spec", qb::json::object()).value("index", 0), 1) << get.dump();
            EXPECT_EQ(spec.value("find_keys", qb::json::object()).value("type", ""), "range") << get.dump();
            EXPECT_TRUE(get.value("tips", qb::json()).is_array()) << get.dump();
            EXPECT_EQ(get.value("subcommands", qb::json()), qb::json::array()) << get.dump();
        }

        EXPECT_TRUE(docs[1].is_null()) << "an unknown name is null: " << docs[1].dump();

        const qb::json &config = docs[2];
        EXPECT_EQ(config.value("name", ""), "config");
        EXPECT_LT(config.value("arity", 0), 0) << "CONFIG takes a variable argument count";
        if (config.contains("subcommands")) { // Redis 7.0+
            EXPECT_FALSE(config["subcommands"].empty());
            for (const auto &sub : config["subcommands"])
                EXPECT_EQ(sub.value("name", "").rfind("config|", 0), 0u) << sub.dump();
        }

        completed = true;
    });
    run_coro_test_until(completed);
}

} // namespace
