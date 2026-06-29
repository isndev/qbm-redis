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
// Unit tier (pure logic, no daemon/loop): deserialization side of the Redis
// reply layer — qb::redis::reply::parse<T>(parser::Value) for every typed
// target except qb::json / json_value (those live in reply-json-decode.cpp).
//
// Value trees are built by hand via shared/reply_value_builders.h; no socket,
// no event loop, no RESOURCE_LOCK.
//

#include <algorithm>
#include <chrono>
#include <cmath>
#include <gtest/gtest.h>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "../../shared/reply_value_builders.h"

using namespace qb::redis::test;

// ============================================================================
// 1. parse<std::vector<score_member>> - RESP2 flat AND RESP3 nested + odd throw
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

// ADD: RESP3 native Double scores survive the flat path with full precision.
TEST(ReplyScoreMemberVector, Resp2FlatNativeDoubleScore) {
    std::vector<std::unique_ptr<Value>> elems;
    elems.push_back(mk_bulk("alice"));
    elems.push_back(mk_dbl(0.1));
    Value v = make_array(std::move(elems));

    auto out = do_parse<std::vector<qb::redis::score_member>>(v);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].member, "alice");
    EXPECT_DOUBLE_EQ(out[0].score, 0.1);
}

// ============================================================================
// 2. parse<score> double/int/string + throw
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

// ADD: inf / -inf scores (sorted-set range edges) survive the string path.
TEST(ReplyScore, StringPlusInf) {
    Value  v(BulkString{"+inf"});
    double s = do_parse<qb::redis::score>(v).value;
    EXPECT_TRUE(std::isinf(s));
    EXPECT_GT(s, 0);
}

TEST(ReplyScore, StringMinusInf) {
    Value  v(BulkString{"-inf"});
    double s = do_parse<qb::redis::score>(v).value;
    EXPECT_TRUE(std::isinf(s));
    EXPECT_LT(s, 0);
}

// ADD: empty string score must be rejected, not silently 0.
TEST(ReplyScore, EmptyStringThrows) {
    Value v(BulkString{""});
    EXPECT_THROW(do_parse<qb::redis::score>(v), qb::redis::ProtoError);
}

// ADD: RESP3 score parser must not choke on a native Double of NaN.
TEST(ReplyScore, NativeNan) {
    Value v(Double{std::numeric_limits<double>::quiet_NaN()});
    EXPECT_TRUE(std::isnan(do_parse<qb::redis::score>(v).value));
}

// ============================================================================
// 3. parse<stream_id> valid/empty/malformed/non-string + overflow
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

// ADD: a timestamp beyond int64 range must throw, not wrap (stoll out_of_range).
TEST(ReplyStreamId, TimestampOverflowThrows) {
    Value v(BulkString{"99999999999999999999999-0"});
    EXPECT_THROW(do_parse<qb::redis::stream_id>(v), qb::redis::ProtoError);
}

// ADD: a sequence beyond int64 range must throw, not wrap.
TEST(ReplyStreamId, SequenceOverflowThrows) {
    Value v(BulkString{"1-99999999999999999999999"});
    EXPECT_THROW(do_parse<qb::redis::stream_id>(v), qb::redis::ProtoError);
}

// ============================================================================
// 4. parse<stream_entry> + stream_entry_list + map_stream_entry_list + throw
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

// ADD: RESP3 multi-stream map — two named streams, each with its own entries.
TEST(ReplyMapStreamEntryList, MultiStream) {
    std::vector<std::unique_ptr<Value>> s1_entries;
    s1_entries.push_back(make_stream_entry_value("1-0", {{"a", "1"}}));
    std::vector<std::unique_ptr<Value>> s1_pair;
    s1_pair.push_back(mk_bulk("stream-one"));
    s1_pair.push_back(std::make_unique<Value>(make_array(std::move(s1_entries))));

    std::vector<std::unique_ptr<Value>> s2_entries;
    s2_entries.push_back(make_stream_entry_value("2-0", {{"b", "2"}}));
    s2_entries.push_back(make_stream_entry_value("3-0", {{"c", "3"}}));
    std::vector<std::unique_ptr<Value>> s2_pair;
    s2_pair.push_back(mk_bulk("stream-two"));
    s2_pair.push_back(std::make_unique<Value>(make_array(std::move(s2_entries))));

    std::vector<std::unique_ptr<Value>> outer;
    outer.push_back(std::make_unique<Value>(make_array(std::move(s1_pair))));
    outer.push_back(std::make_unique<Value>(make_array(std::move(s2_pair))));
    Value v = make_array(std::move(outer));

    auto m = do_parse<qb::redis::map_stream_entry_list>(v);
    ASSERT_EQ(m.size(), 2u);
    ASSERT_EQ(m.at("stream-one").size(), 1u);
    ASSERT_EQ(m.at("stream-two").size(), 2u);
    EXPECT_EQ(m.at("stream-two")[1].id.timestamp, 3);
    EXPECT_EQ(m.at("stream-two")[1].fields.at("c"), "3");
}

// ============================================================================
// 5. parse<cluster_node>
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

// ADD: a master that owns several non-contiguous slot ranges — every trailing
// whitespace-delimited token must land in slots[], in order.
TEST(ReplyClusterNode, MultipleSlotRanges) {
    const std::string line = "abc123 127.0.0.1:7000@17000 myself,master - "
                             "0 0 1 connected 0-100 200-300 5000";
    Value             v(BulkString{line});

    auto node = do_parse<qb::redis::cluster_node>(v);
    EXPECT_EQ(node.master, "-");
    ASSERT_EQ(node.flags.size(), 2u);
    EXPECT_EQ(node.flags[0], "myself");
    EXPECT_EQ(node.flags[1], "master");
    ASSERT_EQ(node.slots.size(), 3u);
    EXPECT_EQ(node.slots[0], "0-100");
    EXPECT_EQ(node.slots[1], "200-300");
    EXPECT_EQ(node.slots[2], "5000");
}

// ============================================================================
// 6. parse<memory_info>
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

// RENAMED from NonNumericValueCaughtAsZero: a non-numeric field value is
// swallowed to 0 by the per-field stoull try/catch. This documents a lenient
// (lossy) behavior, not a desirable one — the name now says so explicitly.
TEST(ReplyMemoryInfo, MemoryInfoNonNumericSwallowedToZero) {
    std::vector<std::unique_ptr<Value>> elems;
    elems.push_back(mk_bulk("used_memory"));
    elems.push_back(mk_bulk("notanumber"));
    Value v = make_array(std::move(elems));

    auto info = do_parse<qb::redis::memory_info>(v);
    EXPECT_EQ(info.used_memory, 0u); // stoull throws -> caught -> 0 (lenient)
}

TEST(ReplyMemoryInfo, NonArrayThrows) {
    Value v(Integer{1});
    EXPECT_THROW(do_parse<qb::redis::memory_info>(v), qb::redis::ProtoError);
}

// ============================================================================
// 7. parse<std::vector<char>>
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
// 8. cluster_node truncated-line throws — each missing field, in order, raises
//    ProtoError. A CLUSTER NODES line is space-delimited; cutting it short after
//    each successive field exercises every "Failed to parse <field>" guard.
// ============================================================================

TEST(ReplyClusterNode, TruncatedLineThrowsAtEachField) {
    const char *truncated[] = {
        "theid",                                     // no address token
        "theid 127.0.0.1:7000@17000",                // no flags token
        "theid 127.0.0.1:7000@17000 myflags",        // no master field
        "theid 127.0.0.1:7000@17000 myflags -",      // no ping-sent field
        "theid 127.0.0.1:7000@17000 myflags - 0",    // no pong-recv field
        "theid 127.0.0.1:7000@17000 myflags - 0 0",  // no epoch field
        "theid 127.0.0.1:7000@17000 myflags - 0 0 0" // no link-state field
    };
    for (const char *line : truncated) {
        Value v(BulkString{line});
        EXPECT_THROW(do_parse<qb::redis::cluster_node>(v), qb::redis::ProtoError) << "line: " << line;
    }
}

// ============================================================================
// 9. score_member shape guards not reachable via the vector parser.
// ============================================================================

// Direct parse<score_member> on a non-array (the vector parser only ever calls
// it on validated 2-element sub-arrays, so this guard is otherwise untested).
TEST(ReplyScoreMember, NonArrayThrowsDirect) {
    Value v(Integer{1});
    EXPECT_THROW(do_parse<qb::redis::score_member>(v), qb::redis::ProtoError);
}

// RESP3 nested form ([[m,s],...]) with a null sub-element: a valid first pair
// selects the nested branch, then the null trips the per-element guard.
TEST(ReplyScoreMemberVector, NullElementInNestedThrows) {
    std::vector<std::unique_ptr<Value>> pair0;
    pair0.push_back(mk_bulk("alice"));
    pair0.push_back(mk_dbl(1.5));
    std::vector<std::unique_ptr<Value>> outer;
    outer.push_back(std::make_unique<Value>(make_array(std::move(pair0))));
    outer.push_back(std::unique_ptr<Value>{}); // null element
    Value v = make_array(std::move(outer));
    EXPECT_THROW(do_parse<std::vector<qb::redis::score_member>>(v), qb::redis::ProtoError);
}

// map_stream_entry_list on a non-array reply.
TEST(ReplyMapStreamEntryList, NonArrayThrows) {
    Value v(Integer{1});
    EXPECT_THROW(do_parse<qb::redis::map_stream_entry_list>(v), qb::redis::ProtoError);
}

// ============================================================================
// 10. pub/sub message/pmessage/subscription null-element guards. An array of the
//     correct length but with a null payload pointer trips the "Invalid …
//     format" throw (distinct from the too-short-array size check).
// ============================================================================

TEST(ReplyPubSubMessage, NullElementThrows) {
    std::vector<std::unique_ptr<Value>> e;
    e.push_back(mk_bulk("message"));
    e.push_back(std::unique_ptr<Value>{}); // null channel (index 1)
    e.push_back(mk_bulk("payload"));
    Value v = make_array(std::move(e));
    EXPECT_THROW(do_parse<qb::redis::message>(v), qb::redis::ProtoError);
}

TEST(ReplyPubSubPMessage, NullElementThrows) {
    std::vector<std::unique_ptr<Value>> e;
    e.push_back(mk_bulk("pmessage"));
    e.push_back(std::unique_ptr<Value>{}); // null pattern (index 1)
    e.push_back(mk_bulk("ch"));
    e.push_back(mk_bulk("payload"));
    Value v = make_array(std::move(e));
    EXPECT_THROW(do_parse<qb::redis::pmessage>(v), qb::redis::ProtoError);
}

TEST(ReplyPubSubSubscription, NullElementThrows) {
    std::vector<std::unique_ptr<Value>> e;
    e.push_back(mk_bulk("subscribe"));
    e.push_back(std::unique_ptr<Value>{}); // null channel (index 1)
    e.push_back(mk_int(1));
    Value v = make_array(std::move(e));
    EXPECT_THROW(do_parse<qb::redis::subscription>(v), qb::redis::ProtoError);
}

// ============================================================================
// 8. parse<std::chrono::milliseconds> and <seconds>
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
// 9. parse<geo_pos>
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
// 10. parse<search_result>
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
// 11. parse<pipeline_result>
// ============================================================================

TEST(ReplyPipelineResult, ErrorCount) {
    std::vector<std::unique_ptr<Value>> elems;
    elems.push_back(mk_simple("OK"));
    elems.push_back(std::make_unique<Value>(Value(qb::redis::parser::SimpleError{"ERR", "boom"})));
    elems.push_back(mk_int(1));
    elems.push_back(std::make_unique<Value>(Value(qb::redis::parser::SimpleError{"WRONGTYPE", "bad"})));
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
// 12. parse<status> nil/string/error/throw
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
    Value v(qb::redis::parser::SimpleError{"ERR", "bad"});
    auto  s = do_parse<qb::redis::status>(v);
    EXPECT_EQ(s.str(), "ERR bad");
    EXPECT_FALSE(s.ok());
}

TEST(ReplyStatus, IntegerThrows) {
    Value v(Integer{1});
    EXPECT_THROW(do_parse<qb::redis::status>(v), qb::redis::ProtoError);
}

// ============================================================================
// 13. parse<bool> nil/native/int/throw
// ============================================================================

TEST(ReplyBool, NilIsFalse) {
    Value v(Null{});
    EXPECT_FALSE(do_parse<bool>(v));
}

TEST(ReplyBool, NativeBoolean) {
    Value vt(qb::redis::parser::Boolean{true});
    Value vf(qb::redis::parser::Boolean{false});
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
// 14. parse<double> inf/+inf/-inf/int/string + reject + nan
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

// ADD: empty string is not a valid double — must be rejected, not silently 0.
TEST(ReplyDouble, EmptyStringRejected) {
    Value v(BulkString{""});
    EXPECT_THROW(do_parse<double>(v), qb::redis::ProtoError);
}

// ADD: "nan" is consumed whole by from_chars and yields a real NaN (documents
// that the parser does NOT special-case it as an error). Locks current behavior.
TEST(ReplyDouble, StringNanParsesToNan) {
    Value v(BulkString{"nan"});
    EXPECT_TRUE(std::isnan(do_parse<double>(v)));
}

// ============================================================================
// 15. parse<long long> — exact int64, including no-truncation edges
// ============================================================================

TEST(ReplyLongLong, Basic) {
    Value v(Integer{42});
    EXPECT_EQ(do_parse<long long>(v), 42);
}

TEST(ReplyLongLong, NonIntegerThrows) {
    Value v(BulkString{"42"});
    EXPECT_THROW(do_parse<long long>(v), qb::redis::ProtoError);
}

// ADD: a value above the 2^53 double-exact boundary must round-trip with full
// 64-bit precision (parse<long long> uses the raw int64, never a double).
TEST(ReplyLongLong, AboveDoubleExactBoundaryNoTruncation) {
    constexpr long long big = 9007199254740993LL; // 2^53 + 1
    Value               v(Integer{big});
    EXPECT_EQ(do_parse<long long>(v), big);
}

TEST(ReplyLongLong, Int64MinMax) {
    Value vmax(Integer{std::numeric_limits<int64_t>::max()});
    Value vmin(Integer{std::numeric_limits<int64_t>::min()});
    EXPECT_EQ(do_parse<long long>(vmax), std::numeric_limits<int64_t>::max());
    EXPECT_EQ(do_parse<long long>(vmin), std::numeric_limits<int64_t>::min());
}

// ============================================================================
// 16. parse<std::string> integer->"42" / nested-array->first / bulk / simple
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
// 17. associative map<string,string> from RESP Map AND flat array + throw
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
// 18. associative set<string> from RESP Set AND array
// ============================================================================

TEST(ReplySetContainer, FromRespSet) {
    std::vector<std::unique_ptr<Value>> elems;
    elems.push_back(mk_bulk("a"));
    elems.push_back(mk_bulk("b"));
    elems.push_back(mk_bulk("c"));
    Value v = make_set(std::move(elems));

    auto s = do_parse<std::set<std::string>>(v);
    // STRENGTHENED: assert exact element membership, not just size.
    ASSERT_EQ(s.size(), 3u);
    EXPECT_EQ(s, (std::set<std::string>{"a", "b", "c"}));
}

TEST(ReplySetContainer, FromArray) {
    std::vector<std::unique_ptr<Value>> elems;
    elems.push_back(mk_bulk("x"));
    elems.push_back(mk_bulk("y"));
    Value v = make_array(std::move(elems));

    auto s = do_parse<std::set<std::string>>(v);
    ASSERT_EQ(s.size(), 2u);
    EXPECT_EQ(s, (std::set<std::string>{"x", "y"}));
}

TEST(ReplySetContainer, NonArrayNonSetThrows) {
    Value v(Integer{1});
    EXPECT_THROW((do_parse<std::set<std::string>>(v)), qb::redis::ProtoError);
}

// ============================================================================
// 19. sequence vector<string> from array AND RESP3 set
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
    // STRENGTHENED: assert both elements present (set has no defined order, so
    // sort before comparing) rather than size-only.
    ASSERT_EQ(out.size(), 2u);
    std::sort(out.begin(), out.end());
    EXPECT_EQ(out, (std::vector<std::string>{"one", "two"}));
}

TEST(ReplySequence, NonArrayNonSetThrows) {
    Value v(Integer{1});
    EXPECT_THROW(do_parse<std::vector<std::string>>(v), qb::redis::ProtoError);
}

// ADD: a RESP3 Map target parsed into a SEQUENCE container (vector) must flatten
// to the RESP2 [k, v, k, v, ...] order (reply.h sequence parser, is_map branch).
// RESP3 returns CONFIG GET / CLIENT INFO / XPENDING summaries as a `%` map; a
// std::vector<std::string> target must see the identical flat sequence it would
// in RESP2, in insertion order.
TEST(ReplySequence, FromRespMapFlattens) {
    std::vector<std::pair<std::unique_ptr<Value>, std::unique_ptr<Value>>> entries;
    entries.push_back(std::make_pair(mk_bulk("k1"), mk_bulk("v1")));
    entries.push_back(std::make_pair(mk_bulk("k2"), mk_bulk("v2")));
    Value v = make_map(std::move(entries));

    auto out = do_parse<std::vector<std::string>>(v);
    ASSERT_EQ(out.size(), 4u);
    EXPECT_EQ(out, (std::vector<std::string>{"k1", "v1", "k2", "v2"}));
}

// ============================================================================
// 20. pair + tuple + short-array/non-array throws
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
// 21. optional<string> nil->nullopt / value
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
// 22. message / pmessage / subscription (Array + Push variant) + too-short throw
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
