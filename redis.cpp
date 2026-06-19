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

#include "redis.h"
#include <charconv>
#include <cmath>

namespace {

// Constants for infinity representations in Redis range queries
const std::string NEGATIVE_INFINITY_NUMERIC = "-inf";
const std::string POSITIVE_INFINITY_NUMERIC = "+inf";

/**
 * @brief Converts a double to the Redis score string representation.
 *
 * Uses std::to_chars with 17 significant digits (round-trip exact for IEEE 754)
 * and handles ±infinity correctly. Locale-independent.
 */
std::string
double_to_redis(double v) {
    if (std::isinf(v))
        return v > 0 ? "+inf" : "-inf";
    char buf[64];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v, std::chars_format::general, 17);
    if (ec == std::errc{})
        return std::string(buf, ptr - buf);
    return "0"; // unreachable in practice
}
const std::string NEGATIVE_INFINITY_STRING = "-";
const std::string POSITIVE_INFINITY_STRING = "+";

/**
 * @brief Creates an unbounded representation of a value
 *
 * Prepends "(" to the value to indicate an open bound in Redis range queries.
 *
 * @param bnd The value to convert to unbounded format
 * @return String representation of the unbounded value
 */
std::string unbound(const std::string &bnd);

/**
 * @brief Creates a bounded representation of a value
 *
 * Prepends "[" to the value to indicate a closed bound in Redis range queries.
 *
 * @param bnd The value to convert to bounded format
 * @return String representation of the bounded value
 */
std::string bound(const std::string &bnd);

} // namespace

namespace qb::redis {

/**
 * @brief Gets the lower bound for an unbounded double interval
 * @return String representing negative infinity for numeric ranges
 */
const std::string &
UnboundedInterval<double>::lower() const {
    return NEGATIVE_INFINITY_NUMERIC;
}

/**
 * @brief Gets the upper bound for an unbounded double interval
 * @return String representing positive infinity for numeric ranges
 */
const std::string &
UnboundedInterval<double>::upper() const {
    return POSITIVE_INFINITY_NUMERIC;
}

/**
 * @brief Constructs a bounded interval for double values
 *
 * @param min Minimum value of the interval
 * @param max Maximum value of the interval
 * @param type Type of bounds (open, closed, etc.)
 */
BoundedInterval<double>::BoundedInterval(double min, double max, BoundType type)
    : _min(double_to_redis(min))
    , _max(double_to_redis(max)) {
    switch (type) {
        case BoundType::CLOSED:
            // Do nothing
            break;

        case BoundType::OPEN:
            _min = unbound(_min);
            _max = unbound(_max);
            break;

        case BoundType::LEFT_OPEN:
            _min = unbound(_min);
            break;

        case BoundType::RIGHT_OPEN:
            _max = unbound(_max);
            break;

        default:
            throw Error("Unknown BoundType");
    }
}

/**
 * @brief Constructs a left-bounded interval for double values
 *
 * @param min Minimum value of the interval
 * @param type Type of bounds (open or right-open)
 */
LeftBoundedInterval<double>::LeftBoundedInterval(double min, BoundType type)
    : _min(double_to_redis(min)) {
    switch (type) {
        case BoundType::OPEN:
            _min = unbound(_min);
            break;

        case BoundType::RIGHT_OPEN:
            // Do nothing.
            break;

        default:
            throw Error("Bound type can only be OPEN or RIGHT_OPEN");
    }
}

/**
 * @brief Gets the upper bound for a left-bounded double interval
 * @return String representing positive infinity for numeric ranges
 */
const std::string &
LeftBoundedInterval<double>::upper() const {
    return POSITIVE_INFINITY_NUMERIC;
}

/**
 * @brief Constructs a right-bounded interval for double values
 *
 * @param max Maximum value of the interval
 * @param type Type of bounds (open or left-open)
 */
RightBoundedInterval<double>::RightBoundedInterval(double max, BoundType type)
    : _max(double_to_redis(max)) {
    switch (type) {
        case BoundType::OPEN:
            _max = unbound(_max);
            break;

        case BoundType::LEFT_OPEN:
            // Do nothing.
            break;

        default:
            throw Error("Bound type can only be OPEN or LEFT_OPEN");
    }
}

/**
 * @brief Gets the lower bound for a right-bounded double interval
 * @return String representing negative infinity for numeric ranges
 */
const std::string &
RightBoundedInterval<double>::lower() const {
    return NEGATIVE_INFINITY_NUMERIC;
}

/**
 * @brief Gets the lower bound for an unbounded string interval
 * @return String representing negative infinity for string ranges
 */
const std::string &
UnboundedInterval<std::string>::lower() const {
    return NEGATIVE_INFINITY_STRING;
}

/**
 * @brief Gets the upper bound for an unbounded string interval
 * @return String representing positive infinity for string ranges
 */
const std::string &
UnboundedInterval<std::string>::upper() const {
    return POSITIVE_INFINITY_STRING;
}

/**
 * @brief Constructs a bounded interval for string values
 *
 * @param min Minimum value of the interval
 * @param max Maximum value of the interval
 * @param type Type of bounds (open, closed, etc.)
 */
BoundedInterval<std::string>::BoundedInterval(const std::string &min, const std::string &max, BoundType type) {
    switch (type) {
        case BoundType::CLOSED:
            _min = bound(min);
            _max = bound(max);
            break;

        case BoundType::OPEN:
            _min = unbound(min);
            _max = unbound(max);
            break;

        case BoundType::LEFT_OPEN:
            _min = unbound(min);
            _max = bound(max);
            break;

        case BoundType::RIGHT_OPEN:
            _min = bound(min);
            _max = unbound(max);
            break;

        default:
            throw Error("Unknown BoundType");
    }
}

/**
 * @brief Constructs a left-bounded interval for string values
 *
 * @param min Minimum value of the interval
 * @param type Type of bounds (open or right-open)
 */
LeftBoundedInterval<std::string>::LeftBoundedInterval(const std::string &min, BoundType type) {
    switch (type) {
        case BoundType::OPEN:
            _min = unbound(min);
            break;

        case BoundType::RIGHT_OPEN:
            _min = bound(min);
            break;

        default:
            throw Error("Bound type can only be OPEN or RIGHT_OPEN");
    }
}

/**
 * @brief Gets the upper bound for a left-bounded string interval
 * @return String representing positive infinity for string ranges
 */
const std::string &
LeftBoundedInterval<std::string>::upper() const {
    return POSITIVE_INFINITY_STRING;
}

/**
 * @brief Constructs a right-bounded interval for string values
 *
 * @param max Maximum value of the interval
 * @param type Type of bounds (open or left-open)
 */
RightBoundedInterval<std::string>::RightBoundedInterval(const std::string &max, BoundType type) {
    switch (type) {
        case BoundType::OPEN:
            _max = unbound(max);
            break;

        case BoundType::LEFT_OPEN:
            _max = bound(max);
            break;

        default:
            throw Error("Bound type can only be OPEN or LEFT_OPEN");
    }
}

/**
 * @brief Gets the lower bound for a right-bounded string interval
 * @return String representing negative infinity for string ranges
 */
const std::string &
RightBoundedInterval<std::string>::lower() const {
    return NEGATIVE_INFINITY_STRING;
}

} // namespace qb::redis

namespace {

std::string
unbound(const std::string &bnd) {
    return "(" + bnd;
}

std::string
bound(const std::string &bnd) {
    return "[" + bnd;
}

} // namespace

namespace qb::redis {

std::string
to_string(BitOp op) {
    switch (op) {
        case BitOp::AND:
            return "AND";
        case BitOp::OR:
            return "OR";
        case BitOp::XOR:
            return "XOR";
        case BitOp::NOT:
            return "NOT";
    }
    return {};
}

std::string
to_string(UpdateType op) {
    switch (op) {
        case UpdateType::EXIST:
            return "XX";
        case UpdateType::NOT_EXIST:
            return "NX";
        default:
            return {};
    }
}

std::string
to_string(Aggregation op) {
    switch (op) {
        case Aggregation::SUM:
            return "SUM";
        case Aggregation::MIN:
            return "MIN";
        case Aggregation::MAX:
            return "MAX";
    }
    return {};
}

std::string
to_string(GeoUnit op) {
    switch (op) {
        case GeoUnit::M:
            return "m";
        case GeoUnit::KM:
            return "km";
        case GeoUnit::MI:
            return "mi";
        case GeoUnit::FT:
            return "ft";
    }
    return {};
}

std::string
to_string(InsertPosition pos) {
    switch (pos) {
        case InsertPosition::BEFORE:
            return "BEFORE";
        case InsertPosition::AFTER:
            return "AFTER";
    }
    return {};
}

std::string
to_string(ListPosition pos) {
    switch (pos) {
        case ListPosition::LEFT:
            return "LEFT";
        case ListPosition::RIGHT:
            return "RIGHT";
    }
    return {};
}

} // namespace qb::redis