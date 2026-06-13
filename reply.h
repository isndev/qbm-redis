/**
 * @file reply.h
 * @brief Reply parsing, Reply<T> wrapper, and command serialization
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

#ifndef QBM_REDIS_REPLY_H
#define QBM_REDIS_REPLY_H

#include <cassert>
#include <string>
#include <string_view>
#include <optional>
#include <memory>
#include <utility>
#include <vector>
#include <chrono>
#include <concepts>
#include <set>
#include <unordered_set>
#include <charconv>
#include <limits>
#include <qb/utility/type_traits.h>
#include <qb/system/allocator/pipe.h>
#include <qb/system/container/unordered_map.h>
#include <qb/json.h>

// Include types first (which includes parser)
#include "types.h"

namespace qb::redis {

// ============================================================================
// Native Reply type - direct use of parser::Value
// ============================================================================

/** @brief Alias for parser::Value - the native Redis reply type */
using ReplyValue = parser::Value;

// ============================================================================
// Error types
// ============================================================================

/**
 * @brief Types of errors that can occur in Redis replies
 */
enum class ReplyErrorType { ERR, MOVED, ASK };

/**
 * @class Error
 * @brief Base exception class for Redis errors
 */
class Error : public std::exception {
public:
    explicit Error(std::string msg)
        : _msg(std::move(msg)) {}

    Error(const Error &)            = default;
    Error &operator=(const Error &) = default;
    Error(Error &&)            = default;
    Error &operator=(Error &&) = default;
    ~Error() override = default;

    [[nodiscard]] const char *
    what() const noexcept override {
        return _msg.data();
    }

private:
    std::string _msg;
};

/**
 * @class ProtoError
 * @brief Exception class for protocol-related errors
 */
class ProtoError : public Error {
public:
    explicit ProtoError(const std::string &msg)
        : Error(msg) {}

    ProtoError(const ProtoError &)            = default;
    ProtoError &operator=(const ProtoError &) = default;
    ProtoError(ProtoError &&)            = default;
    ProtoError &operator=(ProtoError &&) = default;
    ~ProtoError() override = default;
};

/**
 * @class ConnectionError
 * @brief Thrown when a TCP connection to Redis cannot be established.
 */
class ConnectionError : public Error {
public:
    explicit ConnectionError(std::string msg)
        : Error(std::move(msg)) {}

    ConnectionError(const ConnectionError &)            = default;
    ConnectionError &operator=(const ConnectionError &) = default;
    ConnectionError(ConnectionError &&)                 = default;
    ConnectionError &operator=(ConnectionError &&)      = default;
    ~ConnectionError() override                         = default;
};

/**
 * @class AuthError
 * @brief Thrown when Redis rejects authentication (NOAUTH / WRONGPASS).
 */
class AuthError : public Error {
public:
    explicit AuthError(std::string msg)
        : Error(std::move(msg)) {}

    AuthError(const AuthError &)            = default;
    AuthError &operator=(const AuthError &) = default;
    AuthError(AuthError &&)                 = default;
    AuthError &operator=(AuthError &&)      = default;
    ~AuthError() override                   = default;
};

/**
 * @class CommandError
 * @brief Thrown when Redis returns an ERR reply for a command.
 */
class CommandError : public Error {
public:
    explicit CommandError(std::string msg)
        : Error(std::move(msg)) {}

    CommandError(const CommandError &)            = default;
    CommandError &operator=(const CommandError &) = default;
    CommandError(CommandError &&)                 = default;
    CommandError &operator=(CommandError &&)      = default;
    ~CommandError() override                      = default;
};

/**
 * @class TimeoutError
 * @brief Thrown when a connection attempt or command exceeds its deadline.
 */
class TimeoutError : public Error {
public:
    explicit TimeoutError(std::string msg)
        : Error(std::move(msg)) {}

    TimeoutError(const TimeoutError &)            = default;
    TimeoutError &operator=(const TimeoutError &) = default;
    TimeoutError(TimeoutError &&)                 = default;
    TimeoutError &operator=(TimeoutError &&)      = default;
    ~TimeoutError() override                      = default;
};

/**
 * @class ReplyParseError
 * @brief Exception class for Redis reply parsing errors
 */
class ReplyParseError : public ProtoError {
public:
    ReplyParseError(const std::string &expect_type, const ReplyValue &reply)
        : ProtoError(_err_info(expect_type, reply)) {}

    ReplyParseError(const ReplyParseError &)            = default;
    ReplyParseError &operator=(const ReplyParseError &) = default;
    ReplyParseError(ReplyParseError &&)            = default;
    ReplyParseError &operator=(ReplyParseError &&) = default;
    ~ReplyParseError() override = default;

private:
    [[nodiscard]] static std::string _err_info(const std::string &type,
                                               const ReplyValue &reply);
};

// ============================================================================
// Reply parsing namespace
// ============================================================================

/**
 * @namespace reply
 * @brief Reply parsing utilities for converting parser::Value to C++ types
 */
namespace reply {

/** @brief Tag type for parse() overload resolution */
template <typename T>
struct ParseTag {};

// ============================================================================
// Type checking - native C++23 implementations
// ============================================================================

[[nodiscard]] inline bool is_string(const ReplyValue &reply) noexcept {
    // is_string() on Value already covers SimpleString, BulkString, VerbatimString
    return reply.is_string();
}

[[nodiscard]] inline bool is_error(const ReplyValue &reply) noexcept {
    return reply.is_simple_error() || reply.is_error();
}

[[nodiscard]] inline bool is_integer(const ReplyValue &reply) noexcept {
    return reply.is_integer();
}

[[nodiscard]] inline bool is_nil(const ReplyValue &reply) noexcept {
    return reply.is_null();
}

[[nodiscard]] inline bool is_array(const ReplyValue &reply) noexcept {
    return reply.is_array();
}

[[nodiscard]] inline bool is_double(const ReplyValue &reply) noexcept {
    return reply.is_double();
}

[[nodiscard]] inline bool is_bool(const ReplyValue &reply) noexcept {
    return reply.is_boolean();
}

[[nodiscard]] inline bool is_map(const ReplyValue &reply) noexcept {
    return reply.is_map();
}

[[nodiscard]] inline bool is_set(const ReplyValue &reply) noexcept {
    return reply.is_set();
}

[[nodiscard]] inline bool is_push(const ReplyValue &reply) noexcept {
    return reply.is_push();
}

/** Array or Push - pub/sub uses Array in RESP2, Push in RESP3 */
[[nodiscard]] inline bool is_array_or_push(const ReplyValue &reply) noexcept {
    return reply.is_array() || reply.is_push();
}

[[nodiscard]] inline const parser::Value *get_pubsub_element(const ReplyValue &reply, size_t i) {
    if (reply.is_array()) {
        const auto &arr = reply.as_array();
        return (i < arr.size()) ? arr[i] : nullptr;
    }
    if (reply.is_push()) {
        const auto &push = reply.as_push();
        return (i < push.size()) ? push.elements[i].get() : nullptr;
    }
    return nullptr;
}

[[nodiscard]] inline size_t get_pubsub_size(const ReplyValue &reply) {
    if (reply.is_array()) return reply.as_array().size();
    if (reply.is_push()) return reply.as_push().size();
    return 0;
}

[[nodiscard]] inline bool is_bignum(const ReplyValue &reply) noexcept {
    return reply.is_big_number();
}

[[nodiscard]] inline bool is_status(const ReplyValue &reply) noexcept {
    // A Redis "status" reply is specifically the simple string type (+OK\r\n)
    return reply.is_simple_string();
}

// ============================================================================
// Main parse function template
// ============================================================================

template <typename T>
[[nodiscard]] inline T parse(const ReplyValue &reply) {
    return parse(ParseTag<T>{}, reply);
}

// ============================================================================
// Primitive type parsers
// ============================================================================

[[nodiscard]] inline std::string_view parse(ParseTag<std::string_view>, const ReplyValue &reply) {
    if (!is_string(reply) && !is_status(reply)) {
        throw ReplyParseError("STRING or STATUS", reply);
    }
    return reply.as_string_view();
}

[[nodiscard]] inline std::string parse(ParseTag<std::string>, const ReplyValue &reply) {
    if (is_integer(reply)) {
        return std::to_string(reply.as_integer().value);
    }
    // Commands like GEORADIUS with WITHDIST/WITHCOORD return nested arrays
    // [name, distance, ...] per element. Extract the first string sub-element.
    if (is_array(reply) && !reply.as_array().empty() && reply.as_array()[0]->is_string()) {
        return std::string(reply.as_array()[0]->as_string_view());
    }
    auto sv = parse(ParseTag<std::string_view>{}, reply);
    return std::string(sv);
}

[[nodiscard]] inline long long parse(ParseTag<long long>, const ReplyValue &reply) {
    if (!is_integer(reply)) {
        throw ReplyParseError("INTEGER", reply);
    }
    return reply.as_integer().value;
}

[[nodiscard]] inline double parse(ParseTag<double>, const ReplyValue &reply) {
    if (is_double(reply)) {
        return reply.as_double().value;
    }
    if (is_integer(reply)) {
        return static_cast<double>(reply.as_integer().value);
    }
    if (is_string(reply) || is_status(reply)) {
        const auto sv = reply.as_string_view();
        // Redis can return "inf" / "+inf" / "-inf" for sorted-set scores.
        if (sv == "inf"  || sv == "+inf") return  std::numeric_limits<double>::infinity();
        if (sv == "-inf")                 return -std::numeric_limits<double>::infinity();
        // Use std::from_chars: locale-independent, no exception overhead.
        // Require the WHOLE string to be consumed (ptr == end): otherwise a reply like
        // "1.5junk" would silently yield 1.5 instead of being rejected. This matches the
        // SCAN-cursor parse and the RESP parser's own parse_double.
        double value{};
        const auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), value);
        if (ec == std::errc{} && ptr == sv.data() + sv.size()) return value;
    }
    throw ProtoError("not a double reply");
}

[[nodiscard]] inline bool parse(ParseTag<bool>, const ReplyValue &reply) {
    if (is_nil(reply)) {
        return false;
    }
    if (is_bool(reply)) {
        // RESP3 native boolean - must use as_boolean(), NOT as_integer()
        return reply.as_boolean().value;
    }
    if (is_integer(reply)) {
        return reply.as_integer().value != 0;
    }
    throw ProtoError("BOOL or INTEGER required");
}

[[nodiscard]] inline qb::redis::status parse(ParseTag<qb::redis::status>, const ReplyValue &reply) {
    if (is_nil(reply)) {
        return qb::redis::status{};
    }
    if (is_string(reply)) {
        return qb::redis::status(std::string(reply.as_string_view()));
    }
    if (is_error(reply)) {
        return qb::redis::status(std::string(reply.get_error_message()));
    }
    throw ProtoError("STRING or ERROR required for status");
}

// ============================================================================
// Complex type parsers
// ============================================================================

[[nodiscard]] qb::redis::message parse(ParseTag<qb::redis::message>, const ReplyValue &reply);
[[nodiscard]] qb::redis::pmessage parse(ParseTag<qb::redis::pmessage>, const ReplyValue &reply);
[[nodiscard]] qb::redis::subscription parse(ParseTag<qb::redis::subscription>, const ReplyValue &reply);
[[nodiscard]] std::vector<char> parse(ParseTag<std::vector<char>>, const ReplyValue &reply);
[[nodiscard]] std::chrono::milliseconds parse(ParseTag<std::chrono::milliseconds>, const ReplyValue &reply);
[[nodiscard]] std::chrono::seconds parse(ParseTag<std::chrono::seconds>, const ReplyValue &reply);
[[nodiscard]] qb::redis::geo_pos parse(ParseTag<qb::redis::geo_pos>, const ReplyValue &reply);
[[nodiscard]] qb::redis::stream_id parse(ParseTag<qb::redis::stream_id>, const ReplyValue &reply);
[[nodiscard]] qb::redis::stream_entry parse(ParseTag<qb::redis::stream_entry>, const ReplyValue &reply);
[[nodiscard]] stream_entry_list parse(ParseTag<stream_entry_list>, const ReplyValue &reply);
[[nodiscard]] map_stream_entry_list parse(ParseTag<map_stream_entry_list>, const ReplyValue &reply);
[[nodiscard]] qb::redis::score parse(ParseTag<qb::redis::score>, const ReplyValue &reply);
[[nodiscard]] qb::redis::score_member parse(ParseTag<qb::redis::score_member>, const ReplyValue &reply);
[[nodiscard]] std::vector<qb::redis::score_member> parse(ParseTag<std::vector<qb::redis::score_member>>, const ReplyValue &reply);
[[nodiscard]] qb::redis::search_result parse(ParseTag<qb::redis::search_result>, const ReplyValue &reply);
[[nodiscard]] qb::redis::cluster_node parse(ParseTag<qb::redis::cluster_node>, const ReplyValue &reply);
[[nodiscard]] qb::redis::memory_info parse(ParseTag<qb::redis::memory_info>, const ReplyValue &reply);
[[nodiscard]] qb::redis::pipeline_result parse(ParseTag<qb::redis::pipeline_result>, const ReplyValue &reply);
[[nodiscard]] qb::redis::json_value parse(ParseTag<qb::redis::json_value>, const ReplyValue &reply);
[[nodiscard]] qb::json             parse(ParseTag<qb::json>,             const ReplyValue &reply);

// ============================================================================
// Generic container parsers
// ============================================================================

template <typename T>
[[nodiscard]] std::optional<T> parse(ParseTag<std::optional<T>>, const ReplyValue &reply) {
    if (is_nil(reply)) {
        return std::nullopt;
    }
    return std::optional<T>(parse<T>(reply));
}

template <typename T, typename U>
[[nodiscard]] std::pair<T, U> parse(ParseTag<std::pair<T, U>>, const ReplyValue &reply) {
    if (!is_array(reply)) {
        throw ReplyParseError("ARRAY", reply);
    }
    
    const auto &arr = reply.as_array();
    if (arr.size() < 2) {
        throw ProtoError("PAIR needs 2 elements");
    }
    
    return std::make_pair(
        parse<std::remove_cvref_t<T>>(*arr[0]),
        parse<std::remove_cvref_t<U>>(*arr[1])
    );
}

template <typename... Args>
[[nodiscard]] std::tuple<Args...> parse(ParseTag<std::tuple<Args...>>, const ReplyValue &reply) {
    constexpr auto size = sizeof...(Args);
    static_assert(size > 0, "Empty tuple not supported");
    
    if (!is_array(reply)) {
        throw ReplyParseError("ARRAY", reply);
    }
    
    const auto &arr = reply.as_array();
    if (arr.size() < size) {
        throw ProtoError("Tuple size mismatch");
    }
    
    return parse_tuple<Args...>(arr, std::make_index_sequence<size>{});
}

template <typename... Args, size_t... Is>
[[nodiscard]] std::tuple<Args...> parse_tuple(const parser::Array &arr,
                                               std::index_sequence<Is...>) {
    return std::make_tuple(parse<Args>(*arr[Is])...);
}

// ============================================================================
// Sequence container parsers
// ============================================================================

template <typename T>
    requires is_sequence_container<T>::value
[[nodiscard]] T parse(ParseTag<T>, const ReplyValue &reply) {
    if (!is_array(reply) && !is_set(reply)) {
        throw ReplyParseError("ARRAY or SET", reply);
    }
    
    T container;
    if (is_array(reply)) {
        for (const auto &elem : reply.as_array()) {
            container.push_back(parse<typename T::value_type>(*elem));
        }
    } else {
        for (const auto &elem : reply.as_set()) {
            container.push_back(parse<typename T::value_type>(*elem));
        }
    }
    return container;
}

// ============================================================================
// Associative container parsers - Maps (have mapped_type)
// ============================================================================

template <typename T>
    requires is_associative_container<T>::value && requires { typename T::mapped_type; }
[[nodiscard]] T parse(ParseTag<T>, const ReplyValue &reply) {
    if (!is_array(reply) && !is_map(reply)) {
        throw ReplyParseError("ARRAY or MAP", reply);
    }
    
    T container;
    if (is_map(reply)) {
        // Map type - direct key-value pairs
        for (const auto &entry : reply.as_map()) {
            auto k = parse<typename T::key_type>(*entry.first);
            auto v = parse<typename T::mapped_type>(*entry.second);
            container.emplace(std::move(k), std::move(v));
        }
    } else {
        // Array type - flat key-value pairs
        const auto &arr = reply.as_array();
        for (size_t i = 0; i + 1 < arr.size(); i += 2) {
            auto k = parse<typename T::key_type>(*arr[i]);
            auto v = parse<typename T::mapped_type>(*arr[i + 1]);
            container.emplace(std::move(k), std::move(v));
        }
    }
    return container;
}

// ============================================================================
// Associative container parsers - Sets (no mapped_type)
// ============================================================================

template <typename T>
    requires is_associative_container<T>::value && (!requires { typename T::mapped_type; })
[[nodiscard]] T parse(ParseTag<T>, const ReplyValue &reply) {
    if (!is_array(reply) && !is_set(reply)) {
        throw ReplyParseError("ARRAY or SET", reply);
    }
    
    T container;
    if (is_set(reply)) {
        // Set type from RESP3 set
        for (const auto &elem : reply.as_set()) {
            container.emplace(parse<typename T::key_type>(*elem));
        }
    } else {
        // Array type - flat list of elements
        for (const auto &elem : reply.as_array()) {
            container.emplace(parse<typename T::key_type>(*elem));
        }
    }
    return container;
}

// ============================================================================
// Scan result parser
// ============================================================================

template <typename Output>
[[nodiscard]] std::size_t parse_scan_reply(const ReplyValue &reply, Output output) {
    if (!is_array(reply)) {
        throw ProtoError("Invalid scan reply");
    }
    const auto& arr = reply.as_array();
    if (arr.size() < 2) {
        throw ProtoError("Invalid scan reply");
    }

    // SCAN cursors are unsigned 64-bit: reverse-binary bucket indices that can
    // legitimately have the high bit set. std::stoll would throw out_of_range
    // on any cursor > INT64_MAX, spuriously failing the scan; parse the full
    // unsigned range with from_chars (no exceptions, no locale).
    auto cursor_str = parse<std::string>(*arr[0]);
    unsigned long long new_cursor = 0;
    {
        const char *b = cursor_str.data();
        const char *e = b + cursor_str.size();
        auto [ptr, ec] = std::from_chars(b, e, new_cursor);
        if (ec != std::errc{} || ptr != e) {
            throw ProtoError("Invalid cursor");
        }
    }
    
    if (is_array(*arr[1])) {
        const auto &inner = arr[1]->as_array();
        if constexpr (is_map_iterator<Output>::value) {
            // Map-type output: Redis sends a flat [key, val, key, val, ...] array.
            // Consume two elements at a time and build std::pair objects.
            using PairType = typename iterator_type<Output>::type;
            using K = std::remove_cvref_t<typename PairType::first_type>;
            using V = std::remove_cvref_t<typename PairType::second_type>;
            for (size_t i = 0; i + 1 < inner.size(); i += 2) {
                *output = std::make_pair(
                    parse<K>(*inner[i]),
                    parse<V>(*inner[i + 1])
                );
                ++output;
            }
        } else {
            for (const auto &elem : inner) {
                *output = parse<typename iterator_type<Output>::type>(*elem);
                ++output;
            }
        }
    }

    return new_cursor;
}

template <typename Out>
[[nodiscard]] scan<Out> parse(ParseTag<scan<Out>>, const ReplyValue &reply) {
    scan<Out> sc;
    if constexpr (is_mappish<Out>::value) {
        sc.cursor = parse_scan_reply(reply, std::inserter(sc.items, sc.items.end()));
    } else {
        sc.cursor = parse_scan_reply(reply, std::back_inserter(sc.items));
    }
    return sc;
}

// ============================================================================
// Type to string conversion
// ============================================================================

[[nodiscard]] inline std::string type_to_string(const parser::Value& value) {
    using namespace parser;
    if (value.is_simple_string()) return "SIMPLE_STRING";
    if (value.is_simple_error()) return "SIMPLE_ERROR";
    if (value.is_bulk_string()) return "BULK_STRING";
    if (value.is_error()) return "BULK_ERROR";
    if (value.is_integer()) return "INTEGER";
    if (value.is_double()) return "DOUBLE";
    if (value.is_boolean()) return "BOOLEAN";
    if (value.is_null()) return "NULL";
    if (value.is_array()) return "ARRAY";
    if (value.is_map()) return "MAP";
    if (value.is_set()) return "SET";
    if (value.is_push()) return "PUSH";
    if (value.is_attribute()) return "ATTRIBUTE";
    if (value.is_string()) return "VERBATIM_STRING";
    if (value.is_big_number()) return "BIG_NUMBER";
    return "UNKNOWN";
}

// ============================================================================
// Status extraction
// ============================================================================

[[nodiscard]] inline status to_status(const ReplyValue &reply) {
    if (!is_status(reply)) {
        throw ReplyParseError("STATUS", reply);
    }
    return status(std::string(reply.as_string_view()));
}

// ============================================================================
// Set reply parser (returns true for "OK")
// ============================================================================

[[nodiscard]] inline bool parse_set_reply(const ReplyValue &reply) {
    if (is_string(reply) || is_status(reply)) {
        auto sv = reply.as_string_view();
        return sv == "OK";
    }
    return false;
}

// ============================================================================
// Array conversion helpers
// ============================================================================

// Forward declaration
[[nodiscard]] inline bool is_flat_array(const ReplyValue &reply);

template <typename Output>
void to_flat_array(const ReplyValue &reply, Output output) {
    const auto &arr = reply.as_array();
    if (arr.size() % 2 != 0) {
        throw ProtoError("Flat array needs even number of elements");
    }
    
    using Pair = typename iterator_type<Output>::type;
    using FirstType = std::remove_cvref_t<typename Pair::first_type>;
    using SecondType = std::remove_cvref_t<typename Pair::second_type>;
    
    for (size_t i = 0; i < arr.size(); i += 2) {
        *output = std::make_pair(
            parse<FirstType>(*arr[i]),
            parse<SecondType>(*arr[i + 1])
        );
        ++output;
    }
}

template <typename Output>
void to_array(const ReplyValue &reply, Output output) {
    if (!is_array(reply) && !is_map(reply) && !is_set(reply)) {
        throw ReplyParseError("ARRAY, MAP, or SET", reply);
    }
    
    if (is_map(reply) || (is_array(reply) && is_flat_array(reply))) {
        to_flat_array(reply, output);
    } else {
        for (const auto &elem : reply.as_array()) {
            *output = parse<typename iterator_type<Output>::type>(*elem);
            ++output;
        }
    }
}

[[nodiscard]] inline bool is_flat_array(const ReplyValue &reply) {
    const auto& arr = reply.as_array();
    if (!is_array(reply) || arr.empty()) {
        return false;
    }
    // Check if first element is not an array/map (flat)
    const auto &first = *arr[0];
    return !first.is_aggregate();
}

} // namespace reply

// ============================================================================
// Top-level parse function
// ============================================================================

template <typename T>
[[nodiscard]] inline T parse(const ReplyValue &reply) {
    return reply::parse<T>(reply);
}

// ============================================================================
// Redis argument counting and serialization
// ============================================================================

// Count elements for Redis protocol
template <size_t N>
[[nodiscard]] constexpr std::size_t redis_count(const char (&)[N]) noexcept {
    return 1;
}

[[nodiscard]] constexpr std::size_t redis_count(const char *) noexcept {
    return 1;
}

[[nodiscard]] constexpr std::size_t redis_count(std::string_view) noexcept {
    return 1;
}

[[nodiscard]] constexpr std::size_t redis_count(std::string const &) noexcept {
    return 1;
}

template <typename T>
    requires std::is_arithmetic_v<T>
[[nodiscard]] constexpr std::size_t redis_count(T const &) noexcept {
    return 1;
}

template <typename... Args>
[[nodiscard]] constexpr std::size_t redis_count(std::tuple<Args...> const &) noexcept {
    return sizeof...(Args);
}

template <typename... Args>
[[nodiscard]] constexpr std::size_t redis_count(std::pair<Args...> const &) noexcept {
    return sizeof...(Args);
}

template <typename T>
[[nodiscard]] std::size_t redis_count(std::optional<T> const &opt) {
    return opt ? redis_count(opt.value()) : 0;
}

template <typename T>
    requires qb::is_container<T>::value
[[nodiscard]] std::size_t redis_count(T const &cnt) {
    return cnt.empty() ? 0 : redis_count(*cnt.begin()) * cnt.size();
}

[[nodiscard]] inline std::size_t redis_count(qb::json const &json) {
    if (json.is_null()) return 1;
    if (json.is_boolean() || json.is_number() || json.is_string()) return 1;
    
    if (json.is_array()) {
        std::size_t count = 0;
        for (const auto &item : json) {
            count += redis_count(item);
        }
        return count;
    }
    
    if (json.is_object()) {
        std::size_t count = 0;
        for (auto it = json.begin(); it != json.end(); ++it) {
            count += 1;  // Key
            count += redis_count(it.value());
        }
        return count;
    }
    
    return 0;
}

// ============================================================================
// Security
// ============================================================================

constexpr std::size_t REDIS_MAX_STRING_SIZE = 512 * 1024 * 1024;  // 512MB

class SecurityError : public Error {
public:
    explicit SecurityError(const std::string &msg) : Error(msg) {}
};

[[nodiscard]] inline bool is_valid_redis_string_size(const std::string &s) {
    return s.size() <= REDIS_MAX_STRING_SIZE;
}

// ============================================================================
// to_redis_string serialization
// ============================================================================

template <size_t N>
inline bool to_redis_string(qb::allocator::pipe<char> &pipe, const char (&str)[N]) {
    pipe << '$' << (N-1) << "\r\n" << str << "\r\n";
    return true;
}

inline bool to_redis_string(qb::allocator::pipe<char> &pipe, const char* str) {
    if (!str) str = "";
    size_t len = std::strlen(str);
    
    if (len > REDIS_MAX_STRING_SIZE) {
        throw SecurityError("String too large");
    }
    
    pipe << '$' << len << "\r\n" << str << "\r\n";
    return true;
}

inline bool to_redis_string(qb::allocator::pipe<char> &pipe, std::string_view sv) {
    if (sv.size() > REDIS_MAX_STRING_SIZE) {
        throw SecurityError("String too large");
    }
    pipe << '$' << sv.size() << "\r\n" << sv << "\r\n";
    return true;
}

inline bool to_redis_string(qb::allocator::pipe<char> &pipe, std::string const &val) {
    return to_redis_string(pipe, std::string_view{val});
}

template <typename T>
    requires std::is_arithmetic_v<T>
inline bool to_redis_string(qb::allocator::pipe<char> &pipe, T const &val) {
    return to_redis_string(pipe, std::to_string(val));
}

template <typename T>
bool to_redis_string(qb::allocator::pipe<char> &pipe, std::optional<T> const &opt) {
    if (opt) {
        to_redis_string(pipe, opt.value());
    }
    return true;
}

template <typename Tuple, std::size_t... N>
bool put_tuple(qb::allocator::pipe<char> &pipe, Tuple const &t, std::index_sequence<N...>) {
    return (to_redis_string(pipe, std::get<N>(t)) && ...);
}

template <typename... Args>
bool to_redis_string(qb::allocator::pipe<char> &pipe, std::tuple<Args...> const &t) {
    return put_tuple(pipe, t, std::index_sequence_for<Args...>{});
}

template <typename... Args>
bool to_redis_string(qb::allocator::pipe<char> &pipe, std::pair<Args...> const &p) {
    to_redis_string(pipe, p.first);
    to_redis_string(pipe, p.second);
    return true;
}

template <typename T>
    requires qb::is_container<T>::value
bool to_redis_string(qb::allocator::pipe<char> &pipe, T const &cnt) {
    if constexpr (is_map_iterator<decltype(cnt.begin())>::value) {
        for (const auto &[k, v] : cnt) {
            to_redis_string(pipe, k);
            to_redis_string(pipe, v);
        }
    } else {
        for (const auto &el : cnt) {
            to_redis_string(pipe, el);
        }
    }
    return true;
}

inline bool to_redis_string(qb::allocator::pipe<char> &pipe, std::vector<char> const &val) {
    pipe << '$' << val.size() << "\r\n";
    pipe.write(val.data(), val.size());
    pipe << "\r\n";
    return true;
}

inline bool to_redis_string(qb::allocator::pipe<char> &pipe, std::chrono::milliseconds const &val) {
    return to_redis_string(pipe, std::to_string(val.count()));
}

inline bool to_redis_string(qb::allocator::pipe<char> &pipe, std::chrono::seconds const &val) {
    return to_redis_string(pipe, std::to_string(val.count()));
}

inline bool to_redis_string(qb::allocator::pipe<char> &pipe, qb::redis::geo_pos const &pos) {
    to_redis_string(pipe, pos.longitude);
    to_redis_string(pipe, pos.latitude);
    return true;
}

inline bool to_redis_string(qb::allocator::pipe<char> &pipe, qb::redis::stream_id const &id) {
    return to_redis_string(pipe, id.to_string());
}

inline bool to_redis_string(qb::allocator::pipe<char> &pipe, qb::redis::score const &score) {
    return to_redis_string(pipe, score.value);
}

inline bool to_redis_string(qb::allocator::pipe<char> &pipe, qb::redis::score_member const &sm) {
    to_redis_string(pipe, sm.score);
    to_redis_string(pipe, sm.member);
    return true;
}

inline bool to_redis_string(qb::allocator::pipe<char> &pipe, qb::redis::search_result const &sr) {
    to_redis_string(pipe, sr.key);
    for (const auto &field : sr.fields) {
        to_redis_string(pipe, field);
    }
    for (const auto &value : sr.values) {
        to_redis_string(pipe, value);
    }
    return true;
}

inline bool to_redis_string(qb::allocator::pipe<char> &pipe, qb::redis::cluster_node const &node) {
    to_redis_string(pipe, node.id);
    return true;
}

inline bool to_redis_string(qb::allocator::pipe<char> &pipe, qb::redis::json_value const &json) {
    using Type = qb::redis::json_value::Type;
    
    switch (json.type) {
        case Type::Null:
            to_redis_string(pipe, "null");
            break;
        case Type::Boolean:
            to_redis_string(pipe, std::get<bool>(json.data) ? "true" : "false");
            break;
        case Type::Number:
            to_redis_string(pipe, std::to_string(std::get<double>(json.data)));
            break;
        case Type::String:
            to_redis_string(pipe, std::get<std::string>(json.data));
            break;
        case Type::Array:
            for (const auto &val : std::get<std::vector<qb::redis::json_value>>(json.data)) {
                to_redis_string(pipe, val);
            }
            break;
        case Type::Object:
            for (const auto &[key, val] : std::get<qb::unordered_map<std::string, qb::redis::json_value>>(json.data)) {
                to_redis_string(pipe, key);
                to_redis_string(pipe, val);
            }
            break;
    }
    return true;
}

inline bool to_redis_string(qb::allocator::pipe<char> &pipe, qb::json const &json) {
    if (json.is_null()) {
        to_redis_string(pipe, std::string("null"));
    } else if (json.is_boolean()) {
        to_redis_string(pipe, std::string(json.get<bool>() ? "true" : "false"));
    } else if (json.is_number()) {
        if (json.is_number_integer()) {
            to_redis_string(pipe, std::to_string(json.get<int64_t>()));
        } else {
            to_redis_string(pipe, std::to_string(json.get<double>()));
        }
    } else if (json.is_string()) {
        to_redis_string(pipe, json.get<std::string>());
    } else if (json.is_array()) {
        for (const auto &val : json) {
            to_redis_string(pipe, val);
        }
    } else if (json.is_object()) {
        for (auto it = json.begin(); it != json.end(); ++it) {
            to_redis_string(pipe, std::string(it.key()));
            to_redis_string(pipe, it.value());
        }
    }
    return true;
}

// ============================================================================
// Argument counting for custom types
// ============================================================================

inline std::size_t redis_count(qb::redis::score const &) { return 1; }
inline std::size_t redis_count(qb::redis::score_member const &) { return 2; }
inline std::size_t redis_count(qb::redis::search_result const &sr) {
    return 1 + sr.fields.size() + sr.values.size();
}
inline std::size_t redis_count(qb::redis::cluster_node const &) { return 1; }
inline std::size_t redis_count(qb::redis::memory_info const &) { return 0; }
inline std::size_t redis_count(qb::redis::geo_pos const &) { return 2; }
inline std::size_t redis_count(qb::redis::stream_id const &) { return 1; }

inline std::size_t redis_count(qb::redis::json_value const &json) {
    using Type = qb::redis::json_value::Type;
    
    switch (json.type) {
        case Type::Null:
        case Type::Boolean:
        case Type::Number:
        case Type::String:
            return 1;
        case Type::Array: {
            const auto &arr = std::get<std::vector<qb::redis::json_value>>(json.data);
            std::size_t count = 0;
            for (const auto &val : arr) {
                count += redis_count(val);
            }
            return count;
        }
        case Type::Object: {
            const auto &obj = std::get<qb::unordered_map<std::string, qb::redis::json_value>>(json.data);
            std::size_t count = 0;
            for (const auto &[key, val] : obj) {
                count += 1;  // key
                count += redis_count(val);
            }
            return count;
        }
    }
    return 0;
}

// ============================================================================
// put_in_pipe - main command serialization
// ============================================================================

template <typename... Args>
void put_in_pipe(qb::allocator::pipe<char> &pipe, Args &&...args) {
    pipe << '*' << (redis_count(std::forward<Args>(args)) + ...) << "\r\n";
    (to_redis_string(pipe, std::forward<Args>(args)) && ...);
}

// ============================================================================
// Reply template for typed results
// ============================================================================

template <typename T>
struct is_optional_like : std::false_type {};
template <typename U>
struct is_optional_like<std::optional<U>> : std::true_type {};

/**
 * @struct Reply
 * @brief Typed wrapper for Redis command results
 *
 * Holds success flag, parsed result, raw parser::Value, and error message.
 * @tparam T The parsed result type
 */
template <typename T>
struct Reply {
    bool       _ok{};
    T          _result{};
    reply_ptr  _raw{};
    std::string _error{};  ///< owned string – never a dangling view

    [[nodiscard]] bool &ok() noexcept { return _ok; }
    [[nodiscard]] bool  ok() const noexcept { return _ok; }

    /// Boolean context: true if command succeeded (no Redis error).
    [[nodiscard]] explicit operator bool() const noexcept { return _ok; }

    /// Access the parsed result value.
    [[nodiscard]] T       &result()       { return _result; }
    [[nodiscard]] T const &result() const { return _result; }

    /// Alias for result() – preferred in new code.
    [[nodiscard]] T       &value()       { return _result; }
    [[nodiscard]] T const &value() const { return _result; }

    /// Returns the value if ok() and (for optional) has_value(); otherwise returns default_value.
    /// Enables: if (auto email = r.value_or(""); !email.empty()) { ... }
    template <typename U>
    [[nodiscard]] auto value_or(U &&default_value) const {
        if (!_ok) return std::forward<U>(default_value);
        if constexpr (is_optional_like<T>::value) {
            return _result.has_value() ? *_result : std::forward<U>(default_value);
        } else {
            return _result;
        }
    }

    /// Gives access to the original raw parser::Value that was received.
    /// Useful for types like pipeline_result where individual sub-replies
    /// are only accessible through the raw array.
    [[nodiscard]] reply_ptr       &raw()       noexcept { return _raw; }
    [[nodiscard]] reply_ptr const &raw() const noexcept { return _raw; }

    [[nodiscard]] std::string       &error()       noexcept { return _error; }
    [[nodiscard]] std::string const &error() const noexcept { return _error; }
};

// ============================================================================
// Reply handler interface - Modern C++23 with ownership semantics
// ============================================================================

/**
 * @class IReply
 * @brief Abstract interface for handling Redis reply callbacks
 *
 * Implementations receive ownership of the parsed reply via unique_ptr.
 */
class IReply {
public:
    IReply()          = default;
    virtual ~IReply() = default;
    // Takes ownership of the reply via unique_ptr
    virtual void operator()(std::unique_ptr<ReplyValue> reply) = 0;
    // Fail the pending command with an explicit reason (timeout, deadline, …).
    // Default routes through the disconnect path; concrete handlers override
    // to surface the domain-specific message instead of "disconnected".
    virtual void fail(const std::string &reason) { (void) reason; operator()(nullptr); }
};

/**
 * @class TReply
 * @brief Concrete reply handler that invokes a callback with Reply<T>
 * @tparam Func Callback type (invocable with Reply<T>&&)
 * @tparam T Expected result type for parsing
 */
template <typename Func, typename T>
class TReply final : public IReply {
    Func func;

public:
    explicit TReply(Func &&func)
        : func(std::forward<Func>(func)) {}
    
    ~TReply() override = default;

    void fail(const std::string &reason) final {
        func(Reply<T>{false, {}, nullptr, reason});
    }

    void operator()(std::unique_ptr<ReplyValue> raw) final {
        if (raw == nullptr) {
            func(Reply<T>{false, {}, nullptr, "disconnected"});
            return;
        }

        if (raw->is_error()) {
            // Copy the error message before moving raw, since get_error_message()
            // returns a string_view into raw's internal storage.
            std::string err_msg{raw->get_error_message()};
            func(Reply<T>{false, {}, std::move(raw), std::move(err_msg)});
            return;
        }

        try {
            auto value = parse<T>(*raw);
            func(Reply<T>{true, std::move(value), std::move(raw), {}});
        } catch (const Error &e) {
            // Catch all qb::redis::Error subclasses (ProtoError, CommandError, etc.)
            // e.what() is valid only for the lifetime of e, so copy it now.
            func(Reply<T>{false, {}, std::move(raw), std::string(e.what())});
        }
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_REPLY_H
