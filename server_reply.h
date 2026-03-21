/**
 * @file server_reply.h
 * @brief Server-side reply types, ValueExtractor, and AsyncResult
 */
/*
 * qb - C++ Actor Framework
 * Copyright (C) 2011-2025 isndev (cpp.actor). All rights reserved.
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

#ifndef QBM_REDIS_SERVER_REPLY_H
#define QBM_REDIS_SERVER_REPLY_H

#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <chrono>
#include "parser.h"
#include "types.h"

namespace qb::redis {

// ============================================================================
// Modern Reply Types - Direct use of parser::Value
// ============================================================================

/**
 * @struct ServerReply
 * @brief Simplified reply wrapper for server-side Redis handlers
 * @tparam T The result value type
 */
template <typename T>
struct ServerReply {
    bool ok{false};
    T value{};
    std::string error{};
    
    [[nodiscard]] bool is_ok() const noexcept { return ok; }
    [[nodiscard]] const T& result() const& { return value; }
    [[nodiscard]] T& result() & { return value; }
    [[nodiscard]] T&& result() && { return std::move(value); }
    [[nodiscard]] const std::string& error_message() const { return error; }
};

/** @brief Specialization for void (commands that don't return data) */
template <>
struct ServerReply<void> {
    bool ok{false};
    std::string error{};
    
    [[nodiscard]] bool is_ok() const noexcept { return ok; }
    [[nodiscard]] const std::string& error_message() const { return error; }
};

// ============================================================================
// Value Extractors - Modern C++23 approach
// ============================================================================

/**
 * @class ValueExtractor
 * @brief Safe extraction of typed values from parser::Value
 *
 * Provides optional-based accessors for string, integer, double, bool,
 * array, map, set, and error message.
 */
class ValueExtractor {
    const parser::Value* _value;
    
public:
    explicit ValueExtractor(const parser::Value& v) : _value(&v) {}
    explicit ValueExtractor(const std::unique_ptr<parser::Value>& v) : _value(v.get()) {}
    
    // String extraction
    [[nodiscard]] std::optional<std::string_view> as_string_view() const noexcept {
        if (!_value || !_value->is_string()) return std::nullopt;
        return _value->as_string_view();
    }
    
    [[nodiscard]] std::optional<std::string> as_string() const {
        auto sv = as_string_view();
        if (!sv) return std::nullopt;
        return std::string(*sv);
    }
    
    // Integer extraction
    [[nodiscard]] std::optional<int64_t> as_integer() const noexcept {
        if (!_value || !_value->is_integer()) return std::nullopt;
        return _value->as_integer().value;
    }
    
    // Double extraction
    [[nodiscard]] std::optional<double> as_double() const noexcept {
        if (!_value) return std::nullopt;
        if (_value->is_double()) return _value->as_double().value;
        if (_value->is_integer()) return static_cast<double>(_value->as_integer().value);
        return std::nullopt;
    }
    
    // Boolean extraction
    [[nodiscard]] std::optional<bool> as_bool() const noexcept {
        if (!_value || !_value->is_boolean()) return std::nullopt;
        return _value->as_boolean().value;
    }
    
    // Null check
    [[nodiscard]] bool is_null() const noexcept {
        return !_value || _value->is_null();
    }
    
    // Array iteration - returns span-like access
    [[nodiscard]] std::optional<std::reference_wrapper<const parser::Array>> as_array() const noexcept {
        if (!_value || !_value->is_array()) return std::nullopt;
        return std::cref(_value->as_array());
    }
    
    // Map iteration
    [[nodiscard]] std::optional<std::reference_wrapper<const parser::Map>> as_map() const noexcept {
        if (!_value || !_value->is_map()) return std::nullopt;
        return std::cref(_value->as_map());
    }
    
    // Set iteration
    [[nodiscard]] std::optional<std::reference_wrapper<const parser::Set>> as_set() const noexcept {
        if (!_value || !_value->is_set()) return std::nullopt;
        return std::cref(_value->as_set());
    }
    
    // Error check and message
    [[nodiscard]] bool is_error() const noexcept {
        return _value && _value->is_error();
    }
    
    [[nodiscard]] std::string get_error_message() const {
        if (!_value) return "no value";
        return _value->get_error_message();
    }
    
    // Raw access to underlying value
    [[nodiscard]] const parser::Value* raw() const noexcept { return _value; }
};

// ============================================================================
// Convenience helpers for common Redis patterns
// ============================================================================

// Extract string from reply
[[nodiscard]] inline std::expected<std::string, std::string> extract_string(
    const parser::Value& value) {
    if (value.is_null()) return std::unexpected("null value");
    if (!value.is_string()) return std::unexpected("not a string");
    return std::string(value.as_string_view());
}

// Extract integer from reply
[[nodiscard]] inline std::expected<int64_t, std::string> extract_integer(
    const parser::Value& value) {
    if (value.is_null()) return std::unexpected("null value");
    if (!value.is_integer()) return std::unexpected("not an integer");
    return value.as_integer().value;
}

// Extract array of strings
[[nodiscard]] inline std::expected<std::vector<std::string>, std::string> extract_string_array(
    const parser::Value& value) {
    if (value.is_null()) return std::expected<std::vector<std::string>, std::string>{};
    if (!value.is_array()) return std::unexpected("not an array");
    
    std::vector<std::string> result;
    const auto& arr = value.as_array();
    result.reserve(arr.size());
    
    for (const auto& elem : arr) {
        if (!elem || !elem->is_string()) {
            return std::unexpected("array contains non-string");
        }
        result.emplace_back(elem->as_string_view());
    }
    
    return result;
}

// Extract map of string to string
[[nodiscard]] inline std::expected<qb::unordered_map<std::string, std::string>, std::string> 
    extract_string_map(const parser::Value& value) {
    if (value.is_null()) return qb::unordered_map<std::string, std::string>{};
    if (!value.is_map()) return std::unexpected("not a map");
    
    qb::unordered_map<std::string, std::string> result;
    const auto& map = value.as_map();
    result.reserve(map.size());
    
    for (const auto& entry : map) {
        if (!entry.first || !entry.first->is_string()) {
            return std::unexpected("map key is not a string");
        }
        if (!entry.second || !entry.second->is_string()) {
            return std::unexpected("map value is not a string");
        }
        result.emplace(
            std::string(entry.first->as_string_view()),
            std::string(entry.second->as_string_view())
        );
    }
    
    return result;
}

// ============================================================================
// Async result wrapper for coroutines
// ============================================================================

/**
 * @class AsyncResult
 * @brief Coroutine-friendly result wrapper using std::expected
 * @tparam T The value type on success
 */
template <typename T>
class AsyncResult {
    std::expected<T, std::string> _result;
    
public:
    AsyncResult() = default;
    explicit AsyncResult(T&& v) : _result(std::move(v)) {}
    explicit AsyncResult(std::string&& e) : _result(std::unexpected(std::move(e))) {}
    
    [[nodiscard]] bool is_ok() const noexcept { return _result.has_value(); }
    [[nodiscard]] bool has_error() const noexcept { return !_result.has_value(); }
    
    [[nodiscard]] const T& value() const& { return _result.value(); }
    [[nodiscard]] T& value() & { return _result.value(); }
    [[nodiscard]] T&& value() && { return std::move(_result.value()); }
    
    [[nodiscard]] const std::string& error() const { return _result.error(); }
    
    // Conversions
    [[nodiscard]] operator bool() const noexcept { return is_ok(); }
    [[nodiscard]] const T* operator->() const { return &_result.value(); }
    [[nodiscard]] T* operator->() { return &_result.value(); }
};

// Specialization for void
template <>
class AsyncResult<void> {
    std::optional<std::string> _error;
    
public:
    AsyncResult() = default;
    explicit AsyncResult(std::string&& e) : _error(std::move(e)) {}
    
    [[nodiscard]] bool is_ok() const noexcept { return !_error.has_value(); }
    [[nodiscard]] bool has_error() const noexcept { return _error.has_value(); }
    [[nodiscard]] const std::string& error() const { return _error.value(); }
    
    [[nodiscard]] operator bool() const noexcept { return is_ok(); }
};

// ============================================================================
// Stream ID helpers
// ============================================================================

[[nodiscard]] inline std::expected<stream_id, std::string> extract_stream_id(
    const parser::Value& value) {
    if (!value.is_string()) return std::unexpected("stream id must be a string");
    
    auto sv = value.as_string_view();
    auto pos = sv.find('-');
    if (pos == std::string_view::npos) {
        return std::unexpected("invalid stream id format");
    }
    
    try {
        stream_id id;
        id.timestamp = std::stoll(std::string(sv.substr(0, pos)));
        id.sequence = std::stoll(std::string(sv.substr(pos + 1)));
        return id;
    } catch (const std::exception&) {
        return std::unexpected("invalid stream id values");
    }
}

// ============================================================================
// Score member helpers (for sorted sets)
// ============================================================================

[[nodiscard]] inline std::expected<score_member, std::string> extract_score_member(
    const parser::Array& arr, size_t index) {
    if (index + 1 >= arr.size()) {
        return std::unexpected("not enough elements for score-member pair");
    }
    
    score_member sm;
    
    // Member (string)
    if (!arr[index] || !arr[index]->is_string()) {
        return std::unexpected("member must be a string");
    }
    sm.member = std::string(arr[index]->as_string_view());
    
    // Score (double or integer)
    if (!arr[index + 1]) {
        return std::unexpected("score is null");
    }
    if (arr[index + 1]->is_double()) {
        sm.score = arr[index + 1]->as_double().value;
    } else if (arr[index + 1]->is_integer()) {
        sm.score = static_cast<double>(arr[index + 1]->as_integer().value);
    } else {
        return std::unexpected("score must be a number");
    }
    
    return sm;
}

} // namespace qb::redis

#endif // QBM_REDIS_SERVER_REPLY_H
