/**
 * @file qbm/redis/tests/unit/commands/oneway-batch.cpp
 * @brief `qb::redis::oneway_batch`: what joins a one-way batch, what is refused, and the bytes it holds (Huly QB-902).
 *
 * A batch serializes each command as it is added, apart from any client, and `add()` returns why a command was
 * refused -- keeping nothing of it then. Refused: the verbs the server answers, or acts on, past the reply that
 * `CLIENT REPLY OFF` suppresses (measured on Redis 8.10: MULTI queues the CLIENT REPLY after it, HELLO 3 and RESET
 * change the connection unseen, a SUBSCRIBE confirmation is pushed regardless), every blocking command, and a command
 * that cannot be serialized. No server, no loop: the client side is `system/connection/oneway-batch.cpp`.
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
#include <vector>
#include <qbm/redis/redis.h>

namespace oneway_batch_unit_test {

// One argument that cannot be serialized: it throws what the size guard throws (as in serialization-failure.cpp).
struct Unserializable {};

inline std::size_t
redis_count(Unserializable const &) noexcept {
    return 1;
}

inline bool
to_redis_string(qb::allocator::pipe<char> &, Unserializable const &) {
    throw qb::redis::SecurityError("String too large");
}

/// The bytes a client would write for the same command (put_in_pipe, the command path's serializer).
template <typename... Args>
std::string
frame(Args &&...args) {
    qb::allocator::pipe<char> out;
    qb::redis::put_in_pipe(out, std::forward<Args>(args)...);
    return {out.begin(), out.size()};
}

} // namespace oneway_batch_unit_test

using namespace oneway_batch_unit_test;

TEST(OnewayBatch, CommandsJoinInOrderAsTheClientWouldSerializeThem) {
    qb::redis::oneway_batch batch;
    EXPECT_TRUE(batch.empty());
    EXPECT_FALSE(batch.add("SET", "k", "v"));
    EXPECT_FALSE(batch.add("incr", "n")) << "any letter case";
    EXPECT_FALSE(batch.add("PUBLISH", "ch", 42));
    EXPECT_FALSE(batch.add("JSON.SET", "doc", "$", "{}")) << "a verb the client does not know is accepted";
    EXPECT_EQ(batch.size(), 4u);
    EXPECT_EQ(batch.wire(), frame("SET", "k", "v") + frame("incr", "n") + frame("PUBLISH", "ch", 42) + frame("JSON.SET", "doc", "$", "{}"));
}

TEST(OnewayBatch, EveryVerbThatActsPastTheSuppressedReplyIsRefused) {
    const std::vector<std::pair<std::string, std::string_view>> refused{
        {"ASKING", "connection's state"},
        {"AUTH", "connection's state"},
        {"client", "connection's state"},
        {"HELLO", "connection's state"},
        {"MONITOR", "connection's state"},
        {"PSYNC", "connection's state"},
        {"QUIT", "connection's state"},
        {"READONLY", "connection's state"},
        {"ReadWrite", "connection's state"},
        {"REPLCONF", "connection's state"},
        {"reset", "connection's state"},
        {"SELECT", "connection's state"},
        {"SYNC", "connection's state"},
        {"MULTI", "transaction"},
        {"exec", "transaction"},
        {"DISCARD", "transaction"},
        {"WATCH", "transaction"},
        {"UNWATCH", "transaction"},
        {"SUBSCRIBE", "subscription"},
        {"psubscribe", "subscription"},
        {"SSUBSCRIBE", "subscription"},
        {"UNSUBSCRIBE", "subscription"},
        {"PUNSUBSCRIBE", "subscription"},
        {"SUNSUBSCRIBE", "subscription"},
        {"BLPOP", "blocking"},
        {"BRPOPLPUSH", "blocking"},
        {"BZPOPMIN", "blocking"},
        {"WAIT", "blocking"},
        {"XREAD", "blocking"},
        {"XREADGROUP", "blocking"},
    };
    for (auto const &[verb, why] : refused) {
        qb::redis::oneway_batch batch;
        const auto              error = batch.add(verb, "x");
        ASSERT_TRUE(error) << verb << " joined a one-way batch";
        EXPECT_NE(error->find(why), std::string::npos) << verb << ": " << *error;
        EXPECT_TRUE(batch.empty()) << verb;
        EXPECT_TRUE(batch.wire().empty()) << verb << " left bytes behind";
    }
    qb::redis::oneway_batch batch;
    EXPECT_TRUE(batch.add(""));
    EXPECT_TRUE(batch.empty());
}

TEST(OnewayBatch, ARefusedCommandLeavesTheBatchAsItWas) {
    qb::redis::oneway_batch batch;
    ASSERT_FALSE(batch.add("SET", "a", "1"));
    const std::string before{batch.wire()};

    const auto unserializable = batch.add("SET", "b", Unserializable{});
    ASSERT_TRUE(unserializable);
    EXPECT_EQ(*unserializable, "String too large");
    EXPECT_EQ(batch.size(), 1u);
    EXPECT_EQ(batch.wire(), before) << "no partial frame stays";

    ASSERT_TRUE(batch.add("MULTI"));
    EXPECT_EQ(batch.wire(), before);

    ASSERT_FALSE(batch.add("SET", "c", "3"));
    EXPECT_EQ(batch.wire(), before + frame("SET", "c", "3"));
}

TEST(OnewayBatch, ClearAndMoveCarryTheCommands) {
    qb::redis::oneway_batch batch;
    ASSERT_FALSE(batch.add("SET", "a", "1"));
    ASSERT_FALSE(batch.add("SET", "b", "2"));
    const std::string bytes{batch.wire()};

    qb::redis::oneway_batch moved{std::move(batch)};
    EXPECT_EQ(moved.size(), 2u);
    EXPECT_EQ(moved.wire(), bytes);
    EXPECT_TRUE(batch.empty()) << "the moved-from batch is empty"; // NOLINT(bugprone-use-after-move)
    EXPECT_TRUE(batch.wire().empty());                             // NOLINT(bugprone-use-after-move)

    qb::redis::oneway_batch assigned;
    assigned = std::move(moved);
    EXPECT_EQ(assigned.size(), 2u);
    EXPECT_EQ(assigned.wire(), bytes);

    assigned.clear();
    EXPECT_TRUE(assigned.empty());
    EXPECT_TRUE(assigned.wire().empty());
    EXPECT_FALSE(assigned.add("SET", "c", "3"));
    EXPECT_EQ(assigned.wire(), frame("SET", "c", "3"));
}
