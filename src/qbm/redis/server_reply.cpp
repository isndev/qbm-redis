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
#include <qb/system/parse.h>

namespace qb::redis::detail {

bool
extract_string_value(const parser::Value &input, std::string &value, std::string &error) {
    if (input.is_null()) {
        error = "null value";
        return false;
    }
    if (!input.is_string()) {
        error = "not a string";
        return false;
    }
    value = input.as_string_view();
    return true;
}

bool
extract_integer_value(const parser::Value &input, int64_t &value, std::string &error) {
    if (input.is_null()) {
        error = "null value";
        return false;
    }
    if (!input.is_integer()) {
        error = "not an integer";
        return false;
    }
    value = input.as_integer().value;
    return true;
}

bool
extract_string_array_value(const parser::Value &input, std::vector<std::string> &value, std::string &error) {
    if (input.is_null())
        return true;
    if (!input.is_array()) {
        error = "not an array";
        return false;
    }

    const auto &arr = input.as_array();
    value.reserve(arr.size());

    for (const auto &elem : arr) {
        if (!elem || !elem->is_string()) {
            error = "array contains non-string";
            return false;
        }
        value.emplace_back(elem->as_string_view());
    }

    return true;
}

bool
extract_string_map_value(const parser::Value &input, qb::unordered_map<std::string, std::string> &value, std::string &error) {
    if (input.is_null())
        return true;
    if (!input.is_map()) {
        error = "not a map";
        return false;
    }

    const auto &map = input.as_map();
    value.reserve(map.size());

    for (const auto &entry : map) {
        if (!entry.first || !entry.first->is_string()) {
            error = "map key is not a string";
            return false;
        }
        if (!entry.second || !entry.second->is_string()) {
            error = "map value is not a string";
            return false;
        }
        value.emplace(std::string(entry.first->as_string_view()), std::string(entry.second->as_string_view()));
    }

    return true;
}

bool
extract_stream_id_value(const parser::Value &input, stream_id &value, std::string &error) {
    if (!input.is_string()) {
        error = "stream id must be a string";
        return false;
    }

    auto sv  = input.as_string_view();
    auto pos = sv.find('-');
    if (pos == std::string_view::npos) {
        error = "invalid stream id format";
        return false;
    }

    auto ts  = qb::to_number<long long>(sv.substr(0, pos));
    auto seq = qb::to_number<long long>(sv.substr(pos + 1));
    if (!ts || !seq) {
        error = "invalid stream id values";
        return false;
    }

    value.timestamp = *ts;
    value.sequence  = *seq;
    return true;
}

bool
extract_score_member_value(const parser::Array &arr, size_t index, score_member &value, std::string &error) {
    if (index + 1 >= arr.size()) {
        error = "not enough elements for score-member pair";
        return false;
    }

    // Member (string)
    if (!arr[index] || !arr[index]->is_string()) {
        error = "member must be a string";
        return false;
    }
    value.member = std::string(arr[index]->as_string_view());

    // Score (double or integer)
    if (!arr[index + 1]) {
        error = "score is null";
        return false;
    }
    if (arr[index + 1]->is_double()) {
        value.score = arr[index + 1]->as_double().value;
    } else if (arr[index + 1]->is_integer()) {
        value.score = static_cast<double>(arr[index + 1]->as_integer().value);
    } else {
        error = "score must be a number";
        return false;
    }

    return true;
}

} // namespace qb::redis::detail
