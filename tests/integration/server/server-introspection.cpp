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
 * @file integration/server/server-introspection.cpp
 * @brief Live RESP2/RESP3 integration tests for the server *introspection* surface of
 *        `qb::redis::tcp::client`: INFO / TIME / ROLE / SLOWLOG / MEMORY / DATABASE.
 *
 * Shape is asserted EXACTLY (the client's `command<qb::json>` parser yields a deterministic
 * json kind for each command in both RESP modes — INFO is always a json string, SLOWLOG GET an
 * array, MEMORY STATS an object — so the old `is_object()||is_array()||!is_null()` polymorphic
 * gates are replaced with exact checks). The async-flush case polls `dbsize()` to 0 instead of a
 * fixed 100ms sleep.
 *
 * DISABLED vs. enabled — honest state of the three replication/persistence probes:
 *   - `LatencyLatestHistoryReset` is ENABLED and runs by default. LATENCY LATEST/HISTORY are
 *     read-only and LATENCY RESET only clears the shared latency-monitor samples (no user-data
 *     risk) — verified via `redis-cli LATENCY RESET/LATEST/HISTORY` on Redis 8.8. It carries NO
 *     extra label: it is no more destructive than the SLOWLOG RESET this same binary already runs
 *     unconditionally above, and CTest labels are per-binary (a `destructive` label here would
 *     wrongly exclude the safe INFO/TIME/ROLE/SLOWLOG/MEMORY/DATABASE cases too).
 *   - `DISABLED_SyncPsyncResolve` stays DISABLED_: SYNC/PSYNC are replication commands that stream
 *     an RDB / change replication state, genuinely risky on a shared daemon. Requires manual
 *     `--gtest_also_run_disabled_tests`; never auto-run.
 *   - `DISABLED_PersistenceBgsaveLastsave` stays DISABLED_: BGSAVE/BGREWRITEAOF fork the server and
 *     write RDB/AOF to disk — disruptive on a shared daemon. Requires manual enable; never auto-run.
 */

#include <gtest/gtest.h>
#include <string>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include "../redis.h"
#include "../../shared/redis_integration_fixture.h"

// ProtocolMode / ProtocolModesTestBase / INSTANTIATE_PROTOCOL_MODES / run_coro_test_until /
// CO_IGNORE / PROTOCOL_ENSURE_RESP3_VAR come from redis_integration_fixture.h (legacy-compatible
// global re-export).

namespace {

class ServerIntrospectionTest : public ProtocolModesTestBase {};

INSTANTIATE_PROTOCOL_MODES(ServerIntrospectionTest);

// =============== INFO ===============

// INFO (full + section) always arrives as a server-side bulk string; the json parser keeps it a
// json STRING (it is not JSON-structural), in BOTH RESP2 and RESP3. Assert that exact shape and
// the presence of the version/section markers.
TEST_P(ServerIntrospectionTest, InfoIsStringWithSectionMarkers) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);

        auto full = co_await redis.info();
        EXPECT_TRUE(full.ok()) << full.error();
        EXPECT_TRUE(full.result().is_string());
        EXPECT_NE(full.result().get<std::string>().find("redis_version"), std::string::npos);

        auto mem = co_await redis.info("memory");
        EXPECT_TRUE(mem.ok()) << mem.error();
        EXPECT_TRUE(mem.result().is_string());
        EXPECT_NE(mem.result().get<std::string>().find("used_memory"), std::string::npos);

        auto clients = co_await redis.info("clients");
        EXPECT_TRUE(clients.ok()) << clients.error();
        EXPECT_TRUE(clients.result().is_string());
        EXPECT_NE(clients.result().get<std::string>().find("connected_clients"), std::string::npos);

        completed = true;
    });
    run_coro_test_until(completed);
}

// =============== TIME ===============

// TIME returns a (seconds, microseconds) pair; seconds is a current epoch (well past 2020),
// microseconds in [0, 1e6).
TEST_P(ServerIntrospectionTest, TimeReturnsPlausibleEpochPair) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);

        auto t = co_await redis.time();
        EXPECT_TRUE(t.ok()) << t.error();
        EXPECT_GT(t.result().first, 1577836800LL); // 2020-01-01
        EXPECT_GE(t.result().second, 0);
        EXPECT_LT(t.result().second, 1000000);

        completed = true;
    });
    run_coro_test_until(completed);
}

// =============== ROLE ===============

// ROLE returns a HETEROGENEOUS array, not a flat string list (confirmed via
// `redis-cli ROLE`: ["master", <repl_offset:int>, <replicas:array>] on a standalone
// master). The framework therefore yields a qb::json; assert its concrete shape:
// element [0] is the role token (string), [1] the replication offset (integer >= 0),
// [2] the replicas array (empty in standalone).
TEST_P(ServerIntrospectionTest, RoleReportsMaster) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);

        auto role = co_await redis.role();
        EXPECT_TRUE(role.ok()) << role.error();
        if (!role.result().is_array() || role.result().size() < 3u) {
            ADD_FAILURE() << "ROLE must be a >=3-element array, got size "
                          << (role.result().is_array() ? role.result().size() : 0);
            completed = true;
            co_return;
        }
        // [0] role token.
        if (!role.result()[0].is_string()) {
            ADD_FAILURE() << "ROLE[0] must be a string role token";
            completed = true;
            co_return;
        }
        const std::string role_token = role.result()[0].get<std::string>();
        EXPECT_TRUE(role_token == "master" || role_token == "slave" || role_token == "sentinel")
            << role_token;
        // For a standalone server it is specifically "master".
        EXPECT_EQ(role_token, "master");
        // [1] replication offset (non-negative integer).
        EXPECT_TRUE(role.result()[1].is_number()) << "ROLE[1] must be the replication offset";
        EXPECT_GE(role.result()[1].get<long long>(), 0);
        // [2] connected replicas (empty array in standalone).
        EXPECT_TRUE(role.result()[2].is_array()) << "ROLE[2] must be the replicas array";
        EXPECT_EQ(role.result()[2].size(), 0u);

        completed = true;
    });
    run_coro_test_until(completed);
}

// =============== SLOWLOG ===============

// SLOWLOG GET is an ARRAY (its first element is an entry, not a string key → the json parser
// leaves it an array). reset → len==0 is the deterministic invariant.
TEST_P(ServerIntrospectionTest, SlowlogGetIsArrayAndResetZeroesLen) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);

        auto len = co_await redis.slowlog_len();
        EXPECT_TRUE(len.ok()) << len.error();
        EXPECT_GE(len.result(), 0);

        auto entries = co_await redis.slowlog_get();
        EXPECT_TRUE(entries.ok()) << entries.error();
        EXPECT_TRUE(entries.result().is_array());
        for (const auto &entry : entries.result()) {
            // Each entry is itself an array: [id, ts, micros, argv, addr, name].
            EXPECT_TRUE(entry.is_array());
            EXPECT_GE(entry.size(), 4u);
        }

        auto limited = co_await redis.slowlog_get(5);
        EXPECT_TRUE(limited.ok()) << limited.error();
        EXPECT_TRUE(limited.result().is_array());
        EXPECT_LE(limited.result().size(), 5u);

        auto reset = co_await redis.slowlog_reset();
        EXPECT_TRUE(reset.ok()) << reset.error();

        auto after = co_await redis.slowlog_len();
        EXPECT_TRUE(after.ok()) << after.error();
        EXPECT_EQ(after.result(), 0);

        completed = true;
    });
    run_coro_test_until(completed);
}

// =============== MEMORY ===============

// MEMORY USAGE > 0 for an existing key; MEMORY STATS is an OBJECT (flat-map with numeric values
// → json object via the parser heuristic); doctor/help are non-empty strings.
TEST_P(ServerIntrospectionTest, MemoryUsageStatsDoctorHelp) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);

        const std::string key = protocol_key("mem_target");
        auto              set = co_await redis.set(key, "value");
        EXPECT_TRUE(set.ok()) << set.error();

        auto usage = co_await redis.memory_usage(key);
        EXPECT_TRUE(usage.ok()) << usage.error();
        EXPECT_GT(usage.result(), 0);

        auto usage_s = co_await redis.memory_usage(key, 5);
        EXPECT_TRUE(usage_s.ok()) << usage_s.error();
        EXPECT_GT(usage_s.result(), 0);

        auto stats = co_await redis.memory_stats();
        EXPECT_TRUE(stats.ok()) << stats.error();
        EXPECT_TRUE(stats.result().is_object());
        EXPECT_TRUE(stats.result().contains("peak.allocated")
                    || stats.result().contains("total.allocated"));

        auto doctor = co_await redis.memory_doctor();
        EXPECT_TRUE(doctor.ok()) << doctor.error();
        EXPECT_FALSE(doctor.result().empty());

        auto help = co_await redis.memory_help();
        EXPECT_TRUE(help.ok()) << help.error();
        EXPECT_FALSE(help.result().empty());

        auto purge = co_await redis.memory_purge();
        EXPECT_TRUE(purge.ok()) << purge.error();

        completed = true;
    });
    run_coro_test_until(completed);
}

// =============== DATABASE ===============

// dbsize after seeding, flushdb (sync) → 0, then async flushdb polled to 0 (no fixed sleep).
TEST_P(ServerIntrospectionTest, DbsizeFlushSyncAndAsync) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);

        CO_IGNORE(co_await redis.set(protocol_key("db1"), "v1"));
        CO_IGNORE(co_await redis.set(protocol_key("db2"), "v2"));

        auto size = co_await redis.dbsize();
        EXPECT_TRUE(size.ok()) << size.error();
        EXPECT_GE(size.result(), 2);

        auto flush = co_await redis.flushdb();
        EXPECT_TRUE(flush.ok()) << flush.error();

        auto size2 = co_await redis.dbsize();
        EXPECT_TRUE(size2.ok()) << size2.error();
        EXPECT_EQ(size2.result(), 0);

        // Re-seed, then async-flush and poll dbsize to 0 (the lazy free runs in the background).
        CO_IGNORE(co_await redis.set(protocol_key("db1"), "v1"));
        CO_IGNORE(co_await redis.set(protocol_key("db2"), "v2"));

        auto async_flush = co_await redis.flushdb(true);
        EXPECT_TRUE(async_flush.ok()) << async_flush.error();

        long long observed = -1;
        for (int i = 0; i < 200; ++i) {
            auto poll = co_await redis.dbsize();
            EXPECT_TRUE(poll.ok()) << poll.error();
            observed = poll.result();
            if (observed == 0)
                break;
        }
        EXPECT_EQ(observed, 0);

        completed = true;
    });
    run_coro_test_until(completed);
}

// =============== SYNC (DISABLED_ — destructive replication probe) ===============
// SYNC/PSYNC are replication commands; on a standalone server they either stream an RDB or error.
// Either way the call must not throw out of the reply handler — assert the request resolves.
// Kept DISABLED_ (risky on a shared daemon): run manually with --gtest_also_run_disabled_tests.

TEST_P(ServerIntrospectionTest, DISABLED_SyncPsyncResolve) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);

        auto sync_r = co_await redis.sync();
        // Standalone: typically an error. Either outcome is a resolved reply (no throw).
        if (!sync_r.ok())
            EXPECT_FALSE(std::string(sync_r.error()).empty());

        auto psync_r = co_await redis.psync("?", -1);
        if (!psync_r.ok())
            EXPECT_FALSE(std::string(psync_r.error()).empty());

        completed = true;
    });
    run_coro_test_until(completed);
}

// =============== PERSISTENCE (DISABLED_ — forks + writes to disk) ===============
// BGSAVE/BGREWRITEAOF fork the server and write RDB/AOF to disk — disruptive on a shared daemon.
// Kept DISABLED_: run manually with --gtest_also_run_disabled_tests.

TEST_P(ServerIntrospectionTest, DISABLED_PersistenceBgsaveLastsave) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);

        // lastsave is a monotonic epoch timestamp and always available.
        auto last_before = co_await redis.lastsave();
        EXPECT_TRUE(last_before.ok()) << last_before.error();
        EXPECT_GT(last_before.result(), 0);

        // bgrewriteaof / bgsave kick off background work; tolerate the "already in progress"
        // class of error but assert the error text when it fails (never swallow).
        auto bgaof = co_await redis.bgrewriteaof();
        if (!bgaof.ok())
            EXPECT_FALSE(std::string(bgaof.error()).empty());

        auto bgsave = co_await redis.bgsave();
        if (!bgsave.ok())
            EXPECT_FALSE(std::string(bgsave.error()).empty());

        completed = true;
    });
    run_coro_test_until(completed);
}

// =============== LATENCY (ENABLED — diagnostic only, no label) ===============
// LATENCY LATEST/HISTORY are read-only; LATENCY RESET only clears the shared latency-monitor
// samples (no user-data risk). Verified safe via `redis-cli LATENCY RESET/LATEST/HISTORY` on
// Redis 8.8. Runs by default with no extra label — it is no more destructive than the SLOWLOG
// RESET this binary already runs unconditionally, and CTest labels are per-binary.

TEST_P(ServerIntrospectionTest, LatencyLatestHistoryReset) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);

        auto latest = co_await redis.latency_latest();
        EXPECT_TRUE(latest.ok()) << latest.error();
        EXPECT_TRUE(latest.result().is_array());

        // Generate some traffic, then HISTORY for a known event name.
        CO_IGNORE(co_await redis.ping());
        CO_IGNORE(co_await redis.set(protocol_key("lat_key"), "v"));

        auto history = co_await redis.latency_history("command");
        EXPECT_TRUE(history.ok()) << history.error();
        EXPECT_TRUE(history.result().is_array());

        auto reset = co_await redis.latency_reset();
        EXPECT_TRUE(reset.ok()) << reset.error();
        EXPECT_GE(reset.result(), 0); // number of events reset

        completed = true;
    });
    run_coro_test_until(completed);
}

} // namespace
