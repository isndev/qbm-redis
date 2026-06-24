/**
 * @file qbm/redis/server_reply.h
 * @brief Server-side reply types, value extraction, and coroutine result wrappers.
 *
 * Provides the building blocks used by server-side Redis handlers to inspect and
 * consume parsed protocol values:
 *  - @ref qb::redis::ServerReply, a lightweight success/value/error wrapper.
 *  - @ref qb::redis::ValueExtractor, optional-based typed accessors over a
 *    @ref qb::redis::parser::Value.
 *  - @ref qb::redis::AsyncResult, an @c expected -based result type suited to
 *    coroutine-style consumption.
 *  - Free convenience helpers that extract common shapes (strings, integers,
 *    string arrays/maps, stream ids, sorted-set score/member pairs) from a
 *    parsed value.
 *
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2026 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)
 * @ingroup Redis
 */
#ifndef QBM_REDIS_SERVER_REPLY_H
#define QBM_REDIS_SERVER_REPLY_H

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <qb/system/container/unordered_map.h>
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
    bool        ok{false};
    T           value{};
    std::string error{};

    [[nodiscard]] bool
    is_ok() const noexcept {
        return ok;
    }
    [[nodiscard]] const T &
    result() const & {
        return value;
    }
    [[nodiscard]] T &
    result() & {
        return value;
    }
    [[nodiscard]] T &&
    result() && {
        return std::move(value);
    }
    [[nodiscard]] const std::string &
    error_message() const {
        return error;
    }
};

/** @brief Specialization for void (commands that don't return data) */
template <>
struct ServerReply<void> {
    bool        ok{false};
    std::string error{};

    [[nodiscard]] bool
    is_ok() const noexcept {
        return ok;
    }
    [[nodiscard]] const std::string &
    error_message() const {
        return error;
    }
};

// ============================================================================
// Value Extractors - Modern C++20/23 approach
// ============================================================================

/**
 * @class ValueExtractor
 * @brief Safe extraction of typed values from parser::Value
 *
 * Provides optional-based accessors for string, integer, double, bool,
 * array, map, set, and error message.
 */
class ValueExtractor {
    const parser::Value *_value;

public:
    /**
     * @brief Construct from a parsed value reference.
     * @param v The value to inspect; must outlive the extractor.
     */
    explicit ValueExtractor(const parser::Value &v)
        : _value(&v) {}
    /**
     * @brief Construct from an owning pointer to a parsed value.
     * @param v The value to inspect; the pointed-to value must outlive the
     *          extractor. A null pointer yields an extractor that reports null.
     */
    explicit ValueExtractor(const std::unique_ptr<parser::Value> &v)
        : _value(v.get()) {}

    /**
     * @brief Extract the value as a string view, without copying.
     * @return The string view, or @c std::nullopt if the value is absent or not
     *         a string.
     */
    [[nodiscard]] std::optional<std::string_view>
    as_string_view() const noexcept {
        if (!_value || !_value->is_string())
            return std::nullopt;
        return _value->as_string_view();
    }

    /**
     * @brief Extract the value as an owned string.
     * @return The string copy, or @c std::nullopt if the value is absent or not
     *         a string.
     */
    [[nodiscard]] std::optional<std::string>
    as_string() const {
        auto sv = as_string_view();
        if (!sv)
            return std::nullopt;
        return std::string(*sv);
    }

    /**
     * @brief Extract the value as a 64-bit integer.
     * @return The integer, or @c std::nullopt if the value is absent or not an
     *         integer.
     */
    [[nodiscard]] std::optional<int64_t>
    as_integer() const noexcept {
        if (!_value || !_value->is_integer())
            return std::nullopt;
        return _value->as_integer().value;
    }

    /**
     * @brief Extract the value as a double.
     * @return The double; integer values are widened to double. Returns
     *         @c std::nullopt if the value is absent or neither a double nor an
     *         integer.
     */
    [[nodiscard]] std::optional<double>
    as_double() const noexcept {
        if (!_value)
            return std::nullopt;
        if (_value->is_double())
            return _value->as_double().value;
        if (_value->is_integer())
            return static_cast<double>(_value->as_integer().value);
        return std::nullopt;
    }

    /**
     * @brief Extract the value as a boolean.
     * @return The boolean, or @c std::nullopt if the value is absent or not a
     *         boolean.
     */
    [[nodiscard]] std::optional<bool>
    as_bool() const noexcept {
        if (!_value || !_value->is_boolean())
            return std::nullopt;
        return _value->as_boolean().value;
    }

    /**
     * @brief Test whether the value is absent or a Redis null.
     * @return @c true if there is no underlying value or it is null.
     */
    [[nodiscard]] bool
    is_null() const noexcept {
        return !_value || _value->is_null();
    }

    /**
     * @brief Borrow the value as an array for iteration.
     * @return A reference wrapper to the underlying array, or @c std::nullopt if
     *         the value is absent or not an array.
     */
    [[nodiscard]] std::optional<std::reference_wrapper<const parser::Array>>
    as_array() const noexcept {
        if (!_value || !_value->is_array())
            return std::nullopt;
        return std::cref(_value->as_array());
    }

    /**
     * @brief Borrow the value as a map for iteration.
     * @return A reference wrapper to the underlying map, or @c std::nullopt if
     *         the value is absent or not a map.
     */
    [[nodiscard]] std::optional<std::reference_wrapper<const parser::Map>>
    as_map() const noexcept {
        if (!_value || !_value->is_map())
            return std::nullopt;
        return std::cref(_value->as_map());
    }

    /**
     * @brief Borrow the value as a set for iteration.
     * @return A reference wrapper to the underlying set, or @c std::nullopt if
     *         the value is absent or not a set.
     */
    [[nodiscard]] std::optional<std::reference_wrapper<const parser::Set>>
    as_set() const noexcept {
        if (!_value || !_value->is_set())
            return std::nullopt;
        return std::cref(_value->as_set());
    }

    /**
     * @brief Test whether the value is a Redis error reply.
     * @return @c true if there is an underlying value and it is an error.
     */
    [[nodiscard]] bool
    is_error() const noexcept {
        return _value && _value->is_error();
    }

    /**
     * @brief Get the error message carried by the value.
     * @return The error message, or "no value" if there is no underlying value.
     */
    [[nodiscard]] std::string
    get_error_message() const {
        if (!_value)
            return "no value";
        return _value->get_error_message();
    }

    /**
     * @brief Access the underlying parsed value.
     * @return A pointer to the value, or @c nullptr if none.
     */
    [[nodiscard]] const parser::Value *
    raw() const noexcept {
        return _value;
    }
};

// ============================================================================
// Convenience helpers for common Redis patterns
// ============================================================================

/**
 * @brief Extract a non-null string from a parsed value.
 * @param value The value to inspect.
 * @return The string on success, or an error message if the value is null or
 *         not a string.
 */
[[nodiscard]] expected<std::string, std::string> extract_string(const parser::Value &value);

/**
 * @brief Extract a non-null integer from a parsed value.
 * @param value The value to inspect.
 * @return The integer on success, or an error message if the value is null or
 *         not an integer.
 */
[[nodiscard]] expected<int64_t, std::string> extract_integer(const parser::Value &value);

/**
 * @brief Extract an array of strings from a parsed value.
 * @param value The value to inspect.
 * @return The vector of strings on success (empty for a null value), or an error
 *         message if the value is not an array or contains a non-string element.
 */
[[nodiscard]] expected<std::vector<std::string>, std::string> extract_string_array(const parser::Value &value);

/**
 * @brief Extract a string-to-string map from a parsed value.
 * @param value The value to inspect.
 * @return The map on success (empty for a null value), or an error message if
 *         the value is not a map or contains a non-string key or value.
 */
[[nodiscard]] expected<qb::unordered_map<std::string, std::string>, std::string> extract_string_map(const parser::Value &value);

// ============================================================================
// Async result wrapper for coroutines
// ============================================================================

/**
 * @class AsyncResult
 * @brief Coroutine-friendly result wrapper using expected
 * @tparam T The value type on success
 */
template <typename T>
class AsyncResult {
    expected<T, std::string> _result;

public:
    AsyncResult() = default;
    explicit AsyncResult(T &&v)
        : _result(std::move(v)) {}
    explicit AsyncResult(std::string &&e)
        : _result(unexpected(std::move(e))) {}

    [[nodiscard]] bool
    is_ok() const noexcept {
        return _result.has_value();
    }
    [[nodiscard]] bool
    has_error() const noexcept {
        return !_result.has_value();
    }

    [[nodiscard]] const T &
    value() const & {
        return _result.value();
    }
    [[nodiscard]] T &
    value() & {
        return _result.value();
    }
    [[nodiscard]] T &&
    value() && {
        return std::move(_result.value());
    }

    [[nodiscard]] const std::string &
    error() const {
        return _result.error();
    }

    // Conversions
    [[nodiscard]]
    operator bool() const noexcept {
        return is_ok();
    }
    [[nodiscard]] const T *
    operator->() const {
        return &_result.value();
    }
    [[nodiscard]] T *
    operator->() {
        return &_result.value();
    }
};

// Specialization for void
template <>
class AsyncResult<void> {
    std::optional<std::string> _error;

public:
    AsyncResult() = default;
    explicit AsyncResult(std::string &&e)
        : _error(std::move(e)) {}

    [[nodiscard]] bool
    is_ok() const noexcept {
        return !_error.has_value();
    }
    [[nodiscard]] bool
    has_error() const noexcept {
        return _error.has_value();
    }
    [[nodiscard]] const std::string &
    error() const {
        return _error.value();
    }

    [[nodiscard]]
    operator bool() const noexcept {
        return is_ok();
    }
};

// ============================================================================
// Stream ID helpers
// ============================================================================

/**
 * @brief Parse a Redis stream id ("<timestamp>-<sequence>") from a parsed value.
 * @param value The value to inspect.
 * @return The parsed stream id on success, or an error message if the value is
 *         not a string, lacks the '-' separator, or has non-numeric components.
 */
[[nodiscard]] expected<stream_id, std::string> extract_stream_id(const parser::Value &value);

// ============================================================================
// Score member helpers (for sorted sets)
// ============================================================================

/**
 * @brief Extract a sorted-set score/member pair from an array.
 *
 * Reads the member (string) at @p index and its score (double or integer) at
 * @p index + 1.
 *
 * @param arr   The array holding the flattened member/score elements.
 * @param index The position of the member element.
 * @return The score/member pair on success, or an error message if there are not
 *         enough elements, the member is not a string, or the score is null or
 *         not a number.
 */
[[nodiscard]] expected<score_member, std::string> extract_score_member(const parser::Array &arr, size_t index);

} // namespace qb::redis

#endif // QBM_REDIS_SERVER_REPLY_H
