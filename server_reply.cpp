/**
 * @file qbm/redis/server_reply.cpp
 * @brief Out-of-line definitions of the server-side reply extraction helpers.
 *
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2026 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)
 * @ingroup Redis
 */
#include "server_reply.h"

namespace qb::redis {

expected<std::string, std::string>
extract_string(const parser::Value &value) {
    if (value.is_null())
        return unexpected("null value");
    if (!value.is_string())
        return unexpected("not a string");
    return std::string(value.as_string_view());
}

expected<int64_t, std::string>
extract_integer(const parser::Value &value) {
    if (value.is_null())
        return unexpected("null value");
    if (!value.is_integer())
        return unexpected("not an integer");
    return value.as_integer().value;
}

expected<std::vector<std::string>, std::string>
extract_string_array(const parser::Value &value) {
    if (value.is_null())
        return expected<std::vector<std::string>, std::string>{};
    if (!value.is_array())
        return unexpected("not an array");

    std::vector<std::string> result;
    const auto              &arr = value.as_array();
    result.reserve(arr.size());

    for (const auto &elem : arr) {
        if (!elem || !elem->is_string()) {
            return unexpected("array contains non-string");
        }
        result.emplace_back(elem->as_string_view());
    }

    return result;
}

expected<qb::unordered_map<std::string, std::string>, std::string>
extract_string_map(const parser::Value &value) {
    if (value.is_null())
        return qb::unordered_map<std::string, std::string>{};
    if (!value.is_map())
        return unexpected("not a map");

    qb::unordered_map<std::string, std::string> result;
    const auto                                 &map = value.as_map();
    result.reserve(map.size());

    for (const auto &entry : map) {
        if (!entry.first || !entry.first->is_string()) {
            return unexpected("map key is not a string");
        }
        if (!entry.second || !entry.second->is_string()) {
            return unexpected("map value is not a string");
        }
        result.emplace(std::string(entry.first->as_string_view()), std::string(entry.second->as_string_view()));
    }

    return result;
}

expected<stream_id, std::string>
extract_stream_id(const parser::Value &value) {
    if (!value.is_string())
        return unexpected("stream id must be a string");

    auto sv  = value.as_string_view();
    auto pos = sv.find('-');
    if (pos == std::string_view::npos) {
        return unexpected("invalid stream id format");
    }

    try {
        stream_id id;
        id.timestamp = std::stoll(std::string(sv.substr(0, pos)));
        id.sequence  = std::stoll(std::string(sv.substr(pos + 1)));
        return id;
    } catch (const std::exception &) {
        return unexpected("invalid stream id values");
    }
}

expected<score_member, std::string>
extract_score_member(const parser::Array &arr, size_t index) {
    if (index + 1 >= arr.size()) {
        return unexpected("not enough elements for score-member pair");
    }

    score_member sm;

    // Member (string)
    if (!arr[index] || !arr[index]->is_string()) {
        return unexpected("member must be a string");
    }
    sm.member = std::string(arr[index]->as_string_view());

    // Score (double or integer)
    if (!arr[index + 1]) {
        return unexpected("score is null");
    }
    if (arr[index + 1]->is_double()) {
        sm.score = arr[index + 1]->as_double().value;
    } else if (arr[index + 1]->is_integer()) {
        sm.score = static_cast<double>(arr[index + 1]->as_integer().value);
    } else {
        return unexpected("score must be a number");
    }

    return sm;
}

} // namespace qb::redis
