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
#include <qb/utility/type_traits.h>
#include <qb/system/allocator/pipe.h>
#include <qb/system/container/unordered_map.h>
#include <qb/json.h>
#include <hiredis/hiredis.h>
#include "types.h"

namespace qb::redis {

/**
 * @enum ReplyErrorType
 * @brief Types of errors that can occur in Redis replies
 */
enum ReplyErrorType { ERR, MOVED, ASK };

/**
 * @class Error
 * @brief Base exception class for Redis errors
 */
class Error : public std::exception {
public:
    /**
     * @brief Constructs an Error with the given message
     * @param msg Error message
     */
    explicit Error(std::string msg)
        : _msg(std::move(msg)) {}

    Error(const Error &)            = default;
    Error &operator=(const Error &) = default;

    Error(Error &&)            = default;
    Error &operator=(Error &&) = default;

    ~Error() override = default;

    /**
     * @brief Gets the error message
     * @return C-string containing the error message
     */
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
    /**
     * @brief Constructs a ProtoError with the given message
     * @param msg Error message
     */
    explicit ProtoError(const std::string &msg)
        : Error(msg) {}

    ProtoError(const ProtoError &)            = default;
    ProtoError &operator=(const ProtoError &) = default;

    ProtoError(ProtoError &&)            = default;
    ProtoError &operator=(ProtoError &&) = default;

    ~ProtoError() override = default;
};

////////////////////////////////

/**
 * @class ParseError
 * @brief Exception class for Redis reply parsing errors
 */
class ParseError : public ProtoError {
public:
    /**
     * @brief Constructs a ParseError for a type mismatch
     * @param expect_type Expected Redis reply type
     * @param reply Actual Redis reply received
     */
    ParseError(const std::string &expect_type, const redisReply &reply)
        : ProtoError(_err_info(expect_type, reply)) {}

    ParseError(const ParseError &)            = default;
    ParseError &operator=(const ParseError &) = default;

    ParseError(ParseError &&)            = default;
    ParseError &operator=(ParseError &&) = default;

    ~ParseError() override = default;

private:
    [[nodiscard]] static std::string _err_info(const std::string &type,
                                               const redisReply  &reply);
};

namespace reply {

// Types have been moved to types.h

template <typename T>
struct ParseTag {};

template <typename T>
inline T
parse(redisReply &reply) {
    return parse(ParseTag<T>(), reply);
}

std::string_view        parse(ParseTag<std::string_view>, redisReply &reply);
std::string             parse(ParseTag<std::string>, redisReply &reply);
long long               parse(ParseTag<long long>, redisReply &reply);
double                  parse(ParseTag<double>, redisReply &reply);
bool                    parse(ParseTag<bool>, redisReply &reply);
qb::redis::message      parse(ParseTag<qb::redis::message>, redisReply &reply);
qb::redis::pmessage     parse(ParseTag<qb::redis::pmessage>, redisReply &reply);
qb::redis::subscription parse(ParseTag<qb::redis::subscription>, redisReply &reply);
qb::redis::status       parse(ParseTag<qb::redis::status>, redisReply &reply);

// Add parse function declarations for our new types
std::vector<char>         parse(ParseTag<std::vector<char>>, redisReply &reply);
std::chrono::milliseconds parse(ParseTag<std::chrono::milliseconds>, redisReply &reply);
std::chrono::seconds      parse(ParseTag<std::chrono::seconds>, redisReply &reply);
qb::redis::geo_pos        parse(ParseTag<qb::redis::geo_pos>, redisReply &reply);
qb::redis::stream_id      parse(ParseTag<qb::redis::stream_id>, redisReply &reply);
qb::redis::stream_entry   parse(ParseTag<qb::redis::stream_entry>, redisReply &reply);
stream_entry_list         parse(ParseTag<stream_entry_list>, redisReply &reply);
map_stream_entry_list     parse(ParseTag<map_stream_entry_list>, redisReply &reply);
qb::redis::score          parse(ParseTag<qb::redis::score>, redisReply &reply);
qb::redis::score_member   parse(ParseTag<qb::redis::score_member>, redisReply &reply);
std::vector<qb::redis::score_member>
parse(ParseTag<std::vector<qb::redis::score_member>>, redisReply &reply);
qb::redis::search_result   parse(ParseTag<qb::redis::search_result>, redisReply &reply);
qb::redis::cluster_node    parse(ParseTag<qb::redis::cluster_node>, redisReply &reply);
qb::redis::memory_info     parse(ParseTag<qb::redis::memory_info>, redisReply &reply);
qb::redis::pipeline_result parse(ParseTag<qb::redis::pipeline_result>,
                                 redisReply &reply);
qb::redis::json_value      parse(ParseTag<qb::redis::json_value>, redisReply &reply);

template <typename T>
std::optional<T> parse(ParseTag<std::optional<T>>, redisReply &reply);
template <typename T, typename U>
std::pair<T, U> parse(ParseTag<std::pair<T, U>>, redisReply &reply);
template <typename... Args>
std::tuple<Args...> parse(ParseTag<std::tuple<Args...>>, redisReply &reply);

#ifdef REDIS_PLUS_PLUS_HAS_VARIANT

inline Monostate
parse(ParseTag<Monostate>, redisReply &) {
    // Just ignore the reply
    return {};
}

template <typename... Args>
Variant<Args...> parse(ParseTag<Variant<Args...>>, redisReply &reply);

#endif

template <typename T,
          typename std::enable_if<is_sequence_container<T>::value, int>::type = 0>
T parse(ParseTag<T>, redisReply &reply);
template <typename T,
          typename std::enable_if<is_associative_container<T>::value, int>::type = 0>
T parse(ParseTag<T>, redisReply &reply);
template <typename Output>
long long parse_scan_reply(redisReply &reply, Output output);
template <typename Out>
scan<Out>
parse(ParseTag<scan<Out>>, redisReply &reply) {
    scan<Out> sc;
    if constexpr (is_mappish<Out>::value)
        sc.cursor = parse_scan_reply(reply, std::inserter(sc.items, sc.items.end()));
    else
        sc.cursor = parse_scan_reply(reply, std::back_inserter(sc.items));
    return sc;
}

std::string type_to_string(int type);
status      to_status(redisReply &reply);
template <typename Output>
void to_array(redisReply &reply, Output output);
// Parse set reply to bool type
bool parse_set_reply(redisReply &reply);

// Add declaration for qb::json parser
qb::json parse(ParseTag<qb::json>, redisReply &reply);

} // namespace reply

// Inline implementations.

namespace reply {

namespace detail {

template <typename Output>
void
to_array(redisReply &reply, Output output) {
#ifdef REDIS_PLUS_PLUS_RESP_VERSION_3
    if (!qb::redis::is_array(reply) && !qb::redis::is_map(reply) &&
        !qb::redis::is_set(reply)) {
        throw ParseError("ARRAY or MAP or SET", reply);
    }
#else
    if (!qb::redis::is_array(reply)) {
        throw ParseError("ARRAY", reply);
    }
#endif

    if (reply.element == nullptr) {
        // Empty array.
        return;
    }

    for (std::size_t idx = 0; idx != reply.elements; ++idx) {
        auto *sub_reply = reply.element[idx];
        if (sub_reply == nullptr) {
            throw ProtoError("Null array element reply");
        }

        *output = parse<typename iterator_type<Output>::type>(*sub_reply);

        ++output;
    }
}

bool is_flat_array(redisReply &reply);

template <typename Output>
void
to_flat_array(redisReply &reply, Output output) {
    if (reply.element == nullptr) {
        // Empty array.
        return;
    }

    if (reply.elements % 2 != 0) {
        throw ProtoError("Not string pair array reply");
    }

    for (std::size_t idx = 0; idx != reply.elements; idx += 2) {
        auto *key_reply = reply.element[idx];
        auto *val_reply = reply.element[idx + 1];
        if (key_reply == nullptr || val_reply == nullptr) {
            throw ProtoError("Null string array reply");
        }

        using Pair       = typename iterator_type<Output>::type;
        using FirstType  = typename std::decay<typename Pair::first_type>::type;
        using SecondType = typename std::decay<typename Pair::second_type>::type;
        *output =
            std::make_pair(parse<FirstType>(*key_reply), parse<SecondType>(*val_reply));

        ++output;
    }
}

template <typename Output>
void
to_array(std::true_type, redisReply &reply, Output output) {
    if (is_flat_array(reply)) {
        to_flat_array(reply, output);
    } else {
        to_array(reply, output);
    }
}

template <typename Output>
void
to_array(std::false_type, redisReply &reply, Output output) {
    to_array(reply, output);
}

template <typename T>
std::tuple<T>
parse_tuple(redisReply **reply, std::size_t idx) {
    assert(reply != nullptr);

    auto *sub_reply = reply[idx];
    if (sub_reply == nullptr) {
        throw ProtoError("Null reply");
    }

    return std::make_tuple(parse<T>(*sub_reply));
}

template <typename T, typename... Args>
auto
parse_tuple(redisReply **reply, std::size_t idx) ->
    typename std::enable_if<sizeof...(Args) != 0, std::tuple<T, Args...>>::type {
    assert(reply != nullptr);

    return std::tuple_cat(parse_tuple<T>(reply, idx),
                          parse_tuple<Args...>(reply, idx + 1));
}

#ifdef REDIS_PLUS_PLUS_HAS_VARIANT

template <typename T>
Variant<T>
parse_variant(redisReply &reply) {
    return parse<T>(reply);
}

template <typename T, typename... Args>
auto
parse_variant(redisReply &reply) ->
    typename std::enable_if<sizeof...(Args) != 0, Variant<T, Args...>>::type {
    auto return_var = [](auto &&arg) { return Variant<T, Args...>(std::move(arg)); };

    try {
        return std::visit(return_var, parse_variant<T>(reply));
    } catch (const ProtoError &) {
        return std::visit(return_var, parse_variant<Args...>(reply));
    }
}

#endif

} // namespace detail

template <typename T>
std::optional<T>
parse(ParseTag<std::optional<T>>, redisReply &reply) {
    if (qb::redis::is_nil(reply)) {
        // Because of a GCC bug, we cannot return {} for -std=c++17
        // Refer to https://gcc.gnu.org/bugzilla/show_bug.cgi?id=86465
#if defined REDIS_PLUS_PLUS_HAS_OPTIONAL
        return std::nullopt;
#else
        return {};
#endif
    }

    return std::optional<T>(parse<T>(reply));
}

template <typename T, typename U>
std::pair<T, U>
parse(ParseTag<std::pair<T, U>>, redisReply &reply) {
    if (!qb::redis::is_array(reply)) {
        throw ParseError("ARRAY", reply);
    }

    if (reply.element == nullptr) {
        throw ProtoError("Null PAIR reply");
    }

    if (reply.elements == 1) {
        // Nested array reply. Check the first element of the nested array.
        auto *nested_element = reply.element[0];
        if (nested_element == nullptr) {
            throw ProtoError("null nested PAIR reply");
        }

        return parse(ParseTag<std::pair<T, U>>{}, *nested_element);
    }

    if (reply.elements != 2) {
        throw ProtoError("NOT key-value PAIR reply");
    }

    auto *first  = reply.element[0];
    auto *second = reply.element[1];
    if (first == nullptr || second == nullptr) {
        throw ProtoError("Null pair reply");
    }

    return std::make_pair(parse<typename std::decay<T>::type>(*first),
                          parse<typename std::decay<U>::type>(*second));
}

template <typename... Args>
std::tuple<Args...>
parse(ParseTag<std::tuple<Args...>>, redisReply &reply) {
    constexpr auto size = sizeof...(Args);

    static_assert(size > 0, "DO NOT support parsing tuple with 0 element");

    if (!qb::redis::is_array(reply)) {
        throw ParseError("ARRAY", reply);
    }

    if (reply.elements != size) {
        throw ProtoError("Expect tuple reply with " + std::to_string(size) +
                         " elements" + ", but got " + std::to_string(reply.elements) +
                         " elements");
    }

    if (reply.element == nullptr) {
        throw ProtoError("Null TUPLE reply");
    }

    return detail::parse_tuple<Args...>(reply.element, 0);
}

#ifdef REDIS_PLUS_PLUS_HAS_VARIANT

template <typename... Args>
Variant<Args...>
parse(ParseTag<Variant<Args...>>, redisReply &reply) {
    return detail::parse_variant<Args...>(reply);
}

#endif

template <typename T,
          typename std::enable_if<is_sequence_container<T>::value, int>::type>
T
parse(ParseTag<T>, redisReply &reply) {
#ifdef REDIS_PLUS_PLUS_RESP_VERSION_3
    if (!qb::redis::is_array(reply) && !qb::redis::is_set(reply)) {
        throw ParseError("ARRAY or SET", reply);
#else
    if (!qb::redis::is_array(reply)) {
        throw ParseError("ARRAY", reply);
#endif
    }

    T container;

    to_array(reply, std::back_inserter(container));

    return container;
}

template <typename T,
          typename std::enable_if<is_associative_container<T>::value, int>::type>
T
parse(ParseTag<T>, redisReply &reply) {
#ifdef REDIS_PLUS_PLUS_RESP_VERSION_3
    if (!qb::redis::is_array(reply) && !qb::redis::is_map(reply) &&
        !qb::redis::is_set(reply)) {
#else
    if (!qb::redis::is_array(reply)) {
#endif
        throw ParseError("ARRAY", reply);
    }

    T container;

    to_array(reply, std::inserter(container, container.end()));

    return container;
}

template <typename Output>
long long
parse_scan_reply(redisReply &reply, Output output) {
    if (reply.elements != 2 || reply.element == nullptr) {
        throw ProtoError("Invalid scan reply");
    }

    auto *cursor_reply = reply.element[0];
    auto *data_reply   = reply.element[1];
    if (cursor_reply == nullptr || data_reply == nullptr) {
        throw ProtoError("Invalid cursor reply or data reply");
    }

    auto      cursor_str = reply::parse<std::string>(*cursor_reply);
    long long new_cursor = 0;
    try {
        new_cursor = std::stoll(cursor_str);
    } catch (const std::exception &) {
        throw ProtoError("Invalid cursor reply: " + cursor_str);
    }

    reply::to_array(*data_reply, output);

    return new_cursor;
}

template <typename Output>
void
to_array(redisReply &reply, Output output) {
#ifdef REDIS_PLUS_PLUS_RESP_VERSION_3
    if (!qb::redis::is_array(reply) && !qb::redis::is_map(reply) &&
        !qb::redis::is_set(reply)) {
        throw ParseError("ARRAY or MAP or SET", reply);
    }
#else
    if (!qb::redis::is_array(reply)) {
        throw ParseError("ARRAY", reply);
    }
#endif

    detail::to_array(typename is_map_iterator<Output>::type(), reply, output);
}

} // namespace reply

template <typename T>
inline T
parse(redisReply &reply) {
    return reply::parse<T>(reply);
}

/**
 * @brief Counts the number of elements in a string literal for Redis protocol
 * @param str The string literal to count
 * @return Always returns 1, as a string literal is a single element
 */
template <size_t N>
inline std::size_t
redis_count(const char (&)[N]) {
    return 1;
}

/**
 * @brief Counts the number of elements in a C-string for Redis protocol
 * @param str The C-string to count
 * @return Always returns 1, as a C-string is a single element
 */
inline std::size_t
redis_count(const char* ) {
    return 1;
}

/**
 * @brief Counts the number of elements in a string for Redis protocol
 * @param Unused string parameter
 * @return Always returns 1, as a string is a single element
 */
inline std::size_t
redis_count(std::string const &) {
    return 1;
}

/**
 * @brief Counts the number of elements in a scalar arithmetic value for Redis protocol
 * @param Unused arithmetic value parameter
 * @return Always returns 1, as an arithmetic value is a single element
 */
template <typename T>
std::size_t
redis_count(T const &, std::enable_if_t<std::is_arithmetic_v<T>> * = 0) {
    return 1;
}

/**
 * @brief Counts the number of elements in a tuple for Redis protocol
 * @param Unused tuple parameter
 * @return Number of elements in the tuple
 */
template <typename... Args>
constexpr std::size_t
redis_count(std::tuple<Args...> const &) {
    return sizeof...(Args);
}

/**
 * @brief Counts the number of elements in a pair for Redis protocol
 * @param Unused pair parameter
 * @return Always returns 2, as a pair has two elements
 */
template <typename... Args>
constexpr std::size_t
redis_count(std::pair<Args...> const &) {
    return sizeof...(Args);
}

/**
 * @brief Counts the number of elements in an optional value for Redis protocol
 * @param opt Optional value to count
 * @return The count of the contained value if present, 0 otherwise
 */
template <typename T>
std::size_t
redis_count(std::optional<T> const &opt) {
    return opt ? redis_count(opt.value()) : 0;
}

/**
 * @brief Counts the number of elements in a container for Redis protocol
 * @param cnt Container to count
 * @return Number of elements in the container multiplied by the count of a single
 * element
 */
template <typename T>
std::size_t
redis_count(T const &cnt, std::enable_if_t<qb::is_container<T>::value> * = 0) {
    return cnt.size() ? redis_count(*cnt.begin()) * cnt.size() : 0;
}

/**
 * @brief Counts the number of elements in a qb::json value for Redis protocol
 * @param json The JSON value to count
 * @return Number of elements in the JSON value
 */
inline std::size_t
redis_count(qb::json const &json) {
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
            count += 1; // Key
            count += redis_count(it.value());
        }
        return count;
    }
    
    return 0;
}

/**
 * @brief Converts a string literal to Redis protocol format and writes it to a pipe
 * @param pipe Output pipe to write to
 * @param str String literal to convert
 * @return Always returns true
 */
template <size_t N>
inline bool
to_redis_string(qb::allocator::pipe<char> &pipe, const char (&str)[N]) {
    pipe << '$' << (N-1) << "\r\n" << str << "\r\n";
    return true;
}

/**
 * @brief Converts a C-string to Redis protocol format and writes it to a pipe
 * @param pipe Output pipe to write to
 * @param str C-string to convert
 * @return Always returns true
 */
inline bool
to_redis_string(qb::allocator::pipe<char> &pipe, const char* str) {
    if (!str) str = "";
    size_t len = strlen(str);
    pipe << '$' << len << "\r\n" << str << "\r\n";
    return true;
}

/**
 * @brief Converts a string to Redis protocol format and writes it to a pipe
 * @param pipe Output pipe to write to
 * @param val String value to convert
 * @return Always returns true
 */
inline bool
to_redis_string(qb::allocator::pipe<char> &pipe, std::string const &val) {
    pipe << '$' << val.size() << "\r\n" << val << "\r\n";
    return true;
}

/**
 * @brief Converts an arithmetic value to Redis protocol format and writes it to a pipe
 * @param pipe Output pipe to write to
 * @param val Arithmetic value to convert
 * @return Result of the string conversion
 */
template <typename T, typename = typename std::enable_if<std::is_arithmetic<T>::value>::type>
inline bool
to_redis_string(qb::allocator::pipe<char> &pipe, T const &val) {
    return to_redis_string(pipe, std::to_string(val));
}

/**
 * @brief Converts an optional value to Redis protocol format and writes it to a pipe
 * @param pipe Output pipe to write to
 * @param opt Optional value to convert
 * @return Always returns true
 */
template <typename T>
bool
to_redis_string(qb::allocator::pipe<char> &pipe, std::optional<T> const &opt) {
    if (opt)
        to_redis_string(pipe, opt.value());
    return true;
}

/**
 * @brief Helper function to convert all elements of a tuple to Redis protocol format
 * @param pipe Output pipe to write to
 * @param t Tuple to convert
 * @param Indices sequence for tuple element access
 * @return true if all conversions succeed
 */
template <typename Tuple, std::size_t... N>
bool
put_tuple(qb::allocator::pipe<char> &pipe, Tuple const &t, std::index_sequence<N...>) {
    return (to_redis_string(pipe, std::get<N>(t)) && ...);
}

/**
 * @brief Converts a tuple to Redis protocol format and writes it to a pipe
 * @param pipe Output pipe to write to
 * @param t Tuple to convert
 * @return Result of the tuple conversion
 */
template <typename... Args>
bool
to_redis_string(qb::allocator::pipe<char> &pipe, std::tuple<Args...> const &t) {
    return put_tuple(pipe, t, std::index_sequence_for<Args...>{});
}

/**
 * @brief Converts a pair to Redis protocol format and writes it to a pipe
 * @param pipe Output pipe to write to
 * @param p Pair to convert
 * @return Always returns true
 */
template <typename... Args>
bool
to_redis_string(qb::allocator::pipe<char> &pipe, std::pair<Args...> const &p) {
    to_redis_string(pipe, p.first);
    to_redis_string(pipe, p.second);
    return true;
}

/**
 * @brief Converts a container to Redis protocol format and writes it to a pipe
 * @param pipe Output pipe to write to
 * @param cnt Container to convert
 * @return Always returns true
 */
template <typename T>
bool
to_redis_string(qb::allocator::pipe<char> &pipe, T const &cnt,
                std::enable_if_t<qb::is_container<T>::value> * = 0) {
    if constexpr (is_map_iterator<decltype(cnt.begin())>::value) {
        for (const auto &el : cnt) {
            to_redis_string(pipe, el.first);
            to_redis_string(pipe, el.second);
        }
    } else {
        for (const auto &el : cnt)
            to_redis_string(pipe, el);
    }
    return true;
}

/**
 * @brief Converts a vector of chars to Redis protocol format and writes it to a pipe
 * @param pipe Output pipe to write to
 * @param val Vector of chars to convert (binary data)
 * @return Always returns true
 */
inline bool
to_redis_string(qb::allocator::pipe<char> &pipe, std::vector<char> const &val) {
    pipe << '$' << val.size() << "\r\n";
    pipe.write(val.data(), val.size());
    pipe << "\r\n";
    return true;
}

/**
 * @brief Converts milliseconds to Redis protocol format and writes it to a pipe
 * @param pipe Output pipe to write to
 * @param val Milliseconds value to convert
 * @return Always returns true
 */
inline bool
to_redis_string(qb::allocator::pipe<char> &pipe, std::chrono::milliseconds const &val) {
    return to_redis_string(pipe, std::to_string(val.count()));
}

/**
 * @brief Converts seconds to Redis protocol format and writes it to a pipe
 * @param pipe Output pipe to write to
 * @param val Seconds value to convert
 * @return Always returns true
 */
inline bool
to_redis_string(qb::allocator::pipe<char> &pipe, std::chrono::seconds const &val) {
    return to_redis_string(pipe, std::to_string(val.count()));
}

/**
 * @brief Converts a geo_pos to Redis protocol format and writes it to a pipe
 * @param pipe Output pipe to write to
 * @param pos Geo position to convert
 * @return Always returns true
 */
inline bool
to_redis_string(qb::allocator::pipe<char> &pipe, qb::redis::geo_pos const &pos) {
    to_redis_string(pipe, pos.longitude);
    to_redis_string(pipe, pos.latitude);
    return true;
}

/**
 * @brief Converts a stream_id to Redis protocol format and writes it to a pipe
 * @param pipe Output pipe to write to
 * @param id Stream ID to convert
 * @return Always returns true
 */
inline bool
to_redis_string(qb::allocator::pipe<char> &pipe, qb::redis::stream_id const &id) {
    return to_redis_string(pipe, id.to_string());
}

/**
 * @brief Converts a score to Redis protocol format and writes it to a pipe
 * @param pipe Output pipe to write to
 * @param score Score value to convert
 * @return Always returns true
 */
inline bool
to_redis_string(qb::allocator::pipe<char> &pipe, qb::redis::score const &score) {
    return to_redis_string(pipe, score.value);
}

/**
 * @brief Converts a score_member to Redis protocol format and writes it to a pipe
 * @param pipe Output pipe to write to
 * @param sm Score-member pair to convert
 * @return Always returns true
 */
inline bool
to_redis_string(qb::allocator::pipe<char> &pipe, qb::redis::score_member const &sm) {
    to_redis_string(pipe, sm.score);
    to_redis_string(pipe, sm.member);
    return true;
}

/**
 * @brief Converts a search_result to Redis protocol format and writes it to a pipe
 * @param pipe Output pipe to write to
 * @param sr Search result to convert
 * @return Always returns true
 */
inline bool
to_redis_string(qb::allocator::pipe<char> &pipe, qb::redis::search_result const &sr) {
    to_redis_string(pipe, sr.key);
    for (const auto &field : sr.fields) {
        to_redis_string(pipe, field);
    }
    for (const auto &value : sr.values) {
        to_redis_string(pipe, value);
    }
    return true;
}

/**
 * @brief Converts a cluster_node to Redis protocol format and writes it to a pipe
 * @param pipe Output pipe to write to
 * @param node Cluster node information to convert
 * @return Always returns true
 */
inline bool
to_redis_string(qb::allocator::pipe<char> &pipe, qb::redis::cluster_node const &node) {
    to_redis_string(pipe, node.id);
    return true;
}

/**
 * @brief Converts a json_value to Redis protocol format and writes it to a pipe
 * @param pipe Output pipe to write to
 * @param json JSON value to convert
 * @return Always returns true
 */
inline bool
to_redis_string(qb::allocator::pipe<char> &pipe, qb::redis::json_value const &json) {
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
            for (const auto &val :
                 std::get<std::vector<qb::redis::json_value>>(json.data)) {
                to_redis_string(pipe, val);
            }
            break;
        case Type::Object:
            for (const auto &[key, val] :
                 std::get<qb::unordered_map<std::string, qb::redis::json_value>>(
                     json.data)) {
                to_redis_string(pipe, key);
                to_redis_string(pipe, val);
            }
            break;
    }
    return true;
}

/**
 * @brief Converts a qb::json value to Redis protocol format and writes it to a pipe
 * @param pipe Output pipe to write to
 * @param json The JSON value to convert
 * @return Always returns true
 */
inline bool
to_redis_string(qb::allocator::pipe<char> &pipe, qb::json const &json) {
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

/**
 * @brief Counts the number of elements in a score for Redis protocol
 * @param score The score value
 * @return Always returns 1, as a score is a single element
 */
inline std::size_t
redis_count(qb::redis::score const &) {
    return 1;
}

/**
 * @brief Counts the number of elements in a score_member for Redis protocol
 * @param sm The score_member value
 * @return Always returns 2 (score and member)
 */
inline std::size_t
redis_count(qb::redis::score_member const &) {
    return 2;
}

/**
 * @brief Counts the number of elements in a search_result for Redis protocol
 * @param sr The search_result value
 * @return Number of elements (key plus fields and values)
 */
inline std::size_t
redis_count(qb::redis::search_result const &sr) {
    return 1 + sr.fields.size() + sr.values.size();
}

/**
 * @brief Counts the number of elements in a cluster_node for Redis protocol
 * @param node The cluster_node value
 * @return Always returns 1, as cluster node info is sent as a single string
 */
inline std::size_t
redis_count(qb::redis::cluster_node const &) {
    return 1;
}

/**
 * @brief Counts the number of elements in a memory_info for Redis protocol
 * @param Unused memory_info parameter
 * @return Always returns 0, as memory_info is only used for parsing responses
 */
inline std::size_t
redis_count(qb::redis::memory_info const &) {
    return 0;
}

/**
 * @brief Counts the number of elements in a geo_pos for Redis protocol
 * @param Unused geo_pos parameter
 * @return Always returns 2 (longitude and latitude)
 */
inline std::size_t
redis_count(qb::redis::geo_pos const &) {
    return 2;
}

/**
 * @brief Counts the number of elements in a stream_id for Redis protocol
 * @param Unused stream_id parameter
 * @return Always returns 1, as a stream ID is sent as a single string
 */
inline std::size_t
redis_count(qb::redis::stream_id const &) {
    return 1;
}

/**
 * @brief Counts the number of elements in a json_value for Redis protocol
 * @param json The json_value to count
 * @return Number of elements based on the JSON value type
 */
inline std::size_t
redis_count(qb::redis::json_value const &json) {
    using Type = qb::redis::json_value::Type;

    switch (json.type) {
        case Type::Null:
        case Type::Boolean:
        case Type::Number:
        case Type::String:
            return 1;
        case Type::Array: {
            const auto &arr   = std::get<std::vector<qb::redis::json_value>>(json.data);
            std::size_t count = 0;
            for (const auto &val : arr) {
                count += redis_count(val);
            }
            return count;
        }
        case Type::Object: {
            const auto &obj =
                std::get<qb::unordered_map<std::string, qb::redis::json_value>>(
                    json.data);
            std::size_t count = 0;
            for (const auto &[key, val] : obj) {
                count += 1; // key
                count += redis_count(val);
            }
            return count;
        }
        default:
            return 0;
    }
}

/**
 * @brief Formats and writes Redis commands to a pipe
 *
 * This function formats the provided arguments according to the Redis protocol
 * and writes them to the pipe. It handles the command array formatting.
 *
 * @param pipe Output pipe to write to
 * @param args Command arguments to format and write
 */
template <typename... Args>
void
put_in_pipe(qb::allocator::pipe<char> &pipe, Args &&...args) {
    pipe << '*' << (redis_count(std::forward<Args>(args)) + ...) << "\r\n";
    (to_redis_string(pipe, std::forward<Args>(args)) && ...);
}

/**
 * @struct Reply
 * @brief Generic container for Redis command results
 *
 * This structure holds the result of a Redis command execution,
 * including success status, the typed result value, and the raw reply.
 *
 * @tparam T The type of the result value
 */
template <typename T>
struct Reply {
    bool             _ok{};     ///< Whether the command was successful
    T                _result{}; ///< The typed result of the command
    redis::reply_ptr _raw{};    ///< The raw Redis reply
    std::string_view _error{};  ///< Error from Redis

    inline bool &
    ok() {
        return _ok;
    }
    inline T &
    result() {
        return _result;
    }
    inline redis::reply_ptr &
    raw() {
        return _raw;
    }
    inline std::string_view &
    error() {
        return _error;
    }
};

/**
 * @class IReply
 * @brief Interface for Redis reply handlers
 *
 * This abstract base class defines the interface for objects that
 * process Redis replies. It's used primarily for asynchronous command handling.
 */
class IReply {
public:
    IReply()          = default;
    virtual ~IReply() = default;

    /**
     * @brief Process a Redis reply
     * @param reply The raw Redis reply to process
     */
    virtual void operator()(redisReply *reply) = 0;
};

/**
 * @class TReply
 * @brief Type-specific implementation of IReply
 *
 * This class implements the IReply interface for a specific result type.
 * It parses the Redis reply into the specified type and passes it to
 * the provided callback function.
 *
 * @tparam Func The callback function type
 * @tparam T The type of the result value
 */
template <typename Func, typename T>
class TReply final : public IReply {
    Func func;

public:
    /**
     * @brief Constructs a TReply with the specified callback
     * @param func Callback function to process the reply
     */
    explicit TReply(Func &&func)
        : func(std::forward<Func>(func)) {}
    ~TReply() override = default;

    /**
     * @brief Process a Redis reply
     * @param raw The raw Redis reply to process
     */
    void
    operator()(redisReply *raw) final {
        try {
            func(Reply<T>{true, qb::redis::reply::parse<T>(*raw), reply_ptr(raw)});
        } catch (const ProtoError &) {
            func(Reply<T>{false, {}, reply_ptr(raw), {raw->str, raw->len}});
        }
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_REPLY_H
