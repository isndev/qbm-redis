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
 * @file reconnect-contract.cpp
 * @brief The client's side of the reconnect contract (`qb/io/reconnect.h`, Huly QB-126a): R1-R4 and R8.
 *
 * System tier: in-process plumbing over a loopback socket, NO external redis daemon. The test IS the
 * server: it accepts the client's connections, reads every command off the wire and answers what each case
 * scripts -- so ORDER is observed on the wire, not inferred from a reply, and a refusal is whatever the case
 * writes back.
 *
 * What is proved, each case failing on the code before 126a (or, for the API the contract adds, written
 * against it):
 *   - R1: an explicit disconnect() is terminal even with a reconnection policy armed; a peer drop with one
 *     armed reconnects; the command watchdog's own drop stays recoverable;
 *   - R2: connect() resolves only once the handshake ran (AUTH / HELLO 3 / SELECT / the hook), on the first
 *     connection too; work submitted meanwhile is HELD and leaves after the handshake, never ahead of AUTH;
 *     the hook writes through a handle no other coroutine can use, and a leaked handle fails with
 *     "handshake over";
 *   - R3: a handshake the server refuses (WRONGPASS) is terminal, down with handshake_refused; a transient
 *     refusal (LOADING), a link lost during the handshake and a handshake past its deadline are one more
 *     attempt;
 *   - R4: nothing waits for ever -- held work fails at the terminal state with its reason, or at the command
 *     timeout; work submitted when no run is armed fails at once;
 *   - R8: state() walks idle -> connecting -> handshaking -> ready -> reconnecting -> ... -> down, and
 *     on_connected / on_disconnected strictly alternate, on_down closing the sequence;
 *   - QB-1000 (the redis part): connect_awaiter is neither copyable nor movable.
 */

#include <gtest/gtest.h>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <qb/io/tcp/listener.h>
#include <qb/io/tcp/socket.h>
#include <qbm/redis/redis.h>

using namespace std::chrono_literals;

namespace reconnect_contract_test {

using Client  = qb::redis::tcp::client;
using Command = std::vector<std::string>;

static_assert(!std::is_copy_constructible_v<Client::connect_awaiter> && !std::is_move_constructible_v<Client::connect_awaiter>,
              "a connect awaiter's completion is its own: a copy would settle the original's (QB-1000)");
static_assert(!std::is_copy_assignable_v<Client::connect_awaiter> && !std::is_move_assignable_v<Client::connect_awaiter>);

/// Pump the loop -- watchers and ready coroutines -- until `pred()` holds or `timeout` elapses.
template <typename Pred>
bool
run_until(Pred &&pred, std::chrono::milliseconds timeout = 3000ms) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!pred() && std::chrono::steady_clock::now() < deadline)
        qb::io::async::run(EVRUN_NOWAIT);
    return pred();
}

/// Pump the loop for `d` (a settle: the bytes written so far cross, ready coroutines run).
void
settle(std::chrono::milliseconds d) {
    run_until([] { return false; }, d);
}

/// The server end of one of the client's connections: reads whole RESP commands, writes raw replies.
struct Peer {
    qb::io::tcp::socket sock;
    std::string         buffer;

    /// Parse every complete `*N` command buffered so far.
    std::vector<Command>
    parse() {
        std::vector<Command> out;
        for (;;) {
            std::size_t pos = 0;
            if (buffer.empty() || buffer[0] != '*')
                return out;
            auto line_end = buffer.find("\r\n", pos);
            if (line_end == std::string::npos)
                return out;
            const int argc = std::stoi(buffer.substr(1, line_end - 1));
            pos            = line_end + 2;
            Command cmd;
            bool    complete = true;
            for (int i = 0; i < argc; ++i) {
                line_end = buffer.find("\r\n", pos);
                if (line_end == std::string::npos) {
                    complete = false;
                    break;
                }
                const auto len = static_cast<std::size_t>(std::stoul(buffer.substr(pos + 1, line_end - pos - 1)));
                pos            = line_end + 2;
                if (buffer.size() < pos + len + 2) {
                    complete = false;
                    break;
                }
                cmd.push_back(buffer.substr(pos, len));
                pos += len + 2;
            }
            if (!complete)
                return out;
            buffer.erase(0, pos);
            out.push_back(std::move(cmd));
        }
    }

    /// Read what arrived, without blocking.
    void
    pull() {
        char buf[4096];
        for (int r; (r = sock.read(buf, sizeof(buf))) > 0;)
            buffer.append(buf, static_cast<std::size_t>(r));
    }

    /// Pump the client's loop until `n` commands arrived (or the deadline); returns those read.
    std::vector<Command>
    expect(std::size_t n, std::chrono::milliseconds timeout = 3000ms) {
        std::vector<Command> got;
        run_until(
            [&] {
                pull();
                for (auto &c : parse())
                    got.push_back(std::move(c));
                return got.size() >= n;
            },
            timeout);
        return got;
    }

    void
    send(std::string const &raw) {
        ASSERT_EQ(sock.write(raw.data(), raw.size()), static_cast<int>(raw.size()));
    }
};

/// The server: a non-blocking loopback listener; each accepted socket is one of the client's connections.
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
    /// The next connection the client opens, pumping the loop while it comes (nullptr past the deadline).
    std::unique_ptr<Peer>
    accept(std::chrono::milliseconds timeout = 3000ms) {
        auto peer     = std::make_unique<Peer>();
        bool accepted = false; // run_until re-evaluates its predicate: accept once, then remember it
        if (!run_until([&] { return accepted || (accepted = listener.accept(peer->sock) == qb::io::SocketStatus::Done); }, timeout))
            return nullptr;
        peer->sock.set_nonblocking(true);
        return peer;
    }
    /// Whether a connection is waiting in the backlog right now (after `settle_for` of loop passes).
    bool
    has_pending_connection(std::chrono::milliseconds settle_for) {
        settle(settle_for);
        qb::io::tcp::socket probe;
        return listener.accept(probe) == qb::io::SocketStatus::Done;
    }
};

/// An endpoint nothing listens on: a port bound, then released (its listener destroyed).
qb::io::uri
dead_uri() {
    std::uint16_t port = 0;
    {
        qb::io::tcp::listener probe;
        EXPECT_EQ(probe.listen_v4(0, "127.0.0.1"), qb::io::SocketStatus::Done);
        port = probe.local_endpoint().port();
    }
    return qb::io::uri{"tcp://127.0.0.1:" + std::to_string(port)};
}

std::string
bulk(std::string const &s) {
    return "$" + std::to_string(s.size()) + "\r\n" + s + "\r\n";
}

const std::string kOk = "+OK\r\n";

/// Records the R8 notifications in order.
struct Journal {
    std::vector<std::string> events;
    void
    watch(Client &client) {
        client.on_connected([this] { events.push_back("connected"); });
        client.on_disconnected(
            [this](qb::io::down_reason r) { events.push_back(std::string{"disconnected:"} + std::string{qb::io::down_reason_name(r)}); });
        client.on_down([this](qb::io::down_reason r) { events.push_back(std::string{"down:"} + std::string{qb::io::down_reason_name(r)}); });
    }
};

/// A callback command whose outcome the case reads back.
struct Outcome {
    int         calls = 0;
    bool        ok    = false;
    std::string value;
    std::string error;
    auto
    sink() {
        return [this](qb::redis::Reply<std::string> &&r) {
            ++calls;
            ok = r.ok();
            if (r.ok())
                value = r.result();
            else
                error = r.error();
        };
    }
};

class ReconnectContract : public ::testing::Test {
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

    /// connect() through the callback form; returns where the verdict lands.
    static std::shared_ptr<std::optional<bool>>
    start_connect(Client &client, qb::duration timeout = 3s) {
        auto verdict = std::make_shared<std::optional<bool>>();
        client.connect([verdict](bool ok) { *verdict = ok; }, client.uri(), timeout);
        return verdict;
    }
};

} // namespace reconnect_contract_test

using namespace reconnect_contract_test;

// ---------------------------------------------------------------------------------------------- R2

TEST_F(ReconnectContract, ConnectResolvesOnlyOnceTheHandshakeRan) {
    FakeRedis server;
    Client    client{server.uri()};
    Journal   journal;
    journal.watch(client);
    client.with_auth("app", "s3cret").with_select(2);
    EXPECT_EQ(client.state(), qb::io::connection_state::idle);

    auto verdict = start_connect(client);
    EXPECT_EQ(client.state(), qb::io::connection_state::connecting);
    auto peer = server.accept();
    ASSERT_TRUE(peer);
    auto first = peer->expect(1);
    ASSERT_EQ(first.size(), 1u);
    EXPECT_EQ(first[0], (Command{"AUTH", "app", "s3cret"})) << "the handshake writes first, on the first connection too";
    EXPECT_EQ(client.state(), qb::io::connection_state::handshaking);
    EXPECT_FALSE(client.is_connected()) << "is_connected() means ready, and the handshake has not answered";
    EXPECT_FALSE(verdict->has_value()) << "connect() must not resolve before the handshake";

    peer->send(kOk);
    auto second = peer->expect(1);
    ASSERT_EQ(second.size(), 1u);
    EXPECT_EQ(second[0], (Command{"SELECT", "2"}));
    EXPECT_FALSE(verdict->has_value());
    peer->send(kOk);
    ASSERT_TRUE(run_until([&] { return verdict->has_value(); }));
    EXPECT_TRUE(**verdict);
    EXPECT_EQ(client.state(), qb::io::connection_state::ready);
    EXPECT_TRUE(client.is_connected());
    EXPECT_EQ(journal.events, (std::vector<std::string>{"connected"}));
}

TEST_F(ReconnectContract, Resp3CarriesTheCredentialsInHello) {
    FakeRedis server;
    Client    client{server.uri()};
    client.with_resp3().with_auth("s3cret");
    auto verdict = start_connect(client);
    auto peer    = server.accept();
    ASSERT_TRUE(peer);
    auto hello = peer->expect(1);
    ASSERT_EQ(hello.size(), 1u);
    EXPECT_EQ(hello[0], (Command{"HELLO", "3", "AUTH", "default", "s3cret"}));
    peer->send("%1\r\n" + bulk("server") + bulk("redis"));
    ASSERT_TRUE(run_until([&] { return verdict->has_value(); }));
    EXPECT_TRUE(**verdict);
}

TEST_F(ReconnectContract, WorkSubmittedBeforeReadyIsHeldBehindTheHandshake) {
    FakeRedis server;
    Client    client{server.uri()};
    client.with_auth("s3cret");
    auto    verdict = start_connect(client);
    Outcome held;
    client.command<std::string>(held.sink(), "ECHO", "held"); // connecting: held
    EXPECT_EQ(client.pending_reply_count(), 1u) << "a held command awaits its reply too";

    auto peer = server.accept();
    ASSERT_TRUE(peer);
    auto first = peer->expect(1);
    ASSERT_EQ(first.size(), 1u);
    EXPECT_EQ(first[0], (Command{"AUTH", "s3cret"}));
    // Nothing else may cross before AUTH is answered: the held ECHO waits behind the handshake.
    EXPECT_TRUE(peer->expect(1, 150ms).empty()) << "a held command left ahead of the handshake's answer";

    peer->send(kOk);
    auto after = peer->expect(1);
    ASSERT_EQ(after.size(), 1u);
    EXPECT_EQ(after[0], (Command{"ECHO", "held"}));
    peer->send(bulk("held"));
    ASSERT_TRUE(run_until([&] { return held.calls == 1; }));
    EXPECT_TRUE(held.ok);
    EXPECT_EQ(held.value, "held");
    EXPECT_TRUE(verdict->value_or(false));
}

TEST_F(ReconnectContract, TheHookWritesThroughItsOwnHandleAndALeakedHandleFails) {
    FakeRedis server;
    Client    client{server.uri()};

    std::optional<Client::handshake_client> leaked;
    bool                                    hook_ran = false;
    client.set_handshake([&](Client::handshake_client &hs) -> qb::io::async::task<bool> {
        hook_ran = true;
        leaked.emplace(hs); // a copy kept past the hook: it must not write once the hook is over
        auto r = co_await hs.command<std::string>("CLIENT", "SETNAME", "lot-r");
        co_return r.ok();
    });

    auto verdict = start_connect(client);
    auto peer    = server.accept();
    ASSERT_TRUE(peer);
    auto setname = peer->expect(1);
    ASSERT_EQ(setname.size(), 1u);
    EXPECT_EQ(setname[0], (Command{"CLIENT", "SETNAME", "lot-r"})) << "the hook runs on the first connection too";

    // While the hook is suspended, another coroutine's command is held: it does not slip past the gate.
    Outcome other;
    client.command<std::string>(other.sink(), "PING");
    EXPECT_TRUE(peer->expect(1, 150ms).empty()) << "a command slipped past the gate while the hook was suspended";

    peer->send(kOk);
    auto ping = peer->expect(1);
    ASSERT_EQ(ping.size(), 1u);
    EXPECT_EQ(ping[0], (Command{"PING"}));
    peer->send("+PONG\r\n");
    ASSERT_TRUE(run_until([&] { return other.calls == 1 && verdict->has_value(); }));
    EXPECT_TRUE(hook_ran);
    EXPECT_TRUE(**verdict);

    ASSERT_TRUE(leaked.has_value());
    EXPECT_FALSE(leaked->live());
    Outcome late;
    leaked->command<std::string>(late.sink(), "PING");
    EXPECT_EQ(late.calls, 1);
    EXPECT_FALSE(late.ok);
    EXPECT_EQ(late.error, "handshake over");
    EXPECT_TRUE(peer->expect(1, 100ms).empty()) << "a leaked handshake handle wrote on the ready connection";
}

// ---------------------------------------------------------------------------------------------- R3

TEST_F(ReconnectContract, ARefusedHandshakeIsTerminal) {
    FakeRedis server;
    Client    client{server.uri()};
    Journal   journal;
    journal.watch(client);
    client.with_auth("app", "wrong");
    client.enable_auto_reconnect(qb::redis::RetryPolicy{}.with_initial_delay(1ms).with_jitter(false));

    bool done = false, ok = true;
    qb::io::async::coro_scheduler().spawn([&]() -> qb::io::async::task<void> {
        ok   = co_await client.connect_with_retry(qb::redis::RetryPolicy{}.with_max_attempts(5).with_initial_delay(1ms).with_jitter(false));
        done = true;
    });
    // The spawned task starts the connection on the scheduler's next drain: wait for it, then submit.
    ASSERT_TRUE(run_until([&] { return client.state() != qb::io::connection_state::idle; }));
    Outcome held;
    client.command<std::string>(held.sink(), "PING");
    auto peer = server.accept();
    ASSERT_TRUE(peer);
    ASSERT_EQ(peer->expect(1).size(), 1u);
    peer->send("-WRONGPASS invalid username-password pair or user is disabled.\r\n");

    ASSERT_TRUE(run_until([&] { return done && held.calls == 1; }));
    EXPECT_FALSE(ok);
    EXPECT_EQ(client.state(), qb::io::connection_state::down);
    EXPECT_EQ(client.down_reason(), qb::io::down_reason::handshake_refused);
    EXPECT_FALSE(client.is_reconnecting());
    EXPECT_FALSE(held.ok);
    EXPECT_EQ(held.error.rfind("handshake refused: WRONGPASS", 0), 0u) << held.error;
    EXPECT_FALSE(server.has_pending_connection(100ms)) << "a refused handshake is not retried";
    EXPECT_EQ(journal.events, (std::vector<std::string>{"down:handshake_refused"}));
}

TEST_F(ReconnectContract, ATransientRefusalIsOneMoreAttempt) {
    FakeRedis server;
    Client    client{server.uri()};
    client.with_auth("s3cret");
    bool done = false, ok = false;
    qb::io::async::coro_scheduler().spawn([&]() -> qb::io::async::task<void> {
        ok   = co_await client.connect_with_retry(qb::redis::RetryPolicy{}.with_max_attempts(3).with_initial_delay(1ms).with_jitter(false));
        done = true;
    });
    auto first = server.accept();
    ASSERT_TRUE(first);
    ASSERT_EQ(first->expect(1).size(), 1u);
    first->send("-LOADING Redis is loading the dataset in memory\r\n");

    auto second = server.accept();
    ASSERT_TRUE(second) << "a transient refusal must be one more attempt, on a new connection";
    auto auth = second->expect(1);
    ASSERT_EQ(auth.size(), 1u);
    EXPECT_EQ(auth[0], (Command{"AUTH", "s3cret"}));
    second->send(kOk);
    ASSERT_TRUE(run_until([&] { return done; }));
    EXPECT_TRUE(ok);
    EXPECT_EQ(client.state(), qb::io::connection_state::ready);
}

TEST_F(ReconnectContract, ALinkLostDuringTheHandshakeIsOneMoreAttempt) {
    FakeRedis server;
    Client    client{server.uri()};
    client.with_auth("s3cret");
    bool done = false, ok = false;
    qb::io::async::coro_scheduler().spawn([&]() -> qb::io::async::task<void> {
        ok   = co_await client.connect_with_retry(qb::redis::RetryPolicy{}.with_max_attempts(3).with_initial_delay(1ms).with_jitter(false));
        done = true;
    });
    auto first = server.accept();
    ASSERT_TRUE(first);
    ASSERT_EQ(first->expect(1).size(), 1u);
    first->sock.disconnect(); // the server goes away mid-handshake

    auto second = server.accept();
    ASSERT_TRUE(second);
    ASSERT_EQ(second->expect(1).size(), 1u);
    second->send(kOk);
    ASSERT_TRUE(run_until([&] { return done; }));
    EXPECT_TRUE(ok);
}

TEST_F(ReconnectContract, AHandshakePastItsDeadlineFailsTheConnect) {
    FakeRedis server;
    Client    client{server.uri()};
    client.with_auth("s3cret");
    auto verdict = start_connect(client, 200ms);
    auto peer    = server.accept();
    ASSERT_TRUE(peer);
    ASSERT_EQ(peer->expect(1).size(), 1u); // AUTH, never answered
    ASSERT_TRUE(run_until([&] { return verdict->has_value(); }, 3000ms)) << "a handshake that never answers must not hang connect()";
    EXPECT_FALSE(**verdict);
    EXPECT_EQ(client.state(), qb::io::connection_state::down);
    EXPECT_EQ(client.down_reason(), qb::io::down_reason::peer);
}

// ---------------------------------------------------------------------------------------------- R1

TEST_F(ReconnectContract, DisconnectIsTerminalEvenWithAPolicyArmed) {
    FakeRedis server;
    Client    client{server.uri()};
    Journal   journal;
    journal.watch(client);
    client.enable_auto_reconnect(qb::redis::RetryPolicy{}.with_initial_delay(1ms).with_jitter(false));
    auto verdict = start_connect(client);
    auto peer    = server.accept();
    ASSERT_TRUE(peer);
    ASSERT_TRUE(run_until([&] { return verdict->has_value(); }));

    client.disconnect();
    EXPECT_EQ(client.state(), qb::io::connection_state::down);
    EXPECT_EQ(client.down_reason(), qb::io::down_reason::user);
    EXPECT_FALSE(client.is_reconnecting()) << "an explicit disconnect() starts no run";
    EXPECT_FALSE(server.has_pending_connection(150ms)) << "an explicit disconnect() must not reconnect";
    EXPECT_EQ(journal.events, (std::vector<std::string>{"connected", "disconnected:user", "down:user"}));

    // Work submitted now fails, on the next loop pass -- it never waits for a connection nobody will open.
    Outcome after;
    client.command<std::string>(after.sink(), "PING");
    EXPECT_EQ(after.calls, 0) << "failed from the loop, never inside the call";
    ASSERT_TRUE(run_until([&] { return after.calls == 1; }));
    EXPECT_EQ(after.error, "disconnected");
}

TEST_F(ReconnectContract, APeerDropWithAPolicyReconnectsAndReleasesHeldWorkAfterTheHandshake) {
    FakeRedis server;
    Client    client{server.uri()};
    Journal   journal;
    journal.watch(client);
    client.with_auth("s3cret");
    client.enable_auto_reconnect(qb::redis::RetryPolicy{}.with_initial_delay(50ms).with_jitter(false));
    auto verdict = start_connect(client);
    auto first   = server.accept();
    ASSERT_TRUE(first);
    ASSERT_EQ(first->expect(1).size(), 1u);
    first->send(kOk);
    ASSERT_TRUE(run_until([&] { return verdict->has_value(); }));

    Outcome inflight;
    client.command<std::string>(inflight.sink(), "ECHO", "inflight");
    ASSERT_EQ(first->expect(1).size(), 1u);
    first->sock.disconnect(); // the peer drops
    ASSERT_TRUE(run_until([&] { return inflight.calls == 1; }));
    EXPECT_EQ(inflight.error, "disconnected") << "in-flight work fails once, with the reason of the loss";
    EXPECT_TRUE(client.is_reconnecting());

    Outcome held;
    client.command<std::string>(held.sink(), "ECHO", "held"); // reconnecting: held
    auto second = server.accept();
    ASSERT_TRUE(second);
    auto auth = second->expect(1);
    ASSERT_EQ(auth.size(), 1u);
    EXPECT_EQ(auth[0], (Command{"AUTH", "s3cret"})) << "the handshake leads the new connection, ahead of held work";
    EXPECT_TRUE(second->expect(1, 150ms).empty());
    second->send(kOk);
    auto echo = second->expect(1);
    ASSERT_EQ(echo.size(), 1u);
    EXPECT_EQ(echo[0], (Command{"ECHO", "held"}));
    second->send(bulk("held"));
    ASSERT_TRUE(run_until([&] { return held.calls == 1; }));
    EXPECT_TRUE(held.ok);
    EXPECT_EQ(inflight.calls, 1) << "a failed command is never replayed";
    EXPECT_EQ(journal.events, (std::vector<std::string>{"connected", "disconnected:peer", "connected"}));
}

// A failure callback runs inside the loss, and what it decides there stands: a disconnect() is terminal even
// with a policy armed (no run follows it), and a connect() owns the client (the loss does not take it down
// behind it). on_disconnected still reports the lost link, after the in-flight work failed -- so after the
// down the callback caused.
TEST_F(ReconnectContract, ADisconnectFromAFailureCallbackIsTerminal) {
    FakeRedis server;
    Client    client{server.uri()};
    Journal   journal;
    journal.watch(client);
    client.enable_auto_reconnect(qb::redis::RetryPolicy{}.with_initial_delay(1ms).with_jitter(false));
    auto verdict = start_connect(client);
    auto peer    = server.accept();
    ASSERT_TRUE(peer);
    ASSERT_TRUE(run_until([&] { return verdict->has_value(); }));

    int                      failed = 0;
    qb::io::connection_state seen   = qb::io::connection_state::idle;
    client.command<std::string>(
        [&](qb::redis::Reply<std::string> &&r) {
            ++failed;
            seen = client.state();
            if (!r.ok())
                client.disconnect();
        },
        "PING");
    ASSERT_EQ(peer->expect(1).size(), 1u);
    peer->sock.disconnect();
    ASSERT_TRUE(run_until([&] { return failed == 1; }));
    EXPECT_EQ(seen, qb::io::connection_state::reconnecting) << "a failure callback sees where the client is heading";
    EXPECT_EQ(client.state(), qb::io::connection_state::down);
    EXPECT_EQ(client.down_reason(), qb::io::down_reason::user);
    EXPECT_FALSE(client.is_reconnecting());
    EXPECT_FALSE(server.has_pending_connection(150ms)) << "a disconnect() from a failure callback must not reconnect";
    EXPECT_EQ(journal.events, (std::vector<std::string>{"connected", "down:user", "disconnected:peer"}));
}

TEST_F(ReconnectContract, AConnectFromAFailureCallbackOwnsTheClient) {
    FakeRedis server;
    Client    client{server.uri()};
    Journal   journal;
    journal.watch(client);
    auto verdict = start_connect(client);
    auto first   = server.accept();
    ASSERT_TRUE(first);
    ASSERT_TRUE(run_until([&] { return verdict->has_value(); }));

    std::shared_ptr<std::optional<bool>> again;
    client.command<std::string>(
        [&](qb::redis::Reply<std::string> &&r) {
            if (!r.ok())
                again = start_connect(client); // no policy: the client is down here, so a connect() may start
        },
        "PING");
    ASSERT_EQ(first->expect(1).size(), 1u);
    first->sock.disconnect();
    auto second = server.accept();
    ASSERT_TRUE(second) << "the connect() the failure callback started must run";
    ASSERT_TRUE(again);
    ASSERT_TRUE(run_until([&] { return again->has_value(); }));
    EXPECT_TRUE(**again) << "the loss must not take the client down behind the connect() that followed it";
    EXPECT_EQ(client.state(), qb::io::connection_state::ready);
    EXPECT_EQ(journal.events, (std::vector<std::string>{"connected", "disconnected:peer", "connected"}));
}

TEST_F(ReconnectContract, TheWatchdogDropIsRecoverable) {
    FakeRedis server;
    Client    client{server.uri()};
    client.set_command_timeout(50ms);
    client.enable_auto_reconnect(qb::redis::RetryPolicy{}.with_initial_delay(1ms).with_jitter(false));
    auto verdict = start_connect(client);
    auto first   = server.accept();
    ASSERT_TRUE(first);
    ASSERT_TRUE(run_until([&] { return verdict->has_value(); }));

    Outcome stalled;
    client.command<std::string>(stalled.sink(), "PING"); // never answered
    ASSERT_TRUE(run_until([&] { return stalled.calls == 1; }));
    EXPECT_EQ(stalled.error, "command timed out");
    auto second = server.accept();
    ASSERT_TRUE(second) << "the watchdog's drop is the client's own decision: a run follows";
    ASSERT_TRUE(run_until([&] { return client.is_connected(); }));
}

// ---------------------------------------------------------------------------------------------- R4

TEST_F(ReconnectContract, WithoutAPolicyALostPeerIsDownAndNothingWaits) {
    FakeRedis server;
    Client    client{server.uri()};
    auto      verdict = start_connect(client);
    auto      peer    = server.accept();
    ASSERT_TRUE(peer);
    ASSERT_TRUE(run_until([&] { return verdict->has_value(); }));

    peer->sock.disconnect();
    ASSERT_TRUE(run_until([&] { return client.state() == qb::io::connection_state::down; }));
    EXPECT_EQ(client.down_reason(), qb::io::down_reason::peer);
    Outcome after;
    client.command<std::string>(after.sink(), "PING");
    ASSERT_TRUE(run_until([&] { return after.calls == 1; })) << "a command on a down client must fail, never wait for ever";
    EXPECT_EQ(after.error, "disconnected");
}

TEST_F(ReconnectContract, HeldWorkFailsAtTheCommandTimeout) {
    FakeRedis server;
    Client    client{server.uri()};
    client.enable_auto_reconnect(qb::redis::RetryPolicy{}.with_initial_delay(10s).with_jitter(false));
    auto verdict = start_connect(client);
    auto peer    = server.accept();
    ASSERT_TRUE(peer);
    ASSERT_TRUE(run_until([&] { return verdict->has_value(); }));
    client.set_uri(dead_uri()); // nothing will accept the next attempt
    peer->sock.disconnect();
    ASSERT_TRUE(run_until([&] { return client.is_reconnecting(); }));

    client.set_command_timeout(100ms);
    Outcome held;
    client.command<std::string>(held.sink(), "PING");
    ASSERT_TRUE(run_until([&] { return held.calls == 1; })) << "held work must fail at the command timeout";
    EXPECT_EQ(held.error, "command timed out");
    EXPECT_TRUE(client.is_reconnecting()) << "the timeout fails the command, not the run";
    client.disconnect();
}

TEST_F(ReconnectContract, HeldWorkFailsWhenTheRunIsSpent) {
    FakeRedis server;
    Client    client{server.uri()};
    Journal   journal;
    journal.watch(client);
    client.enable_auto_reconnect(
        qb::redis::RetryPolicy{}.with_max_attempts(2).with_initial_delay(1ms).with_connect_timeout(300ms).with_jitter(false));
    auto verdict = start_connect(client);
    auto peer    = server.accept();
    ASSERT_TRUE(peer);
    ASSERT_TRUE(run_until([&] { return verdict->has_value(); }));
    client.set_uri(dead_uri()); // nothing will accept the next attempts
    peer->sock.disconnect();
    ASSERT_TRUE(run_until([&] { return client.is_reconnecting(); }));

    Outcome held;
    client.command<std::string>(held.sink(), "PING");
    ASSERT_TRUE(run_until([&] { return held.calls == 1; }, 5000ms));
    EXPECT_EQ(held.error, "reconnection attempts exhausted");
    EXPECT_EQ(client.state(), qb::io::connection_state::down);
    EXPECT_EQ(client.down_reason(), qb::io::down_reason::retries_exhausted);
    EXPECT_EQ(journal.events, (std::vector<std::string>{"connected", "disconnected:peer", "down:retries_exhausted"}));
}

TEST_F(ReconnectContract, WorkSubmittedBeforeAnyConnectFails) {
    Client  client{qb::io::uri{"tcp://127.0.0.1:1"}};
    Outcome early;
    client.command<std::string>(early.sink(), "PING");
    ASSERT_TRUE(run_until([&] { return early.calls == 1; }));
    EXPECT_EQ(early.error, "not connected");
    EXPECT_EQ(client.pending_reply_count(), 0u);
}

// ---------------------------------------------------------------------------------------------- R8

TEST_F(ReconnectContract, StateWalksTheContract) {
    FakeRedis server;
    Client    client{server.uri()};
    client.with_auth("s3cret");
    client.enable_auto_reconnect(qb::redis::RetryPolicy{}.with_initial_delay(50ms).with_jitter(false));
    EXPECT_EQ(client.state(), qb::io::connection_state::idle);
    auto verdict = start_connect(client);
    EXPECT_EQ(client.state(), qb::io::connection_state::connecting);
    auto first = server.accept();
    ASSERT_TRUE(first);
    ASSERT_EQ(first->expect(1).size(), 1u);
    EXPECT_EQ(client.state(), qb::io::connection_state::handshaking);
    first->send(kOk);
    ASSERT_TRUE(run_until([&] { return client.state() == qb::io::connection_state::ready; }));

    first->sock.disconnect();
    ASSERT_TRUE(run_until([&] { return client.state() == qb::io::connection_state::reconnecting; }));
    auto second = server.accept();
    ASSERT_TRUE(second);
    ASSERT_EQ(second->expect(1).size(), 1u);
    EXPECT_EQ(client.state(), qb::io::connection_state::handshaking);
    EXPECT_TRUE(client.is_reconnecting()) << "the run is in progress until the new connection is ready";
    second->send(kOk);
    ASSERT_TRUE(run_until([&] { return client.state() == qb::io::connection_state::ready; }));
    EXPECT_FALSE(client.is_reconnecting());

    client.disconnect();
    EXPECT_EQ(client.state(), qb::io::connection_state::down);
    EXPECT_EQ(client.down_reason(), qb::io::down_reason::user);
}

TEST_F(ReconnectContract, AConnectAfterDownStartsANewLifecycle) {
    FakeRedis server;
    Client    client{server.uri()};
    Journal   journal;
    journal.watch(client);
    auto first_verdict = start_connect(client);
    auto first         = server.accept();
    ASSERT_TRUE(first);
    ASSERT_TRUE(run_until([&] { return first_verdict->has_value(); }));
    client.disconnect();

    auto second_verdict = start_connect(client);
    auto second         = server.accept();
    ASSERT_TRUE(second);
    ASSERT_TRUE(run_until([&] { return second_verdict->has_value(); }));
    EXPECT_TRUE(**second_verdict);
    EXPECT_TRUE(client.is_connected());
    EXPECT_EQ(journal.events, (std::vector<std::string>{"connected", "disconnected:user", "down:user", "connected"}));
}
