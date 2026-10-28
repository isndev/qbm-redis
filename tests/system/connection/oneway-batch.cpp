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
 * @file oneway-batch.cpp
 * @brief The client's side of a one-way batch, `redis.send_oneway(std::move(batch))` (Huly QB-902).
 *
 * System tier: a loopback socket and NO redis daemon. The test is the server: it reads each command off the wire and
 * answers as the case scripts -- as Redis does (`CLIENT REPLY OFF` and the commands after it answer nothing, the
 * `CLIENT REPLY ON` answers `+OK`), or as a server that does not honour OFF.
 *
 * What is proved:
 *   - the batch leaves as OFF, its commands, ON, behind the commands submitted before it and ahead of those after;
 *     each reply completes the command it answers, once; the batch completes at the ON's `+OK`;
 *   - an empty batch sends nothing and succeeds at once;
 *   - it is refused, nothing written, while a submitted MULTI is not closed -- EXEC, DISCARD or a lost link close
 *     it -- and while MONITOR owns the connection;
 *   - a server that answers the ON otherwise than `+OK` (OFF unsupported, or a raw MULTI the client does not track)
 *     drops the link: the batch fails with the reason, and the command sent behind it fails as on a lost link,
 *     never completed by a reply of the batch;
 *   - a lost link and the command timeout fail the batch; a batch submitted while connecting is held and leaves once
 *     the client is ready; an idle client fails it on the next loop pass;
 *   - a RESP3 push arriving during the batch is not taken for a reply;
 *   - the coroutine form completes the same way.
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

namespace oneway_batch_test {

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

struct FakeServer {
    qb::io::tcp::listener listener;
    std::uint16_t         port = 0;

    FakeServer() {
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

const Command off{"CLIENT", "REPLY", "OFF"};
const Command on{"CLIENT", "REPLY", "ON"};

/// Every outcome a callback saw, in order: each must see exactly one.
struct Outcomes {
    std::vector<std::string> seen; ///< "<what> ok:<value>" or "<what> error:<text>"

    auto
    oneway_sink(std::string what) {
        return [this, what = std::move(what)](qb::redis::Reply<qb::redis::oneway_result> &&r) {
            seen.push_back(what + (r.ok() ? " ok:" + std::to_string(r.result().commands) : " error:" + r.error()));
        };
    }
    auto
    get_sink(std::string what) {
        return [this, what = std::move(what)](qb::redis::Reply<std::optional<std::string>> &&r) {
            seen.push_back(what + (r.ok() ? " ok:" + r.result().value_or("<nil>") : " error:" + r.error()));
        };
    }
    auto
    status_sink(std::string what) {
        return [this, what = std::move(what)](qb::redis::Reply<qb::redis::status> &&r) {
            seen.push_back(what + (r.ok() ? std::string{" ok"} : " error:" + r.error()));
        };
    }
};

qb::redis::oneway_batch
batch_of(std::initializer_list<std::pair<std::string, std::string>> sets) {
    qb::redis::oneway_batch batch;
    for (auto const &[k, v] : sets)
        EXPECT_FALSE(batch.add("SET", k, v));
    return batch;
}

qb::io::async::task<void>
co_send(Client &client, qb::redis::oneway_batch batch, std::string *out) {
    auto r = co_await client.send_oneway(std::move(batch));
    *out   = r.ok() ? "ok:" + std::to_string(r.result().commands) : "error:" + r.error();
}

class OnewayBatch : public ::testing::Test {
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
    connect(Client &client, FakeServer &server) {
        auto done = std::make_shared<bool>(false);
        client.connect([done](bool) { *done = true; }, client.uri());
        auto peer = server.accept();
        EXPECT_TRUE(peer);
        EXPECT_TRUE(run_until([&] { return *done; }));
        return peer;
    }
};

} // namespace oneway_batch_test

using namespace oneway_batch_test;

TEST_F(OnewayBatch, LeavesAsOffCommandsOnBetweenTheCommandsAroundItAndEachReplyCompletesItsOwn) {
    FakeServer server;
    Client     client{server.uri()};
    auto       peer = connect(client, server);
    ASSERT_TRUE(peer);
    Outcomes out;

    client.get(out.get_sink("get a"), "a");
    qb::redis::oneway_batch batch;
    ASSERT_FALSE(batch.add("SET", "k", "v"));
    ASSERT_FALSE(batch.add("INCR", "n"));
    client.send_oneway(std::move(batch), out.oneway_sink("batch"));
    client.get(out.get_sink("get b"), "b");
    EXPECT_EQ(client.pending_reply_count(), 3u) << "the batch waits for one reply: its ON's";

    EXPECT_EQ(peer->expect(6), (std::vector<Command>{{"GET", "a"}, off, {"SET", "k", "v"}, {"INCR", "n"}, on, {"GET", "b"}}));
    peer->send(bulk("x") + "+OK\r\n" + bulk("y"));
    ASSERT_TRUE(run_until([&] { return out.seen.size() == 3; }));
    EXPECT_EQ(out.seen, (std::vector<std::string>{"get a ok:x", "batch ok:2", "get b ok:y"}));
    EXPECT_EQ(client.pending_reply_count(), 0u);
    EXPECT_EQ(client.stats().oneway, 2u) << "the commands of the batches the server confirmed";
    EXPECT_EQ(client.stats().commands, 3u) << "the batch completes as one command";
    run_until([] { return false; }, 50ms);
    EXPECT_EQ(out.seen.size(), 3u) << "each callback is called once";
}

TEST_F(OnewayBatch, AnEmptyBatchSendsNothingAndSucceedsAtOnce) {
    FakeServer server;
    Client     client{server.uri()};
    auto       peer = connect(client, server);
    ASSERT_TRUE(peer);
    Outcomes out;

    client.send_oneway(qb::redis::oneway_batch{}, out.oneway_sink("empty"));
    EXPECT_EQ(out.seen, (std::vector<std::string>{"empty ok:0"})) << "completed before send_oneway returned";
    EXPECT_EQ(client.pending_reply_count(), 0u);
    EXPECT_TRUE(peer->expect(1, 150ms).empty()) << "nothing was written";
}

TEST_F(OnewayBatch, RefusedWhileASubmittedMultiIsNotClosedAndAcceptedOnceItIs) {
    FakeServer server;
    Client     client{server.uri()};
    auto       peer = connect(client, server);
    ASSERT_TRUE(peer);
    Outcomes out;

    client.multi(out.status_sink("multi")); // submitted: its +OK has not arrived, is_in_multi() is still false
    EXPECT_FALSE(client.is_in_multi());
    client.send_oneway(batch_of({{"k", "1"}}), out.oneway_sink("inside"));
    ASSERT_EQ(out.seen.size(), 1u) << "refused before send_oneway returned";
    EXPECT_EQ(out.seen[0].rfind("inside error:a MULTI is open", 0), 0u) << out.seen[0];

    client.discard(out.status_sink("discard"));
    client.send_oneway(batch_of({{"k", "2"}}), out.oneway_sink("after"));
    EXPECT_EQ(peer->expect(5), (std::vector<Command>{{"MULTI"}, {"DISCARD"}, off, {"SET", "k", "2"}, on})) << "the refused batch wrote nothing";
    peer->send("+OK\r\n+OK\r\n+OK\r\n");
    ASSERT_TRUE(run_until([&] { return out.seen.size() == 4; }));
    EXPECT_EQ(out.seen[1], "multi ok");
    EXPECT_EQ(out.seen[2], "discard ok");
    EXPECT_EQ(out.seen[3], "after ok:1");

    // EXEC closes it too.
    client.multi(out.status_sink("multi 2"));
    client.send_oneway(batch_of({{"k", "3"}}), out.oneway_sink("inside 2"));
    EXPECT_EQ(out.seen.back().rfind("inside 2 error:a MULTI is open", 0), 0u) << out.seen.back();
    client.exec<std::string>(
        [&](qb::redis::Reply<std::vector<std::string>> &&r) { out.seen.push_back(r.ok() ? "exec ok" : "exec error:" + r.error()); });
    client.send_oneway(batch_of({{"k", "4"}}), out.oneway_sink("after exec"));
    EXPECT_EQ(peer->expect(5), (std::vector<Command>{{"MULTI"}, {"EXEC"}, off, {"SET", "k", "4"}, on}));
    peer->send("+OK\r\n*0\r\n+OK\r\n");
    ASSERT_TRUE(run_until([&] { return out.seen.size() == 8; }));
    EXPECT_EQ(out.seen[5], "multi 2 ok");
    EXPECT_EQ(out.seen[6], "exec ok");
    EXPECT_EQ(out.seen[7], "after exec ok:1");
}

TEST_F(OnewayBatch, ALinkLostAfterAMultiClosesItForTheNextConnection) {
    FakeServer server;
    Client     client{server.uri()};
    client.enable_auto_reconnect(qb::redis::RetryPolicy{}.with_initial_delay(1ms).with_max_delay(10ms).with_jitter(false));
    auto first = connect(client, server);
    ASSERT_TRUE(first);
    Outcomes out;

    client.multi(out.status_sink("multi"));
    EXPECT_EQ(first->expect(1), (std::vector<Command>{{"MULTI"}}));
    first->sock.disconnect(); // the server forgets the transaction with the connection
    ASSERT_TRUE(run_until([&] { return out.seen.size() == 1; }));
    EXPECT_EQ(out.seen[0], "multi error:disconnected");
    auto second = server.accept();
    ASSERT_TRUE(second);
    ASSERT_TRUE(run_until([&] { return client.is_connected(); }));

    client.send_oneway(batch_of({{"k", "v"}}), out.oneway_sink("batch"));
    EXPECT_EQ(second->expect(3), (std::vector<Command>{off, {"SET", "k", "v"}, on})) << "the lost MULTI does not refuse it";
    second->send("+OK\r\n");
    ASSERT_TRUE(run_until([&] { return out.seen.size() == 2; }));
    EXPECT_EQ(out.seen[1], "batch ok:1");
}

TEST_F(OnewayBatch, RefusedWhileMonitorOwnsTheConnection) {
    FakeServer server;
    Client     client{server.uri()};
    auto       peer = connect(client, server);
    ASSERT_TRUE(peer);
    Outcomes                 out;
    std::vector<std::string> lines;

    client.monitor([&](qb::redis::Reply<std::string> &&r) { lines.push_back(r.ok() ? r.result() : "error:" + r.error()); });
    EXPECT_EQ(peer->expect(1), (std::vector<Command>{{"MONITOR"}}));
    peer->send("+OK\r\n");
    ASSERT_TRUE(run_until([&] { return lines.size() == 1; }));

    client.send_oneway(batch_of({{"k", "v"}}), out.oneway_sink("batch"));
    ASSERT_EQ(out.seen.size(), 1u);
    EXPECT_EQ(out.seen[0].rfind("batch error:MONITOR owns this connection", 0), 0u) << out.seen[0];
    EXPECT_TRUE(peer->expect(1, 150ms).empty()) << "nothing was written";
}

TEST_F(OnewayBatch, AServerThatDoesNotHonourOffDropsTheLinkAndNoReplyOfTheBatchCompletesAnotherCommand) {
    FakeServer server;
    Client     client{server.uri()};
    auto       peer = connect(client, server);
    ASSERT_TRUE(peer);
    Outcomes out;

    client.send_oneway(batch_of({{"k", "v"}}), out.oneway_sink("batch"));
    client.get(out.get_sink("get"), "k");
    EXPECT_EQ(peer->expect(4), (std::vector<Command>{off, {"SET", "k", "v"}, on, {"GET", "k"}}));
    // A server without CLIENT REPLY: OFF and ON are errors, and the SET is answered.
    peer->send("-ERR unknown subcommand 'REPLY'\r\n+OK\r\n-ERR unknown subcommand 'REPLY'\r\n" + bulk("v"));
    ASSERT_TRUE(run_until([&] { return out.seen.size() == 2; }));
    EXPECT_EQ(out.seen[0].rfind("batch error:the server did not honour CLIENT REPLY OFF: ", 0), 0u) << out.seen[0];
    EXPECT_NE(out.seen[0].find("unknown subcommand"), std::string::npos) << out.seen[0];
    EXPECT_EQ(out.seen[1], "get error:disconnected") << "the SET's +OK must not complete the GET";
    EXPECT_FALSE(client.is_connected());
    EXPECT_EQ(client.stats().oneway, 0u);
}

TEST_F(OnewayBatch, ARawMultiTheClientDoesNotTrackIsCaughtAtTheOnAndDropsTheLink) {
    FakeServer server;
    Client     client{server.uri()};
    auto       peer = connect(client, server);
    ASSERT_TRUE(peer);
    Outcomes out;

    client.command<qb::redis::status>(out.status_sink("raw multi"), "MULTI"); // the escape hatch: not tracked
    client.send_oneway(batch_of({{"k", "v"}}), out.oneway_sink("batch"));
    client.get(out.get_sink("get"), "k");
    EXPECT_EQ(peer->expect(5), (std::vector<Command>{{"MULTI"}, off, {"SET", "k", "v"}, on, {"GET", "k"}}));
    peer->send("+OK\r\n+QUEUED\r\n+QUEUED\r\n+QUEUED\r\n+QUEUED\r\n"); // what a transaction answers
    ASSERT_TRUE(run_until([&] { return out.seen.size() == 3; }));
    EXPECT_EQ(out.seen[0], "raw multi ok");
    EXPECT_EQ(out.seen[1].rfind("batch error:the server did not honour CLIENT REPLY OFF: ", 0), 0u) << out.seen[1];
    EXPECT_EQ(out.seen[2], "get error:disconnected");
}

TEST_F(OnewayBatch, ALinkLostBeforeTheOnFailsTheBatch) {
    FakeServer server;
    Client     client{server.uri()};
    auto       peer = connect(client, server);
    ASSERT_TRUE(peer);
    Outcomes out;

    client.send_oneway(batch_of({{"k", "v"}}), out.oneway_sink("batch"));
    EXPECT_EQ(peer->expect(3).size(), 3u);
    peer->sock.disconnect();
    ASSERT_TRUE(run_until([&] { return !out.seen.empty(); }));
    EXPECT_EQ(out.seen, (std::vector<std::string>{"batch error:disconnected"}));
    EXPECT_EQ(client.stats().oneway, 0u);
}

TEST_F(OnewayBatch, TheCommandTimeoutCoversTheBatch) {
    FakeServer server;
    Client     client{server.uri()};
    auto       peer = connect(client, server);
    ASSERT_TRUE(peer);
    Outcomes out;

    client.set_command_timeout(200ms);
    const auto start = std::chrono::steady_clock::now();
    client.send_oneway(batch_of({{"k", "v"}}), out.oneway_sink("batch"));
    EXPECT_EQ(peer->expect(3).size(), 3u);
    ASSERT_TRUE(run_until([&] { return !out.seen.empty(); }, 5000ms)) << "a batch the server never closes must not hang";
    EXPECT_EQ(out.seen, (std::vector<std::string>{"batch error:command timed out"}));
    EXPECT_GE(std::chrono::steady_clock::now() - start, 200ms);
}

TEST_F(OnewayBatch, HeldWhileConnectingAndSentOnceTheClientIsReady) {
    FakeServer server;
    Client     client{server.uri()};
    Outcomes   out;
    auto       done = std::make_shared<bool>(false);
    client.connect([done](bool) { *done = true; }, client.uri());

    client.send_oneway(batch_of({{"k", "v"}}), out.oneway_sink("held")); // connecting: held
    EXPECT_EQ(client.stats().held, 1u);
    EXPECT_EQ(client.pending_reply_count(), 1u);
    auto peer = server.accept();
    ASSERT_TRUE(peer);
    ASSERT_TRUE(run_until([&] { return *done; }));
    EXPECT_EQ(peer->expect(3), (std::vector<Command>{off, {"SET", "k", "v"}, on}));
    peer->send("+OK\r\n");
    ASSERT_TRUE(run_until([&] { return !out.seen.empty(); }));
    EXPECT_EQ(out.seen, (std::vector<std::string>{"held ok:1"}));
}

TEST_F(OnewayBatch, AnIdleClientFailsTheBatchOnTheNextLoopPass) {
    Client   client{qb::io::uri{"tcp://127.0.0.1:1"}}; // never connected
    Outcomes out;
    client.send_oneway(batch_of({{"k", "v"}}), out.oneway_sink("idle"));
    EXPECT_TRUE(out.seen.empty()) << "a failure is never called inside the call that submitted it";
    ASSERT_TRUE(run_until([&] { return !out.seen.empty(); }));
    ASSERT_EQ(out.seen.size(), 1u);
    EXPECT_EQ(out.seen[0].rfind("idle error:", 0), 0u) << out.seen[0];
    EXPECT_GT(out.seen[0].size(), std::string{"idle error:"}.size()) << "the reason is said";
}

TEST_F(OnewayBatch, APushDuringTheBatchIsNotTakenForAReply) {
    FakeServer server;
    Client     client{server.uri()};
    auto       peer = connect(client, server);
    ASSERT_TRUE(peer);
    Outcomes out;

    client.send_oneway(batch_of({{"k", "v"}}), out.oneway_sink("batch"));
    client.get(out.get_sink("get"), "k");
    EXPECT_EQ(peer->expect(4).size(), 4u);
    // Redis 8.10 delivers a RESP3 push even with replies off (measured: an invalidation, a pub/sub message).
    peer->send(">2\r\n$10\r\ninvalidate\r\n*1\r\n$1\r\nk\r\n+OK\r\n" + bulk("v"));
    ASSERT_TRUE(run_until([&] { return out.seen.size() == 2; }));
    EXPECT_EQ(out.seen, (std::vector<std::string>{"batch ok:1", "get ok:v"}));
}

TEST_F(OnewayBatch, TheCoroutineFormCompletesTheSameWay) {
    FakeServer server;
    Client     client{server.uri()};
    auto       peer = connect(client, server);
    ASSERT_TRUE(peer);
    std::string sent, refused;

    qb::io::async::coro_scheduler().spawn(co_send(client, batch_of({{"a", "1"}, {"b", "2"}}), &sent));
    EXPECT_EQ(peer->expect(4), (std::vector<Command>{off, {"SET", "a", "1"}, {"SET", "b", "2"}, on}));
    peer->send("+OK\r\n");
    ASSERT_TRUE(run_until([&] { return !sent.empty(); }));
    EXPECT_EQ(sent, "ok:2");

    client.multi([](qb::redis::Reply<qb::redis::status> &&) {});
    qb::io::async::coro_scheduler().spawn(co_send(client, batch_of({{"c", "3"}}), &refused));
    ASSERT_TRUE(run_until([&] { return !refused.empty(); })) << "a refused batch resumes its awaiter";
    EXPECT_EQ(refused.rfind("error:a MULTI is open", 0), 0u) << refused;
}

// ---- The MULTI marker never sticks (the QB-902 lot's review) -------------------------------------------------

TEST_F(OnewayBatch, AMultiThatNeverLeftDoesNotKeepTheGateClosed) {
    Client   client{qb::io::uri{"tcp://127.0.0.1:1"}}; // idle: the MULTI fails on the next loop pass, unsent
    Outcomes out;
    client.multi(out.status_sink("multi"));
    ASSERT_TRUE(run_until([&] { return out.seen.size() == 1; }));
    client.send_oneway(batch_of({{"k", "v"}}), out.oneway_sink("batch"));
    ASSERT_TRUE(run_until([&] { return out.seen.size() == 2; }));
    EXPECT_EQ(out.seen[1].find("a MULTI is open"), std::string::npos) << "refused as idle, not as inside a MULTI: " << out.seen[1];
}

TEST_F(OnewayBatch, AMultiTheServerRefusesOpensNoTransaction) {
    FakeServer server;
    Client     client{server.uri()};
    auto       peer = connect(client, server);
    ASSERT_TRUE(peer);
    Outcomes out;

    client.multi(out.status_sink("multi"));
    EXPECT_EQ(peer->expect(1), (std::vector<Command>{{"MULTI"}}));
    peer->send("-NOAUTH Authentication required.\r\n");
    ASSERT_TRUE(run_until([&] { return out.seen.size() == 1; }));
    client.send_oneway(batch_of({{"k", "v"}}), out.oneway_sink("batch"));
    EXPECT_EQ(peer->expect(3), (std::vector<Command>{off, {"SET", "k", "v"}, on})) << "the gate is open again";
}

TEST_F(OnewayBatch, ANestedMultiErrorLeavesTheTransactionOpen) {
    FakeServer server;
    Client     client{server.uri()};
    auto       peer = connect(client, server);
    ASSERT_TRUE(peer);
    Outcomes out;

    client.multi(out.status_sink("first"));
    client.multi(out.status_sink("nested"));
    EXPECT_EQ(peer->expect(2).size(), 2u);
    peer->send("+OK\r\n-ERR MULTI calls can not be nested\r\n");
    ASSERT_TRUE(run_until([&] { return out.seen.size() == 2; }));
    client.send_oneway(batch_of({{"k", "v"}}), out.oneway_sink("inside"));
    ASSERT_EQ(out.seen.size(), 3u);
    EXPECT_EQ(out.seen[2].rfind("inside error:a MULTI is open", 0), 0u) << "the first MULTI is still open: " << out.seen[2];
}

TEST_F(OnewayBatch, AResetClosesAMulti) {
    FakeServer server;
    Client     client{server.uri()};
    auto       peer = connect(client, server);
    ASSERT_TRUE(peer);
    Outcomes out;

    client.multi(out.status_sink("multi"));
    client.reset(out.status_sink("reset"));
    client.send_oneway(batch_of({{"k", "v"}}), out.oneway_sink("after"));
    EXPECT_EQ(peer->expect(5), (std::vector<Command>{{"MULTI"}, {"RESET"}, off, {"SET", "k", "v"}, on}))
        << "RESET discards the transaction: the batch goes behind it";
}

TEST_F(OnewayBatch, AMultiHeldForTheNextLinkKeepsTheGateClosed) {
    FakeServer server;
    Client     client{server.uri()};
    client.enable_auto_reconnect(qb::redis::RetryPolicy{}.with_initial_delay(50ms).with_max_delay(100ms).with_jitter(false));
    auto first = connect(client, server);
    ASSERT_TRUE(first);
    Outcomes out;

    // A failure callback submits the MULTI again while the lost link is torn down: it is held for the next link.
    client.get(
        [&](qb::redis::Reply<std::optional<std::string>> &&) {
            out.seen.push_back("get failed");
            client.multi(out.status_sink("multi again"));
        },
        "k");
    EXPECT_EQ(first->expect(1).size(), 1u);
    first->sock.disconnect();
    ASSERT_TRUE(run_until([&] { return !out.seen.empty(); }));
    client.send_oneway(batch_of({{"k", "v"}}), out.oneway_sink("batch"));
    ASSERT_EQ(out.seen.size(), 2u);
    EXPECT_EQ(out.seen[1].rfind("batch error:a MULTI is open", 0), 0u) << "the held MULTI goes first on the next link: " << out.seen[1];
}

TEST_F(OnewayBatch, AnEarlierMultisFailureDoesNotCloseALaterOne) {
    FakeServer server;
    Client     client{server.uri()};
    auto       peer = connect(client, server);
    ASSERT_TRUE(peer);
    Outcomes out;

    client.multi(out.status_sink("first"));
    client.discard(out.status_sink("discard"));
    client.multi(out.status_sink("second")); // pipelined: the first MULTI's failure arrives after this one was sent
    EXPECT_EQ(peer->expect(3).size(), 3u);
    peer->send("-NOAUTH Authentication required.\r\n-ERR DISCARD without MULTI\r\n+OK\r\n");
    ASSERT_TRUE(run_until([&] { return out.seen.size() == 3; }));
    client.send_oneway(batch_of({{"k", "v"}}), out.oneway_sink("inside"));
    ASSERT_EQ(out.seen.size(), 4u);
    EXPECT_EQ(out.seen[3].rfind("inside error:a MULTI is open", 0), 0u) << "the second MULTI is open: " << out.seen[3];
}

TEST_F(OnewayBatch, AHandshakeLinkLostWhileAMultiIsHeldKeepsTheGateClosed) {
    FakeServer server;
    Client     client{server.uri()};
    client.with_auth("s3cret");
    client.enable_auto_reconnect(qb::redis::RetryPolicy{}.with_initial_delay(20ms).with_max_delay(50ms).with_jitter(false));
    auto done = std::make_shared<bool>(false);
    client.connect([done](bool) { *done = true; }, client.uri());
    auto first = server.accept();
    ASSERT_TRUE(first);
    EXPECT_EQ(first->expect(1), (std::vector<Command>{{"AUTH", "s3cret"}}));
    first->send("+OK\r\n");
    ASSERT_TRUE(run_until([&] { return *done && client.is_connected(); }));
    Outcomes out;

    first->sock.disconnect();
    ASSERT_TRUE(run_until([&] { return client.is_reconnecting(); }));
    client.multi(out.status_sink("multi")); // held for the next link
    auto second = server.accept();
    ASSERT_TRUE(second);
    EXPECT_EQ(second->expect(1), (std::vector<Command>{{"AUTH", "s3cret"}}));
    second->sock.disconnect(); // that link is lost in its handshake: the MULTI never left, it is still held
    run_until([] { return false; }, 30ms);
    client.send_oneway(batch_of({{"k", "v"}}), out.oneway_sink("batch"));
    ASSERT_FALSE(out.seen.empty());
    EXPECT_EQ(out.seen.back().rfind("batch error:a MULTI is open", 0), 0u) << "the held MULTI goes first on a later link: " << out.seen.back();
}

TEST_F(OnewayBatch, AResetThroughAViewClosesAMulti) {
    // reset() through retrying() or following_redirects() went to the client as a plain command: the marker stayed,
    // every later batch refused until the link was lost (Huly QB-902, second review).
    FakeServer server;
    Client     client{server.uri()};
    auto       peer = connect(client, server);
    ASSERT_TRUE(peer);
    Outcomes                     out;
    qb::redis::node_pool<Client> nodes;

    client.multi(out.status_sink("multi"));
    client.retrying().reset(out.status_sink("reset"));
    client.send_oneway(batch_of({{"k", "v"}}), out.oneway_sink("after"));
    EXPECT_EQ(peer->expect(5), (std::vector<Command>{{"MULTI"}, {"RESET"}, off, {"SET", "k", "v"}, on}));

    client.multi(out.status_sink("multi 2"));
    client.following_redirects(nodes).reset(out.status_sink("reset 2"));
    client.send_oneway(batch_of({{"k", "w"}}), out.oneway_sink("after 2"));
    EXPECT_EQ(peer->expect(5), (std::vector<Command>{{"MULTI"}, {"RESET"}, off, {"SET", "k", "w"}, on}));
}

TEST_F(OnewayBatch, IsInMultiIsOpenedOnlyByAMultiNothingClosedSince) {
    // A MULTI's +OK set is_in_multi() even when a RESET pipelined behind it had already closed the transaction.
    FakeServer server;
    Client     client{server.uri()};
    auto       peer = connect(client, server);
    ASSERT_TRUE(peer);
    Outcomes out;

    client.multi(out.status_sink("multi"));
    client.reset(out.status_sink("reset"));
    EXPECT_EQ(peer->expect(2), (std::vector<Command>{{"MULTI"}, {"RESET"}}));
    peer->send("+OK\r\n+RESET\r\n");
    ASSERT_TRUE(run_until([&] { return out.seen.size() == 2; }));
    EXPECT_FALSE(client.is_in_multi()) << "the RESET submitted after the MULTI closed it";

    // The control: a nested MULTI pipelined behind the first does not close it, and the first one's +OK opens it.
    client.multi(out.status_sink("multi a"));
    client.multi(out.status_sink("multi b"));
    EXPECT_EQ(peer->expect(2), (std::vector<Command>{{"MULTI"}, {"MULTI"}}));
    peer->send("+OK\r\n-ERR MULTI calls can not be nested\r\n");
    ASSERT_TRUE(run_until([&] { return out.seen.size() == 4; }));
    EXPECT_TRUE(client.is_in_multi());
}
