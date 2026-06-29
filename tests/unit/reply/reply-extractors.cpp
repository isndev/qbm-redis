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
// Unit tier (pure logic, no daemon/loop): the server-side reply consumption
// layer — ServerReply<T>/<void>, ValueExtractor optional accessors,
// AsyncResult<T>/<void>, and the free extract_*() helpers (string, integer,
// string array/map, stream id, score member).
//
// Renamed from the misleading "test-server-api.cpp": none of this is a server
// API — it is pure value extraction over parser::Value.
//

#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "../reply.h"
#include "../server_reply.h"

using qb::redis::parser::Array;
using qb::redis::parser::BulkString;
using qb::redis::parser::Integer;
using qb::redis::parser::Map;
using qb::redis::parser::Null;
using qb::redis::parser::SimpleError;
using qb::redis::parser::SimpleString;
using qb::redis::parser::Value;

namespace {
std::unique_ptr<Value>
heap(Value v) {
    return std::make_unique<Value>(std::move(v));
}
} // namespace

// ============================================================================
// ServerReply<T> / ServerReply<void> — exercised through real construction
// (reworked from the old field-poke TypesCompile, which only set then re-read
// POD members and proved nothing).
// ============================================================================

TEST(ServerReply, IntegerOkCarriesValue) {
    qb::redis::ServerReply<int64_t> r{true, 42, {}};
    EXPECT_TRUE(r.is_ok());
    EXPECT_EQ(r.result(), 42);
    EXPECT_TRUE(r.error_message().empty());
}

TEST(ServerReply, StringOkCarriesValue) {
    qb::redis::ServerReply<std::string> r{true, "test", {}};
    EXPECT_TRUE(r.is_ok());
    EXPECT_EQ(r.result(), "test");
}

TEST(ServerReply, ErrorCaseExposesMessageNotValue) {
    qb::redis::ServerReply<std::string> r{false, {}, "ERR something wrong"};
    EXPECT_FALSE(r.is_ok());
    EXPECT_EQ(r.error_message(), "ERR something wrong");
}

TEST(ServerReply, ResultRvalueMovesOut) {
    qb::redis::ServerReply<std::string> r{true, "payload", {}};
    std::string                         moved = std::move(r).result();
    EXPECT_EQ(moved, "payload");
}

TEST(ServerReply, VoidOkAndError) {
    qb::redis::ServerReply<void> ok{true, {}};
    EXPECT_TRUE(ok.is_ok());

    qb::redis::ServerReply<void> err{false, "error"};
    EXPECT_FALSE(err.is_ok());
    EXPECT_EQ(err.error_message(), "error");
}

// ============================================================================
// ValueExtractor — typed optional accessors over a parser::Value
// ============================================================================

TEST(ValueExtractor, StringViewMatchesOnlyStrings) {
    Value                     val(SimpleString{"OK"});
    qb::redis::ValueExtractor ex(val);
    ASSERT_TRUE(ex.as_string_view().has_value());
    EXPECT_EQ(*ex.as_string_view(), "OK");
    EXPECT_FALSE(ex.as_integer().has_value());
    EXPECT_FALSE(ex.is_null());
}

TEST(ValueExtractor, IntegerMatchesOnlyIntegers) {
    Value                     val(Integer{42});
    qb::redis::ValueExtractor ex(val);
    ASSERT_TRUE(ex.as_integer().has_value());
    EXPECT_EQ(*ex.as_integer(), 42);
    EXPECT_FALSE(ex.as_string_view().has_value());
}

TEST(ValueExtractor, AsStringCopies) {
    Value                     val(BulkString{"copy me"});
    qb::redis::ValueExtractor ex(val);
    auto                      s = ex.as_string();
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(*s, "copy me");
}

TEST(ValueExtractor, AsDoubleWidensInteger) {
    Value                     val(Integer{7});
    qb::redis::ValueExtractor ex(val);
    ASSERT_TRUE(ex.as_double().has_value());
    EXPECT_DOUBLE_EQ(*ex.as_double(), 7.0);
}

// ADD: as_bool() — previously ZERO coverage. Matches only the RESP3 native
// Boolean; true and false survive, and a non-boolean (or absent) value is
// reported as nullopt (NOT coerced from an integer the way as_double widens).
TEST(ValueExtractor, AsBoolMatchesNativeBoolean) {
    Value                     vt(qb::redis::parser::Boolean{true});
    Value                     vf(qb::redis::parser::Boolean{false});
    qb::redis::ValueExtractor et(vt);
    qb::redis::ValueExtractor ef(vf);
    ASSERT_TRUE(et.as_bool().has_value());
    EXPECT_TRUE(*et.as_bool());
    ASSERT_TRUE(ef.as_bool().has_value());
    EXPECT_FALSE(*ef.as_bool());
}

TEST(ValueExtractor, AsBoolRejectsNonBoolean) {
    Value                     vi(Integer{1}); // not coerced to true
    qb::redis::ValueExtractor ei(vi);
    EXPECT_FALSE(ei.as_bool().has_value());

    std::unique_ptr<Value>    vnull; // absent
    qb::redis::ValueExtractor en(vnull);
    EXPECT_FALSE(en.as_bool().has_value());
}

// ADD: as_set() — previously ZERO coverage. Borrows a RESP3 native Set for
// iteration; a non-set value yields nullopt.
TEST(ValueExtractor, AsSetBorrowsNativeSet) {
    qb::redis::parser::Set set;
    set.elements.push_back(heap(Value(BulkString{"a"})));
    set.elements.push_back(heap(Value(BulkString{"b"})));
    Value                     val(std::move(set));
    qb::redis::ValueExtractor ex(val);

    auto s = ex.as_set();
    ASSERT_TRUE(s.has_value());
    ASSERT_EQ(s->get().elements.size(), 2u);
    EXPECT_EQ(s->get().elements[0]->as_string_view(), "a");
    EXPECT_EQ(s->get().elements[1]->as_string_view(), "b");
}

TEST(ValueExtractor, AsSetRejectsNonSet) {
    Value                     val(Array{}); // an array is not a set
    qb::redis::ValueExtractor ex(val);
    EXPECT_FALSE(ex.as_set().has_value());
}

TEST(ValueExtractor, UniquePtrConstructor) {
    auto                      val = std::make_unique<Value>(BulkString{"hello world"});
    qb::redis::ValueExtractor ex(val);
    ASSERT_TRUE(ex.as_string_view().has_value());
    EXPECT_EQ(*ex.as_string_view(), "hello world");
}

TEST(ValueExtractor, NullUniquePtrReportsNull) {
    std::unique_ptr<Value>    val; // null
    qb::redis::ValueExtractor ex(val);
    EXPECT_TRUE(ex.is_null());
    EXPECT_FALSE(ex.as_string_view().has_value());
    EXPECT_EQ(ex.raw(), nullptr);
}

TEST(ValueExtractor, ErrorReportsMessage) {
    Value                     val(SimpleError{"ERR test error"});
    qb::redis::ValueExtractor ex(val);
    EXPECT_TRUE(ex.is_error());
    EXPECT_EQ(ex.get_error_message(), "ERR test error");
}

TEST(ValueExtractor, NullValueIsNull) {
    Value                     val(Null{});
    qb::redis::ValueExtractor ex(val);
    EXPECT_TRUE(ex.is_null());
    EXPECT_FALSE(ex.as_string_view().has_value());
    EXPECT_FALSE(ex.as_integer().has_value());
}

TEST(ValueExtractor, ArrayBorrow) {
    Array arr;
    arr.elements.push_back(heap(Value(Integer{1})));
    arr.elements.push_back(heap(Value(Integer{2})));
    arr.elements.push_back(heap(Value(Integer{3})));
    Value                     val(std::move(arr));
    qb::redis::ValueExtractor ex(val);

    auto a = ex.as_array();
    ASSERT_TRUE(a.has_value());
    ASSERT_EQ(a->get().size(), 3u);
    // STRENGTHENED: read the actual borrowed elements, not just the size.
    EXPECT_EQ(a->get()[0]->as_integer().value, 1);
    EXPECT_EQ(a->get()[2]->as_integer().value, 3);
}

TEST(ValueExtractor, MapBorrowMultiEntry) {
    Map map;
    map.entries.push_back(std::make_pair(heap(Value(BulkString{"k1"})), heap(Value(BulkString{"v1"}))));
    map.entries.push_back(std::make_pair(heap(Value(BulkString{"k2"})), heap(Value(BulkString{"v2"}))));
    Value                     val(std::move(map));
    qb::redis::ValueExtractor ex(val);

    auto m = ex.as_map();
    ASSERT_TRUE(m.has_value());
    // ADD: multi-entry value retrieval, not just the single-entry size check.
    const auto &entries = m->get().entries;
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].first->as_string_view(), "k1");
    EXPECT_EQ(entries[0].second->as_string_view(), "v1");
    EXPECT_EQ(entries[1].first->as_string_view(), "k2");
    EXPECT_EQ(entries[1].second->as_string_view(), "v2");
}

// ============================================================================
// extract_string / extract_integer — value + failure paths
// ============================================================================

TEST(ExtractString, Success) {
    Value val(SimpleString{"test"});
    auto  r = qb::redis::extract_string(val);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), "test");
}

TEST(ExtractString, NullIsError) {
    Value val(Null{});
    auto  r = qb::redis::extract_string(val);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), "null value");
}

TEST(ExtractString, WrongTypeIsError) {
    Value val(Integer{42});
    auto  r = qb::redis::extract_string(val);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), "not a string");
}

TEST(ExtractInteger, Success) {
    Value val(Integer{123});
    auto  r = qb::redis::extract_integer(val);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), 123);
}

// ADD: extract_integer failure paths (null and wrong type) — previously only
// the happy path was covered.
TEST(ExtractInteger, NullIsError) {
    Value val(Null{});
    auto  r = qb::redis::extract_integer(val);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), "null value");
}

TEST(ExtractInteger, WrongTypeIsError) {
    Value val(BulkString{"42"});
    auto  r = qb::redis::extract_integer(val);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), "not an integer");
}

// ============================================================================
// extract_string_array — success, mixed (non-string element), empty, null,
// non-array
// ============================================================================

TEST(ExtractStringArray, Success) {
    Array arr;
    arr.elements.push_back(heap(Value(BulkString{"first"})));
    arr.elements.push_back(heap(Value(BulkString{"second"})));
    Value val(std::move(arr));

    auto r = qb::redis::extract_string_array(val);
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(r.value().size(), 2u);
    EXPECT_EQ(r.value()[0], "first");
    EXPECT_EQ(r.value()[1], "second");
}

// ADD: a non-string element must fail the whole extraction.
TEST(ExtractStringArray, MixedElementIsError) {
    Array arr;
    arr.elements.push_back(heap(Value(BulkString{"ok"})));
    arr.elements.push_back(heap(Value(Integer{7}))); // not a string
    Value val(std::move(arr));

    auto r = qb::redis::extract_string_array(val);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), "array contains non-string");
}

// ADD: an empty array succeeds with an empty vector.
TEST(ExtractStringArray, EmptyArrayIsEmptyVector) {
    Value val(Array{});
    auto  r = qb::redis::extract_string_array(val);
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(r.value().empty());
}

// ADD: null maps to an empty (successful) vector, per the documented contract.
TEST(ExtractStringArray, NullIsEmptyVector) {
    Value val(Null{});
    auto  r = qb::redis::extract_string_array(val);
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(r.value().empty());
}

TEST(ExtractStringArray, NonArrayIsError) {
    Value val(Integer{1});
    auto  r = qb::redis::extract_string_array(val);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), "not an array");
}

// ============================================================================
// extract_string_map — success, null, non-map, non-string key/value
// ============================================================================

TEST(ExtractStringMap, Success) {
    Map map;
    map.entries.push_back(std::make_pair(heap(Value(BulkString{"k1"})), heap(Value(BulkString{"v1"}))));
    map.entries.push_back(std::make_pair(heap(Value(BulkString{"k2"})), heap(Value(BulkString{"v2"}))));
    Value val(std::move(map));

    auto r = qb::redis::extract_string_map(val);
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(r.value().size(), 2u);
    EXPECT_EQ(r.value().at("k1"), "v1");
    EXPECT_EQ(r.value().at("k2"), "v2");
}

TEST(ExtractStringMap, NullIsEmptyMap) {
    Value val(Null{});
    auto  r = qb::redis::extract_string_map(val);
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(r.value().empty());
}

TEST(ExtractStringMap, NonMapIsError) {
    Value val(Integer{1});
    auto  r = qb::redis::extract_string_map(val);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), "not a map");
}

TEST(ExtractStringMap, NonStringValueIsError) {
    Map map;
    map.entries.push_back(std::make_pair(heap(Value(BulkString{"k"})), heap(Value(Integer{9}))));
    Value val(std::move(map));

    auto r = qb::redis::extract_string_map(val);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), "map value is not a string");
}

// ADD: a non-string KEY must fail the whole extraction (the "map key is not a
// string" branch was previously untested despite the file's failure-path banner).
TEST(ExtractStringMap, NonStringKeyIsError) {
    Map map;
    map.entries.push_back(std::make_pair(heap(Value(Integer{1})), heap(Value(BulkString{"v"}))));
    Value val(std::move(map));

    auto r = qb::redis::extract_string_map(val);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), "map key is not a string");
}

// ============================================================================
// extract_stream_id — parametrized edge table
// ============================================================================

struct StreamIdCase {
    const char *input;
    bool        ok;
    long long   timestamp;
    long long   sequence;
};

class ExtractStreamIdParam : public ::testing::TestWithParam<StreamIdCase> {};

TEST_P(ExtractStreamIdParam, Edges) {
    const auto &c = GetParam();
    Value       val(BulkString{c.input});
    auto        r = qb::redis::extract_stream_id(val);
    if (c.ok) {
        ASSERT_TRUE(r.has_value()) << "input=" << c.input;
        EXPECT_EQ(r.value().timestamp, c.timestamp);
        EXPECT_EQ(r.value().sequence, c.sequence);
    } else {
        EXPECT_FALSE(r.has_value()) << "input=" << c.input;
    }
}

INSTANTIATE_TEST_SUITE_P(
    ExtractStreamId, ExtractStreamIdParam,
    ::testing::Values(StreamIdCase{"1234567890-0", true, 1234567890, 0}, StreamIdCase{"1-5", true, 1, 5},
                      StreamIdCase{"0-0", true, 0, 0}, StreamIdCase{"invalid", false, 0, 0},
                      StreamIdCase{"123", false, 0, 0},      // no dash
                      StreamIdCase{"x-y", false, 0, 0},      // non-numeric components
                      StreamIdCase{"99999999999999999999999-0", false, 0, 0})); // overflow

// stream id from a non-string value is rejected.
TEST(ExtractStreamId, NonStringIsError) {
    Value val(Integer{1});
    auto  r = qb::redis::extract_stream_id(val);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), "stream id must be a string");
}

// ============================================================================
// extract_score_member — value + failure paths
// ============================================================================

TEST(ExtractScoreMember, DoubleScore) {
    Array arr;
    arr.elements.push_back(heap(Value(BulkString{"member"})));
    arr.elements.push_back(heap(Value(qb::redis::parser::Double{3.5})));
    Value val(std::move(arr));

    auto r = qb::redis::extract_score_member(val.as_array(), 0);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r.value().member, "member");
    EXPECT_DOUBLE_EQ(r.value().score, 3.5);
}

TEST(ExtractScoreMember, IntegerScoreWidens) {
    Array arr;
    arr.elements.push_back(heap(Value(BulkString{"m"})));
    arr.elements.push_back(heap(Value(Integer{4})));
    Value val(std::move(arr));

    auto r = qb::redis::extract_score_member(val.as_array(), 0);
    ASSERT_TRUE(r.has_value());
    EXPECT_DOUBLE_EQ(r.value().score, 4.0);
}

TEST(ExtractScoreMember, NotEnoughElementsIsError) {
    Array arr;
    arr.elements.push_back(heap(Value(BulkString{"only-member"})));
    Value val(std::move(arr));

    auto r = qb::redis::extract_score_member(val.as_array(), 0);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), "not enough elements for score-member pair");
}

TEST(ExtractScoreMember, NonNumericScoreIsError) {
    Array arr;
    arr.elements.push_back(heap(Value(BulkString{"m"})));
    arr.elements.push_back(heap(Value(BulkString{"not-a-number"})));
    Value val(std::move(arr));

    auto r = qb::redis::extract_score_member(val.as_array(), 0);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), "score must be a number");
}

// ADD: a non-string MEMBER must fail with "member must be a string" (this named
// branch was untested despite the file's failure-path banner).
TEST(ExtractScoreMember, NonStringMemberIsError) {
    Array arr;
    arr.elements.push_back(heap(Value(Integer{7}))); // member must be a string
    arr.elements.push_back(heap(Value(qb::redis::parser::Double{1.0})));
    Value val(std::move(arr));

    auto r = qb::redis::extract_score_member(val.as_array(), 0);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), "member must be a string");
}

// ADD: a null (empty unique_ptr) score element must fail with "score is null".
// Note this is distinct from a Null *Value* — the extractor's guard tests the
// owning unique_ptr, so the element pointer itself must be empty.
TEST(ExtractScoreMember, NullScoreIsError) {
    Array arr;
    arr.elements.push_back(heap(Value(BulkString{"m"})));
    arr.elements.push_back(std::unique_ptr<Value>{}); // empty pointer, not a Null Value
    Value val(std::move(arr));

    auto r = qb::redis::extract_score_member(val.as_array(), 0);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), "score is null");
}

// ============================================================================
// AsyncResult<T> / AsyncResult<void>
// ============================================================================

TEST(AsyncResult, SuccessHoldsValue) {
    qb::redis::AsyncResult<int> r(42);
    EXPECT_TRUE(r.is_ok());
    EXPECT_FALSE(r.has_error());
    EXPECT_EQ(r.value(), 42);
    EXPECT_TRUE(static_cast<bool>(r));
}

TEST(AsyncResult, ErrorHoldsMessage) {
    qb::redis::AsyncResult<int> r(std::string("failure"));
    EXPECT_FALSE(r.is_ok());
    EXPECT_TRUE(r.has_error());
    EXPECT_EQ(r.error(), "failure");
    EXPECT_FALSE(static_cast<bool>(r));
}

TEST(AsyncResult, ArrowOperator) {
    // NB: AsyncResult<std::string> is unusable (value ctor AsyncResult(T&&) and
    // error ctor AsyncResult(std::string&&) collide), so exercise operator-> on
    // a non-string value type.
    qb::redis::AsyncResult<std::vector<int>> r(std::vector<int>{1, 2, 3});
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r->size(), 3u);
}

TEST(AsyncResult, VoidSuccessAndError) {
    qb::redis::AsyncResult<void> ok;
    EXPECT_TRUE(ok.is_ok());
    EXPECT_FALSE(ok.has_error());        // void specialization has_error()
    EXPECT_TRUE(static_cast<bool>(ok));  // operator bool() success path

    qb::redis::AsyncResult<void> err(std::string("void error"));
    EXPECT_FALSE(err.is_ok());
    EXPECT_TRUE(err.has_error());        // has_error() error path
    EXPECT_FALSE(static_cast<bool>(err)); // operator bool() error path
    EXPECT_EQ(err.error(), "void error");
}

// ============================================================================
// ValueExtractor negative / non-matching accessor branches (the success arms
// are covered above; these pin the nullopt / wrong-type / null-pointer returns).
// ============================================================================

TEST(ValueExtractor, AsDoubleNativeDoubleAndRejectsNonNumeric) {
    Value                     vd(qb::redis::parser::Double{2.5});
    qb::redis::ValueExtractor ed(vd);
    ASSERT_TRUE(ed.as_double().has_value());
    EXPECT_DOUBLE_EQ(*ed.as_double(), 2.5); // native Double (not integer-widened)

    Value                     vs(BulkString{"x"});
    qb::redis::ValueExtractor es(vs);
    EXPECT_FALSE(es.as_double().has_value()); // non-numeric -> nullopt

    std::unique_ptr<Value>    n; // null
    qb::redis::ValueExtractor en(n);
    EXPECT_FALSE(en.as_double().has_value()); // null pointer -> nullopt
}

TEST(ValueExtractor, AsStringRejectsNonString) {
    Value                     v(Integer{1});
    qb::redis::ValueExtractor ex(v);
    EXPECT_FALSE(ex.as_string().has_value());
}

TEST(ValueExtractor, AsArrayAndAsMapRejectWrongType) {
    Value                     v(Integer{1});
    qb::redis::ValueExtractor ex(v);
    EXPECT_FALSE(ex.as_array().has_value());
    EXPECT_FALSE(ex.as_map().has_value());
}

TEST(ValueExtractor, GetErrorMessageOnNullIsNoValue) {
    std::unique_ptr<Value>    n; // null
    qb::redis::ValueExtractor ex(n);
    EXPECT_EQ(ex.get_error_message(), "no value");
}
