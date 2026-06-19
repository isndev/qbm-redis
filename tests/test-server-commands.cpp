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
#include <thread>
#include "../redis.h"
#include "protocol_test_common.h"

using namespace qb::io;
using namespace std::chrono;

// ============================================================================
// Fixture: all tests run in both RESP2 and RESP3
// ============================================================================

class ServerProtocolModesTest : public ProtocolModesTestBase {};

INSTANTIATE_PROTOCOL_MODES(ServerProtocolModesTest);

// =============== CLIENT MANAGEMENT COMMANDS ===============

TEST_P(ServerProtocolModesTest, CORO_SERVER_CLIENT_MANAGEMENT) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        // Test client_setname
        auto setname_reply = co_await redis.client_setname("test_client_coro");
        EXPECT_TRUE(setname_reply.ok());

        // Test client_getname
        auto getname_reply = co_await redis.client_getname();
        EXPECT_TRUE(getname_reply.ok());
        EXPECT_TRUE(getname_reply.result().has_value());
        EXPECT_EQ(*getname_reply.result(), "test_client_coro");

        // Test client_list — CLIENT LIST returns a bulk string, not JSON
        auto list_reply = co_await redis.client_list();
        EXPECT_TRUE(list_reply.ok());
        // Result is a JSON string containing the raw CLIENT LIST output
        EXPECT_TRUE(list_reply.result().is_string() || list_reply.result().is_array());

        // Test client_tracking_info — CLIENT TRACKINGINFO returns a flat map (JSON object)
        auto tracking_info_reply = co_await redis.client_tracking_info();
        EXPECT_TRUE(tracking_info_reply.ok());
        // Redis 8+: may be object or array representation depending on RESP version
        EXPECT_TRUE(tracking_info_reply.result().is_object() || tracking_info_reply.result().is_array()
                    || !tracking_info_reply.result().is_null());

        // Test client_pause
        auto pause_reply = co_await redis.client_pause(100, "WRITE");
        EXPECT_TRUE(pause_reply.ok());

        // Immediately lift the pause. On Redis 8.x a CLIENT PAUSE holds every
        // subsequent command on this connection until the timeout elapses (and
        // in practice keeps the connection parked), which would otherwise wedge
        // the fixture's TearDown flushall(). Restore normal processing now.
        auto unpause_reply = co_await redis.client_unpause();
        EXPECT_TRUE(unpause_reply.ok());

        // Test client_tracking ON
        auto tracking_on_reply = co_await redis.client_tracking(true);
        EXPECT_TRUE(tracking_on_reply.ok());

        // Test client_tracking OFF
        auto tracking_off_reply = co_await redis.client_tracking(false);
        EXPECT_TRUE(tracking_off_reply.ok());

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// =============== FAILOVER, LATENCY, EXTENDED CLIENT COMMANDS ===============

TEST_P(ServerProtocolModesTest, CORO_SERVER_FAILOVER_LATENCY_CLIENT_EXT) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        // FAILOVER - requires replica; expect failure on standalone
        auto failover_r = co_await redis.failover();
        if (!failover_r.ok()) {
            std::string err(failover_r.error());
            EXPECT_TRUE(err.find("replica") != std::string::npos || err.find("ERR") != std::string::npos
                        || err.find("cluster") != std::string::npos);
        }

        // LATENCY DOCTOR - returns string
        auto doctor_r = co_await redis.latency_doctor();
        EXPECT_TRUE(doctor_r.ok());
        EXPECT_FALSE(doctor_r.result().empty());

        // LATENCY GRAPH - event may not exist; some Redis versions return error
        auto graph_r = co_await redis.latency_graph("command");
        if (!graph_r.ok()) {
            std::string e(graph_r.error());
            EXPECT_TRUE(e.find("ERR") != std::string::npos || e.find("event") != std::string::npos);
        }

        // LATENCY HISTOGRAM - returns array; format may vary by Redis version
        auto hist_r = co_await redis.latency_histogram();
        EXPECT_TRUE(hist_r.ok());
        if (hist_r.ok() && !hist_r.result().is_null()) {
            EXPECT_TRUE(hist_r.result().is_array() || hist_r.result().is_object());
        }

        // CLIENT CACHING - for client-side caching; may fail without tracking
        auto caching_r = co_await redis.client_caching(true);
        if (!caching_r.ok()) {
            std::string e(caching_r.error());
            EXPECT_TRUE(e.find("ERR") != std::string::npos || e.find("tracking") != std::string::npos);
        }

        // CLIENT GETREDIR - returns redirect ID or -1
        auto getredir_r = co_await redis.client_getredir();
        EXPECT_TRUE(getredir_r.ok());

        // CLIENT INFO - returns client info string
        auto info_r = co_await redis.client_info();
        EXPECT_TRUE(info_r.ok());
        EXPECT_FALSE(info_r.result().empty());

        // CLIENT NO-EVICT
        auto noevict_r = co_await redis.client_no_evict(false);
        EXPECT_TRUE(noevict_r.ok());

        // CLIENT NO-TOUCH
        auto notouch_r = co_await redis.client_no_touch(false);
        EXPECT_TRUE(notouch_r.ok());

        // CLIENT REPLY ON|OFF|SKIP
        auto reply_r = co_await redis.client_reply("ON");
        EXPECT_TRUE(reply_r.ok());

        // CLIENT SETINFO - set client metadata
        auto setinfo_r = co_await redis.client_setinfo("lib-name", "qb-redis-test");
        EXPECT_TRUE(setinfo_r.ok());

        // CLIENT UNPAUSE - may fail if no pause
        auto unpause_r = co_await redis.client_unpause();
        if (!unpause_r.ok()) {
            std::string err(unpause_r.error());
            EXPECT_TRUE(err.find("UNPAUSE") != std::string::npos || err.find("ERR") != std::string::npos);
        }

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// =============== CONFIGURATION COMMANDS ===============

TEST_P(ServerProtocolModesTest, CORO_SERVER_CONFIGURATION) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        // Test config_get (format may differ between RESP2 and RESP3; may be disabled)
        auto maxmemory_reply = co_await redis.config_get("maxmemory");
        if (maxmemory_reply.ok()) {
            auto maxmemory_pairs = maxmemory_reply.result();
            if (!maxmemory_pairs.empty()) {
                auto set_reply = co_await redis.config_set("maxmemory", maxmemory_pairs[0].second);
                EXPECT_TRUE(set_reply.ok());
            }
        }

        // Test config_get with pattern
        auto configs_reply = co_await redis.config_get("*max*");
        if (configs_reply.ok()) {
            EXPECT_FALSE(configs_reply.result().empty());
        }

        // Test config_resetstat
        auto reset_reply = co_await redis.config_resetstat();
        EXPECT_TRUE(reset_reply.ok());

        // Test config_rewrite (may fail without permissions)
        try {
            auto rewrite_reply = co_await redis.config_rewrite();
            EXPECT_TRUE(rewrite_reply.ok());
        } catch (const std::exception &e) {
            std::cerr << "config_rewrite failed (might lack permissions): " << e.what() << std::endl;
        }
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// =============== COMMAND INFORMATION COMMANDS ===============

TEST_P(ServerProtocolModesTest, CORO_SERVER_COMMAND_INFORMATION) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        // Test command with specific commands
        std::vector<std::string> specific_cmds = {"get", "set"};
        auto                     cmd_reply     = co_await redis.command(specific_cmds);
        EXPECT_TRUE(cmd_reply.ok());
        auto cmd_json_info = cmd_reply.result();
        EXPECT_FALSE(cmd_json_info.empty());

        bool has_get = false, has_set = false;
        if (cmd_json_info.is_object()) {
            has_get = cmd_json_info.contains("get");
            has_set = cmd_json_info.contains("set");
        } else if (cmd_json_info.is_array()) {
            for (const auto &cmd : cmd_json_info) {
                if (cmd.is_array() && cmd.size() > 0 && cmd[0].is_string()) {
                    std::string name = cmd[0].get<std::string>();
                    if (name == "get")
                        has_get = true;
                    else if (name == "set")
                        has_set = true;
                } else if (cmd.is_object() && cmd.contains("name")) {
                    std::string name = cmd["name"].get<std::string>();
                    if (name == "get")
                        has_get = true;
                    else if (name == "set")
                        has_set = true;
                }
            }
        }
        EXPECT_TRUE(has_get);
        EXPECT_TRUE(has_set);

        // Test command (all commands)
        auto all_reply = co_await redis.command();
        EXPECT_TRUE(all_reply.ok());
        auto cmd_json_all = all_reply.result();
        EXPECT_FALSE(cmd_json_all.empty());
        EXPECT_GT(cmd_json_all.size(), 10);

        bool has_ping = false;
        if (cmd_json_all.is_object()) {
            has_ping = cmd_json_all.contains("ping");
        } else if (cmd_json_all.is_array()) {
            for (const auto &cmd : cmd_json_all) {
                if (cmd.is_array() && cmd.size() > 0 && cmd[0].is_string()) {
                    if (cmd[0].get<std::string>() == "ping") {
                        has_ping = true;
                        break;
                    }
                } else if (cmd.is_object() && cmd.contains("name")) {
                    if (cmd["name"].get<std::string>() == "ping") {
                        has_ping = true;
                        break;
                    }
                }
            }
        }
        EXPECT_TRUE(has_ping);

        // Test command_stats — COMMAND STATS was removed in Redis 8.x (ERR unknown subcommand)
        // Kept in API for older Redis versions; skip assertion if not supported
        try {
            auto stats_reply = co_await redis.command_stats();
            if (stats_reply.ok()) {
                EXPECT_TRUE(stats_reply.result().contains("total_calls") || stats_reply.result().contains("cmdstat_get")
                            || stats_reply.result().is_object());
            }
        } catch (const std::exception &e) {
            // Not supported on this Redis version — acceptable
        }

        // Test command_count
        auto count_reply = co_await redis.command_count();
        EXPECT_TRUE(count_reply.ok());
        EXPECT_GT(count_reply.result(), 0);

        // Test command_getkeys
        std::vector<std::string> getkeys_args = {"test:key", "value"};
        auto                     keys_reply   = co_await redis.command_getkeys("set", getkeys_args);
        EXPECT_TRUE(keys_reply.ok());
        EXPECT_FALSE(keys_reply.result().empty());
        EXPECT_EQ(keys_reply.result()[0], "test:key");

        // Test command_docs
        auto docs_reply = co_await redis.command_docs({"GET", "SET"});
        EXPECT_TRUE(docs_reply.ok());
        EXPECT_TRUE(docs_reply.result().is_array() || docs_reply.result().is_object());

        // Test command_list (returns std::vector<std::string>)
        auto list_reply = co_await redis.command_list();
        EXPECT_TRUE(list_reply.ok());
        EXPECT_FALSE(list_reply.result().empty());

        // Test command_getkeysandflags
        auto keysflags_reply = co_await redis.command_getkeysandflags("GET", {"mykey"});
        EXPECT_TRUE(keysflags_reply.ok());
        EXPECT_TRUE(keysflags_reply.result().is_array());

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// =============== DEBUG COMMANDS ===============

TEST_P(ServerProtocolModesTest, DISABLED_CORO_SERVER_DEBUG_COMMANDS) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        // Create test data
        std::string key   = protocol_key("debug_test");
        std::string value = "test_value";
        (void) co_await redis.set(key, value);

        // Test debug_object
        auto debug_reply = co_await redis.debug_object(key);
        if (debug_reply.ok()) {
            auto debug_info = debug_reply.result();
            EXPECT_FALSE(debug_info.empty());
            EXPECT_TRUE(debug_info.find("encoding") != std::string::npos || debug_info.find("refcount") != std::string::npos
                        || debug_info.find("serializedlength") != std::string::npos);
        }

        // Test debug_sleep (with a very short delay)
        try {
            auto sleep_reply = co_await redis.debug_sleep(std::chrono::milliseconds(10));
            EXPECT_TRUE(sleep_reply.ok());
        } catch (const std::exception &e) {
            std::cerr << "debug_sleep failed: " << e.what() << std::endl;
        }
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// =============== MEMORY MANAGEMENT COMMANDS ===============

TEST_P(ServerProtocolModesTest, CORO_SERVER_MEMORY_MANAGEMENT) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        // Create test data
        std::string key   = protocol_key("memory_test");
        std::string value = "test_value";
        (void) co_await redis.set(key, value);

        // Test memory_usage
        auto usage_reply = co_await redis.memory_usage(key);
        EXPECT_TRUE(usage_reply.ok());
        EXPECT_GT(usage_reply.result(), 0);

        // Test memory_usage with samples
        auto usage_samples_reply = co_await redis.memory_usage(key, 5);
        EXPECT_TRUE(usage_samples_reply.ok());
        EXPECT_GT(usage_samples_reply.result(), 0);

        // Test memory_stats (JSON version)
        auto stats_reply = co_await redis.memory_stats();
        EXPECT_TRUE(stats_reply.ok());
        EXPECT_TRUE(stats_reply.result().contains("peak.allocated") || stats_reply.result().contains("total.allocated"));

        // Test memory_doctor
        auto doctor_reply = co_await redis.memory_doctor();
        EXPECT_TRUE(doctor_reply.ok());
        EXPECT_FALSE(doctor_reply.result().empty());

        // Test memory_help
        auto help_reply = co_await redis.memory_help();
        EXPECT_TRUE(help_reply.ok());
        EXPECT_FALSE(help_reply.result().empty());

        // Test memory_malloc_stats
        try {
            auto malloc_reply = co_await redis.memory_malloc_stats();
            EXPECT_TRUE(malloc_reply.ok());
            EXPECT_FALSE(malloc_reply.result().empty());
        } catch (const std::exception &e) {
            std::cerr << "memory_malloc_stats failed: " << e.what() << std::endl;
        }

        // Test memory_purge
        auto purge_reply = co_await redis.memory_purge();
        EXPECT_TRUE(purge_reply.ok());

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// =============== SLOWLOG COMMANDS ===============

TEST_P(ServerProtocolModesTest, CORO_SERVER_SLOWLOG) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        // Test slowlog_len
        auto len_reply = co_await redis.slowlog_len();
        EXPECT_TRUE(len_reply.ok());
        EXPECT_GE(len_reply.result(), 0);

        // Test slowlog_get (JSON version)
        auto entries_reply = co_await redis.slowlog_get();
        EXPECT_TRUE(entries_reply.ok());
        auto entries_json = entries_reply.result();
        EXPECT_TRUE(entries_json.is_array());
        if (!entries_json.empty()) {
            for (const auto &entry : entries_json) {
                if (entry.is_object()) {
                    EXPECT_TRUE(entry.contains("id") || entry.contains("command"));
                } else if (entry.is_array()) {
                    EXPECT_GE(entry.size(), 4);
                } else {
                    ADD_FAILURE() << "Unexpected entry format: " << entry.dump();
                }
            }
        }

        // Test slowlog_get with limit
        auto entries_limit_reply = co_await redis.slowlog_get(5);
        EXPECT_TRUE(entries_limit_reply.ok());
        EXPECT_TRUE(entries_limit_reply.result().is_array());
        EXPECT_LE(entries_limit_reply.result().size(), 5);

        // Test slowlog_reset
        auto reset_reply = co_await redis.slowlog_reset();
        EXPECT_TRUE(reset_reply.ok());

        // Verify reset worked
        auto new_len_reply = co_await redis.slowlog_len();
        EXPECT_TRUE(new_len_reply.ok());
        EXPECT_EQ(new_len_reply.result(), 0);

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// =============== SYNC COMMANDS ===============

TEST_P(ServerProtocolModesTest, DISABLED_CORO_SERVER_SYNC_COMMANDS) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        // Test sync - this is a replication command and might not work in all environments
        auto sync_reply = co_await redis.sync();
        if (!sync_reply.ok()) {
            std::cerr << "Coro SYNC failed (expected in standalone): " << sync_reply.error() << std::endl;
        }

        // Test psync - this is a replication command and might not work in all environments
        auto psync_reply = co_await redis.psync("?", -1);
        if (!psync_reply.ok()) {
            std::cerr << "Coro PSYNC failed (expected in standalone): " << psync_reply.error() << std::endl;
        }
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// =============== PERSISTENCE COMMANDS ===============

TEST_P(ServerProtocolModesTest, DISABLED_CORO_SERVER_PERSISTENCE) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        // Test bgrewriteaof
        try {
            auto bgrewriteaof_reply = co_await redis.bgrewriteaof();
            if (bgrewriteaof_reply.ok()) {
                // OK
            }
        } catch (const std::exception &e) {
            std::cerr << "bgrewriteaof failed: " << e.what() << std::endl;
        }

        // Test bgsave
        try {
            auto bgsave_reply = co_await redis.bgsave();
            if (bgsave_reply.ok()) {
                // OK
            }
        } catch (const std::exception &e) {
            std::cerr << "bgsave failed: " << e.what() << std::endl;
        }

        // Test bgsave with schedule
        try {
            auto bgsave_schedule_reply = co_await redis.bgsave(true);
            if (bgsave_schedule_reply.ok()) {
                // OK
            }
        } catch (const std::exception &e) {
            std::cerr << "bgsave(schedule) failed: " << e.what() << std::endl;
        }

        // Test save - this blocks the server and might timeout in some environments
        try {
            auto save_reply = co_await redis.save();
            if (save_reply.ok()) {
                // OK
            }
        } catch (const std::exception &e) {
            std::cerr << "save failed: " << e.what() << std::endl;
        }

        // Test lastsave
        auto lastsave_reply = co_await redis.lastsave();
        EXPECT_TRUE(lastsave_reply.ok());
        EXPECT_GT(lastsave_reply.result(), 0);

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// =============== DATABASE COMMANDS ===============

TEST_P(ServerProtocolModesTest, CORO_SERVER_DATABASE) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        // Create test data
        (void) co_await redis.set(protocol_key("db1"), "value1");
        (void) co_await redis.set(protocol_key("db2"), "value2");

        // Test dbsize
        auto dbsize_reply = co_await redis.dbsize();
        EXPECT_TRUE(dbsize_reply.ok());
        EXPECT_GE(dbsize_reply.result(), 2);

        // Test flushdb
        auto flushdb_reply = co_await redis.flushdb();
        EXPECT_TRUE(flushdb_reply.ok());

        // Verify flushdb worked
        auto dbsize2_reply = co_await redis.dbsize();
        EXPECT_TRUE(dbsize2_reply.ok());
        EXPECT_EQ(dbsize2_reply.result(), 0);

        // Recreate test data
        (void) co_await redis.set(protocol_key("db1"), "value1");
        (void) co_await redis.set(protocol_key("db2"), "value2");

        // Test async flushdb
        auto async_flushdb_reply = co_await redis.flushdb(true);
        EXPECT_TRUE(async_flushdb_reply.ok());

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Verify async flushdb worked
        auto dbsize3_reply = co_await redis.dbsize();
        EXPECT_TRUE(dbsize3_reply.ok());
        EXPECT_EQ(dbsize3_reply.result(), 0);

        // Test flushall
        auto flushall_reply = co_await redis.flushall();
        EXPECT_TRUE(flushall_reply.ok());

        // Test async flushall
        auto async_flushall_reply = co_await redis.flushall(true);
        EXPECT_TRUE(async_flushall_reply.ok());

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// =============== SERVER INFORMATION COMMANDS ===============

TEST_P(ServerProtocolModesTest, CORO_SERVER_INFORMATION) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        // Test info (JSON version)
        auto info_reply = co_await redis.info();
        EXPECT_TRUE(info_reply.ok());
        auto server_info_json = info_reply.result();
        if (server_info_json.is_object()) {
            EXPECT_TRUE(server_info_json.contains("redis_version")
                        || (server_info_json.contains("server") && server_info_json["server"].is_object()
                            && server_info_json["server"].contains("redis_version")));
        } else if (server_info_json.is_string()) {
            EXPECT_TRUE(server_info_json.get<std::string>().find("redis_version") != std::string::npos);
        } else {
            ADD_FAILURE() << "Unexpected INFO format";
        }

        // Test info with section
        auto memory_reply = co_await redis.info("memory");
        EXPECT_TRUE(memory_reply.ok());
        auto memory_info_json = memory_reply.result();
        if (memory_info_json.is_object()) {
            EXPECT_TRUE(memory_info_json.contains("used_memory")
                        || (memory_info_json.contains("memory") && memory_info_json["memory"].is_object()
                            && memory_info_json["memory"].contains("used_memory")));
        } else if (memory_info_json.is_string()) {
            EXPECT_TRUE(memory_info_json.get<std::string>().find("used_memory") != std::string::npos);
        } else {
            ADD_FAILURE() << "Unexpected INFO MEMORY format";
        }

        // Test info clients section
        auto clients_reply = co_await redis.info("clients");
        EXPECT_TRUE(clients_reply.ok());
        auto clients_section_json = clients_reply.result();
        if (clients_section_json.is_object()) {
            EXPECT_TRUE(clients_section_json.contains("connected_clients")
                        || (clients_section_json.contains("clients") && clients_section_json["clients"].is_object()
                            && clients_section_json["clients"].contains("connected_clients")));
        } else if (clients_section_json.is_string()) {
            EXPECT_TRUE(clients_section_json.get<std::string>().find("connected_clients") != std::string::npos);
        } else {
            ADD_FAILURE() << "Unexpected INFO CLIENTS format";
        }

        // Test time
        auto time_reply = co_await redis.time();
        EXPECT_TRUE(time_reply.ok());
        auto time_info = time_reply.result();
        EXPECT_GT(time_info.first, 0);
        EXPECT_GE(time_info.second, 0);

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// =============== ROLE COMMANDS ===============

TEST_P(ServerProtocolModesTest, CORO_SERVER_ROLE) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto role_reply = co_await redis.role();
        if (role_reply.ok()) {
            auto role_info = role_reply.result();
            EXPECT_FALSE(role_info.empty());
            if (!role_info.empty()) {
                EXPECT_TRUE(role_info[0] == "master" || role_info[0] == "slave" || role_info[0] == "sentinel");
            }
        }
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// =============== LATENCY COMMANDS ===============

TEST_P(ServerProtocolModesTest, DISABLED_CORO_SERVER_LATENCY_COMMANDS) {
    bool        completed  = false;
    std::string event_name = "command";
    auto        test_task  = [this, &completed, event_name]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        // LATENCY LATEST
        auto latest_reply = co_await redis.latency_latest();
        if (latest_reply.ok() && !latest_reply.result().empty()) {
            for (const auto &event : latest_reply.result()) {
                EXPECT_TRUE(event.is_object());
                EXPECT_TRUE(event.contains("event"));
            }
        }

        // Generate some commands
        (void) co_await redis.ping();
        (void) co_await redis.set(protocol_key("coro_latency_key"), "value");

        // LATENCY HISTORY
        auto history_reply = co_await redis.latency_history(event_name);
        if (history_reply.ok()) {
            EXPECT_TRUE(history_reply.result().is_array());
        }

        // LATENCY RESET (all)
        auto reset_reply = co_await redis.latency_reset();
        if (!reset_reply.ok()) {
            std::cerr << "Coro LATENCY RESET failed: " << reset_reply.error() << std::endl;
        }

        // LATENCY RESET (specific event)
        auto reset_specific_reply = co_await redis.latency_reset(event_name);
        if (!reset_specific_reply.ok()) {
            std::cerr << "Coro LATENCY RESET " << event_name << " failed: " << reset_specific_reply.error() << std::endl;
        }

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// =============== SHUTDOWN/SLAVE COMMANDS (DISABLED - Dangerous) ===============

TEST_P(ServerProtocolModesTest, DISABLED_CORO_SERVER_SHUTDOWN) {
    // Disabled by default as this would shut down the Redis server
    // Test shutdown commands would go here if enabled
}

TEST_P(ServerProtocolModesTest, DISABLED_CORO_SERVER_SLAVE) {
    // Disabled by default as this would change the server's replication configuration
    // Test slaveof commands would go here if enabled
}

TEST_P(ServerProtocolModesTest, DBSIZE) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto r = co_await redis.dbsize();
        EXPECT_TRUE(r.ok()) << r.error();
        if (r.ok())
            EXPECT_GE(r.result(), 0);
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(ServerProtocolModesTest, TIME) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto r = co_await redis.time();
        EXPECT_TRUE(r.ok()) << r.error();
        if (r.ok()) {
            EXPECT_GE(r.result().first, 0);
            EXPECT_GE(r.result().second, 0);
        }
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(ServerProtocolModesTest, CLIENT_ID_INTEGER) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto r = co_await redis.client_id();
        EXPECT_TRUE(r.ok()) << r.error();
        if (r.ok())
            EXPECT_GE(r.result(), 0);
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(ServerProtocolModesTest, INFO_JSON) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto r = co_await redis.info();
        EXPECT_TRUE(r.ok()) << r.error();
        if (r.ok())
            EXPECT_TRUE(r.result().is_object() || r.result().is_string());
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(ServerProtocolModesTest, FLUSHDB_FLUSHALL) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto r1 = co_await redis.flushdb();
        EXPECT_TRUE(r1.ok()) << r1.error();
        if (r1.ok())
            EXPECT_TRUE(r1.result().ok());
        auto r2 = co_await redis.flushall();
        EXPECT_TRUE(r2.ok()) << r2.error();
        if (r2.ok())
            EXPECT_TRUE(r2.result().ok());
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
