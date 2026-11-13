/*
 * qb - C++ Actor Framework
 * Copyright (c) 2011-2026 qb - isndev (cpp.actor). All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * See the License for the specific terms.
 */

/**
 * @file client-static-exit.cpp
 * @brief A client of this module with STATIC STORAGE ends cleanly when the process exits (Huly QB-1246): the plain
 *        client, a `node_pool`, the two views, the two consumers -- never connected, a connect in flight, a
 *        handshake in flight, ready and idle, a command in flight (its callback pending, or a coroutine parked on
 *        it), disconnected by the program, a reconnection run under way.
 *
 * `exit()` destroys the main thread's thread-locals before the objects with static storage, and qb-io's
 * `listener::current` is one: a client a program keeps in static storage is destroyed AFTER its thread's event loop,
 * where nothing can be registered, armed or resumed any more -- a request for the loop there stops the process on a
 * line that names it, and a watcher started there writes into the freed loop. Each case asks, of one client in one
 * state: the process ends with the code its judge chose and its stderr is the judge's line and NOTHING else (no named
 * stop, no warning, no sanitizer report: under AddressSanitizer the destroyed listener is poisoned); the client's
 * destructor ran after the loop -- asserted, not assumed -- and returned; the peer of a connection reads a clean EOF,
 * never a reset; what was in flight ended as the module says it does (the judge's line carries how many times its
 * callback ran, whether after the loop, and with what: 1 a reply, 2 "disconnected", 3 another failure; whether the
 * coroutine parked on it was resumed, and when its frame was destroyed); LeakSanitizer, asked by the judge, has
 * nothing to report.
 *
 * Every case is a CHILD process (a death test, re-executed) whose judge is an `atexit` handler registered before the
 * object with static storage is built. The peer is a relay the case steps by hand (qb's `static_exit_support.h`):
 * by default it is a scripted server that answers every command, so the binary needs no daemon; built with
 * `QB_STATIC_EXIT_DAEMON` -- the `integration` tier's copy -- every octet goes through it to the redis-server that
 * `REDIS_URI` names. A case that stops stepping the relay has a peer that is open and silent.
 */

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <qb/io/uri.h>

#include <qbm/redis/redis.h>

#ifdef QB_STATIC_EXIT_DAEMON
#include "../../shared/redis_integration_fixture.h"
#endif
#include "static_exit_support.h"

using namespace std::chrono_literals;

namespace client_static_exit_test {

namespace async = qb::io::async;
using namespace qb::io::test::static_exit;

using client = qb::redis::tcp::client;

/// What a reply's callback received: 1 a reply, 2 "disconnected", 3 another failure.
template <typename Reply>
int
outcome_of(Reply const &reply) {
    if (reply.ok())
        return 1;
    return reply.error() == "disconnected" ? 2 : 3;
}

/// A scripted redis-server: every command a client sends in full is answered -- PING with PONG, SUBSCRIBE with the
/// acknowledgement of each channel, the rest with OK.
std::string
answer_commands(std::string &unread) {
    std::string out;
    for (;;) {
        if (unread.empty() || unread[0] != '*')
            return out;
        const auto head = unread.find("\r\n");
        if (head == std::string::npos)
            return out;
        const int   parts = std::atoi(unread.c_str() + 1);
        std::size_t at    = head + 2;
        std::string verb;
        std::string acknowledged;
        for (int i = 0; i < parts; ++i) {
            if (at >= unread.size() || unread[at] != '$')
                return out;
            const auto end = unread.find("\r\n", at);
            if (end == std::string::npos)
                return out;
            const auto size = static_cast<std::size_t>(std::atoi(unread.c_str() + at + 1));
            if (unread.size() < end + 2 + size + 2)
                return out;
            if (i == 0)
                verb = unread.substr(end + 2, size);
            else
                acknowledged += "*3\r\n$9\r\nsubscribe\r\n$" + std::to_string(size) + "\r\n" + unread.substr(end + 2, size)
                                + "\r\n:" + std::to_string(i) + "\r\n";
            at = end + 2 + size + 2;
        }
        unread.erase(0, at);
        out += verb == "SUBSCRIBE" ? acknowledged : verb == "PING" ? "+PONG\r\n" : "+OK\r\n";
    }
}

#ifdef QB_STATIC_EXIT_DAEMON
/// Where the redis-server of the integration tier listens (`REDIS_URI`), as an IPv4 host and a port.
std::pair<std::string, std::uint16_t>
daemon_address() {
    char const       *env = std::getenv("REDIS_URI");
    const qb::io::uri daemon{env && *env ? env : "tcp://localhost:6379"};
    return {std::string{daemon.host()} == "localhost" ? "127.0.0.1" : std::string{daemon.host()}, daemon.u_port()};
}
#endif

/// The peer of a case, and the address a client is given for it.
relay &
redis_peer() {
    relay &peer = new_peer();
#ifdef QB_STATIC_EXIT_DAEMON
    const auto daemon = daemon_address();
    peer.through(daemon.first, daemon.second);
#else
    peer.answer(answer_commands);
#endif
    return peer;
}

#ifdef QB_STATIC_EXIT_DAEMON
/// The daemon serves the cases: it answers a client connected as the cases connect theirs, to its host and port
/// alone, which is what the tier's own fixture asks (`redis_try_connect`). Asked once, by the process that runs the
/// cases.
[[nodiscard]] bool
daemon_serves() {
    return asked_once("QB_STATIC_EXIT_REDIS_SERVES", [] {
        const auto daemon = daemon_address();
        client     probe{qb::io::uri{"tcp://" + daemon.first + ":" + std::to_string(daemon.second)}};
        return qb::redis::test::redis_try_connect(probe);
    });
}
#endif

/// The fixture of the cases here: the integration tier's copy skips, it does not fail, without a daemon that serves
/// it.
template <typename Base>
class with_peer : public Base {
protected:
    void
    SetUp() override {
        Base::SetUp();
#ifdef QB_STATIC_EXIT_DAEMON
        if (!daemon_serves()) {
            const auto daemon = daemon_address();
            GTEST_SKIP() << qb::redis::test::kDaemonUnreachableSentinel << " (redis at " << daemon.first << ":" << daemon.second
                         << " not reachable)";
        }
#endif
    }
};

qb::io::uri
address(relay const &peer) {
    return qb::io::uri{"tcp://127.0.0.1:" + std::to_string(peer.port())};
}

template <typename Client>
void
make_ready(Client &redis, relay &peer) {
    bool answered = false;
    bool up       = false;
    redis.connect([&](bool ok) {
        answered = true;
        up       = ok;
    });
    if (!pump_until(&peer, [&] { return answered; }) || !up)
        setup_failed("the client never became ready");
    if (redis.state() != qb::io::connection_state::ready)
        setup_failed("the client is not ready");
    g.want_peer = 0;
}

void
on_pong(qb::redis::Reply<std::string> &&reply) {
    called_with(outcome_of(reply));
}

async::task<void>
parked_on_ping(client &redis) {
    frame_local local;
    g.parked   = true;
    auto reply = co_await redis.ping();
    ++g.resumed;
    g.outcome = outcome_of(reply);
}

int g_retry_waits = 0; ///< waits the reconnection run announced (`on_retry`)

qb::redis::RetryPolicy
one_hour_between_attempts() {
    qb::redis::RetryPolicy policy;
    policy.with_initial_delay(1h).with_max_delay(1h).with_jitter(false).with_connect_timeout(2s);
    policy.on_retry = [](int, qb::duration) {
        ++g_retry_waits;
    };
    return policy;
}

// ======================================================================================================================
// qb::redis::tcp::client
// ======================================================================================================================

enum class state {
    never_connected,
    connecting,          ///< connect() asked, no pass of the loop since
    handshake,           ///< the transport is up, the SELECT of the handshake unanswered
    ready,               ///< ready, nothing asked
    command,             ///< a command written, its callback pending
    coroutine,           ///< a coroutine parked on a command
    retrying_command,    ///< a command written through the retrying view
    own_callback,        ///< `exit()` called by the callback of a reply
    disconnected,        ///< disconnect(), then `exit()`
    reconnecting,        ///< the server gone, the run waiting for its next attempt
    reconnecting_held,   ///< ... with a command submitted meanwhile, held for the next connection
    reconnect_handshake, ///< the server back, the run's handshake unanswered
    retry_waiting,       ///< the retrying view waiting to issue again a command the lost link failed
};

[[noreturn]] void
run_client(state const st, site const where) {
    std::atexit(judge); // first: it runs after the destructor of `object`
    relay                 &peer = redis_peer();
    static at_exit<client> object{address(peer)};
    client                &redis = object.client;
    switch (st) {
        case state::never_connected:
            break;
        case state::connecting:
            // Not counted: the flow connect() spawned has not run yet, and the passes the listener's teardown makes
            // may run it -- to ready, on a loopback that answers at once -- or not.
            redis.connect([](bool) {});
            break;
        case state::handshake:
            redis.with_select(0);
            peer.go_silent(); // the port completes the TCP handshake; nobody answers the SELECT
            redis.connect([](bool) { ++g.calls; });
            if (!pump_until(nullptr, [&] { return redis.state() == qb::io::connection_state::handshaking; }))
                setup_failed("the client never reached its handshake");
            pump_for(nullptr, 20ms);
            break;
        case state::ready:
            make_ready(redis, peer);
            break;
        case state::command:
            make_ready(redis, peer);
            peer.go_silent();
            redis.ping(on_pong);
            pump_for(nullptr, 20ms);
            break;
        case state::coroutine:
            make_ready(redis, peer);
            peer.go_silent();
            async::coro_scheduler().spawn(parked_on_ping(redis));
            if (!pump_until(nullptr, [] { return g.parked; }))
                setup_failed("the coroutine never reached its command");
            pump_for(nullptr, 20ms);
            break;
        case state::retrying_command:
            make_ready(redis, peer);
            peer.go_silent();
            redis.retrying().ping(on_pong);
            pump_for(nullptr, 20ms);
            break;
        case state::own_callback:
            make_ready(redis, peer);
            redis.ping([](qb::redis::Reply<std::string> &&reply) {
                called_with(outcome_of(reply));
                std::exit(3); // inside the client's own dispatch: its reply handler, its protocol, its io handler
            });
            pump_for(&peer, 30s);
            setup_failed("the reply never came");
        case state::disconnected:
            make_ready(redis, peer);
            redis.disconnect();
            break;
        case state::reconnecting:
        case state::reconnecting_held:
            redis.enable_auto_reconnect(one_hour_between_attempts());
            make_ready(redis, peer);
            g.want_peer = -1;
            peer.go_away(); // the connection drops and the port refuses the next: attempt 1 fails, the run waits an hour
            if (!pump_until(nullptr, [&] { return g_retry_waits >= 1 && redis.state() == qb::io::connection_state::reconnecting; }))
                setup_failed("the reconnection run never waited for its second attempt");
            if (st == state::reconnecting_held)
                redis.ping(on_pong); // held until the next connection
            pump_for(nullptr, 20ms);
            break;
        case state::reconnect_handshake:
            redis.with_select(0);
            redis.enable_auto_reconnect(one_hour_between_attempts());
            make_ready(redis, peer);
            peer.hold_new_connections(); // the server comes back deaf: the next connection's SELECT stays unanswered
            peer.drop_client();
            g.want_peer = -1; // the connection the relay accepted is gone; the next one stays in its backlog
            if (!pump_until(&peer, [&] { return redis.is_reconnecting() && redis.state() == qb::io::connection_state::handshaking; }))
                setup_failed("the reconnection run never reached its handshake");
            pump_for(nullptr, 20ms);
            break;
        case state::retry_waiting: {
            redis.enable_auto_reconnect(one_hour_between_attempts());
            make_ready(redis, peer);
            g.want_peer = -1;
            peer.go_silent();
            qb::redis::command_retry retry;
            retry.backoff.with_initial_delay(1h).with_max_delay(1h).with_jitter(false);
            redis.retrying(retry).ping(on_pong); // in flight on the link the server is about to drop
            pump_for(nullptr, 20ms);
            peer.go_away();
            if (!pump_until(nullptr, [&] { return g_retry_waits >= 1 && redis.state() == qb::io::connection_state::reconnecting; }))
                setup_failed("the reconnection run never waited for its second attempt");
            pump_for(nullptr, 20ms); // the view's retry waits its hour
            break;
        }
    }
    leave(where);
}

class RedisClientStaticExitDeathTest : public with_peer<in_a_child_from> {};

TEST_P(RedisClientStaticExitDeathTest, NeverConnected) {
    EXPECT_EXIT(run_client(state::never_connected, GetParam()), ::testing::ExitedWithCode(0), stderr_is(verdict(-1, 0, 0, 0, 0, 0, 0)));
}

TEST_P(RedisClientStaticExitDeathTest, InItsHandshake) {
    EXPECT_EXIT(run_client(state::handshake, GetParam()), ::testing::ExitedWithCode(0), stderr_is(verdict(-1, 0, 0, 0, 0, 0, 0)));
}

TEST_P(RedisClientStaticExitDeathTest, ReadyAndIdle) {
    EXPECT_EXIT(run_client(state::ready, GetParam()), ::testing::ExitedWithCode(0), stderr_is(verdict(0, 0, 0, 0, 0, 0, 0)));
}

TEST_P(RedisClientStaticExitDeathTest, ACommandInFlight) {
    EXPECT_EXIT(run_client(state::command, GetParam()), ::testing::ExitedWithCode(0), stderr_is(verdict(0, 0, 0, 0, 0, 0, 0)));
}

TEST_P(RedisClientStaticExitDeathTest, ACoroutineParkedOnACommand) {
    EXPECT_EXIT(run_client(state::coroutine, GetParam()), ::testing::ExitedWithCode(0), stderr_is(verdict(0, 0, 0, 0, 0, 1, 0)));
}

TEST_P(RedisClientStaticExitDeathTest, ACommandInFlightThroughTheRetryingView) {
    EXPECT_EXIT(run_client(state::retrying_command, GetParam()), ::testing::ExitedWithCode(0), stderr_is(verdict(0, 0, 0, 0, 0, 0, 0)));
}

TEST_P(RedisClientStaticExitDeathTest, Disconnected) {
    EXPECT_EXIT(run_client(state::disconnected, GetParam()), ::testing::ExitedWithCode(0), stderr_is(verdict(0, 0, 0, 0, 0, 0, 0)));
}

TEST_P(RedisClientStaticExitDeathTest, InAReconnectionRun) {
    EXPECT_EXIT(run_client(state::reconnecting, GetParam()), ::testing::ExitedWithCode(0), stderr_is(verdict(-1, 0, 0, 0, 0, 0, 0)));
}

TEST_P(RedisClientStaticExitDeathTest, InAReconnectionRunWithACommandHeld) {
    EXPECT_EXIT(run_client(state::reconnecting_held, GetParam()), ::testing::ExitedWithCode(0), stderr_is(verdict(-1, 0, 0, 0, 0, 0, 0)));
}

TEST_P(RedisClientStaticExitDeathTest, InTheHandshakeOfAReconnectionRun) {
    EXPECT_EXIT(run_client(state::reconnect_handshake, GetParam()), ::testing::ExitedWithCode(0), stderr_is(verdict(-1, 0, 0, 0, 0, 0, 0)));
}

TEST_P(RedisClientStaticExitDeathTest, WithARetryOfTheViewWaiting) {
    EXPECT_EXIT(run_client(state::retry_waiting, GetParam()), ::testing::ExitedWithCode(0), stderr_is(verdict(-1, 0, 0, 0, 0, 0, 0)));
}

INSTANTIATE_TEST_SUITE_P(Exit, RedisClientStaticExitDeathTest, ::testing::Values(site::main, site::callback), site_name);

class RedisClientStaticExitOnceDeathTest : public with_peer<in_a_child> {};

// A connect() asked and no pass of the loop since: the coroutine that runs it has not started.
TEST_F(RedisClientStaticExitOnceDeathTest, AConnectInFlight) {
    EXPECT_EXIT(run_client(state::connecting, site::main), ::testing::ExitedWithCode(0), stderr_is(verdict(-1, 0, 0, 0, 0, 0, 0)));
}

// `exit()` from the callback of a reply: the client's own dispatch is on the stack when its destructor runs.
TEST_F(RedisClientStaticExitOnceDeathTest, ExitFromTheCallbackOfAReply) {
    EXPECT_EXIT(run_client(state::own_callback, site::main), ::testing::ExitedWithCode(0), stderr_is(verdict(0, 1, 0, 1, 0, 0, 0)));
}

// A coroutine that calls `exit()`: the thread's scheduler is destroyed from inside one of its coroutines, abandons the
// two it owns and says so; the frame parked on the command is never destroyed.
TEST_F(RedisClientStaticExitOnceDeathTest, ACoroutineParkedOnACommandAndExitFromACoroutine) {
    EXPECT_EXIT(run_client(state::coroutine, site::coroutine), ::testing::ExitedWithCode(0),
                stderr_is(verdict(0, 0, 0, 0, 0, 0, 0), abandoned(2)));
}

// ======================================================================================================================
// qb::redis::node_pool: the clients of a cluster's nodes, disconnected by the pool's destructor
// ======================================================================================================================

enum class pool { empty, ready, command, resubmitted_command, retrying_command };

client *g_node = nullptr;

[[noreturn]] void
run_pool(pool const st, site const where) {
    std::atexit(judge);
    relay                                       &peer = redis_peer();
    static at_exit<qb::redis::node_pool<client>> object;
    if (st != pool::empty) {
        client *node = object.client.get("127.0.0.1", peer.port());
        if (!pump_until(&peer, [&] { return node->state() == qb::io::connection_state::ready; }))
            setup_failed("the node's client never became ready");
        g.want_peer = 0;
        if (st != pool::ready) {
            peer.go_silent();
            g_node = node;
            if (st == pool::command)
                node->ping(on_pong);
            else if (st == pool::resubmitted_command)
                node->ping([](qb::redis::Reply<std::string> &&reply) { // the retry pattern: what failed is submitted again
                    called_with(outcome_of(reply));
                    g_node->ping(on_pong);
                });
            else
                node->retrying().ping(on_pong);
            pump_for(nullptr, 20ms);
        }
    }
    leave(where);
}

class RedisNodePoolStaticExitDeathTest : public with_peer<in_a_child_from> {};

TEST_P(RedisNodePoolStaticExitDeathTest, Empty) {
    EXPECT_EXIT(run_pool(pool::empty, GetParam()), ::testing::ExitedWithCode(0), stderr_is(verdict(-1, 0, 0, 0, 0, 0, 0)));
}

TEST_P(RedisNodePoolStaticExitDeathTest, ANodeReadyAndIdle) {
    EXPECT_EXIT(run_pool(pool::ready, GetParam()), ::testing::ExitedWithCode(0), stderr_is(verdict(0, 0, 0, 0, 0, 0, 0)));
}

// The pool's destructor disconnects its nodes: what was in flight on one fails, once, with "disconnected".
TEST_P(RedisNodePoolStaticExitDeathTest, ACommandInFlightOnANode) {
    EXPECT_EXIT(run_pool(pool::command, GetParam()), ::testing::ExitedWithCode(0), stderr_is(verdict(0, 1, 1, 2, 0, 0, 0)));
}

// The retry pattern, from the pool's destructor after the loop: the client is down, so the command submitted again is
// refused -- from the loop, which is gone: its failure is dropped with what was deferred, and nothing is asked.
TEST_P(RedisNodePoolStaticExitDeathTest, ACommandInFlightOnANodeWhoseCallbackSubmitsAgain) {
    EXPECT_EXIT(run_pool(pool::resubmitted_command, GetParam()), ::testing::ExitedWithCode(0), stderr_is(verdict(0, 1, 1, 2, 0, 0, 0)));
}

TEST_P(RedisNodePoolStaticExitDeathTest, ACommandInFlightOnANodeThroughTheRetryingView) {
    EXPECT_EXIT(run_pool(pool::retrying_command, GetParam()), ::testing::ExitedWithCode(0), stderr_is(verdict(0, 1, 1, 2, 0, 0, 0)));
}

INSTANTIATE_TEST_SUITE_P(Exit, RedisNodePoolStaticExitDeathTest, ::testing::Values(site::main, site::callback), site_name);

#ifndef QB_STATIC_EXIT_DAEMON

// ======================================================================================================================
// The redirect-following view: a command a cluster redirected, in flight on another node's client (scripted peers only:
// a single redis-server redirects nothing)
// ======================================================================================================================

[[noreturn]] void
run_redirected(site const where) {
    std::atexit(judge);
    relay &seed = new_peer(); // the node the command is sent to: it answers MOVED
    relay &node = new_peer(); // the node it is redirected to: it accepts, and never answers (the judge reads this one)
    seed.answer([port = node.port()](std::string &unread) {
        std::string out;
        if (unread.find("GET") != std::string::npos)
            out = "-MOVED 42 127.0.0.1:" + std::to_string(port) + "\r\n";
        unread.clear();
        return out;
    });
    static at_exit<client>              object{address(seed)};
    static qb::redis::node_pool<client> nodes; // built after the client: destroyed before it, its node disconnected
    make_ready(object.client, seed);
    object.client.following_redirects(nodes).get([](auto &&reply) { called_with(outcome_of(reply)); }, "key");
    if (!pump_until(&seed, [&] {
            node.step();
            return node.from_client() != 0;
        }))
        setup_failed("the redirected command never reached the second node");
    g.want_peer = 0;
    leave(where);
}

class RedisRedirectStaticExitDeathTest : public in_a_child_from {};

// The pool goes first and disconnects the node: the redirected command fails once, with "disconnected", after the
// loop -- the view's caller hears -- and the origin, destroyed after it, has nothing left to discard.
TEST_P(RedisRedirectStaticExitDeathTest, ARedirectedCommandInFlightOnAnotherNode) {
    EXPECT_EXIT(run_redirected(GetParam()), ::testing::ExitedWithCode(0), stderr_is(verdict(0, 1, 1, 2, 0, 0, 0)));
}

INSTANTIATE_TEST_SUITE_P(Exit, RedisRedirectStaticExitDeathTest, ::testing::Values(site::main, site::callback), site_name);

#endif // QB_STATIC_EXIT_DAEMON

// ======================================================================================================================
// The consumers: cb_consumer and co_consumer
// ======================================================================================================================

async::task<void>
parked_on_receive(qb::redis::tcp::co_consumer &consumer) {
    frame_local local;
    g.parked     = true;
    auto message = co_await consumer.receive();
    static_cast<void>(message);
    ++g.resumed;
}

/// What a consumer says, as the judge's outcome: none of it may come from a destructor.
enum : int { consumer_lost_its_link = 4, consumer_error = 5, consumer_message = 6 };

enum class consumer {
    callback_never_connected,
    callback_ready,
    coroutine_ready,
    coroutine_parked,
    subscribing,  ///< a SUBSCRIBE written, its acknowledgement pending
    subscribed,   ///< a subscription acknowledged, no message yet
    disconnected, ///< subscribed, then disconnect() and `exit()`
    reconnecting, ///< subscribed, the server gone, the run waiting for its next attempt
};

/// A consumer of either kind in one of the states they share.
template <typename Consumer>
[[noreturn]] void
drive_consumer(Consumer &redis, relay &peer, consumer const st, site const where) {
    static bool acknowledged = false;
    if (st == consumer::reconnecting)
        redis.enable_auto_reconnect(one_hour_between_attempts());
    make_ready(redis, peer);
    if (st == consumer::subscribing)
        peer.go_silent(); // the SUBSCRIBE stays unread: its acknowledgement never comes
    redis.subscribe([](qb::redis::Reply<qb::redis::subscription> &&reply) { acknowledged = reply.ok(); }, "news");
    if (st == consumer::subscribing) {
        pump_for(nullptr, 20ms);
        leave(where);
    }
    if (!pump_until(&peer, [] { return acknowledged; }))
        setup_failed("the subscription was never acknowledged");
    if (st == consumer::disconnected)
        redis.disconnect();
    if (st == consumer::reconnecting) {
        g.want_peer = -1;
        peer.go_away(); // the connection drops and the port refuses the next: attempt 1 fails, the run waits an hour
        if (!pump_until(nullptr, [&] { return g_retry_waits >= 1 && redis.state() == qb::io::connection_state::reconnecting; }))
            setup_failed("the reconnection run never waited for its second attempt");
        pump_for(nullptr, 20ms);
    }
    leave(where);
}

[[noreturn]] void
run_consumer(consumer const st, site const where) {
    std::atexit(judge);
    relay &peer = redis_peer();
    if (st == consumer::callback_never_connected || st == consumer::callback_ready) {
        static at_exit<qb::redis::tcp::cb_consumer> object{address(peer)};
        if (st == consumer::callback_ready)
            make_ready(object.client, peer);
        leave(where);
    }
    static at_exit<qb::redis::tcp::co_consumer> object{address(peer)};
    make_ready(object.client, peer);
    if (st == consumer::coroutine_parked) {
        async::coro_scheduler().spawn(parked_on_receive(object.client));
        if (!pump_until(&peer, [] { return g.parked; }))
            setup_failed("the coroutine never reached receive()");
        pump_for(&peer, 20ms);
    }
    leave(where);
}

[[noreturn]] void
run_callback_consumer(consumer const st, site const where) {
    std::atexit(judge);
    relay                                      &peer = redis_peer();
    static at_exit<qb::redis::tcp::cb_consumer> object{
        address(peer), [](qb::redis::message &&) { called_with(consumer_message); }, [](qb::redis::error &&) { called_with(consumer_error); },
        [](qb::io::down_reason) { called_with(consumer_lost_its_link); }
    };
    drive_consumer(object.client, peer, st, where);
}

[[noreturn]] void
run_coroutine_consumer(consumer const st, site const where) {
    std::atexit(judge);
    relay                                      &peer = redis_peer();
    static at_exit<qb::redis::tcp::co_consumer> object{address(peer)};
    object.client.on_disconnected([](qb::io::down_reason) { called_with(consumer_lost_its_link); });
    drive_consumer(object.client, peer, st, where);
}

class RedisConsumerStaticExitDeathTest : public with_peer<in_a_child_from> {};

TEST_P(RedisConsumerStaticExitDeathTest, CallbackConsumerNeverConnected) {
    EXPECT_EXIT(run_consumer(consumer::callback_never_connected, GetParam()), ::testing::ExitedWithCode(0),
                stderr_is(verdict(-1, 0, 0, 0, 0, 0, 0)));
}

TEST_P(RedisConsumerStaticExitDeathTest, CallbackConsumerReady) {
    EXPECT_EXIT(run_consumer(consumer::callback_ready, GetParam()), ::testing::ExitedWithCode(0), stderr_is(verdict(0, 0, 0, 0, 0, 0, 0)));
}

TEST_P(RedisConsumerStaticExitDeathTest, CoroutineConsumerReady) {
    EXPECT_EXIT(run_consumer(consumer::coroutine_ready, GetParam()), ::testing::ExitedWithCode(0), stderr_is(verdict(0, 0, 0, 0, 0, 0, 0)));
}

TEST_P(RedisConsumerStaticExitDeathTest, ACoroutineParkedOnReceive) {
    EXPECT_EXIT(run_consumer(consumer::coroutine_parked, GetParam()), ::testing::ExitedWithCode(0), stderr_is(verdict(0, 0, 0, 0, 0, 1, 0)));
}

// A SUBSCRIBE in flight: the consumer destroyed, its acknowledgement's callback is dropped with it.
TEST_P(RedisConsumerStaticExitDeathTest, CallbackConsumerWithASubscribeInFlight) {
    EXPECT_EXIT(run_callback_consumer(consumer::subscribing, GetParam()), ::testing::ExitedWithCode(0),
                stderr_is(verdict(0, 0, 0, 0, 0, 0, 0)));
}

// Subscribed and waiting for a message: nothing is said by the destructor -- no message, no error, no link lost.
TEST_P(RedisConsumerStaticExitDeathTest, CallbackConsumerSubscribed) {
    EXPECT_EXIT(run_callback_consumer(consumer::subscribed, GetParam()), ::testing::ExitedWithCode(0), stderr_is(verdict(0, 0, 0, 0, 0, 0, 0)));
}

// disconnect() says the link is lost (`on_disconnected`) in the call, before the process ends; the destructor adds
// nothing to it.
TEST_P(RedisConsumerStaticExitDeathTest, CallbackConsumerDisconnected) {
    EXPECT_EXIT(run_callback_consumer(consumer::disconnected, GetParam()), ::testing::ExitedWithCode(0),
                stderr_is(verdict(0, 1, 0, consumer_lost_its_link, 0, 0, 0)));
}

// The run said the link was lost when the server went, once; the consumer destroyed while the run waits says nothing.
TEST_P(RedisConsumerStaticExitDeathTest, CallbackConsumerInAReconnectionRun) {
    EXPECT_EXIT(run_callback_consumer(consumer::reconnecting, GetParam()), ::testing::ExitedWithCode(0),
                stderr_is(verdict(-1, 1, 0, consumer_lost_its_link, 0, 0, 0)));
}

TEST_P(RedisConsumerStaticExitDeathTest, CoroutineConsumerSubscribed) {
    EXPECT_EXIT(run_coroutine_consumer(consumer::subscribed, GetParam()), ::testing::ExitedWithCode(0),
                stderr_is(verdict(0, 0, 0, 0, 0, 0, 0)));
}

TEST_P(RedisConsumerStaticExitDeathTest, CoroutineConsumerDisconnected) {
    EXPECT_EXIT(run_coroutine_consumer(consumer::disconnected, GetParam()), ::testing::ExitedWithCode(0),
                stderr_is(verdict(0, 1, 0, consumer_lost_its_link, 0, 0, 0)));
}

TEST_P(RedisConsumerStaticExitDeathTest, CoroutineConsumerInAReconnectionRun) {
    EXPECT_EXIT(run_coroutine_consumer(consumer::reconnecting, GetParam()), ::testing::ExitedWithCode(0),
                stderr_is(verdict(-1, 1, 0, consumer_lost_its_link, 0, 0, 0)));
}

INSTANTIATE_TEST_SUITE_P(Exit, RedisConsumerStaticExitDeathTest, ::testing::Values(site::main, site::callback), site_name);

} // namespace client_static_exit_test
