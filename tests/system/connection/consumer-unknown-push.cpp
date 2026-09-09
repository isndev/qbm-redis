/*
 * qb - C++ Actor Framework
 * Copyright (C) 2011-2026 isndev (cpp.actor). All rights reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *         http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 *         limitations under the License.
 */

/**
 * @file consumer-unknown-push.cpp
 * @brief A RESP3 PUSH frame of a kind the consumer does not know is dropped, never popped
 *        against the FIFO head (Huly QB-141).
 *
 * System tier: in-process plumbing over a loopback socket, NO external redis daemon, no
 * RESOURCE_LOCK. The test IS the server: it listens on a loopback port, the consumer connects,
 * the test `accept()`s the connection and writes raw RESP3 bytes into it, so every frame below
 * crosses the real socket, the real parser and the real dispatcher. The consumer first issues a
 * SUBSCRIBE that the server never confirms, so its handler sits at the head of the reply FIFO;
 * every case then sends the frame under test FOLLOWED by a known `message` push, and waits for
 * that message to be delivered -- TCP order guarantees the frame under test was dispatched first,
 * so the assertions read a settled state rather than a timer.
 *
 * What is proved, in both polarities:
 *   - a PUSH frame whose kind is unknown (`invalidate`, the shape CLIENT TRACKING produces;
 *     `smessage`, the sharded pub/sub one) leaves the pending handler UNRESOLVED and the FIFO
 *     at the same depth -- until 3.2 it popped the head and resolved the SUBSCRIBE with an
 *     invalidation, desynchronising the reply/command FIFO for the connection's lifetime;
 *   - a known PUSH kind (`message`) is still delivered to the message callback;
 *   - a PUSH `subscribe` confirmation still resolves the pending handler -- the guard drops
 *     the unknown, not the push;
 *   - a plain RESP2 ARRAY reply whose first element is not a pub/sub kind (`["pong", ""]`, what
 *     PING answers in subscriber mode) still resolves the FIFO head: that path is a command
 *     reply, not a push, and stays.
 */

#include <gtest/gtest.h>
#include <chrono>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <qb/io/system/sys__socket.h>
#include <qbm/redis/redis.h>

using namespace qb::io;
using namespace std::chrono_literals;

namespace {

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

// Pump the loop until `pred()` is true or `timeout` elapses; returns pred's final value.
template <typename Pred>
bool
run_until(Pred &&pred, std::chrono::milliseconds timeout = 2000ms) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!pred() && std::chrono::steady_clock::now() < deadline)
        qb::io::async::run(EVRUN_NOWAIT);
    return pred();
}

/// The consumer under test with one SUBSCRIBE pending (the server side -- this test -- never
/// confirms it), a message callback that counts, and a handler that records whether it was
/// resolved. `sfd` is the accepted server end the frames are written into.
struct Harness {
    int                                          port = 0;
    socket_type                                  lfd  = qb::io::inet::invalid_socket;
    socket_type                                  sfd  = qb::io::inet::invalid_socket;
    std::unique_ptr<qb::redis::tcp::cb_consumer> consumer;
    int                                          messages_seen = 0;
    int                                          handler_calls = 0;
    bool                                         handler_ok    = false;

    Harness() {
        lfd      = open_loopback_listener(port);
        consumer = std::make_unique<qb::redis::tcp::cb_consumer>(qb::io::uri{"tcp://127.0.0.1:" + std::to_string(port)},
                                                                 [this](qb::redis::message &&) { ++messages_seen; });
        EXPECT_TRUE(qb::io::async::run_sync(consumer->connect())) << "the loopback listener completes the handshake";
        sfd = ::accept(lfd, nullptr, nullptr);
        EXPECT_NE(sfd, qb::io::inet::invalid_socket) << "the server end of the consumer's connection";
        consumer->subscribe(
            [this](qb::redis::Reply<qb::redis::subscription> &&r) {
                ++handler_calls;
                handler_ok = r.ok();
            },
            "news");
        EXPECT_EQ(consumer->pending_reply_count(), 1u) << "the SUBSCRIBE handler must sit at the FIFO head";
        // Let the SUBSCRIBE bytes leave; the server (this test) reads and ignores them.
        for (int i = 0; i < 50; ++i)
            qb::io::async::run(EVRUN_NOWAIT);
    }
    ~Harness() {
        consumer.reset();
        if (sfd != qb::io::inet::invalid_socket)
            QB_CLOSESOCKET(sfd);
        if (lfd != qb::io::inet::invalid_socket)
            QB_CLOSESOCKET(lfd);
    }
    /// Write raw RESP bytes from the server side and pump the loop until the consumer has seen
    /// `expect_messages` message pushes in total -- the frame under test precedes the known
    /// `message` push in the same write, so it was dispatched first.
    bool
    serve(const std::string &bytes, int expect_messages) {
        const auto sent = ::send(sfd, bytes.data(), static_cast<int>(bytes.size()), 0);
        EXPECT_EQ(static_cast<std::size_t>(sent), bytes.size());
        return run_until([&] { return messages_seen >= expect_messages; });
    }
};

/// A RESP3 PUSH (`>`) or RESP2 ARRAY (`*`) of bulk strings.
std::string
frame(char kind, std::initializer_list<const char *> elems) {
    std::string out = std::string(1, kind) + std::to_string(elems.size()) + "\r\n";
    for (auto const *e : elems)
        out += "$" + std::to_string(std::strlen(e)) + "\r\n" + e + "\r\n";
    return out;
}
const std::string kKnownMessage = frame('>', {"message", "news", "hello"});

} // namespace

TEST(ConsumerUnknownPush, InvalidatePushLeavesThePendingHandlerAlone) {
    qb::io::async::init();
    Harness h;
    // `>2 invalidate ["k"]` -- what CLIENT TRACKING sends on the tracking connection -- then the
    // known message that proves the consumer got past it.
    const std::string invalidate = ">2\r\n$10\r\ninvalidate\r\n*1\r\n$1\r\nk\r\n";
    ASSERT_TRUE(h.serve(invalidate + kKnownMessage, 1)) << "the consumer never delivered the message that follows the push";
    EXPECT_EQ(h.handler_calls, 0) << "an unknown PUSH kind resolved the SUBSCRIBE at the FIFO head";
    EXPECT_EQ(h.consumer->pending_reply_count(), 1u) << "the FIFO must keep its depth";
    EXPECT_EQ(h.messages_seen, 1);
}

TEST(ConsumerUnknownPush, ShardedMessagePushLeavesThePendingHandlerAlone) {
    qb::io::async::init();
    Harness h;
    ASSERT_TRUE(h.serve(frame('>', {"smessage", "shard-channel", "payload"}) + kKnownMessage, 1));
    EXPECT_EQ(h.handler_calls, 0);
    EXPECT_EQ(h.consumer->pending_reply_count(), 1u);
    EXPECT_EQ(h.messages_seen, 1) << "an unknown kind is not a message either: only the known one counts";
}

TEST(ConsumerUnknownPush, KnownMessagePushStillDelivers) {
    qb::io::async::init();
    Harness h;
    ASSERT_TRUE(h.serve(kKnownMessage + kKnownMessage, 2)) << "the guard must not swallow a known pub/sub kind";
    EXPECT_EQ(h.messages_seen, 2);
    EXPECT_EQ(h.handler_calls, 0);
    EXPECT_EQ(h.consumer->pending_reply_count(), 1u);
}

TEST(ConsumerUnknownPush, KnownSubscribePushStillResolvesTheHandler) {
    qb::io::async::init();
    Harness           h;
    const std::string confirmation = ">3\r\n$9\r\nsubscribe\r\n$4\r\nnews\r\n:1\r\n";
    ASSERT_TRUE(h.serve(confirmation + kKnownMessage, 1));
    EXPECT_EQ(h.handler_calls, 1) << "the confirmation must still resolve its own handler";
    EXPECT_TRUE(h.handler_ok);
    EXPECT_EQ(h.consumer->pending_reply_count(), 0u);
}

TEST(ConsumerUnknownPush, ArrayReplyOfAnotherShapeStillResolvesTheHead) {
    qb::io::async::init();
    Harness h;
    // `["pong", ""]`: a RESP2 ARRAY, what PING answers in subscriber mode -- a command reply for
    // the FIFO head, and the reason the ARRAY path is not the PUSH path.
    ASSERT_TRUE(h.serve(frame('*', {"pong", ""}) + kKnownMessage, 1));
    EXPECT_EQ(h.handler_calls, 1) << "an array reply that is not a pub/sub kind belongs to the FIFO head";
    EXPECT_EQ(h.consumer->pending_reply_count(), 0u);
}
