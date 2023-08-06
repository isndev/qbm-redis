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

#ifndef QBM_REDIS_SET_COMMANDS_H
#define QBM_REDIS_SET_COMMANDS_H
#include "reply.h"

namespace qb::redis {

template <typename Derived>
class set_commands {
    template <typename Func>
    class scanner {
        Derived &_handler;
        std::string _key;
        std::string _pattern;
        Func _func;
        qb::redis::Reply<qb::redis::reply::scan<>> _reply;

    public:
        scanner(Derived &handler, std::string key, std::string pattern, Func &&func)
            : _handler(handler)
            , _key(std::move(key))
            , _pattern(std::move(pattern))
            , _func(std::forward<Func>(func)) {
            _handler.sscan(std::ref(*this), _key, 0, _pattern, 100);
        }

        void
        operator()(qb::redis::Reply<qb::redis::reply::scan<>> &&reply) {
            _reply.ok = reply.ok;
            std::move(reply.result.items.begin(), reply.result.items.end(), std::back_inserter(_reply.result.items));
            if (reply.ok && reply.result.cursor)
                _handler.sscan(std::ref(*this), _key, reply.result.cursor, _pattern, 100);
            else {
                _func(std::move(_reply));
                delete this;
            }
        }
    };

public:
    template <typename... Members>
    long long
    sadd(const std::string &key, Members &&...members) {
        return static_cast<Derived &>(*this)
            .template command<long long>("SADD", key, std::forward<Members>(members)...)
            .result;
    }
    template <typename Func, typename... Members>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    sadd(Func &&func, const std::string &key, Members &&...members) {
        return static_cast<Derived &>(*this)
            .template command<long long>(std::forward<Func>(func), "SADD", key, std::forward<Members>(members)...);
    }

    long long
    scard(const std::string &key) {
        return static_cast<Derived &>(*this).template command<long long>("SCARD", key).result;
    }
    template <typename Func>
    Derived &
    scard(Func &&func, const std::string &key) {
        return static_cast<Derived &>(*this).template command<long long>(std::forward<Func>(func), "SCARD", key);
    }

    template <typename... Keys>
    std::vector<std::string>
    sdiff(Keys &&...keys) {
        return static_cast<Derived &>(*this)
            .template command<std::vector<std::string>>("SDIFF", std::forward<Keys>(keys)...)
            .result;
    }
    template <typename Func, typename... Keys>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>, Derived &>
    sdiff(Func &&func, Keys &&...keys) {
        return static_cast<Derived &>(*this).template command<std::vector<std::string>>(
            std::forward<Func>(func),
            "SDIFF",
            std::forward<Keys>(keys)...);
    }

    template <typename... Keys>
    long long
    sdiffstore(const std::string &destination, Keys &&...keys) {
        return static_cast<Derived &>(*this)
            .template command<long long>("SDIFFSTORE", destination, std::forward<Keys>(keys)...)
            .result;
    }
    template <typename Func, typename... Keys>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    sdiffstore(Func &&func, const std::string &destination, Keys &&...keys) {
        return static_cast<Derived &>(*this).template command<long long>(
            std::forward<Func>(func),
            "SDIFFSTORE",
            destination,
            std::forward<Keys>(keys)...);
    }

    template <typename... Keys>
    std::vector<std::string>
    sinter(Keys &&...keys) {
        return static_cast<Derived &>(*this)
            .template command<std::vector<std::string>>("SINTER", std::forward<Keys>(keys)...)
            .result;
    }
    template <typename Func, typename... Keys>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>, Derived &>
    sinter(Func &&func, Keys &&...keys) {
        return static_cast<Derived &>(*this).template command<std::vector<std::string>>(
            std::forward<Func>(func),
            "SINTER",
            std::forward<Keys>(keys)...);
    }

    template <typename... Keys>
    long long
    sinterstore(const std::string &destination, Keys &&...keys) {
        return static_cast<Derived &>(*this)
            .template command<long long>("SINTERSTORE", destination, std::forward<Keys>(keys)...)
            .result;
    }
    template <typename Func, typename... Keys>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    sinterstore(Func &&func, const std::string &destination, Keys &&...keys) {
        return static_cast<Derived &>(*this).template command<long long>(
            std::forward<Func>(func),
            "SINTERSTORE",
            destination,
            std::forward<Keys>(keys)...);
    }

    bool
    sismember(const std::string &key, const std::string &member) {
        return static_cast<Derived &>(*this).template command<bool>("SISMEMBER", key, member).ok;
    }
    template <typename Func>
    Derived &
    sismember(Func &&func, const std::string &key, const std::string &member) {
        return static_cast<Derived &>(*this).template command<bool>(std::forward<Func>(func), "SISMEMBER", key, member);
    }

    template <typename... Members>
    std::enable_if_t<sizeof...(Members), qb::unordered_set<std::optional<std::string>>>
    smembers(const std::string &key, Members &&...members) {
        return static_cast<Derived &>(*this)
            .template command<qb::unordered_set<std::optional<std::string>>>(
                "SMEMBERS",
                key,
                std::forward<Members>(members)...)
            .result;
    }
    template <typename Func, typename... Members>
    std::enable_if_t<
        sizeof...(Members) && std::is_invocable_v<Func, Reply<qb::unordered_set<std::optional<std::string>>> &&>,
        Derived &>
    smembers(Func &&func, const std::string &key, Members &&...members) {
        return static_cast<Derived &>(*this).template command<qb::unordered_set<std::optional<std::string>>>(
            std::forward<Func>(func),
            "SMEMBERS",
            key,
            std::forward<Members>(members)...);
    }

    qb::unordered_set<std::string>
    smembers(const std::string &key) {
        return static_cast<Derived &>(*this).template command<qb::unordered_set<std::string>>("SMEMBERS", key).result;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::unordered_set<std::string>> &&>, Derived &>
    smembers(Func &&func, const std::string &key) {
        return static_cast<Derived &>(*this).template command<qb::unordered_set<std::string>>(
            std::forward<Func>(func),
            "SMEMBERS",
            key);
    }

    bool
    smove(const std::string &source, const std::string &destination, const std::string &member) {
        return static_cast<Derived &>(*this).template command<bool>("SMOVE", source, destination, member).ok;
    }
    template <typename Func>
    Derived &
    smove(Func &&func, const std::string &source, const std::string &destination, const std::string &member) {
        return static_cast<Derived &>(*this)
            .template command<bool>(std::forward<Func>(func), "SMOVE", source, destination, member);
    }

    std::optional<std::string>
    spop(const std::string &key) {
        return static_cast<Derived &>(*this).template command<std::optional<std::string>>("SPOP", key).result;
    }
    template <typename Func>
    Derived &
    spop(Func &&func, const std::string &key) {
        return static_cast<Derived &>(*this).template command<std::optional<std::string>>(
            std::forward<Func>(func),
            "SPOP",
            key);
    }

    std::vector<std::string>
    spop(const std::string &key, long long count) {
        return static_cast<Derived &>(*this).template command<std::vector<std::string>>("SPOP", key, count).result;
    }
    template <typename Func>
    Derived &
    spop(Func &&func, const std::string &key, long long count) {
        return static_cast<Derived &>(*this)
            .template command<std::vector<std::string>>(std::forward<Func>(func), "SPOP", key, count);
    }

    std::optional<std::string>
    srandmember(const std::string &key) {
        return static_cast<Derived &>(*this).template command<std::optional<std::string>>("SRANDMEMBER", key).result;
    }
    template <typename Func>
    Derived &
    srandmember(Func &&func, const std::string &key) {
        return static_cast<Derived &>(*this).template command<std::optional<std::string>>(
            std::forward<Func>(func),
            "SRANDMEMBER",
            key);
    }

    std::vector<std::string>
    srandmember(const std::string &key, long long count) {
        return static_cast<Derived &>(*this)
            .template command<std::vector<std::string>>("SRANDMEMBER", key, count)
            .result;
    }
    template <typename Func>
    Derived &
    srandmember(Func &&func, const std::string &key, long long count) {
        return static_cast<Derived &>(*this)
            .template command<std::vector<std::string>>(std::forward<Func>(func), "SRANDMEMBER", key, count);
    }

    template <typename... Members>
    long long
    srem(const std::string &key, Members &&...members) {
        return static_cast<Derived &>(*this)
            .template command<long long>("SREM", key, std::forward<Members>(members)...)
            .result;
    }
    template <typename Func, typename... Members>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    srem(Func &&func, const std::string &key, Members &&...members) {
        return static_cast<Derived &>(*this)
            .template command<long long>(std::forward<Func>(func), "SREM", key, std::forward<Members>(members)...);
    }

    reply::scan<>
    sscan(const std::string &key, long long cursor, const std::string &pattern = "*", long long count = 10) {
        return static_cast<Derived &>(*this)
            .template command<reply::scan<>>("SSCAN", key, cursor, "MATCH", pattern, "COUNT", count)
            .result;
    }
    template <typename Func>
    Derived &
    sscan(
        Func &&func, const std::string &key, long long cursor, const std::string &pattern = "*", long long count = 10) {
        return static_cast<Derived &>(*this).template command<reply::scan<>>(
            std::forward<Func>(func),
            "SSCAN",
            key,
            cursor,
            "MATCH",
            pattern,
            "COUNT",
            count);
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<reply::scan<>> &&>, Derived &>
    sscan(Func &&func, const std::string &key, const std::string &pattern = "*") {
        new scanner<Func>(static_cast<Derived &>(*this), key, pattern, std::forward<Func>(func));
        return static_cast<Derived &>(*this);
    }

    template <typename... Keys>
    std::vector<std::string>
    sunion(Keys &&...keys) {
        return static_cast<Derived &>(*this)
            .template command<std::vector<std::string>>("SUNION", std::forward<Keys>(keys)...)
            .result;
    }
    template <typename Func, typename... Keys>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>, Derived &>
    sunion(Func &&func, Keys &&...keys) {
        return static_cast<Derived &>(*this).template command<std::vector<std::string>>(
            std::forward<Func>(func),
            "SUNION",
            std::forward<Keys>(keys)...);
    }

    template <typename... Keys>
    long long
    sunionstore(const std::string &destination, Keys &&...keys) {
        return static_cast<Derived &>(*this)
            .template command<long long>("SUNIONSTORE", destination, std::forward<Keys>(keys)...)
            .result;
    }
    template <typename Func, typename... Keys>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    sunionstore(Func &&func, const std::string &destination, Keys &&...keys) {
        return static_cast<Derived &>(*this).template command<long long>(
            std::forward<Func>(func),
            "SUNIONSTORE",
            destination,
            std::forward<Keys>(keys)...);
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_SET_COMMANDS_H
