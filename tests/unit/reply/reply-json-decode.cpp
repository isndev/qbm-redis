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
// Unit tier (pure logic, NO eval / NO daemon / NO loop): RESP -> JSON
// reconstruction. Covers BOTH json targets:
//   - parse<qb::json>      (the {…}/[…] string auto-detect + flat-map heuristic)
//   - parse<json_value>    (the qb::redis::json_value variant tree)
//
// All inputs are canned parser::Value trees built via reply_value_builders.h —
// the same shapes the live eval<qb::json> integration tests produce, but with
// zero round-trip cost. The eval<qb::json>/cjson round-trips stay in the
// integration tier (integration/scripting/eval-json.cpp).
//

#include <cmath>
#include <gtest/gtest.h>
#include <limits>
#include <string>
#include "../../shared/reply_value_builders.h"

using namespace qb::redis::test;
using qb::redis::parser::BigNumber;
using qb::redis::parser::Boolean;
using qb::redis::parser::BulkError;
using qb::redis::parser::SimpleError;

// ============================================================================
// 1. parse<qb::json> - all scalar types
// ============================================================================

TEST(ReplyJsonScalar, Integer) {
    Value v(Integer{42});
    auto  j = do_parse<qb::json>(v);
    EXPECT_TRUE(j.is_number_integer());
    EXPECT_EQ(j.get<int64_t>(), 42);
}

TEST(ReplyJsonScalar, Double) {
    Value v(Double{3.5});
    auto  j = do_parse<qb::json>(v);
    EXPECT_TRUE(j.is_number());
    EXPECT_DOUBLE_EQ(j.get<double>(), 3.5);
}

TEST(ReplyJsonScalar, Boolean) {
    Value v(Boolean{true});
    auto  j = do_parse<qb::json>(v);
    EXPECT_TRUE(j.is_boolean());
    EXPECT_TRUE(j.get<bool>());
}

TEST(ReplyJsonScalar, Null) {
    Value v(Null{});
    auto  j = do_parse<qb::json>(v);
    EXPECT_TRUE(j.is_null());
}

TEST(ReplyJsonScalar, BigNumber) {
    // BigNumber maps to a JSON string (arbitrary precision is not representable as a number)
    Value v(BigNumber{"123456789012345678901234567890", false});
    auto  j = do_parse<qb::json>(v);
    EXPECT_TRUE(j.is_string());
    EXPECT_EQ(j.get<std::string>(), "123456789012345678901234567890");
}

TEST(ReplyJsonScalar, BulkStringPlain) {
    Value v(BulkString{"hello"});
    auto  j = do_parse<qb::json>(v);
    EXPECT_TRUE(j.is_string());
    EXPECT_EQ(j.get<std::string>(), "hello");
}

// ADD: a 64-bit integer above the 2^53 double-exact boundary must keep all 64
// bits (parse<qb::json> uses qb::json(int64_t), never a double).
TEST(ReplyJsonScalar, IntegerNoTruncation) {
    constexpr int64_t big = 9007199254740993LL; // 2^53 + 1
    Value             v(Integer{big});
    auto              j = do_parse<qb::json>(v);
    ASSERT_TRUE(j.is_number_integer());
    EXPECT_EQ(j.get<int64_t>(), big);
}

// ADD: a native RESP3 Double of +inf decodes to a JSON number reporting inf.
TEST(ReplyJsonScalar, DoubleInfinity) {
    Value v(Double{std::numeric_limits<double>::infinity()});
    auto  j = do_parse<qb::json>(v);
    ASSERT_TRUE(j.is_number());
    EXPECT_TRUE(std::isinf(j.get<double>()));
}

// ADD: a native RESP3 Double of NaN decodes to a JSON number reporting NaN.
TEST(ReplyJsonScalar, DoubleNan) {
    Value v(Double{std::numeric_limits<double>::quiet_NaN()});
    auto  j = do_parse<qb::json>(v);
    ASSERT_TRUE(j.is_number());
    EXPECT_TRUE(std::isnan(j.get<double>()));
}

// ============================================================================
// 2. parse<qb::json> - string heuristic, flat-map->object, array->array
// ============================================================================

TEST(ReplyJsonHeuristic, BulkStringJsonObject) {
    // A bulk string that looks like JSON ({...}) is parsed as structured JSON.
    Value v(BulkString{R"({"a":1,"b":"two"})"});
    auto  j = do_parse<qb::json>(v);
    EXPECT_TRUE(j.is_object());
    EXPECT_EQ(j["a"], 1);
    EXPECT_EQ(j["b"], "two");
}

TEST(ReplyJsonHeuristic, BulkStringJsonArray) {
    Value v(BulkString{"[1,2,3]"});
    auto  j = do_parse<qb::json>(v);
    EXPECT_TRUE(j.is_array());
    EXPECT_EQ(j.size(), 3u);
    EXPECT_EQ(j[0], 1);
}

TEST(ReplyJsonHeuristic, BulkStringLooksLikeJsonButInvalid) {
    // Starts with { and ends with } but is not valid JSON => falls back to plain string.
    Value v(BulkString{"{not json}"});
    auto  j = do_parse<qb::json>(v);
    EXPECT_TRUE(j.is_string());
    EXPECT_EQ(j.get<std::string>(), "{not json}");
}

// ADD: leading whitespace defeats the front()=='{'/'[' guard, so the heuristic
// does NOT fire — the value stays a plain string. Locks the (intentional)
// front/back-char gate.
TEST(ReplyJsonHeuristic, LeadingWhitespaceStaysString) {
    Value v(BulkString{"  {\"a\":1}"});
    auto  j = do_parse<qb::json>(v);
    EXPECT_TRUE(j.is_string());
    EXPECT_EQ(j.get<std::string>(), "  {\"a\":1}");
}

// ADD: a bare number string is NOT wrapped in {}/[], so it stays a plain string
// (the heuristic is structural-only — it does not coerce scalars).
TEST(ReplyJsonHeuristic, BareNumberStringStaysString) {
    Value v(BulkString{"42"});
    auto  j = do_parse<qb::json>(v);
    EXPECT_TRUE(j.is_string());
    EXPECT_EQ(j.get<std::string>(), "42");
}

// ADD: a single '{' (len <= 1 guard / unbalanced) is not treated as JSON.
TEST(ReplyJsonHeuristic, SingleBraceStaysString) {
    Value v(BulkString{"{"});
    auto  j = do_parse<qb::json>(v);
    EXPECT_TRUE(j.is_string());
    EXPECT_EQ(j.get<std::string>(), "{");
}

// ADD: a valid embedded JSON object with unicode escapes round-trips correctly.
TEST(ReplyJsonHeuristic, UnicodeEscapeInEmbeddedJson) {
    Value v(BulkString{R"({"name":"café"})"});
    auto  j = do_parse<qb::json>(v);
    ASSERT_TRUE(j.is_object());
    EXPECT_EQ(j["name"], std::string("caf\xc3\xa9")); // é in UTF-8
}

// ADD: an embedded JSON number larger than 2^53 must not be truncated by the
// nested qb::json::parse.
TEST(ReplyJsonHeuristic, EmbeddedLargeIntNoTruncation) {
    Value v(BulkString{R"({"id":9007199254740993})"});
    auto  j = do_parse<qb::json>(v);
    ASSERT_TRUE(j.is_object());
    ASSERT_TRUE(j["id"].is_number_integer());
    EXPECT_EQ(j["id"].get<int64_t>(), 9007199254740993LL);
}

TEST(ReplyJsonHeuristic, FlatMapArrayBecomesObject) {
    // Even-sized array, all keys strings, at least one non-string value =>
    // converted to a JSON object (RESP2 flat-map shape, e.g. MEMORY STATS).
    std::vector<std::unique_ptr<Value>> elems;
    elems.push_back(mk_bulk("count"));
    elems.push_back(mk_int(7));
    elems.push_back(mk_bulk("name"));
    elems.push_back(mk_bulk("widget"));
    Value v = make_array(std::move(elems));

    auto j = do_parse<qb::json>(v);
    EXPECT_TRUE(j.is_object());
    EXPECT_EQ(j["count"], 7);
    EXPECT_EQ(j["name"], "widget");
}

TEST(ReplyJsonHeuristic, AllStringArrayStaysArray) {
    // Even-sized array but every value is a string (no non-string value) =>
    // stays a JSON array, NOT an object.
    std::vector<std::unique_ptr<Value>> elems;
    elems.push_back(mk_bulk("a"));
    elems.push_back(mk_bulk("b"));
    Value v = make_array(std::move(elems));

    auto j = do_parse<qb::json>(v);
    EXPECT_TRUE(j.is_array());
    EXPECT_EQ(j.size(), 2u);
}

TEST(ReplyJsonHeuristic, MixedArrayStaysArray) {
    // Odd-sized / first element non-string => plain array.
    std::vector<std::unique_ptr<Value>> elems;
    elems.push_back(mk_int(1));
    elems.push_back(mk_bulk("x"));
    elems.push_back(mk_int(3));
    Value v = make_array(std::move(elems));

    auto j = do_parse<qb::json>(v);
    EXPECT_TRUE(j.is_array());
    EXPECT_EQ(j.size(), 3u);
    EXPECT_EQ(j[0], 1);
}

TEST(ReplyJsonHeuristic, EmptyArray) {
    Value v = make_array({});
    auto  j = do_parse<qb::json>(v);
    EXPECT_TRUE(j.is_array());
    EXPECT_EQ(j.size(), 0u);
}

TEST(ReplyJsonHeuristic, MapBecomesObject) {
    std::vector<std::pair<std::unique_ptr<Value>, std::unique_ptr<Value>>> entries;
    entries.push_back(std::make_pair(mk_bulk("k"), mk_int(9)));
    Value v = make_map(std::move(entries));

    auto j = do_parse<qb::json>(v);
    EXPECT_TRUE(j.is_object());
    EXPECT_EQ(j["k"], 9);
}

TEST(ReplyJsonHeuristic, SetBecomesArray) {
    std::vector<std::unique_ptr<Value>> elems;
    elems.push_back(mk_bulk("one"));
    elems.push_back(mk_bulk("two"));
    Value v = make_set(std::move(elems));

    auto j = do_parse<qb::json>(v);
    EXPECT_TRUE(j.is_array());
    EXPECT_EQ(j.size(), 2u);
}

TEST(ReplyJsonHeuristic, PushBecomesArray) {
    std::vector<std::unique_ptr<Value>> elems;
    elems.push_back(mk_bulk("a"));
    Value v = make_push(std::move(elems));

    auto j = do_parse<qb::json>(v);
    EXPECT_TRUE(j.is_array());
    EXPECT_EQ(j.size(), 1u);
}

// ============================================================================
// 3. parse<qb::json> / parse<json_value> error (SimpleError/BulkError) ->
//    CommandError
// ============================================================================

TEST(ReplyJsonError, SimpleErrorThrowsCommandError) {
    Value v(SimpleError{"ERR", "boom"});
    EXPECT_THROW(do_parse<qb::json>(v), qb::redis::CommandError);
}

TEST(ReplyJsonError, BulkErrorThrowsCommandError) {
    Value v(BulkError{"WRONGTYPE", "bad"});
    EXPECT_THROW(do_parse<qb::json>(v), qb::redis::CommandError);
}

TEST(ReplyJsonValueError, SimpleErrorThrowsCommandError) {
    Value v(SimpleError{"ERR", "nope"});
    EXPECT_THROW(do_parse<qb::redis::json_value>(v), qb::redis::CommandError);
}

// ============================================================================
// 4. parse<json_value> - all types including Map->Object, Array->Array
// ============================================================================

TEST(ReplyJsonValue, Null) {
    Value v(Null{});
    auto  jv = do_parse<qb::redis::json_value>(v);
    EXPECT_EQ(jv.type, qb::redis::json_value::Type::Null);
    EXPECT_TRUE(jv.is_null());
}

TEST(ReplyJsonValue, Integer) {
    Value v(Integer{5});
    auto  jv = do_parse<qb::redis::json_value>(v);
    EXPECT_EQ(jv.type, qb::redis::json_value::Type::Number);
    EXPECT_DOUBLE_EQ(std::get<double>(jv.data), 5.0);
}

TEST(ReplyJsonValue, Double) {
    Value v(Double{2.25});
    auto  jv = do_parse<qb::redis::json_value>(v);
    EXPECT_EQ(jv.type, qb::redis::json_value::Type::Number);
    EXPECT_DOUBLE_EQ(std::get<double>(jv.data), 2.25);
}

TEST(ReplyJsonValue, Boolean) {
    Value v(Boolean{false});
    auto  jv = do_parse<qb::redis::json_value>(v);
    EXPECT_EQ(jv.type, qb::redis::json_value::Type::Boolean);
    EXPECT_FALSE(std::get<bool>(jv.data));
}

TEST(ReplyJsonValue, String) {
    Value v(BulkString{"hi"});
    auto  jv = do_parse<qb::redis::json_value>(v);
    EXPECT_EQ(jv.type, qb::redis::json_value::Type::String);
    EXPECT_EQ(std::get<std::string>(jv.data), "hi");
}

TEST(ReplyJsonValue, BigNumberToString) {
    Value v(BigNumber{"999999999999999999999", false});
    auto  jv = do_parse<qb::redis::json_value>(v);
    EXPECT_EQ(jv.type, qb::redis::json_value::Type::String);
    EXPECT_EQ(std::get<std::string>(jv.data), "999999999999999999999");
}

TEST(ReplyJsonValue, ArrayToArray) {
    std::vector<std::unique_ptr<Value>> elems;
    elems.push_back(mk_int(1));
    elems.push_back(mk_int(2));
    Value v  = make_array(std::move(elems));
    auto  jv = do_parse<qb::redis::json_value>(v);
    EXPECT_EQ(jv.type, qb::redis::json_value::Type::Array);
    const auto &arr = std::get<std::vector<qb::redis::json_value>>(jv.data);
    ASSERT_EQ(arr.size(), 2u);
    EXPECT_EQ(arr[0].type, qb::redis::json_value::Type::Number);
}

TEST(ReplyJsonValue, SetToArray) {
    std::vector<std::unique_ptr<Value>> elems;
    elems.push_back(mk_bulk("x"));
    Value v  = make_set(std::move(elems));
    auto  jv = do_parse<qb::redis::json_value>(v);
    EXPECT_EQ(jv.type, qb::redis::json_value::Type::Array);
    EXPECT_EQ(std::get<std::vector<qb::redis::json_value>>(jv.data).size(), 1u);
}

TEST(ReplyJsonValue, PushToArray) {
    std::vector<std::unique_ptr<Value>> elems;
    elems.push_back(mk_bulk("p"));
    Value v  = make_push(std::move(elems));
    auto  jv = do_parse<qb::redis::json_value>(v);
    EXPECT_EQ(jv.type, qb::redis::json_value::Type::Array);
    EXPECT_EQ(std::get<std::vector<qb::redis::json_value>>(jv.data).size(), 1u);
}

TEST(ReplyJsonValue, MapToObject) {
    std::vector<std::pair<std::unique_ptr<Value>, std::unique_ptr<Value>>> entries;
    entries.push_back(std::make_pair(mk_bulk("key"), mk_bulk("val")));
    Value v  = make_map(std::move(entries));
    auto  jv = do_parse<qb::redis::json_value>(v);
    EXPECT_EQ(jv.type, qb::redis::json_value::Type::Object);
    const auto &obj = std::get<qb::unordered_map<std::string, qb::redis::json_value>>(jv.data);
    ASSERT_EQ(obj.size(), 1u);
    auto it = obj.find("key");
    ASSERT_NE(it, obj.end());
    EXPECT_EQ(it->second.type, qb::redis::json_value::Type::String);
}

// ============================================================================
// 5. RESP3 Attribute: out-of-band metadata precedes the real reply. Both JSON
//    targets must transparently UNWRAP the attribute and decode its inner value,
//    and map an attribute carrying no inner value to JSON null.
// ============================================================================

TEST(ReplyJsonValue, AttributeUnwrapsInnerValue) {
    qb::redis::parser::Attribute attr;
    attr.value = mk_bulk("hi"); // the actual reply behind the metadata
    Value v(std::move(attr));
    auto  jv = do_parse<qb::redis::json_value>(v);
    EXPECT_EQ(jv.type, qb::redis::json_value::Type::String);
    EXPECT_EQ(std::get<std::string>(jv.data), "hi");
}

TEST(ReplyJsonValue, AttributeWithNoInnerValueIsNull) {
    Value v(qb::redis::parser::Attribute{}); // attr.value == nullptr
    auto  jv = do_parse<qb::redis::json_value>(v);
    EXPECT_EQ(jv.type, qb::redis::json_value::Type::Null);
}

TEST(ReplyJsonScalar, AttributeUnwrapsInnerValue) {
    qb::redis::parser::Attribute attr;
    attr.value = mk_bulk("hi");
    Value v(std::move(attr));
    auto  j = do_parse<qb::json>(v);
    ASSERT_TRUE(j.is_string());
    EXPECT_EQ(j.get<std::string>(), "hi");
}

TEST(ReplyJsonScalar, AttributeWithNoInnerValueIsNull) {
    Value v(qb::redis::parser::Attribute{});
    auto  j = do_parse<qb::json>(v);
    EXPECT_TRUE(j.is_null());
}
