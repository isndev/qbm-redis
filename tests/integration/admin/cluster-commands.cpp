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
 * @file integration/admin/cluster-commands.cpp
 * @brief Live RESP2/RESP3 integration tests for the CLUSTER surface of `qb::redis::tcp::client`.
 *
 * Two distinct suites:
 *   - ClusterStandaloneTest — runs against the stock standalone daemon. On a non-cluster server
 *     EVERY cluster verb returns the exact error "This instance has cluster support disabled".
 *     The legacy file accepted any reply via `if(ok){...}else{find("ERR")}` + `catch{same}`, so
 *     the success branch was dead and the failure branch matched almost anything. Here each verb
 *     is asserted to fail with that *specific* substring — a changed/unknown error now fails.
 *   - ClusterRealTest (DISABLED_, REQUIRES_CLUSTER) — the positive flows that need a real cluster
 *     (INFO has cluster_state:, MYID is a 40-char id, KEYSLOT in [0,16383], SLOTS is an array).
 *
 * Deleted: the tautology `keys.empty()||!keys.empty()` and the 2 dups
 * (CLUSTER_INFO_JSON / CLUSTER_NODES_SLOTS), both strict subsets of the INFO/NODES/SLOTS bodies.
 */

#include <gtest/gtest.h>
#include <string>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include "../../shared/redis_integration_fixture.h"
#include "../redis.h"

// ProtocolMode / ProtocolModesTestBase / macros from redis_integration_fixture.h (global re-export).

namespace {

constexpr const char *kClusterDisabled = "cluster support disabled";

class ClusterStandaloneTest : public ProtocolModesTestBase {};

INSTANTIATE_PROTOCOL_MODES(ClusterStandaloneTest);

// All read-introspection verbs reject with the exact "cluster support disabled" error.
TEST_P(ClusterStandaloneTest, IntrospectionRejectedWhenDisabled) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);

        auto expect_disabled = [](const auto &r) {
            EXPECT_FALSE(r.ok());
            if (!r.ok()) {
                EXPECT_NE(std::string(r.error()).find(kClusterDisabled), std::string::npos) << r.error();
            }
        };

        expect_disabled(co_await redis.cluster_info());
        expect_disabled(co_await redis.cluster_nodes());
        expect_disabled(co_await redis.cluster_slots());
        expect_disabled(co_await redis.cluster_shards());
        expect_disabled(co_await redis.cluster_myid());
        expect_disabled(co_await redis.cluster_keyslot(protocol_key("ks")));
        expect_disabled(co_await redis.cluster_countkeysinslot(0));
        expect_disabled(co_await redis.cluster_getkeysinslot(0, 10));
        expect_disabled(co_await redis.cluster_links());
        expect_disabled(co_await redis.cluster_myshardid());

        completed = true;
    });
    run_coro_test_until(completed);
}

// All mutation/slot/replication verbs likewise reject with the exact disabled error.
TEST_P(ClusterStandaloneTest, MutationRejectedWhenDisabled) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);

        const std::string dummy_id        = "0000000000000000000000000000000000000000";
        auto              expect_disabled = [](const auto &r) {
            EXPECT_FALSE(r.ok());
            if (!r.ok()) {
                EXPECT_NE(std::string(r.error()).find(kClusterDisabled), std::string::npos) << r.error();
            }
        };

        expect_disabled(co_await redis.cluster_meet("127.0.0.1", 7000));
        expect_disabled(co_await redis.cluster_forget(dummy_id));
        expect_disabled(co_await redis.cluster_reset());
        expect_disabled(co_await redis.cluster_failover());
        expect_disabled(co_await redis.cluster_replicate(dummy_id));
        expect_disabled(co_await redis.cluster_saveconfig());
        expect_disabled(co_await redis.cluster_set_config_epoch(1));
        expect_disabled(co_await redis.cluster_bumpepoch());
        expect_disabled(co_await redis.cluster_addslots(0));
        expect_disabled(co_await redis.cluster_addslotsrange({{1, 2}}));
        expect_disabled(co_await redis.cluster_delslots(0));
        expect_disabled(co_await redis.cluster_delslotsrange({{1, 2}}));
        expect_disabled(co_await redis.cluster_flushslots());
        expect_disabled(co_await redis.cluster_count_failure_reports(dummy_id));
        expect_disabled(co_await redis.cluster_replicas(dummy_id));
        expect_disabled(co_await redis.cluster_setslot(0, "NODE", dummy_id));
        expect_disabled(co_await redis.cluster_slaves(dummy_id));

        completed = true;
    });
    run_coro_test_until(completed);
}

// ASKING / READONLY / READWRITE are redirect verbs; on a non-cluster server they reject with the
// same disabled error.
TEST_P(ClusterStandaloneTest, RedirectVerbsRejectedWhenDisabled) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);

        auto expect_disabled = [](const auto &r) {
            EXPECT_FALSE(r.ok());
            if (!r.ok()) {
                EXPECT_NE(std::string(r.error()).find(kClusterDisabled), std::string::npos) << r.error();
            }
        };

        expect_disabled(co_await redis.asking());
        expect_disabled(co_await redis.readonly());
        expect_disabled(co_await redis.readwrite());

        completed = true;
    });
    run_coro_test_until(completed);
}

// ============================================================================
// Real-cluster positive flows. DISABLED_ + REQUIRES_CLUSTER: enable when pointed at a node of a
// running Redis Cluster (cluster-enabled yes). These are the assertions the standalone server can
// never satisfy.
// ============================================================================

class ClusterRealTest : public ProtocolModesTestBase {};

INSTANTIATE_PROTOCOL_MODES(ClusterRealTest);

TEST_P(ClusterRealTest, DISABLED_InfoNodesSlotsMyidKeyslot) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);

        auto info = co_await redis.cluster_info();
        EXPECT_TRUE(info.ok()) << info.error();
        EXPECT_TRUE(info.result().is_string());
        EXPECT_NE(info.result().get<std::string>().find("cluster_state:"), std::string::npos);

        auto myid = co_await redis.cluster_myid();
        EXPECT_TRUE(myid.ok()) << myid.error();
        EXPECT_EQ(myid.result().length(), 40u); // 40-hex node id

        auto slot = co_await redis.cluster_keyslot(protocol_key("ks"));
        EXPECT_TRUE(slot.ok()) << slot.error();
        EXPECT_GE(slot.result(), 0);
        EXPECT_LE(slot.result(), 16383);

        auto slots = co_await redis.cluster_slots();
        EXPECT_TRUE(slots.ok()) << slots.error();
        EXPECT_TRUE(slots.result().is_array());

        auto nodes = co_await redis.cluster_nodes();
        EXPECT_TRUE(nodes.ok()) << nodes.error();
        EXPECT_TRUE(nodes.result().is_string());
        EXPECT_FALSE(nodes.result().get<std::string>().empty());

        auto count = co_await redis.cluster_countkeysinslot(0);
        EXPECT_TRUE(count.ok()) << count.error();
        EXPECT_GE(count.result(), 0);

        completed = true;
    });
    run_coro_test_until(completed);
}

} // namespace
