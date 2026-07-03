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
// Pure-logic unit tests for the UNIFORM empty-argument guard across the command
// mixins. A command that requires a non-empty variadic pack / key-or-member
// collection (DEL, SADD, LPUSH, PFCOUNT, GEOADD, SCRIPT EXISTS, SDIFF, ZMPOP, …)
// would otherwise put a malformed frame on the wire when handed nothing. The
// guard now rejects it client-side via `fail_client<Ret>` — it INVOKES the
// callback synchronously with a failed Reply<Ret> instead of a bare
// `return derived();` (which never fired the callback → a silent no-op on the
// callback API and a FOREVER-PARKED awaiter on the coroutine API).
//
// This is exactly what the coroutine `co_await redis.del()` form forwards to on
// the empty path, so the behaviour under test is identical — and because the
// guard returns before any command<>() dispatch, these tests need NO live Redis,
// NO event loop, NO fixture (daemon-free, parallel-safe, no RESOURCE_LOCK).
//
// The load-bearing assertion is `fired == true`: pre-fix (silent return) the
// callback was NEVER invoked, so this test would fail (and the coroutine form
// would hang). Post-fix it fires with ok()==false and a non-empty error().
//

#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <utility>
#include <vector>
// Resolves to qbm/redis/redis.h via the tests/ include dir (INCLUDES=tests).
#include "../redis.h"

namespace {

// Owner of the guard methods only; connect() is never called — the empty-argument
// branch returns before any command<>() dispatch, so no socket / loop is touched.
qb::redis::tcp::client &
unconnected_client() {
    static qb::redis::tcp::client client{qb::io::uri{"tcp://localhost:6379"}};
    return client;
}

// Drive a command's callback overload with the empty-argument case and assert the
// guard fired the callback synchronously with a failed reply. `Ret` is the
// command's decoded reply type; `invoke(cb)` calls the command with `cb` + the
// (empty) arguments.
template <typename Ret, typename Invoke>
void
expect_sync_fail(const char *name, Invoke &&invoke) {
    bool                  fired = false;
    qb::redis::Reply<Ret> captured;
    invoke([&](qb::redis::Reply<Ret> &&reply) {
        fired    = true;
        captured = std::move(reply);
    });
    // Fired inline, no loop pumped: fail_client resolved it — NOT a silent no-op / awaiter hang.
    EXPECT_TRUE(fired) << name << ": empty-argument guard must invoke the callback synchronously (fail_client)";
    if (fired) {
        EXPECT_FALSE(captured.ok()) << name << ": empty-argument call must be a failed reply";
        EXPECT_FALSE(captured.error().empty()) << name << ": failed reply must carry a reason";
    }
}

} // namespace

// Variadic-key commands (added guards).
TEST(EmptyArgGuards, DelExistsTouchUnlinkNoKeys) {
    expect_sync_fail<long long>("DEL", [](auto &&cb) { unconnected_client().del(std::forward<decltype(cb)>(cb)); });
    expect_sync_fail<long long>("EXISTS", [](auto &&cb) { unconnected_client().exists(std::forward<decltype(cb)>(cb)); });
    expect_sync_fail<long long>("TOUCH", [](auto &&cb) { unconnected_client().touch(std::forward<decltype(cb)>(cb)); });
    expect_sync_fail<long long>("UNLINK", [](auto &&cb) { unconnected_client().unlink(std::forward<decltype(cb)>(cb)); });
}

// Variadic-value list pushes (added guards): non-empty key, empty value pack.
TEST(EmptyArgGuards, ListPushesNoValues) {
    expect_sync_fail<long long>("LPUSH", [](auto &&cb) { unconnected_client().lpush(std::forward<decltype(cb)>(cb), "k"); });
    expect_sync_fail<long long>("RPUSH", [](auto &&cb) { unconnected_client().rpush(std::forward<decltype(cb)>(cb), "k"); });
}

// Variadic-member commands (added + converted guards).
TEST(EmptyArgGuards, SetAndGeoMembers) {
    expect_sync_fail<long long>("SADD", [](auto &&cb) { unconnected_client().sadd(std::forward<decltype(cb)>(cb), "k"); });
    expect_sync_fail<long long>("SREM", [](auto &&cb) { unconnected_client().srem(std::forward<decltype(cb)>(cb), "k"); });
    expect_sync_fail<long long>("GEOADD", [](auto &&cb) { unconnected_client().geoadd(std::forward<decltype(cb)>(cb), "k"); });
}

// HyperLogLog / scripting variadic (added guards).
TEST(EmptyArgGuards, HyperLogLogAndScripting) {
    expect_sync_fail<long long>("PFCOUNT", [](auto &&cb) { unconnected_client().pfcount(std::forward<decltype(cb)>(cb)); });
    expect_sync_fail<std::vector<bool>>("SCRIPT_EXISTS", [](auto &&cb) { unconnected_client().script_exists(std::forward<decltype(cb)>(cb)); });
}

// Required key-VECTOR commands (converted guards): the vector-argument form.
TEST(EmptyArgGuards, KeyVectorCommands) {
    expect_sync_fail<std::vector<std::string>>("SDIFF",
                                               [](auto &&cb) { unconnected_client().sdiff(std::forward<decltype(cb)>(cb), std::vector<std::string>{}); });
    expect_sync_fail<std::optional<std::pair<std::string, std::vector<qb::redis::score_member>>>>(
        "ZMPOP", [](auto &&cb) { unconnected_client().zmpop(std::forward<decltype(cb)>(cb), std::vector<std::string>{}, "MIN"); });
}
