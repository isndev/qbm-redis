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

#include <functional>
#include <gtest/gtest.h>
#include <optional>
#include <string>
#include "../../shared/reply_value_builders.h"

using namespace qb::redis::test;

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

TEST(ReplyTReply, FailRoutesReason) {
    qb::redis::Reply<long long> got;
    auto handler = std::function<void(qb::redis::Reply<long long>)>([&](qb::redis::Reply<long long> r) { got = std::move(r); });
    qb::redis::TReply<decltype(handler), long long> tr(std::move(handler));

    tr.fail("timeout");
    EXPECT_FALSE(got.ok());
    EXPECT_EQ(got.error(), "timeout");
}
