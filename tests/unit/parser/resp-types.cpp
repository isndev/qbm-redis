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
 * @file qbm/redis/tests/unit/parser/resp-types.cpp
 * @brief Unit tests for the RESP value-model types (`parser/types.h`):
 *        @ref qb::redis::parser::Value conversion helpers, aggregate
 *        @c operator== branch coverage, scalar implicit conversions, string
 *        accessors, the constexpr type-prefix predicates, and
 *        @ref qb::redis::parser::ParseError::what() for every error code.
 *
 * Split out of the legacy `test-parser-units.cpp` (Types/equality aspect).
 * Pure logic, no daemon, no event loop, no RESOURCE_LOCK.
 *
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2026 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)
 * @ingroup Redis
 */

#include <memory>
#include <optional>
#include <string>
#include <gtest/gtest.h>

#include "../parser/types.h"

using namespace qb::redis::parser;

// ============================================================================
// Value conversion helpers (to_integer / to_double / to_string / size / ...)
// ============================================================================

class ValueConversionTest : public ::testing::Test {};

TEST_F(ValueConversionTest, ToIntegerCoversIntBoolAndNullopt) {
    EXPECT_EQ(Value(Integer{99}).to_integer(), std::optional<int64_t>(99));
    EXPECT_EQ(Value(Boolean{true}).to_integer(), std::optional<int64_t>(1));
    EXPECT_EQ(Value(Boolean{false}).to_integer(), std::optional<int64_t>(0));
    EXPECT_FALSE(Value(SimpleString{"x"}).to_integer().has_value());
}

TEST_F(ValueConversionTest, ToDoubleCoversDoubleIntPromotionAndNullopt) {
    EXPECT_EQ(Value(Double{2.5}).to_double(), std::optional<double>(2.5));
    EXPECT_EQ(Value(Integer{4}).to_double(), std::optional<double>(4.0));
    EXPECT_FALSE(Value(SimpleString{"x"}).to_double().has_value());
}

TEST_F(ValueConversionTest, ToStringCoversThreeStringKindsAndNullopt) {
    EXPECT_EQ(Value(SimpleString{"ss"}).to_string(), std::optional<std::string>("ss"));
    EXPECT_EQ(Value(BulkString{"bs"}).to_string(), std::optional<std::string>("bs"));
    {
        VerbatimString vs;
        vs.encoding[0] = 't';
        vs.encoding[1] = 'x';
        vs.encoding[2] = 't';
        vs.value       = "vv";
        EXPECT_EQ(Value(std::move(vs)).to_string(), std::optional<std::string>("vv"));
    }
    EXPECT_FALSE(Value(Integer{1}).to_string().has_value());
}

TEST_F(ValueConversionTest, AsStringViewAcrossKindsAndEmptyDefault) {
    EXPECT_EQ(Value(SimpleString{"abc"}).as_string_view(), "abc");
    EXPECT_EQ(Value(BulkString{"def"}).as_string_view(), "def");
    EXPECT_TRUE(Value(Integer{1}).as_string_view().empty());
}

TEST_F(ValueConversionTest, SizeAndEmptyAcrossAggregatesAndScalars) {
    {
        Array arr;
        arr.elements.push_back(std::make_unique<Value>(Value(Integer{1})));
        arr.elements.push_back(std::make_unique<Value>(Value(Integer{2})));
        Value v(std::move(arr));
        EXPECT_EQ(v.size(), 2u);
        EXPECT_FALSE(v.empty());
    }
    EXPECT_EQ(Value(BulkString{"hello"}).size(), 5u);
    EXPECT_EQ(Value(SimpleString{"hi"}).size(), 2u);
    EXPECT_EQ(Value(Integer{1}).size(), 0u);
    EXPECT_TRUE(Value(Integer{1}).empty());
}

TEST_F(ValueConversionTest, GetErrorMessageCoversBothErrorsAndEmptyDefault) {
    EXPECT_EQ(Value(SimpleError{"ERR", "boom"}).get_error_message(), "ERR boom");
    EXPECT_EQ(Value(BulkError{"WRONGTYPE", "no"}).get_error_message(), "WRONGTYPE no");
    EXPECT_TRUE(Value(Integer{1}).get_error_message().empty());
}

// ============================================================================
// Type predicates (is_*) — including the union predicates is_string/is_error/
// is_number/is_aggregate which span several alternatives.
// ============================================================================

TEST_F(ValueConversionTest, UnionTypePredicates) {
    // is_string covers SimpleString, BulkString, VerbatimString.
    EXPECT_TRUE(Value(SimpleString{"s"}).is_string());
    EXPECT_TRUE(Value(BulkString{"b"}).is_string());
    {
        VerbatimString vs;
        vs.encoding[0] = 't';
        vs.encoding[1] = 'x';
        vs.encoding[2] = 't';
        vs.value       = "v";
        EXPECT_TRUE(Value(std::move(vs)).is_string());
    }
    EXPECT_FALSE(Value(Integer{1}).is_string());

    // is_error covers SimpleError and BulkError.
    EXPECT_TRUE(Value(SimpleError{"ERR", ""}).is_error());
    EXPECT_TRUE(Value(BulkError{"ERR", ""}).is_error());
    EXPECT_FALSE(Value(SimpleString{"OK"}).is_error());

    // is_number covers Integer, Double, BigNumber.
    EXPECT_TRUE(Value(Integer{1}).is_number());
    EXPECT_TRUE(Value(Double{1.0}).is_number());
    EXPECT_TRUE(Value(BigNumber{"1", false}).is_number());
    EXPECT_FALSE(Value(SimpleString{"1"}).is_number());

    // is_aggregate covers Array, Map, Set, Push, Attribute.
    EXPECT_TRUE(Value(Array{}).is_aggregate());
    EXPECT_TRUE(Value(Map{}).is_aggregate());
    EXPECT_TRUE(Value(Set{}).is_aggregate());
    EXPECT_TRUE(Value(Push{}).is_aggregate());
    EXPECT_FALSE(Value(Integer{1}).is_aggregate());
}

// ============================================================================
// Aggregate operator== branch coverage (size / element / equal)
// ============================================================================

class EqualityTest : public ::testing::Test {};

TEST_F(EqualityTest, ArrayEquality) {
    auto mk = [](int64_t v) {
        Array a;
        a.elements.push_back(std::make_unique<Value>(Value(Integer{v})));
        return a;
    };
    // size-mismatch -> false
    {
        Array a1 = mk(1);
        Array a2; // empty
        EXPECT_FALSE(a1 == a2);
    }
    // element-mismatch -> false
    EXPECT_FALSE(mk(1) == mk(2));
    // equal -> true
    EXPECT_TRUE(mk(1) == mk(1));
}

TEST_F(EqualityTest, MapEquality) {
    auto make_map = [](int64_t k, int64_t v) {
        Map m;
        m.entries.emplace_back(std::make_unique<Value>(Value(Integer{k})), std::make_unique<Value>(Value(Integer{v})));
        return m;
    };
    { // size-mismatch
        Map a = make_map(1, 2);
        Map b;
        EXPECT_FALSE(a == b);
    }
    EXPECT_FALSE(make_map(1, 2) == make_map(1, 3)); // value differs
    EXPECT_FALSE(make_map(1, 2) == make_map(9, 2)); // key differs
    EXPECT_TRUE(make_map(1, 2) == make_map(1, 2));  // equal
}

TEST_F(EqualityTest, SetEquality) {
    auto mk = [](int64_t v) {
        Set s;
        s.elements.push_back(std::make_unique<Value>(Value(Integer{v})));
        return s;
    };
    {
        Set a = mk(1);
        Set b;
        EXPECT_FALSE(a == b); // size-mismatch
    }
    EXPECT_FALSE(mk(1) == mk(2)); // element-mismatch
    EXPECT_TRUE(mk(7) == mk(7));  // equal
}

TEST_F(EqualityTest, PushEquality) {
    auto mk = [](int64_t v) {
        Push p;
        p.elements.push_back(std::make_unique<Value>(Value(Integer{v})));
        return p;
    };
    {
        Push a = mk(1);
        Push b;
        EXPECT_FALSE(a == b); // size-mismatch
    }
    EXPECT_FALSE(mk(1) == mk(2)); // element-mismatch
    EXPECT_TRUE(mk(5) == mk(5));  // equal
}

TEST_F(EqualityTest, AttributeEqualityAllFourBranches) {
    auto make_attr = [](int64_t k, int64_t mapv, std::optional<int64_t> val) {
        Attribute a;
        a.data.entries.emplace_back(std::make_unique<Value>(Value(Integer{k})), std::make_unique<Value>(Value(Integer{mapv})));
        if (val)
            a.value = std::make_unique<Value>(Value(Integer{*val}));
        return a;
    };

    // Branch 1: data differs -> false.
    EXPECT_FALSE(make_attr(1, 2, 42) == make_attr(1, 9, 42));
    // Branch 2: equal data, both values null -> true.
    EXPECT_TRUE(make_attr(1, 2, std::nullopt) == make_attr(1, 2, std::nullopt));
    // Branch 3: equal data, exactly one value null -> false (both orders).
    EXPECT_FALSE(make_attr(1, 2, 42) == make_attr(1, 2, std::nullopt));
    EXPECT_FALSE(make_attr(1, 2, std::nullopt) == make_attr(1, 2, 42));
    // Branch 4: equal data, both values present -> compare values.
    EXPECT_TRUE(make_attr(1, 2, 42) == make_attr(1, 2, 42));
    EXPECT_FALSE(make_attr(1, 2, 42) == make_attr(1, 2, 99));
}

// ============================================================================
// full_message() three branches (SimpleError + BulkError)
// ============================================================================

TEST(ErrorFullMessage, ThreeBranchesEach) {
    EXPECT_EQ((SimpleError{"", "only-msg"}).full_message(), "only-msg"); // empty prefix
    EXPECT_EQ((SimpleError{"ONLY", ""}).full_message(), "ONLY");         // empty message
    EXPECT_EQ((SimpleError{"ERR", "boom"}).full_message(), "ERR boom");  // both

    EXPECT_EQ((BulkError{"", "only-msg"}).full_message(), "only-msg");
    EXPECT_EQ((BulkError{"ONLY", ""}).full_message(), "ONLY");
    EXPECT_EQ((BulkError{"ERR", "boom"}).full_message(), "ERR boom");
}

// ============================================================================
// ParseError::what() for EVERY code + code()/message() accessors
// ============================================================================

TEST(ParseErrorWhat, EveryCode) {
    EXPECT_STREQ(ParseError(ParseErrorCode::OK).what(), "OK");
    EXPECT_STREQ(ParseError(ParseErrorCode::INCOMPLETE_DATA).what(), "Incomplete data");
    EXPECT_STREQ(ParseError(ParseErrorCode::INVALID_TYPE).what(), "Invalid type");
    EXPECT_STREQ(ParseError(ParseErrorCode::INVALID_LENGTH).what(), "Invalid length");
    EXPECT_STREQ(ParseError(ParseErrorCode::INVALID_INTEGER).what(), "Invalid integer");
    EXPECT_STREQ(ParseError(ParseErrorCode::INVALID_DOUBLE).what(), "Invalid double");
    EXPECT_STREQ(ParseError(ParseErrorCode::INVALID_BOOLEAN).what(), "Invalid boolean");
    EXPECT_STREQ(ParseError(ParseErrorCode::INVALID_BIG_NUMBER).what(), "Invalid big number");
    EXPECT_STREQ(ParseError(ParseErrorCode::INVALID_VERBATIM_FORMAT).what(), "Invalid verbatim format");
    EXPECT_STREQ(ParseError(ParseErrorCode::NESTING_TOO_DEEP).what(), "Nesting too deep");
    EXPECT_STREQ(ParseError(ParseErrorCode::BUFFER_OVERFLOW).what(), "Buffer overflow");
    EXPECT_STREQ(ParseError(ParseErrorCode::PROTOCOL_ERROR).what(), "Protocol error");
}

TEST(ParseErrorWhat, CodeAndMessageAccessors) {
    ParseError pe(ParseErrorCode::INVALID_LENGTH, "bad len");
    EXPECT_EQ(pe.code(), ParseErrorCode::INVALID_LENGTH);
    EXPECT_EQ(pe.message(), "bad len");

    // Default (empty) message accessor.
    ParseError bare(ParseErrorCode::OK);
    EXPECT_TRUE(bare.message().empty());
}

// ============================================================================
// Scalar implicit conversions + string accessors
// ============================================================================

TEST(ScalarConversions, ImplicitOperators) {
    {
        Integer i{1234};
        int64_t n = i; // implicit
        EXPECT_EQ(n, 1234);
        EXPECT_EQ(static_cast<int64_t>(Integer{-7}), -7);
    }
    {
        Boolean t{true};
        Boolean f{false};
        bool    bt = t; // implicit
        bool    bf = f;
        EXPECT_TRUE(bt);
        EXPECT_FALSE(bf);
    }
    {
        Double d{3.5};
        double x = d; // implicit
        EXPECT_DOUBLE_EQ(x, 3.5);
        EXPECT_DOUBLE_EQ(static_cast<double>(Double{-1.25}), -1.25);
    }
}

TEST(ScalarConversions, BulkAndVerbatimAccessors) {
    {
        BulkString b{"hello"};
        EXPECT_EQ(b.view(), "hello");
        EXPECT_FALSE(b.empty());
        EXPECT_EQ(b.size(), 5u);
    }
    {
        BulkString b{""};
        EXPECT_TRUE(b.view().empty());
        EXPECT_TRUE(b.empty());
        EXPECT_EQ(b.size(), 0u);
    }
    {
        VerbatimString vs;
        vs.encoding[0] = 'm';
        vs.encoding[1] = 'k';
        vs.encoding[2] = 'd';
        vs.value       = "body";
        auto ev        = vs.encoding_view();
        EXPECT_EQ(ev.size(), VerbatimString::ENCODING_LEN);
        EXPECT_EQ(ev, "mkd");
    }
}

// ============================================================================
// constexpr type-prefix predicates
// ============================================================================

TEST(TypePrefixHelpers, ValidResp3Aggregate) {
    // is_valid_type_prefix: one true per family + default false.
    EXPECT_TRUE(is_valid_type_prefix(type_id::SIMPLE_STRING));
    EXPECT_TRUE(is_valid_type_prefix(type_id::SIMPLE_ERROR));
    EXPECT_TRUE(is_valid_type_prefix(type_id::INTEGER));
    EXPECT_TRUE(is_valid_type_prefix(type_id::BULK_STRING));
    EXPECT_TRUE(is_valid_type_prefix(type_id::ARRAY));
    EXPECT_TRUE(is_valid_type_prefix(type_id::NULL_));
    EXPECT_TRUE(is_valid_type_prefix(type_id::BOOLEAN));
    EXPECT_TRUE(is_valid_type_prefix(type_id::DOUBLE));
    EXPECT_TRUE(is_valid_type_prefix(type_id::BIG_NUMBER));
    EXPECT_TRUE(is_valid_type_prefix(type_id::BULK_ERROR));
    EXPECT_TRUE(is_valid_type_prefix(type_id::VERBATIM_STRING));
    EXPECT_TRUE(is_valid_type_prefix(type_id::MAP));
    EXPECT_TRUE(is_valid_type_prefix(type_id::ATTRIBUTE));
    EXPECT_TRUE(is_valid_type_prefix(type_id::SET));
    EXPECT_TRUE(is_valid_type_prefix(type_id::PUSH));
    EXPECT_FALSE(is_valid_type_prefix('X'));
    EXPECT_FALSE(is_valid_type_prefix('\0'));

    // is_resp3_type: RESP3-only true, RESP2 + junk false.
    EXPECT_TRUE(is_resp3_type(type_id::NULL_));
    EXPECT_TRUE(is_resp3_type(type_id::BOOLEAN));
    EXPECT_TRUE(is_resp3_type(type_id::DOUBLE));
    EXPECT_TRUE(is_resp3_type(type_id::BIG_NUMBER));
    EXPECT_TRUE(is_resp3_type(type_id::BULK_ERROR));
    EXPECT_TRUE(is_resp3_type(type_id::VERBATIM_STRING));
    EXPECT_TRUE(is_resp3_type(type_id::MAP));
    EXPECT_TRUE(is_resp3_type(type_id::ATTRIBUTE));
    EXPECT_TRUE(is_resp3_type(type_id::SET));
    EXPECT_TRUE(is_resp3_type(type_id::PUSH));
    EXPECT_FALSE(is_resp3_type(type_id::SIMPLE_STRING));
    EXPECT_FALSE(is_resp3_type(type_id::INTEGER));
    EXPECT_FALSE(is_resp3_type('X'));

    // is_aggregate_type: the five aggregates true, scalars + junk false.
    EXPECT_TRUE(is_aggregate_type(type_id::ARRAY));
    EXPECT_TRUE(is_aggregate_type(type_id::MAP));
    EXPECT_TRUE(is_aggregate_type(type_id::ATTRIBUTE));
    EXPECT_TRUE(is_aggregate_type(type_id::SET));
    EXPECT_TRUE(is_aggregate_type(type_id::PUSH));
    EXPECT_FALSE(is_aggregate_type(type_id::SIMPLE_STRING));
    EXPECT_FALSE(is_aggregate_type(type_id::INTEGER));
    EXPECT_FALSE(is_aggregate_type('X'));

    // constexpr evaluation context.
    static_assert(is_valid_type_prefix(type_id::ARRAY));
    static_assert(!is_valid_type_prefix('X'));
    static_assert(is_resp3_type(type_id::MAP));
    static_assert(!is_resp3_type(type_id::INTEGER));
    static_assert(is_aggregate_type(type_id::SET));
    static_assert(!is_aggregate_type(type_id::INTEGER));
}
