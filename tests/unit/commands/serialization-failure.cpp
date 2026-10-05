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
 * reaches exactly that path without a 512 MiB allocation. No server, no loop: what the client queued
 * and wrote is read back directly. The consumer serializes through the same connector helper; its
 * command paths take only strings, so it is not driven here (that would take a 512 MiB argument).
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

} // namespace serialization_failure_test

using namespace serialization_failure_test;

TEST(RedisSerializationFailure, ACommandThatCannotBeSerializedLeavesNoHandlerAndNoBytes) {
    qb::redis::tcp::client client{qb::io::uri{"tcp://localhost:6379"}};

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
    EXPECT_EQ(written(client.out()), "") << "no partial frame may stay in out()";

    // The next command is the only thing on the wire, whole.
    client.command<qb::redis::status>([](qb::redis::Reply<qb::redis::status> &&) {}, "SET", "key", "v");
    EXPECT_EQ(client.pending_reply_count(), 1u);
    EXPECT_EQ(written(client.out()), "*3\r\n$3\r\nSET\r\n$3\r\nkey\r\n$1\r\nv\r\n");
}

TEST(RedisSerializationFailure, ACommandBehindQueuedOnesRollsBackOnlyItsOwnBytes) {
    qb::redis::tcp::client client{qb::io::uri{"tcp://localhost:6379"}};
    client.command<qb::redis::status>([](qb::redis::Reply<qb::redis::status> &&) {}, "SET", "a", "1");
    const std::string before = written(client.out());

    bool fired = false;
    client.command<qb::redis::status>([&](qb::redis::Reply<qb::redis::status> &&reply) { fired = !reply.ok(); }, "SET", "b", Unserializable{});

    EXPECT_TRUE(fired);
    EXPECT_EQ(client.pending_reply_count(), 1u) << "the command queued before it keeps its handler";
    EXPECT_EQ(written(client.out()), before) << "the bytes queued before it are untouched";
}
