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
// Pure-logic unit tests for the Redis reply layer: converting a native
// parser::Value (RESP2/RESP3) into strongly typed C++ values. These tests build
// parser::Value trees by hand (no live server) and exercise
// qb::redis::reply::parse<T>(...) end to end.
//

#include <chrono>
#include <cmath>
#include <gtest/gtest.h>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "../reply.h"

using namespace qb::redis::parser;

namespace {

// ============================================================================
// Value-construction helpers
// ============================================================================

// Wrap a parser type in a heap Value owned by a unique_ptr (the element/entry
// representation used everywhere by Array/Map/Set/Push).
std::unique_ptr<Value>
mk_bulk(std::string s) {
    return std::make_unique<Value>(Value(BulkString{std::move(s)}));
}

std::unique_ptr<Value>
mk_simple(std::string s) {
    return std::make_unique<Value>(Value(SimpleString{std::move(s)}));
}

std::unique_ptr<Value>
mk_int(int64_t i) {
    return std::make_unique<Value>(Value(Integer{i}));
}

std::unique_ptr<Value>
mk_dbl(double d) {
    return std::make_unique<Value>(Value(Double{d}));
}

// Build a Value holding an Array from a list of element makers.
Value
make_array(std::vector<std::unique_ptr<Value>> elems) {
    Array arr;
    arr.elements = std::move(elems);
    return Value(std::move(arr));
}

// Build a Value holding a Set from a list of element makers.
Value
make_set(std::vector<std::unique_ptr<Value>> elems) {
    Set s;
    s.elements = std::move(elems);
    return Value(std::move(s));
}

// Build a Value holding a Push from a list of element makers.
Value
make_push(std::vector<std::unique_ptr<Value>> elems) {
    Push p;
    p.elements = std::move(elems);
    return Value(std::move(p));
}

// Build a Value holding a Map from (key, value) pairs.
Value
make_map(std::vector<std::pair<std::unique_ptr<Value>, std::unique_ptr<Value>>> entries) {
    Map m;
    m.entries = std::move(entries);
    return Value(std::move(m));
}

template <typename T>
T
do_parse(const Value &v) {
    return qb::redis::reply::parse<T>(v);
}

} // namespace

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
// 3. parse<qb::json> / parse<json_value> error (SimpleError) -> CommandError
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
// 5. parse<std::vector<score_member>> - RESP2 flat AND RESP3 nested + odd throw
// ============================================================================

TEST(ReplyScoreMemberVector, Resp2FlatArray) {
    // [member, score, member, score]
    std::vector<std::unique_ptr<Value>> elems;
    elems.push_back(mk_bulk("alice"));
    elems.push_back(mk_bulk("1.5"));
    elems.push_back(mk_bulk("bob"));
    elems.push_back(mk_bulk("2.5"));
    Value v = make_array(std::move(elems));

    auto out = do_parse<std::vector<qb::redis::score_member>>(v);
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[0].member, "alice");
    EXPECT_DOUBLE_EQ(out[0].score, 1.5);
    EXPECT_EQ(out[1].member, "bob");
    EXPECT_DOUBLE_EQ(out[1].score, 2.5);
}

TEST(ReplyScoreMemberVector, Resp3NestedArray) {
    // [[member, score], [member, score]]
    std::vector<std::unique_ptr<Value>> pair0;
    pair0.push_back(mk_bulk("alice"));
    pair0.push_back(mk_dbl(1.5));
    std::vector<std::unique_ptr<Value>> pair1;
    pair1.push_back(mk_bulk("bob"));
    pair1.push_back(mk_dbl(2.5));

    std::vector<std::unique_ptr<Value>> outer;
    outer.push_back(std::make_unique<Value>(make_array(std::move(pair0))));
    outer.push_back(std::make_unique<Value>(make_array(std::move(pair1))));
    Value v = make_array(std::move(outer));

    auto out = do_parse<std::vector<qb::redis::score_member>>(v);
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[0].member, "alice");
    EXPECT_DOUBLE_EQ(out[0].score, 1.5);
    EXPECT_EQ(out[1].member, "bob");
    EXPECT_DOUBLE_EQ(out[1].score, 2.5);
}

TEST(ReplyScoreMemberVector, OddLengthFlatThrows) {
    std::vector<std::unique_ptr<Value>> elems;
    elems.push_back(mk_bulk("alice"));
    elems.push_back(mk_bulk("1.5"));
    elems.push_back(mk_bulk("bob")); // odd
    Value v = make_array(std::move(elems));
    EXPECT_THROW(do_parse<std::vector<qb::redis::score_member>>(v), qb::redis::ProtoError);
}

TEST(ReplyScoreMemberVector, NonArrayThrows) {
    Value v(Integer{1});
    EXPECT_THROW(do_parse<std::vector<qb::redis::score_member>>(v), qb::redis::ProtoError);
}

// ============================================================================
// 6. parse<score> double/int/string + throw
// ============================================================================

TEST(ReplyScore, Double) {
    Value v(Double{3.14});
    EXPECT_DOUBLE_EQ(do_parse<qb::redis::score>(v).value, 3.14);
}

TEST(ReplyScore, Integer) {
    Value v(Integer{7});
    EXPECT_DOUBLE_EQ(do_parse<qb::redis::score>(v).value, 7.0);
}

TEST(ReplyScore, String) {
    Value v(BulkString{"2.5"});
    EXPECT_DOUBLE_EQ(do_parse<qb::redis::score>(v).value, 2.5);
}

TEST(ReplyScore, NonNumericTypeThrows) {
    Value v = make_array({});
    EXPECT_THROW(do_parse<qb::redis::score>(v), qb::redis::ProtoError);
}

// ============================================================================
// 7. parse<stream_id> valid/empty/malformed/non-string
// ============================================================================

TEST(ReplyStreamId, Valid) {
    Value v(BulkString{"1234567890-5"});
    auto  id = do_parse<qb::redis::stream_id>(v);
    EXPECT_EQ(id.timestamp, 1234567890);
    EXPECT_EQ(id.sequence, 5);
}

TEST(ReplyStreamId, EmptyReturnsDefault) {
    Value v(BulkString{""});
    auto  id = do_parse<qb::redis::stream_id>(v);
    EXPECT_EQ(id.timestamp, 0);
    EXPECT_EQ(id.sequence, 0);
}

TEST(ReplyStreamId, NoDashThrows) {
    Value v(BulkString{"nodash"});
    EXPECT_THROW(do_parse<qb::redis::stream_id>(v), qb::redis::ProtoError);
}

TEST(ReplyStreamId, NonNumericComponentsThrow) {
    Value v(BulkString{"x-y"});
    EXPECT_THROW(do_parse<qb::redis::stream_id>(v), qb::redis::ProtoError);
}

TEST(ReplyStreamId, NonStringThrows) {
    Value v(Integer{1});
    EXPECT_THROW(do_parse<qb::redis::stream_id>(v), qb::redis::ProtoError);
}

// ============================================================================
// 8. parse<stream_entry> + stream_entry_list + map_stream_entry_list + throw
// ============================================================================

TEST(ReplyStreamEntry, Valid) {
    // [ "1-0", [ "field1", "value1", "field2", "value2" ] ]
    std::vector<std::unique_ptr<Value>> fields;
    fields.push_back(mk_bulk("field1"));
    fields.push_back(mk_bulk("value1"));
    fields.push_back(mk_bulk("field2"));
    fields.push_back(mk_bulk("value2"));

    std::vector<std::unique_ptr<Value>> entry;
    entry.push_back(mk_bulk("1-0"));
    entry.push_back(std::make_unique<Value>(make_array(std::move(fields))));
    Value v = make_array(std::move(entry));

    auto se = do_parse<qb::redis::stream_entry>(v);
    EXPECT_EQ(se.id.timestamp, 1);
    EXPECT_EQ(se.id.sequence, 0);
    ASSERT_EQ(se.fields.size(), 2u);
    EXPECT_EQ(se.fields.at("field1"), "value1");
    EXPECT_EQ(se.fields.at("field2"), "value2");
}

TEST(ReplyStreamEntry, TooShortThrows) {
    std::vector<std::unique_ptr<Value>> entry;
    entry.push_back(mk_bulk("1-0"));
    Value v = make_array(std::move(entry));
    EXPECT_THROW(do_parse<qb::redis::stream_entry>(v), qb::redis::ProtoError);
}

static std::unique_ptr<Value>
make_stream_entry_value(std::string id, std::vector<std::pair<std::string, std::string>> fv) {
    std::vector<std::unique_ptr<Value>> fields;
    for (auto &p : fv) {
        fields.push_back(mk_bulk(p.first));
        fields.push_back(mk_bulk(p.second));
    }
    std::vector<std::unique_ptr<Value>> entry;
    entry.push_back(mk_bulk(std::move(id)));
    entry.push_back(std::make_unique<Value>(make_array(std::move(fields))));
    return std::make_unique<Value>(make_array(std::move(entry)));
}

TEST(ReplyStreamEntryList, Valid) {
    std::vector<std::unique_ptr<Value>> entries;
    entries.push_back(make_stream_entry_value("1-0", {{"a", "1"}}));
    entries.push_back(make_stream_entry_value("2-0", {{"b", "2"}}));
    Value v = make_array(std::move(entries));

    auto list = do_parse<qb::redis::stream_entry_list>(v);
    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(list[0].id.timestamp, 1);
    EXPECT_EQ(list[1].id.timestamp, 2);
    EXPECT_EQ(list[1].fields.at("b"), "2");
}

TEST(ReplyStreamEntryList, NonArrayThrows) {
    Value v(BulkString{"x"});
    EXPECT_THROW(do_parse<qb::redis::stream_entry_list>(v), qb::redis::ProtoError);
}

TEST(ReplyMapStreamEntryList, Valid) {
    // [ [ streamName, [ entry, entry ] ], ... ]
    std::vector<std::unique_ptr<Value>> entries_for_stream;
    entries_for_stream.push_back(make_stream_entry_value("1-0", {{"a", "1"}}));

    std::vector<std::unique_ptr<Value>> stream_pair;
    stream_pair.push_back(mk_bulk("mystream"));
    stream_pair.push_back(std::make_unique<Value>(make_array(std::move(entries_for_stream))));

    std::vector<std::unique_ptr<Value>> outer;
    outer.push_back(std::make_unique<Value>(make_array(std::move(stream_pair))));
    Value v = make_array(std::move(outer));

    auto m = do_parse<qb::redis::map_stream_entry_list>(v);
    ASSERT_EQ(m.size(), 1u);
    auto it = m.find("mystream");
    ASSERT_NE(it, m.end());
    ASSERT_EQ(it->second.size(), 1u);
    EXPECT_EQ(it->second[0].fields.at("a"), "1");
}

TEST(ReplyMapStreamEntryList, MalformedEntryThrows) {
    // Inner element is not an array of size >= 2.
    std::vector<std::unique_ptr<Value>> outer;
    outer.push_back(mk_bulk("not-an-array"));
    Value v = make_array(std::move(outer));
    EXPECT_THROW(do_parse<qb::redis::map_stream_entry_list>(v), qb::redis::ProtoError);
}

// ============================================================================
// 9. parse<cluster_node>
// ============================================================================

TEST(ReplyClusterNode, RealisticLine) {
    // id ip:port@cport flags master ping pong epoch link-state slots...
    const std::string line = "07c37dfeb235213a872192d90877d0cd55635b91 "
                             "127.0.0.1:30004@31004 slave,fail 07c37dfeb235213a872192d90877d0cd55635b92 "
                             "0 1426238317239 4 connected 0-5460";
    Value             v(BulkString{line});

    auto node = do_parse<qb::redis::cluster_node>(v);
    EXPECT_EQ(node.id, "07c37dfeb235213a872192d90877d0cd55635b91");
    EXPECT_EQ(node.ip, "127.0.0.1");
    EXPECT_EQ(node.port, 30004);
    ASSERT_EQ(node.flags.size(), 2u);
    EXPECT_EQ(node.flags[0], "slave");
    EXPECT_EQ(node.flags[1], "fail");
    EXPECT_EQ(node.master, "07c37dfeb235213a872192d90877d0cd55635b92");
    EXPECT_EQ(node.ping_sent, 0);
    EXPECT_EQ(node.pong_received, 1426238317239LL);
    EXPECT_EQ(node.epoch, 4);
    EXPECT_EQ(node.link_state, "connected");
    ASSERT_EQ(node.slots.size(), 1u);
    EXPECT_EQ(node.slots[0], "0-5460");
}

TEST(ReplyClusterNode, NonStringThrows) {
    Value v(Integer{1});
    EXPECT_THROW(do_parse<qb::redis::cluster_node>(v), qb::redis::ProtoError);
}

TEST(ReplyClusterNode, EmptyThrows) {
    Value v(BulkString{""});
    EXPECT_THROW(do_parse<qb::redis::cluster_node>(v), qb::redis::ProtoError);
}

TEST(ReplyClusterNode, NoColonInAddressThrows) {
    // id + addr-without-colon, then it fails when trying to find ':'
    Value v(BulkString{"theid noColonHere"});
    EXPECT_THROW(do_parse<qb::redis::cluster_node>(v), qb::redis::ProtoError);
}

TEST(ReplyClusterNode, BadPortThrows) {
    Value v(BulkString{"theid 127.0.0.1:notaport@1 flags"});
    EXPECT_THROW(do_parse<qb::redis::cluster_node>(v), qb::redis::ProtoError);
}

// ============================================================================
// 10. parse<memory_info>
// ============================================================================

TEST(ReplyMemoryInfo, FlatKeyValue) {
    std::vector<std::unique_ptr<Value>> elems;
    elems.push_back(mk_bulk("used_memory"));
    elems.push_back(mk_bulk("1024"));
    elems.push_back(mk_bulk("connected_clients"));
    elems.push_back(mk_bulk("3"));
    elems.push_back(mk_bulk("total_commands_processed"));
    elems.push_back(mk_bulk("42"));
    Value v = make_array(std::move(elems));

    auto info = do_parse<qb::redis::memory_info>(v);
    EXPECT_EQ(info.used_memory, 1024u);
    EXPECT_EQ(info.number_of_connected_clients, 3u);
    EXPECT_EQ(info.number_of_commands_processed, 42u);
    EXPECT_EQ(info.total_commands_processed, 42u);
}

TEST(ReplyMemoryInfo, NonNumericValueCaughtAsZero) {
    std::vector<std::unique_ptr<Value>> elems;
    elems.push_back(mk_bulk("used_memory"));
    elems.push_back(mk_bulk("notanumber"));
    Value v = make_array(std::move(elems));

    auto info = do_parse<qb::redis::memory_info>(v);
    EXPECT_EQ(info.used_memory, 0u); // stoull throws -> caught -> 0
}

TEST(ReplyMemoryInfo, NonArrayThrows) {
    Value v(Integer{1});
    EXPECT_THROW(do_parse<qb::redis::memory_info>(v), qb::redis::ProtoError);
}

// ============================================================================
// 11. parse<std::vector<char>>
// ============================================================================

TEST(ReplyVectorChar, FromString) {
    Value v(BulkString{"abc"});
    auto  out = do_parse<std::vector<char>>(v);
    ASSERT_EQ(out.size(), 3u);
    EXPECT_EQ(out[0], 'a');
    EXPECT_EQ(out[2], 'c');
}

TEST(ReplyVectorChar, EmptyString) {
    Value v(BulkString{""});
    auto  out = do_parse<std::vector<char>>(v);
    EXPECT_TRUE(out.empty());
}

TEST(ReplyVectorChar, NonStringThrows) {
    Value v(Integer{1});
    EXPECT_THROW(do_parse<std::vector<char>>(v), qb::redis::ProtoError);
}

// ============================================================================
// 12. parse<std::chrono::milliseconds> and <seconds>
// ============================================================================

TEST(ReplyChrono, Milliseconds) {
    Value v(Integer{1500});
    auto  ms = do_parse<std::chrono::milliseconds>(v);
    EXPECT_EQ(ms.count(), 1500);
}

TEST(ReplyChrono, Seconds) {
    Value v(Integer{30});
    auto  s = do_parse<std::chrono::seconds>(v);
    EXPECT_EQ(s.count(), 30);
}

TEST(ReplyChrono, MillisecondsNonIntegerThrows) {
    Value v(BulkString{"100"});
    EXPECT_THROW(do_parse<std::chrono::milliseconds>(v), qb::redis::ProtoError);
}

TEST(ReplyChrono, SecondsNonIntegerThrows) {
    Value v(BulkString{"100"});
    EXPECT_THROW(do_parse<std::chrono::seconds>(v), qb::redis::ProtoError);
}

// ============================================================================
// 13. parse<geo_pos>
// ============================================================================

TEST(ReplyGeoPos, Pair) {
    // [ longitude, latitude ] as strings (typical GEOPOS reply)
    std::vector<std::unique_ptr<Value>> elems;
    elems.push_back(mk_bulk("13.361389"));
    elems.push_back(mk_bulk("38.115556"));
    Value v = make_array(std::move(elems));

    auto pos = do_parse<qb::redis::geo_pos>(v);
    EXPECT_DOUBLE_EQ(pos.longitude, 13.361389);
    EXPECT_DOUBLE_EQ(pos.latitude, 38.115556);
}

TEST(ReplyGeoPos, NonArrayThrows) {
    Value v(BulkString{"x"});
    EXPECT_THROW(do_parse<qb::redis::geo_pos>(v), qb::redis::ProtoError);
}

TEST(ReplyGeoPos, TooFewElementsThrows) {
    std::vector<std::unique_ptr<Value>> elems;
    elems.push_back(mk_bulk("13.0"));
    Value v = make_array(std::move(elems));
    EXPECT_THROW(do_parse<qb::redis::geo_pos>(v), qb::redis::ProtoError);
}

// ============================================================================
// 14. parse<search_result>
// ============================================================================

TEST(ReplySearchResult, KeyAndFields) {
    // [ key, field1, value1, field2, value2 ]
    std::vector<std::unique_ptr<Value>> elems;
    elems.push_back(mk_bulk("doc:1"));
    elems.push_back(mk_bulk("title"));
    elems.push_back(mk_bulk("Hello"));
    elems.push_back(mk_bulk("body"));
    elems.push_back(mk_bulk("World"));
    Value v = make_array(std::move(elems));

    auto sr = do_parse<qb::redis::search_result>(v);
    EXPECT_EQ(sr.key, "doc:1");
    ASSERT_EQ(sr.fields.size(), 2u);
    ASSERT_EQ(sr.values.size(), 2u);
    EXPECT_EQ(sr.fields[0], "title");
    EXPECT_EQ(sr.values[0], "Hello");
    EXPECT_EQ(sr.fields[1], "body");
    EXPECT_EQ(sr.values[1], "World");
}

TEST(ReplySearchResult, Empty) {
    Value v  = make_array({});
    auto  sr = do_parse<qb::redis::search_result>(v);
    EXPECT_TRUE(sr.key.empty());
    EXPECT_TRUE(sr.fields.empty());
    EXPECT_TRUE(sr.values.empty());
}

TEST(ReplySearchResult, NonArrayThrows) {
    Value v(Integer{1});
    EXPECT_THROW(do_parse<qb::redis::search_result>(v), qb::redis::ProtoError);
}

// ============================================================================
// 15. parse<pipeline_result>
// ============================================================================

TEST(ReplyPipelineResult, ErrorCount) {
    std::vector<std::unique_ptr<Value>> elems;
    elems.push_back(mk_simple("OK"));
    elems.push_back(std::make_unique<Value>(Value(SimpleError{"ERR", "boom"})));
    elems.push_back(mk_int(1));
    elems.push_back(std::make_unique<Value>(Value(SimpleError{"WRONGTYPE", "bad"})));
    Value v = make_array(std::move(elems));

    auto pr = do_parse<qb::redis::pipeline_result>(v);
    EXPECT_EQ(pr.size, 4u);
    EXPECT_EQ(pr.error_count, 2u);
    EXPECT_FALSE(pr.all_succeeded);
}

TEST(ReplyPipelineResult, AllSucceeded) {
    std::vector<std::unique_ptr<Value>> elems;
    elems.push_back(mk_simple("OK"));
    elems.push_back(mk_int(1));
    Value v = make_array(std::move(elems));

    auto pr = do_parse<qb::redis::pipeline_result>(v);
    EXPECT_EQ(pr.size, 2u);
    EXPECT_EQ(pr.error_count, 0u);
    EXPECT_TRUE(pr.all_succeeded);
}

TEST(ReplyPipelineResult, NonArrayThrows) {
    Value v(Integer{1});
    EXPECT_THROW(do_parse<qb::redis::pipeline_result>(v), qb::redis::ProtoError);
}

// ============================================================================
// 16. parse<status> nil/string/error/throw
// ============================================================================

TEST(ReplyStatus, Nil) {
    Value v(Null{});
    auto  s = do_parse<qb::redis::status>(v);
    EXPECT_TRUE(s.str().empty());
}

TEST(ReplyStatus, StringOk) {
    Value v(SimpleString{"OK"});
    auto  s = do_parse<qb::redis::status>(v);
    EXPECT_TRUE(s.ok());
    EXPECT_EQ(s.str(), "OK");
}

TEST(ReplyStatus, ErrorMessage) {
    Value v(SimpleError{"ERR", "bad"});
    auto  s = do_parse<qb::redis::status>(v);
    EXPECT_EQ(s.str(), "ERR bad");
    EXPECT_FALSE(s.ok());
}

TEST(ReplyStatus, IntegerThrows) {
    Value v(Integer{1});
    EXPECT_THROW(do_parse<qb::redis::status>(v), qb::redis::ProtoError);
}

// ============================================================================
// 17. parse<bool> nil/native/int/throw
// ============================================================================

TEST(ReplyBool, NilIsFalse) {
    Value v(Null{});
    EXPECT_FALSE(do_parse<bool>(v));
}

TEST(ReplyBool, NativeBoolean) {
    Value vt(Boolean{true});
    Value vf(Boolean{false});
    EXPECT_TRUE(do_parse<bool>(vt));
    EXPECT_FALSE(do_parse<bool>(vf));
}

TEST(ReplyBool, IntegerNonZeroIsTrue) {
    Value v1(Integer{1});
    Value v0(Integer{0});
    EXPECT_TRUE(do_parse<bool>(v1));
    EXPECT_FALSE(do_parse<bool>(v0));
}

TEST(ReplyBool, StringThrows) {
    Value v(BulkString{"x"});
    EXPECT_THROW(do_parse<bool>(v), qb::redis::ProtoError);
}

// ============================================================================
// 18. parse<double> inf/+inf/-inf/int/reject
// ============================================================================

TEST(ReplyDouble, NativeDouble) {
    Value v(Double{1.25});
    EXPECT_DOUBLE_EQ(do_parse<double>(v), 1.25);
}

TEST(ReplyDouble, Integer) {
    Value v(Integer{8});
    EXPECT_DOUBLE_EQ(do_parse<double>(v), 8.0);
}

TEST(ReplyDouble, StringNumber) {
    Value v(BulkString{"3.5"});
    EXPECT_DOUBLE_EQ(do_parse<double>(v), 3.5);
}

TEST(ReplyDouble, Inf) {
    Value  v(BulkString{"inf"});
    double d = do_parse<double>(v);
    EXPECT_TRUE(std::isinf(d));
    EXPECT_GT(d, 0);
}

TEST(ReplyDouble, PlusInf) {
    Value  v(BulkString{"+inf"});
    double d = do_parse<double>(v);
    EXPECT_TRUE(std::isinf(d));
    EXPECT_GT(d, 0);
}

TEST(ReplyDouble, MinusInf) {
    Value  v(BulkString{"-inf"});
    double d = do_parse<double>(v);
    EXPECT_TRUE(std::isinf(d));
    EXPECT_LT(d, 0);
}

TEST(ReplyDouble, TrailingJunkRejected) {
    Value v(BulkString{"1.5junk"});
    EXPECT_THROW(do_parse<double>(v), qb::redis::ProtoError);
}

TEST(ReplyDouble, NonNumericTypeThrows) {
    Value v = make_array({});
    EXPECT_THROW(do_parse<double>(v), qb::redis::ProtoError);
}

// ============================================================================
// 19. parse<std::string> integer->"42" / nested-array->first / bulk
// ============================================================================

TEST(ReplyString, IntegerToString) {
    Value v(Integer{42});
    EXPECT_EQ(do_parse<std::string>(v), "42");
}

TEST(ReplyString, NestedArrayFirstString) {
    // [ "name", "distance" ] => extracts first string sub-element.
    std::vector<std::unique_ptr<Value>> elems;
    elems.push_back(mk_bulk("Palermo"));
    elems.push_back(mk_bulk("190.4424"));
    Value v = make_array(std::move(elems));
    EXPECT_EQ(do_parse<std::string>(v), "Palermo");
}

TEST(ReplyString, BulkString) {
    Value v(BulkString{"hello"});
    EXPECT_EQ(do_parse<std::string>(v), "hello");
}

TEST(ReplyString, SimpleString) {
    Value v(SimpleString{"PONG"});
    EXPECT_EQ(do_parse<std::string>(v), "PONG");
}

// ============================================================================
// 20. associative map<string,string> from RESP Map AND flat array + throw
// ============================================================================

TEST(ReplyMapContainer, FromRespMap) {
    std::vector<std::pair<std::unique_ptr<Value>, std::unique_ptr<Value>>> entries;
    entries.push_back(std::make_pair(mk_bulk("k1"), mk_bulk("v1")));
    entries.push_back(std::make_pair(mk_bulk("k2"), mk_bulk("v2")));
    Value v = make_map(std::move(entries));

    auto m = do_parse<std::map<std::string, std::string>>(v);
    ASSERT_EQ(m.size(), 2u);
    EXPECT_EQ(m["k1"], "v1");
    EXPECT_EQ(m["k2"], "v2");
}

TEST(ReplyMapContainer, FromFlatArray) {
    std::vector<std::unique_ptr<Value>> elems;
    elems.push_back(mk_bulk("k1"));
    elems.push_back(mk_bulk("v1"));
    elems.push_back(mk_bulk("k2"));
    elems.push_back(mk_bulk("v2"));
    Value v = make_array(std::move(elems));

    auto m = do_parse<std::map<std::string, std::string>>(v);
    ASSERT_EQ(m.size(), 2u);
    EXPECT_EQ(m["k1"], "v1");
    EXPECT_EQ(m["k2"], "v2");
}

TEST(ReplyMapContainer, NonArrayNonMapThrows) {
    Value v(Integer{1});
    EXPECT_THROW((do_parse<std::map<std::string, std::string>>(v)), qb::redis::ProtoError);
}

// ============================================================================
// 21. associative set<string> from RESP Set AND array
// ============================================================================

TEST(ReplySetContainer, FromRespSet) {
    std::vector<std::unique_ptr<Value>> elems;
    elems.push_back(mk_bulk("a"));
    elems.push_back(mk_bulk("b"));
    elems.push_back(mk_bulk("c"));
    Value v = make_set(std::move(elems));

    auto s = do_parse<std::set<std::string>>(v);
    ASSERT_EQ(s.size(), 3u);
    EXPECT_TRUE(s.count("a"));
    EXPECT_TRUE(s.count("c"));
}

TEST(ReplySetContainer, FromArray) {
    std::vector<std::unique_ptr<Value>> elems;
    elems.push_back(mk_bulk("x"));
    elems.push_back(mk_bulk("y"));
    Value v = make_array(std::move(elems));

    auto s = do_parse<std::set<std::string>>(v);
    ASSERT_EQ(s.size(), 2u);
    EXPECT_TRUE(s.count("x"));
    EXPECT_TRUE(s.count("y"));
}

TEST(ReplySetContainer, NonArrayNonSetThrows) {
    Value v(Integer{1});
    EXPECT_THROW((do_parse<std::set<std::string>>(v)), qb::redis::ProtoError);
}

// ============================================================================
// 22. sequence vector<string> from array AND RESP3 set
// ============================================================================

TEST(ReplySequence, FromArray) {
    std::vector<std::unique_ptr<Value>> elems;
    elems.push_back(mk_bulk("a"));
    elems.push_back(mk_bulk("b"));
    Value v = make_array(std::move(elems));

    auto out = do_parse<std::vector<std::string>>(v);
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[0], "a");
    EXPECT_EQ(out[1], "b");
}

TEST(ReplySequence, FromRespSet) {
    std::vector<std::unique_ptr<Value>> elems;
    elems.push_back(mk_bulk("one"));
    elems.push_back(mk_bulk("two"));
    Value v = make_set(std::move(elems));

    auto out = do_parse<std::vector<std::string>>(v);
    ASSERT_EQ(out.size(), 2u);
}

TEST(ReplySequence, NonArrayNonSetThrows) {
    Value v(Integer{1});
    EXPECT_THROW(do_parse<std::vector<std::string>>(v), qb::redis::ProtoError);
}

// ============================================================================
// 23. pair + tuple + short-array/non-array throws
// ============================================================================

TEST(ReplyPair, Valid) {
    std::vector<std::unique_ptr<Value>> elems;
    elems.push_back(mk_bulk("name"));
    elems.push_back(mk_int(99));
    Value v = make_array(std::move(elems));

    auto p = (do_parse<std::pair<std::string, long long>>(v));
    EXPECT_EQ(p.first, "name");
    EXPECT_EQ(p.second, 99);
}

TEST(ReplyPair, ShortArrayThrows) {
    std::vector<std::unique_ptr<Value>> elems;
    elems.push_back(mk_bulk("only-one"));
    Value v = make_array(std::move(elems));
    EXPECT_THROW((do_parse<std::pair<std::string, long long>>(v)), qb::redis::ProtoError);
}

TEST(ReplyPair, NonArrayThrows) {
    Value v(Integer{1});
    EXPECT_THROW((do_parse<std::pair<std::string, long long>>(v)), qb::redis::ProtoError);
}

TEST(ReplyTuple, Valid) {
    std::vector<std::unique_ptr<Value>> elems;
    elems.push_back(mk_bulk("alpha"));
    elems.push_back(mk_int(2));
    elems.push_back(mk_dbl(3.5));
    Value v = make_array(std::move(elems));

    auto t = (do_parse<std::tuple<std::string, long long, double>>(v));
    EXPECT_EQ(std::get<0>(t), "alpha");
    EXPECT_EQ(std::get<1>(t), 2);
    EXPECT_DOUBLE_EQ(std::get<2>(t), 3.5);
}

TEST(ReplyTuple, ShortArrayThrows) {
    std::vector<std::unique_ptr<Value>> elems;
    elems.push_back(mk_bulk("alpha"));
    elems.push_back(mk_int(2));
    Value v = make_array(std::move(elems));
    EXPECT_THROW((do_parse<std::tuple<std::string, long long, double>>(v)), qb::redis::ProtoError);
}

TEST(ReplyTuple, NonArrayThrows) {
    Value v(Integer{1});
    EXPECT_THROW((do_parse<std::tuple<std::string, long long, double>>(v)), qb::redis::ProtoError);
}

// ============================================================================
// 24. optional<string> nil->nullopt / value
// ============================================================================

TEST(ReplyOptional, NilIsNullopt) {
    Value v(Null{});
    auto  o = do_parse<std::optional<std::string>>(v);
    EXPECT_FALSE(o.has_value());
}

TEST(ReplyOptional, ValuePresent) {
    Value v(BulkString{"present"});
    auto  o = do_parse<std::optional<std::string>>(v);
    ASSERT_TRUE(o.has_value());
    EXPECT_EQ(*o, "present");
}

// ============================================================================
// 25. message / pmessage / subscription (Array + Push variant) + too-short throw
// ============================================================================

TEST(ReplyMessage, FromArray) {
    // [ "message", channel, payload ]
    std::vector<std::unique_ptr<Value>> elems;
    elems.push_back(mk_bulk("message"));
    elems.push_back(mk_bulk("news"));
    elems.push_back(mk_bulk("hello world"));
    Value v = make_array(std::move(elems));

    auto m = do_parse<qb::redis::message>(v);
    EXPECT_EQ(m.channel, "news");
    EXPECT_EQ(m.payload, "hello world");
    EXPECT_TRUE(m.pattern.empty());
}

TEST(ReplyMessage, FromPush) {
    std::vector<std::unique_ptr<Value>> elems;
    elems.push_back(mk_bulk("message"));
    elems.push_back(mk_bulk("news"));
    elems.push_back(mk_bulk("payload3"));
    Value v = make_push(std::move(elems));

    auto m = do_parse<qb::redis::message>(v);
    EXPECT_EQ(m.channel, "news");
    EXPECT_EQ(m.payload, "payload3");
}

TEST(ReplyMessage, TooShortThrows) {
    std::vector<std::unique_ptr<Value>> elems;
    elems.push_back(mk_bulk("message"));
    elems.push_back(mk_bulk("news"));
    Value v = make_array(std::move(elems));
    EXPECT_THROW(do_parse<qb::redis::message>(v), qb::redis::ProtoError);
}

TEST(ReplyPMessage, FromArray) {
    // [ "pmessage", pattern, channel, payload ]
    std::vector<std::unique_ptr<Value>> elems;
    elems.push_back(mk_bulk("pmessage"));
    elems.push_back(mk_bulk("news.*"));
    elems.push_back(mk_bulk("news.tech"));
    elems.push_back(mk_bulk("breaking"));
    Value v = make_array(std::move(elems));

    auto m = do_parse<qb::redis::pmessage>(v);
    EXPECT_EQ(m.pattern, "news.*");
    EXPECT_EQ(m.channel, "news.tech");
    EXPECT_EQ(m.payload, "breaking");
}

TEST(ReplyPMessage, TooShortThrows) {
    std::vector<std::unique_ptr<Value>> elems;
    elems.push_back(mk_bulk("pmessage"));
    elems.push_back(mk_bulk("news.*"));
    elems.push_back(mk_bulk("news.tech"));
    Value v = make_array(std::move(elems));
    EXPECT_THROW(do_parse<qb::redis::pmessage>(v), qb::redis::ProtoError);
}

TEST(ReplySubscription, FromArray) {
    // [ "subscribe", channel, count ]
    std::vector<std::unique_ptr<Value>> elems;
    elems.push_back(mk_bulk("subscribe"));
    elems.push_back(mk_bulk("news"));
    elems.push_back(mk_int(1));
    Value v = make_array(std::move(elems));

    auto s = do_parse<qb::redis::subscription>(v);
    ASSERT_TRUE(s.channel.has_value());
    EXPECT_EQ(*s.channel, "news");
    EXPECT_EQ(s.num, 1);
}

TEST(ReplySubscription, FromPushWithNilChannel) {
    // Unsubscribe-all replies carry a nil channel.
    std::vector<std::unique_ptr<Value>> elems;
    elems.push_back(mk_bulk("unsubscribe"));
    elems.push_back(std::make_unique<Value>(Value(Null{})));
    elems.push_back(mk_int(0));
    Value v = make_push(std::move(elems));

    auto s = do_parse<qb::redis::subscription>(v);
    EXPECT_FALSE(s.channel.has_value());
    EXPECT_EQ(s.num, 0);
}

TEST(ReplySubscription, TooShortThrows) {
    std::vector<std::unique_ptr<Value>> elems;
    elems.push_back(mk_bulk("subscribe"));
    elems.push_back(mk_bulk("news"));
    Value v = make_array(std::move(elems));
    EXPECT_THROW(do_parse<qb::redis::subscription>(v), qb::redis::ProtoError);
}

// ============================================================================
// 26. SERIALIZATION: to_redis_string per overload (exact RESP wire bytes)
//
// Each helper writes one bulk string as `$<len>\r\n<payload>\r\n`. We read the
// emitted bytes back with pipe<char>::str() and assert the exact wire form,
// confirmed against the inline serializers in reply.h.
// ============================================================================

namespace {
using namespace qb::redis;

// Fresh pipe, serialize one argument, read raw bytes back.
template <typename T>
std::string
ser(const T &v) {
    qb::allocator::pipe<char> p;
    qb::redis::to_redis_string(p, v);
    return p.str();
}
} // namespace

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

// ============================================================================
// 29. type_to_string - one arm per RESP kind (reply.cpp 16-arm cascade)
// ============================================================================

TEST(ReplyTypeToString, SimpleString) {
    Value v(SimpleString{"OK"});
    EXPECT_EQ(qb::redis::reply::type_to_string(v), "SIMPLE_STRING");
}

TEST(ReplyTypeToString, SimpleError) {
    Value v(SimpleError{"ERR", "x"});
    EXPECT_EQ(qb::redis::reply::type_to_string(v), "SIMPLE_ERROR");
}

TEST(ReplyTypeToString, BulkString) {
    Value v(BulkString{"data"});
    EXPECT_EQ(qb::redis::reply::type_to_string(v), "BULK_STRING");
}

TEST(ReplyTypeToString, BulkError) {
    Value v(BulkError{"WRONGTYPE", "bad"});
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
    Value v(Boolean{true});
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
    Attribute attr;
    Value     v(std::move(attr));
    EXPECT_EQ(qb::redis::reply::type_to_string(v), "ATTRIBUTE");
}

TEST(ReplyTypeToString, VerbatimString) {
    // VerbatimString is_string() but is neither simple nor bulk string.
    // encoding is char[3]; build the struct field-by-field to avoid char[4] literal mismatch.
    VerbatimString vs;
    vs.encoding[0] = 't';
    vs.encoding[1] = 'x';
    vs.encoding[2] = 't';
    vs.value       = "hello";
    Value v(std::move(vs));
    EXPECT_EQ(qb::redis::reply::type_to_string(v), "VERBATIM_STRING");
}

TEST(ReplyTypeToString, BigNumber) {
    Value v(BigNumber{"123456789012345678901234567890", false});
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
// 31. TReply<Func,T> - the 4 dispatch paths via std::function callback
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

    auto raw = std::make_unique<Value>(Value(SimpleError{"ERR", "boom"}));
    tr(std::move(raw));
    EXPECT_FALSE(got.ok());
    // get_error_message() returns the joined "ERR boom".
    EXPECT_EQ(got.error(), "ERR boom");
    EXPECT_NE(got.raw(), nullptr); // raw is moved through on error
}

TEST(ReplyTReply, ParseThrowCaughtAsError) {
    // Parsing an Integer reply as long long succeeds; instead feed an Array to a
    // long long parser to force ReplyParseError -> caught -> error string set.
    qb::redis::Reply<long long> got;
    auto handler = std::function<void(qb::redis::Reply<long long>)>([&](qb::redis::Reply<long long> r) { got = std::move(r); });
    qb::redis::TReply<decltype(handler), long long> tr(std::move(handler));

    auto raw = std::make_unique<Value>(make_array({}));
    tr(std::move(raw));
    EXPECT_FALSE(got.ok());
    EXPECT_FALSE(got.error().empty()); // e.what() copied
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

int
main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
