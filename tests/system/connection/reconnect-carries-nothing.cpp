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
 * @file reconnect-carries-nothing.cpp
 * @brief A reconnection carries nothing of the connection that dropped (Huly QB-202).
 *
 * System tier: in-process plumbing over a loopback socket, NO external redis daemon, no
 * RESOURCE_LOCK. The test IS the server: it listens on a loopback port, the client connects,
 * the test accepts each connection and reads, byte for byte, what reaches it -- so what a
 * reconnection carries is observed on the wire, not inferred from a reply.
 *
 * The client object is the io of every connection it opens, and nothing used to empty its
 * buffers between two of them. Proved here, each case failing on the code before the fix:
 *   - `disconnect()` has failed every queued handler by the time it returns. It used to defer
 *     the teardown to the watcher's next dispatch, where a connect() completing first could
 *     cancel it (libev runs pending watchers last-in, first-out): the commands in flight were
 *     then never failed and received the next connection's replies;
 *   - the next connection carries nothing of the one that dropped. A command serialized but never
 *     flushed used to go out first on it, and its reply was handed to whichever command was then
 *     first in line -- the FIFO off by one for good. Since the reconnect contract (Huly QB-126a) an
 *     explicit disconnect() is terminal: what a failing callback re-issues after it fails too, and
 *     the next connect() carries nothing at all;
 *   - over a loss the client recovers from -- the peer drops, a reconnection run is armed -- a
 *     command a failing callback re-issues (the documented retry shape) is HELD, leaves with the
 *     next connection, and receives ITS reply.
 */

#include <gtest/gtest.h>
#include <array>
#include <chrono>
#include <string>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <qb/io/tcp/listener.h>
#include <qb/io/tcp/socket.h>
#include <qbm/redis/redis.h>

using namespace std::chrono_literals;

namespace {

// Pump the loop until `pred()` is true or `timeout` elapses; returns pred's final value.
template <typename Pred>
bool
run_until(Pred &&pred, std::chrono::milliseconds timeout = 2000ms) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!pred() && std::chrono::steady_clock::now() < deadline)
        qb::io::async::run(EVRUN_NOWAIT);
    return pred();
}

/// The server side: a loopback listener; each accepted connection is read without blocking
/// while the client's loop is pumped, so the test sees exactly the bytes that crossed it.
struct LoopbackServer {
    qb::io::tcp::listener listener;
    std::uint16_t         port = 0;

    LoopbackServer() {
        EXPECT_EQ(listener.listen_v4(0, "127.0.0.1"), qb::io::SocketStatus::Done);
        port = listener.local_endpoint().port();
        EXPECT_NE(port, 0);
    }
    [[nodiscard]] std::string
    uri() const {
        return "tcp://127.0.0.1:" + std::to_string(port);
    }
    /// The server end of the connection the client just completed (the kernel holds it in the
    /// backlog, so a blocking accept returns at once), switched to non-blocking for reads.
    qb::io::tcp::socket
    accept_one() {
        qb::io::tcp::socket s;
        EXPECT_EQ(listener.accept(s), qb::io::SocketStatus::Done);
        s.set_nonblocking(true);
        return s;
    }
};

/// Read what `s` has received while pumping the client's loop, until `want` bytes arrived or the
/// loop has gone quiet for a while -- returning MORE than `want` when more came is the point.
std::string
read_while_pumping(qb::io::tcp::socket &s, std::size_t want) {
    std::string           got;
    std::array<char, 256> buf{};
    run_until([&] {
        for (int r; (r = s.read(buf.data(), buf.size())) > 0;)
            got.append(buf.data(), static_cast<std::size_t>(r));
        return got.size() >= want;
    });
    // A short settle: anything the client wrote right behind `want` is in the kernel by now.
    for (int i = 0; i < 50; ++i) {
        qb::io::async::run(EVRUN_NOWAIT);
        for (int r; (r = s.read(buf.data(), buf.size())) > 0;)
            got.append(buf.data(), static_cast<std::size_t>(r));
    }
    return got;
}

const std::string kEchoFresh = "*2\r\n$4\r\nECHO\r\n$5\r\nfresh\r\n";

} // namespace

namespace reconnect_carries_nothing_test {

// One argument that cannot be serialized: it throws what the size guard throws (Huly QB-255).
struct Unserializable {};

inline std::size_t
redis_count(Unserializable const &) noexcept {
    return 1;
}

inline bool
to_redis_string(qb::allocator::pipe<char> &, Unserializable const &) {
    throw qb::redis::SecurityError("String too large");
}

} // namespace reconnect_carries_nothing_test

TEST(RedisReconnectCarriesNothing, DisconnectHasFailedTheQueuedCommandsWhenItReturns) {
    qb::io::async::init();
    LoopbackServer         server;
    qb::redis::tcp::client client{qb::io::uri{server.uri()}};
    ASSERT_TRUE(qb::io::async::run_sync(client.connect()));
    auto peer = server.accept_one();

    int  calls = 0;
    bool ok    = true;
    client.command<std::string>(
        [&](qb::redis::Reply<std::string> &&r) {
            ++calls;
            ok = r.ok();
        },
        "ECHO", "stale");
    ASSERT_EQ(client.pending_reply_count(), 1u);

    client.disconnect();
    // Before QB-202 the teardown waited for the watcher's next dispatch: zero calls here, and a
    // connect() completing first could cancel it altogether.
    EXPECT_EQ(calls, 1) << "disconnect() must have failed the queued command before returning";
    EXPECT_FALSE(ok);
    EXPECT_EQ(client.pending_reply_count(), 0u);
    EXPECT_FALSE(client.is_connected());
}

TEST(RedisReconnectCarriesNothing, AfterAnExplicitDisconnectTheNextConnectionCarriesNothing) {
    qb::io::async::init();
    LoopbackServer         server;
    qb::redis::tcp::client client{qb::io::uri{server.uri()}};
    ASSERT_TRUE(qb::io::async::run_sync(client.connect()));
    auto first = server.accept_one();

    int         reissued_calls = 0;
    std::string reissued_error;
    // ECHO stale is serialized but never flushed -- no loop pass runs before the disconnect. Its
    // failing callback re-issues a command: the documented retry shape.
    client.command<std::string>(
        [&](qb::redis::Reply<std::string> &&r) {
            EXPECT_FALSE(r.ok()) << "the command that never left must be failed, not answered";
            client.command<std::string>(
                [&](qb::redis::Reply<std::string> &&r2) {
                    ++reissued_calls;
                    reissued_error = r2.error();
                },
                "ECHO", "fresh");
        },
        "ECHO", "stale");
    client.disconnect();
    EXPECT_EQ(client.state(), qb::io::connection_state::down);
    // disconnect() is terminal: the re-issued command waits for no connection, it fails from the loop.
    ASSERT_TRUE(run_until([&] { return reissued_calls == 1; })) << "a command issued after disconnect() must not wait";
    EXPECT_EQ(reissued_error, "disconnected");
    EXPECT_EQ(client.pending_reply_count(), 0u);

    ASSERT_TRUE(qb::io::async::run_sync(client.connect())) << "the same client reconnects";
    auto              second = server.accept_one();
    const std::string got    = read_while_pumping(second, 1);
    EXPECT_TRUE(got.empty()) << "the next connection must carry nothing of the one that dropped, got: " << got;
}

TEST(RedisReconnectCarriesNothing, ACommandAFailingCallbackReissuesLeavesWithTheRecoveredConnection) {
    qb::io::async::init();
    LoopbackServer         server;
    qb::redis::tcp::client client{qb::io::uri{server.uri()}};
    client.enable_auto_reconnect(qb::redis::RetryPolicy{}.with_initial_delay(1ms).with_jitter(false));
    ASSERT_TRUE(qb::io::async::run_sync(client.connect()));
    auto first = server.accept_one();

    int         stale_calls = 0;
    int         fresh_calls = 0;
    std::string fresh_reply;
    client.command<std::string>(
        [&](qb::redis::Reply<std::string> &&r) {
            ++stale_calls;
            EXPECT_FALSE(r.ok()) << "the command in flight on the dropped link fails";
            client.command<std::string>(
                [&](qb::redis::Reply<std::string> &&r2) {
                    ++fresh_calls;
                    if (r2.ok())
                        fresh_reply = r2.result();
                },
                "ECHO", "fresh");
        },
        "ECHO", "stale");
    const std::string kEchoStale = "*2\r\n$4\r\nECHO\r\n$5\r\nstale\r\n";
    ASSERT_EQ(read_while_pumping(first, kEchoStale.size()), kEchoStale);
    server.listener.set_nonblocking(true); // the second accept is polled while the client's loop runs
    first.disconnect();                    // the peer drops: a loss the armed run recovers from

    // Wait for the loss itself -- the failure of the command that was in flight -- not for a state: the client is
    // still is_connected() until it reads the drop, and a check made then counts the stale command, not the re-issue.
    ASSERT_TRUE(run_until([&] { return stale_calls == 1; })) << "the drop never failed the command in flight";
    EXPECT_EQ(client.pending_reply_count(), 1u) << "only the re-issued command may be waiting, held";
    EXPECT_EQ(client.stats().held, 1u) << "the re-issued command was held for the next connection";
    qb::io::tcp::socket second;
    bool                accepted = false; // run_until re-evaluates its predicate: accept once, then remember it
    ASSERT_TRUE(run_until([&] { return accepted || (accepted = server.listener.accept(second) == qb::io::SocketStatus::Done); }));
    second.set_nonblocking(true);

    // Exactly the re-issued command, and nothing of the connection that dropped.
    const std::string got = read_while_pumping(second, kEchoFresh.size());
    EXPECT_EQ(got, kEchoFresh) << "the next connection must carry only what was held after the loss";

    // And its reply reaches it -- not a command that was failed.
    const std::string reply = "$5\r\nfresh\r\n";
    ASSERT_EQ(second.write(reply.data(), reply.size()), static_cast<int>(reply.size()));
    ASSERT_TRUE(run_until([&] { return fresh_calls == 1; })) << "the re-issued command never got its reply";
    EXPECT_EQ(fresh_reply, "fresh");
}

// Huly QB-255 on the wire: a command that cannot be serialized leaves no partial frame behind, whether the
// client is ready (it writes to the link) or connecting (it holds) -- the next connection carries exactly
// the command that followed it.
TEST(RedisReconnectCarriesNothing, AnUnserializableCommandLeavesNoPartialFrameOnTheWire) {
    qb::io::async::init();
    LoopbackServer         server;
    qb::redis::tcp::client client{qb::io::uri{server.uri()}};

    bool        held_failed = false;
    std::string held_error;
    client.connect([](bool) {}, client.uri()); // connecting: what follows is held
    client.command<qb::redis::status>(
        [&](qb::redis::Reply<qb::redis::status> &&r) {
            held_failed = !r.ok();
            held_error  = r.error();
        },
        "SET", "held", reconnect_carries_nothing_test::Unserializable{});
    EXPECT_TRUE(held_failed) << "the failure arrives before command() returns";
    EXPECT_EQ(held_error, "String too large");
    client.command<qb::redis::status>([](qb::redis::Reply<qb::redis::status> &&) {}, "SET", "k", "1");
    ASSERT_TRUE(run_until([&] { return client.is_connected(); }));
    auto              peer     = server.accept_one();
    const std::string kHeldSet = "*3\r\n$3\r\nSET\r\n$1\r\nk\r\n$1\r\n1\r\n";
    EXPECT_EQ(read_while_pumping(peer, kHeldSet.size()), kHeldSet) << "only the whole held command may cross";

    bool ready_failed = false;
    client.command<qb::redis::status>([&](qb::redis::Reply<qb::redis::status> &&r) { ready_failed = !r.ok(); }, "SET", "ready",
                                      reconnect_carries_nothing_test::Unserializable{});
    EXPECT_TRUE(ready_failed);
    EXPECT_EQ(std::string(client.out().begin(), client.out().size()), "") << "no partial frame may stay in out()";
    client.command<qb::redis::status>([](qb::redis::Reply<qb::redis::status> &&) {}, "SET", "k", "2");
    const std::string kReadySet = "*3\r\n$3\r\nSET\r\n$1\r\nk\r\n$1\r\n2\r\n";
    EXPECT_EQ(std::string(client.out().begin(), client.out().size()), kReadySet);
    EXPECT_EQ(read_while_pumping(peer, kReadySet.size()), kReadySet);
}
