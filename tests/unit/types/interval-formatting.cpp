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
// Pure-logic unit tests for the qbm-redis range/interval value types and the
// interval bound-formatting helpers. No live server, no network, no event loop:
// every test exercises the out-of-line specializations defined in redis.cpp
// directly (qb::redis::{Unbounded,Bounded,Left/RightBounded}Interval over `double`
// and `std::string`, plus the enum `to_string` stringizers and the score double
// formatter).
//
// Tier: unit. Daemon-free, parallel-safe, NO RESOURCE_LOCK.
//
// Ground-truth bound syntax (confirmed against redis.cpp):
//   - bound(x)   -> "[" + x   (CLOSED / inclusive endpoint)
//   - unbound(x) -> "(" + x   (OPEN   / exclusive endpoint)
//   - numeric infinities: "-inf" / "+inf"
//   - lexicographic infinities: "-" / "+"
//   - double scores are formatted via std::to_chars(general, 17 sig. digits);
//     a CLOSED numeric endpoint is the *bare* number (no "[" prefix), because
//     Redis treats a plain score as inclusive. Only OPEN prepends "(".
//   - string (lex) CLOSED endpoints DO carry the "[" prefix.
//

#include <charconv>
#include <cmath>
#include <gtest/gtest.h>
#include <limits>
#include <string>
// Umbrella header: pulls in types.h (interval types, BoundType, enums) and
// reply.h (qb::redis::Error, thrown by the interval ctors on invalid BoundType).
#include "../redis.h"

using qb::redis::BoundedInterval;
using qb::redis::BoundType;
using qb::redis::LeftBoundedInterval;
using qb::redis::RightBoundedInterval;
using qb::redis::UnboundedInterval;

// Convenience aliases declared by the module.
using qb::redis::lex_interval;   // BoundedInterval<std::string>
using qb::redis::score_interval; // BoundedInterval<double>

namespace {

// Parse a Redis-formatted numeric score back to a double. Strips a leading
// open-bound marker '(' if present. Returns true iff std::from_chars consumes
// the ENTIRE token with no error. NOTE: std::from_chars for floating point
// DOES accept the non-finite tokens "inf"/"-inf"/"nan" (per [charconv.from.chars]),
// so a "true" result does not by itself imply a finite value — callers that care
// about finiteness must additionally check std::isfinite(out).
[[nodiscard]] bool
parse_score(std::string_view s, double &out) {
    if (!s.empty() && s.front() == '(')
        s.remove_prefix(1);
    const char *begin = s.data();
    const char *end   = s.data() + s.size();
    const auto [ptr, ec] = std::from_chars(begin, end, out);
    return ec == std::errc{} && ptr == end;
}

} // namespace

// ============================================================================
// Unbounded intervals (no constructor args; fixed infinity sentinels)
// ============================================================================

TEST(IntervalUnboundedDouble, InfinitySentinels) {
    UnboundedInterval<double> i;
    EXPECT_EQ(i.lower(), "-inf");
    EXPECT_EQ(i.upper(), "+inf");
}

TEST(IntervalUnboundedString, InfinitySentinels) {
    UnboundedInterval<std::string> i;
    // Lexicographic ranges use bare "-" / "+" (NOT "-inf"/"+inf").
    EXPECT_EQ(i.lower(), "-");
    EXPECT_EQ(i.upper(), "+");
}

// ============================================================================
// BoundedInterval<double> : per-endpoint inclusivity
// ============================================================================

TEST(IntervalBoundedDouble, Closed) {
    // CLOSED numeric endpoints are bare numbers (Redis treats them inclusive).
    score_interval i(20.0, 40.0, BoundType::CLOSED);
    EXPECT_EQ(i.lower(), "20");
    EXPECT_EQ(i.upper(), "40");
}

TEST(IntervalBoundedDouble, Open) {
    // OPEN endpoints get the "(" exclusive marker on both ends.
    score_interval i(20.0, 40.0, BoundType::OPEN);
    EXPECT_EQ(i.lower(), "(20");
    EXPECT_EQ(i.upper(), "(40");
}

TEST(IntervalBoundedDouble, LeftOpen) {
    // LEFT_OPEN: lower exclusive "(", upper inclusive (bare).
    score_interval i(20.0, 40.0, BoundType::LEFT_OPEN);
    EXPECT_EQ(i.lower(), "(20");
    EXPECT_EQ(i.upper(), "40");
}

TEST(IntervalBoundedDouble, RightOpen) {
    // RIGHT_OPEN: lower inclusive (bare), upper exclusive "(".
    score_interval i(20.0, 40.0, BoundType::RIGHT_OPEN);
    EXPECT_EQ(i.lower(), "20");
    EXPECT_EQ(i.upper(), "(40");
}

TEST(IntervalBoundedDouble, FractionalFormatting) {
    // std::to_chars(general, 17) renders a round-trippable, locale-independent
    // representation. 1.5 / 2.25 are exactly representable.
    score_interval i(1.5, 2.25, BoundType::OPEN);
    EXPECT_EQ(i.lower(), "(1.5");
    EXPECT_EQ(i.upper(), "(2.25");
}

TEST(IntervalBoundedDouble, NegativeValues) {
    score_interval i(-3.0, 3.0, BoundType::CLOSED);
    EXPECT_EQ(i.lower(), "-3");
    EXPECT_EQ(i.upper(), "3");
}

TEST(IntervalBoundedDouble, ExplicitInfinityArgs) {
    // double_to_redis maps ±inf to the textual sentinels even as concrete args.
    score_interval i(-std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(), BoundType::CLOSED);
    EXPECT_EQ(i.lower(), "-inf");
    EXPECT_EQ(i.upper(), "+inf");
}

TEST(IntervalBoundedDouble, InfinityArgsOpenGetsMarker) {
    // OPEN still prepends "(" even for the infinity sentinel string.
    score_interval i(-std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(), BoundType::OPEN);
    EXPECT_EQ(i.lower(), "(-inf");
    EXPECT_EQ(i.upper(), "(+inf");
}

// ── ADDED: to_chars round-trip stability ────────────────────────────────────
// The score formatter (double_to_redis, redis.cpp) uses std::to_chars with 17
// significant digits, which is the IEEE-754 round-trip-exact precision. These
// tests pin (a) the exact textual form Redis receives for tricky values and
// (b) that the emitted text parses back to the *identical* double (no drift).

TEST(IntervalScoreFormat, RoundTripPointOne) {
    // 0.1 is not exactly representable; 17 sig-digits renders the full mantissa.
    score_interval i(0.1, 0.1, BoundType::CLOSED);
    EXPECT_EQ(i.lower(), "0.10000000000000001"); // exact textual form Redis sees
    double back = 0.0;
    ASSERT_TRUE(parse_score(i.lower(), back));
    EXPECT_EQ(back, 0.1); // round-trips to the identical double
}

TEST(IntervalScoreFormat, RoundTripLargeMagnitude) {
    // 1e300 forces scientific notation; confirm the exact form and exact round-trip.
    constexpr double big = 1e300;
    score_interval   i(big, big, BoundType::CLOSED);
    EXPECT_EQ(i.lower(), "1.0000000000000001e+300");
    double back = 0.0;
    ASSERT_TRUE(parse_score(i.lower(), back));
    EXPECT_EQ(back, big);
}

TEST(IntervalScoreFormat, RoundTripIrrationalApprox) {
    constexpr double pi = 3.14159265358979;
    score_interval   i(pi, pi, BoundType::OPEN);
    EXPECT_EQ(i.lower(), "(3.14159265358979");
    double back = 0.0;
    ASSERT_TRUE(parse_score(i.lower(), back)); // strips the '(' marker
    EXPECT_EQ(back, pi);
}

TEST(IntervalScoreFormat, RoundTripSmallSubnormalNeighbour) {
    // A small-but-normal value with a long decimal expansion must still round-trip.
    constexpr double v = 1.2345678901234567e-10;
    score_interval   i(v, v, BoundType::CLOSED);
    double           back = 0.0;
    ASSERT_TRUE(parse_score(i.lower(), back));
    EXPECT_EQ(back, v);
}

TEST(IntervalScoreFormat, NaNScoreFormatsAsNan) {
    // double_to_redis only special-cases ±inf; NaN flows through to_chars, which
    // emits the lowercase token "nan". Pin this so a future formatter change
    // (e.g. throwing, or emitting "0") is caught here, not silently in Redis.
    const double nan = std::numeric_limits<double>::quiet_NaN();
    score_interval i(nan, nan, BoundType::CLOSED);
    EXPECT_EQ(i.lower(), "nan");
    EXPECT_EQ(i.upper(), "nan");
    // std::from_chars accepts the "nan" token (it parses non-finite floats), so
    // the token round-trips to a NaN value — NOT to a finite score. Assert both
    // the successful parse and the non-finiteness, which is the real contract.
    double back = 0.0;
    ASSERT_TRUE(parse_score(i.lower(), back));
    EXPECT_TRUE(std::isnan(back));
    EXPECT_FALSE(std::isfinite(back)); // "nan" is parseable but is not a finite score
}

TEST(IntervalScoreFormat, NaNScoreOpenGetsMarker) {
    const double   nan = std::numeric_limits<double>::quiet_NaN();
    score_interval i(nan, nan, BoundType::OPEN);
    EXPECT_EQ(i.lower(), "(nan");
    EXPECT_EQ(i.upper(), "(nan");
}

// ============================================================================
// LeftBoundedInterval<double> : lower endpoint set, upper fixed at +inf
// ============================================================================

TEST(IntervalLeftBoundedDouble, RightOpenInclusiveLower) {
    // RIGHT_OPEN => lower stays inclusive (bare); upper is the +inf sentinel.
    LeftBoundedInterval<double> i(10.0, BoundType::RIGHT_OPEN);
    EXPECT_EQ(i.lower(), "10");
    EXPECT_EQ(i.upper(), "+inf");
}

TEST(IntervalLeftBoundedDouble, OpenExclusiveLower) {
    // OPEN => lower exclusive "("; upper still +inf.
    LeftBoundedInterval<double> i(10.0, BoundType::OPEN);
    EXPECT_EQ(i.lower(), "(10");
    EXPECT_EQ(i.upper(), "+inf");
}

TEST(IntervalLeftBoundedDouble, RejectsClosed) {
    // Only OPEN / RIGHT_OPEN are valid for a left-bounded interval.
    EXPECT_THROW((LeftBoundedInterval<double>(10.0, BoundType::CLOSED)), qb::redis::Error);
    EXPECT_THROW((LeftBoundedInterval<double>(10.0, BoundType::LEFT_OPEN)), qb::redis::Error);
}

// ============================================================================
// RightBoundedInterval<double> : upper endpoint set, lower fixed at -inf
// ============================================================================

TEST(IntervalRightBoundedDouble, LeftOpenInclusiveUpper) {
    // LEFT_OPEN => upper stays inclusive (bare); lower is the -inf sentinel.
    RightBoundedInterval<double> i(50.0, BoundType::LEFT_OPEN);
    EXPECT_EQ(i.lower(), "-inf");
    EXPECT_EQ(i.upper(), "50");
}

TEST(IntervalRightBoundedDouble, OpenExclusiveUpper) {
    // OPEN => upper exclusive "("; lower still -inf.
    RightBoundedInterval<double> i(50.0, BoundType::OPEN);
    EXPECT_EQ(i.lower(), "-inf");
    EXPECT_EQ(i.upper(), "(50");
}

TEST(IntervalRightBoundedDouble, RejectsClosed) {
    // Only OPEN / LEFT_OPEN are valid for a right-bounded interval.
    EXPECT_THROW((RightBoundedInterval<double>(50.0, BoundType::CLOSED)), qb::redis::Error);
    EXPECT_THROW((RightBoundedInterval<double>(50.0, BoundType::RIGHT_OPEN)), qb::redis::Error);
}

// ============================================================================
// BoundedInterval<std::string> : lexicographic, CLOSED carries "[" prefix
// ============================================================================

TEST(IntervalBoundedString, Closed) {
    lex_interval i("b", "d", BoundType::CLOSED);
    EXPECT_EQ(i.lower(), "[b");
    EXPECT_EQ(i.upper(), "[d");
}

TEST(IntervalBoundedString, Open) {
    lex_interval i("b", "d", BoundType::OPEN);
    EXPECT_EQ(i.lower(), "(b");
    EXPECT_EQ(i.upper(), "(d");
}

TEST(IntervalBoundedString, LeftOpen) {
    // LEFT_OPEN: lower exclusive "(", upper inclusive "[".
    lex_interval i("b", "e", BoundType::LEFT_OPEN);
    EXPECT_EQ(i.lower(), "(b");
    EXPECT_EQ(i.upper(), "[e");
}

TEST(IntervalBoundedString, RightOpen) {
    // RIGHT_OPEN: lower inclusive "[", upper exclusive "(".
    lex_interval i("b", "e", BoundType::RIGHT_OPEN);
    EXPECT_EQ(i.lower(), "[b");
    EXPECT_EQ(i.upper(), "(e");
}

TEST(IntervalBoundedString, EmptyMembers) {
    // Markers are prepended even to empty member strings.
    lex_interval i("", "", BoundType::CLOSED);
    EXPECT_EQ(i.lower(), "[");
    EXPECT_EQ(i.upper(), "[");
}

// ============================================================================
// LeftBoundedInterval<std::string> : lower set, upper fixed at "+"
// ============================================================================

TEST(IntervalLeftBoundedString, RightOpenInclusiveLower) {
    LeftBoundedInterval<std::string> i("b", BoundType::RIGHT_OPEN);
    EXPECT_EQ(i.lower(), "[b");
    EXPECT_EQ(i.upper(), "+");
}

TEST(IntervalLeftBoundedString, OpenExclusiveLower) {
    LeftBoundedInterval<std::string> i("b", BoundType::OPEN);
    EXPECT_EQ(i.lower(), "(b");
    EXPECT_EQ(i.upper(), "+");
}

TEST(IntervalLeftBoundedString, RejectsClosed) {
    EXPECT_THROW((LeftBoundedInterval<std::string>("b", BoundType::CLOSED)), qb::redis::Error);
    EXPECT_THROW((LeftBoundedInterval<std::string>("b", BoundType::LEFT_OPEN)), qb::redis::Error);
}

// ============================================================================
// RightBoundedInterval<std::string> : upper set, lower fixed at "-"
// ============================================================================

TEST(IntervalRightBoundedString, LeftOpenInclusiveUpper) {
    RightBoundedInterval<std::string> i("e", BoundType::LEFT_OPEN);
    EXPECT_EQ(i.lower(), "-");
    EXPECT_EQ(i.upper(), "[e");
}

TEST(IntervalRightBoundedString, OpenExclusiveUpper) {
    RightBoundedInterval<std::string> i("e", BoundType::OPEN);
    EXPECT_EQ(i.lower(), "-");
    EXPECT_EQ(i.upper(), "(e");
}

TEST(IntervalRightBoundedString, RejectsClosed) {
    EXPECT_THROW((RightBoundedInterval<std::string>("e", BoundType::CLOSED)), qb::redis::Error);
    EXPECT_THROW((RightBoundedInterval<std::string>("e", BoundType::RIGHT_OPEN)), qb::redis::Error);
}

// ============================================================================
// Enum stringizers in redis.cpp — the full to_string(...) family.
// redis.cpp defines exactly six overloads (BitOp, UpdateType, Aggregation,
// GeoUnit, InsertPosition, ListPosition); all six are covered below, every
// enumerator asserted. (XtrimStrategy intentionally has NO to_string overload —
// it is serialized inline by the stream command builder, so there is nothing to
// stringize here; adding a probe for it would not compile.)
// ============================================================================

TEST(EnumToString, BitOp) {
    EXPECT_EQ(qb::redis::to_string(qb::redis::BitOp::AND), "AND");
    EXPECT_EQ(qb::redis::to_string(qb::redis::BitOp::OR), "OR");
    EXPECT_EQ(qb::redis::to_string(qb::redis::BitOp::XOR), "XOR");
    EXPECT_EQ(qb::redis::to_string(qb::redis::BitOp::NOT), "NOT");
}

TEST(EnumToString, UpdateType) {
    EXPECT_EQ(qb::redis::to_string(qb::redis::UpdateType::EXIST), "XX");
    EXPECT_EQ(qb::redis::to_string(qb::redis::UpdateType::NOT_EXIST), "NX");
    EXPECT_EQ(qb::redis::to_string(qb::redis::UpdateType::ALWAYS), "");
}

TEST(EnumToString, Aggregation) {
    EXPECT_EQ(qb::redis::to_string(qb::redis::Aggregation::SUM), "SUM");
    EXPECT_EQ(qb::redis::to_string(qb::redis::Aggregation::MIN), "MIN");
    EXPECT_EQ(qb::redis::to_string(qb::redis::Aggregation::MAX), "MAX");
}

TEST(EnumToString, GeoUnit) {
    EXPECT_EQ(qb::redis::to_string(qb::redis::GeoUnit::M), "m");
    EXPECT_EQ(qb::redis::to_string(qb::redis::GeoUnit::KM), "km");
    EXPECT_EQ(qb::redis::to_string(qb::redis::GeoUnit::MI), "mi");
    EXPECT_EQ(qb::redis::to_string(qb::redis::GeoUnit::FT), "ft");
}

TEST(EnumToString, InsertPosition) {
    EXPECT_EQ(qb::redis::to_string(qb::redis::InsertPosition::BEFORE), "BEFORE");
    EXPECT_EQ(qb::redis::to_string(qb::redis::InsertPosition::AFTER), "AFTER");
}

TEST(EnumToString, ListPosition) {
    EXPECT_EQ(qb::redis::to_string(qb::redis::ListPosition::LEFT), "LEFT");
    EXPECT_EQ(qb::redis::to_string(qb::redis::ListPosition::RIGHT), "RIGHT");
}
