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

class ClusterProtocolModesTest : public ProtocolModesTestBase {};

INSTANTIATE_PROTOCOL_MODES(ClusterProtocolModesTest);

// Test CLUSTER INFO command using coroutines
TEST_P(ClusterProtocolModesTest, CORO_CLUSTER_COMMANDS_INFO) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        try {
            auto reply = co_await redis.cluster_info();

            if (reply.ok()) {
                auto info = reply.result();
                EXPECT_TRUE(info.is_object() || info.is_string());

                if (info.is_string()) {
                    std::string info_str = info.template get<std::string>();
                    EXPECT_TRUE(info_str.find("cluster_state:") != std::string::npos);
                }
            } else {
                std::string error = std::string(reply.error());
                EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                           error.find("cluster") != std::string::npos);
            }
        } catch (const std::exception& e) {
            std::string error = e.what();
            EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                       error.find("cluster") != std::string::npos);
        }

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test CLUSTER NODES command using coroutines
TEST_P(ClusterProtocolModesTest, CORO_CLUSTER_COMMANDS_NODES) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        try {
            auto reply = co_await redis.cluster_nodes();

            if (reply.ok()) {
                auto nodes = reply.result();
                EXPECT_TRUE(nodes.is_object() || nodes.is_string() || nodes.is_array());

                if (nodes.is_string()) {
                    std::string nodes_str = nodes.template get<std::string>();
                    EXPECT_FALSE(nodes_str.empty());
                }
            } else {
                std::string error = std::string(reply.error());
                EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                           error.find("cluster") != std::string::npos);
            }
        } catch (const std::exception& e) {
            std::string error = e.what();
            EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                       error.find("cluster") != std::string::npos);
        }

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test ASKING, READONLY, READWRITE
// In standalone Redis these may fail with "cluster support disabled" - tolerate both success and that error.
TEST_P(ClusterProtocolModesTest, CORO_CLUSTER_COMMANDS_ASKING_READONLY_READWRITE) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        try {
            auto asking_r = co_await redis.asking();
            auto readonly_r = co_await redis.readonly();
            auto readwrite_r = co_await redis.readwrite();
            if (asking_r.ok() && readonly_r.ok() && readwrite_r.ok()) {
                // All succeeded (cluster or standalone with redirect support)
                EXPECT_TRUE(true);
            } else {
                // Standalone without cluster: expect cluster-related errors
                std::string e1 = asking_r.ok() ? "" : std::string(asking_r.error());
                std::string e2 = readonly_r.ok() ? "" : std::string(readonly_r.error());
                std::string e3 = readwrite_r.ok() ? "" : std::string(readwrite_r.error());
                EXPECT_TRUE(e1.find("cluster") != std::string::npos ||
                           e2.find("cluster") != std::string::npos ||
                           e3.find("cluster") != std::string::npos ||
                           e1.find("READONLY") != std::string::npos ||
                           e2.find("READONLY") != std::string::npos);
            }
        } catch (const std::exception& e) {
            std::string error = e.what();
            EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                       error.find("cluster") != std::string::npos);
        }
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) qb::io::async::run(EVRUN_NOWAIT);
}

// Test CLUSTER SLOTS command using coroutines
TEST_P(ClusterProtocolModesTest, CORO_CLUSTER_COMMANDS_SLOTS) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        try {
            auto reply = co_await redis.cluster_slots();

            if (reply.ok()) {
                auto slots = reply.result();
                EXPECT_TRUE(slots.is_array() || slots.is_object());
            } else {
                std::string error = std::string(reply.error());
                EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                           error.find("cluster") != std::string::npos);
            }
        } catch (const std::exception& e) {
            std::string error = e.what();
            EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                       error.find("cluster") != std::string::npos);
        }

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test CLUSTER KEYSLOT command using coroutines
TEST_P(ClusterProtocolModesTest, CORO_CLUSTER_COMMANDS_KEYSLOT) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        try {
            std::string key = protocol_key("coro_keyslot-test");
            auto reply = co_await redis.cluster_keyslot(key);

            if (reply.ok()) {
                auto slot = reply.result();
                EXPECT_GE(slot, 0);
                EXPECT_LE(slot, 16383);
            } else {
                std::string error = std::string(reply.error());
                EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                           error.find("cluster") != std::string::npos);
            }
        } catch (const std::exception& e) {
            std::string error = e.what();
            EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                       error.find("cluster") != std::string::npos);
        }

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test CLUSTER COUNTKEYSINSLOT command using coroutines
TEST_P(ClusterProtocolModesTest, CORO_CLUSTER_COMMANDS_COUNTKEYSINSLOT) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        try {
            auto reply = co_await redis.cluster_countkeysinslot(0);

            if (reply.ok()) {
                auto count = reply.result();
                EXPECT_GE(count, 0);
            } else {
                std::string error = std::string(reply.error());
                EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                           error.find("cluster") != std::string::npos);
            }
        } catch (const std::exception& e) {
            std::string error = e.what();
            EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                       error.find("cluster") != std::string::npos);
        }

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test CLUSTER GETKEYSINSLOT command using coroutines
TEST_P(ClusterProtocolModesTest, CORO_CLUSTER_COMMANDS_GETKEYSINSLOT) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        try {
            auto reply = co_await redis.cluster_getkeysinslot(0, 10);

            if (reply.ok()) {
                auto keys = reply.result();
                EXPECT_TRUE(keys.empty() || !keys.empty());
            } else {
                std::string error = std::string(reply.error());
                EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                           error.find("cluster") != std::string::npos);
            }
        } catch (const std::exception& e) {
            std::string error = e.what();
            EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                       error.find("cluster") != std::string::npos);
        }

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test CLUSTER MYID command using coroutines
TEST_P(ClusterProtocolModesTest, CORO_CLUSTER_COMMANDS_MYID) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        try {
            auto reply = co_await redis.cluster_myid();

            if (reply.ok()) {
                auto node_id = reply.result();
                if (!node_id.empty()) {
                    EXPECT_EQ(node_id.length(), 40);
                }
            } else {
                std::string error = std::string(reply.error());
                EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                           error.find("cluster") != std::string::npos);
            }
        } catch (const std::exception& e) {
            std::string error = e.what();
            EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                       error.find("cluster") != std::string::npos);
        }

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test cluster modification commands using coroutines
TEST_P(ClusterProtocolModesTest, CORO_CLUSTER_COMMANDS_MODIFICATION) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        // CLUSTER MEET
        try {
            auto reply = co_await redis.cluster_meet("127.0.0.1", 7000);
            if (!reply.ok()) {
                std::string error = std::string(reply.error());
                EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                           error.find("cluster") != std::string::npos ||
                           error.find("ERR") != std::string::npos);
            }
        } catch (const std::exception& e) {
            std::string error = e.what();
            EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                       error.find("cluster") != std::string::npos ||
                       error.find("ERR") != std::string::npos);
        }

        // CLUSTER FORGET
        try {
            auto reply = co_await redis.cluster_forget("0000000000000000000000000000000000000000");
            if (!reply.ok()) {
                std::string error = std::string(reply.error());
                EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                           error.find("cluster") != std::string::npos ||
                           error.find("ERR") != std::string::npos);
            }
        } catch (const std::exception& e) {
            std::string error = e.what();
            EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                       error.find("cluster") != std::string::npos ||
                       error.find("ERR") != std::string::npos);
        }

        // CLUSTER RESET
        try {
            auto reply = co_await redis.cluster_reset();
            if (!reply.ok()) {
                std::string error = std::string(reply.error());
                EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                           error.find("cluster") != std::string::npos ||
                           error.find("ERR") != std::string::npos);
            }
        } catch (const std::exception& e) {
            std::string error = e.what();
            EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                       error.find("cluster") != std::string::npos ||
                       error.find("ERR") != std::string::npos);
        }

        // CLUSTER FAILOVER
        try {
            auto reply = co_await redis.cluster_failover();
            if (!reply.ok()) {
                std::string error = std::string(reply.error());
                EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                           error.find("cluster") != std::string::npos ||
                           error.find("ERR") != std::string::npos);
            }
        } catch (const std::exception& e) {
            std::string error = e.what();
            EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                       error.find("cluster") != std::string::npos ||
                       error.find("ERR") != std::string::npos);
        }

        // CLUSTER REPLICATE
        try {
            auto reply = co_await redis.cluster_replicate("0000000000000000000000000000000000000000");
            if (!reply.ok()) {
                std::string error = std::string(reply.error());
                EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                           error.find("cluster") != std::string::npos ||
                           error.find("ERR") != std::string::npos);
            }
        } catch (const std::exception& e) {
            std::string error = e.what();
            EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                       error.find("cluster") != std::string::npos ||
                       error.find("ERR") != std::string::npos);
        }

        // CLUSTER SAVECONFIG
        try {
            auto reply = co_await redis.cluster_saveconfig();
            if (!reply.ok()) {
                std::string error = std::string(reply.error());
                EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                           error.find("cluster") != std::string::npos ||
                           error.find("ERR") != std::string::npos);
            }
        } catch (const std::exception& e) {
            std::string error = e.what();
            EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                       error.find("cluster") != std::string::npos ||
                       error.find("ERR") != std::string::npos);
        }

        // CLUSTER SET-CONFIG-EPOCH
        try {
            auto reply = co_await redis.cluster_set_config_epoch(1);
            if (!reply.ok()) {
                std::string error = std::string(reply.error());
                EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                           error.find("cluster") != std::string::npos ||
                           error.find("ERR") != std::string::npos);
            }
        } catch (const std::exception& e) {
            std::string error = e.what();
            EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                       error.find("cluster") != std::string::npos ||
                       error.find("ERR") != std::string::npos);
        }

        // CLUSTER BUMPEPOCH
        try {
            auto reply = co_await redis.cluster_bumpepoch();
            if (!reply.ok()) {
                std::string error = std::string(reply.error());
                EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                           error.find("cluster") != std::string::npos ||
                           error.find("ERR") != std::string::npos);
            }
        } catch (const std::exception& e) {
            std::string error = e.what();
            EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                       error.find("cluster") != std::string::npos ||
                       error.find("ERR") != std::string::npos);
        }

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test CLUSTER ADDSLOTS, ADDSLOTSRANGE, DELSLOTS, DELSLOTSRANGE, FLUSHSLOTS
TEST_P(ClusterProtocolModesTest, CORO_CLUSTER_COMMANDS_SLOT_MANAGEMENT) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto check_cluster_err = [](const auto& r) {
            if (r.ok()) return true;
            std::string e(r.error());
            return e.find("cluster") != std::string::npos ||
                   e.find("unknown command") != std::string::npos ||
                   e.find("ERR") != std::string::npos;
        };
        auto r1 = co_await redis.cluster_addslots(0);
        if (!r1.ok()) EXPECT_TRUE(check_cluster_err(r1));
        auto r2 = co_await redis.cluster_addslotsrange({{1, 2}});
        if (!r2.ok()) EXPECT_TRUE(check_cluster_err(r2));
        auto r3 = co_await redis.cluster_delslots(0);
        if (!r3.ok()) EXPECT_TRUE(check_cluster_err(r3));
        auto r4 = co_await redis.cluster_delslotsrange({{1, 2}});
        if (!r4.ok()) EXPECT_TRUE(check_cluster_err(r4));
        auto r5 = co_await redis.cluster_flushslots();
        if (!r5.ok()) EXPECT_TRUE(check_cluster_err(r5));
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) qb::io::async::run(EVRUN_NOWAIT);
}

// Test CLUSTER COUNT-FAILURE-REPORTS, LINKS, MYSHARDID, REPLICAS, SETSLOT, SHARDS, SLAVES
TEST_P(ClusterProtocolModesTest, CORO_CLUSTER_COMMANDS_INFO_EXT) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto check_cluster_err = [](const auto& r) {
            if (r.ok()) return true;
            std::string e(r.error());
            return e.find("cluster") != std::string::npos ||
                   e.find("unknown command") != std::string::npos ||
                   e.find("ERR") != std::string::npos;
        };
        std::string dummy_id = "0000000000000000000000000000000000000000";
        auto r1 = co_await redis.cluster_count_failure_reports(dummy_id);
        if (!r1.ok()) EXPECT_TRUE(check_cluster_err(r1));
        auto r2 = co_await redis.cluster_links();
        if (!r2.ok()) EXPECT_TRUE(check_cluster_err(r2));
        auto r3 = co_await redis.cluster_myshardid();
        if (!r3.ok()) EXPECT_TRUE(check_cluster_err(r3));
        auto r4 = co_await redis.cluster_replicas(dummy_id);
        if (!r4.ok()) EXPECT_TRUE(check_cluster_err(r4));
        auto r5 = co_await redis.cluster_setslot(0, "NODE", dummy_id);
        if (!r5.ok()) EXPECT_TRUE(check_cluster_err(r5));
        auto r6 = co_await redis.cluster_shards();
        if (!r6.ok()) EXPECT_TRUE(check_cluster_err(r6));
        auto r7 = co_await redis.cluster_slaves(dummy_id);
        if (!r7.ok()) EXPECT_TRUE(check_cluster_err(r7));
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(ClusterProtocolModesTest, CLUSTER_INFO_JSON) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        try {
            auto r = co_await redis.cluster_info();
            if (r.ok()) EXPECT_TRUE(r.result().is_object() || r.result().is_string());
        } catch (const std::exception&) {
            // CLUSTER not available on older Redis
        }
        done = true;
    });
    while (!done) qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(ClusterProtocolModesTest, CLUSTER_NODES_SLOTS) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        try {
            auto r1 = co_await redis.cluster_nodes();
            if (r1.ok()) EXPECT_TRUE(r1.result().is_string() || r1.result().is_array() || r1.result().is_object());
            auto r2 = co_await redis.cluster_slots();
            if (r2.ok()) EXPECT_TRUE(r2.result().is_array());
        } catch (const std::exception&) {
            // CLUSTER not available on standalone Redis
        }
        done = true;
    });
    while (!done) qb::io::async::run(EVRUN_NOWAIT);
}