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

#ifndef QBM_REDIS_LIST_COMMANDS_H
#define QBM_REDIS_LIST_COMMANDS_H
#include <chrono>
#include "reply.h"

namespace qb::redis {

template <typename Derived>
class list_commands {
public:
    template <typename... Keys>
    std::optional<std::pair<std::string, std::string>>
    blpop(long long timeout, Keys &&...keys) {
        return static_cast<Derived &>(*this)
            .template command<std::optional<std::pair<std::string, std::string>>>(
                "BLPOP",
                std::forward<Keys>(keys)...,
                timeout)
            .result;
    }
    template <typename Func, typename... Keys>
    std::enable_if_t<
        std::is_invocable_v<Func, Reply<std::optional<std::pair<std::string, std::string>>> &&>, Derived &>
    blpop(Func &&func, long long timeout, Keys &&...keys) {
        return static_cast<Derived &>(*this)
            .template command<std::optional<std::pair<std::string, std::string>>>(
                std::forward<Func>(func),
                "BLPOP",
                std::forward<Keys>(keys)...,
                timeout);
    }

    template <typename... Keys>
    std::optional<std::pair<std::string, std::string>>
    blpop(const std::chrono::seconds &timeout, Keys &&...keys) {
        return blpop(timeout.count(), std::forward<Keys>(keys)...);
    }
    template <typename Func, typename... Keys>
    std::enable_if_t<
        std::is_invocable_v<Func, Reply<std::optional<std::pair<std::string, std::string>>> &&>, Derived &>
    blpop(Func &&func, const std::chrono::seconds &timeout, Keys &&...keys) {
        return blpop(std::forward<Func>(func), timeout.count(), std::forward<Keys>(keys)...);
    }

    template <typename... Keys>
    std::optional<std::pair<std::string, std::string>>
    brpop(long long timeout, Keys &&...keys) {
        return static_cast<Derived &>(*this)
            .template command<std::optional<std::pair<std::string, std::string>>>(
                "BRPOP",
                std::forward<Keys>(keys)...,
                timeout)
            .result;
    }
    template <typename Func, typename... Keys>
    std::enable_if_t<
        std::is_invocable_v<Func, Reply<std::optional<std::pair<std::string, std::string>>> &&>, Derived &>
    brpop(Func &&func, long long timeout, Keys &&...keys) {
        return static_cast<Derived &>(*this)
            .template command<std::optional<std::pair<std::string, std::string>>>(
                std::forward<Func>(func),
                "BRPOP",
                std::forward<Keys>(keys)...,
                timeout);
    }

    template <typename... Keys>
    std::optional<std::pair<std::string, std::string>>
    brpop(const std::chrono::seconds &timeout, Keys &&...keys) {
        return brpop(timeout.count(), std::forward<Keys>(keys)...);
    }
    template <typename Func, typename... Keys>
    std::enable_if_t<
        std::is_invocable_v<Func, Reply<std::optional<std::pair<std::string, std::string>>> &&>, Derived &>
    brpop(Func &&func, const std::chrono::seconds &timeout, Keys &&...keys) {
        return brpop(std::forward<Func>(func), timeout.count(), std::forward<Keys>(keys)...);
    }

    std::optional<std::string>
    brpoplpush(const std::string &source, const std::string &destination, long long timeout) {
        return static_cast<Derived &>(*this)
            .template command<std::optional<std::string>>("BRPOPLPUSH", source, destination, timeout)
            .result;
    }
    template <typename Func>
    Derived &
    brpoplpush(Func &&func, const std::string &source, const std::string &destination, long long timeout) {
        return static_cast<Derived &>(*this).template command<std::optional<std::string>>(
            std::forward<Func>(func),
            "BRPOPLPUSH",
            source,
            destination,
            timeout);
    }

    std::optional<std::string>
    brpoplpush(
        const std::string &source, const std::string &destination,
        const std::chrono::seconds &timeout = std::chrono::seconds{0}) {
        return brpoplpush(source, destination, timeout.count());
    }
    template <typename Func>
    Derived &
    brpoplpush(
        Func &&func, const std::string &source, const std::string &destination,
        const std::chrono::seconds &timeout = std::chrono::seconds{0}) {
        return brpoplpush(std::forward<Func>(func), source, destination, timeout.count());
    }

    std::optional<std::string>
    lindex(const std::string &key, long long index) {
        return static_cast<Derived &>(*this)
            .template command<std::optional<std::string>>("LINDEX", key, index)
            .result;
    }
    template <typename Func>
    Derived &
    lindex(Func &&func, const std::string &key, long long index) {
        return static_cast<Derived &>(*this)
            .template command<std::optional<std::string>>(std::forward<Func>(func), "LINDEX", key, index);
    }

    long long
    linsert(
        const std::string &key, InsertPosition position, const std::string &pivot, const std::string &val) {
        return static_cast<Derived &>(*this)
            .template command<long long>("LINSERT", key, std::to_string(position), pivot, val)
            .result;
    }
    template <typename Func>
    Derived &
    linsert(
        Func &&func, const std::string &key, InsertPosition position, const std::string &pivot,
        const std::string &val) {
        return static_cast<Derived &>(*this).template command<long long>(
            std::forward<Func>(func),
            "LINSERT",
            key,
            std::to_string(position),
            pivot,
            val);
    }

    long long
    llen(const std::string &key) {
        return static_cast<Derived &>(*this).template command<long long>("LLEN", key).result;
    }
    template <typename Func>
    Derived &
    llen(Func &&func, const std::string &key) {
        return static_cast<Derived &>(*this).template command<long long>(
            std::forward<Func>(func),
            "LLEN",
            key);
    }

    std::optional<std::string>
    lpop(const std::string &key) {
        return static_cast<Derived &>(*this)
            .template command<std::optional<std::string>>("LPOP", key)
            .result;
    }
    template <typename Func>
    Derived &
    lpop(Func &&func, const std::string &key) {
        return static_cast<Derived &>(*this).template command<std::optional<std::string>>(
            std::forward<Func>(func),
            "LPOP",
            key);
    }

    template <typename... Values>
    long long
    lpush(const std::string &key, Values &&...values) {
        return static_cast<Derived &>(*this)
            .template command<long long>("LPUSH", key, std::forward<Values>(values)...)
            .result;
    }
    template <typename Func, typename... Values>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    lpush(Func &&func, const std::string &key, Values &&...values) {
        return static_cast<Derived &>(*this).template command<long long>(
            std::forward<Func>(func),
            "LPUSH",
            key,
            std::forward<Values>(values)...);
    }

    long long
    lpushx(const std::string &key, const std::string &val) {
        return static_cast<Derived &>(*this).template command<long long>("LPUSHX", key, val).result;
    }
    template <typename Func>
    Derived &
    lpushx(Func &&func, const std::string &key, const std::string &val) {
        return static_cast<Derived &>(*this)
            .template command<long long>(std::forward<Func>(func), "LPUSHX", key, val);
    }

    std::vector<std::string>
    lrange(const std::string &key, long long start, long long stop) {
        return static_cast<Derived &>(*this)
            .template command<std::vector<std::string>>("LRANGE", key, start, stop)
            .result;
    }
    template <typename Func>
    Derived &
    lrange(Func &&func, const std::string &key, long long start, long long stop) {
        return static_cast<Derived &>(*this).template command<std::vector<std::string>>(
            std::forward<Func>(func),
            "LRANGE",
            key,
            start,
            stop);
    }

    long long
    lrem(const std::string &key, long long count, const std::string &val) {
        return static_cast<Derived &>(*this).template command<long long>("LREM", key, count, val).result;
    }
    template <typename Func>
    Derived &
    lrem(Func &&func, const std::string &key, long long count, const std::string &val) {
        return static_cast<Derived &>(*this)
            .template command<long long>(std::forward<Func>(func), "LREM", key, count, val);
    }

    bool
    lset(const std::string &key, long long index, const std::string &val) {
        return static_cast<Derived &>(*this).template command<void>("LSET", key, index, val).ok;
    }
    template <typename Func>
    Derived &
    lset(Func &&func, const std::string &key, long long index, const std::string &val) {
        return static_cast<Derived &>(*this)
            .template command<void>(std::forward<Func>(func), "LSET", key, index, val);
    }

    bool
    ltrim(const std::string &key, long long start, long long stop) {
        return static_cast<Derived &>(*this).template command<void>("LTRIM", key, start, stop).ok;
    }
    template <typename Func>
    Derived &
    ltrim(Func &&func, const std::string &key, long long start, long long stop) {
        return static_cast<Derived &>(*this)
            .template command<void>(std::forward<Func>(func), "LTRIM", key, start, stop);
    }

    std::optional<std::string>
    rpop(const std::string &key) {
        return static_cast<Derived &>(*this)
            .template command<std::optional<std::string>>("RPOP", key)
            .result;
    }
    template <typename Func>
    Derived &
    rpop(Func &&func, const std::string &key) {
        return static_cast<Derived &>(*this).template command<std::optional<std::string>>(
            std::forward<Func>(func),
            "RPOP",
            key);
    }

    std::optional<std::string>
    rpoplpush(const std::string &source, const std::string &destination) {
        return static_cast<Derived &>(*this)
            .template command<std::optional<std::string>>("RPOPLPUSH", source, destination)
            .result;
    }
    template <typename Func>
    Derived &
    rpoplpush(Func &&func, const std::string &source, const std::string &destination) {
        return static_cast<Derived &>(*this).template command<std::optional<std::string>>(
            std::forward<Func>(func),
            "RPOPLPUSH",
            source,
            destination);
    }

    template <typename... Values>
    long long
    rpush(const std::string &key, Values &&...values) {
        return static_cast<Derived &>(*this)
            .template command<long long>("RPUSH", key, std::forward<Values>(values)...)
            .result;
    }
    template <typename Func, typename... Values>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    rpush(Func &&func, const std::string &key, Values &&...values) {
        return static_cast<Derived &>(*this).template command<long long>(
            std::forward<Func>(func),
            "RPUSH",
            key,
            std::forward<Values>(values)...);
    }

    long long
    rpushx(const std::string &key, const std::string &val) {
        return static_cast<Derived &>(*this).template command<long long>("RPUSHX", key, val).result;
    }
    template <typename Func>
    Derived &
    rpushx(Func &&func, const std::string &key, const std::string &val) {
        return static_cast<Derived &>(*this)
            .template command<long long>(std::forward<Func>(func), "RPUSHX", key, val);
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_LIST_COMMANDS_H
