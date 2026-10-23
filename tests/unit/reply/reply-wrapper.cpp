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

//
// Unit tier (pure logic, no daemon/loop): the typed result wrapper Reply<T>
// (ok/value/error/raw + value_or fallback) and the TReply<Func,T> reply-handler
// dispatch paths (disconnect / error reply / parse-throw / success / fail).
//

#include <cstddef>
#include <functional>
#include <gtest/gtest.h>
#include <memory>
#include <type_traits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "../../shared/reply_value_builders.h"

using namespace qb::redis::test;

namespace reply_wrapper_test {
std::unique_ptr<Value>
decorate(std::unique_ptr<Value> value, unsigned depth = 1) {
    for (unsigned i = 0; i < depth; ++i) {
        qb::redis::parser::Attribute attribute;
        attribute.data.entries.emplace_back(mk_simple("ttl"), mk_int(30));
        attribute.value = std::move(value);
        value           = std::make_unique<Value>(std::move(attribute));
    }
    return value;
}

struct DecodeProbe {
    long long value{};
    unsigned  moves{};

    DecodeProbe() = default;
    explicit DecodeProbe(long long value)
        : value(value) {}
    DecodeProbe(DecodeProbe &&other) noexcept
        : value(other.value)
        , moves(other.moves + 1) {}
    DecodeProbe &operator=(DecodeProbe &&) = default;
};
} // namespace reply_wrapper_test

namespace qb::redis {
template <>
reply_wrapper_test::DecodeProbe
parse<reply_wrapper_test::DecodeProbe>(const ReplyValue &reply) {
    const auto value = reply.as_integer().value;
    if (value == -1)
        throw Error("decode redis error");
    if (value == -2)
        throw std::length_error("decode standard error");
    if (value == -3)
        throw 42;
    return reply_wrapper_test::DecodeProbe{value};
}
} // namespace qb::redis

// ============================================================================
// 30. Reply<T> accessors: value_or (engaged / disengaged / !ok), bool, raw
//
// Reply<T> aggregate field order (reply.h): { bool _ok, T _result,
// reply_ptr _raw, std::string _error }.
// ============================================================================

TEST(ReplyWrapper, OkAccessorsAndBool) {
    qb::redis::Reply<long long> r{true, 7, nullptr, {}};
    EXPECT_TRUE(r.ok());
    EXPECT_TRUE(static_cast<bool>(r));
    EXPECT_EQ(r.result(), 7);
    EXPECT_EQ(r.value(), 7);
    EXPECT_TRUE(r.error().empty());
    EXPECT_EQ(r.raw(), nullptr);
}

TEST(ReplyWrapper, NotOkBoolFalseAndError) {
    qb::redis::Reply<long long> r{false, 0, nullptr, "some error"};
    EXPECT_FALSE(r.ok());
    EXPECT_FALSE(static_cast<bool>(r));
    EXPECT_EQ(r.error(), "some error");
}

TEST(ReplyWrapper, ValueOrPlainOkReturnsValue) {
    qb::redis::Reply<long long> r{true, 11, nullptr, {}};
    EXPECT_EQ(r.value_or(99LL), 11);
}

TEST(ReplyWrapper, ValueOrPlainNotOkReturnsDefault) {
    qb::redis::Reply<long long> r{false, 11, nullptr, "boom"};
    EXPECT_EQ(r.value_or(99LL), 99);
}

TEST(ReplyWrapper, ValueOrOptionalEngaged) {
    qb::redis::Reply<std::optional<std::string>> r{true, std::optional<std::string>{"mail"}, nullptr, {}};
    EXPECT_EQ(r.value_or(std::string("def")), "mail");
}

TEST(ReplyWrapper, ValueOrOptionalDisengagedReturnsDefault) {
    qb::redis::Reply<std::optional<std::string>> r{true, std::nullopt, nullptr, {}};
    EXPECT_EQ(r.value_or(std::string("def")), "def");
}

TEST(ReplyWrapper, ValueOrOptionalNotOkReturnsDefault) {
    qb::redis::Reply<std::optional<std::string>> r{false, std::optional<std::string>{"mail"}, nullptr, "err"};
    EXPECT_EQ(r.value_or(std::string("def")), "def");
}

// The documented idiom passes a LITERAL default (`value_or("")`, `value_or(-1)`); until
// 2026-09-10 only a typed one compiled, because each `return` of the auto function deduced its
// own type. The static_asserts are the compile-time half of the pin.
TEST(ReplyWrapper, ValueOrOptionalAcceptsALiteralDefault) {
    qb::redis::Reply<std::optional<std::string>> nil{true, std::nullopt, nullptr, {}};
    static_assert(std::is_same_v<decltype(nil.value_or("")), std::string>);
    EXPECT_EQ(nil.value_or(""), "");
    EXPECT_EQ(nil.value_or("fallback"), "fallback");
    qb::redis::Reply<std::optional<std::string>> mail{true, std::optional<std::string>{"mail"}, nullptr, {}};
    EXPECT_EQ(mail.value_or(""), "mail");
    qb::redis::Reply<std::optional<std::string>> bad{false, std::optional<std::string>{"mail"}, nullptr, "err"};
    EXPECT_EQ(bad.value_or("x"), "x");
}

TEST(ReplyWrapper, ValueOrPlainAcceptsALiteralDefault) {
    qb::redis::Reply<long long> bad{false, 0, nullptr, "boom"};
    static_assert(std::is_same_v<decltype(bad.value_or(-1)), long long>);
    EXPECT_EQ(bad.value_or(-1), -1);
    qb::redis::Reply<long long> ok{true, 7, nullptr, {}};
    EXPECT_EQ(ok.value_or(-1), 7);
    qb::redis::Reply<std::optional<long long>> rank{true, std::nullopt, nullptr, {}};
    static_assert(std::is_same_v<decltype(rank.value_or(-1)), long long>);
    EXPECT_EQ(rank.value_or(-1) + 1, 0);
}

// ============================================================================
// 31. TReply<Func,T> - the 5 dispatch paths via std::function callback
// ============================================================================

TEST(ReplyTReply, NullRawIsDisconnected) {
    qb::redis::Reply<long long> got;
    bool                        called  = false;
    auto                        handler = std::function<void(qb::redis::Reply<long long>)>([&](qb::redis::Reply<long long> r) {
        got    = std::move(r);
        called = true;
    });
    qb::redis::TReply<decltype(handler), long long> tr(std::move(handler));

    tr(nullptr); // disconnect path
    ASSERT_TRUE(called);
    EXPECT_FALSE(got.ok());
    EXPECT_EQ(got.error(), "disconnected");
}

TEST(ReplyTReply, ErrorReplyCopiesMessage) {
    qb::redis::Reply<long long> got;
    auto handler = std::function<void(qb::redis::Reply<long long>)>([&](qb::redis::Reply<long long> r) { got = std::move(r); });
    qb::redis::TReply<decltype(handler), long long> tr(std::move(handler));

    auto raw = std::make_unique<Value>(Value(qb::redis::parser::SimpleError{"ERR", "boom"}));
    tr(std::move(raw));
    EXPECT_FALSE(got.ok());
    // get_error_message() returns the joined "ERR boom".
    EXPECT_EQ(got.error(), "ERR boom");
    EXPECT_NE(got.raw(), nullptr); // raw is moved through on error
}

TEST(ReplyTReply, ParseThrowCaughtAsError) {
    // Feed an Array to a long long parser to force ReplyParseError -> caught ->
    // error string set (success path stays false).
    qb::redis::Reply<long long> got;
    auto handler = std::function<void(qb::redis::Reply<long long>)>([&](qb::redis::Reply<long long> r) { got = std::move(r); });
    qb::redis::TReply<decltype(handler), long long> tr(std::move(handler));

    auto raw = std::make_unique<Value>(make_array({}));
    tr(std::move(raw));
    EXPECT_FALSE(got.ok());
    EXPECT_FALSE(got.error().empty()); // e.what() copied
    EXPECT_NE(got.raw(), nullptr);     // raw is moved through even on parse failure
}

TEST(ReplyTReply, SuccessParsesValue) {
    qb::redis::Reply<long long> got;
    auto handler = std::function<void(qb::redis::Reply<long long>)>([&](qb::redis::Reply<long long> r) { got = std::move(r); });
    qb::redis::TReply<decltype(handler), long long> tr(std::move(handler));

    auto raw = std::make_unique<Value>(Value(Integer{123}));
    tr(std::move(raw));
    ASSERT_TRUE(got.ok());
    EXPECT_EQ(got.value(), 123);
    EXPECT_TRUE(got.error().empty());
    EXPECT_NE(got.raw(), nullptr);
}

TEST(ReplyTReply, SuccessConstructsDecodedValueDirectlyOnLvalueCallback) {
    auto        raw      = mk_int(123);
    const auto *identity = raw.get();
    int         calls    = 0;
    struct Callback {
        int         &calls;
        const Value *identity;

        void
        operator()(qb::redis::Reply<reply_wrapper_test::DecodeProbe> &&reply) & {
            ++calls;
            EXPECT_TRUE(reply.ok());
            EXPECT_EQ(reply.value().value, 123);
            EXPECT_EQ(reply.value().moves, 0u);
            EXPECT_TRUE(reply.error().empty());
            EXPECT_EQ(reply.raw().get(), identity);
        }
        void operator()(qb::redis::Reply<reply_wrapper_test::DecodeProbe> &&) && = delete;
    } callback{calls, identity};
    qb::redis::TReply<Callback, reply_wrapper_test::DecodeProbe> handler(std::move(callback));

    handler(std::move(raw));
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(raw, nullptr);
}

TEST(ReplyTReply, DecodeExceptionsCompleteOnceWithOriginalRaw) {
    const std::pair<long long, const char *> cases[] = {
        {-1, "decode redis error"}, {-2, "decode standard error"}, {-3, "unknown reply-decode error"}
    };
    for (const auto &[value, error] : cases) {
        auto        raw      = mk_int(value);
        const auto *identity = raw.get();
        int         calls    = 0;
        auto        callback = [&](qb::redis::Reply<reply_wrapper_test::DecodeProbe> &&reply) {
            ++calls;
            EXPECT_FALSE(reply.ok());
            EXPECT_EQ(reply.error(), error);
            EXPECT_EQ(reply.value().value, 0);
            EXPECT_EQ(reply.raw().get(), identity);
        };
        qb::redis::TReply<decltype(callback), reply_wrapper_test::DecodeProbe> handler(std::move(callback));

        handler(std::move(raw));
        EXPECT_EQ(calls, 1);
        EXPECT_EQ(raw, nullptr);
    }
}

TEST(ReplyTReply, RepeatedSuccessRepliesKeepTheirOwnValueAndRaw) {
    qb::redis::Reply<long long> got;
    int                         calls = 0;
    std::vector<long long>      observed;
    auto                        callback = std::function<void(qb::redis::Reply<long long>)>([&](qb::redis::Reply<long long> r) {
        ++calls;
        if (r.ok())
            observed.push_back(r.value());
        got = std::move(r);
    });
    qb::redis::TReply<decltype(callback), long long> handler(std::move(callback));
    const std::vector<long long>                     expected{123, -7, 0, 42};
    for (std::size_t i = 0; i < expected.size(); ++i) {
        got                  = {};
        auto        raw      = mk_int(expected[i]);
        const auto *identity = raw.get();
        handler(std::move(raw));
        EXPECT_EQ(calls, static_cast<int>(i + 1));
        ASSERT_TRUE(got.ok());
        EXPECT_EQ(got.value(), expected[i]);
        EXPECT_TRUE(got.error().empty());
        EXPECT_EQ(got.raw().get(), identity);
    }
    EXPECT_EQ(calls, static_cast<int>(expected.size()));
    EXPECT_EQ(observed, expected);
}

TEST(ReplyTReply, ThrowingSuccessCallbackRunsOnce) {
    const auto check = [](auto exception) {
        int  calls   = 0;
        auto handler = std::function<void(qb::redis::Reply<long long>)>([&](qb::redis::Reply<long long> r) {
            ++calls;
            EXPECT_TRUE(r.ok());
            EXPECT_EQ(r.result(), 123);
            throw exception;
        });
        qb::redis::TReply<decltype(handler), long long> tr(std::move(handler));
        EXPECT_ANY_THROW(tr(std::make_unique<Value>(Value(Integer{123}))));
        EXPECT_EQ(calls, 1);
    };
    check(std::runtime_error("user callback"));
    check(qb::redis::Error("user callback"));
    check(42);
}

TEST(ReplyTReply, FailRoutesReason) {
    qb::redis::Reply<long long> got;
    auto handler = std::function<void(qb::redis::Reply<long long>)>([&](qb::redis::Reply<long long> r) { got = std::move(r); });
    qb::redis::TReply<decltype(handler), long long> tr(std::move(handler));

    tr.fail("timeout");
    EXPECT_FALSE(got.ok());
    EXPECT_EQ(got.error(), "timeout");
}

TEST(ReplyTReply, DecoratedSuccessKeepsOriginalRawAndDirectConstruction) {
    auto        raw      = reply_wrapper_test::decorate(mk_int(123), 2);
    const auto *identity = raw.get();
    int         calls    = 0;
    auto        callback = [&](qb::redis::Reply<reply_wrapper_test::DecodeProbe> &&reply) {
        ++calls;
        EXPECT_TRUE(reply.ok()) << reply.error();
        EXPECT_EQ(reply.value().value, 123);
        EXPECT_EQ(reply.value().moves, 0u);
        EXPECT_EQ(reply.raw().get(), identity);
        ASSERT_TRUE(reply.raw());
        EXPECT_EQ(reply.raw()->as_attribute().data.entries.size(), 1u);
        EXPECT_TRUE(std::holds_alternative<qb::redis::parser::Attribute>(*reply.raw()->as_attribute().value));
    };
    qb::redis::TReply<decltype(callback), reply_wrapper_test::DecodeProbe> handler(std::move(callback));
    handler(std::move(raw));
    EXPECT_EQ(calls, 1);
    EXPECT_FALSE(raw);
}

TEST(ReplyTReply, DecoratedServerErrorsKeepTheirMessageAndOriginalRaw) {
    for (bool bulk : {false, true}) {
        SCOPED_TRACE(bulk);
        auto        error    = bulk ? std::make_unique<Value>(qb::redis::parser::BulkError{"WRONGTYPE", "wrong kind"})
                                    : std::make_unique<Value>(qb::redis::parser::SimpleError{"WRONGTYPE", "wrong kind"});
        auto        raw      = reply_wrapper_test::decorate(std::move(error), 2);
        const auto *identity = raw.get();
        int         calls    = 0;
        auto        callback = [&](qb::redis::Reply<long long> &&reply) {
            ++calls;
            EXPECT_FALSE(reply.ok());
            EXPECT_EQ(reply.error(), "WRONGTYPE wrong kind");
            EXPECT_EQ(reply.raw().get(), identity);
        };
        qb::redis::TReply<decltype(callback), long long> handler(std::move(callback));
        handler(std::move(raw));
        EXPECT_EQ(calls, 1);
    }
}

TEST(ReplyTReply, DecoratedDecodeErrorsCompleteOnceWithOriginalRaw) {
    const std::pair<long long, const char *> cases[] = {
        {-1, "decode redis error"}, {-2, "decode standard error"}, {-3, "unknown reply-decode error"}
    };
    for (const auto &[value, error] : cases) {
        auto        raw      = reply_wrapper_test::decorate(mk_int(value));
        const auto *identity = raw.get();
        int         calls    = 0;
        auto        callback = [&](qb::redis::Reply<reply_wrapper_test::DecodeProbe> &&reply) {
            ++calls;
            EXPECT_FALSE(reply.ok());
            EXPECT_EQ(reply.error(), error);
            EXPECT_EQ(reply.raw().get(), identity);
        };
        qb::redis::TReply<decltype(callback), reply_wrapper_test::DecodeProbe> handler(std::move(callback));
        handler(std::move(raw));
        EXPECT_EQ(calls, 1);
    }
}

TEST(ReplyTReply, DecoratedRepliesDoNotRetryThrowingCallbacks) {
    const auto check = [](auto exception) {
        for (bool error : {false, true}) {
            auto        value    = error ? std::make_unique<Value>(qb::redis::parser::SimpleError{"ERR", "refused"}) : mk_int(123);
            auto        raw      = reply_wrapper_test::decorate(std::move(value));
            const auto *identity = raw.get();
            int         calls    = 0;
            auto        callback = [&](qb::redis::Reply<long long> &&reply) {
                ++calls;
                EXPECT_EQ(reply.ok(), !error);
                if (error)
                    EXPECT_EQ(reply.error(), "ERR refused");
                else
                    EXPECT_EQ(reply.result(), 123);
                EXPECT_EQ(reply.raw().get(), identity);
                throw exception;
            };
            qb::redis::TReply<decltype(callback), long long> handler(std::move(callback));
            EXPECT_ANY_THROW(handler(std::move(raw)));
            EXPECT_EQ(calls, 1);
        }
    };
    check(std::runtime_error("callback failed"));
    check(qb::redis::Error("callback failed"));
    check(42);
}

TEST(ReplyTReply, MissingDecoratedValueCompletesWithFailureAndOriginalRaw) {
    auto        raw      = reply_wrapper_test::decorate(nullptr);
    const auto *identity = raw.get();
    int         calls    = 0;
    auto        callback = [&](qb::redis::Reply<long long> &&reply) {
        ++calls;
        EXPECT_FALSE(reply.ok());
        EXPECT_FALSE(reply.error().empty());
        EXPECT_EQ(reply.raw().get(), identity);
    };
    qb::redis::TReply<decltype(callback), long long> handler(std::move(callback));
    handler(std::move(raw));
    EXPECT_EQ(calls, 1);
}

TEST(ReplyWrapper, AttributesAreAbsentWithoutDecoratedRaw) {
    qb::redis::Reply<long long> empty;
    EXPECT_EQ(empty.attributes(), nullptr);
    qb::redis::Reply<long long> plain{true, 7, mk_int(7), {}};
    EXPECT_EQ(plain.attributes(), nullptr);
    static_assert(noexcept(std::as_const(plain).attributes()));
    static_assert(std::is_same_v<decltype(std::as_const(plain).attributes()), const qb::redis::parser::Map *>);
}

TEST(ReplyWrapper, AttributesBorrowOnlyTheOutermostMapAndFollowRawOwnership) {
    auto                        raw      = reply_wrapper_test::decorate(mk_int(7), 2);
    const auto                 *metadata = &raw->as_attribute().data;
    const auto                 *nested   = &raw->as_attribute().value->as_attribute().data;
    qb::redis::Reply<long long> reply{true, 7, std::move(raw), {}};
    ASSERT_EQ(reply.attributes(), metadata);
    EXPECT_NE(reply.attributes(), nested);
    ASSERT_EQ(reply.attributes()->size(), 1u);
    EXPECT_EQ(reply.attributes()->entries.front().first->as_string_view(), "ttl");
    EXPECT_EQ(reply.attributes()->entries.front().second->as_integer().value, 30);

    auto moved = std::move(reply);
    EXPECT_EQ(reply.attributes(), nullptr);
    EXPECT_EQ(moved.attributes(), metadata);
    auto owner = std::move(moved.raw());
    EXPECT_EQ(moved.attributes(), nullptr);
    EXPECT_EQ(&owner->as_attribute().data, metadata);
}

TEST(ReplyWrapper, EmptyAttributeMapIsPresentEvenOnFailure) {
    auto                        raw      = std::make_unique<Value>(qb::redis::parser::Attribute{});
    const auto                 *metadata = &raw->as_attribute().data;
    qb::redis::Reply<long long> reply{false, 0, std::move(raw), "missing value"};
    ASSERT_EQ(reply.attributes(), metadata);
    EXPECT_TRUE(reply.attributes()->empty());
}

// The root facade remains specialized above. This separate type protects the
// other public customization seam, reply::parse<T>, through the same handler.
namespace reply_wrapper_test {
struct FacadeProbe {
    long long    value{};
    const Value *input{};
};
unsigned facade_parse_calls = 0;
} // namespace reply_wrapper_test

namespace qb::redis::reply {
template <>
reply_wrapper_test::FacadeProbe
parse<reply_wrapper_test::FacadeProbe>(const ReplyValue &reply) {
    ++reply_wrapper_test::facade_parse_calls;
    return {reply.as_integer().value ^ 17LL, &reply};
}
} // namespace qb::redis::reply

TEST(ReplyWrapperCustomization, ReplyFacadeSpecializationIsCalledByBothPublicFacades) {
    using reply_wrapper_test::FacadeProbe;
    reply_wrapper_test::facade_parse_calls = 0;
    auto raw                               = mk_int(42);

    const auto direct = qb::redis::reply::parse<FacadeProbe>(*raw);
    EXPECT_EQ(direct.value, 42 ^ 17LL);
    EXPECT_EQ(direct.input, raw.get());
    const auto root = qb::redis::parse<FacadeProbe>(*raw);
    EXPECT_EQ(root.value, 42 ^ 17LL);
    EXPECT_EQ(root.input, raw.get());
    EXPECT_EQ(reply_wrapper_test::facade_parse_calls, 2u);
}

TEST(ReplyTReply, ReplyFacadeSpecializationPreservesPlainAndDecoratedOwnership) {
    using reply_wrapper_test::FacadeProbe;
    for (unsigned depth : {0u, 2u}) {
        reply_wrapper_test::facade_parse_calls = 0;
        auto        raw                        = mk_int(42);
        const auto *inner                      = raw.get();
        raw                                    = reply_wrapper_test::decorate(std::move(raw), depth);
        const auto *identity                   = raw.get();
        int         calls                      = 0;
        auto        callback                   = [&](qb::redis::Reply<FacadeProbe> &&reply) {
            ++calls;
            ASSERT_TRUE(reply.ok());
            EXPECT_EQ(reply.value().value, 42 ^ 17LL);
            EXPECT_EQ(reply.value().input, inner);
            EXPECT_EQ(reply.raw().get(), identity);
            EXPECT_EQ(reply.attributes() != nullptr, depth != 0);
            EXPECT_TRUE(reply.error().empty());
        };
        qb::redis::TReply<decltype(callback), FacadeProbe> handler(std::move(callback));
        handler(std::move(raw));
        EXPECT_EQ(calls, 1);
        EXPECT_EQ(reply_wrapper_test::facade_parse_calls, 1u);
        EXPECT_EQ(raw, nullptr);
    }
}
