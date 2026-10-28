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
 * @file cluster-redirects.cpp
 * @brief The redirect-following view, `redis.following_redirects(nodes, qb::redis::redirect_policy{...})` (Huly QB-129).
 *
 * System tier: loopback sockets and NO redis daemon. The test is every node: it reads each command off the wire and
 * answers -- a value, a `-MOVED`, an `-ASK` -- as the case scripts, in the formats a Redis 8 cluster sends (measured on
 * a local three-node cluster: `MOVED 15495 127.0.0.1:30003`, `ASK 15495 127.0.0.1:30002`).
 *
 * What is proved:
 *   - a MOVED is followed to the node it names, through the pool's client of that node, which is created once and
 *     reused; the callback is called ONCE with the last outcome; each redirection counts in stats().redirects; the
 *     client is asked first again for the next command (no slot map);
 *   - an ASK is followed by ASKING then the command, back to back on the node's connection;
 *   - a non-idempotent write is followed too (a redirection means the command did not run);
 *   - the redirections are bounded: past max_redirects the last MOVED reaches the caller;
 *   - an empty host is the answering node's host; a `?` host, a resolver that has no node, an ordinary error, a
 *     failure without an answer and a command inside MULTI are the caller's, unchanged, and contact no other node;
 *   - the coroutine form follows the same way.
 */

#include <gtest/gtest.h>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <optional>
#include <string>
#include <vector>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <qb/io/tcp/listener.h>
#include <qb/io/tcp/socket.h>
#include <qbm/redis/redis.h>

using namespace std::chrono_literals;

namespace cluster_redirects_test {

using Client  = qb::redis::tcp::client;
using Nodes   = qb::redis::node_pool<Client>;
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

struct FakeNode {
    qb::io::tcp::listener listener;
    std::uint16_t         port = 0;

    FakeNode() {
        EXPECT_EQ(listener.listen_v4(0, "127.0.0.1"), qb::io::SocketStatus::Done);
        listener.set_nonblocking(true);
        port = listener.local_endpoint().port();
    }
    [[nodiscard]] qb::io::uri
    uri() const {
        return qb::io::uri{"tcp://127.0.0.1:" + std::to_string(port)};
    }
    [[nodiscard]] std::string
    endpoint() const {
        return "127.0.0.1:" + std::to_string(port);
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
    get_sink() {
        return [this](qb::redis::Reply<std::optional<std::string>> &&r) {
            seen.push_back(r.ok() ? "ok:" + r.result().value_or("<nil>") : "error:" + r.error());
        };
    }
    auto
    int_sink() {
        return [this](qb::redis::Reply<long long> &&r) {
            seen.push_back(r.ok() ? "ok:" + std::to_string(r.result()) : "error:" + r.error());
        };
    }
    auto
    status_sink() {
        return [this](qb::redis::Reply<qb::redis::status> &&r) {
            seen.push_back(r.ok() ? std::string{"ok"} : "error:" + r.error());
        };
    }
};

qb::io::async::task<void>
co_get(qb::redis::detail::RedirectingClient<Client> view, std::string *out) {
    auto r = co_await view.get("k");
    *out   = r.ok() ? "ok:" + r.result().value_or("<nil>") : "error:" + r.error();
}

class ClusterRedirects : public ::testing::Test {
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
    connect(Client &client, FakeNode &node) {
        auto done = std::make_shared<bool>(false);
        client.connect([done](bool) { *done = true; }, client.uri());
        auto peer = node.accept();
        EXPECT_TRUE(peer);
        EXPECT_TRUE(run_until([&] { return *done; }));
        return peer;
    }
};

} // namespace cluster_redirects_test

using namespace cluster_redirects_test;

TEST_F(ClusterRedirects, AMovedIsFollowedThroughThePoolsClientOfThatNodeAndCalledOnce) {
    FakeNode a, b;
    Client   client{a.uri()};
    auto     pa = connect(client, a);
    ASSERT_TRUE(pa);
    Nodes    nodes;
    Outcomes out;

    client.following_redirects(nodes).get(out.get_sink(), "k");
    EXPECT_EQ(pa->expect(1), (std::vector<Command>{{"GET", "k"}}));
    pa->send("-MOVED 3999 " + b.endpoint() + "\r\n");
    auto pb = b.accept();
    ASSERT_TRUE(pb) << "the pool connects to the node the MOVED names";
    EXPECT_EQ(pb->expect(1), (std::vector<Command>{{"GET", "k"}}));
    pb->send(bulk("v"));
    ASSERT_TRUE(run_until([&] { return !out.seen.empty(); }));
    EXPECT_EQ(out.seen, (std::vector<std::string>{"ok:v"}));
    EXPECT_EQ(client.stats().redirects, 1u);
    EXPECT_EQ(nodes.size(), 1u);

    // No slot map: the next command asks the client first again, and the node's client is reused.
    client.following_redirects(nodes).get(out.get_sink(), "k");
    EXPECT_EQ(pa->expect(1), (std::vector<Command>{{"GET", "k"}}));
    pa->send("-MOVED 3999 " + b.endpoint() + "\r\n");
    EXPECT_EQ(pb->expect(1), (std::vector<Command>{{"GET", "k"}}));
    pb->send(bulk("w"));
    ASSERT_TRUE(run_until([&] { return out.seen.size() == 2; }));
    EXPECT_EQ(out.seen.back(), "ok:w");
    EXPECT_FALSE(b.accept(200ms)) << "one connection per node";
    EXPECT_EQ(client.stats().redirects, 2u);
}

TEST_F(ClusterRedirects, AnAskSendsAskingThenTheCommandBackToBack) {
    FakeNode a, b;
    Client   client{a.uri()};
    auto     pa = connect(client, a);
    ASSERT_TRUE(pa);
    Nodes    nodes;
    Outcomes out;

    client.following_redirects(nodes).get(out.get_sink(), "k");
    EXPECT_EQ(pa->expect(1), (std::vector<Command>{{"GET", "k"}}));
    pa->send("-ASK 15495 " + b.endpoint() + "\r\n");
    auto pb = b.accept();
    ASSERT_TRUE(pb);
    EXPECT_EQ(pb->expect(2), (std::vector<Command>{{"ASKING"}, {"GET", "k"}}));
    pb->send("+OK\r\n" + bulk("migrating"));
    ASSERT_TRUE(run_until([&] { return !out.seen.empty(); }));
    EXPECT_EQ(out.seen, (std::vector<std::string>{"ok:migrating"}));
    EXPECT_EQ(client.stats().redirects, 1u);
}

TEST_F(ClusterRedirects, ANonIdempotentWriteIsFollowedBecauseItDidNotRun) {
    FakeNode a, b;
    Client   client{a.uri()};
    auto     pa = connect(client, a);
    ASSERT_TRUE(pa);
    Nodes    nodes;
    Outcomes out;

    client.following_redirects(nodes).incr(out.int_sink(), "counter");
    EXPECT_EQ(pa->expect(1), (std::vector<Command>{{"INCR", "counter"}}));
    pa->send("-MOVED 1 " + b.endpoint() + "\r\n");
    auto pb = b.accept();
    ASSERT_TRUE(pb);
    EXPECT_EQ(pb->expect(1), (std::vector<Command>{{"INCR", "counter"}}));
    pb->send(":1\r\n");
    ASSERT_TRUE(run_until([&] { return !out.seen.empty(); }));
    EXPECT_EQ(out.seen, (std::vector<std::string>{"ok:1"}));
}

TEST_F(ClusterRedirects, PastMaxRedirectsTheLastMovedReachesTheCaller) {
    FakeNode a, b;
    Client   client{a.uri()};
    auto     pa = connect(client, a);
    ASSERT_TRUE(pa);
    Nodes    nodes;
    Outcomes out;

    client.following_redirects(nodes, qb::redis::redirect_policy{.max_redirects = 2}).get(out.get_sink(), "k");
    EXPECT_EQ(pa->expect(1).size(), 1u);
    pa->send("-MOVED 3999 " + b.endpoint() + "\r\n"); // 1st, followed
    auto pb = b.accept();
    ASSERT_TRUE(pb);
    EXPECT_EQ(pb->expect(1).size(), 1u);
    pb->send("-MOVED 3999 " + a.endpoint() + "\r\n"); // 2nd, followed: the pool's own client of A
    auto pa2 = a.accept();
    ASSERT_TRUE(pa2);
    EXPECT_EQ(pa2->expect(1).size(), 1u);
    pa2->send("-MOVED 3999 " + b.endpoint() + "\r\n"); // 3rd: past the bound
    ASSERT_TRUE(run_until([&] { return !out.seen.empty(); }));
    EXPECT_EQ(out.seen, (std::vector<std::string>{"error:MOVED 3999 " + b.endpoint()}));
    EXPECT_TRUE(pb->expect(1, 200ms).empty()) << "nothing sent past the bound";
    EXPECT_EQ(client.stats().redirects, 2u);
}

TEST_F(ClusterRedirects, AnEmptyHostIsTheAnsweringNodesHost) {
    FakeNode a, b;
    Client   client{a.uri()};
    auto     pa = connect(client, a);
    ASSERT_TRUE(pa);
    Nodes    nodes;
    Outcomes out;

    client.following_redirects(nodes).get(out.get_sink(), "k");
    EXPECT_EQ(pa->expect(1).size(), 1u);
    pa->send("-MOVED 3999 :" + std::to_string(b.port) + "\r\n");
    auto pb = b.accept();
    ASSERT_TRUE(pb) << "127.0.0.1, the host of the client that answered";
    EXPECT_EQ(pb->expect(1), (std::vector<Command>{{"GET", "k"}}));
    pb->send(bulk("v"));
    ASSERT_TRUE(run_until([&] { return !out.seen.empty(); }));
    EXPECT_EQ(out.seen, (std::vector<std::string>{"ok:v"}));
    EXPECT_NE(nodes.find("127.0.0.1", b.port), nullptr);
}

TEST_F(ClusterRedirects, WhatIsNotAFollowableRedirectionReachesTheCallerUnchanged) {
    FakeNode a, b;
    Client   client{a.uri()};
    auto     pa = connect(client, a);
    ASSERT_TRUE(pa);
    Nodes    nodes;
    Outcomes out;
    auto     none = [](std::string const &, std::uint16_t) -> Client     *{
        return nullptr;
    };

    const std::vector<std::pair<std::string, std::string>> answers = {
        {"-MOVED 3999 ?:" + std::to_string(b.port) + "\r\n", "error:MOVED 3999 ?:" + std::to_string(b.port)}, // unknown endpoint
        {"-ERR boom\r\n", "error:ERR boom"},                                                                  // an ordinary error
        {"-MOVED 99999 " + b.endpoint() + "\r\n", "error:MOVED 99999 " + b.endpoint()},                       // no such slot
    };
    for (auto const &[raw, expected] : answers) {
        client.following_redirects(nodes).get(out.get_sink(), "k");
        EXPECT_EQ(pa->expect(1).size(), 1u);
        pa->send(raw);
        ASSERT_TRUE(run_until([&] { return !out.seen.empty(); }));
        EXPECT_EQ(out.seen, (std::vector<std::string>{expected}));
        out.seen.clear();
    }
    // A resolver that has no node for it.
    client.following_redirects(none).get(out.get_sink(), "k");
    EXPECT_EQ(pa->expect(1).size(), 1u);
    pa->send("-MOVED 3999 " + b.endpoint() + "\r\n");
    ASSERT_TRUE(run_until([&] { return !out.seen.empty(); }));
    EXPECT_EQ(out.seen, (std::vector<std::string>{"error:MOVED 3999 " + b.endpoint()}));
    out.seen.clear();

    EXPECT_FALSE(b.accept(200ms)) << "no other node was contacted";
    EXPECT_EQ(nodes.size(), 0u);
    EXPECT_EQ(client.stats().redirects, 0u);
}

TEST_F(ClusterRedirects, InsideMultiARedirectionIsNotFollowed) {
    FakeNode a, b;
    Client   client{a.uri()};
    auto     pa = connect(client, a);
    ASSERT_TRUE(pa);
    Nodes    nodes;
    Outcomes out;

    client.multi(out.status_sink());
    EXPECT_EQ(pa->expect(1), (std::vector<Command>{{"MULTI"}}));
    pa->send("+OK\r\n");
    ASSERT_TRUE(run_until([&] { return out.seen.size() == 1; }));
    client.following_redirects(nodes).get(out.get_sink(), "k");
    EXPECT_EQ(pa->expect(1), (std::vector<Command>{{"GET", "k"}}));
    pa->send("-MOVED 3999 " + b.endpoint() + "\r\n");
    ASSERT_TRUE(run_until([&] { return out.seen.size() == 2; }));
    EXPECT_EQ(out.seen.back(), "error:MOVED 3999 " + b.endpoint());
    EXPECT_FALSE(b.accept(200ms));
    EXPECT_EQ(client.stats().redirects, 0u);
}

TEST_F(ClusterRedirects, AFailureWithoutAnAnswerIsNotARedirection) {
    FakeNode a;
    Client   client{a.uri()};
    auto     pa = connect(client, a);
    ASSERT_TRUE(pa);
    Nodes    nodes;
    Outcomes out;

    client.following_redirects(nodes).get(out.get_sink(), "k");
    EXPECT_EQ(pa->expect(1).size(), 1u);
    pa->sock.disconnect();
    ASSERT_TRUE(run_until([&] { return !out.seen.empty(); }));
    ASSERT_EQ(out.seen.size(), 1u);
    EXPECT_EQ(out.seen[0].rfind("error:", 0), 0u);
    EXPECT_EQ(nodes.size(), 0u);
}

TEST_F(ClusterRedirects, TheCoroutineFormFollowsTheSameWay) {
    FakeNode a, b;
    Client   client{a.uri()};
    auto     pa = connect(client, a);
    ASSERT_TRUE(pa);
    Nodes       nodes;
    std::string got;

    qb::io::async::coro_scheduler().spawn(co_get(client.following_redirects(nodes), &got));
    EXPECT_EQ(pa->expect(1), (std::vector<Command>{{"GET", "k"}}));
    pa->send("-ASK 77 " + b.endpoint() + "\r\n");
    auto pb = b.accept();
    ASSERT_TRUE(pb);
    EXPECT_EQ(pb->expect(2), (std::vector<Command>{{"ASKING"}, {"GET", "k"}}));
    pb->send("+OK\r\n" + bulk("co"));
    ASSERT_TRUE(run_until([&] { return !got.empty(); }));
    EXPECT_EQ(got, "ok:co");
}

// ---- What an independent review found (the QB-129 lot's review) ----------------------------------------------

TEST_F(ClusterRedirects, ARedirectedCommandIsPendingOnTheOriginAndItsCallbackIsCalledOnce) {
    FakeNode a, b;
    Client   client{a.uri()};
    auto     pa = connect(client, a);
    ASSERT_TRUE(pa);
    Nodes    nodes;
    Outcomes out;

    client.following_redirects(nodes).get(out.get_sink(), "k");
    EXPECT_EQ(pa->expect(1), (std::vector<Command>{{"GET", "k"}}));
    pa->send("-MOVED 3999 " + b.endpoint() + "\r\n");
    auto pb = b.accept();
    ASSERT_TRUE(pb);
    EXPECT_EQ(pb->expect(1), (std::vector<Command>{{"GET", "k"}}));
    EXPECT_EQ(client.pending_reply_count(), 1u) << "on b's queue, held by the view: the origin's await() waits for it";
    pb->send(bulk("v"));
    ASSERT_TRUE(run_until([&] { return !out.seen.empty(); }));
    EXPECT_EQ(client.pending_reply_count(), 0u);
    run_until([] { return false; }, 150ms);
    EXPECT_EQ(out.seen, (std::vector<std::string>{"ok:v"})) << "once, and not again";
}

TEST_F(ClusterRedirects, AnOriginGoneWithAHopInFlightDropsTheCallbackAsTheClientDoes) {
    FakeNode a, b;
    auto     client = std::make_unique<Client>(a.uri());
    auto     pa     = connect(*client, a);
    ASSERT_TRUE(pa);
    Nodes nodes;
    auto  calls = std::make_shared<int>(0);

    client->following_redirects(nodes).get([calls](qb::redis::Reply<std::optional<std::string>> &&) { ++*calls; }, "k");
    EXPECT_EQ(pa->expect(1).size(), 1u);
    pa->send("-MOVED 3999 " + b.endpoint() + "\r\n");
    auto pb = b.accept();
    ASSERT_TRUE(pb);
    EXPECT_EQ(pb->expect(1), (std::vector<Command>{{"GET", "k"}}));
    client.reset(); // the client the command was sent through is gone while it waits on b
    pb->send(bulk("v"));
    run_until([] { return false; }, 200ms);
    EXPECT_EQ(*calls, 0) << "dropped, never called: what a destroyed client does with its own pending commands";
}

TEST_F(ClusterRedirects, ANodePoolGoneWithAHopInFlightFailsIt) {
    FakeNode a, b;
    Client   client{a.uri()};
    auto     pa = connect(client, a);
    ASSERT_TRUE(pa);
    auto     nodes = std::make_unique<Nodes>();
    Outcomes out;

    client.following_redirects(*nodes).get(out.get_sink(), "k");
    EXPECT_EQ(pa->expect(1).size(), 1u);
    pa->send("-MOVED 3999 " + b.endpoint() + "\r\n");
    auto pb = b.accept();
    ASSERT_TRUE(pb);
    EXPECT_EQ(pb->expect(1).size(), 1u);
    nodes.reset(); // its node client is destroyed with the GET in flight
    ASSERT_TRUE(run_until([&] { return !out.seen.empty(); })) << "it vanished with the node's client before";
    EXPECT_EQ(out.seen, (std::vector<std::string>{"error:disconnected"}));
    EXPECT_EQ(client.pending_reply_count(), 0u);
}

TEST_F(ClusterRedirects, AResolverThatThrowsHandsTheRedirectionToTheCaller) {
    FakeNode a;
    Client   client{a.uri()};
    auto     pa = connect(client, a);
    ASSERT_TRUE(pa);
    Outcomes out;

    client.following_redirects([](std::string const &, std::uint16_t) -> Client * { throw std::runtime_error("no such node"); })
        .get(out.get_sink(), "k");
    EXPECT_EQ(pa->expect(1).size(), 1u);
    pa->send("-MOVED 3999 10.0.0.9:7000\r\n");
    ASSERT_TRUE(run_until([&] { return !out.seen.empty(); })) << "the callback was lost with the throw before";
    EXPECT_EQ(out.seen, (std::vector<std::string>{"error:MOVED 3999 10.0.0.9:7000"}));
    EXPECT_EQ(client.stats().redirects, 0u);
}

TEST_F(ClusterRedirects, NoRedirectionIsFollowedWithMaxRedirectsZero) {
    FakeNode a, b;
    Client   client{a.uri()};
    auto     pa = connect(client, a);
    ASSERT_TRUE(pa);
    Nodes    nodes;
    Outcomes out;

    client.following_redirects(nodes, qb::redis::redirect_policy{.max_redirects = 0}).get(out.get_sink(), "k");
    EXPECT_EQ(pa->expect(1).size(), 1u);
    pa->send("-MOVED 3999 " + b.endpoint() + "\r\n");
    ASSERT_TRUE(run_until([&] { return !out.seen.empty(); }));
    EXPECT_EQ(out.seen, (std::vector<std::string>{"error:MOVED 3999 " + b.endpoint()}));
    EXPECT_FALSE(b.accept(200ms));
}

TEST_F(ClusterRedirects, AnAskToANodeAlreadyConnectedSendsAskingAndTheCommandBackToBack) {
    FakeNode a, b;
    Client   client{a.uri()};
    auto     pa = connect(client, a);
    ASSERT_TRUE(pa);
    Nodes    nodes;
    Outcomes out;

    client.following_redirects(nodes).get(out.get_sink(), "k");
    EXPECT_EQ(pa->expect(1).size(), 1u);
    pa->send("-MOVED 3999 " + b.endpoint() + "\r\n");
    auto pb = b.accept();
    ASSERT_TRUE(pb);
    EXPECT_EQ(pb->expect(1).size(), 1u);
    pb->send(bulk("v"));
    ASSERT_TRUE(run_until([&] { return out.seen.size() == 1; }));

    client.following_redirects(nodes).get(out.get_sink(), "k2"); // b's client is ready now: the pair goes on its link
    EXPECT_EQ(pa->expect(1), (std::vector<Command>{{"GET", "k2"}}));
    pa->send("-ASK 15495 " + b.endpoint() + "\r\n");
    EXPECT_EQ(pb->expect(2), (std::vector<Command>{{"ASKING"}, {"GET", "k2"}}));
    pb->send("+OK\r\n" + bulk("w"));
    ASSERT_TRUE(run_until([&] { return out.seen.size() == 2; }));
    EXPECT_EQ(out.seen.back(), "ok:w");
}

TEST_F(ClusterRedirects, TheCoroutineFormFollowsAMoved) {
    FakeNode a, b;
    Client   client{a.uri()};
    auto     pa = connect(client, a);
    ASSERT_TRUE(pa);
    Nodes       nodes;
    std::string got;

    qb::io::async::coro_scheduler().spawn(co_get(client.following_redirects(nodes), &got));
    EXPECT_EQ(pa->expect(1).size(), 1u);
    pa->send("-MOVED 3999 " + b.endpoint() + "\r\n");
    auto pb = b.accept();
    ASSERT_TRUE(pb);
    EXPECT_EQ(pb->expect(1), (std::vector<Command>{{"GET", "k"}}));
    pb->send(bulk("co"));
    ASSERT_TRUE(run_until([&] { return !got.empty(); }));
    EXPECT_EQ(got, "ok:co");
}

TEST_F(ClusterRedirects, ACommandBehindASubmittedMultiIsPassedThroughBeforeTheMultiIsAnswered) {
    // is_in_multi() turns true only at MULTI's reply: a command pipelined behind it before that was followed out of
    // the transaction. The view now reads the MULTI submitted (Huly QB-902's marker).
    FakeNode a, b;
    Client   client{a.uri()};
    auto     pa = connect(client, a);
    ASSERT_TRUE(pa);
    Nodes    nodes;
    Outcomes out;

    client.multi(out.status_sink());
    client.following_redirects(nodes).get(out.get_sink(), "k");
    EXPECT_EQ(pa->expect(2), (std::vector<Command>{{"MULTI"}, {"GET", "k"}}));
    pa->send("+OK\r\n-MOVED 3999 " + b.endpoint() + "\r\n");
    ASSERT_TRUE(run_until([&] { return out.seen.size() == 2; }));
    EXPECT_EQ(out.seen[1], "error:MOVED 3999 " + b.endpoint());
    EXPECT_FALSE(b.accept(200ms));
}
