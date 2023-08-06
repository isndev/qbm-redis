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
#include <qb/utility/type_traits.h>
#include <qb/system/allocator/pipe.h>
#include <qb/system/container/unordered_map.h>
#include <hiredis/hiredis.h>
#include "types.h"

namespace qb::redis {

enum ReplyErrorType { ERR, MOVED, ASK };

class Error : public std::exception {
public:
    explicit Error(std::string msg)
        : _msg(std::move(msg)) {}

    Error(const Error &) = default;
    Error &operator=(const Error &) = default;

    Error(Error &&) = default;
    Error &operator=(Error &&) = default;

    ~Error() override = default;

    [[nodiscard]] const char *
    what() const noexcept override {
        return _msg.data();
    }

private:
    std::string _msg;
};

class ProtoError : public Error {
public:
    explicit ProtoError(const std::string &msg)
        : Error(msg) {}

    ProtoError(const ProtoError &) = default;
    ProtoError &operator=(const ProtoError &) = default;

    ProtoError(ProtoError &&) = default;
    ProtoError &operator=(ProtoError &&) = default;

    ~ProtoError() override = default;
};

////////////////////////////////

struct ReplyDeleter {
    void
    operator()(redisReply *reply) const {
        freeReplyObject(reply);
    }
};

using reply_ptr = std::unique_ptr<redisReply, ReplyDeleter>;

class ParseError : public ProtoError {
public:
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

struct error {
    std::string what;
    redis::reply_ptr raw;
};
struct status : public std::string {};
struct set {
    bool status{};
    bool
    operator()() const {
        return status;
    }
};
template <typename Out = std::vector<std::string>>
struct scan {
    std::size_t cursor;
    Out items;
};
struct message {
    std::string_view pattern;
    std::string_view channel;
    std::string_view message;
    redis::reply_ptr raw;
};
struct pmessage : public message {};
struct subscription {
    std::optional<std::string> channel;
    long long num{};
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

inline std::size_t
redis_count(std::string const &val) {
    return 1;
}

template <typename T>
std::size_t
redis_count(T const &, std::enable_if_t<std::is_arithmetic_v<T>> * = 0) {
    return 1;
}

template <typename... Args>
constexpr std::size_t
redis_count(std::tuple<Args...> const &) {
    return sizeof...(Args);
}

template <typename... Args>
constexpr std::size_t
redis_count(std::pair<Args...> const &) {
    return sizeof...(Args);
}

template <typename T>
std::size_t
redis_count(std::optional<T> const &opt) {
    return opt ? redis_count(opt.value()) : 0;
}

template <typename T>
std::size_t
redis_count(T const &cnt, std::enable_if_t<qb::is_container<T>::value> * = 0) {
    return cnt.size() ? redis_count(*cnt.begin()) * cnt.size() : 0;
}

inline bool
to_redis_string(qb::allocator::pipe<char> &pipe, std::string const &val) {
    pipe << '$' << val.size() << "\r\n" << val << "\r\n";
    return true;
}

template <typename T>
bool
to_redis_string(qb::allocator::pipe<char> &pipe, T const &val, std::enable_if_t<std::is_arithmetic_v<T>> * = 0) {
    return to_redis_string(pipe, std::to_string(val));
}

template <typename T>
bool
to_redis_string(qb::allocator::pipe<char> &pipe, std::optional<T> const &opt) {
    if (opt)
        to_redis_string(pipe, opt.value());
    return true;
}

template <typename Tuple, std::size_t... N>
bool
put_tuple(qb::allocator::pipe<char> &pipe, Tuple const &t, std::index_sequence<N...>) {
    return (to_redis_string(pipe, std::get<N>(t)) && ...);
}

template <typename... Args>
bool
to_redis_string(qb::allocator::pipe<char> &pipe, std::tuple<Args...> const &t) {
    return put_tuple(pipe, t, std::index_sequence_for<Args...>{});
}

template <typename... Args>
bool
to_redis_string(qb::allocator::pipe<char> &pipe, std::pair<Args...> const &p) {
    to_redis_string(pipe, p.first);
    to_redis_string(pipe, p.second);
    return true;
}

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

template <typename... Args>
void
put_in_pipe(qb::allocator::pipe<char> &pipe, Args &&...args) {
    pipe << '*' << (redis_count(std::forward<Args>(args)) + ...) << "\r\n";
    (to_redis_string(pipe, std::forward<Args>(args)) && ...);
}

template <typename T>
struct Reply {
    bool ok{};
    T result;
    redis::reply_ptr raw;
};

template <>
struct Reply<void> {
    bool ok;
    redis::reply_ptr raw;
};

template <>
struct Reply<bool> {
    bool ok;
    redis::reply_ptr raw;
};

class IReply {
public:
    IReply() = default;
    virtual ~IReply() = default;
    virtual void operator()(redisReply *) = 0;
};

template <typename Func, typename T>
class TReply final : public IReply {
    Func func;

public:
    explicit TReply(Func &&func)
        : func(std::forward<Func>(func)) {}
    ~TReply() override = default;
    void
    operator()(redisReply *raw) final {
        try {
            func(Reply<T>{true, qb::redis::reply::parse<T>(*raw), reply_ptr(raw)});
        } catch (...) {
            func(Reply<T>{false, {}, reply_ptr(raw)});
        }
    }
};

template <typename Func>
class TReply<Func, void> final : public IReply {
    Func func;

public:
    explicit TReply(Func &&func)
        : func(std::forward<Func>(func)) {}
    ~TReply() override = default;
    void
    operator()(redisReply *raw) final {
        try {
            qb::redis::reply::parse<void>(*raw);
            func(Reply<void>{true, reply_ptr(raw)});
        } catch (...) {
            func(Reply<void>{false, reply_ptr(raw)});
        }
    }
};

template <typename Func>
class TReply<Func, bool> final : public IReply {
    Func func;

public:
    explicit TReply(Func &&func)
        : func(std::forward<Func>(func)) {}
    ~TReply() override = default;
    void
    operator()(redisReply *raw) final {
        try {
            func(Reply<bool>{qb::redis::reply::parse<bool>(*raw), reply_ptr(raw)});
        } catch (...) {
            func(Reply<bool>{false, reply_ptr(raw)});
        }
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_REPLY_H
