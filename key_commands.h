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

#ifndef QBM_REDIS_KEY_COMMANDS_H
#define QBM_REDIS_KEY_COMMANDS_H
#include <chrono>
#include "reply.h"

namespace qb::redis {

template <typename Derived>
class key_commands {
public:
    /// @brief Delete the given list of keys.
    /// @param keys Keys to remove.
    /// @return Number of keys removed.
    /// @see https://redis.io/commands/del
    template <typename... Keys>
    long long
    del(Keys &&...keys) {
        return static_cast<Derived &>(*this)
            .template command<long long>("DEL", std::forward<Keys>(keys)...)
            .result;
    }
    template <typename Func, typename... Keys>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    del(Func &&func, Keys &&...keys) {
        return static_cast<Derived &>(*this).template command<long long>(
            std::forward<Func>(func),
            "DEL",
            std::forward<Keys>(keys)...);
    }

    /// @brief Get the serialized valued stored at key.
    /// @param key Key.
    /// @return The serialized value.
    /// @note If key does not exist, `dump` returns `OptionalString{}` (`std::nullopt`).
    /// @see https://redis.io/commands/dump
    std::optional<std::string>
    dump(const std::string &key) {
        return static_cast<Derived &>(*this)
            .template command<std::optional<std::string>>("DUMP", key)
            .result;
    }
    template <typename Func>
    Derived &
    dump(Func &&func, const std::string &key) {
        return static_cast<Derived &>(*this).template command<std::optional<std::string>>(
            std::forward<Func>(func),
            "DUMP",
            key);
    }

    /// @brief Check if the given keys exist.
    /// @param keys Keys to check if exist.
    /// @return Number of keys existing.
    /// @see https://redis.io/commands/exists
    template <typename... Keys>
    long long
    exists(Keys &&...keys) {
        return static_cast<Derived &>(*this)
            .template command<long long>("EXISTS", std::forward<Keys>(keys)...)
            .result;
    }
    template <typename Func, typename... Keys>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    exists(Func &&func, Keys &&...keys) {
        return static_cast<Derived &>(*this).template command<long long>(
            std::forward<Func>(func),
            "EXISTS",
            std::forward<Keys>(keys)...);
    }

    /// @brief Set a timeout on key.
    /// @param key Key.
    /// @param timeout Timeout in seconds.
    /// @return Whether timeout has been set.
    /// @retval true If timeout has been set.
    /// @retval false If key does not exist.
    /// @see https://redis.io/commands/expire
    bool
    expire(const std::string &key, long long timeout) {
        return static_cast<Derived &>(*this).template command<bool>("EXPIRE", key, timeout).ok;
    }
    template <typename Func>
    Derived &
    expire(Func &&func, const std::string &key, long long timeout) {
        return static_cast<Derived &>(*this)
            .template command<bool>(std::forward<Func>(func), "EXPIRE", key, timeout);
    }

    /// @brief Set a timeout on key.
    /// @param key Key.
    /// @param timeout Timeout in seconds.
    /// @return Whether timeout has been set.
    /// @retval true If timeout has been set.
    /// @retval false If key does not exist.
    /// @see https://redis.io/commands/expire
    bool
    expire(const std::string &key, const std::chrono::seconds &timeout) {
        return expire(key, timeout.count());
    }
    template <typename Func>
    Derived &
    expire(Func &&func, const std::string &key, const std::chrono::seconds &timeout) {
        return expire(std::forward<Func>(func), key, timeout.count());
    }

    /// @brief Set a timeout on key, i.e. expire the key at a future time point.
    /// @param key Key.
    /// @param timestamp Time in seconds since UNIX epoch.
    /// @return Whether timeout has been set.
    /// @retval true If timeout has been set.
    /// @retval false If key does not exist.
    /// @see https://redis.io/commands/expireat
    bool
    expireat(const std::string &key, long long timestamp) {
        return static_cast<Derived &>(*this).template command<bool>("EXPIREAT", key, timestamp).ok;
    }
    template <typename Func>
    Derived &
    expireat(Func &&func, const std::string &key, long long timestamp) {
        return static_cast<Derived &>(*this)
            .template command<bool>(std::forward<Func>(func), "EXPIREAT", key, timestamp);
    }

    /// @brief Set a timeout on key, i.e. expire the key at a future time point.
    /// @param key Key.
    /// @param timestamp Time in seconds since UNIX epoch.
    /// @return Whether timeout has been set.
    /// @retval true If timeout has been set.
    /// @retval false If key does not exist.
    /// @see https://redis.io/commands/expireat
    bool
    expireat(
        const std::string &key,
        const std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds> &tp) {
        return expireat(key, tp.time_since_epoch().count());
    }
    template <typename Func>
    Derived &
    expireat(
        Func &&func, const std::string &key,
        const std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds> &tp) {
        return expireat(std::forward<Func>(func), key, tp.time_since_epoch().count());
    }

    std::vector<std::string>
    keys(const std::string &pattern = "*") {
        return static_cast<Derived &>(*this)
            .template command<std::vector<std::string>>("KEYS", pattern)
            .result;
    }
    template <typename Func>
    Derived &
    keys(Func &&func, const std::string &pattern = "*") {
        return static_cast<Derived &>(*this).template command<std::vector<std::string>>(
            std::forward<Func>(func),
            "KEYS",
            pattern);
    }

    bool
    move(const std::string &key, long long db) {
        return static_cast<Derived &>(*this).template command<bool>("MOVE", key, db).ok;
    }
    template <typename Func>
    Derived &
    move(Func &&func, const std::string &key, long long db) {
        return static_cast<Derived &>(*this)
            .template command<bool>(std::forward<Func>(func), "MOVE", key, db);
    }

    bool
    persist(const std::string &key) {
        return static_cast<Derived &>(*this).template command<bool>("PERSIST", key).ok;
    }
    template <typename Func>
    Derived &
    persist(Func &&func, const std::string &key) {
        return static_cast<Derived &>(*this).template command<bool>(
            std::forward<Func>(func),
            "PERSIST",
            key);
    }

    bool
    pexpire(const std::string &key, long long timeout) {
        return static_cast<Derived &>(*this).template command<bool>("PEXPIRE", key, timeout).ok;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    pexpire(Func &&func, const std::string &key, long long timeout) {
        return static_cast<Derived &>(*this)
            .template command<bool>(std::forward<Func>(func), "PEXPIRE", key, timeout);
    }

    bool
    pexpire(const std::string &key, const std::chrono::milliseconds &timeout) {
        return pexpire(key, timeout.count());
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    pexpire(Func &&func, const std::string &key, const std::chrono::milliseconds &timeout) {
        return pexpire(std::forward<Func>(func), key, timeout.count());
    }

    bool
    pexpireat(const std::string &key, long long timestamp) {
        return static_cast<Derived &>(*this).template command<bool>("PEXPIREAT", key, timestamp).ok;
    }
    template <typename Func>
    Derived &
    pexpireat(Func &&func, const std::string &key, long long timestamp) {
        return static_cast<Derived &>(*this)
            .template command<bool>(std::forward<Func>(func), "PEXPIREAT", key, timestamp);
    }

    bool
    pexpireat(
        const std::string &key,
        const std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds> &tp) {
        return pexpireat(key, tp.time_since_epoch().count());
    }
    template <typename Func>
    Derived &
    pexpireat(
        Func &&func, const std::string &key,
        const std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds> &tp) {
        return pexpireat(std::forward<Func>(func), key, tp.time_since_epoch().count());
    }

    long long
    pttl(const std::string &key) {
        return static_cast<Derived &>(*this).template command<long long>("PTTL", key).result;
    }
    template <typename Func>
    Derived &
    pttl(Func &&func, const std::string &key) {
        return static_cast<Derived &>(*this).template command<long long>(
            std::forward<Func>(func),
            "PTTL",
            key);
    }

    std::optional<std::string>
    randomkey() {
        return static_cast<Derived &>(*this)
            .template command<std::optional<std::string>>("RANDOMKEY")
            .result;
    }
    template <typename Func>
    Derived &
    randomkey(Func &&func) {
        return static_cast<Derived &>(*this).template command<std::optional<std::string>>(
            std::forward<Func>(func),
            "RANDOMKEY");
    }

    bool
    rename(const std::string &key, const std::string &new_key) {
        return static_cast<Derived &>(*this).template command<void>("RENAME", key, new_key).ok;
    }
    template <typename Func>
    Derived &
    rename(Func &&func, const std::string &key, const std::string &new_key) {
        return static_cast<Derived &>(*this)
            .template command<void>(std::forward<Func>(func), "RENAME", key, new_key);
    }

    bool
    renamenx(const std::string &key, const std::string &new_key) {
        return static_cast<Derived &>(*this).template command<bool>("RENAMENX", key, new_key).ok;
    }
    template <typename Func>
    Derived &
    renamenx(Func &&func, const std::string &key, const std::string &new_key) {
        return static_cast<Derived &>(*this)
            .template command<bool>(std::forward<Func>(func), "RENAMENX", key, new_key);
    }

    bool
    restore(const std::string &key, const std::string &val, long long ttl, bool replace = false) {
        std::optional<std::string> opt;
        if (replace)
            opt = "REPLACE";
        return static_cast<Derived &>(*this).template command<void>("RESTORE", key, ttl, val, opt).ok;
    }
    template <typename Func>
    Derived &
    restore(
        Func &&func, const std::string &key, const std::string &val, long long ttl, bool replace = false) {
        std::optional<std::string> opt;
        if (replace)
            opt = "REPLACE";
        return static_cast<Derived &>(*this)
            .template command<void>(std::forward<Func>(func), "RESTORE", key, ttl, val, opt);
    }

    bool
    restore(
        const std::string &key, const std::string &val,
        const std::chrono::milliseconds &ttl = std::chrono::milliseconds{0}, bool replace = false) {
        return restore(key, val, ttl.count(), replace);
    }
    template <typename Func>
    Derived &
    restore(
        Func &&func, const std::string &key, const std::string &val,
        const std::chrono::milliseconds &ttl = std::chrono::milliseconds{0}, bool replace = false) {
        return restore(std::forward<Func>(func), key, val, ttl.count(), replace);
    }

    reply::scan<>
    scan(long long cursor, const std::string &pattern = "*", long long count = 10) {
        return static_cast<Derived &>(*this)
            .template command<reply::scan<>>("SCAN", cursor, "MATCH", pattern, "COUNT", count)
            .result;
    }
    template <typename Func>
    Derived &
    scan(Func &&func, long long cursor, const std::string &pattern = "*", long long count = 10) {
        return static_cast<Derived &>(*this).template command<reply::scan<>>(
            std::forward<Func>(func),
            "SCAN",
            cursor,
            "MATCH",
            pattern,
            "COUNT",
            count);
    }

    template <typename... Keys>
    long long
    touch(Keys &&...keys) {
        return static_cast<Derived &>(*this)
            .template command<long long>("TOUCH", std::forward<Keys>(keys)...)
            .result;
    }
    template <typename Func, typename... Keys>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    touch(Func &&func, Keys &&...keys) {
        return static_cast<Derived &>(*this).template command<long long>(
            std::forward<Func>(func),
            "TOUCH",
            std::forward<Keys>(keys)...);
    }

    long long
    ttl(const std::string &key) {
        return static_cast<Derived &>(*this).template command<long long>("TTL", key).result;
    }
    template <typename Func>
    Derived &
    ttl(Func &&func, const std::string &key) {
        return static_cast<Derived &>(*this).template command<long long>(
            std::forward<Func>(func),
            "TTL",
            key);
    }

    std::string
    type(const std::string &key) {
        return static_cast<Derived &>(*this).template command<std::string>("TYPE", key).result;
    }
    template <typename Func>
    Derived &
    type(Func &&func, const std::string &key) {
        return static_cast<Derived &>(*this).template command<std::string>(
            std::forward<Func>(func),
            "TYPE",
            key);
    }

    template <typename... Keys>
    long long
    unlink(Keys &&...keys) {
        return static_cast<Derived &>(*this)
            .template command<long long>("UNLINK", std::forward<Keys>(keys)...)
            .result;
    }
    template <typename Func, typename... Keys>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    unlink(Func &&func, Keys &&...keys) {
        return static_cast<Derived &>(*this).template command<long long>(
            std::forward<Func>(func),
            "UNLINK",
            std::forward<Keys>(keys)...);
    }

    long long
    wait(long long num_slaves, long long timeout) {
        return static_cast<Derived &>(*this).template command<long long>("WAIT", num_slaves, timeout).result;
    }
    template <typename Func>
    Derived &
    wait(Func &&func, long long num_slaves, long long timeout) {
        return static_cast<Derived &>(*this)
            .template command<long long>(std::forward<Func>(func), "WAIT", num_slaves, timeout);
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_KEY_COMMANDS_H
