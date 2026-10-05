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
 * @file co-consumer-lifecycle.cpp
 * @brief `co_consumer::receive()` across a disconnect and a reconnect (Huly QB-252).
 *
 * System tier: in-process plumbing over a loopback socket, NO external redis daemon. The test IS
 * the server: it accepts the consumer's connection and pushes RESP2 pub/sub `message` frames,
 * which the consumer delivers out of band whatever its reply FIFO holds.
 *
 * The documented loop `while (auto msg = co_await consumer.receive())` ends when the consumer
 * disconnects -- the README's shutdown pattern rests on it. It never did: RedisConsumer routes the
 * disconnect to the derived consumer through has_method_on, the coroutine consumer's handler is
 * private and befriended only RedisConsumer, so the detector never saw it and the queue was never
 * closed. And a closed queue stays closed: after a reconnect nothing could be received again.
 * Proved here, each case failing on the code before the fix:
 *   - a parked receive() resumes with nullopt when the consumer disconnects, and when the peer drops;
 *   - what was received before the disconnect is still read, then nullopt -- and a receive()
 *     while disconnected answers at once instead of parking;
 *   - after a reconnect, receive() serves the new connection, starting with what was received
 *     before the disconnect and not yet read.
 */

#include <gtest/gtest.h>
#include <array>
#include <chrono>
#include <optional>
#include <string>
#include <vector>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <qb/io/tcp/listener.h>
#include <qb/io/tcp/socket.h>
#include <qbm/redis/redis.h>

using namespace std::chrono_literals;

namespace co_consumer_lifecycle_test {

// Pump the loop -- watchers and ready coroutines -- until `pred()` holds or `timeout` elapses.
template <typename Pred>
bool
run_until(Pred &&pred, std::chrono::milliseconds timeout = 2000ms) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!pred() && std::chrono::steady_clock::now() < deadline)
        qb::io::async::run(EVRUN_NOWAIT);
    return pred();
}

/// Pump the loop for `d`: the frames written so far are parsed, ready coroutines run.
void
settle(std::chrono::milliseconds d) {
    run_until([] { return false; }, d);
}

/// The server side: a loopback listener; each accepted connection is the consumer's.
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
    /// The server end of the connection the consumer just completed (held in the backlog).
    qb::io::tcp::socket
    accept_one() {
        qb::io::tcp::socket s;
        EXPECT_EQ(listener.accept(s), qb::io::SocketStatus::Done);
        return s;
    }
};

/// A RESP2 pub/sub `message` frame on channel "chan".
std::string
message_frame(std::string const &payload) {
    return "*3\r\n$7\r\nmessage\r\n$4\r\nchan\r\n$" + std::to_string(payload.size()) + "\r\n" + payload + "\r\n";
}

void
push(qb::io::tcp::socket &peer, std::string const &payload) {
    const auto frame = message_frame(payload);
    ASSERT_EQ(peer.write(frame.data(), frame.size()), static_cast<int>(frame.size()));
}

/// The documented receive loop: collects payloads until receive() yields nullopt.
struct Drain {
    std::vector<std::string> got;
    bool                     ended = false;
};

qb::io::async::task<void>
drain(qb::redis::tcp::co_consumer *consumer, Drain *out) {
    while (auto msg = co_await consumer->receive())
        out->got.push_back(msg->payload);
    out->ended = true;
}

class CoConsumerLifecycle : public ::testing::Test {
protected:
    void
    SetUp() override {
        qb::io::async::init();
    }
    void
    TearDown() override {
        if (qb::io::async::listener::current.has_coro_scheduler()) {
            qb::io::async::run_for(5ms);
            qb::io::async::listener::current.reset_coro_scheduler();
        }
        qb::io::async::listener::current.clear();
    }
};

} // namespace co_consumer_lifecycle_test

using namespace co_consumer_lifecycle_test;

TEST_F(CoConsumerLifecycle, AParkedReceiveEndsWhenTheConsumerDisconnects) {
    LoopbackServer              server;
    qb::redis::tcp::co_consumer consumer{qb::io::uri{server.uri()}};
    ASSERT_TRUE(qb::io::async::run_sync(consumer.connect()));
    auto peer = server.accept_one();

    Drain d;
    qb::io::async::coro_scheduler().spawn(drain(&consumer, &d));
    push(peer, "hello");
    ASSERT_TRUE(run_until([&] { return d.got.size() == 1; })) << "the message never reached receive()";
    EXPECT_FALSE(d.ended) << "receive() must still be parked while connected";

    consumer.disconnect(); // the README's shutdown pattern
    EXPECT_TRUE(run_until([&] { return d.ended; })) << "the receive loop must end when the consumer disconnects";
    EXPECT_EQ(d.got, (std::vector<std::string>{"hello"}));
}

TEST_F(CoConsumerLifecycle, AParkedReceiveEndsWhenThePeerDrops) {
    LoopbackServer              server;
    qb::redis::tcp::co_consumer consumer{qb::io::uri{server.uri()}};
    ASSERT_TRUE(qb::io::async::run_sync(consumer.connect()));
    auto peer = server.accept_one();

    Drain d;
    qb::io::async::coro_scheduler().spawn(drain(&consumer, &d));
    settle(50ms); // the loop parks on receive()
    EXPECT_FALSE(d.ended);

    peer.disconnect(); // the server goes away
    EXPECT_TRUE(run_until([&] { return d.ended; })) << "the receive loop must end when the peer drops";
    EXPECT_FALSE(consumer.is_connected());
}

TEST_F(CoConsumerLifecycle, WhatWasReceivedIsReadThenNulloptWhileDisconnected) {
    LoopbackServer              server;
    qb::redis::tcp::co_consumer consumer{qb::io::uri{server.uri()}};
    ASSERT_TRUE(qb::io::async::run_sync(consumer.connect()));
    auto peer = server.accept_one();

    push(peer, "a");
    push(peer, "b");
    settle(100ms); // both frames parsed and queued; nobody reads yet
    consumer.disconnect();

    Drain d;
    qb::io::async::coro_scheduler().spawn(drain(&consumer, &d));
    EXPECT_TRUE(run_until([&] { return d.ended; })) << "receive() must not park while disconnected";
    EXPECT_EQ(d.got, (std::vector<std::string>{"a", "b"})) << "what was received before the disconnect is still read";

    Drain again;
    qb::io::async::coro_scheduler().spawn(drain(&consumer, &again));
    EXPECT_TRUE(run_until([&] { return again.ended; })) << "a later receive() while disconnected answers at once";
    EXPECT_TRUE(again.got.empty());
}

TEST_F(CoConsumerLifecycle, AfterAReconnectReceiveServesTheNewConnection) {
    LoopbackServer              server;
    qb::redis::tcp::co_consumer consumer{qb::io::uri{server.uri()}};
    ASSERT_TRUE(qb::io::async::run_sync(consumer.connect()));
    auto first = server.accept_one();

    push(first, "before"); // received, never read before the disconnect
    settle(100ms);
    consumer.disconnect();

    ASSERT_TRUE(qb::io::async::run_sync(consumer.connect())) << "the same consumer reconnects";
    auto second = server.accept_one();

    Drain d;
    qb::io::async::coro_scheduler().spawn(drain(&consumer, &d));
    push(second, "after");
    ASSERT_TRUE(run_until([&] { return d.got.size() == 2; })) << "receive() must serve the new connection";
    EXPECT_EQ(d.got, (std::vector<std::string>{"before", "after"})) << "what was unread is carried over, in order";
    EXPECT_FALSE(d.ended) << "connected again: receive() parks for the next message";

    consumer.disconnect();
    EXPECT_TRUE(run_until([&] { return d.ended; })) << "and the second disconnect ends it again";
}
