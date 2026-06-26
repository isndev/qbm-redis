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
// System tier: in-process plumbing over a loopback socket. NO external redis
// daemon, NO RESOURCE_LOCK. This is the AddressSanitizer regression for the
// client-lifetime-vs-in-flight-reconnect use-after-free.
//
// What is exercised
// -----------------
// Destroying the client while an auto-reconnect connect is in flight must not
// use-after-free the connector. The reconnect task is spawned (detached), so it
// outlives the client; its connect_awaiter completion lambda touches the client
// (`_client.setup_connection(_client._uri, ...)`) and must check the client's
// liveness (connector_alive()), not just the awaiter's own validity flag (which
// stays true on the still-alive reconnect coroutine frame). See redis.h
// connect_awaiter::await_suspend.
//
// Why a loopback listener (no live redis)
// ---------------------------------------
// The UAF is only reachable when the connect SUCCEEDS (raw_io is open) while the
// client is dead — a failed/timed-out connect never touches _client. A redis
// client `connect()` is purely TCP-level (it calls setup_connection on a
// completed handshake; no server reply is needed for connect() to return true),
// so we point BOTH the initial connect AND the reconnect at a local listening
// socket that is bound + listen()'d but never accept()s. The kernel completes
// the 3-way handshake from its backlog, so each connect succeeds and the
// completion lambda reaches setup_connection — with NO redis daemon involved.
//
// We destroy the client after the reconnect starts but before the connect
// completes, then pump so the lambda fires against the freed client. With the
// fix it detects the dead client and exits cleanly; before the fix
// AddressSanitizer reports heap-use-after-free in setup_connection.
//
// NOTE: on a non-ASan build this test has no value-level assertion to make about
// the UAF itself (the bug is a memory error, not an observable return value), so
// it intentionally ends in SUCCEED(): it documents that the sequence completes
// without crashing. Its teeth are in the ASan CI run.
//

#include <gtest/gtest.h>
#include <string>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
// Cross-platform socket layer: platform socket headers + the socket_type alias,
// inet::invalid_socket and the closesocket() shim. Winsock is initialised by
// qb-io's global ws2_32 guard (linked via the qb-io / qb-redis sockets used here).
#include <qb/io/system/sys__socket.h>
#include "../redis.h"

using namespace qb::io;
using namespace std::chrono_literals;

namespace {

// Pump the loop until `pred()` is true or `timeout` elapses; returns pred's
// final value. Deterministic (no fixed sleep): bounded by a wall-clock deadline
// only as a hang watchdog, not as a timing assumption.
template <typename Pred>
bool
run_until(Pred &&pred, std::chrono::milliseconds timeout = 2000ms) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!pred()) {
        qb::io::async::run(EVRUN_NOWAIT);
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
    }
    return true;
}

// Open a loopback TCP listener on an ephemeral port that we never accept() on.
// Returns the fd (caller closes) and writes the chosen port to `port`.
socket_type
open_loopback_listener(int &port) {
    const socket_type lfd = ::socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_NE(lfd, qb::io::inet::invalid_socket);
    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = 0; // ephemeral
    EXPECT_EQ(::bind(lfd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)), 0);
    EXPECT_EQ(::listen(lfd, 16), 0);
    socklen_t alen = sizeof(addr);
    EXPECT_EQ(::getsockname(lfd, reinterpret_cast<sockaddr *>(&addr), &alen), 0);
    port = ntohs(addr.sin_port);
    return lfd;
}

} // namespace

// ASan regression: destroy the client mid-reconnect; the in-flight connect's
// completion lambda must not touch the freed connector.
TEST(ReconnectLifetime, DestroyDuringInflightReconnectNoUAF) {
    qb::io::async::init();

    int               port = 0;
    const socket_type lfd  = open_loopback_listener(port);
    ASSERT_NE(lfd, qb::io::inet::invalid_socket);

    const auto uri = qb::io::uri{"tcp://127.0.0.1:" + std::to_string(port)};

    // Initial connect goes to the same accept-less loopback listener: the TCP
    // handshake completes, so connect() succeeds with NO redis daemon.
    auto client = std::make_unique<qb::redis::tcp::client>(uri);
    ASSERT_TRUE(qb::io::async::run_sync(client->connect()))
        << "loopback listener should complete the TCP handshake";

    // Enable auto-reconnect (same loopback target) and force a disconnect so a
    // reconnect is spawned. A 2s connect timeout leaves a comfortable window in
    // which the connect is in flight.
    client->enable_auto_reconnect(qb::redis::RetryPolicy{}
                                      .with_max_attempts(3)
                                      .with_initial_delay(50ms)
                                      .with_connect_timeout(2s)
                                      .with_jitter(false));
    client->disconnect();

    // Reconnect started: the connect to the listener is registered and in flight
    // (its completion lambda runs on a later loop iteration, not this one).
    ASSERT_TRUE(run_until([&] { return client->is_reconnecting(); }, 2000ms))
        << "auto-reconnect did not start";

    // Destroy the client while the (about-to-succeed) connect is in flight.
    client.reset();

    // Pump so the connect completes and the lambda fires against the freed
    // client. With the fix the lambda's connector_alive() check short-circuits;
    // before the fix ASan reports heap-use-after-free in setup_connection.
    for (int i = 0; i < 500; ++i)
        qb::io::async::run(EVRUN_NOWAIT);

    closesocket(lfd);
    SUCCEED() << "no crash / no ASan use-after-free during mid-reconnect destruction";
}

// No in-file main(): links the framework's shared gtest-main.
