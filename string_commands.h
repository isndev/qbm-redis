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

#ifndef QBM_REDIS_STRING_COMMANDS_H
#define QBM_REDIS_STRING_COMMANDS_H
#include <bitset>
#include "reply.h"

namespace qb::redis {

template <typename Derived>
class string_commands {
public:
    long long
    append(const std::string &key, const std::string &val) {
        return static_cast<Derived &>(*this).template command<long long>("APPEND", key, val).result;
    }
    template <typename Func>
    Derived &
    append(Func &&func, const std::string &key, const std::string &val) {
        return static_cast<Derived &>(*this)
            .template command<long long>(std::forward<Func>(func), "APPEND", key, val);
    }

    long long
    bitcount(const std::string &key, long long start = 0, long long end = -1) {
        return static_cast<Derived &>(*this).template command<long long>("BITCOUNT", key, start, end).result;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    bitcount(Func &&func, const std::string &key, long long start = 0, long long end = -1) {
        return static_cast<Derived &>(*this)
            .template command<long long>(std::forward<Func>(func), "BITCOUNT", key, start, end);
    }

    template <typename... Keys>
    long long
    bitop(BitOp op, const std::string &destination, Keys &&...keys) {
        return static_cast<Derived &>(*this)
            .template command<long long>(
                "BITOP",
                std::to_string(op),
                destination,
                std::forward<Keys>(keys)...)
            .result;
    }
    template <typename Func, typename... Keys>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    bitop(Func &&func, BitOp op, const std::string &destination, Keys &&...keys) {
        return static_cast<Derived &>(*this).template command<long long>(
            std::forward<Func>(func),
            "BITOP",
            std::to_string(op),
            destination,
            std::forward<Keys>(keys)...);
    }

    long long
    bitpos(const std::string &key, long long bit, long long start = 0, long long end = -1) {
        return static_cast<Derived &>(*this)
            .template command<long long>("BITPOS", key, bit, start, end)
            .result;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    bitpos(Func &&func, const std::string &key, long long bit, long long start = 0, long long end = -1) {
        return static_cast<Derived &>(*this)
            .template command<long long>(std::forward<Func>(func), "BITPOS", key, bit, start, end);
    }

    long long
    decr(const std::string &key) {
        return static_cast<Derived &>(*this).template command<long long>("DECR", key).result;
    }
    template <typename Func>
    Derived &
    decr(Func &&func, const std::string &key) {
        return static_cast<Derived &>(*this).template command<long long>(
            std::forward<Func>(func),
            "DECR",
            key);
    }

    long long
    decrby(const std::string &key, long long decrement) {
        return static_cast<Derived &>(*this).template command<long long>("DECRBY", key, decrement).result;
    }
    template <typename Func>
    Derived &
    decrby(Func &&func, const std::string &key, long long decrement) {
        return static_cast<Derived &>(*this)
            .template command<long long>(std::forward<Func>(func), "DECRBY", key, decrement);
    }

    std::optional<std::string>
    get(const std::string &key) {
        return static_cast<Derived &>(*this).template command<std::optional<std::string>>("GET", key).result;
    }
    template <typename Func>
    Derived &
    get(Func &&func, const std::string &key) {
        return static_cast<Derived &>(*this).template command<std::optional<std::string>>(
            std::forward<Func>(func),
            "GET",
            key);
    }

    long long
    getbit(const std::string &key, long long offset) {
        return static_cast<Derived &>(*this).template command<long long>("GETBIT", key, offset).result;
    }
    template <typename Func>
    Derived &
    getbit(Func &&func, const std::string &key, long long offset) {
        return static_cast<Derived &>(*this)
            .template command<long long>(std::forward<Func>(func), "GETBIT", key, offset);
    }

    std::string
    getrange(const std::string &key, long long start, long long end) {
        return static_cast<Derived &>(*this)
            .template command<std::string>("GETRANGE", key, start, end)
            .result;
    }
    template <typename Func>
    Derived &
    getrange(Func &&func, const std::string &key, long long start, long long end) {
        return static_cast<Derived &>(*this)
            .template command<std::string>(std::forward<Func>(func), "GETRANGE", key, start, end);
    }

    std::optional<std::string>
    getset(const std::string &key, const std::string &val) {
        return static_cast<Derived &>(*this)
            .template command<std::optional<std::string>>("GETSET", key, val)
            .result;
    }
    template <typename Func>
    Derived &
    getset(Func &&func, const std::string &key, const std::string &val) {
        return static_cast<Derived &>(*this)
            .template command<std::optional<std::string>>(std::forward<Func>(func), "GETSET", key, val);
    }

    long long
    incr(const std::string &key) {
        return static_cast<Derived &>(*this).template command<long long>("INCR", key).result;
    }
    template <typename Func>
    Derived &
    incr(Func &&func, const std::string &key) {
        return static_cast<Derived &>(*this).template command<long long>(
            std::forward<Func>(func),
            "INCR",
            key);
    }

    long long
    incrby(const std::string &key, long long increment) {
        return static_cast<Derived &>(*this).template command<long long>("INCRBY", key, increment).result;
    }
    template <typename Func>
    Derived &
    incrby(Func &&func, const std::string &key, long long increment) {
        return static_cast<Derived &>(*this)
            .template command<long long>(std::forward<Func>(func), "INCRBY", key, increment);
    }

    double
    incrbyfloat(const std::string &key, double increment) {
        return static_cast<Derived &>(*this).template command<double>("INCRBYFLOAT", key, increment).result;
    }
    template <typename Func>
    Derived &
    incrbyfloat(Func &&func, const std::string &key, double increment) {
        return static_cast<Derived &>(*this)
            .template command<double>(std::forward<Func>(func), "INCRBYFLOAT", key, increment);
    }

    template <typename... Keys>
    std::vector<std::optional<std::string>>
    mget(Keys &&...keys) {
        return static_cast<Derived &>(*this)
            .template command<std::vector<std::optional<std::string>>>("MGET", std::forward<Keys>(keys)...)
            .result;
    }
    template <typename Func, typename... Keys>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::optional<std::string>>> &&>, Derived &>
    mget(Func &&func, Keys &&...keys) {
        return static_cast<Derived &>(*this).template command<std::vector<std::optional<std::string>>>(
            std::forward<Func>(func),
            "MGET",
            std::forward<Keys>(keys)...);
    }

    template <typename... Keys>
    bool
    mset(Keys &&...keys) {
        return static_cast<Derived &>(*this).template command<void>("MSET", std::forward<Keys>(keys)...).ok;
    }
    template <typename Func, typename... Keys>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    mset(Func &&func, Keys &&...keys) {
        return static_cast<Derived &>(*this).template command<void>(
            std::forward<Func>(func),
            "MSET",
            std::forward<Keys>(keys)...);
    }

    template <typename... Keys>
    bool
    msetnx(Keys &&...keys) {
        return static_cast<Derived &>(*this)
            .template command<bool>("MSETNX", std::forward<Keys>(keys)...)
            .ok;
    }
    template <typename Func, typename... Keys>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    msetnx(Func &&func, Keys &&...keys) {
        return static_cast<Derived &>(*this).template command<bool>(
            std::forward<Func>(func),
            "MSETNX",
            std::forward<Keys>(keys)...);
    }

    bool
    psetex(const std::string &key, long long ttl, const std::string &val) {
        return static_cast<Derived &>(*this).template command<void>("PSETEX", key, ttl, val).ok;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<void> &&>, Derived &>
    psetex(Func &&func, const std::string &key, long long ttl, const std::string &val) {
        return static_cast<Derived &>(*this)
            .template command<void>(std::forward<Func>(func), "PSETEX", key, ttl, val);
    }

    bool
    psetex(const std::string &key, std::chrono::milliseconds const &ttl, const std::string &val) {
        return psetex(key, ttl.count(), val);
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<void> &&>, Derived &>
    psetex(
        Func &&func, const std::string &key, std::chrono::milliseconds const &ttl, const std::string &val) {
        return psetex(std::forward<Func>(func), key, ttl.count(), val);
    }

    bool
    set(const std::string &key, const std::string &val, UpdateType type = UpdateType::ALWAYS) {
        std::optional<std::string> opt;
        if (type != UpdateType::ALWAYS)
            opt = std::to_string(type);

        return static_cast<Derived &>(*this).template command<reply::set>("SET", key, val, opt).ok;
    }
    template <typename Func>
    Derived &
    set(Func &&func, const std::string &key, const std::string &val, UpdateType type = UpdateType::ALWAYS) {
        std::optional<std::string> opt;
        if (type != UpdateType::ALWAYS)
            opt = std::to_string(type);

        return static_cast<Derived &>(*this)
            .template command<reply::set>(std::forward<Func>(func), "SET", key, val, opt);
    }

    bool
    set(const std::string &key, const std::string &val, long long ttl,
        UpdateType type = UpdateType::ALWAYS) {
        std::optional<std::string> opt;
        if (type != UpdateType::ALWAYS)
            opt = std::to_string(type);
        return static_cast<Derived &>(*this)
            .template command<reply::set>("SET", key, val, "PX", ttl, opt)
            .ok;
    }
    template <typename Func>
    Derived &
    set(Func &&func, const std::string &key, const std::string &val, long long ttl,
        UpdateType type = UpdateType::ALWAYS) {
        std::optional<std::string> opt;
        if (type != UpdateType::ALWAYS)
            opt = std::to_string(type);
        return static_cast<Derived &>(*this)
            .template command<reply::set>(std::forward<Func>(func), "SET", key, val, "PX", ttl, opt);
    }

    bool
    set(const std::string &key, const std::string &val, const std::chrono::milliseconds &ttl,
        UpdateType type = UpdateType::ALWAYS) {
        return set(key, val, static_cast<long long>(ttl.count()), type);
    }
    template <typename Func>
    Derived &
    set(Func &&func, const std::string &key, const std::string &val, const std::chrono::milliseconds &ttl,
        UpdateType type = UpdateType::ALWAYS) {
        return set(std::forward<Func>(func), key, val, static_cast<long long>(ttl.count()), type);
    }

    bool
    set(const std::string &key, const std::string &val, bool keepttl, UpdateType type = UpdateType::ALWAYS) {
        std::optional<std::string> opt;
        if (keepttl)
            opt = "KEEPTTL";
        std::optional<std::string> opt2;
        if (type != UpdateType::ALWAYS)
            opt2 = std::to_string(type);
        return static_cast<Derived &>(*this).template command<reply::set>("SET", key, val, opt, opt2).ok;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<reply::set> &&>, Derived &>
    set(Func &&func, const std::string &key, const std::string &val, bool keepttl,
        UpdateType type = UpdateType::ALWAYS) {
        std::optional<std::string> opt;
        if (keepttl)
            opt = "KEEPTTL";
        std::optional<std::string> opt2;
        if (type != UpdateType::ALWAYS)
            opt2 = std::to_string(type);
        return static_cast<Derived &>(*this)
            .template command<reply::set>(std::forward<Func>(func), "SET", key, val, opt, opt2);
    }

    bool
    setex(const std::string &key, long long ttl, const std::string &val) {
        return static_cast<Derived &>(*this).template command<void>("SETEX", key, ttl, val).ok;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<void> &&>, Derived &>
    setex(Func &&func, const std::string &key, long long ttl, const std::string &val) {
        return static_cast<Derived &>(*this)
            .template command<void>(std::forward<Func>(func), "SETEX", key, ttl, val);
    }

    bool
    setex(const std::string &key, std::chrono::seconds const &ttl, const std::string &val) {
        return setex(key, ttl.count(), val);
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<void> &&>, Derived &>
    setex(Func &&func, const std::string &key, std::chrono::seconds const &ttl, const std::string &val) {
        return setex(std::forward<Func>(func), key, ttl.count(), val);
    }

    bool
    setnx(const std::string &key, const std::string &val) {
        return static_cast<Derived &>(*this).template command<bool>("SETNX", key, val).ok;
    }
    template <typename Func>
    Derived &
    setnx(Func &&func, const std::string &key, const std::string &val) {
        return static_cast<Derived &>(*this)
            .template command<bool>(std::forward<Func>(func), "SETNX", key, val);
    }

    long long
    setrange(const std::string &key, long long offset, const std::string &val) {
        return static_cast<Derived &>(*this)
            .template command<long long>("SETRANGE", key, offset, val)
            .result;
    }
    template <typename Func>
    Derived &
    setrange(Func &&func, const std::string &key, long long offset, const std::string &val) {
        return static_cast<Derived &>(*this)
            .template command<long long>(std::forward<Func>(func), "SETRANGE", key, offset, val);
    }

    long long
    setbit(const std::string &key, long long offset, long long value) {
        return static_cast<Derived &>(*this)
            .template command<long long>("SETBIT", key, offset, value)
            .result;
    }
    template <typename Func>
    Derived &
    setbit(Func &&func, const std::string &key, long long offset, long long value) {
        return static_cast<Derived &>(*this)
            .template command<long long>(std::forward<Func>(func), "SETBIT", key, offset, value);
    }

    template <std::size_t NB_BITS>
    long long
    setbit(const std::string &key, long long offset, std::bitset<NB_BITS> const &bits) {
        auto ret = 0ll;
        for (auto i = 0; i < NB_BITS; ++i) {
            if (setbit(key, offset++, bits[i]))
                ret++;
        }
        return ret;
    }
    template <typename Func, std::size_t NB_BITS>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    setbit(Func &&func, const std::string &key, long long offset, std::bitset<NB_BITS> const &bits) {
        auto ret = new long long{0};
        for (auto i = 0; i < NB_BITS; ++i) {
            setbit(
                [ret](auto &&r) {
                    if (r.ok)
                        ++(*ret);
                },
                key,
                offset++,
                bits[i]);
        }
        return static_cast<Derived &>(*this).ping([ret, func = std::forward<Func>(func)](auto &&r) {
            func(redis::Reply<long long>{*ret == NB_BITS, *ret, nullptr});
            delete ret;
        });
    }

    long long
    strlen(const std::string &key) {
        return static_cast<Derived &>(*this).template command<long long>("STRLEN", key).result;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    strlen(Func &&func, const std::string &key) {
        return static_cast<Derived &>(*this).template command<long long>(
            std::forward<Func>(func),
            "STRLEN",
            key);
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_STRING_COMMANDS_H
