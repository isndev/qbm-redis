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
// Unit tier (pure logic, no daemon/loop): the serialization side of the reply
// layer — to_redis_string() per overload (exact RESP wire bytes), redis_count()
// argument-count semantics, put_in_pipe() full command framing, and the
// type_to_string() RESP-kind cascade.
//

#include <chrono>
#include <gtest/gtest.h>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>
#include "../../shared/reply_value_builders.h"

using namespace qb::redis::test;
using namespace qb::redis;

namespace {
// Fresh pipe, serialize one argument, read raw bytes back.
template <typename T>
std::string
ser(const T &v) {
    qb::allocator::pipe<char> p;
    qb::redis::to_redis_string(p, v);
    return p.str();
}
} // namespace

// ============================================================================
// 26. SERIALIZATION: to_redis_string per overload (exact RESP wire bytes)
//
// Each helper writes one bulk string as `$<len>\r\n<payload>\r\n`. We read the
// emitted bytes back with pipe<char>::str() and assert the exact wire form.
// ============================================================================

TEST(ReplySerString, CharArrayLiteral) {
    // const char (&)[N] overload: length is N-1 (NUL excluded).
    qb::allocator::pipe<char> p;
    qb::redis::to_redis_string(p, "hello");
    EXPECT_EQ(p.str(), "$5\r\nhello\r\n");
}

TEST(ReplySerString, ConstCharPtr) {
    const char               *s = "world";
    qb::allocator::pipe<char> p;
    qb::redis::to_redis_string(p, s);
    EXPECT_EQ(p.str(), "$5\r\nworld\r\n");
}

TEST(ReplySerString, ConstCharPtrNullBecomesEmpty) {
    // null const char* -> str = "" -> "$0\r\n\r\n"
    const char               *s = nullptr;
    qb::allocator::pipe<char> p;
    qb::redis::to_redis_string(p, s);
    EXPECT_EQ(p.str(), "$0\r\n\r\n");
}

TEST(ReplySerString, StringView) {
    EXPECT_EQ(ser(std::string_view{"hello"}), "$5\r\nhello\r\n");
}

TEST(ReplySerString, StringViewEmpty) {
    EXPECT_EQ(ser(std::string_view{""}), "$0\r\n\r\n");
}

TEST(ReplySerString, StdString) {
    EXPECT_EQ(ser(std::string{"abc"}), "$3\r\nabc\r\n");
}

TEST(ReplySerArithmetic, Int) {
    // arithmetic -> std::to_string -> string overload.
    EXPECT_EQ(ser(42), "$2\r\n42\r\n");
}

TEST(ReplySerArithmetic, NegativeInt) {
    EXPECT_EQ(ser(-7), "$2\r\n-7\r\n");
}

TEST(ReplySerArithmetic, Double) {
    // std::to_string(double) yields fixed 6-decimal notation.
    EXPECT_EQ(ser(1.5), "$8\r\n1.500000\r\n");
}

TEST(ReplySerOptional, Engaged) {
    std::optional<std::string> opt{"hi"};
    EXPECT_EQ(ser(opt), "$2\r\nhi\r\n");
}

TEST(ReplySerOptional, Disengaged) {
    // Disengaged optional emits NOTHING.
    std::optional<std::string> opt;
    EXPECT_EQ(ser(opt), "");
    EXPECT_EQ(qb::redis::redis_count(opt), 0u);
}

TEST(ReplySerTuple, MultiElement) {
    // put_tuple emits each element in order; no leading '*' count.
    std::tuple<const char *, int> t{"k", 9};
    qb::allocator::pipe<char>     p;
    qb::redis::to_redis_string(p, t);
    EXPECT_EQ(p.str(), "$1\r\nk\r\n$1\r\n9\r\n");
}

TEST(ReplySerPair, Concatenated) {
    std::pair<std::string, long long> pr{"f", 3};
    qb::allocator::pipe<char>         p;
    qb::redis::to_redis_string(p, pr);
    EXPECT_EQ(p.str(), "$1\r\nf\r\n$1\r\n3\r\n");
}

TEST(ReplySerContainer, SequenceVector) {
    // qb::is_container vector<string> -> each element serialized in turn.
    std::vector<std::string>  v{"a", "bb"};
    qb::allocator::pipe<char> p;
    qb::redis::to_redis_string(p, v);
    EXPECT_EQ(p.str(), "$1\r\na\r\n$2\r\nbb\r\n");
}

TEST(ReplySerContainer, MapEmitsKeyThenValue) {
    // map iterator -> k then v for each pair (std::map gives sorted order).
    std::map<std::string, std::string> m{{"k1", "v1"}, {"k2", "v2"}};
    qb::allocator::pipe<char>          p;
    qb::redis::to_redis_string(p, m);
    EXPECT_EQ(p.str(), "$2\r\nk1\r\n$2\r\nv1\r\n$2\r\nk2\r\n$2\r\nv2\r\n");
}

TEST(ReplySerVectorChar, BinaryWithEmbeddedNul) {
    // vector<char> overload writes raw bytes verbatim, including embedded NUL.
    std::vector<char>         bin{'a', '\0', 'b'};
    qb::allocator::pipe<char> p;
    qb::redis::to_redis_string(p, bin);
    std::string       out      = p.str();
    const std::string expected = std::string("$3\r\na") + '\0' + std::string("b\r\n");
    ASSERT_EQ(out.size(), expected.size());
    EXPECT_EQ(out, expected);
}

TEST(ReplySerVectorChar, Empty) {
    std::vector<char>         bin;
    qb::allocator::pipe<char> p;
    qb::redis::to_redis_string(p, bin);
    EXPECT_EQ(p.str(), "$0\r\n\r\n");
}

TEST(ReplySerChrono, Milliseconds) {
    EXPECT_EQ(ser(std::chrono::milliseconds{1500}), "$4\r\n1500\r\n");
}

TEST(ReplySerChrono, Seconds) {
    EXPECT_EQ(ser(std::chrono::seconds{30}), "$2\r\n30\r\n");
}

TEST(ReplySerGeoPos, LongitudeThenLatitude) {
    qb::redis::geo_pos        pos{1.0, 2.0};
    qb::allocator::pipe<char> p;
    qb::redis::to_redis_string(p, pos);
    // longitude then latitude, each as std::to_string(double).
    EXPECT_EQ(p.str(), "$8\r\n1.000000\r\n$8\r\n2.000000\r\n");
}

TEST(ReplySerStreamId, ToStringForm) {
    qb::redis::stream_id      id{1234567890LL, 5LL};
    qb::allocator::pipe<char> p;
    qb::redis::to_redis_string(p, id);
    // id.to_string() == "1234567890-5" (length 12).
    EXPECT_EQ(p.str(), "$12\r\n1234567890-5\r\n");
}

TEST(ReplySerScore, SingleDouble) {
    qb::redis::score          sc{2.5};
    qb::allocator::pipe<char> p;
    qb::redis::to_redis_string(p, sc);
    EXPECT_EQ(p.str(), "$8\r\n2.500000\r\n");
}

TEST(ReplySerScoreMember, ScoreThenMember) {
    qb::redis::score_member   sm{1.0, "alice"};
    qb::allocator::pipe<char> p;
    qb::redis::to_redis_string(p, sm);
    // sm.score first, then sm.member.
    EXPECT_EQ(p.str(), "$8\r\n1.000000\r\n$5\r\nalice\r\n");
}

TEST(ReplySerSearchResult, KeyFieldsValues) {
    qb::redis::search_result sr;
    sr.key    = "doc:1";
    sr.fields = {"title"};
    sr.values = {"Hi"};
    qb::allocator::pipe<char> p;
    qb::redis::to_redis_string(p, sr);
    // key, then all fields, then all values.
    EXPECT_EQ(p.str(), "$5\r\ndoc:1\r\n$5\r\ntitle\r\n$2\r\nHi\r\n");
}

TEST(ReplySerClusterNode, OnlyId) {
    qb::redis::cluster_node node;
    node.id = "nodeABC";
    qb::allocator::pipe<char> p;
    qb::redis::to_redis_string(p, node);
    EXPECT_EQ(p.str(), "$7\r\nnodeABC\r\n");
}

// --- json_value 6-arm switch ---

TEST(ReplySerJsonValue, Null) {
    qb::redis::json_value     jv{qb::redis::json_value::Type::Null, nullptr};
    qb::allocator::pipe<char> p;
    qb::redis::to_redis_string(p, jv);
    EXPECT_EQ(p.str(), "$4\r\nnull\r\n");
}

TEST(ReplySerJsonValue, BooleanTrue) {
    qb::redis::json_value     jv{qb::redis::json_value::Type::Boolean, true};
    qb::allocator::pipe<char> p;
    qb::redis::to_redis_string(p, jv);
    EXPECT_EQ(p.str(), "$4\r\ntrue\r\n");
}

TEST(ReplySerJsonValue, Number) {
    qb::redis::json_value     jv{qb::redis::json_value::Type::Number, 3.0};
    qb::allocator::pipe<char> p;
    qb::redis::to_redis_string(p, jv);
    // std::to_string(3.0) == "3.000000".
    EXPECT_EQ(p.str(), "$8\r\n3.000000\r\n");
}

TEST(ReplySerJsonValue, String) {
    qb::redis::json_value     jv{qb::redis::json_value::Type::String, std::string("hey")};
    qb::allocator::pipe<char> p;
    qb::redis::to_redis_string(p, jv);
    EXPECT_EQ(p.str(), "$3\r\nhey\r\n");
}

TEST(ReplySerJsonValue, Array) {
    std::vector<qb::redis::json_value> arr;
    arr.push_back(qb::redis::json_value{qb::redis::json_value::Type::String, std::string("x")});
    arr.push_back(qb::redis::json_value{qb::redis::json_value::Type::String, std::string("yy")});
    qb::redis::json_value     jv{qb::redis::json_value::Type::Array, std::move(arr)};
    qb::allocator::pipe<char> p;
    qb::redis::to_redis_string(p, jv);
    EXPECT_EQ(p.str(), "$1\r\nx\r\n$2\r\nyy\r\n");
}

TEST(ReplySerJsonValue, Object) {
    qb::unordered_map<std::string, qb::redis::json_value> obj;
    obj.emplace("k", qb::redis::json_value{qb::redis::json_value::Type::String, std::string("v")});
    qb::redis::json_value     jv{qb::redis::json_value::Type::Object, std::move(obj)};
    qb::allocator::pipe<char> p;
    qb::redis::to_redis_string(p, jv);
    // single entry: key then value.
    EXPECT_EQ(p.str(), "$1\r\nk\r\n$1\r\nv\r\n");
}

// --- qb::json arm: int-vs-double + null/bool/string/array/object ---

TEST(ReplySerQbJson, Null) {
    qb::json                  j(nullptr);
    qb::allocator::pipe<char> p;
    qb::redis::to_redis_string(p, j);
    EXPECT_EQ(p.str(), "$4\r\nnull\r\n");
}

TEST(ReplySerQbJson, BooleanFalse) {
    qb::json                  j(false);
    qb::allocator::pipe<char> p;
    qb::redis::to_redis_string(p, j);
    EXPECT_EQ(p.str(), "$5\r\nfalse\r\n");
}

TEST(ReplySerQbJson, NumberInteger) {
    // is_number_integer -> std::to_string(int64_t).
    qb::json                  j(42);
    qb::allocator::pipe<char> p;
    qb::redis::to_redis_string(p, j);
    EXPECT_EQ(p.str(), "$2\r\n42\r\n");
}

// ADD: a 64-bit integer above the double-exact boundary must serialize without
// truncation (qb::json int arm uses std::to_string(int64_t), not a double).
TEST(ReplySerQbJson, NumberIntegerLargeNoTruncation) {
    qb::json                  j(static_cast<int64_t>(9007199254740993LL)); // 2^53 + 1
    qb::allocator::pipe<char> p;
    qb::redis::to_redis_string(p, j);
    EXPECT_EQ(p.str(), "$16\r\n9007199254740993\r\n");
}

TEST(ReplySerQbJson, NumberDouble) {
    // non-integer number -> std::to_string(double).
    qb::json                  j(2.5);
    qb::allocator::pipe<char> p;
    qb::redis::to_redis_string(p, j);
    EXPECT_EQ(p.str(), "$8\r\n2.500000\r\n");
}

TEST(ReplySerQbJson, String) {
    qb::json                  j("hi");
    qb::allocator::pipe<char> p;
    qb::redis::to_redis_string(p, j);
    EXPECT_EQ(p.str(), "$2\r\nhi\r\n");
}

TEST(ReplySerQbJson, Array) {
    qb::json j = qb::json::array();
    j.push_back("a");
    j.push_back("b");
    qb::allocator::pipe<char> p;
    qb::redis::to_redis_string(p, j);
    EXPECT_EQ(p.str(), "$1\r\na\r\n$1\r\nb\r\n");
}

TEST(ReplySerQbJson, Object) {
    qb::json j = qb::json::object();
    j["k"]     = "v";
    qb::allocator::pipe<char> p;
    qb::redis::to_redis_string(p, j);
    // single key/value: key then value.
    EXPECT_EQ(p.str(), "$1\r\nk\r\n$1\r\nv\r\n");
}

// ============================================================================
// 27. redis_count per overload (argument-count semantics)
// ============================================================================

TEST(ReplyCount, CharArrayLiteral) {
    EXPECT_EQ(qb::redis::redis_count("hello"), 1u);
}

TEST(ReplyCount, ConstCharPtr) {
    const char *s = "x";
    EXPECT_EQ(qb::redis::redis_count(s), 1u);
}

TEST(ReplyCount, StringView) {
    EXPECT_EQ(qb::redis::redis_count(std::string_view{"x"}), 1u);
}

TEST(ReplyCount, StdString) {
    EXPECT_EQ(qb::redis::redis_count(std::string{"x"}), 1u);
}

TEST(ReplyCount, Arithmetic) {
    EXPECT_EQ(qb::redis::redis_count(42), 1u);
}

TEST(ReplyCount, Tuple) {
    EXPECT_EQ((qb::redis::redis_count(std::tuple<int, std::string, double>{1, "a", 2.0})), 3u);
}

TEST(ReplyCount, Pair) {
    EXPECT_EQ((qb::redis::redis_count(std::pair<int, std::string>{1, "a"})), 2u);
}

TEST(ReplyCount, OptionalEngaged) {
    std::optional<std::string> opt{"x"};
    EXPECT_EQ(qb::redis::redis_count(opt), 1u);
}

TEST(ReplyCount, OptionalDisengaged) {
    std::optional<std::string> opt;
    EXPECT_EQ(qb::redis::redis_count(opt), 0u);
}

TEST(ReplyCount, ContainerEmptyIsZero) {
    std::vector<std::string> v;
    EXPECT_EQ(qb::redis::redis_count(v), 0u);
}

TEST(ReplyCount, ContainerPerElemTimesSize) {
    // per-element count (1 for string) * size.
    std::vector<std::string> v{"a", "b", "c"};
    EXPECT_EQ(qb::redis::redis_count(v), 3u);
}

TEST(ReplyCount, Score) {
    EXPECT_EQ(qb::redis::redis_count(qb::redis::score{1.0}), 1u);
}

TEST(ReplyCount, ScoreMember) {
    EXPECT_EQ(qb::redis::redis_count(qb::redis::score_member{1.0, "m"}), 2u);
}

TEST(ReplyCount, SearchResult) {
    // 1 + fields.size() + values.size().
    qb::redis::search_result sr;
    sr.key    = "k";
    sr.fields = {"a", "b"};
    sr.values = {"x", "y"};
    EXPECT_EQ(qb::redis::redis_count(sr), 1u + 2u + 2u);
}

TEST(ReplyCount, GeoPos) {
    EXPECT_EQ(qb::redis::redis_count(qb::redis::geo_pos{1.0, 2.0}), 2u);
}

TEST(ReplyCount, StreamId) {
    EXPECT_EQ(qb::redis::redis_count(qb::redis::stream_id{1, 2}), 1u);
}

TEST(ReplyCount, ClusterNode) {
    qb::redis::cluster_node node;
    node.id = "n";
    EXPECT_EQ(qb::redis::redis_count(node), 1u);
}

TEST(ReplyCount, MemoryInfoIsZero) {
    EXPECT_EQ(qb::redis::redis_count(qb::redis::memory_info{}), 0u);
}

// --- json_value recursive counting ---

TEST(ReplyCountJsonValue, ScalarsAreOne) {
    using JV = qb::redis::json_value;
    EXPECT_EQ(qb::redis::redis_count(JV{JV::Type::Null, nullptr}), 1u);
    EXPECT_EQ(qb::redis::redis_count(JV{JV::Type::Boolean, true}), 1u);
    EXPECT_EQ(qb::redis::redis_count(JV{JV::Type::Number, 1.0}), 1u);
    EXPECT_EQ(qb::redis::redis_count(JV{JV::Type::String, std::string("s")}), 1u);
}

TEST(ReplyCountJsonValue, ArraySumsElements) {
    using JV = qb::redis::json_value;
    std::vector<JV> arr;
    arr.push_back(JV{JV::Type::String, std::string("a")});
    arr.push_back(JV{JV::Type::Number, 2.0});
    EXPECT_EQ(qb::redis::redis_count(JV{JV::Type::Array, std::move(arr)}), 2u);
}

TEST(ReplyCountJsonValue, ObjectCountsKeyPlusValue) {
    using JV = qb::redis::json_value;
    qb::unordered_map<std::string, JV> obj;
    obj.emplace("k1", JV{JV::Type::String, std::string("v1")});
    obj.emplace("k2", JV{JV::Type::Number, 3.0});
    // 2 entries: each contributes 1 (key) + 1 (scalar value) = 4.
    EXPECT_EQ(qb::redis::redis_count(JV{JV::Type::Object, std::move(obj)}), 4u);
}

// --- qb::json recursive counting ---

TEST(ReplyCountQbJson, NullIsOne) {
    EXPECT_EQ(qb::redis::redis_count(qb::json(nullptr)), 1u);
}

TEST(ReplyCountQbJson, ScalarIsOne) {
    EXPECT_EQ(qb::redis::redis_count(qb::json(5)), 1u);
    EXPECT_EQ(qb::redis::redis_count(qb::json("str")), 1u);
    EXPECT_EQ(qb::redis::redis_count(qb::json(true)), 1u);
}

TEST(ReplyCountQbJson, ArraySumsElements) {
    qb::json j = qb::json::array();
    j.push_back("a");
    j.push_back(2);
    j.push_back(3.5);
    EXPECT_EQ(qb::redis::redis_count(j), 3u);
}

TEST(ReplyCountQbJson, ObjectKeyPlusValue) {
    qb::json j = qb::json::object();
    j["a"]     = 1;
    j["b"]     = "x";
    // 2 entries * (1 key + 1 value) = 4.
    EXPECT_EQ(qb::redis::redis_count(j), 4u);
}

// ============================================================================
// 28. put_in_pipe - full command framing (*<count>\r\n + each arg)
// ============================================================================

TEST(ReplyPutInPipe, SetCommand) {
    qb::allocator::pipe<char> p;
    qb::redis::put_in_pipe(p, "SET", "k", 42);
    EXPECT_EQ(p.str(), "*3\r\n$3\r\nSET\r\n$1\r\nk\r\n$2\r\n42\r\n");
}

TEST(ReplyPutInPipe, SingleArg) {
    qb::allocator::pipe<char> p;
    qb::redis::put_in_pipe(p, "PING");
    EXPECT_EQ(p.str(), "*1\r\n$4\r\nPING\r\n");
}

TEST(ReplyPutInPipe, CountAggregatesVariadic) {
    // A pair counts as 2 args; total count must reflect that.
    qb::allocator::pipe<char> p;
    qb::redis::put_in_pipe(p, "HSET", std::pair<std::string, std::string>{"f", "v"});
    EXPECT_EQ(p.str(), "*3\r\n$4\r\nHSET\r\n$1\r\nf\r\n$1\r\nv\r\n");
}

// ADD: a disengaged optional arg must drop OUT of the '*' count, not just emit
// no payload — proving redis_count and to_redis_string stay in lockstep.
TEST(ReplyPutInPipe, DisengagedOptionalDropsFromCount) {
    qb::allocator::pipe<char>  p;
    std::optional<std::string> absent;
    qb::redis::put_in_pipe(p, "GET", "k", absent);
    EXPECT_EQ(p.str(), "*2\r\n$3\r\nGET\r\n$1\r\nk\r\n");
}

// ============================================================================
// 29. type_to_string - one arm per RESP kind (reply.cpp cascade)
// ============================================================================

TEST(ReplyTypeToString, SimpleString) {
    Value v(SimpleString{"OK"});
    EXPECT_EQ(qb::redis::reply::type_to_string(v), "SIMPLE_STRING");
}

TEST(ReplyTypeToString, SimpleError) {
    Value v(qb::redis::parser::SimpleError{"ERR", "x"});
    EXPECT_EQ(qb::redis::reply::type_to_string(v), "SIMPLE_ERROR");
}

TEST(ReplyTypeToString, BulkString) {
    Value v(BulkString{"data"});
    EXPECT_EQ(qb::redis::reply::type_to_string(v), "BULK_STRING");
}

TEST(ReplyTypeToString, BulkError) {
    Value v(qb::redis::parser::BulkError{"WRONGTYPE", "bad"});
    EXPECT_EQ(qb::redis::reply::type_to_string(v), "BULK_ERROR");
}

TEST(ReplyTypeToString, Integer) {
    Value v(Integer{1});
    EXPECT_EQ(qb::redis::reply::type_to_string(v), "INTEGER");
}

TEST(ReplyTypeToString, Double) {
    Value v(Double{1.5});
    EXPECT_EQ(qb::redis::reply::type_to_string(v), "DOUBLE");
}

TEST(ReplyTypeToString, Boolean) {
    Value v(qb::redis::parser::Boolean{true});
    EXPECT_EQ(qb::redis::reply::type_to_string(v), "BOOLEAN");
}

TEST(ReplyTypeToString, Null) {
    Value v(Null{});
    EXPECT_EQ(qb::redis::reply::type_to_string(v), "NULL");
}

TEST(ReplyTypeToString, Array) {
    Value v = make_array({});
    EXPECT_EQ(qb::redis::reply::type_to_string(v), "ARRAY");
}

TEST(ReplyTypeToString, Map) {
    Value v = make_map({});
    EXPECT_EQ(qb::redis::reply::type_to_string(v), "MAP");
}

TEST(ReplyTypeToString, Set) {
    Value v = make_set({});
    EXPECT_EQ(qb::redis::reply::type_to_string(v), "SET");
}

TEST(ReplyTypeToString, Push) {
    Value v = make_push({});
    EXPECT_EQ(qb::redis::reply::type_to_string(v), "PUSH");
}

TEST(ReplyTypeToString, Attribute) {
    qb::redis::parser::Attribute attr;
    Value                        v(std::move(attr));
    EXPECT_EQ(qb::redis::reply::type_to_string(v), "ATTRIBUTE");
}

TEST(ReplyTypeToString, VerbatimString) {
    // VerbatimString is_string() but is neither simple nor bulk string.
    // encoding is char[3]; build the struct field-by-field to avoid char[4] literal mismatch.
    qb::redis::parser::VerbatimString vs;
    vs.encoding[0] = 't';
    vs.encoding[1] = 'x';
    vs.encoding[2] = 't';
    vs.value       = "hello";
    Value v(std::move(vs));
    EXPECT_EQ(qb::redis::reply::type_to_string(v), "VERBATIM_STRING");
}

TEST(ReplyTypeToString, BigNumber) {
    Value v(qb::redis::parser::BigNumber{"123456789012345678901234567890", false});
    EXPECT_EQ(qb::redis::reply::type_to_string(v), "BIG_NUMBER");
}

// ReplyParseError diagnostic embeds type_to_string of the offending reply.
TEST(ReplyParseErrorInfo, EmbedsActualType) {
    Value v(Integer{1});
    try {
        (void) do_parse<std::vector<std::string>>(v); // expects ARRAY or SET
        FAIL() << "expected throw";
    } catch (const qb::redis::ProtoError &e) {
        const std::string msg = e.what();
        EXPECT_NE(msg.find("INTEGER"), std::string::npos);
        EXPECT_NE(msg.find("expect"), std::string::npos);
    }
}
