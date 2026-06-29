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
 * @file qbm/redis/tests/unit/types/value-domain-types.cpp
 * @brief Pure-logic unit tests for the qbm-redis value-domain structs in
 *        `qbm/redis/types.h` — the inline accessors, comparison/equality
 *        operators and conversions that are exercised only indirectly (if at
 *        all) by the reply-parse path.
 *
 * Covers, directly and by value:
 *   - @ref qb::redis::stream_id   to_string(), ==, !=, ordering operator<
 *   - @ref qb::redis::score       == and ordering operator<
 *   - @ref qb::redis::score_member defaulted ==
 *   - @ref qb::redis::geo_pos     defaulted == (and != via the defaulted op)
 *   - @ref qb::redis::geo_distance / @ref qb::redis::stream_entry aggregates
 *   - @ref qb::redis::status      ctors, operator std::string, operator bool,
 *                                 operator(), ok(), operator== / operator!=
 *   - @ref qb::redis::json_value  is_null/is_bool/is_number/is_string/
 *                                 is_array/is_object over every alternative,
 *                                 plus the default-constructed Null state
 *   - @ref qb::redis::LimitOptions / @ref qb::redis::pipeline_result defaults
 *   - @ref qb::redis::subscription / @ref qb::redis::scan / @ref qb::redis::error
 *                                 / @ref qb::redis::message / @ref qb::redis::pmessage
 *
 * Tier: unit. Daemon-free, parallel-safe, NO RESOURCE_LOCK. The umbrella header
 * `../redis.h` pulls in types.h (these structs) and reply.h.
 *
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2026 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)
 * @ingroup Redis
 */

#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <variant>
#include <vector>
// Umbrella header: brings in types.h (the value-domain structs under test) and
// reply.h. Mirrors interval-formatting.cpp in this same directory.
#include "../redis.h"

// ============================================================================
// stream_id — to_string(), equality, inequality and the strict-weak ordering.
// ============================================================================

TEST(StreamId, ToStringRendersTimestampDashSequence) {
    EXPECT_EQ((qb::redis::stream_id{1526919030474, 55}).to_string(), "1526919030474-55");
    EXPECT_EQ((qb::redis::stream_id{0, 0}).to_string(), "0-0");
}

TEST(StreamId, EqualityAndInequality) {
    qb::redis::stream_id a{10, 3};
    qb::redis::stream_id same{10, 3};
    qb::redis::stream_id diff_seq{10, 4};
    qb::redis::stream_id diff_ts{11, 3};

    EXPECT_TRUE(a == same);
    EXPECT_FALSE(a != same);
    EXPECT_TRUE(a != diff_seq);
    EXPECT_TRUE(a != diff_ts);
    EXPECT_FALSE(a == diff_seq);
}

TEST(StreamId, OrderingByTimestampThenSequence) {
    // Lower timestamp orders first regardless of sequence.
    EXPECT_TRUE((qb::redis::stream_id{1, 999}) < (qb::redis::stream_id{2, 0}));
    // Equal timestamps fall back to the sequence component.
    EXPECT_TRUE((qb::redis::stream_id{5, 1}) < (qb::redis::stream_id{5, 2}));
    EXPECT_FALSE((qb::redis::stream_id{5, 2}) < (qb::redis::stream_id{5, 1}));
    // Strict: an id is not less than itself.
    EXPECT_FALSE((qb::redis::stream_id{5, 2}) < (qb::redis::stream_id{5, 2}));
}

// ============================================================================
// score — defaulted equality + the value-ordering operator<.
// ============================================================================

TEST(Score, EqualityComparesValue) {
    EXPECT_TRUE((qb::redis::score{1.5}) == (qb::redis::score{1.5}));
    EXPECT_FALSE((qb::redis::score{1.5}) == (qb::redis::score{1.6}));
}

TEST(Score, OrderingByValue) {
    EXPECT_TRUE((qb::redis::score{-3.0}) < (qb::redis::score{0.0}));
    EXPECT_FALSE((qb::redis::score{0.0}) < (qb::redis::score{-3.0}));
    EXPECT_FALSE((qb::redis::score{2.0}) < (qb::redis::score{2.0})); // strict
}

// ============================================================================
// score_member — defaulted aggregate equality (score AND member must match).
// ============================================================================

TEST(ScoreMember, EqualityRequiresBothFields) {
    qb::redis::score_member a{3.5, "alice"};
    EXPECT_TRUE((a == qb::redis::score_member{3.5, "alice"}));
    EXPECT_FALSE((a == qb::redis::score_member{3.5, "bob"}));   // member differs
    EXPECT_FALSE((a == qb::redis::score_member{9.0, "alice"})); // score differs
}

// ============================================================================
// geo_pos — defaulted equality over (longitude, latitude).
// ============================================================================

TEST(GeoPos, EqualityComparesBothCoordinates) {
    qb::redis::geo_pos p{13.361389, 38.115556};
    EXPECT_TRUE((p == qb::redis::geo_pos{13.361389, 38.115556}));
    EXPECT_FALSE((p == qb::redis::geo_pos{13.361389, 0.0})); // latitude differs
    EXPECT_FALSE((p == qb::redis::geo_pos{0.0, 38.115556})); // longitude differs
    // The defaulted operator== also synthesizes operator!= in C++20.
    EXPECT_TRUE((p != qb::redis::geo_pos{0.0, 0.0}));
}

// ============================================================================
// geo_distance / stream_entry — aggregate construction carries its payload.
// ============================================================================

TEST(GeoDistance, AggregateHoldsMemberAndDistance) {
    qb::redis::geo_distance d{"Palermo", 190.4424};
    EXPECT_EQ(d.member, "Palermo");
    EXPECT_DOUBLE_EQ(d.distance, 190.4424);
}

TEST(StreamEntry, AggregateHoldsIdAndFields) {
    qb::redis::stream_entry e;
    e.id = qb::redis::stream_id{42, 7};
    e.fields.emplace("temp", "21.5");
    e.fields.emplace("unit", "C");

    EXPECT_EQ(e.id.to_string(), "42-7");
    ASSERT_EQ(e.fields.size(), 2u);
    EXPECT_EQ(e.fields.at("temp"), "21.5");
    EXPECT_EQ(e.fields.at("unit"), "C");
}

// ============================================================================
// status — every conversion and comparison. (parse<status> covers str()/ok()
// in reply-parse.cpp; here we pin the conversion operators directly.)
// ============================================================================

TEST(Status, DefaultIsEmptyAndNotOk) {
    qb::redis::status s; // default ctor
    EXPECT_TRUE(s.str().empty());
    EXPECT_FALSE(s.ok());
    EXPECT_FALSE(static_cast<bool>(s));
    EXPECT_FALSE(s()); // operator()
    EXPECT_EQ(static_cast<std::string>(s), "");
}

TEST(Status, OkStringIsTruthyEveryWay) {
    qb::redis::status s{std::string("OK")}; // explicit string ctor
    EXPECT_TRUE(s.ok());
    EXPECT_TRUE(static_cast<bool>(s)); // operator bool
    EXPECT_TRUE(s());                  // operator()
    EXPECT_EQ(s.str(), "OK");
    EXPECT_EQ(static_cast<std::string>(s), "OK"); // operator std::string
}

TEST(Status, NonOkStringIsFalsyButPreserved) {
    // Only the literal "OK" is truthy; any other status text is preserved but
    // reports false through every boolean accessor.
    qb::redis::status s{std::string("QUEUED")};
    EXPECT_FALSE(s.ok());
    EXPECT_FALSE(static_cast<bool>(s));
    EXPECT_FALSE(s());
    EXPECT_EQ(s.str(), "QUEUED");
}

TEST(Status, ComparisonAgainstRawString) {
    qb::redis::status s{std::string("PONG")};
    EXPECT_TRUE(s == std::string("PONG"));  // operator==
    EXPECT_FALSE(s != std::string("PONG")); // operator!=
    EXPECT_TRUE(s != std::string("OK"));
    EXPECT_FALSE(s == std::string("OK"));
}

// ============================================================================
// json_value — the is_* convenience predicates for every Type alternative.
// reply-json-decode.cpp builds json_value via parse<> and reads `data`/`type`
// directly; the predicate accessors themselves are exercised here.
// ============================================================================

TEST(JsonValue, DefaultConstructedIsNull) {
    qb::redis::json_value j; // default => Type::Null, holds nullptr
    EXPECT_EQ(j.type, qb::redis::json_value::Type::Null);
    EXPECT_TRUE(j.is_null());
    EXPECT_FALSE(j.is_bool());
    EXPECT_FALSE(j.is_number());
    EXPECT_FALSE(j.is_string());
    EXPECT_FALSE(j.is_array());
    EXPECT_FALSE(j.is_object());
}

TEST(JsonValue, BooleanPredicate) {
    qb::redis::json_value j;
    j.type = qb::redis::json_value::Type::Boolean;
    j.data = true;
    EXPECT_TRUE(j.is_bool());
    EXPECT_FALSE(j.is_null());
    EXPECT_FALSE(j.is_number());
    EXPECT_TRUE(std::get<bool>(j.data));
}

TEST(JsonValue, NumberPredicate) {
    qb::redis::json_value j;
    j.type = qb::redis::json_value::Type::Number;
    j.data = 2.5;
    EXPECT_TRUE(j.is_number());
    EXPECT_FALSE(j.is_string());
    EXPECT_DOUBLE_EQ(std::get<double>(j.data), 2.5);
}

TEST(JsonValue, StringPredicate) {
    qb::redis::json_value j;
    j.type = qb::redis::json_value::Type::String;
    j.data = std::string("hello");
    EXPECT_TRUE(j.is_string());
    EXPECT_FALSE(j.is_array());
    EXPECT_EQ(std::get<std::string>(j.data), "hello");
}

TEST(JsonValue, ArrayPredicate) {
    qb::redis::json_value child;
    child.type = qb::redis::json_value::Type::Number;
    child.data = 1.0;

    qb::redis::json_value j;
    j.type = qb::redis::json_value::Type::Array;
    j.data = std::vector<qb::redis::json_value>{child};
    EXPECT_TRUE(j.is_array());
    EXPECT_FALSE(j.is_object());
    ASSERT_EQ(std::get<std::vector<qb::redis::json_value>>(j.data).size(), 1u);
}

TEST(JsonValue, ObjectPredicate) {
    qb::redis::json_value member;
    member.type = qb::redis::json_value::Type::Boolean;
    member.data = false;

    qb::redis::json_value j;
    j.type = qb::redis::json_value::Type::Object;
    qb::unordered_map<std::string, qb::redis::json_value> obj;
    obj.emplace("flag", member);
    j.data = std::move(obj);
    EXPECT_TRUE(j.is_object());
    EXPECT_FALSE(j.is_array());
    EXPECT_FALSE(j.is_null());
    // Extract first: the comma in the map's template args would be parsed as a macro
    // argument separator inside EXPECT_EQ.
    const auto object_size = std::get<qb::unordered_map<std::string, qb::redis::json_value>>(j.data).size();
    EXPECT_EQ(object_size, 1u);
}

// ============================================================================
// Option / result aggregates — documented defaults are the actual contract.
// ============================================================================

TEST(LimitOptions, DefaultsAreOffsetZeroCountUnlimited) {
    qb::redis::LimitOptions lo; // {0, -1}
    EXPECT_EQ(lo.offset, 0);
    EXPECT_EQ(lo.count, -1); // -1 == unlimited
}

TEST(PipelineResult, DefaultsReportEmptyAllSucceeded) {
    qb::redis::pipeline_result pr; // {0, 0, true}
    EXPECT_EQ(pr.size, 0u);
    EXPECT_EQ(pr.error_count, 0u);
    EXPECT_TRUE(pr.all_succeeded);
}

TEST(PipelineResult, FailureFieldsArePreserved) {
    qb::redis::pipeline_result pr{3, 1, false};
    EXPECT_EQ(pr.size, 3u);
    EXPECT_EQ(pr.error_count, 1u);
    EXPECT_FALSE(pr.all_succeeded);
}

// ============================================================================
// subscription — optional channel (engaged + unset) and remaining count.
// ============================================================================

TEST(Subscription, EngagedChannelAndCount) {
    qb::redis::subscription sub{std::optional<std::string>("news"), 2};
    ASSERT_TRUE(sub.channel.has_value());
    EXPECT_EQ(*sub.channel, "news");
    EXPECT_EQ(sub.num, 2);
}

TEST(Subscription, UnsetChannelOnGlobalUnsubscribe) {
    qb::redis::subscription sub{std::nullopt, 0};
    EXPECT_FALSE(sub.channel.has_value());
    EXPECT_EQ(sub.num, 0);
}

// ============================================================================
// scan<Out> — cursor + items, default and custom container alternatives.
// ============================================================================

TEST(Scan, DefaultVectorContainer) {
    qb::redis::scan<> s; // Out defaults to std::vector<std::string>
    s.cursor = 17;
    s.items  = {"a", "b"};
    EXPECT_EQ(s.cursor, 17u);
    ASSERT_EQ(s.items.size(), 2u);
    EXPECT_EQ(s.items[0], "a");
    EXPECT_EQ(s.items[1], "b");
}

TEST(Scan, ZeroCursorMeansComplete) {
    qb::redis::scan<> s;
    s.cursor = 0; // documented: 0 when the scan is complete
    EXPECT_EQ(s.cursor, 0u);
    EXPECT_TRUE(s.items.empty());
}

// ============================================================================
// error / message / pmessage — value-carrying structs used by the pub/sub and
// error reply paths. Asserts field carriage; the reply_ptr stays null here.
// ============================================================================

TEST(Error, CarriesWhatMessage) {
    qb::redis::error e{"WRONGTYPE Operation against a key", nullptr};
    EXPECT_EQ(e.what, "WRONGTYPE Operation against a key");
    EXPECT_EQ(e.raw, nullptr);
}

TEST(Message, CarriesChannelAndPayload) {
    qb::redis::message m;
    m.channel = "chan";
    m.payload = "body";
    EXPECT_TRUE(m.pattern.empty()); // plain SUBSCRIBE => no pattern
    EXPECT_EQ(m.channel, "chan");
    EXPECT_EQ(m.payload, "body");
    EXPECT_EQ(m.raw, nullptr);
}

TEST(PMessage, InheritsMessageWithPattern) {
    qb::redis::pmessage pm; // : public message
    pm.pattern = "news.*";  // PSUBSCRIBE sets the matching pattern
    pm.channel = "news.sports";
    pm.payload = "goal";
    // Slices to the message base; the inherited fields are intact.
    const qb::redis::message &base = pm;
    EXPECT_EQ(base.pattern, "news.*");
    EXPECT_EQ(base.channel, "news.sports");
    EXPECT_EQ(base.payload, "goal");
}
