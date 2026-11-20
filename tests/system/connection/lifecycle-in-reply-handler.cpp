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
 * @file lifecycle-in-reply-handler.cpp
 * @brief A reply handler that ends the connection and begins another one before it returns.
 *
 * System tier: a loopback socket and NO external redis daemon. The test is the server.
 *
 * A reply handler runs inside the read loop's dispatch of the client's own link. `disconnect()` called there cannot
 * tear the link down under the handler: the teardown -- `on(disconnected)`, the failure of what was in flight -- runs
 * right after the handler returns. A lifecycle begun in the same handler must not install its link over the one whose
 * teardown is still owed:
 *   - `connect()` opens its socket asynchronously: its link is installed from the loop, after that teardown;
 *   - `setup_connection()` adopts an opened socket: it is handed the next link at once.
 * Either way the old link ends once -- its in-flight commands fail, `on_disconnected` fires -- and the new one comes
 * up, takes commands and answers them; nothing the old link's read had buffered reaches the new one. The consumers
 * share the connector: the messages one read brought behind the one whose callback ended the link are dropped too.
 * Outside any dispatch `setup_connection()` installs its link before it returns, as it always did.
 */

#include <gtest/gtest.h>
#include <chrono>
#include <memory>
#include <string>
#include <vector>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <qb/io/tcp/listener.h>
#include <qb/io/tcp/socket.h>
#include <qbm/redis/redis.h>

using namespace std::chrono_literals;

namespace lifecycle_in_reply_handler_test {

using Client = qb::redis::tcp::client;

template <typename Pred>
bool
run_until(Pred &&pred, std::chrono::milliseconds timeout = 3000ms) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!pred() && std::chrono::steady_clock::now() < deadline)
        qb::io::async::run(EVRUN_NOWAIT);
    return pred();
}

/// The server end of one connection: reads whole RESP commands (their verbs kept), writes raw replies.
struct Peer {
    qb::io::tcp::socket      sock;
    std::string              buffer;
    std::vector<std::string> verbs;

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
            std::string verb;
            for (int i = 0; i < argc; ++i) {
                line_end = buffer.find("\r\n", pos);
                if (line_end == std::string::npos)
                    return;
                const auto len = static_cast<std::size_t>(std::stoul(buffer.substr(pos + 1, line_end - pos - 1)));
                pos            = line_end + 2;
                if (buffer.size() < pos + len + 2)
                    return;
                if (i == 0)
                    verb = buffer.substr(pos, len);
                pos += len + 2;
            }
            buffer.erase(0, pos);
            verbs.push_back(std::move(verb));
        }
    }

    /// Pump the loop until `n` more commands arrived.
    bool
    expect(std::size_t n, std::chrono::milliseconds timeout = 3000ms) {
        const std::size_t want = verbs.size() + n;
        return run_until(
            [&] {
                char buf[4096];
                for (int r; (r = sock.read(buf, sizeof(buf))) > 0;)
                    buffer.append(buf, static_cast<std::size_t>(r));
                parse();
                return verbs.size() >= want;
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

/// A RESP3 push frame of a published message, as a subscribed consumer receives it.
std::string
push_message(std::string const &channel, std::string const &payload) {
    return ">3\r\n" + bulk("message") + bulk(channel) + bulk(payload);
}

/// What the client told its application, in order.
struct Told {
    std::vector<std::string> events;
    int                      downs = 0;

    void
    watch(Client &client) {
        client.on_disconnected([this](qb::io::down_reason cause) {
            ++downs;
            events.push_back(std::string{"disconnected:"} + std::string{qb::io::down_reason_name(cause)});
        });
    }
};

class LifecycleInReplyHandler : public ::testing::Test {
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

} // namespace lifecycle_in_reply_handler_test

using namespace lifecycle_in_reply_handler_test;

TEST_F(LifecycleInReplyHandler, DisconnectThenConnectFromAReplyHandlerEndsTheOldLinkThenBringsTheNewOneUp) {
    FakeRedis server;
    Client    client{server.uri()};
    Told      told;
    told.watch(client);
    auto first = connect(client, server);
    ASSERT_TRUE(first);

    int  replies = 0, failed_in_flight = 0;
    bool connected_again = false, connect_called = false;
    client.command<std::string>(
        [&](qb::redis::Reply<std::string> &&) {
            ++replies;
            client.disconnect();
            client.connect(
                [&](bool ok) {
                    connected_again = ok;
                    connect_called  = true;
                },
                client.uri());
        },
        "GET", "a");
    client.command<std::string>(
        [&](qb::redis::Reply<std::string> &&r) {
            if (!r.ok())
                ++failed_in_flight;
        },
        "GET", "b"); // in flight when the handler of the first disconnects: it fails with the link
    ASSERT_TRUE(first->expect(2));
    first->send(bulk("va"));

    auto second = server.accept();
    ASSERT_TRUE(second) << "the connect() made in the reply handler opened no connection";
    ASSERT_TRUE(run_until([&] { return connect_called; }));
    EXPECT_TRUE(connected_again);
    EXPECT_EQ(replies, 1);
    EXPECT_EQ(failed_in_flight, 1) << "what was in flight on the old link failed with it";
    EXPECT_EQ(told.downs, 1) << "the old link's end is told once";
    EXPECT_EQ(client.state(), qb::io::connection_state::ready);

    std::string answer;
    client.command<std::string>(
        [&](qb::redis::Reply<std::string> &&r) {
            if (r.ok())
                answer = r.result();
        },
        "GET", "c");
    ASSERT_TRUE(second->expect(1));
    second->send(bulk("vc"));
    ASSERT_TRUE(run_until([&] { return !answer.empty(); }));
    EXPECT_EQ(answer, "vc") << "the new link answers";
}

TEST_F(LifecycleInReplyHandler, DisconnectThenSetupConnectionFromAReplyHandlerEndsTheOldLinkThenBringsTheNewOneUp) {
    FakeRedis server;
    Client    client{server.uri()};
    Told      told;
    told.watch(client);
    auto first = connect(client, server);
    ASSERT_TRUE(first);

    int  replies = 0, failed_in_flight = 0, stray = 0;
    bool adopted = false, socket_opened = false;
    client.command<std::string>(
        [&](qb::redis::Reply<std::string> &&) {
            ++replies;
            client.disconnect();
            qb::io::tcp::socket fresh;
            socket_opened = fresh.connect_v4("127.0.0.1", server.port) == 0; // loopback: connected when it returns
            adopted       = client.setup_connection(server.uri(), std::move(fresh));
        },
        "GET", "a");
    client.command<std::string>(
        [&](qb::redis::Reply<std::string> &&r) {
            if (r.ok())
                ++stray; // the old link's second reply, read by the same read as the first, must not be delivered
            else
                ++failed_in_flight;
        },
        "GET", "b");
    ASSERT_TRUE(first->expect(2));
    first->send(bulk("va") + bulk("vb")); // both replies in one read: the slicing loop goes on after the handler

    auto second = server.accept();
    ASSERT_TRUE(second) << "the socket the handler opened was not accepted";
    ASSERT_TRUE(run_until([&] { return client.state() == qb::io::connection_state::ready; }))
        << "the adopted link never came up: state " << static_cast<int>(client.state());
    EXPECT_TRUE(socket_opened);
    EXPECT_TRUE(adopted);
    EXPECT_EQ(replies, 1);
    EXPECT_EQ(stray, 0) << "a reply of the link the handler ended was delivered after it";
    EXPECT_EQ(failed_in_flight, 1) << "what was in flight on the old link failed with it";
    EXPECT_EQ(told.downs, 1) << "the old link's end is told once";

    std::string answer;
    client.command<std::string>(
        [&](qb::redis::Reply<std::string> &&r) {
            if (r.ok())
                answer = r.result();
        },
        "GET", "c");
    ASSERT_TRUE(second->expect(1));
    EXPECT_EQ(second->verbs.back(), "GET") << "the new link carries the new command";
    second->send(bulk("vc"));
    ASSERT_TRUE(run_until([&] { return !answer.empty(); }));
    EXPECT_EQ(answer, "vc") << "the new link answers";
}

TEST_F(LifecycleInReplyHandler, AConnectMadeInAReplyHandlerThatThenRunsTheLoopWaitsForTheOldLinkToEnd) {
    // The handler ends the link, begins another lifecycle, then runs the loop itself -- a synchronous wait on some
    // other work. The loop it runs completes the new transport's connect while the old link's teardown is still
    // owed (it runs when this handler returns): the new link must not be installed under it.
    FakeRedis server;
    Client    client{server.uri()};
    Told      told;
    told.watch(client);
    auto first = connect(client, server);
    ASSERT_TRUE(first);

    int  replies = 0, stray = 0, failed_in_flight = 0;
    bool connected_again = false, connect_called = false, called_under_the_handler = false;
    client.command<std::string>(
        [&](qb::redis::Reply<std::string> &&) {
            ++replies;
            client.disconnect();
            client.connect(
                [&](bool ok) {
                    connected_again = ok;
                    connect_called  = true;
                },
                client.uri());
            run_until([&] { return connect_called; }, 300ms); // the handler waits, the loop turns under it
            called_under_the_handler = connect_called;
        },
        "GET", "a");
    client.command<std::string>(
        [&](qb::redis::Reply<std::string> &&r) {
            if (r.ok())
                ++stray;
            else
                ++failed_in_flight;
        },
        "GET", "b");
    ASSERT_TRUE(first->expect(2));
    first->send(bulk("va") + bulk("vb"));

    auto second = server.accept();
    ASSERT_TRUE(second) << "the connect() made in the reply handler opened no connection";
    ASSERT_TRUE(run_until([&] { return connect_called; })) << "state " << static_cast<int>(client.state());
    EXPECT_FALSE(called_under_the_handler) << "the new link came up under the handler of the link it replaces";
    EXPECT_TRUE(connected_again);
    EXPECT_EQ(replies, 1);
    EXPECT_EQ(stray, 0) << "a reply of the link the handler ended was delivered after it";
    EXPECT_EQ(failed_in_flight, 1) << "what was in flight on the old link failed with it";
    EXPECT_EQ(told.downs, 1) << "the old link's end is told once";
    EXPECT_EQ(client.state(), qb::io::connection_state::ready);

    std::string answer;
    client.command<std::string>(
        [&](qb::redis::Reply<std::string> &&r) {
            if (r.ok())
                answer = r.result();
        },
        "GET", "c");
    ASSERT_TRUE(second->expect(1));
    second->send(bulk("vc"));
    ASSERT_TRUE(run_until([&] { return !answer.empty(); }));
    EXPECT_EQ(answer, "vc") << "the new link answers";
}

TEST_F(LifecycleInReplyHandler, ASetupConnectionMadeInAReplyHandlerThatThenRunsTheLoopWaitsForTheOldLinkToEnd) {
    // As above with an adopted socket: the adoption runs at the scheduler's next drain, which the handler's own loop
    // reaches while the old link's teardown is still owed.
    FakeRedis server;
    Client    client{server.uri()};
    Told      told;
    told.watch(client);
    auto first = connect(client, server);
    ASSERT_TRUE(first);

    int  replies = 0, stray = 0, failed_in_flight = 0;
    bool adopted = false, ready_under_the_handler = false;
    client.command<std::string>(
        [&](qb::redis::Reply<std::string> &&) {
            ++replies;
            client.disconnect();
            qb::io::tcp::socket fresh;
            ASSERT_EQ(fresh.connect_v4("127.0.0.1", server.port), 0);
            adopted = client.setup_connection(server.uri(), std::move(fresh));
            run_until([&] { return client.state() == qb::io::connection_state::ready; }, 300ms);
            ready_under_the_handler = client.state() == qb::io::connection_state::ready;
        },
        "GET", "a");
    client.command<std::string>(
        [&](qb::redis::Reply<std::string> &&r) {
            if (r.ok())
                ++stray;
            else
                ++failed_in_flight;
        },
        "GET", "b");
    ASSERT_TRUE(first->expect(2));
    first->send(bulk("va") + bulk("vb"));

    auto second = server.accept();
    ASSERT_TRUE(second) << "the socket the handler opened was not accepted";
    ASSERT_TRUE(run_until([&] { return client.state() == qb::io::connection_state::ready; }))
        << "the adopted link never came up: state " << static_cast<int>(client.state());
    EXPECT_TRUE(adopted);
    EXPECT_FALSE(ready_under_the_handler) << "the adopted link came up under the handler of the link it replaces";
    EXPECT_EQ(replies, 1);
    EXPECT_EQ(stray, 0) << "a reply of the link the handler ended was delivered after it";
    EXPECT_EQ(failed_in_flight, 1) << "what was in flight on the old link failed with it";
    EXPECT_EQ(told.downs, 1) << "the old link's end is told once";

    std::string answer;
    client.command<std::string>(
        [&](qb::redis::Reply<std::string> &&r) {
            if (r.ok())
                answer = r.result();
        },
        "GET", "c");
    ASSERT_TRUE(second->expect(1));
    second->send(bulk("vc"));
    ASSERT_TRUE(run_until([&] { return !answer.empty(); }));
    EXPECT_EQ(answer, "vc") << "the new link answers";
}

TEST_F(LifecycleInReplyHandler, AConnectMadeWhenTheLinkIsLostThatThenRunsTheLoopWaitsForTheTeardownToEnd) {
    // The peer closes. The command in flight fails from inside the link's teardown, and its callback connects again
    // and runs the loop: the new transport connects while that teardown is still running -- the watcher of the old
    // link is stopped, and its transport ended, only after the callback returns.
    FakeRedis server;
    Client    client{server.uri()};
    Told      told;
    told.watch(client);
    auto first = connect(client, server);
    ASSERT_TRUE(first);

    int  failures        = 0;
    bool connected_again = false, connect_called = false, called_under_the_teardown = false;
    client.command<std::string>(
        [&](qb::redis::Reply<std::string> &&r) {
            if (r.ok())
                return;
            ++failures;
            client.connect(
                [&](bool ok) {
                    connected_again = ok;
                    connect_called  = true;
                },
                client.uri());
            run_until([&] { return connect_called; }, 300ms);
            called_under_the_teardown = connect_called;
        },
        "GET", "a");
    ASSERT_TRUE(first->expect(1));
    first->sock.disconnect(); // the link is lost with the command in flight

    auto second = server.accept();
    ASSERT_TRUE(second) << "the connect() made in the failure callback opened no connection";
    ASSERT_TRUE(run_until([&] { return connect_called; })) << "state " << static_cast<int>(client.state());
    EXPECT_FALSE(called_under_the_teardown) << "the new link came up inside the teardown of the link it replaces";
    EXPECT_TRUE(connected_again);
    EXPECT_EQ(failures, 1);
    EXPECT_EQ(told.downs, 1) << "the lost link's end is told once";
    EXPECT_EQ(client.state(), qb::io::connection_state::ready);

    std::string answer;
    client.command<std::string>(
        [&](qb::redis::Reply<std::string> &&r) {
            if (r.ok())
                answer = r.result();
        },
        "GET", "c");
    ASSERT_TRUE(second->expect(1)) << "the new link carries nothing: it was ended with the old one";
    second->send(bulk("vc"));
    ASSERT_TRUE(run_until([&] { return !answer.empty(); }));
    EXPECT_EQ(answer, "vc") << "the new link answers";
}

TEST_F(LifecycleInReplyHandler, SetupConnectionOutsideAnyDispatchInstallsItsLinkBeforeItReturns) {
    // The ordinary caller: nothing of a previous link is owed, and the adopted socket is the client's transport when
    // the call returns, as it always was -- what the caller reads of the client then is the new link's.
    FakeRedis           server;
    Client              client{server.uri()};
    qb::io::tcp::socket fresh;
    ASSERT_EQ(fresh.connect_v4("127.0.0.1", server.port), 0);
    ASSERT_TRUE(client.setup_connection(server.uri(), std::move(fresh)));
    EXPECT_EQ(client.state(), qb::io::connection_state::handshaking) << "the link is not installed when setup_connection() returns";
    EXPECT_TRUE(client.transport().is_open()) << "the adopted socket is not the client's transport when setup_connection() returns";

    auto peer = server.accept();
    ASSERT_TRUE(peer);
    ASSERT_TRUE(run_until([&] { return client.state() == qb::io::connection_state::ready; }));
    std::string answer;
    client.command<std::string>(
        [&](qb::redis::Reply<std::string> &&r) {
            if (r.ok())
                answer = r.result();
        },
        "GET", "k");
    ASSERT_TRUE(peer->expect(1));
    peer->send(bulk("v"));
    ASSERT_TRUE(run_until([&] { return !answer.empty(); }));
    EXPECT_EQ(answer, "v");
}

TEST_F(LifecycleInReplyHandler, ADisconnectMadeWhileTheNewLinkWaitsSupersedesIt) {
    // The handler ends its link, begins another lifecycle, runs the loop -- the new transport connects and waits for
    // the old link's teardown -- and then changes its mind: the waiting attempt installs nothing and gives its
    // socket up.
    FakeRedis server;
    Client    client{server.uri()};
    Told      told;
    told.watch(client);
    auto first = connect(client, server);
    ASSERT_TRUE(first);

    bool connect_called = false, connected_again = true, waited_under_the_handler = false;
    client.command<std::string>(
        [&](qb::redis::Reply<std::string> &&) {
            client.disconnect();
            client.connect(
                [&](bool ok) {
                    connected_again = ok;
                    connect_called  = true;
                },
                client.uri());
            run_until([&] { return connect_called; }, 300ms); // the new transport connects; its installation waits
            waited_under_the_handler = !connect_called;
            client.disconnect(); // and the caller changes its mind before the handler returns
        },
        "GET", "a");
    ASSERT_TRUE(first->expect(1));
    first->send(bulk("va"));

    auto second = server.accept();
    ASSERT_TRUE(second) << "the connect() made in the handler opened no connection";
    ASSERT_TRUE(run_until([&] { return connect_called; })) << "the superseded connect() never completed";
    EXPECT_TRUE(waited_under_the_handler);
    EXPECT_FALSE(connected_again) << "a connect() superseded while its link waited reported success";
    EXPECT_EQ(client.state(), qb::io::connection_state::down);
    EXPECT_EQ(told.downs, 1) << "the old link's end is told once";
    EXPECT_TRUE(run_until([&] {
        char octet = 0; // the accepted end is non-blocking: 0 is the end of the stream, an error is "nothing yet"
        return ::recv(second->sock.native_handle(), &octet, 1, 0) == 0;
    })) << "the socket of the superseded attempt was not closed";
}

TEST_F(LifecycleInReplyHandler, AConsumersMessageCallbackThatDisconnectsIsNotHandedTheRestOfItsRead) {
    // Two messages in one read: the callback of the first ends the link. The second is the dead link's.
    FakeRedis                                    server;
    int                                          seen = 0;
    std::string                                  after_disconnect;
    std::unique_ptr<qb::redis::tcp::cb_consumer> consumer;
    consumer  = std::make_unique<qb::redis::tcp::cb_consumer>(server.uri(), [&](qb::redis::message &&m) {
        if (++seen == 1) {
            consumer->disconnect();
            return;
        }
        after_disconnect += m.payload;
    });
    auto done = std::make_shared<bool>(false);
    consumer->connect([done](bool) { *done = true; }, consumer->uri());
    auto peer = server.accept();
    ASSERT_TRUE(peer);
    ASSERT_TRUE(run_until([&] { return *done; }));

    peer->send(push_message("news", "first") + push_message("news", "second"));
    ASSERT_TRUE(run_until([&] { return seen >= 1 && consumer->state() == qb::io::connection_state::down; }));
    run_until([] { return false; }, 100ms); // anything still due is dispatched
    EXPECT_EQ(seen, 1) << "a message of the link the callback had just ended was delivered after its disconnect(): '" << after_disconnect
                       << "'";
    consumer.reset();
}

TEST_F(LifecycleInReplyHandler, AConsumersMessageCallbackThatDisconnectsThenConnectsIsNotHandedTheOldLinksMessages) {
    FakeRedis                                    server;
    int                                          seen            = 0;
    bool                                         connected_again = false;
    std::string                                  after_reconnect;
    std::unique_ptr<qb::redis::tcp::cb_consumer> consumer;
    consumer  = std::make_unique<qb::redis::tcp::cb_consumer>(server.uri(), [&](qb::redis::message &&m) {
        if (m.is_gap())
            return; // the marker of the gap between the two links: not a message of either
        if (++seen == 1) {
            consumer->disconnect();
            consumer->connect([&](bool ok) { connected_again = ok; }, consumer->uri());
            return;
        }
        after_reconnect += m.payload;
    });
    auto done = std::make_shared<bool>(false);
    consumer->connect([done](bool) { *done = true; }, consumer->uri());
    auto first = server.accept();
    ASSERT_TRUE(first);
    ASSERT_TRUE(run_until([&] { return *done; }));

    first->send(push_message("news", "first") + push_message("news", "second"));
    auto second = server.accept();
    ASSERT_TRUE(second) << "the connect() made in the callback opened no connection";
    ASSERT_TRUE(run_until([&] { return connected_again; }));
    EXPECT_EQ(seen, 1) << "the ended link's next message reached the consumer of the new lifecycle: '" << after_reconnect << "'";

    second->send(push_message("news", "third"));
    ASSERT_TRUE(run_until([&] { return seen >= 2; }));
    EXPECT_EQ(after_reconnect, "third") << "the new link's messages are delivered";
    consumer.reset();
}

TEST_F(LifecycleInReplyHandler, ADisconnectMadeByTheHookOfALostLinkStartsNoRunWhateverPolicyThatHookArms) {
    // disconnect() is terminal: it starts no reconnection run, whatever the policy. On a client the loss had already
    // left down -- no policy armed when the link was lost -- it did nothing at all: a policy the same hook armed then
    // started a run, and the client came back by itself.
    FakeRedis server;
    Client    client{server.uri()};
    auto      first = connect(client, server);
    ASSERT_TRUE(first);

    int                              hooks = 0;
    std::vector<qb::io::down_reason> downs;
    client.on_down([&](qb::io::down_reason reason) { downs.push_back(reason); });
    client.on_disconnected([&](qb::io::down_reason) {
        ++hooks;
        client.enable_auto_reconnect(
            qb::redis::RetryPolicy{}.with_max_attempts(-1).with_initial_delay(10ms).with_max_delay(20ms).with_jitter(false));
        client.disconnect();
    });
    first->sock.disconnect(); // the peer closes: the link is lost with no policy armed

    ASSERT_TRUE(run_until([&] { return hooks == 1; }));
    EXPECT_FALSE(server.accept(300ms)) << "a reconnection run dialed after an explicit disconnect()";
    EXPECT_EQ(client.state(), qb::io::connection_state::down);
    EXPECT_FALSE(client.is_reconnecting());
    ASSERT_EQ(downs.size(), 1u) << "down is told once";
    EXPECT_EQ(downs.front(), qb::io::down_reason::peer) << "down as the loss left it";

    // The next connect() is a new lifecycle: the mark does not outlive it.
    auto second = connect(client, server);
    ASSERT_TRUE(second);
    EXPECT_EQ(client.state(), qb::io::connection_state::ready);
    hooks = 0;
    client.on_disconnected([&](qb::io::down_reason) { ++hooks; });
    second->sock.disconnect();
    ASSERT_TRUE(run_until([&] { return hooks == 1; }));
    EXPECT_TRUE(server.accept()) << "the policy armed earlier recovers the next loss: nothing was left of the disconnect()";
}

TEST_F(LifecycleInReplyHandler, APolicyArmedByTheHookOfALostLinkStartsARun) {
    // The other half: a hook that arms a policy, and does not disconnect, has its run.
    FakeRedis server;
    Client    client{server.uri()};
    auto      first = connect(client, server);
    ASSERT_TRUE(first);

    int hooks = 0;
    client.on_disconnected([&](qb::io::down_reason) {
        ++hooks;
        client.enable_auto_reconnect(
            qb::redis::RetryPolicy{}.with_max_attempts(-1).with_initial_delay(10ms).with_max_delay(20ms).with_jitter(false));
    });
    first->sock.disconnect();

    ASSERT_TRUE(run_until([&] { return hooks == 1; }));
    auto second = server.accept();
    ASSERT_TRUE(second) << "the policy the hook armed started no run";
    ASSERT_TRUE(run_until([&] { return client.state() == qb::io::connection_state::ready; }));
}

TEST_F(LifecycleInReplyHandler, ADisconnectMadeByAFailureCallbackOfALostLinkStartsNoRunWhateverPolicyTheHookArms) {
    // The same from the failure callback of a command the lost link carried, the hook arming the policy after it:
    // the loss is still deciding when the callback disconnects.
    FakeRedis server;
    Client    client{server.uri()};
    auto      first = connect(client, server);
    ASSERT_TRUE(first);

    int                              failures = 0, hooks = 0;
    std::vector<qb::io::down_reason> downs;
    client.on_down([&](qb::io::down_reason reason) { downs.push_back(reason); });
    client.on_disconnected([&](qb::io::down_reason) {
        ++hooks;
        client.enable_auto_reconnect(
            qb::redis::RetryPolicy{}.with_max_attempts(-1).with_initial_delay(10ms).with_max_delay(20ms).with_jitter(false));
    });
    client.command<std::string>(
        [&](qb::redis::Reply<std::string> &&r) {
            if (r.ok())
                return;
            ++failures;
            client.disconnect();
        },
        "GET", "a");
    ASSERT_TRUE(first->expect(1));
    first->sock.disconnect(); // the link is lost with the command in flight, no policy armed

    ASSERT_TRUE(run_until([&] { return hooks == 1; }));
    EXPECT_EQ(failures, 1);
    EXPECT_FALSE(server.accept(300ms)) << "a policy the hook armed started a run after the failure callback's disconnect()";
    EXPECT_EQ(client.state(), qb::io::connection_state::down);
    ASSERT_EQ(downs.size(), 1u) << "down is told once";
    EXPECT_EQ(downs.front(), qb::io::down_reason::peer) << "down as the loss left it";
}
