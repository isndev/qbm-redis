/**
 * @file qbm/redis/tests/unit/commands/serialization-failure.cpp
 * @brief A command whose arguments cannot be serialized leaves nothing behind (Huly QB-255).
 *
 * `put_in_pipe()` writes the RESP array header, then each argument, and an argument that cannot be
 * written throws: one over `REDIS_MAX_STRING_SIZE` throws `SecurityError("String too large")` with the
 * header and the arguments before it already in `out()`. `command()` used to queue the reply handler
 * first, so the throw left an orphan handler at the tail of the FIFO AND a partial frame in `out()`:
 * the next command went out behind the truncated one, and the server's replies no longer matched
 * the handlers. Now a command is serialized before its handler is queued, `out()` is rolled back
 * when that fails, and the callback receives a failed `Reply` with the reason.
 *
 * An argument type whose `to_redis_string` throws -- found by ADL, as any user type would be --
 * reaches exactly that path without a 512 MiB allocation. No server, no loop. Since the reconnect
 * contract (Huly QB-126a) a client writes nothing until it is ready, so the cases drive a client that
 * is CONNECTING -- its connect is started, and no loop pass runs it -- whose commands are held: the
 * serialization happens there too, all or nothing, and what was held is counted. The bytes a ready
 * client puts on the wire are read by the server end of a loopback connection in
 * `system/connection/reconnect-carries-nothing.cpp`. The command frames below are read off the command
 * groups themselves, through a capture that serializes what a client would write. The consumer
 * serializes through the same connector helper; its command paths take only strings, so it is not
 * driven here (that would take a 512 MiB argument).
 *
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2026 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)
 * @ingroup Redis
 */

#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include <utility>
// Resolves to qbm/redis/redis.h via the tests/ include dir (INCLUDES=tests).
#include <qbm/redis/redis.h>

namespace serialization_failure_test {

// One argument that cannot be serialized: it throws what the size guard throws.
struct Unserializable {};

inline std::size_t
redis_count(Unserializable const &) noexcept {
    return 1;
}

inline bool
to_redis_string(qb::allocator::pipe<char> &, Unserializable const &) {
    throw qb::redis::SecurityError("String too large");
}

std::string
written(qb::allocator::pipe<char> const &out) {
    return {out.begin(), out.size()};
}

/// A client whose connect was started and never run: it is `connecting`, so what it is given is held.
struct ConnectingClient {
    qb::redis::tcp::client client{qb::io::uri{"tcp://127.0.0.1:1"}};
    ConnectingClient() {
        client.connect([](bool) {}, client.uri());
        EXPECT_EQ(client.state(), qb::io::connection_state::connecting);
    }
};

/// The frame a client would write for a command group's call, without a client: the call's verb and
/// arguments serialized by the client's own `put_in_pipe`.
struct FrameCapture : qb::redis::server_commands<FrameCapture> {
    qb::allocator::pipe<char> out;
    std::size_t               queued = 0;

    using qb::redis::server_commands<FrameCapture>::command;

    template <typename Ret, typename Func, typename... Args>
    FrameCapture &
    command(Func &&, std::string const &name, Args &&...args) {
        qb::redis::put_in_pipe(out, name, std::forward<Args>(args)...);
        ++queued;
        return *this;
    }

    template <typename T, typename Func>
    [[nodiscard]] auto
    make_coro_command(Func &&operation) {
        return qb::redis::detail::make_redis_awaiter<T>(std::forward<Func>(operation));
    }
};

} // namespace serialization_failure_test

using namespace serialization_failure_test;

TEST(RedisSerializationFailure, ACommandThatCannotBeSerializedLeavesNoHandlerAndNoBytes) {
    ConnectingClient connecting;
    auto            &client = connecting.client;

    bool                                fired = false;
    qb::redis::Reply<qb::redis::status> failed;
    client.command<qb::redis::status>(
        [&](qb::redis::Reply<qb::redis::status> &&reply) {
            fired  = true;
            failed = std::move(reply);
        },
        "SET", "key", Unserializable{});

    EXPECT_TRUE(fired) << "the callback must receive the failure, before command() returns";
    EXPECT_FALSE(failed.ok());
    EXPECT_EQ(failed.error(), "String too large");
    EXPECT_EQ(client.pending_reply_count(), 0u) << "no handler may wait for a reply that will never come";
    EXPECT_EQ(written(client.out()), "") << "nothing may reach out() while the client is not ready";

    // The next command is held, whole: the wire case is reconnect-carries-nothing's.
    client.command<qb::redis::status>([](qb::redis::Reply<qb::redis::status> &&) {}, "SET", "key", "v");
    EXPECT_EQ(client.pending_reply_count(), 1u);
    EXPECT_EQ(written(client.out()), "");
}

TEST(RedisSerializationFailure, ACommandBehindQueuedOnesRollsBackOnlyItsOwnBytes) {
    ConnectingClient connecting;
    auto            &client = connecting.client;
    client.command<qb::redis::status>([](qb::redis::Reply<qb::redis::status> &&) {}, "SET", "a", "1");
    const std::string before = written(client.out());

    bool fired = false;
    client.command<qb::redis::status>([&](qb::redis::Reply<qb::redis::status> &&reply) { fired = !reply.ok(); }, "SET", "b", Unserializable{});

    EXPECT_TRUE(fired);
    EXPECT_EQ(client.pending_reply_count(), 1u) << "the command queued before it keeps its handler";
    EXPECT_EQ(written(client.out()), before) << "the bytes queued before it are untouched";
}

TEST(RedisCommandFrames, ClientKillEncodesBothSkipmeValues) {
    const auto noop = [](qb::redis::Reply<long long> &&) {
    };
    for (bool skipme : {false, true}) {
        SCOPED_TRACE(skipme);
        const std::string suffix = skipme ? "$6\r\nSKIPME\r\n$3\r\nyes\r\n" : "$6\r\nSKIPME\r\n$2\r\nno\r\n";
        {
            FrameCapture client;
            client.client_kill(noop, "", 37, "", skipme);
            EXPECT_EQ(written(client.out), "*6\r\n$6\r\nCLIENT\r\n$4\r\nKILL\r\n$2\r\nID\r\n$2\r\n37\r\n" + suffix);
            EXPECT_EQ(client.queued, 1u);
        }
        {
            FrameCapture client;
            client.client_kill(noop, "127.0.0.1:45678", 0, "", skipme);
            EXPECT_EQ(written(client.out), "*6\r\n$6\r\nCLIENT\r\n$4\r\nKILL\r\n$4\r\nADDR\r\n$15\r\n127.0.0.1:45678\r\n" + suffix);
        }
        {
            FrameCapture client;
            client.client_kill(noop, "", 0, "", skipme);
            EXPECT_EQ(written(client.out), "*4\r\n$6\r\nCLIENT\r\n$4\r\nKILL\r\n" + suffix);
        }
    }
}

TEST(RedisCommandFrames, CommandStatsUsesNativeInfoSection) {
    FrameCapture client;
    client.command_stats([](qb::redis::Reply<qb::json> &&) {});
    EXPECT_EQ(written(client.out), "*2\r\n$4\r\nINFO\r\n$12\r\ncommandstats\r\n");
    EXPECT_EQ(client.queued, 1u);
}
