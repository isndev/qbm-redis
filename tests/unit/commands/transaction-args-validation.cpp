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

//
// Pure-logic unit tests for client-side argument validation in the transaction
// command mixin (WATCH). These guard checks run *before* any wire I/O: the
// callback overload of `watch(...)` short-circuits on an empty key / empty key
// list and invokes the supplied callback synchronously with a failed Reply<status>.
//
// Because the validation never reaches the socket, these tests need NO live
// Redis, NO event loop, and NO ProtocolModesTestBase. They were trapped in the
// integration lane only because the legacy file derived from the protocol
// fixture; here they run daemon-free and parallel-safe (NO RESOURCE_LOCK).
//
// We drive the synchronous callback overload directly — `watch(callback, key)` —
// which is exactly what the coroutine `co_await redis.watch(...)` form forwards
// to on the empty-argument path, so the behaviour under test is identical.
//
// Ground truth (commands/transaction_commands.h):
//   watch(func, "")                       -> reply.ok()=false, error()="Key cannot be empty"
//   watch(func, std::vector<std::string>{}) -> reply.ok()=false, error()="Key list cannot be empty"
// Both error strings contain the substring "empty".
//

#include <gtest/gtest.h>
#include <string>
#include <vector>
// Resolves to qbm/redis/redis.h via the tests/ include dir (INCLUDES=tests).
// Pulls in the tcp::client type whose transaction_commands<> base owns watch().
#include "../redis.h"

namespace {

// A client instance is needed only as the owner of the validation method; no
// connect() is ever called, so no socket / loop is touched. The empty-argument
// branch returns before any command<>() dispatch.
qb::redis::tcp::client &
unconnected_client() {
    static qb::redis::tcp::client client{qb::io::uri{"tcp://localhost:6379"}};
    return client;
}

} // namespace

// ── Single-key WATCH with an empty key ──────────────────────────────────────
TEST(TransactionWatchValidation, EmptyKeyRejectedSynchronously) {
    bool                   fired = false;
    qb::redis::Reply<qb::redis::status> captured;

    unconnected_client().watch(
        [&](qb::redis::Reply<qb::redis::status> &&reply) {
            fired    = true;
            captured = std::move(reply);
        },
        std::string{""});

    // Callback ran inline (no loop pumped) — this is client-side validation.
    ASSERT_TRUE(fired) << "empty-key validation must invoke the callback synchronously";
    EXPECT_FALSE(captured.ok());
    ASSERT_FALSE(captured.error().empty());
    EXPECT_EQ(captured.error(), "Key cannot be empty");
    EXPECT_NE(captured.error().find("empty"), std::string::npos);
}

// ── Multi-key WATCH with an empty key list ──────────────────────────────────
TEST(TransactionWatchValidation, EmptyKeyListRejectedSynchronously) {
    bool                   fired = false;
    qb::redis::Reply<qb::redis::status> captured;

    unconnected_client().watch(
        [&](qb::redis::Reply<qb::redis::status> &&reply) {
            fired    = true;
            captured = std::move(reply);
        },
        std::vector<std::string>{});

    ASSERT_TRUE(fired) << "empty-key-list validation must invoke the callback synchronously";
    EXPECT_FALSE(captured.ok());
    ASSERT_FALSE(captured.error().empty());
    EXPECT_EQ(captured.error(), "Key list cannot be empty");
    EXPECT_NE(captured.error().find("empty"), std::string::npos);
}

// Note: the complementary "non-empty list bypasses the guard" assertion is
// intentionally NOT tested here. The bypass path calls command<>() →
// _command() → ready_to_write()/out(), which touches the transport; on an
// unconnected client that is not a daemon-free, deterministic operation, so it
// belongs to the integration tier (transaction-multi-exec.cpp), not this unit.
