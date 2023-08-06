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

#ifndef QBM_REDIS_HASH_COMMANDS_H
#define QBM_REDIS_HASH_COMMANDS_H
#include "reply.h"

namespace qb::redis {

template <typename Derived>
class hash_commands {
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
            _handler.hscan(std::ref(*this), _key, 0, _pattern, 100);
        }

        void
        operator()(qb::redis::Reply<qb::redis::reply::scan<>> &&reply) {
            _reply.ok = reply.ok;
            std::move(reply.result.items.begin(), reply.result.items.end(), std::back_inserter(_reply.result.items));
            if (reply.ok && reply.result.cursor)
                _handler.hscan(std::ref(*this), _key, reply.result.cursor, _pattern, 100);
            else {
                _func(std::move(_reply));
                delete this;
            }
        }
    };

    template <typename Func>
    class multi_hvals {
        const std::vector<std::string> _keys;
        Func _func;
        Reply<std::vector<std::string>> _reply;

    public:
        multi_hvals(Derived &handler, std::vector<std::string> keys, Func &&func)
            : _keys(std::move(keys))
            , _func(std::forward<Func>(func))
            , _reply{true} {
            if (!_keys.empty()) {
                for (auto it = _keys.begin(); it != std::end(_keys); ++it) {
                    handler.hvals(
                        [this, is_last = ((it + 1) == _keys.end())](auto &&reply) {
                            _reply.ok &= reply.ok;
                            std::move(reply.result.begin(), reply.result.end(), std::back_inserter(_reply.result));
                            if (is_last) {
                                this->_func(std::move(_reply));
                                delete this;
                            }
                        },
                        *it);
                }
            } else {
                this->_func(std::move(_reply));
                delete this;
            }
        }
    };

public:
    template <typename... Fields>
    long long
    hdel(const std::string &key, Fields &&...fields) {
        return static_cast<Derived &>(*this)
            .template command<long long>("HDEL", key, std::forward<Fields>(fields)...)
            .result;
    }
    template <typename Func, typename... Fields>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    hdel(Func &&func, const std::string &key, Fields &&...fields) {
        return static_cast<Derived &>(*this)
            .template command<long long>(std::forward<Func>(func), "HDEL", key, std::forward<Fields>(fields)...);
    }

    bool
    hexists(const std::string &key, const std::string &field) {
        return static_cast<Derived &>(*this).template command<bool>("HEXISTS", key, field).ok;
    }
    template <typename Func>
    Derived &
    hexists(Func &&func, const std::string &key, const std::string &field) {
        return static_cast<Derived &>(*this).template command<bool>(std::forward<Func>(func), "HEXISTS", key, field);
    }

    std::optional<std::string>
    hget(const std::string &key, const std::string &field) {
        return static_cast<Derived &>(*this).template command<std::optional<std::string>>("HGET", key, field).result;
    }
    template <typename Func>
    Derived &
    hget(Func &&func, const std::string &key, const std::string &field) {
        return static_cast<Derived &>(*this)
            .template command<std::optional<std::string>>(std::forward<Func>(func), "HGET", key, field);
    }

    qb::unordered_map<std::string, std::string>
    hgetall(const std::string &key) {
        return static_cast<Derived &>(*this)
            .template command<qb::unordered_map<std::string, std::string>>("HGETALL", key)
            .result;
    }
    template <typename Func>
    Derived &
    hgetall(Func &&func, const std::string &key) {
        return static_cast<Derived &>(*this).template command<qb::unordered_map<std::string, std::string>>(
            std::forward<Func>(func),
            "HGETALL",
            key);
    }

    long long
    hincrby(const std::string &key, const std::string &field, long long increment) {
        return static_cast<Derived &>(*this).template command<long long>("HINCRBY", key, field, increment).result;
    }
    template <typename Func>
    Derived &
    hincrby(Func &&func, const std::string &key, const std::string &field, long long increment) {
        return static_cast<Derived &>(*this)
            .template command<long long>(std::forward<Func>(func), "HINCRBY", key, field, increment);
    }

    double
    hincrbyfloat(const std::string &key, const std::string &field, double increment) {
        return static_cast<Derived &>(*this).template command<double>("HINCRBYFLOAT", key, field, increment).result;
    }
    template <typename Func>
    Derived &
    hincrbyfloat(Func &&func, const std::string &key, const std::string &field, double increment) {
        return static_cast<Derived &>(*this)
            .template command<double>(std::forward<Func>(func), "HINCRBYFLOAT", key, field, increment);
    }

    std::vector<std::string>
    hkeys(const std::string &pattern = "*") {
        return static_cast<Derived &>(*this).template command<std::vector<std::string>>("HKEYS", pattern).result;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>, Derived &>
    hkeys(Func &&func, const std::string &pattern = "*") {
        return static_cast<Derived &>(*this).template command<std::vector<std::string>>(
            std::forward<Func>(func),
            "HKEYS",
            pattern);
    }

    long long
    hlen(const std::string &key) {
        return static_cast<Derived &>(*this).template command<long long>("HLEN", key).result;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    hlen(Func &&func, const std::string &key) {
        return static_cast<Derived &>(*this).template command<long long>(std::forward<Func>(func), "HLEN", key);
    }

    template <typename... Fields>
    std::vector<std::optional<std::string>>
    hmget(const std::string &key, Fields &&...fields) {
        return static_cast<Derived &>(*this)
            .template command<std::vector<std::optional<std::string>>>("HMGET", key, std::forward<Fields>(fields)...)
            .result;
    }
    template <typename Func, typename... Fields>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::optional<std::string>>> &&>, Derived &>
    hmget(Func &&func, const std::string &key, Fields &&...fields) {
        return static_cast<Derived &>(*this).template command<std::vector<std::optional<std::string>>>(
            std::forward<Func>(func),
            "HMGET",
            key,
            std::forward<Fields>(fields)...);
    }

    template <typename... FieldValues>
    bool
    hmset(const std::string &key, FieldValues &&...field_values) {
        return static_cast<Derived &>(*this)
            .template command<void>("HMSET", key, std::forward<FieldValues>(field_values)...)
            .ok;
    }
    template <typename Func, typename... FieldValues>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    hmset(Func &&func, const std::string &key, FieldValues &&...field_values) {
        return static_cast<Derived &>(*this)
            .template command<void>(std::forward<Func>(func), "HMSET", key, std::forward<FieldValues>(field_values)...);
    }

    template <typename Out = std::vector<std::string>>
    reply::scan<Out>
    hscan(const std::string &key, long long cursor, const std::string &pattern = "*", long long count = 10) {
        return static_cast<Derived &>(*this)
            .template command<reply::scan<Out>>("HSCAN", key, cursor, "MATCH", pattern, "COUNT", count)
            .result;
    }
    template <typename Func, typename Out = std::vector<std::string>>
    Derived &
    hscan(
        Func &&func, const std::string &key, long long cursor, const std::string &pattern = "*", long long count = 10) {
        return static_cast<Derived &>(*this).template command<reply::scan<Out>>(
            std::forward<Func>(func),
            "HSCAN",
            key,
            cursor,
            "MATCH",
            pattern,
            "COUNT",
            count);
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<reply::scan<>> &&>, Derived &>
    hscan(Func &&func, const std::string &key, const std::string &pattern = "*") {
        new scanner<Func>(static_cast<Derived &>(*this), key, pattern, std::forward<Func>(func));
        return static_cast<Derived &>(*this);
    }

    long long
    hset(const std::string &key, const std::string &field, const std::string &val) {
        return static_cast<Derived &>(*this).template command<long long>("HSET", key, field, val).result;
    }
    template <typename Func>
    Derived &
    hset(Func &&func, const std::string &key, const std::string &field, const std::string &val) {
        return static_cast<Derived &>(*this)
            .template command<long long>(std::forward<Func>(func), "HSET", key, field, val);
    }

    bool
    hset(const std::string &key, const std::pair<std::string, std::string> &item) {
        return hset(key, item.first, item.second);
    }
    template <typename Func>
    Derived &
    hset(Func &&func, const std::string &key, const std::pair<std::string, std::string> &item) {
        return hset(std::forward<Func>(func), key, item.first, item.second);
    }

    bool
    hsetnx(const std::string &key, const std::string &field, const std::string &val) {
        return static_cast<Derived &>(*this).template command<bool>("HSETNX", key, field, val).ok;
    }
    template <typename Func>
    Derived &
    hsetnx(Func &&func, const std::string &key, const std::string &field, const std::string &val) {
        return static_cast<Derived &>(*this)
            .template command<bool>(std::forward<Func>(func), "HSETNX", key, field, val);
    }

    bool
    hsetnx(const std::string &key, const std::pair<std::string, std::string> &item) {
        return hsetnx(key, item.first, item.second);
    }
    template <typename Func>
    Derived &
    hsetnx(Func &&func, const std::string &key, const std::pair<std::string, std::string> &item) {
        return hsetnx(std::forward<Func>(func), key, item.first, item.second);
    }

    long long
    hstrlen(const std::string &key, const std::string &field) {
        return static_cast<Derived &>(*this).template command<long long>("HSTRLEN", key, field).result;
    }
    template <typename Func>
    Derived &
    hstrlen(Func &&func, const std::string &key, const std::string &field) {
        return static_cast<Derived &>(*this)
            .template command<long long>(std::forward<Func>(func), "HSTRLEN", key, field);
    }

    std::vector<std::string>
    hvals(const std::string &key) {
        return static_cast<Derived &>(*this).template command<std::vector<std::string>>("HVALS", key).result;
    }
    template <typename Func>
    Derived &
    hvals(Func &&func, const std::string &key) {
        return static_cast<Derived &>(*this).template command<std::vector<std::string>>(
            std::forward<Func>(func),
            "HVALS",
            key);
    }
    template <typename Func>
    Derived &
    hvals(Func &&func, std::vector<std::string> keys) {
        new multi_hvals<Func>(static_cast<Derived &>(*this), std::move(keys), std::forward<Func>(func));
        return static_cast<Derived &>(*this);
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_HASH_COMMANDS_H
