/**
 * @file qbm/redis/tests/unit/commands/command-reply-types.cpp
 * @brief Each command decodes every reply Redis documents for it, nil replies included.
 *
 * A command's mixin chooses the type its reply is decoded into. When that type cannot hold a
 * documented reply -- the nil of a missing key -- a successful command reaches the caller
 * as a failed `Reply` with a decode error, the same
 * outcome as a real failure:
 *   - counted `LPOP` / `RPOP` and `MEMORY USAGE` answer nil for a missing key (Huly QB-655).
 *
 * The client here is the public command mixins composed with a scripted `command<T>` seam: the
 * real mixin builds the arguments and picks the type, `put_in_pipe` serializes the frame the
 * connector would send, the RESP parser reads the reply bytes Redis sent (captured from Redis
 * 8.0, RESP2 and RESP3), and the dispatcher's own `TReply<Func, T>` decodes them and completes
 * the callback. The coroutine forms run their operation through the same seam, synchronously.
 * No socket, no event loop, no daemon.
 *
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2026 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)
 * @ingroup Redis
 */

#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>
// Resolves to qbm/redis/redis.h via the tests/ include dir (INCLUDES=tests).
#include <qbm/redis/redis.h>

namespace command_reply_types_test {

/// The command mixins over a transport that answers every command with one scripted reply.
class ScriptedClient
    : public qb::redis::key_commands<ScriptedClient>
    , public qb::redis::list_commands<ScriptedClient>
    , public qb::redis::server_commands<ScriptedClient> {
public:
    explicit ScriptedClient(std::string_view reply_wire)
        : _reply_wire(reply_wire) {}

    template <typename Ret, typename Func, typename... Args>
    ScriptedClient &
    command(Func &&func, const std::string &name, Args &&...args) {
        qb::allocator::pipe<char> out;
        qb::redis::put_in_pipe(out, name, std::forward<Args>(args)...);
        frame.assign(out.begin(), out.size());
        auto parsed = qb::redis::parser::parse(_reply_wire);
        if (!parsed.has_value()) {
            ADD_FAILURE() << "scripted reply does not parse: " << parsed.error().what();
            return *this;
        }
        qb::redis::TReply<Func, Ret> handler{std::forward<Func>(func)};
        handler(std::make_unique<qb::redis::ReplyValue>(std::move(*parsed)));
        ++completed;
        return *this;
    }

    /// The coroutine form: run the operation now and return the Reply it completed with.
    template <typename T, typename Operation>
    [[nodiscard]] qb::redis::Reply<T>
    make_coro_command(Operation &&operation) {
        qb::redis::Reply<T> result;
        std::forward<Operation>(operation)([&result](qb::redis::Reply<T> &&reply) { result = std::move(reply); });
        return result;
    }

    std::string frame; ///< the last command, as it would go on the wire
    int         completed{0};

private:
    std::string_view _reply_wire;
};

/// The callback form's reply, captured once.
template <typename T>
struct Captured {
    qb::redis::Reply<T> reply;
    int                 calls{0};
    auto
    sink() {
        return [this](qb::redis::Reply<T> &&r) {
            reply = std::move(r);
            ++calls;
        };
    }
};

} // namespace command_reply_types_test

using namespace command_reply_types_test;
using StringList = std::vector<std::string>;

// ============================================================================
// Counted LPOP / RPOP and MEMORY USAGE (Huly QB-655) -- nil for a missing key
// ============================================================================

TEST(CommandReplyTypes, CountedPopOfAMissingKeyIsAnEmptyListNotAFailure) {
    for (const std::string_view nil : {"*-1\r\n", "_\r\n"}) { // RESP2 null array, RESP3 null
        SCOPED_TRACE(nil);
        ScriptedClient client(nil);
        auto           left = client.lpop("missing", 2);
        ASSERT_TRUE(left.ok()) << left.error();
        EXPECT_TRUE(left.result().empty());
        ASSERT_TRUE(left.raw());
        EXPECT_TRUE(left.raw()->is_null()) << "raw() keeps the server's nil";
        EXPECT_EQ(client.frame, "*3\r\n$4\r\nLPOP\r\n$7\r\nmissing\r\n$1\r\n2\r\n");

        Captured<StringList> right;
        client.rpop(right.sink(), "missing", 2);
        EXPECT_EQ(right.calls, 1);
        ASSERT_TRUE(right.reply.ok()) << right.reply.error();
        EXPECT_TRUE(right.reply.result().empty());
        EXPECT_EQ(client.frame, "*3\r\n$4\r\nRPOP\r\n$7\r\nmissing\r\n$1\r\n2\r\n");
    }

    ScriptedClient popped("*2\r\n$1\r\na\r\n$1\r\nb\r\n");
    auto           values = popped.lpop("list", 2);
    ASSERT_TRUE(values.ok()) << values.error();
    EXPECT_EQ(values.result(), (StringList{"a", "b"}));

    ScriptedClient zero_count("*0\r\n"); // LPOP list 0 on an existing list: an empty array, not nil
    auto           none = zero_count.rpop("list", 0);
    ASSERT_TRUE(none.ok()) << none.error();
    EXPECT_TRUE(none.result().empty());
    ASSERT_TRUE(none.raw());
    EXPECT_FALSE(none.raw()->is_null());

    ScriptedClient wrong_type("-WRONGTYPE Operation against a key holding the wrong kind of value\r\n");
    auto           failed = wrong_type.lpop("hash", 2);
    EXPECT_FALSE(failed.ok());
    EXPECT_EQ(failed.error(), "WRONGTYPE Operation against a key holding the wrong kind of value");
}

TEST(CommandReplyTypes, MemoryUsageOfAMissingKeyIsZeroBytesNotAFailure) {
    for (const std::string_view nil : {"$-1\r\n", "_\r\n"}) {
        SCOPED_TRACE(nil);
        ScriptedClient client(nil);
        auto           usage = client.memory_usage("missing");
        ASSERT_TRUE(usage.ok()) << usage.error();
        EXPECT_EQ(usage.result(), 0);
        ASSERT_TRUE(usage.raw());
        EXPECT_TRUE(usage.raw()->is_null());
        EXPECT_EQ(client.frame, "*3\r\n$6\r\nMEMORY\r\n$5\r\nUSAGE\r\n$7\r\nmissing\r\n");

        Captured<long long> sampled;
        client.memory_usage(sampled.sink(), "missing", 5);
        EXPECT_EQ(sampled.calls, 1);
        ASSERT_TRUE(sampled.reply.ok()) << sampled.reply.error();
        EXPECT_EQ(sampled.reply.result(), 0);
        EXPECT_EQ(client.frame, "*5\r\n$6\r\nMEMORY\r\n$5\r\nUSAGE\r\n$7\r\nmissing\r\n$7\r\nSAMPLES\r\n$1\r\n5\r\n");
    }

    ScriptedClient present(":72\r\n");
    auto           bytes = present.memory_usage("key");
    ASSERT_TRUE(bytes.ok()) << bytes.error();
    EXPECT_EQ(bytes.result(), 72);

    ScriptedClient refused("-ERR syntax error\r\n");
    auto           failed = refused.memory_usage("key", 5);
    EXPECT_FALSE(failed.ok());
    EXPECT_EQ(failed.error(), "ERR syntax error");
    ASSERT_TRUE(failed.raw());
    EXPECT_TRUE(failed.raw()->is_error());
}
