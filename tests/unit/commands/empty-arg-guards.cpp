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

// Drive a command whose single empty/invalid key-or-member argument trips a
// *silent* guard — a bare `return derived();` with NO `fail_client<>()` — and
// assert the callback did NOT fire. This is the DOCUMENTED current behaviour of
// these one-argument guards (SCARD/SISMEMBER/SMEMBERS/SMOVE/SPOP/SRANDMEMBER/
// {S,H,Z}SCAN/LPOS): unlike the variadic/vector guards above they do not resolve
// the callback, so the callback API sees a silent no-op (and the coroutine form
// would park forever). Pinning it here makes any future conversion to
// `fail_client` a deliberate, visible change rather than an accident. `Ret` is
// the command's decoded reply type; `invoke(cb)` calls the command with `cb`
// plus the empty/invalid argument(s).
template <typename Ret, typename Invoke>
void
expect_silent_guard(const char *name, Invoke &&invoke) {
    bool fired = false;
    invoke([&](qb::redis::Reply<Ret> &&) { fired = true; });
    EXPECT_FALSE(fired) << name << ": single-argument empty/invalid guard must return silently (callback must NOT fire)";
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

// ── ADDED: further fail_client guards ───────────────────────────────────────

// Set intersection / union / diff-store variants: empty key vector (and, for the
// *store forms, a present destination) still trip fail_client.
TEST(EmptyArgGuards, SetVectorFailClient) {
    expect_sync_fail<std::vector<std::string>>(
        "SINTER", [](auto &&cb) { unconnected_client().sinter(std::forward<decltype(cb)>(cb), std::vector<std::string>{}); });
    expect_sync_fail<long long>(
        "SINTERCARD", [](auto &&cb) { unconnected_client().sintercard(std::forward<decltype(cb)>(cb), std::vector<std::string>{}); });
    expect_sync_fail<long long>(
        "SINTERSTORE", [](auto &&cb) { unconnected_client().sinterstore(std::forward<decltype(cb)>(cb), "d", std::vector<std::string>{}); });
    expect_sync_fail<long long>(
        "SDIFFSTORE", [](auto &&cb) { unconnected_client().sdiffstore(std::forward<decltype(cb)>(cb), "d", std::vector<std::string>{}); });
    expect_sync_fail<std::vector<std::string>>(
        "SUNION", [](auto &&cb) { unconnected_client().sunion(std::forward<decltype(cb)>(cb), std::vector<std::string>{}); });
    expect_sync_fail<long long>(
        "SUNIONSTORE", [](auto &&cb) { unconnected_client().sunionstore(std::forward<decltype(cb)>(cb), "d", std::vector<std::string>{}); });
}

// SMISMEMBER: present key, empty member pack.
TEST(EmptyArgGuards, SetMembershipNoMembers) {
    expect_sync_fail<std::vector<bool>>("SMISMEMBER",
                                        [](auto &&cb) { unconnected_client().smismember(std::forward<decltype(cb)>(cb), "k"); });
}

// LPUSHX / RPUSHX: present key, empty value pack.
TEST(EmptyArgGuards, ListPushXNoValues) {
    expect_sync_fail<long long>("LPUSHX", [](auto &&cb) { unconnected_client().lpushx(std::forward<decltype(cb)>(cb), "k"); });
    expect_sync_fail<long long>("RPUSHX", [](auto &&cb) { unconnected_client().rpushx(std::forward<decltype(cb)>(cb), "k"); });
}

// LMPOP / BLMPOP: empty key vector (ListPosition + count/timeout still supplied).
TEST(EmptyArgGuards, ListMultiPopNoKeys) {
    using LmpopRet = std::optional<std::pair<std::string, std::vector<std::string>>>;
    expect_sync_fail<LmpopRet>("LMPOP", [](auto &&cb) {
        unconnected_client().lmpop(std::forward<decltype(cb)>(cb), std::vector<std::string>{}, qb::redis::ListPosition::LEFT, 1);
    });
    expect_sync_fail<LmpopRet>("BLMPOP", [](auto &&cb) {
        unconnected_client().blmpop(std::forward<decltype(cb)>(cb), std::vector<std::string>{}, qb::redis::ListPosition::LEFT, 0, 1);
    });
}

// Hash field packs: present key, empty field / field-value pack.
TEST(EmptyArgGuards, HashFieldPacks) {
    expect_sync_fail<std::vector<std::optional<std::string>>>(
        "HMGET", [](auto &&cb) { unconnected_client().hmget(std::forward<decltype(cb)>(cb), "k"); });
    expect_sync_fail<long long>("HDEL", [](auto &&cb) { unconnected_client().hdel(std::forward<decltype(cb)>(cb), "k"); });
    expect_sync_fail<qb::redis::status>("HMSET", [](auto &&cb) { unconnected_client().hmset(std::forward<decltype(cb)>(cb), "k"); });
}

// Geo member packs: present key, empty member pack.
TEST(EmptyArgGuards, GeoMemberPacks) {
    expect_sync_fail<std::vector<std::optional<std::string>>>(
        "GEOHASH", [](auto &&cb) { unconnected_client().geohash(std::forward<decltype(cb)>(cb), "k"); });
    expect_sync_fail<std::vector<std::optional<qb::redis::geo_pos>>>(
        "GEOPOS", [](auto &&cb) { unconnected_client().geopos(std::forward<decltype(cb)>(cb), "k"); });
}

// BZMPOP: empty key vector.
TEST(EmptyArgGuards, SortedSetBlockingMultiPopNoKeys) {
    expect_sync_fail<std::optional<std::pair<std::string, std::vector<qb::redis::score_member>>>>(
        "BZMPOP", [](auto &&cb) { unconnected_client().bzmpop(std::forward<decltype(cb)>(cb), std::vector<std::string>{}, 0, "MIN", 1); });
}

// Cluster slot management (empty variadic slot pack) + HyperLogLog merge (present
// destination, empty source-key pack).
TEST(EmptyArgGuards, ClusterSlotsAndPfmerge) {
    expect_sync_fail<qb::redis::status>("CLUSTER_ADDSLOTS",
                                        [](auto &&cb) { unconnected_client().cluster_addslots(std::forward<decltype(cb)>(cb)); });
    expect_sync_fail<qb::redis::status>("CLUSTER_DELSLOTS",
                                        [](auto &&cb) { unconnected_client().cluster_delslots(std::forward<decltype(cb)>(cb)); });
    expect_sync_fail<qb::redis::status>("PFMERGE", [](auto &&cb) { unconnected_client().pfmerge(std::forward<decltype(cb)>(cb), "d"); });
}

// ── ADDED: silent single-argument guards (documented CURRENT behaviour) ──────
// These commands reject a single empty (or otherwise invalid) key/member argument
// with a bare `return derived();` — NO fail_client — so the callback is NEVER
// invoked. This is the CURRENT documented behaviour, pinned here so a future
// conversion to fail_client becomes a deliberate, visible change.
TEST(EmptyArgGuards, SilentSetGuards) {
    expect_silent_guard<long long>("SCARD", [](auto &&cb) { unconnected_client().scard(std::forward<decltype(cb)>(cb), ""); });
    expect_silent_guard<bool>("SISMEMBER", [](auto &&cb) { unconnected_client().sismember(std::forward<decltype(cb)>(cb), "", ""); });
    expect_silent_guard<qb::unordered_set<std::string>>("SMEMBERS",
                                                        [](auto &&cb) { unconnected_client().smembers(std::forward<decltype(cb)>(cb), ""); });
    expect_silent_guard<bool>("SMOVE", [](auto &&cb) { unconnected_client().smove(std::forward<decltype(cb)>(cb), "", "", ""); });
    expect_silent_guard<std::optional<std::string>>("SPOP", [](auto &&cb) { unconnected_client().spop(std::forward<decltype(cb)>(cb), ""); });
    expect_silent_guard<std::vector<std::string>>("SPOP_COUNT",
                                                  [](auto &&cb) { unconnected_client().spop(std::forward<decltype(cb)>(cb), "", 1LL); });
    expect_silent_guard<std::optional<std::string>>(
        "SRANDMEMBER", [](auto &&cb) { unconnected_client().srandmember(std::forward<decltype(cb)>(cb), ""); });
    expect_silent_guard<std::vector<std::string>>(
        "SRANDMEMBER_COUNT", [](auto &&cb) { unconnected_client().srandmember(std::forward<decltype(cb)>(cb), "", 1LL); });
    // cursor is `long long`; 0LL disambiguates from the (key, pattern) SSCAN overload.
    expect_silent_guard<qb::redis::scan<>>("SSCAN", [](auto &&cb) { unconnected_client().sscan(std::forward<decltype(cb)>(cb), "", 0LL); });
}

TEST(EmptyArgGuards, SilentListHashZSetScanGuards) {
    expect_silent_guard<std::vector<long long>>("LPOS", [](auto &&cb) { unconnected_client().lpos(std::forward<decltype(cb)>(cb), "", ""); });
    // HSCAN yields scan<unordered_map<string,string>>; ZSCAN yields scan<unordered_map<string,double>>.
    expect_silent_guard<qb::redis::scan<qb::unordered_map<std::string, std::string>>>(
        "HSCAN", [](auto &&cb) { unconnected_client().hscan(std::forward<decltype(cb)>(cb), "", 0LL); });
    expect_silent_guard<qb::redis::scan<qb::unordered_map<std::string, double>>>(
        "ZSCAN", [](auto &&cb) { unconnected_client().zscan(std::forward<decltype(cb)>(cb), "", 0LL); });
}
