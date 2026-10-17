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
 * @file client-stats.cpp
 * @brief What a client or consumer reports of itself (Huly QB-134): `stats()`, `on_command_complete`, the
 *        per-family latency histograms.
 *
 * System tier: a loopback socket and NO external redis daemon. The test is the server: it reads each command
 * off the wire and answers what the case scripts -- a value, an error reply, nothing (the peer drops).
 *
 * What is proved:
 *   - every command is counted once, when it completes, whatever way: a value, the server's error, a failure
 *     with no answer (the link lost, the client down, refused locally); `commands` equals the callbacks
 *     called, and the hook sees each one, before the callback, with its verb, family and outcome;
 *   - latency is opt-in: off, no histogram and a zero duration; on, the family's histogram records each
 *     command and the hook's duration is the round trip;
 *   - a held command is counted as held; a reconnection the run reached counts, a `disconnect()` + `connect()`
 *     does not; an unknown verb is named, in family `other`; a throwing hook is contained;
 *   - a consumer counts its stream: the messages received, those its full queue dropped, the gap markers.
 */

#include <gtest/gtest.h>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <qb/io/tcp/listener.h>
#include <qb/io/tcp/socket.h>
#include <qbm/redis/redis.h>

using namespace std::chrono_literals;

namespace client_stats_test {

using Client = qb::redis::tcp::client;

template <typename Pred>
bool
run_until(Pred &&pred, std::chrono::milliseconds timeout = 3000ms) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!pred() && std::chrono::steady_clock::now() < deadline)
        qb::io::async::run(EVRUN_NOWAIT);
    return pred();
}

void
settle(std::chrono::milliseconds d) {
    run_until([] { return false; }, d);
}

/// The server end of one connection: reads whole RESP commands (counted), writes raw replies.
struct Peer {
    qb::io::tcp::socket sock;
    std::string         buffer;
    std::size_t         commands = 0;

    /// Consume every complete `*N` command buffered so far.
    void
    parse() {
        for (;;) {
            if (buffer.empty() || buffer[0] != '*')
                return;
            auto line_end = buffer.find("\r\n");
            if (line_end == std::string::npos)
                return;
            const int   argc = std::stoi(buffer.substr(1, line_end - 1));
            std::size_t pos  = line_end + 2;
            for (int i = 0; i < argc; ++i) {
                line_end = buffer.find("\r\n", pos);
                if (line_end == std::string::npos)
                    return;
                const auto len = static_cast<std::size_t>(std::stoul(buffer.substr(pos + 1, line_end - pos - 1)));
                pos            = line_end + 2;
                if (buffer.size() < pos + len + 2)
                    return;
                pos += len + 2;
            }
            buffer.erase(0, pos);
            ++commands;
        }
    }

    /// Pump the loop until `n` more commands arrived.
    bool
    expect(std::size_t n, std::chrono::milliseconds timeout = 3000ms) {
        const std::size_t want = commands + n;
        return run_until(
            [&] {
                char buf[4096];
                for (int r; (r = sock.read(buf, sizeof(buf))) > 0;)
                    buffer.append(buf, static_cast<std::size_t>(r));
                parse();
                return commands >= want;
            },
            timeout);
    }

    void
    send(std::string const &raw) {
        ASSERT_EQ(sock.write(raw.data(), raw.size()), static_cast<int>(raw.size()));
    }
};

struct FakeRedis {
    qb::io::tcp::listener listener;
    std::uint16_t         port = 0;

    FakeRedis() {
        EXPECT_EQ(listener.listen_v4(0, "127.0.0.1"), qb::io::SocketStatus::Done);
        listener.set_nonblocking(true);
        port = listener.local_endpoint().port();
    }
    [[nodiscard]] qb::io::uri
    uri() const {
        return qb::io::uri{"tcp://127.0.0.1:" + std::to_string(port)};
    }
    std::unique_ptr<Peer>
    accept(std::chrono::milliseconds timeout = 3000ms) {
        auto peer     = std::make_unique<Peer>();
        bool accepted = false;
        if (!run_until([&] { return accepted || (accepted = listener.accept(peer->sock) == qb::io::SocketStatus::Done); }, timeout))
            return nullptr;
        peer->sock.set_nonblocking(true);
        return peer;
    }
};

std::string
bulk(std::string const &s) {
    return "$" + std::to_string(s.size()) + "\r\n" + s + "\r\n";
}

/// What the hook saw, copied (its views die with the call).
struct Seen {
    std::string                name;
    qb::redis::command_family  family{};
    qb::redis::command_outcome outcome{};
    bool                       ok = false;
    std::string                error;
    qb::duration               duration{};
    int                        callbacks_before = 0; ///< callbacks already called when the hook ran
};

struct Recorder {
    std::vector<Seen> seen;
    int               callbacks = 0;

    void
    watch(Client &client) {
        client.on_command_complete([this](qb::redis::command_info const &info) {
            seen.push_back(Seen{std::string{info.name}, info.family, info.outcome, info.ok, std::string{info.error}, info.duration, callbacks});
        });
    }
    auto
    sink() {
        return [this](qb::redis::Reply<std::string> &&) {
            ++callbacks;
        };
    }
};

/// A consumer's receive loop, counting what it received until the stream ends.
struct Drain {
    int  got   = 0;
    bool ended = false;
};

qb::io::async::task<void>
drain(qb::redis::tcp::co_consumer *consumer, Drain *out) {
    while (auto m = co_await consumer->receive())
        ++out->got;
    out->ended = true;
}

std::size_t
family(qb::redis::command_family f) {
    return static_cast<std::size_t>(f);
}

class ClientStats : public ::testing::Test {
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

    /// Connect `client` to `server` and return the server end.
    static std::unique_ptr<Peer>
    connect(Client &client, FakeRedis &server) {
        auto done = std::make_shared<bool>(false);
        client.connect([done](bool) { *done = true; }, client.uri());
        auto peer = server.accept();
        EXPECT_TRUE(peer);
        EXPECT_TRUE(run_until([&] { return *done; }));
        return peer;
    }
};

} // namespace client_stats_test

using namespace client_stats_test;
using qb::redis::command_family;
using qb::redis::command_outcome;

TEST_F(ClientStats, EveryCommandIsCountedOnceWhenItCompletes) {
    FakeRedis server;
    Client    client{server.uri()};
    Recorder  rec;
    rec.watch(client);
    auto peer = connect(client, server);
    ASSERT_TRUE(peer);

    client.command<std::string>(rec.sink(), "GET", "k");        // a value
    client.command<std::string>(rec.sink(), "SET", "k", "v");   // the server's error
    client.command<std::string>(rec.sink(), "LPUSH", "l", "x"); // lost with the link
    ASSERT_TRUE(peer->expect(3));
    peer->send(bulk("v") + "-ERR boom\r\n");
    ASSERT_TRUE(run_until([&] { return rec.callbacks == 2; }));
    peer->sock.disconnect();
    ASSERT_TRUE(run_until([&] { return rec.callbacks == 3; }));
    ASSERT_TRUE(run_until([&] { return client.state() == qb::io::connection_state::down; }));
    client.command<std::string>(rec.sink(), "GET", "k"); // down: refused, on the next loop pass
    ASSERT_TRUE(run_until([&] { return rec.callbacks == 4; }));

    const auto s = client.stats();
    EXPECT_EQ(s.commands, 4u);
    EXPECT_EQ(s.commands, static_cast<std::uint64_t>(rec.callbacks)) << "each command counted once, as many as callbacks";
    EXPECT_EQ(s.errors, 3u);
    EXPECT_EQ(s.failed, 2u);
    EXPECT_EQ(s.by_family[family(command_family::string)], 3u);
    EXPECT_EQ(s.by_family[family(command_family::list)], 1u);
    EXPECT_EQ(s.held, 0u);

    ASSERT_EQ(rec.seen.size(), 4u);
    EXPECT_EQ(rec.seen[0].name, "GET");
    EXPECT_EQ(rec.seen[0].family, command_family::string);
    EXPECT_EQ(rec.seen[0].outcome, command_outcome::ok);
    EXPECT_TRUE(rec.seen[0].ok);
    EXPECT_EQ(rec.seen[0].callbacks_before, 0) << "the hook runs before the command's own callback";
    EXPECT_EQ(rec.seen[1].name, "SET");
    EXPECT_EQ(rec.seen[1].outcome, command_outcome::error);
    EXPECT_EQ(rec.seen[1].error, "ERR boom");
    EXPECT_EQ(rec.seen[2].name, "LPUSH");
    EXPECT_EQ(rec.seen[2].family, command_family::list);
    EXPECT_EQ(rec.seen[2].outcome, command_outcome::failed);
    EXPECT_EQ(rec.seen[2].error, "disconnected");
    EXPECT_EQ(rec.seen[3].outcome, command_outcome::failed);
    EXPECT_EQ(rec.seen[3].error, "disconnected") << "a command on a down client fails with the reason it went down";
    for (auto const &seen : rec.seen)
        EXPECT_EQ(seen.duration, qb::duration::zero()) << "latency stats are off: no clock";
}

TEST_F(ClientStats, LatencyIsRecordedOnlyOnceTurnedOn) {
    FakeRedis server;
    Client    client{server.uri()};
    Recorder  rec;
    rec.watch(client);
    auto peer = connect(client, server);
    ASSERT_TRUE(peer);
    EXPECT_FALSE(client.latency_stats());

    client.command<std::string>(rec.sink(), "GET", "a");
    ASSERT_TRUE(peer->expect(1));
    peer->send(bulk("1"));
    ASSERT_TRUE(run_until([&] { return rec.callbacks == 1; }));
    EXPECT_EQ(client.latency(command_family::string).count(), 0u) << "off: nothing recorded";

    client.set_latency_stats(true);
    EXPECT_TRUE(client.latency_stats());
    client.command<std::string>(rec.sink(), "GET", "b");
    client.command<std::string>(rec.sink(), "HGET", "h", "f");
    ASSERT_TRUE(peer->expect(2));
    settle(2ms); // the round trip takes measurable time
    peer->send(bulk("2") + bulk("3"));
    ASSERT_TRUE(run_until([&] { return rec.callbacks == 3; }));
    const auto strings = client.latency(command_family::string);
    EXPECT_EQ(strings.count(), 1u);
    EXPECT_EQ(client.latency(command_family::hash).count(), 1u);
    EXPECT_GE(strings.percentile(0.5), qb::duration{std::chrono::milliseconds{1}}) << "the bucket holds the 2 ms wait";
    ASSERT_EQ(rec.seen.size(), 3u);
    EXPECT_EQ(rec.seen[0].duration, qb::duration::zero());
    EXPECT_GE(rec.seen[1].duration, qb::duration{std::chrono::milliseconds{2}}) << "send to completion";

    client.set_latency_stats(false);
    EXPECT_EQ(client.latency(command_family::string).count(), 0u) << "off drops what was recorded";
}

TEST_F(ClientStats, HeldCommandsAndReconnectionsAreCounted) {
    FakeRedis server;
    Client    client{server.uri()};
    Recorder  rec;
    client.enable_auto_reconnect(qb::redis::RetryPolicy{}.with_initial_delay(1ms).with_jitter(false));
    auto first = connect(client, server);
    ASSERT_TRUE(first);
    first->sock.disconnect(); // the peer's loss: the run reconnects
    ASSERT_TRUE(run_until([&] { return client.is_reconnecting(); }));
    client.command<std::string>(rec.sink(), "PING"); // held until ready
    EXPECT_EQ(client.stats().held, 1u);
    auto second = server.accept();
    ASSERT_TRUE(second);
    ASSERT_TRUE(run_until([&] { return client.is_connected(); }));
    EXPECT_EQ(client.stats().reconnects, 1u) << "a ready connection reached after a lost one";
    ASSERT_TRUE(second->expect(1));
    second->send("+PONG\r\n");
    ASSERT_TRUE(run_until([&] { return rec.callbacks == 1; }));

    client.disconnect(); // ends the session: the next ready is no reconnection
    auto again = std::make_shared<bool>(false);
    client.connect([again](bool) { *again = true; }, client.uri());
    auto third = server.accept();
    ASSERT_TRUE(third);
    ASSERT_TRUE(run_until([&] { return *again; }));
    EXPECT_EQ(client.stats().reconnects, 1u) << "disconnect() then connect() is a new session, not a reconnection";
}

TEST_F(ClientStats, AnUnknownVerbIsNamedAndAThrowingHookIsContained) {
    FakeRedis                server;
    Client                   client{server.uri()};
    Recorder                 rec;
    std::vector<std::string> names;
    client.on_command_complete([&names](qb::redis::command_info const &info) {
        names.emplace_back(info.name);
        throw std::runtime_error("a hook that throws");
    });
    auto peer = connect(client, server);
    ASSERT_TRUE(peer);
    client.command<std::string>(rec.sink(), "MY.MODULE.CMD", "x");
    client.command<std::string>(rec.sink(), "get", "k"); // another letter case
    ASSERT_TRUE(peer->expect(2));
    peer->send(bulk("a") + bulk("b"));
    ASSERT_TRUE(run_until([&] { return rec.callbacks == 2; })) << "the hook threw; the callbacks still ran";
    EXPECT_EQ(names, (std::vector<std::string>{"MY.MODULE.CMD", "GET"}));
    EXPECT_EQ(client.stats().by_family[family(command_family::other)], 1u);
    EXPECT_EQ(client.stats().by_family[family(command_family::string)], 1u);
}

TEST_F(ClientStats, AConsumerCountsItsStream) {
    FakeRedis                   server;
    qb::redis::tcp::co_consumer consumer{server.uri(), 2};
    auto                        done = std::make_shared<bool>(false);
    consumer.connect([done](bool) { *done = true; }, consumer.uri());
    auto peer = server.accept();
    ASSERT_TRUE(peer);
    ASSERT_TRUE(run_until([&] { return *done; }));

    const auto message = [](std::string const &payload) {
        return "*3\r\n$7\r\nmessage\r\n$4\r\nchan\r\n" + bulk(payload);
    };
    peer->send(message("1") + message("2") + message("3") + message("4")); // capacity 2: two dropped
    ASSERT_TRUE(run_until([&] { return consumer.stats().messages_received == 4; }));
    EXPECT_EQ(consumer.stats().messages_dropped, 2u);
    EXPECT_EQ(consumer.stats().gaps, 0u) << "the marker waits for room";

    Drain d;
    qb::io::async::coro_scheduler().spawn(drain(&consumer, &d));
    ASSERT_TRUE(run_until([&] { return d.got == 2; }));
    peer->send(message("5"));
    ASSERT_TRUE(run_until([&] { return d.got == 4; })) << "the marker, then message 5";
    EXPECT_EQ(consumer.stats().gaps, 1u);
    EXPECT_EQ(consumer.stats().messages_received, 5u);
    consumer.disconnect();
    ASSERT_TRUE(run_until([&] { return d.ended; }));
}
