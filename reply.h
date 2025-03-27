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

    Error(const Error &) = default;
    Error &operator=(const Error &) = default;

    Error(Error &&) = default;
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

    ProtoError(const ProtoError &) = default;
    ProtoError &operator=(const ProtoError &) = default;

    ProtoError(ProtoError &&) = default;
    ProtoError &operator=(ProtoError &&) = default;

    ~ProtoError() override = default;
};

////////////////////////////////

/**
 * @struct ReplyDeleter
 * @brief Custom deleter for Redis reply objects
 */
struct ReplyDeleter {
    /**
     * @brief Deallocates a Redis reply object
     * @param reply Redis reply to delete
     */
    void
    operator()(redisReply *reply) const {
        freeReplyObject(reply);
    }
};

/**
 * @typedef reply_ptr
 * @brief Smart pointer for Redis reply objects
 */
using reply_ptr = std::unique_ptr<redisReply, ReplyDeleter>;

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

    ParseError(const ParseError &) = default;
    ParseError &operator=(const ParseError &) = default;

    ParseError(ParseError &&) = default;
    ParseError &operator=(ParseError &&) = default;

    ~ParseError() override = default;

private:
    [[nodiscard]] static std::string _err_info(const std::string &type, const redisReply &reply);
};

namespace reply {

/**
 * @struct error
 * @brief Container for Redis error information
 */
struct error {
    std::string what;
    redis::reply_ptr raw;
};

/**
 * @struct status
 * @brief Container for Redis status reply
 */
struct status : public std::string {};

/**
 * @struct set
 * @brief Container for Redis SET command result
 */
struct set {
    bool status{};
    bool
    operator()() const {
        return status;
    }
};

/**
 * @struct scan
 * @brief Container for Redis scan command results
 * @tparam Out Output container type for scan results
 */
template <typename Out = std::vector<std::string>>
struct scan {
    std::size_t cursor;
    Out items;
};

/**
 * @struct message
 * @brief Container for Redis pub/sub message data
 */
struct message {
    std::string_view pattern;
    std::string_view channel;
    std::string_view message;
    redis::reply_ptr raw;
};

/**
 * @struct pmessage
 * @brief Container for Redis pub/sub pattern message data
 */
struct pmessage : public message {};

/**
 * @struct subscription
 * @brief Container for Redis subscription information
 */
struct subscription {
    std::optional<std::string> channel;
    long long num{};
};

/**
 * @struct geo_pos
 * @brief Container for Redis GEO position information
 */
struct geo_pos {
    double longitude{};
    double latitude{};

    bool operator==(const geo_pos& other) const {
        return longitude == other.longitude && latitude == other.latitude;
    }

    bool operator!=(const geo_pos& other) const {
        return !(*this == other);
    }
};

/**
 * @struct geo_distance
 * @brief Container for Redis GEO distance information
 */
struct geo_distance {
    std::string member;
    double distance{};
};

/**
 * @struct stream_id
 * @brief Container for Redis Stream ID
 */
struct stream_id {
    long long timestamp{};
    long long sequence{};

    std::string to_string() const {
        return std::to_string(timestamp) + "-" + std::to_string(sequence);
    }

    bool operator==(const stream_id& other) const {
        return timestamp == other.timestamp && sequence == other.sequence;
    }

    bool operator!=(const stream_id& other) const {
        return !(*this == other);
    }

    bool operator<(const stream_id& other) const {
        return timestamp < other.timestamp ||
              (timestamp == other.timestamp && sequence < other.sequence);
    }
};

/**
 * @struct stream_entry
 * @brief Container for Redis Stream entry
 */
struct stream_entry {
    stream_id id;
    std::unordered_map<std::string, std::string> fields;
};

template <typename T>
struct ParseTag {};

template <typename T>
inline T
parse(redisReply &reply) {
    return parse(ParseTag<T>(), reply);
}

void parse(ParseTag<void>, redisReply &reply);
set parse(ParseTag<set>, redisReply &reply);
std::string_view parse(ParseTag<std::string_view>, redisReply &reply);
std::string parse(ParseTag<std::string>, redisReply &reply);
long long parse(ParseTag<long long>, redisReply &reply);
double parse(ParseTag<double>, redisReply &reply);
bool parse(ParseTag<bool>, redisReply &reply);
message parse(ParseTag<message>, redisReply &reply);
pmessage parse(ParseTag<pmessage>, redisReply &reply);
subscription parse(ParseTag<subscription>, redisReply &reply);

// Add parse function declarations for our new types
std::vector<char> parse(ParseTag<std::vector<char>>, redisReply &reply);
std::chrono::milliseconds parse(ParseTag<std::chrono::milliseconds>, redisReply &reply);
std::chrono::seconds parse(ParseTag<std::chrono::seconds>, redisReply &reply);
geo_pos parse(ParseTag<geo_pos>, redisReply &reply);
stream_id parse(ParseTag<stream_id>, redisReply &reply);
stream_entry parse(ParseTag<stream_entry>, redisReply &reply);

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

template <typename T, typename std::enable_if<is_sequence_container<T>::value, int>::type = 0>
T parse(ParseTag<T>, redisReply &reply);
template <typename T, typename std::enable_if<is_associative_container<T>::value, int>::type = 0>
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

inline bool
is_error(redisReply &reply) {
    return reply.type == REDIS_REPLY_ERROR;
}

inline bool
is_nil(redisReply &reply) {
    return reply.type == REDIS_REPLY_NIL;
}

inline bool
is_string(redisReply &reply) {
    return reply.type == REDIS_REPLY_STRING;
}

inline bool
is_status(redisReply &reply) {
    return reply.type == REDIS_REPLY_STATUS;
}

inline bool
is_integer(redisReply &reply) {
    return reply.type == REDIS_REPLY_INTEGER;
}

inline bool
is_array(redisReply &reply) {
    return reply.type == REDIS_REPLY_ARRAY;
}

#ifdef REDIS_PLUS_PLUS_RESP_VERSION_3

inline bool
is_double(redisReply &reply) {
    return reply.type == REDIS_REPLY_DOUBLE;
}

inline bool
is_bool(redisReply &reply) {
    return reply.type == REDIS_REPLY_BOOL;
}

inline bool
is_map(redisReply &reply) {
    return reply.type == REDIS_REPLY_MAP;
}

inline bool
is_set(redisReply &reply) {
    return reply.type == REDIS_REPLY_SET;
}

inline bool
is_attr(redisReply &reply) {
    return reply.type == REDIS_REPLY_ATTR;
}

inline bool
is_push(redisReply &reply) {
    return reply.type == REDIS_REPLY_PUSH;
}

inline bool
is_bignum(redisReply &reply) {
    return reply.type == REDIS_REPLY_BIGNUM;
}

inline bool
is_verb(redisReply &reply) {
    return reply.type == REDIS_REPLY_VERB;
}

#endif

std::string type_to_string(int type);
std::string to_status(redisReply &reply);
template <typename Output>
void to_array(redisReply &reply, Output output);
// Parse set reply to bool type
bool parse_set_reply(redisReply &reply);

stream_id parse(ParseTag<stream_id>, redisReply &reply);
geo_pos parse(ParseTag<geo_pos>, redisReply &reply);
std::vector<char> parse(ParseTag<std::vector<char>>, redisReply &reply);
std::chrono::milliseconds parse(ParseTag<std::chrono::milliseconds>, redisReply &reply);
std::chrono::seconds parse(ParseTag<std::chrono::seconds>, redisReply &reply);
stream_entry parse(ParseTag<stream_entry>, redisReply &reply);

} // namespace reply

// Inline implementations.

namespace reply {

namespace detail {

template <typename Output>
void
to_array(redisReply &reply, Output output) {
#ifdef REDIS_PLUS_PLUS_RESP_VERSION_3
    if (!is_array(reply) && !is_map(reply) && !is_set(reply)) {
        throw ParseError("ARRAY or MAP or SET", reply);
    }
#else
    if (!is_array(reply)) {
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

        using Pair = typename iterator_type<Output>::type;
        using FirstType = typename std::decay<typename Pair::first_type>::type;
        using SecondType = typename std::decay<typename Pair::second_type>::type;
        *output = std::make_pair(parse<FirstType>(*key_reply), parse<SecondType>(*val_reply));

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

    return std::tuple_cat(parse_tuple<T>(reply, idx), parse_tuple<Args...>(reply, idx + 1));
}

#ifdef REDIS_PLUS_PLUS_HAS_VARIANT

template <typename T>
Variant<T>
parse_variant(redisReply &reply) {
    return parse<T>(reply);
}

template <typename T, typename... Args>
auto
parse_variant(redisReply &reply) -> typename std::enable_if<sizeof...(Args) != 0, Variant<T, Args...>>::type {
    auto return_var = [](auto &&arg) {
        return Variant<T, Args...>(std::move(arg));
    };

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
    if (reply::is_nil(reply)) {
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
    if (!is_array(reply)) {
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

    auto *first = reply.element[0];
    auto *second = reply.element[1];
    if (first == nullptr || second == nullptr) {
        throw ProtoError("Null pair reply");
    }

    return std::make_pair(parse<typename std::decay<T>::type>(*first), parse<typename std::decay<U>::type>(*second));
}

template <typename... Args>
std::tuple<Args...>
parse(ParseTag<std::tuple<Args...>>, redisReply &reply) {
    constexpr auto size = sizeof...(Args);

    static_assert(size > 0, "DO NOT support parsing tuple with 0 element");

    if (!is_array(reply)) {
        throw ParseError("ARRAY", reply);
    }

    if (reply.elements != size) {
        throw ProtoError(
            "Expect tuple reply with " + std::to_string(size) + " elements" + ", but got " +
            std::to_string(reply.elements) + " elements");
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

template <typename T, typename std::enable_if<is_sequence_container<T>::value, int>::type>
T
parse(ParseTag<T>, redisReply &reply) {
#ifdef REDIS_PLUS_PLUS_RESP_VERSION_3
    if (!is_array(reply) && !is_set(reply)) {
        throw ParseError("ARRAY or SET", reply);
#else
    if (!is_array(reply)) {
        throw ParseError("ARRAY", reply);
#endif
    }

    T container;

    to_array(reply, std::back_inserter(container));

    return container;
}

template <typename T, typename std::enable_if<is_associative_container<T>::value, int>::type>
T
parse(ParseTag<T>, redisReply &reply) {
#ifdef REDIS_PLUS_PLUS_RESP_VERSION_3
    if (!is_array(reply) && !is_map(reply) && !is_set(reply)) {
#else
    if (!is_array(reply)) {
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
    auto *data_reply = reply.element[1];
    if (cursor_reply == nullptr || data_reply == nullptr) {
        throw ProtoError("Invalid cursor reply or data reply");
    }

    auto cursor_str = reply::parse<std::string>(*cursor_reply);
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
    if (!is_array(reply) && !is_map(reply) && !is_set(reply)) {
        throw ParseError("ARRAY or MAP or SET", reply);
    }
#else
    if (!is_array(reply)) {
        throw ParseError("ARRAY", reply);
    }
#endif

    detail::to_array(typename is_map_iterator<Output>::type(), reply, output);
}

} // namespace reply

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
 * @return Number of elements in the container multiplied by the count of a single element
 */
template <typename T>
std::size_t
redis_count(T const &cnt, std::enable_if_t<qb::is_container<T>::value> * = 0) {
    return cnt.size() ? redis_count(*cnt.begin()) * cnt.size() : 0;
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
template <typename T>
bool
to_redis_string(qb::allocator::pipe<char> &pipe, T const &val, std::enable_if_t<std::is_arithmetic_v<T>> * = 0) {
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
to_redis_string(qb::allocator::pipe<char> &pipe, T const &cnt, std::enable_if_t<qb::is_container<T>::value> * = 0) {
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
to_redis_string(qb::allocator::pipe<char> &pipe, qb::redis::reply::geo_pos const &pos) {
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
to_redis_string(qb::allocator::pipe<char> &pipe, qb::redis::reply::stream_id const &id) {
    return to_redis_string(pipe, id.to_string());
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
    bool ok{};       ///< Whether the command was successful
    T result{};      ///< The typed result of the command
    redis::reply_ptr raw{}; ///< The raw Redis reply
    std::string_view error; ///< Error from Redis
};

/**
 * @struct Reply<void>
 * @brief Specialization for commands that don't return a value
 * 
 * This specialization is used for Redis commands that don't return
 * a meaningful value but only success/failure status.
 */
template <>
struct Reply<void> {
    bool ok{};       ///< Whether the command was successful
    redis::reply_ptr raw{}; ///< The raw Redis reply
    std::string_view error; ///< Error from Redis
};

/**
 * @struct Reply<bool>
 * @brief Specialization for commands that return a boolean status
 * 
 * This specialization is used for Redis commands that return
 * a boolean status indicating success or failure.
 */
template <>
struct Reply<bool> {
    bool ok{};       ///< The boolean result
    redis::reply_ptr raw{}; ///< The raw Redis reply
    std::string_view error; ///< Error from Redis
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
    IReply() = default;
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

/**
 * @class TReply<Func, void>
 * @brief Specialization for void result type
 * 
 * This specialization handles Redis commands that don't return
 * a meaningful value but only success/failure status.
 * 
 * @tparam Func The callback function type
 */
template <typename Func>
class TReply<Func, void> final : public IReply {
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
            qb::redis::reply::parse<void>(*raw);
            func(Reply<void>{true, reply_ptr(raw)});
        } catch (const ProtoError &) {
            func(Reply<void>{false, reply_ptr(raw), {raw->str, raw->len}});
        }
    }
};

/**
 * @class TReply<Func, bool>
 * @brief Specialization for boolean result type
 * 
 * This specialization handles Redis commands that return
 * a boolean status indicating success or failure.
 * 
 * @tparam Func The callback function type
 */
template <typename Func>
class TReply<Func, bool> final : public IReply {
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
            func(Reply<bool>{qb::redis::reply::parse<bool>(*raw), reply_ptr(raw)});
        } catch (const ProtoError &) {
            func(Reply<bool>{false, reply_ptr(raw), {raw->str, raw->len}});
        }
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_REPLY_H
