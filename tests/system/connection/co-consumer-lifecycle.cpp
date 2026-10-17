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
 *     before the disconnect and not yet read, then the gap marker, then the new connection's messages.
 *
 * And the consumers' side of the reconnect contract (Huly QB-126b), each case failing on the code before it:
 *   - R5: the CONFIRMED subscriptions are replayed by the handshake of the new connection, before anything
 *     held; one gap marker (empty channel, `kind` gap) precedes the new connection's first item, and a message
 *     that arrives during the replay queues behind it;
 *   - R6: with a run armed, a lost connection does not end the stream: receive() stays parked across the
 *     reconnection and ends only when the consumer goes down, with down_reason();
 *   - R7: a full queue drops, on_message_dropped fires for each loss, and ONE marker carrying the count sits
 *     where the messages were lost.
 */

#include <gtest/gtest.h>
#include <array>
#include <chrono>
#include <cstring>
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

/// The documented receive loop: collects payloads -- a gap marker as "<gap:reason:lost>" -- until receive()
/// yields nullopt.
struct Drain {
    std::vector<std::string> got;
    bool                     ended = false;
};

std::string
item(qb::redis::message const &msg) {
    if (msg.is_gap())
        return "<gap:" + std::string{qb::io::gap_reason_name(msg.gap.reason)} + ":" + std::to_string(msg.gap.lost) + ">";
    EXPECT_FALSE(msg.channel.empty()) << "a published message always has a channel";
    return msg.payload;
}

qb::io::async::task<void>
drain(qb::redis::tcp::co_consumer *consumer, Drain *out) {
    while (auto msg = co_await consumer->receive())
        out->got.push_back(item(*msg));
    out->ended = true;
}

/// Read whole RESP commands off the server end, pumping the client's loop until `n` arrived.
std::vector<std::vector<std::string>>
read_commands(qb::io::tcp::socket &peer, std::size_t n, std::chrono::milliseconds timeout = 2000ms) {
    std::string                           buffer;
    std::vector<std::vector<std::string>> out;
    std::array<char, 1024>                buf{};
    run_until(
        [&] {
            for (int r; (r = peer.read(buf.data(), buf.size())) > 0;)
                buffer.append(buf.data(), static_cast<std::size_t>(r));
            for (;;) {
                if (buffer.empty() || buffer[0] != '*')
                    break;
                auto end = buffer.find("\r\n");
                if (end == std::string::npos)
                    break;
                const int                argc = std::stoi(buffer.substr(1, end - 1));
                std::size_t              pos  = end + 2;
                std::vector<std::string> cmd;
                bool                     whole = true;
                for (int i = 0; i < argc && whole; ++i) {
                    end = buffer.find("\r\n", pos);
                    if (end == std::string::npos) {
                        whole = false;
                        break;
                    }
                    const auto len = static_cast<std::size_t>(std::stoul(buffer.substr(pos + 1, end - pos - 1)));
                    pos            = end + 2;
                    if (buffer.size() < pos + len + 2) {
                        whole = false;
                        break;
                    }
                    cmd.push_back(buffer.substr(pos, len));
                    pos += len + 2;
                }
                if (!whole)
                    break;
                buffer.erase(0, pos);
                out.push_back(std::move(cmd));
            }
            return out.size() >= n;
        },
        timeout);
    return out;
}

/// A RESP2 ssubscribe confirmation for `channel`, `count` shard subscriptions active.
std::string
shard_confirmation(std::string const &channel, int count) {
    return "*3\r\n$10\r\nssubscribe\r\n$" + std::to_string(channel.size()) + "\r\n" + channel + "\r\n:" + std::to_string(count) + "\r\n";
}

/// A RESP2 subscribe confirmation for `channel`, `count` subscriptions active.
std::string
confirmation(std::string const &channel, int count) {
    return "*3\r\n$9\r\nsubscribe\r\n$" + std::to_string(channel.size()) + "\r\n" + channel + "\r\n:" + std::to_string(count) + "\r\n";
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
    ASSERT_TRUE(run_until([&] { return d.got.size() == 3; })) << "receive() must serve the new connection";
    EXPECT_EQ(d.got, (std::vector<std::string>{"before", "<gap:reconnect:0>", "after"}))
        << "what was unread is carried over, in order, then ONE marker where the stream has a hole (Huly QB-126b)";
    EXPECT_FALSE(d.ended) << "connected again: receive() parks for the next message";

    consumer.disconnect();
    EXPECT_TRUE(run_until([&] { return d.ended; })) << "and the second disconnect ends it again";
}

// ---------------------------------------------------------------------------------------- QB-126b

TEST_F(CoConsumerLifecycle, AReconnectionReplaysTheConfirmedSubscriptionsAndMarksTheGap) {
    LoopbackServer              server;
    qb::redis::tcp::co_consumer consumer{qb::io::uri{server.uri()}};
    consumer.enable_auto_reconnect(qb::redis::RetryPolicy{}.with_initial_delay(1ms).with_jitter(false));
    std::vector<qb::io::gap_info> gaps;
    consumer.on_gap([&gaps](qb::io::gap_info const &g) { gaps.push_back(g); });
    ASSERT_TRUE(qb::io::async::run_sync(consumer.connect()));
    auto first = server.accept_one();
    first.set_nonblocking(true);

    // "chan" is confirmed by the server; "never" is sent and never confirmed: only the first is replayed.
    bool confirmed = false;
    consumer.subscribe([&](qb::redis::Reply<qb::redis::subscription> &&r) { confirmed = r.ok(); }, "chan");
    consumer.subscribe([](qb::redis::Reply<qb::redis::subscription> &&) {}, "never");
    ASSERT_EQ(read_commands(first, 2).size(), 2u);
    const auto conf = confirmation("chan", 1);
    ASSERT_EQ(first.write(conf.data(), conf.size()), static_cast<int>(conf.size()));
    ASSERT_TRUE(run_until([&] { return confirmed; }));

    Drain d;
    qb::io::async::coro_scheduler().spawn(drain(&consumer, &d));
    push(first, "one");
    ASSERT_TRUE(run_until([&] { return d.got.size() == 1; }));
    first.disconnect(); // the peer drops: the run reconnects
    ASSERT_TRUE(run_until([&] { return consumer.is_reconnecting(); }));
    EXPECT_FALSE(d.ended) << "a recoverable loss must not end the stream (R6)";

    qb::io::tcp::socket second;
    server.listener.set_nonblocking(true);
    ASSERT_TRUE(run_until(
        [&, accepted = false]() mutable { return accepted || (accepted = server.listener.accept(second) == qb::io::SocketStatus::Done); }));
    second.set_nonblocking(true);
    auto replay = read_commands(second, 1);
    ASSERT_EQ(replay.size(), 1u);
    EXPECT_EQ(replay[0], (std::vector<std::string>{"SUBSCRIBE", "chan"})) << "only the confirmed subscription is replayed";
    EXPECT_FALSE(consumer.is_connected()) << "the replay is part of the handshake";

    // A message for the replayed channel arrives before the replay's confirmation: it queues behind the marker.
    push(second, "during");
    const auto again = confirmation("chan", 1);
    ASSERT_EQ(second.write(again.data(), again.size()), static_cast<int>(again.size()));
    ASSERT_TRUE(run_until([&] { return consumer.is_connected(); }));
    push(second, "after");
    ASSERT_TRUE(run_until([&] { return d.got.size() == 4; }));
    EXPECT_EQ(d.got, (std::vector<std::string>{"one", "<gap:reconnect:0>", "during", "after"}));
    ASSERT_EQ(gaps.size(), 1u) << "one marker, one on_gap";
    EXPECT_EQ(gaps[0].reason, qb::io::gap_reason::reconnect);
    EXPECT_FALSE(d.ended);

    consumer.disconnect();
    ASSERT_TRUE(run_until([&] { return d.ended; })) << "the stream ends at the terminal state (R6)";
    EXPECT_EQ(consumer.down_reason(), qb::io::down_reason::user);
}

TEST_F(CoConsumerLifecycle, AFullQueueLeavesOneMarkerCarryingTheCount) {
    LoopbackServer              server;
    qb::redis::tcp::co_consumer consumer{qb::io::uri{server.uri()}, 4};
    int                         dropped = 0;
    consumer.on_message_dropped([&dropped](qb::redis::message &&) { ++dropped; });
    std::vector<qb::io::gap_info> gaps;
    consumer.on_gap([&gaps](qb::io::gap_info const &g) { gaps.push_back(g); });
    ASSERT_TRUE(qb::io::async::run_sync(consumer.connect()));
    auto peer = server.accept_one();

    for (int i = 0; i < 7; ++i)
        push(peer, "m" + std::to_string(i)); // capacity 4: m4, m5, m6 do not fit
    settle(100ms);
    EXPECT_EQ(dropped, 3) << "on_message_dropped still fires for each loss (R7)";

    Drain d;
    qb::io::async::coro_scheduler().spawn(drain(&consumer, &d));
    ASSERT_TRUE(run_until([&] { return d.got.size() == 4; }));
    push(peer, "m7"); // the queue drained: the marker, then m7
    ASSERT_TRUE(run_until([&] { return d.got.size() == 6; }));
    EXPECT_EQ(d.got, (std::vector<std::string>{"m0", "m1", "m2", "m3", "<gap:overflow:3>", "m7"}));
    ASSERT_EQ(gaps.size(), 1u);
    EXPECT_EQ(gaps[0].lost, 3u);
    consumer.disconnect();
    // The receive loop writes into `d` when it ends: wait for it, or it resumes after `d` is gone.
    ASSERT_TRUE(run_until([&] { return d.ended; }));
}

TEST_F(CoConsumerLifecycle, WithoutARunALostPeerEndsTheStreamWithItsReason) {
    LoopbackServer              server;
    qb::redis::tcp::co_consumer consumer{qb::io::uri{server.uri()}};
    ASSERT_TRUE(qb::io::async::run_sync(consumer.connect()));
    auto peer = server.accept_one();

    Drain d;
    qb::io::async::coro_scheduler().spawn(drain(&consumer, &d));
    push(peer, "last");
    ASSERT_TRUE(run_until([&] { return d.got.size() == 1; }));
    peer.disconnect();
    ASSERT_TRUE(run_until([&] { return d.ended; })) << "no run armed: the loss is terminal, the stream ends";
    EXPECT_EQ(consumer.state(), qb::io::connection_state::down);
    EXPECT_EQ(consumer.down_reason(), qb::io::down_reason::peer);
}

// Huly QB-127: the confirmed shard channels are replayed too, one SSUBSCRIBE each -- on a cluster several slots
// in one call are refused (CROSSSLOT), and the consumer computes no slot. An unsubscribe-all of the shards
// expects one confirmation per shard channel it predicted.
TEST_F(CoConsumerLifecycle, AReconnectionReplaysTheConfirmedShardChannelsOneByOne) {
    LoopbackServer              server;
    qb::redis::tcp::co_consumer consumer{qb::io::uri{server.uri()}};
    consumer.enable_auto_reconnect(qb::redis::RetryPolicy{}.with_initial_delay(1ms).with_jitter(false));
    ASSERT_TRUE(qb::io::async::run_sync(consumer.connect()));
    auto first = server.accept_one();
    first.set_nonblocking(true);

    int confirmed = 0;
    consumer.ssubscribe([&](qb::redis::Reply<qb::redis::subscription> &&r) { confirmed += r.ok(); }, "s1");
    consumer.ssubscribe([&](qb::redis::Reply<qb::redis::subscription> &&r) { confirmed += r.ok(); }, "s2");
    const auto sent = read_commands(first, 2);
    ASSERT_EQ(sent.size(), 2u);
    EXPECT_EQ(sent[0], (std::vector<std::string>{"SSUBSCRIBE", "s1"}));
    const auto conf = shard_confirmation("s1", 1) + shard_confirmation("s2", 2);
    ASSERT_EQ(first.write(conf.data(), conf.size()), static_cast<int>(conf.size()));
    ASSERT_TRUE(run_until([&] { return confirmed == 2; }));

    first.disconnect();
    ASSERT_TRUE(run_until([&] { return consumer.is_reconnecting(); }));
    qb::io::tcp::socket second;
    server.listener.set_nonblocking(true);
    ASSERT_TRUE(run_until(
        [&, accepted = false]() mutable { return accepted || (accepted = server.listener.accept(second) == qb::io::SocketStatus::Done); }));
    second.set_nonblocking(true);
    auto replay = read_commands(second, 1);
    ASSERT_EQ(replay.size(), 1u) << "one SSUBSCRIBE at a time: the replay waits for each confirmation";
    ASSERT_EQ(replay[0].size(), 2u);
    EXPECT_EQ(replay[0][0], "SSUBSCRIBE");
    const auto first_shard = replay[0][1];
    auto       c1          = shard_confirmation(first_shard, 1);
    ASSERT_EQ(second.write(c1.data(), c1.size()), static_cast<int>(c1.size()));
    replay = read_commands(second, 1);
    ASSERT_EQ(replay.size(), 1u);
    ASSERT_EQ(replay[0].size(), 2u);
    EXPECT_EQ(replay[0][0], "SSUBSCRIBE");
    EXPECT_NE(replay[0][1], first_shard);
    EXPECT_TRUE((first_shard == "s1" && replay[0][1] == "s2") || (first_shard == "s2" && replay[0][1] == "s1"));
    auto c2 = shard_confirmation(replay[0][1], 2);
    ASSERT_EQ(second.write(c2.data(), c2.size()), static_cast<int>(c2.size()));
    ASSERT_TRUE(run_until([&] { return consumer.is_connected(); })) << "the replay is part of the handshake";

    bool all_gone = false;
    consumer.sunsubscribe([&](qb::redis::Reply<qb::redis::subscription> &&r) { all_gone = r.ok() && r.result().num == 0; });
    const auto unsub = read_commands(second, 1);
    ASSERT_EQ(unsub.size(), 1u);
    EXPECT_EQ(unsub[0], (std::vector<std::string>{"SUNSUBSCRIBE"}));
    const auto u = "*3\r\n$12\r\nsunsubscribe\r\n$2\r\ns1\r\n:1\r\n*3\r\n$12\r\nsunsubscribe\r\n$2\r\ns2\r\n:0\r\n";
    ASSERT_EQ(second.write(u, std::strlen(u)), static_cast<int>(std::strlen(u)));
    ASSERT_TRUE(run_until([&] { return all_gone; })) << "resolved on the second confirmation, the one per predicted shard";
    EXPECT_EQ(consumer.pending_reply_count(), 0u);
    consumer.disconnect();
}
