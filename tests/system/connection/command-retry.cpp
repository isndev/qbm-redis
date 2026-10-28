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
 * @file command-retry.cpp
 * @brief The retrying view, `redis.retrying(qb::redis::command_retry{...})` (Huly QB-133).
 *
 * System tier: a loopback socket and NO external redis daemon. The test is the server: it reads each command
 * off the wire and answers -- or drops the link, or refuses with a transient error -- as the case scripts.
 *
 * What is proved:
 *   - a read-only command that loses its link while a reconnection run is armed is sent again after the
 *     handshake, and its callback is called ONCE, with the last outcome; the re-issue counts in
 *     stats().retries, both attempts in stats().commands;
 *   - LOADING / BUSY / TRYAGAIN / MASTERDOWN are retried within the attempts, and the last reply comes back
 *     unchanged when they are spent; an error that merely starts like one (BUSYGROUP) is not;
 *   - never retried: a non-idempotent write, an idempotent write unless opted in, SET with NX, ZADD with INCR,
 *     a blocking command, a command inside MULTI, a command on a client that is down;
 *   - the coroutine form retries the same way; a retry pending when the client is destroyed is dropped,
 *     never called.
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

namespace command_retry_test {

using Client  = qb::redis::tcp::client;
using Command = std::vector<std::string>;

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

/// The server end of one connection: reads whole RESP commands, writes raw replies.
struct Peer {
    qb::io::tcp::socket sock;
    std::string         buffer;

    std::vector<Command>
    parse() {
        std::vector<Command> out;
        for (;;) {
            if (buffer.empty() || buffer[0] != '*')
                return out;
            auto line_end = buffer.find("\r\n");
            if (line_end == std::string::npos)
                return out;
            const int   argc = std::stoi(buffer.substr(1, line_end - 1));
            std::size_t pos  = line_end + 2;
            Command     cmd;
            for (int i = 0; i < argc; ++i) {
                line_end = buffer.find("\r\n", pos);
                if (line_end == std::string::npos)
                    return out;
                const auto len = static_cast<std::size_t>(std::stoul(buffer.substr(pos + 1, line_end - pos - 1)));
                pos            = line_end + 2;
                if (buffer.size() < pos + len + 2)
                    return out;
                cmd.push_back(buffer.substr(pos, len));
                pos += len + 2;
            }
            buffer.erase(0, pos);
            out.push_back(std::move(cmd));
        }
    }

    /// Pump the loop until `n` commands arrived (or the deadline); returns those read.
    std::vector<Command>
    expect(std::size_t n, std::chrono::milliseconds timeout = 3000ms) {
        std::vector<Command> got;
        run_until(
            [&] {
                char buf[4096];
                for (int r; (r = sock.read(buf, sizeof(buf))) > 0;)
                    buffer.append(buf, static_cast<std::size_t>(r));
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

/// Every outcome a callback saw: it must see exactly one.
struct Outcomes {
    std::vector<std::string> seen; ///< "ok:<value>" or "error:<text>"
    auto
    sink() {
        return [this](qb::redis::Reply<std::string> &&r) {
            seen.push_back(r.ok() ? "ok:" + r.result() : "error:" + r.error());
        };
    }
    auto
    get_sink() {
        return [this](qb::redis::Reply<std::optional<std::string>> &&r) {
            seen.push_back(r.ok() ? "ok:" + r.result().value_or("<nil>") : "error:" + r.error());
        };
    }
    auto
    status_sink() {
        return [this](qb::redis::Reply<qb::redis::status> &&r) {
            seen.push_back(r.ok() ? std::string{"ok"} : "error:" + r.error());
        };
    }
};

/// Fast attempts: 3 of them, 1 ms apart.
qb::redis::command_retry
quick(bool writes = false) {
    qb::redis::command_retry r;
    r.backoff           = qb::io::reconnect_policy{}.with_max_attempts(3).with_initial_delay(1ms).with_jitter(false);
    r.idempotent_writes = writes;
    return r;
}

class CommandRetry : public ::testing::Test {
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

} // namespace command_retry_test

using namespace command_retry_test;

TEST_F(CommandRetry, AReadIsSentAgainAfterTheReconnectionAndCalledOnce) {
    FakeRedis server;
    Client    client{server.uri()};
    client.enable_auto_reconnect(qb::redis::RetryPolicy{}.with_initial_delay(1ms).with_jitter(false));
    auto first = connect(client, server);
    ASSERT_TRUE(first);

    Outcomes out;
    client.retrying(quick()).get(out.get_sink(), "k");
    auto sent = first->expect(1);
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0], (Command{"GET", "k"}));
    first->sock.disconnect(); // the link is lost with the GET in flight; the run reconnects

    auto second = server.accept();
    ASSERT_TRUE(second);
    auto again = second->expect(1);
    ASSERT_EQ(again.size(), 1u) << "the read is sent again on the new connection";
    EXPECT_EQ(again[0], (Command{"GET", "k"}));
    EXPECT_TRUE(out.seen.empty()) << "no outcome yet: the first failure was retried, not reported";
    second->send(bulk("v"));
    ASSERT_TRUE(run_until([&] { return out.seen.size() == 1; }));
    settle(20ms);
    EXPECT_EQ(out.seen, (std::vector<std::string>{"ok:v"})) << "called once, with the last outcome";
    EXPECT_EQ(client.stats().retries, 1u);
    EXPECT_EQ(client.stats().commands, 2u) << "each attempt is a command";
}

TEST_F(CommandRetry, TransientServerErrorsAreRetriedAndTheLastReplyComesBack) {
    FakeRedis server;
    Client    client{server.uri()};
    auto      peer = connect(client, server);
    ASSERT_TRUE(peer);

    Outcomes out;
    client.retrying(quick()).get(out.get_sink(), "k");
    for (int attempt = 1; attempt <= 3; ++attempt) {
        auto got = peer->expect(1);
        ASSERT_EQ(got.size(), 1u) << "attempt " << attempt;
        peer->send("-LOADING Redis is loading the dataset in memory\r\n");
    }
    ASSERT_TRUE(run_until([&] { return out.seen.size() == 1; }));
    settle(20ms);
    EXPECT_EQ(out.seen, (std::vector<std::string>{"error:LOADING Redis is loading the dataset in memory"}))
        << "the attempts are spent: the last reply, unchanged, once";
    EXPECT_TRUE(peer->expect(1, 50ms).empty()) << "no fourth attempt";
    EXPECT_EQ(client.stats().retries, 2u);

    Outcomes group;
    client.retrying(quick()).get(group.get_sink(), "k");
    ASSERT_EQ(peer->expect(1).size(), 1u);
    peer->send("-BUSYGROUP Consumer Group name already exists\r\n");
    ASSERT_TRUE(run_until([&] { return group.seen.size() == 1; }));
    EXPECT_TRUE(peer->expect(1, 50ms).empty()) << "BUSYGROUP is not BUSY: no retry";
    EXPECT_EQ(client.stats().retries, 2u);
}

TEST_F(CommandRetry, WritesAreRetriedOnlyWhenIdempotentAndOptedIn) {
    FakeRedis server;
    Client    client{server.uri()};
    auto      peer = connect(client, server);
    ASSERT_TRUE(peer);

    // Each case: the server answers LOADING once; a retried command is then answered, a passed-through one is done.
    const auto once = [&](auto submit) -> std::size_t {
        submit();
        auto first = peer->expect(1);
        EXPECT_EQ(first.size(), 1u);
        peer->send("-LOADING Redis is loading the dataset in memory\r\n");
        auto again = peer->expect(1, 100ms);
        if (!again.empty())
            peer->send("+OK\r\n");
        settle(10ms);
        return again.size();
    };
    Outcomes o;
    EXPECT_EQ(once([&] { client.retrying(quick()).incr([](qb::redis::Reply<long long> &&) {}, "n"); }), 0u) << "INCR: never";
    EXPECT_EQ(once([&] { client.retrying(quick()).set(o.status_sink(), "k", "v"); }), 0u) << "SET: not without opting in";
    EXPECT_EQ(once([&] { client.retrying(quick(true)).set(o.status_sink(), "k", "v"); }), 1u) << "SET, opted in";
    EXPECT_EQ(once([&] { client.retrying(quick(true)).command<qb::redis::status>(o.status_sink(), "SET", "k", "v", "NX"); }), 0u)
        << "SET NX: its reply would depend on the first attempt";
    EXPECT_EQ(once([&] { client.retrying(quick(true)).command<std::string>(o.sink(), "ZADD", "z", "INCR", "1", "m"); }), 0u)
        << "ZADD INCR accumulates";
    EXPECT_EQ(once([&] { client.retrying(quick(true)).command<long long>([](qb::redis::Reply<long long> &&) {}, "ZADD", "z", "1", "m"); }), 1u)
        << "ZADD, opted in";
    EXPECT_EQ(client.stats().retries, 2u);
}

TEST_F(CommandRetry, BlockingCommandsCommandsInsideMultiAndADownClientAreNeverRetried) {
    FakeRedis server;
    Client    client{server.uri()};
    client.enable_auto_reconnect(qb::redis::RetryPolicy{}.with_initial_delay(1ms).with_jitter(false));
    auto peer = connect(client, server);
    ASSERT_TRUE(peer);

    Outcomes blpop;
    client.retrying(quick()).command<std::string>(blpop.sink(), "BLPOP", "l", "0");
    ASSERT_EQ(peer->expect(1).size(), 1u);
    peer->send("-LOADING Redis is loading the dataset in memory\r\n");
    ASSERT_TRUE(run_until([&] { return blpop.seen.size() == 1; }));
    EXPECT_TRUE(peer->expect(1, 50ms).empty()) << "a blocking command is never retried";

    bool in_multi = false;
    client.multi([&](qb::redis::Reply<qb::redis::status> &&r) { in_multi = r.ok(); });
    ASSERT_EQ(peer->expect(1).size(), 1u);
    peer->send("+OK\r\n");
    ASSERT_TRUE(run_until([&] { return in_multi; }));
    ASSERT_TRUE(client.is_in_multi());
    Outcomes queued;
    client.retrying(quick()).get(queued.get_sink(), "k");
    ASSERT_EQ(peer->expect(1).size(), 1u);
    peer->send("-LOADING Redis is loading the dataset in memory\r\n");
    ASSERT_TRUE(run_until([&] { return queued.seen.size() == 1; }));
    EXPECT_TRUE(peer->expect(1, 50ms).empty()) << "nothing inside MULTI is retried";
    EXPECT_EQ(client.stats().retries, 0u);

    bool discarded = false;
    client.discard([&](qb::redis::Reply<qb::redis::status> &&r) { discarded = r.ok(); });
    ASSERT_EQ(peer->expect(1).size(), 1u);
    peer->send("+OK\r\n");
    ASSERT_TRUE(run_until([&] { return discarded; }));
    ASSERT_FALSE(client.is_in_multi());

    client.disable_auto_reconnect();
    Outcomes lost;
    client.retrying(quick()).get(lost.get_sink(), "k");
    ASSERT_EQ(peer->expect(1).size(), 1u);
    peer->sock.disconnect(); // no run armed: the client goes down, a terminal state
    ASSERT_TRUE(run_until([&] { return lost.seen.size() == 1; }));
    EXPECT_EQ(lost.seen, (std::vector<std::string>{"error:disconnected"}));
    EXPECT_EQ(client.state(), qb::io::connection_state::down);
    EXPECT_EQ(client.stats().retries, 0u) << "a client that is down carries nothing more";
}

namespace command_retry_test {
struct CoroResult {
    std::optional<std::string> value;
    bool                       done = false;
};

qb::io::async::task<void>
get_through_view(Client *client, CoroResult *out) {
    auto r     = co_await client->retrying(quick()).get("k");
    out->value = r.ok() && r.result() ? *r.result() : std::string{"<failed>"};
    out->done  = true;
}
} // namespace command_retry_test

TEST_F(CommandRetry, TheCoroutineFormRetriesTheSameWay) {
    FakeRedis server;
    Client    client{server.uri()};
    auto      peer = connect(client, server);
    ASSERT_TRUE(peer);

    CoroResult result;
    qb::io::async::coro_scheduler().spawn(get_through_view(&client, &result));
    ASSERT_EQ(peer->expect(1).size(), 1u);
    peer->send("-TRYAGAIN Multiple keys request during rehashing of slot\r\n");
    ASSERT_EQ(peer->expect(1).size(), 1u) << "retried";
    peer->send(bulk("v"));
    ASSERT_TRUE(run_until([&] { return result.done; }));
    EXPECT_EQ(result.value, "v");
    EXPECT_EQ(client.stats().retries, 1u);
}

TEST_F(CommandRetry, ARetryPendingWhenTheClientIsDestroyedIsDropped) {
    FakeRedis server;
    auto      client = std::make_unique<Client>(server.uri());
    auto      peer   = connect(*client, server);
    ASSERT_TRUE(peer);

    Outcomes                 out;
    qb::redis::command_retry slow;
    slow.backoff = qb::io::reconnect_policy{}.with_max_attempts(3).with_initial_delay(50ms).with_jitter(false);
    client->retrying(slow).get(out.get_sink(), "k");
    ASSERT_EQ(peer->expect(1).size(), 1u);
    peer->send("-BUSY Redis is busy running a script\r\n");
    ASSERT_TRUE(run_until([&] { return client->stats().retries == 1; })) << "the retry is scheduled, 50 ms out";
    client.reset();
    settle(100ms); // past the retry's time
    EXPECT_TRUE(out.seen.empty()) << "a client gone discards its pending work uninvoked, the retry included";
}

TEST_F(CommandRetry, ARetryWaitingForItsBackoffIsPendingOnTheClient) {
    // The retry sat outside the client's queues during its backoff: await() returned and pending_reply_count() said
    // 0 with the command not done (the QB-129 lot's review).
    FakeRedis server;
    Client    client{server.uri()};
    auto      peer = connect(client, server);
    ASSERT_TRUE(peer);
    qb::redis::command_retry slow;
    slow.backoff = qb::io::reconnect_policy{}.with_max_attempts(2).with_initial_delay(300ms).with_jitter(false);

    Outcomes out;
    client.retrying(slow).get(out.get_sink(), "k");
    ASSERT_EQ(peer->expect(1).size(), 1u);
    peer->send("-LOADING Redis is loading the dataset in memory\r\n");
    ASSERT_TRUE(run_until([&] { return client.stats().retries == 1; }));
    EXPECT_EQ(client.pending_reply_count(), 1u) << "waiting for its backoff, still the client's";
    ASSERT_EQ(peer->expect(1, 2000ms).size(), 1u) << "the second attempt";
    peer->send(bulk("v"));
    ASSERT_TRUE(run_until([&] { return out.seen.size() == 1; }));
    EXPECT_EQ(out.seen, (std::vector<std::string>{"ok:v"}));
    EXPECT_EQ(client.pending_reply_count(), 0u);
}

TEST_F(CommandRetry, ACommandBehindASubmittedMultiIsNeverRetried) {
    FakeRedis server;
    Client    client{server.uri()};
    auto      peer = connect(client, server);
    ASSERT_TRUE(peer);

    Outcomes out;
    client.multi([](qb::redis::Reply<qb::redis::status> &&) {});
    client.retrying(quick()).get(out.get_sink(), "k"); // behind the MULTI, before its +OK
    ASSERT_EQ(peer->expect(2).size(), 2u);
    peer->send("+OK\r\n-LOADING Redis is loading the dataset in memory\r\n");
    ASSERT_TRUE(run_until([&] { return out.seen.size() == 1; }));
    EXPECT_TRUE(peer->expect(1, 50ms).empty()) << "inside the transaction: not retried";
    EXPECT_EQ(client.stats().retries, 0u);
}

TEST_F(CommandRetry, ARetryPendingAcrossAMultiIsNotIssuedInsideIt) {
    // A multi() submitted while a retry waited for its backoff: the retry went out behind the MULTI, was queued
    // (+QUEUED came back as the value) and joined the EXEC (Huly QB-129 lot, second review).
    FakeRedis server;
    Client    client{server.uri()};
    auto      peer = connect(client, server);
    ASSERT_TRUE(peer);
    qb::redis::command_retry slow;
    slow.backoff = qb::io::reconnect_policy{}.with_max_attempts(3).with_initial_delay(100ms).with_jitter(false);

    Outcomes out;
    client.retrying(slow).get(out.get_sink(), "k");
    ASSERT_EQ(peer->expect(1).size(), 1u);
    peer->send("-LOADING Redis is loading the dataset in memory\r\n");
    ASSERT_TRUE(run_until([&] { return client.stats().retries == 1; }));
    client.multi(out.status_sink());
    EXPECT_EQ(peer->expect(1), (std::vector<Command>{{"MULTI"}}));
    peer->send("+OK\r\n");
    ASSERT_TRUE(run_until([&] { return out.seen.size() == 2; }, 1000ms));
    EXPECT_EQ(out.seen, (std::vector<std::string>{"ok", "error:LOADING Redis is loading the dataset in memory"}))
        << "the last failure is the outcome, once";
    EXPECT_TRUE(peer->expect(1, 250ms).empty()) << "nothing sent inside the transaction";
    EXPECT_EQ(client.pending_reply_count(), 0u);
}

TEST_F(CommandRetry, AwaitReturnsOnlyOnceTheRetriedCommandIsDone) {
    FakeRedis server;
    Client    client{server.uri()};
    auto      peer = connect(client, server);
    ASSERT_TRUE(peer);
    qb::redis::command_retry slow;
    slow.backoff = qb::io::reconnect_policy{}.with_max_attempts(2).with_initial_delay(50ms).with_jitter(false);

    Outcomes out;
    client.retrying(slow).get(out.get_sink(), "k");
    ASSERT_EQ(peer->expect(1).size(), 1u);
    peer->send("-LOADING Redis is loading the dataset in memory\r\n");
    ASSERT_TRUE(run_until([&] { return client.stats().retries == 1; }));
    // The second attempt leaves 50 ms from now; the peer answers it at 150 ms, while await() spins.
    qb::io::async::callback([&peer] { peer->send(bulk("v")); }, 150ms);
    client.await();
    EXPECT_EQ(out.seen, (std::vector<std::string>{"ok:v"})) << "await() returned with the retry still to come";
}

TEST_F(CommandRetry, AResetThroughTheViewEndsTheTransactionItWasWaitingBehind) {
    FakeRedis server;
    Client    client{server.uri()};
    auto      peer = connect(client, server);
    ASSERT_TRUE(peer);

    Outcomes out;
    client.multi(out.status_sink());
    client.retrying(quick()).reset(out.status_sink());
    client.retrying(quick()).get(out.get_sink(), "k"); // after the RESET: retried again
    ASSERT_EQ(peer->expect(3), (std::vector<Command>{{"MULTI"}, {"RESET"}, {"GET", "k"}}));
    peer->send("+OK\r\n+RESET\r\n-LOADING Redis is loading the dataset in memory\r\n");
    ASSERT_EQ(peer->expect(1), (std::vector<Command>{{"GET", "k"}})) << "retried: the RESET closed the transaction";
    peer->send(bulk("v"));
    ASSERT_TRUE(run_until([&] { return out.seen.size() == 3; }));
    EXPECT_EQ(out.seen.back(), "ok:v");
}
