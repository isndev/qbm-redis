/*
* qb - C++ Actor Framework
* Copyright (C) 2011-2021 isndev (www.qbaf.io). All rights reserved.
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

namespace {

const std::string NEGATIVE_INFINITY_NUMERIC = "-inf";
const std::string POSITIVE_INFINITY_NUMERIC = "+inf";
const std::string NEGATIVE_INFINITY_STRING = "-";
const std::string POSITIVE_INFINITY_STRING = "+";

std::string unbound(const std::string &bnd);
std::string bound(const std::string &bnd);

} // namespace

namespace qb::redis {

const std::string &
UnboundedInterval<double>::lower() const {
    return NEGATIVE_INFINITY_NUMERIC;
}

const std::string &
UnboundedInterval<double>::upper() const {
    return POSITIVE_INFINITY_NUMERIC;
}

BoundedInterval<double>::BoundedInterval(double min, double max, BoundType type)
    : _min(std::to_string(min))
    , _max(std::to_string(max)) {
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
        throw Error("Unknow BoundType");
    }
}

LeftBoundedInterval<double>::LeftBoundedInterval(double min, BoundType type)
    : _min(std::to_string(min)) {
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

const std::string &
LeftBoundedInterval<double>::upper() const {
    return POSITIVE_INFINITY_NUMERIC;
}

RightBoundedInterval<double>::RightBoundedInterval(double max, BoundType type)
    : _max(std::to_string(max)) {
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

const std::string &
RightBoundedInterval<double>::lower() const {
    return NEGATIVE_INFINITY_NUMERIC;
}

const std::string &
UnboundedInterval<std::string>::lower() const {
    return NEGATIVE_INFINITY_STRING;
}

const std::string &
UnboundedInterval<std::string>::upper() const {
    return POSITIVE_INFINITY_STRING;
}

BoundedInterval<std::string>::BoundedInterval(
    const std::string &min, const std::string &max, BoundType type) {
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
        throw Error("Unknow BoundType");
    }
}

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

const std::string &
LeftBoundedInterval<std::string>::upper() const {
    return POSITIVE_INFINITY_STRING;
}

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

namespace std {
std::string
to_string(qb::redis::BitOp op) {
    static const qb::unordered_map<qb::redis::BitOp, std::string> op_to_string{
        {qb::redis::BitOp::AND, "AND"},
        {qb::redis::BitOp::NOT, "NOT"},
        {qb::redis::BitOp::OR, "OR"},
        {qb::redis::BitOp::XOR, "XOR"}};
    return op_to_string.at(op);
}

std::string
to_string(qb::redis::UpdateType op) {
    static const qb::unordered_map<qb::redis::UpdateType, std::string> ut_to_string{
        {qb::redis::UpdateType::EXIST, "XX"},
        {qb::redis::UpdateType::NOT_EXIST, "NX"},
        {qb::redis::UpdateType::ALWAYS, ""}};
    return ut_to_string.at(op);
}

std::string
to_string(qb::redis::Aggregation op) {
    static const qb::unordered_map<qb::redis::Aggregation, std::string> ag_to_string{
        {qb::redis::Aggregation::SUM, "SUM"},
        {qb::redis::Aggregation::MIN, "MIN"},
        {qb::redis::Aggregation::MAX, "MAX"}};
    return ag_to_string.at(op);
}

std::string
to_string(qb::redis::GeoUnit op) {
    static const qb::unordered_map<qb::redis::GeoUnit, std::string> ag_to_string{
        {qb::redis::GeoUnit::M, "m"},
        {qb::redis::GeoUnit::KM, "km"},
        {qb::redis::GeoUnit::MI, "mi"},
        {qb::redis::GeoUnit::FT, "ft"}};
    return ag_to_string.at(op);
}

std::string
to_string(qb::redis::InsertPosition pos) {
    switch (pos) {
    case qb::redis::InsertPosition::BEFORE:
        return "BEFORE";
    case qb::redis::InsertPosition::AFTER:
        return "AFTER";
    default:
        return "";
    }
}
} // namespace std