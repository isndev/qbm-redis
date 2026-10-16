/**
 * @file qbm/redis/tests/unit/commands/command-reply-types.cpp
 * @brief Each command decodes every reply Redis documents for it, nil and integer replies included.
 *
 * A command's mixin chooses the type its reply is decoded into. When that type cannot hold a
 * documented reply -- the nil of a missing key, an integer where a status was assumed -- a
 * successful command reaches the caller as a failed `Reply` with a decode error, the same
 * outcome as a real failure:
 *   - `CLIENT UNBLOCK` answers `:1` / `:0`, decoded as a status (Huly QB-652);
 *   - counted `LPOP` / `RPOP` and `MEMORY USAGE` answer nil for a missing key (Huly QB-655);
 *   - `SORT ... GET` answers nil for each element whose referenced key is missing (Huly QB-644).
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
    , public qb::redis::hash_commands<ScriptedClient>
    , public qb::redis::set_commands<ScriptedClient>
    , public qb::redis::server_commands<ScriptedClient> {
public:
    using Responses = std::vector<std::optional<std::string_view>>;

    explicit ScriptedClient(std::string_view reply_wire)
        : _reply_wires{reply_wire}
        , _repeat_reply(true) {}

    // nullopt models the dispatcher's disconnect path. Consume before invoking
    // the handler so a synchronous scan continuation receives the next page.
    explicit ScriptedClient(Responses replies)
        : _reply_wires(std::move(replies)) {}

    template <typename Ret, typename Func, typename... Args>
    ScriptedClient &
    command(Func &&func, const std::string &name, Args &&...args) {
        qb::allocator::pipe<char> out;
        qb::redis::put_in_pipe(out, name, std::forward<Args>(args)...);
        frame.assign(out.begin(), out.size());
        frames.push_back(frame);
        const auto index = _repeat_reply ? 0 : _next_reply++;
        if (index >= _reply_wires.size()) {
            ADD_FAILURE() << "command exceeded its scripted replies";
            return *this;
        }
        qb::redis::reply_ptr raw;
        if (const auto &wire = _reply_wires[index]; wire) {
            auto parsed = qb::redis::parser::parse(*wire);
            if (!parsed.has_value()) {
                ADD_FAILURE() << "scripted reply does not parse: " << parsed.error().what();
                return *this;
            }
            raw = std::make_unique<qb::redis::ReplyValue>(std::move(*parsed));
        }
        last_raw = raw.get();
        qb::redis::TReply<Func, Ret> handler{std::forward<Func>(func)};
        handler(std::move(raw));
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

    std::string                  frame; ///< the last command, as it would go on the wire
    int                          completed{0};
    std::vector<std::string>     frames;
    const qb::redis::ReplyValue *last_raw{};

private:
    Responses   _reply_wires;
    std::size_t _next_reply{};
    bool        _repeat_reply{false};
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
using StringList         = std::vector<std::string>;
using NullableStringList = std::vector<std::optional<std::string>>;

// ============================================================================
// CLIENT UNBLOCK (Huly QB-652) -- an integer, 1 when a blocked client was unblocked
// ============================================================================

TEST(CommandReplyTypes, ClientUnblockReadsTheIntegerOutcome) {
    const std::pair<std::string_view, bool> outcomes[] = {{":1\r\n", true}, {":0\r\n", false}};
    for (const auto &[wire, unblocked] : outcomes) {
        SCOPED_TRACE(wire);
        ScriptedClient client(wire);
        auto           awaited = client.client_unblock(42);
        static_assert(std::is_same_v<decltype(awaited), qb::redis::Reply<bool>>);
        ASSERT_TRUE(awaited.ok()) << awaited.error();
        EXPECT_EQ(awaited.result(), unblocked);
        EXPECT_EQ(client.frame, "*3\r\n$6\r\nCLIENT\r\n$7\r\nUNBLOCK\r\n$2\r\n42\r\n");

        Captured<bool> callback;
        client.client_unblock(callback.sink(), 42, true);
        EXPECT_EQ(callback.calls, 1);
        ASSERT_TRUE(callback.reply.ok()) << callback.reply.error();
        EXPECT_EQ(callback.reply.result(), unblocked);
        EXPECT_EQ(client.frame, "*4\r\n$6\r\nCLIENT\r\n$7\r\nUNBLOCK\r\n$2\r\n42\r\n$5\r\nERROR\r\n");
    }

    ScriptedClient refused("-ERR CLIENT UNBLOCK reason should be TIMEOUT or ERROR\r\n");
    auto           error = refused.client_unblock(42);
    EXPECT_FALSE(error.ok());
    EXPECT_EQ(error.error(), "ERR CLIENT UNBLOCK reason should be TIMEOUT or ERROR") << "a real refusal keeps its own reason";
}

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

// ============================================================================
// SORT ... GET (Huly QB-644) -- nil for an element whose referenced key is missing
// ============================================================================

TEST(CommandReplyTypes, SortGetKeepsTheNilOfAMissingReferencedKey) {
    // SORT key GET w_*: element "1" references w_1 = "one", element "2" references a missing w_2.
    for (const std::string_view wire : {"*2\r\n$3\r\none\r\n$-1\r\n", "*2\r\n$3\r\none\r\n_\r\n"}) {
        SCOPED_TRACE(wire);
        ScriptedClient client(wire);
        auto           read = client.sortKeyGet("list", {"w_*"});
        static_assert(std::is_same_v<decltype(read), qb::redis::Reply<NullableStringList>>);
        ASSERT_TRUE(read.ok()) << read.error();
        EXPECT_EQ(read.result(), (NullableStringList{"one", std::nullopt}));
        EXPECT_EQ(client.frame, "*4\r\n$4\r\nSORT\r\n$4\r\nlist\r\n$3\r\nGET\r\n$3\r\nw_*\r\n");

        Captured<NullableStringList> read_only;
        client.sortKeyRoGet(read_only.sink(), "list", {"w_*"}, {"ALPHA"});
        EXPECT_EQ(read_only.calls, 1);
        ASSERT_TRUE(read_only.reply.ok()) << read_only.reply.error();
        EXPECT_EQ(read_only.reply.result(), (NullableStringList{"one", std::nullopt}));
        EXPECT_EQ(client.frame, "*5\r\n$7\r\nSORT_RO\r\n$4\r\nlist\r\n$5\r\nALPHA\r\n$3\r\nGET\r\n$3\r\nw_*\r\n");

        // sortKey's elements are strings: a nil fails the reply, it never becomes "".
        auto plain = client.sortKey("list", {"GET", "w_*"});
        EXPECT_FALSE(plain.ok());
        EXPECT_TRUE(plain.result().empty());
    }

    // Two patterns: one entry per pattern for each sorted element, in pattern order; an empty
    // string stays an engaged empty string.
    ScriptedClient multi("*4\r\n$1\r\n1\r\n$0\r\n\r\n$1\r\n2\r\n_\r\n");
    auto           both = multi.sortKeyGet("list", {"#", "w_*"});
    ASSERT_TRUE(both.ok()) << both.error();
    EXPECT_EQ(both.result(), (NullableStringList{"1", "", "2", std::nullopt}));
    EXPECT_EQ(multi.frame, "*6\r\n$4\r\nSORT\r\n$4\r\nlist\r\n$3\r\nGET\r\n$1\r\n#\r\n$3\r\nGET\r\n$3\r\nw_*\r\n");

    ScriptedClient wrong_type("-WRONGTYPE Operation against a key holding the wrong kind of value\r\n");
    auto           failed = wrong_type.sortKeyGet("hash", {"w_*"});
    EXPECT_FALSE(failed.ok());
    EXPECT_EQ(failed.error(), "WRONGTYPE Operation against a key holding the wrong kind of value");
}

// Aggregators keep the first failure and every successfully received item. A
// collection of replies has no single raw value; one-reply adapters retain it.
TEST(CommandReplyFailures, AutomaticScansKeepFirstAndLastPageErrorsAndPartialItems) {
    for (bool set_scan : {false, true}) {
        for (bool last_page : {false, true}) {
            const std::pair<std::optional<std::string_view>, std::string_view> failures[] = {
                {"-WRONGTYPE wrong kind\r\n", "WRONGTYPE wrong kind"}, {std::nullopt, "disconnected"}
            };
            for (const auto &[wire, reason] : failures) {
                SCOPED_TRACE(set_scan ? "SSCAN" : "SCAN");
                SCOPED_TRACE(last_page ? "last page" : "first page");
                SCOPED_TRACE(reason);
                ScriptedClient::Responses responses;
                if (last_page)
                    responses.push_back("*2\r\n$1\r\n7\r\n*1\r\n$4\r\nkept\r\n");
                responses.push_back(wire);
                ScriptedClient              client(std::move(responses));
                Captured<qb::redis::scan<>> result;
                if (set_scan)
                    client.sscan(result.sink(), "set", "*");
                else
                    client.scan(result.sink(), "*");
                EXPECT_EQ(result.calls, 1);
                EXPECT_FALSE(result.reply.ok());
                EXPECT_EQ(result.reply.error(), reason);
                EXPECT_EQ(result.reply.result().items, last_page ? StringList{"kept"} : StringList{});
                EXPECT_FALSE(result.reply.raw());
                EXPECT_EQ(client.completed, last_page ? 2 : 1);
                EXPECT_EQ(client.frames.size(), last_page ? 2u : 1u);
            }
        }
    }
}

TEST(CommandReplyFailures, AutomaticScansKeepEverySuccessfulPageWithoutInventingRaw) {
    for (bool set_scan : {false, true}) {
        ScriptedClient client(ScriptedClient::Responses{"*2\r\n$1\r\n7\r\n*1\r\n$4\r\nkept\r\n", "*2\r\n$1\r\n0\r\n*1\r\n$4\r\nlast\r\n"});
        Captured<qb::redis::scan<>> result;
        if (set_scan)
            client.sscan(result.sink(), "set", "*");
        else
            client.scan(result.sink(), "*");
        EXPECT_EQ(result.calls, 1);
        EXPECT_TRUE(result.reply.ok()) << result.reply.error();
        EXPECT_TRUE(result.reply.error().empty());
        EXPECT_EQ(result.reply.result().items, (StringList{"kept", "last"}));
        EXPECT_FALSE(result.reply.raw());
        EXPECT_EQ(client.completed, 2);
    }
}

TEST(CommandReplyFailures, MultiHvalsKeepsFirstObservedFailureAndSuccessfulValues) {
    const ScriptedClient::Responses scenarios[] = {
        {"-WRONGTYPE wrong kind\r\n", "*1\r\n$4\r\nkept\r\n", "-ERR later failure\r\n"},
        {"*1\r\n$4\r\nkept\r\n", "*0\r\n", "-WRONGTYPE wrong kind\r\n"},
        {std::nullopt, "*1\r\n$4\r\nkept\r\n", "-ERR later failure\r\n"}
    };
    for (const auto &responses : scenarios) {
        ScriptedClient       client(responses);
        Captured<StringList> result;
        client.hvals(result.sink(), StringList{"first", "second", "last"});
        EXPECT_EQ(result.calls, 1);
        EXPECT_FALSE(result.reply.ok());
        EXPECT_EQ(result.reply.error(), responses.front() ? "WRONGTYPE wrong kind" : "disconnected");
        EXPECT_EQ(result.reply.result(), (StringList{"kept"}));
        EXPECT_FALSE(result.reply.raw());
        EXPECT_EQ(client.completed, 3);
    }
}

TEST(CommandReplyFailures, MultiHvalsSuccessfulAndEmptyInputsCompleteOnceWithoutRaw) {
    for (bool empty : {false, true}) {
        ScriptedClient       client(ScriptedClient::Responses{"*1\r\n$4\r\nkept\r\n", "*1\r\n$4\r\nlast\r\n"});
        Captured<StringList> result;
        client.hvals(result.sink(), empty ? StringList{} : StringList{"first", "last"});
        EXPECT_EQ(result.calls, 1);
        EXPECT_TRUE(result.reply.ok()) << result.reply.error();
        EXPECT_TRUE(result.reply.error().empty());
        EXPECT_EQ(result.reply.result(), empty ? StringList{} : (StringList{"kept", "last"}));
        EXPECT_FALSE(result.reply.raw());
        EXPECT_EQ(client.completed, empty ? 0 : 2);
    }
}

TEST(CommandReplyFailures, ConfigGetAndTimeKeepSourceErrorsAndRaw) {
    const std::pair<std::optional<std::string_view>, std::string_view> failures[] = {
        {"-NOPERM command refused\r\n", "NOPERM command refused"}, {std::nullopt, "disconnected"}
    };
    for (const auto &[wire, reason] : failures) {
        SCOPED_TRACE(reason);
        ScriptedClient config_client(ScriptedClient::Responses{wire});
        auto           config = config_client.config_get("maxmemory");
        EXPECT_FALSE(config.ok());
        EXPECT_EQ(config.error(), reason);
        EXPECT_EQ(config.raw().get(), config_client.last_raw);
        EXPECT_TRUE(config.result().empty());
        EXPECT_EQ(config_client.completed, 1);

        ScriptedClient time_client(ScriptedClient::Responses{wire});
        auto           time = time_client.time();
        EXPECT_FALSE(time.ok());
        EXPECT_EQ(time.error(), reason);
        EXPECT_EQ(time.raw().get(), time_client.last_raw);
        EXPECT_EQ(time_client.completed, 1);
    }
}

TEST(CommandReplyFailures, ConfigGetAndTimeKeepSuccessfulSourceRaw) {
    for (std::string_view wire : {"*2\r\n$9\r\nmaxmemory\r\n$1\r\n0\r\n", "%1\r\n$9\r\nmaxmemory\r\n$1\r\n0\r\n"}) {
        ScriptedClient                                             client(wire);
        Captured<std::vector<std::pair<std::string, std::string>>> result;
        client.config_get(result.sink(), "maxmemory");
        EXPECT_EQ(result.calls, 1);
        ASSERT_TRUE(result.reply.ok()) << result.reply.error();
        EXPECT_EQ(result.reply.result(), (std::vector<std::pair<std::string, std::string>>{{"maxmemory", "0"}}));
        EXPECT_EQ(result.reply.raw().get(), client.last_raw);
    }
    ScriptedClient client("*2\r\n$2\r\n42\r\n$1\r\n7\r\n");
    auto           time = client.time();
    ASSERT_TRUE(time.ok()) << time.error();
    EXPECT_EQ(time.result(), (std::pair<long long, long long>{42, 7}));
    EXPECT_EQ(time.raw().get(), client.last_raw);
}

TEST(CommandReplyFailures, ConfigGetAndTimeKeepDecodeFailureDetails) {
    ScriptedClient       source(":42\r\n");
    Captured<StringList> expected;
    source.command<StringList>(expected.sink(), "TIME");
    ASSERT_FALSE(expected.reply.ok());
    ASSERT_FALSE(expected.reply.error().empty());

    ScriptedClient config_client(":42\r\n");
    auto           config = config_client.config_get("maxmemory");
    EXPECT_FALSE(config.ok());
    EXPECT_EQ(config.error(), expected.reply.error());
    EXPECT_EQ(config.raw().get(), config_client.last_raw);

    ScriptedClient time_client(":42\r\n");
    auto           time = time_client.time();
    EXPECT_FALSE(time.ok());
    EXPECT_EQ(time.error(), expected.reply.error());
    EXPECT_EQ(time.raw().get(), time_client.last_raw);

    // These decode as string arrays successfully; the TIME adapter itself must
    // fail the numeric conversion while keeping that original source reply.
    for (std::string_view wire : {"*2\r\n$4\r\noops\r\n$1\r\n0\r\n", "*2\r\n$1\r\n1\r\n$4\r\noops\r\n"}) {
        SCOPED_TRACE(wire);
        ScriptedClient malformed(wire);
        auto           result = malformed.time();
        EXPECT_FALSE(result.ok());
        EXPECT_EQ(result.error(), "Malformed TIME reply");
        ASSERT_TRUE(result.raw());
        EXPECT_EQ(result.raw().get(), malformed.last_raw);
        EXPECT_EQ(malformed.completed, 1);
        EXPECT_EQ(malformed.frames.size(), 1u);
    }
}
