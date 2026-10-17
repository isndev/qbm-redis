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
 * @file integration/connection/connection-commands.cpp
 * @brief Connection-level verbs (AUTH/ECHO/HELLO/PING/RESET/SELECT/SWAPDB) + the
 *        connect_with_retry / RetryPolicy ladder, RESP2 and RESP3.
 *
 * Integration tier — needs a live redis (env `REDIS_URI`, default tcp://localhost:6379).
 * Migrated from test-connection-commands.cpp: the 6 short-form verb dups (PING /
 * PING_WITH_MESSAGE / ECHO / SELECT / SWAPDB / HELLO_RESP3_MAP) were deleted (strictly
 * subsumed by the CORO_* cases); the `run_for(5s)` magic budgets on the retry-exhausted /
 * observer tests were replaced with a watchdog-bounded `run_coro_test_until`; a real
 * auth-failure case (WRONGPASS / NOAUTH) was added.
 */

#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <vector>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include "../../shared/redis_integration_fixture.h"
#include <qbm/redis/redis.h>

using namespace qb::io;
using namespace std::chrono;
using namespace std::chrono_literals;
using namespace qb::redis::test;

class ConnectionProtocolModesTest : public ProtocolModesTestBase {};

INSTANTIATE_PROTOCOL_MODES(ConnectionProtocolModesTest);

// ============================================================================
// Verbs
// ============================================================================

// AUTH happy path: against an unauthenticated server, AUTH default/"" succeeds.
TEST_P(ConnectionProtocolModesTest, AUTH_EMPTY_PASSWORD_OK) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto reply = co_await redis.auth("default", "");
        EXPECT_TRUE(reply.ok()) << reply.error();
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// AUTH failure: invalid credentials must surface a Redis error, NOT a green pass.
//
// Ground-truth (Redis 8.x, confirmed via redis-cli):
//   - The built-in `default` user is created with the `nopass` flag, which means it
//     accepts ANY password. So two-arg `AUTH default <anything>` returns +OK — that is
//     real, correct Redis behaviour, NOT a bug. Asserting it fails would contradict the
//     server.
//   - The unambiguous "bad credentials" path that fails on every standalone server,
//     regardless of requirepass/ACL config, is AUTH for a username that does not exist:
//     `AUTH <nonexistent-user> <pw>` → "WRONGPASS invalid username-password pair or user
//     is disabled.". That is what we assert here.
TEST_P(ConnectionProtocolModesTest, AUTH_WRONG_PASSWORD_FAILS) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto reply = co_await redis.auth("definitely-not-a-real-user", "definitely-not-the-password");
        EXPECT_FALSE(reply.ok()) << "AUTH for a nonexistent user must fail, got OK";
        const std::string err(reply.error());
        EXPECT_FALSE(err.empty());
        EXPECT_TRUE(err.find("WRONGPASS") != std::string::npos || err.find("NOAUTH") != std::string::npos
                    || err.find("ERR") != std::string::npos)
            << "unexpected AUTH error text: " << err;
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

TEST_P(ConnectionProtocolModesTest, ECHO) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string message = "Hello Redis!";
        auto              reply   = co_await redis.echo(message);
        EXPECT_TRUE(reply.ok()) << reply.error();
        EXPECT_EQ(reply.result(), message);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

TEST_P(ConnectionProtocolModesTest, PING) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto reply = co_await redis.ping();
        EXPECT_TRUE(reply.ok()) << reply.error();
        EXPECT_EQ(reply.result(), "PONG");
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

TEST_P(ConnectionProtocolModesTest, PING_WITH_MESSAGE) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string message = "Hello";
        auto              reply   = co_await redis.ping(message);
        EXPECT_TRUE(reply.ok()) << reply.error();
        EXPECT_EQ(reply.result(), message);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

TEST_P(ConnectionProtocolModesTest, ERROR_REPLY) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto reply = co_await redis.command<std::string>("INVALIDCMD");
        EXPECT_FALSE(reply.ok()) << "expected an error for an invalid command";
        EXPECT_FALSE(std::string(reply.error()).empty());
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// HELLO 3 (RESP3 handshake) returns a map with server info.
TEST_P(ConnectionProtocolModesTest, HELLO_RESP3_MAP) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto reply = co_await redis.hello(3);
        EXPECT_TRUE(reply.ok()) << "HELLO 3 failed: " << reply.error();
        if (reply.ok()) {
            const auto &info = reply.result();
            EXPECT_TRUE(info.is_object()) << "HELLO 3 reply must be a RESP3 map";
            EXPECT_TRUE(info.contains("server")) << "HELLO map must contain a server field";
            EXPECT_TRUE(info.contains("proto")) << "HELLO map must report the negotiated proto";
        }
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// PING continues to work after the protocol switch.
TEST_P(ConnectionProtocolModesTest, HELLO_THEN_PING) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto hello = co_await redis.hello(3);
        EXPECT_TRUE(hello.ok()) << "HELLO 3 failed: " << hello.error();
        if (!hello.ok()) {
            completed = true;
            co_return;
        }
        auto ping = co_await redis.ping();
        EXPECT_TRUE(ping.ok()) << "PING after HELLO 3 failed: " << ping.error();
        EXPECT_EQ(ping.result(), "PONG");
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// RESET resets server-side connection state but does NOT close the connection;
// verify PING still works afterwards.
TEST_P(ConnectionProtocolModesTest, RESET) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto reply = co_await redis.reset();
        EXPECT_TRUE(reply.ok()) << "RESET failed: " << reply.error();
        if (!reply.ok()) {
            completed = true;
            co_return;
        }
        auto ping = co_await redis.ping();
        EXPECT_TRUE(ping.ok()) << "PING after RESET failed: " << ping.error();
        EXPECT_EQ(ping.result(), "PONG");
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

TEST_P(ConnectionProtocolModesTest, SELECT) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto r0 = co_await redis.select(0);
        EXPECT_TRUE(r0.ok()) << r0.error();
        EXPECT_TRUE(r0.result().ok());
        const std::string key = protocol_key("select-db-witness");
        EXPECT_TRUE((co_await redis.set(key, "db-zero")).ok());
        auto r1 = co_await redis.select(1);
        EXPECT_TRUE(r1.ok()) << r1.error();
        EXPECT_TRUE(r1.result().ok());
        EXPECT_TRUE((co_await redis.set(key, "db-one")).ok());
        auto db1 = co_await redis.get(key);
        EXPECT_TRUE(db1.ok()) << db1.error();
        EXPECT_EQ(db1.value_or("missing"), "db-one");
        auto r2 = co_await redis.select(0); // restore db 0 for the next test
        EXPECT_TRUE(r2.ok()) << r2.error();
        EXPECT_TRUE(r2.result().ok());
        auto db0 = co_await redis.get(key);
        EXPECT_TRUE(db0.ok()) << db0.error();
        EXPECT_EQ(db0.value_or("missing"), "db-zero");
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

TEST_P(ConnectionProtocolModesTest, SWAPDB) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("swapdb-witness");
        EXPECT_TRUE((co_await redis.select(0)).ok());
        EXPECT_TRUE((co_await redis.set(key, "original-zero")).ok());
        EXPECT_TRUE((co_await redis.select(1)).ok());
        EXPECT_TRUE((co_await redis.set(key, "original-one")).ok());
        EXPECT_TRUE((co_await redis.select(0)).ok());

        auto r1 = co_await redis.swapdb(0, 1);
        EXPECT_TRUE(r1.ok()) << r1.error();
        EXPECT_TRUE(r1.result().ok());
        auto swapped0 = co_await redis.get(key);
        EXPECT_TRUE(swapped0.ok()) << swapped0.error();
        EXPECT_EQ(swapped0.value_or("missing"), "original-one");
        EXPECT_TRUE((co_await redis.select(1)).ok());
        auto swapped1 = co_await redis.get(key);
        EXPECT_TRUE(swapped1.ok()) << swapped1.error();
        EXPECT_EQ(swapped1.value_or("missing"), "original-zero");
        EXPECT_TRUE((co_await redis.select(0)).ok());

        auto r2 = co_await redis.swapdb(0, 1); // swap back
        EXPECT_TRUE(r2.ok()) << r2.error();
        EXPECT_TRUE(r2.result().ok());
        auto restored = co_await redis.get(key);
        EXPECT_TRUE(restored.ok()) << restored.error();
        EXPECT_EQ(restored.value_or("missing"), "original-zero");
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// ============================================================================
// connect_with_retry / RetryPolicy ladder
// ============================================================================

// Immediate success when the server is up.
TEST_P(ConnectionProtocolModesTest, RETRY_IMMEDIATE_SUCCESS) {
    qb::redis::tcp::client client{qb::io::uri{redis_test_uri()}};
    bool                   completed = false;
    bool                   connected = false;
    auto                   test_task = [&]() -> qb::io::async::task<void> {
        connected =
            co_await client.connect_with_retry(qb::redis::RetryPolicy{}.with_max_attempts(5).with_initial_delay(10ms).with_connect_timeout(2s));
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
    EXPECT_TRUE(connected);
}

// Exhausts all attempts when the host is unreachable. ECONNREFUSED on a dead port returns
// almost instantly, so the whole ladder (3 attempts × ≤500 ms + 2 backoffs) finishes well
// inside the watchdog. No fixed run_for budget.
TEST_P(ConnectionProtocolModesTest, RETRY_EXHAUSTED) {
    qb::redis::tcp::client client{qb::io::uri{"tcp://127.0.0.1:19379"}};
    bool                   completed = false;
    bool                   connected = true; // intentionally wrong; expect false
    auto                   test_task = [&]() -> qb::io::async::task<void> {
        connected = co_await client.connect_with_retry(
            qb::redis::RetryPolicy{}.with_max_attempts(3).with_initial_delay(20ms).with_max_delay(50ms).with_jitter(false).with_connect_timeout(
                500ms));
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed, 10s);
    EXPECT_TRUE(completed);
    EXPECT_FALSE(connected);
}

// on_retry is invoked once after each failed attempt except the last:
// 3 attempts → exactly 2 on_retry calls (deterministic, jitter off).
TEST_P(ConnectionProtocolModesTest, RETRY_OBSERVER) {
    qb::redis::tcp::client client{qb::io::uri{"tcp://127.0.0.1:19380"}};
    bool                   completed   = false;
    int                    retry_count = 0;
    auto                   test_task   = [&]() -> qb::io::async::task<void> {
        (void) co_await client.connect_with_retry(
            qb::redis::RetryPolicy{}.with_max_attempts(3).with_initial_delay(10ms).with_jitter(false).with_connect_timeout(200ms).with_on_retry(
                [&](int /*attempt*/, qb::duration /*wait*/) { ++retry_count; }));
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed, 10s);
    EXPECT_TRUE(completed);
    EXPECT_EQ(retry_count, 2);
}

// connect_with_retry with the URI overload.
TEST_P(ConnectionProtocolModesTest, RETRY_WITH_URI) {
    qb::redis::tcp::client client;
    bool                   completed = false;
    bool                   connected = false;
    auto                   test_task = [&]() -> qb::io::async::task<void> {
        connected = co_await client.connect_with_retry(qb::io::uri{redis_test_uri()},
                                                       qb::redis::RetryPolicy{}.with_max_attempts(3).with_initial_delay(10ms));
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
    EXPECT_TRUE(connected);
}

// connect() with a configurable timeout.
TEST_P(ConnectionProtocolModesTest, CUSTOM_TIMEOUT) {
    qb::redis::tcp::client client{qb::io::uri{redis_test_uri()}};
    bool                   completed = false;
    bool                   connected = false;
    auto                   test_task = [&]() -> qb::io::async::task<void> {
        connected = co_await client.connect(1s);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
    EXPECT_TRUE(connected);
}

// What a coroutine waits on, in its scheduler's dump (Huly QB-71): the module's awaiters label themselves with
// qb::io::async::track_suspension, so a coroutine parked on a connect, then on a command, says so -- under the
// module's names, not the record of an earlier wait. The observer runs after the client in the same ready-queue
// drain, so it sees the connect parked; a BLPOP on an empty list holds the command for a second.
TEST_P(ConnectionProtocolModesTest, THE_DUMP_SAYS_A_COROUTINE_WAITS_ON_A_CONNECT_THEN_A_COMMAND) {
    auto &sched = qb::io::async::coro_scheduler();
    ASSERT_TRUE(sched.set_suspension_tracking(true));
    qb::redis::tcp::client client{qb::io::uri{redis_test_uri()}};
    const std::string      key      = protocol_key("dump:blpop");
    bool                   done     = false;
    bool                   observed = false;
    std::string            on_connect;
    std::string            on_command;
    // what the coroutine spawned as "redis-client" waits on, at the end of its chain
    auto client_waits_on = [&sched]() -> std::string {
        const auto                             dump = sched.dump();
        qb::io::async::parked_coroutine const *at   = nullptr;
        for (auto const &p : dump)
            if (p.name == "redis-client")
                at = &p;
        for (std::size_t depth = 0; at && at->waits_on && depth < dump.size(); ++depth) {
            qb::io::async::parked_coroutine const *next = nullptr;
            for (auto const &p : dump)
                if (p.frame == at->waits_on)
                    next = &p;
            if (!next)
                break;
            at = next;
        }
        return at && at->kind ? at->kind : "";
    };
    auto client_task = [&]() -> qb::io::async::task<void> {
        if (co_await client.connect())
            (void) co_await client.blpop({key}, 1);
        done = true;
    };
    auto observer = [&]() -> qb::io::async::task<void> {
        on_connect = client_waits_on();
        for (int i = 0; i < 400 && !done && on_command != "redis"; ++i) {
            co_await qb::io::async::sleep(5ms);
            on_command = client_waits_on();
        }
        for (int i = 0; i < 400 && !done; ++i) // the BLPOP times out after its second
            co_await qb::io::async::sleep(5ms);
        observed = true;
    };
    sched.spawn("redis-client", client_task());
    sched.spawn(observer());
    run_coro_test_until(observed, 10s);
    sched.set_suspension_tracking(false);

    EXPECT_TRUE(done) << "the client never finished";
    EXPECT_EQ(on_connect, "redis connect");
    EXPECT_EQ(on_command, "redis");
}

// ============================================================================
// disconnect() ends the connection for the server too (Huly QB-1060)
// ============================================================================

// qb-io's standalone dispose() used to leave the socket untouched: after disconnect() the server
// kept listing the client until the object was destroyed. Since QB-1060 a disconnect the client
// asked for shuts the sending side down -- the descriptor stays until destruction or the next
// connect -- so the server reads EOF and drops the client. `victim` lives in the coroutine frame
// across the whole check: destroying it would close the socket on any qb. The bound is the
// server's reaction to an EOF on an idle connection, milliseconds, with a loaded host's margin.
TEST_P(ConnectionProtocolModesTest, DISCONNECT_ENDS_THE_CONNECTION_FOR_THE_SERVER) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        qb::redis::tcp::client victim{REDIS_URI_PROTOCOL};
        if (!co_await victim.connect()) {
            ADD_FAILURE() << "the second connection did not connect";
            completed = true;
            co_return;
        }
        PROTOCOL_ENSURE_RESP3_CLIENT(victim, completed);
        auto id = co_await victim.client_id();
        if (!id.ok()) {
            ADD_FAILURE() << id.error();
            completed = true;
            co_return;
        }
        const std::string needle = "id=" + std::to_string(id.result()) + " ";
        // Whether CLIENT LIST names the victim once `until` holds or 10 s have passed.
        auto listed = [this, needle](bool until) -> qb::io::async::task<bool> {
            const auto deadline = steady_clock::now() + 10s;
            for (;;) {
                auto       list = co_await redis.client_list(); // the raw CLIENT LIST text, boxed in qb::json
                const bool named =
                    !list.ok() || !list.result().is_string() || list.result().get<std::string>().find(needle) != std::string::npos;
                if (named == until || steady_clock::now() >= deadline)
                    co_return named;
                co_await qb::io::async::sleep(20ms);
            }
        };
        EXPECT_TRUE(co_await listed(true)) << "the connected client must be listed";

        victim.disconnect();
        EXPECT_FALSE(co_await listed(false)) << "the server still lists client " << id.result() << " 10 s after disconnect()";
        EXPECT_FALSE(victim.is_connected());
        completed = true;
    });
    run_coro_test_until(completed, 30s);
}

// ============================================================================
// The handshake against a live server (Huly QB-126a): ACL credentials, a database and RESP3 replayed on
// every connection; a refusal is terminal and visible. The ACL user is created on the server under test
// and deleted after, so the cases need nothing but a Redis 6+ the suite already uses.
// ============================================================================

namespace {

/// One ACL user for the handshake cases, created on the server under test and deleted with the object.
struct AclUser {
    std::string name     = "qbm-redis-hs-" + std::to_string(qb::redis::test::detail::process_id_for_keys());
    std::string password = "hs-s3cret";
    bool        ok       = false;

    explicit AclUser(std::vector<std::string> const &rules) {
        qb::redis::tcp::client admin{qb::io::uri{redis_test_uri()}};
        if (!qb::io::async::run_sync(admin.connect()))
            return;
        (void) qb::io::async::run_sync(admin.command<long long>("ACL", "DELUSER", name));
        auto r =
            qb::io::async::run_sync(admin.command<qb::redis::status>("ACL", "SETUSER", name, "reset", "on", ">" + password, "~*", "&*", rules));
        ok = r.ok();
    }
    ~AclUser() {
        qb::redis::tcp::client admin{qb::io::uri{redis_test_uri()}};
        if (qb::io::async::run_sync(admin.connect()))
            (void) qb::io::async::run_sync(admin.command<long long>("ACL", "DELUSER", name));
    }
};

} // namespace

TEST_P(ConnectionProtocolModesTest, HANDSHAKE_REPLAYS_ACL_USER_DATABASE_AND_PROTOCOL_ON_EVERY_CONNECTION) {
    AclUser user{{"+@all"}};
    ASSERT_TRUE(user.ok) << "ACL SETUSER failed on the server under test";
    const bool             resp3 = GetParam() == ProtocolMode::RESP3;
    qb::redis::tcp::client client{REDIS_URI_PROTOCOL};
    client.with_resp3(resp3).with_auth(user.name, user.password).with_select(3);
    int connected = 0;
    client.on_connected([&connected] { ++connected; });
    ASSERT_TRUE(qb::io::async::run_sync(client.connect()));

    auto check = [&](char const *when) {
        auto who = qb::io::async::run_sync(client.command<std::string>("ACL", "WHOAMI"));
        EXPECT_TRUE(who.ok()) << when << ": " << who.error();
        EXPECT_EQ(who.result(), user.name) << when;
        auto info = qb::io::async::run_sync(client.command<std::string>("CLIENT", "INFO"));
        EXPECT_TRUE(info.ok()) << when << ": " << info.error();
        EXPECT_NE(info.result().find(" db=3 "), std::string::npos) << when << ": " << info.result();
        EXPECT_NE(info.result().find(resp3 ? " resp=3" : " resp=2"), std::string::npos) << when << ": " << info.result();
    };
    check("first connection");

    client.enable_auto_reconnect(qb::redis::RetryPolicy{}.with_initial_delay(20ms).with_jitter(false));
    const auto id = qb::io::async::run_sync(client.client_id());
    ASSERT_TRUE(id.ok()) << id.error();
    {
        qb::redis::tcp::client admin{REDIS_URI_PROTOCOL};
        ASSERT_TRUE(qb::io::async::run_sync(admin.connect()));
        ASSERT_EQ(qb::io::async::run_sync(admin.client_kill("", id.result())).result(), 1);
    }
    const auto deadline = steady_clock::now() + 5s; // a watchdog, never the expected path
    while (connected < 2 && steady_clock::now() < deadline)
        qb::io::async::run(EVRUN_NOWAIT);
    ASSERT_EQ(connected, 2) << "the armed run did not bring the connection back";
    check("after a reconnect"); // nothing redone by hand: the handshake replayed all three
    client.disconnect();
}

TEST_P(ConnectionProtocolModesTest, HANDSHAKE_WRONGPASS_IS_TERMINAL) {
    AclUser user{{"+@all"}};
    ASSERT_TRUE(user.ok);
    qb::redis::tcp::client client{REDIS_URI_PROTOCOL};
    client.with_resp3(GetParam() == ProtocolMode::RESP3).with_auth(user.name, "not-the-password");
    client.enable_auto_reconnect(qb::redis::RetryPolicy{}.with_initial_delay(1ms).with_jitter(false));
    std::optional<qb::io::down_reason> down;
    client.on_down([&down](qb::io::down_reason r) { down = r; });

    EXPECT_FALSE(qb::io::async::run_sync(client.connect()));
    ASSERT_TRUE(down.has_value());
    EXPECT_EQ(*down, qb::io::down_reason::handshake_refused);
    EXPECT_EQ(client.state(), qb::io::connection_state::down);
    auto after = qb::io::async::run_sync(client.ping());
    EXPECT_FALSE(after.ok());
    EXPECT_EQ(after.error().rfind("handshake refused: WRONGPASS", 0), 0u) << after.error();
}

TEST_P(ConnectionProtocolModesTest, HANDSHAKE_NOPERM_IS_TERMINAL) {
    AclUser user{{"+@all", "-select"}};
    ASSERT_TRUE(user.ok);
    qb::redis::tcp::client client{REDIS_URI_PROTOCOL};
    client.with_resp3(GetParam() == ProtocolMode::RESP3).with_auth(user.name, user.password).with_select(1);

    EXPECT_FALSE(qb::io::async::run_sync(client.connect()));
    EXPECT_EQ(client.state(), qb::io::connection_state::down);
    EXPECT_EQ(client.down_reason(), qb::io::down_reason::handshake_refused);
    auto after = qb::io::async::run_sync(client.ping());
    EXPECT_EQ(after.error().rfind("handshake refused: NOPERM", 0), 0u) << after.error();
}
