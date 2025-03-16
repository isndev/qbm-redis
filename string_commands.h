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
    /// @brief Append the given string to the string stored at key.
    /// @param key Key.
    /// @param str String to be appended.
    /// @return The length of the string after the append operation.
    /// @see https://redis.io/commands/append
    long long
    append(const std::string &key, const std::string &val) {
        return static_cast<Derived &>(*this).template command<long long>("APPEND", key, val).result;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    append(Func &&func, const std::string &key, const std::string &val) {
        return static_cast<Derived &>(*this).template command<long long>(std::forward<Func>(func), "APPEND", key, val);
    }

    /// @brief Get the number of bits that have been set for the given range of the string.
    /// @param key Key.
    /// @param start Start index (inclusive) of the range. 0 means the beginning of the string.
    /// @param end End index (inclusive) of the range. -1 means the end of the string.
    /// @return Number of bits that have been set.
    /// @note The index can be negative to index from the end of the string.
    /// @see https://redis.io/commands/bitcount
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

    /// @brief Do bit operation on the string stored at `key`, and save the result to `destination`.
    /// @param op Bit operations.
    /// @param destination The destination key where the result is saved.
    /// @param keys Keys, variadic(could be a string, or a container of keys)... .
    /// @return The length of the string saved at `destination`.
    /// @see https://redis.io/commands/bitop
    /// @see `BitOp`
    template <typename... Keys>
    long long
    bitop(BitOp op, const std::string &destination, Keys &&...keys) {
        return static_cast<Derived &>(*this)
            .template command<long long>("BITOP", std::to_string(op), destination, std::forward<Keys>(keys)...)
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

    /// @brief Get the position of the first bit set to 0 or 1 in the given range of the string.
    /// @param key Key.
    /// @param bit 0 or 1.
    /// @param start Start index (inclusive) of the range. 0 means the beginning of the string.
    /// @param end End index (inclusive) of the range. -1 means the end of the string.
    /// @return The position of the first bit set to 0 or 1.
    /// @see https://redis.io/commands/bitpos
    long long
    bitpos(const std::string &key, long long bit, long long start = 0, long long end = -1) {
        return static_cast<Derived &>(*this).template command<long long>("BITPOS", key, bit, start, end).result;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    bitpos(Func &&func, const std::string &key, long long bit, long long start = 0, long long end = -1) {
        return static_cast<Derived &>(*this)
            .template command<long long>(std::forward<Func>(func), "BITPOS", key, bit, start, end);
    }

    /// @brief Decrement the integer stored at key by 1.
    /// @param key Key.
    /// @return The value after the decrement.
    /// @see https://redis.io/commands/decr
    long long
    decr(const std::string &key) {
        return static_cast<Derived &>(*this).template command<long long>("DECR", key).result;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    decr(Func &&func, const std::string &key) {
        return static_cast<Derived &>(*this).template command<long long>(std::forward<Func>(func), "DECR", key);
    }

    /// @brief Decrement the integer stored at key by `decrement`.
    /// @param key Key.
    /// @param decrement Decrement.
    /// @return The value after the decrement.
    /// @see https://redis.io/commands/decrby
    long long
    decrby(const std::string &key, long long decrement) {
        return static_cast<Derived &>(*this).template command<long long>("DECRBY", key, decrement).result;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    decrby(Func &&func, const std::string &key, long long decrement) {
        return static_cast<Derived &>(*this)
            .template command<long long>(std::forward<Func>(func), "DECRBY", key, decrement);
    }

    /// @brief Get the string value stored at key.
    ///
    /// Example:
    /// @code{.cpp}
    /// auto val = redis.get("key");
    /// if (val)
    ///     std::cout << *val << std::endl;
    /// else
    ///     std::cout << "key not exist" << std::endl;
    /// @endcode
    /// @param key Key.
    /// @return The value stored at key.
    /// @note If key does not exist, `get` returns `std::optional<std::string>{}` (`std::nullopt`).
    /// @see https://redis.io/commands/get
    std::optional<std::string>
    get(const std::string &key) {
        return static_cast<Derived &>(*this).template command<std::optional<std::string>>("GET", key).result;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::string>> &&>, Derived &>
    get(Func &&func, const std::string &key) {
        return static_cast<Derived &>(*this).template command<std::optional<std::string>>(
            std::forward<Func>(func),
            "GET",
            key);
    }

    /// @brief Get the bit value at offset in the string.
    /// @param key Key.
    /// @param offset Offset.
    /// @return The bit value.
    /// @see https://redis.io/commands/getbit
    long long
    getbit(const std::string &key, long long offset) {
        return static_cast<Derived &>(*this).template command<long long>("GETBIT", key, offset).result;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    getbit(Func &&func, const std::string &key, long long offset) {
        return static_cast<Derived &>(*this)
            .template command<long long>(std::forward<Func>(func), "GETBIT", key, offset);
    }

    /// @brief Get the substring of the string stored at key.
    /// @param key Key.
    /// @param start Start index (inclusive) of the range. 0 means the beginning of the string.
    /// @param end End index (inclusive) of the range. -1 means the end of the string.
    /// @return The substring in range [start, end]. If key does not exist, return an empty string.
    /// @see https://redis.io/commands/getrange
    std::string
    getrange(const std::string &key, long long start, long long end) {
        return static_cast<Derived &>(*this).template command<std::string>("GETRANGE", key, start, end).result;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::string> &&>, Derived &>
    getrange(Func &&func, const std::string &key, long long start, long long end) {
        return static_cast<Derived &>(*this)
            .template command<std::string>(std::forward<Func>(func), "GETRANGE", key, start, end);
    }

    /// @brief Atomically set the string stored at `key` to `val`, and return the old value.
    /// @param key Key.
    /// @param val Value to be set.
    /// @return The old value stored at key.
    /// @note If key does not exist, `getset` returns `std::optional<std::string>{}` (`std::nullopt`).
    /// @see https://redis.io/commands/getset
    /// @see `std::optional<std::string>`
    std::optional<std::string>
    getset(const std::string &key, const std::string &val) {
        return static_cast<Derived &>(*this).template command<std::optional<std::string>>("GETSET", key, val).result;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::string>> &&>, Derived &>
    getset(Func &&func, const std::string &key, const std::string &val) {
        return static_cast<Derived &>(*this)
            .template command<std::optional<std::string>>(std::forward<Func>(func), "GETSET", key, val);
    }

    /// @brief Increment the integer stored at key by 1.
    /// @param key Key.
    /// @return The value after the increment.
    /// @see https://redis.io/commands/incr
    long long
    incr(const std::string &key) {
        return static_cast<Derived &>(*this).template command<long long>("INCR", key).result;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    incr(Func &&func, const std::string &key) {
        return static_cast<Derived &>(*this).template command<long long>(std::forward<Func>(func), "INCR", key);
    }

    /// @brief Increment the integer stored at key by `increment`.
    /// @param key Key.
    /// @param increment Increment.
    /// @return The value after the increment.
    /// @see https://redis.io/commands/incrby
    long long
    incrby(const std::string &key, long long increment) {
        return static_cast<Derived &>(*this).template command<long long>("INCRBY", key, increment).result;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    incrby(Func &&func, const std::string &key, long long increment) {
        return static_cast<Derived &>(*this)
            .template command<long long>(std::forward<Func>(func), "INCRBY", key, increment);
    }

    /// @brief Increment the floating point number stored at key by `increment`.
    /// @param key Key.
    /// @param increment Increment.
    /// @return The value after the increment.
    /// @see https://redis.io/commands/incrbyfloat
    double
    incrbyfloat(const std::string &key, double increment) {
        return static_cast<Derived &>(*this).template command<double>("INCRBYFLOAT", key, increment).result;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<double> &&>, Derived &>
    incrbyfloat(Func &&func, const std::string &key, double increment) {
        return static_cast<Derived &>(*this)
            .template command<double>(std::forward<Func>(func), "INCRBYFLOAT", key, increment);
    }

    /// @brief Get the values of multiple keys atomically.
    ///
    /// Example:
    /// @code{.cpp}
    /// std::vector<std::string> keys = {"k1", "k2", "k3"};
    /// std::vector<std::optional<std::string>> vals;
    /// redis.mget(keys, "k4");
    /// for (const auto &val : vals) {
    ///     if (val)
    ///         std::cout << *val << std::endl;
    ///     else
    ///         std::cout << "key does not exist" << std::endl;
    /// }
    /// @endcode
    /// @param keys Keys, variadic(could be a string, or a container of keys)... .
    /// @return values of keys in std::vector<std::optional<std::string>>
    /// @note The destination should be a container of `std::optional<std::string>` type,
    ///       since the given key might not exist (in this case, the value of the corresponding
    ///       key is `std::optional<std::string>{}` (`std::nullopt`)).
    /// @see https://redis.io/commands/mget
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

    /// @brief Set multiple key-value pairs.
    ///
    /// Example:
    /// @code{.cpp}
    /// std::vector<std::pair<std::string, std::string>> kvs1 = {{"k1", "v1"}, {"k2", "v2"}};
    /// redis.mset(kvs1);
    /// std::unordered_map<std::string, std::string> kvs2 = {{"k3", "v3"}, {"k4", "v4"}};
    /// redis.mset(kvs2);
    /// redis.mset("k5", "v5", "k6", "v6");
    /// @endcode
    /// @param keys Keys, variadic(could be a string, or a container of (key/value)(s))... .
    /// @see https://redis.io/commands/mset
    template <typename... Keys>
    bool
    mset(Keys &&...keys) {
        return static_cast<Derived &>(*this).template command<void>("MSET", std::forward<Keys>(keys)...).ok;
    }
    template <typename Func, typename... Keys>
    std::enable_if_t<std::is_invocable_v<Func, Reply<void> &&>, Derived &>
    mset(Func &&func, Keys &&...keys) {
        return static_cast<Derived &>(*this).template command<void>(
            std::forward<Func>(func),
            "MSET",
            std::forward<Keys>(keys)...);
    }

    /// @brief Set the given key-value pairs if all specified keys do not exist.
    ///
    /// Example:
    /// @code{.cpp}
    /// std::vector<std::pair<std::string, std::string>> kvs1;
    /// redis.msetnx(kvs1.begin(), kvs1.end());
    /// std::unordered_map<std::string, std::string> kvs2;
    /// redis.msetnx(kvs2.begin(), kvs2.end());
    /// redis.mset("k5", "v5", "k6", "v6");
    /// @endcode
    /// @param keys Keys, variadic(could be a string, or a container of (key/value)(s))... .
    /// @return Whether all keys have been set.
    /// @retval true If all keys have been set.
    /// @retval false If no key was set, i.e. at least one key already exist.
    /// @see https://redis.io/commands/msetnx
    template <typename... Keys>
    bool
    msetnx(Keys &&...keys) {
        return static_cast<Derived &>(*this).template command<bool>("MSETNX", std::forward<Keys>(keys)...).ok;
    }
    template <typename Func, typename... Keys>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    msetnx(Func &&func, Keys &&...keys) {
        return static_cast<Derived &>(*this).template command<bool>(
            std::forward<Func>(func),
            "MSETNX",
            std::forward<Keys>(keys)...);
    }

    /// @brief Set key-value pair with the given timeout in milliseconds.
    /// @param key Key.
    /// @param ttl Time-To-Live in milliseconds.
    /// @param val Value.
    /// @see https://redis.io/commands/psetex
    bool
    psetex(const std::string &key, long long ttl, const std::string &val) {
        return static_cast<Derived &>(*this).template command<void>("PSETEX", key, ttl, val).ok;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<void> &&>, Derived &>
    psetex(Func &&func, const std::string &key, long long ttl, const std::string &val) {
        return static_cast<Derived &>(*this).template command<void>(std::forward<Func>(func), "PSETEX", key, ttl, val);
    }

    /// @brief Set key-value pair with the given timeout in milliseconds.
    /// @param key Key.
    /// @param ttl Time-To-Live in milliseconds.
    /// @param val Value.
    /// @see https://redis.io/commands/psetex
    bool
    psetex(const std::string &key, std::chrono::milliseconds const &ttl, const std::string &val) {
        return psetex(key, ttl.count(), val);
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<void> &&>, Derived &>
    psetex(Func &&func, const std::string &key, std::chrono::milliseconds const &ttl, const std::string &val) {
        return psetex(std::forward<Func>(func), key, ttl.count(), val);
    }

    /// @brief Set a key-value pair.
    ///
    /// Example:
    /// @code{.cpp}
    /// // Set a key-value pair.
    /// redis.set("key", "value");
    /// // Set a key-value pair, and expire it after 10 seconds.
    /// redis.set("key", "value", std::chrono::seconds(10));
    /// // Set a key-value pair with a timeout, only if the key already exists.
    /// if (redis.set("key", "value", std::chrono::seconds(10), UpdateType::EXIST))
    ///     std::cout << "OK" << std::endl;
    /// else
    ///     std::cout << "key does not exist" << std::endl;
    /// @endcode
    /// @param key Key.
    /// @param val Value.
    /// @param ttl Timeout on the key. If `ttl` is 0ms, do not set timeout.
    /// @param type Options for set command:
    ///             - UpdateType::EXIST: Set the key only if it already exists.
    ///             - UpdateType::NOT_EXIST: Set the key only if it does not exist.
    ///             - UpdateType::ALWAYS: Always set the key no matter whether it exists.
    /// @return Whether the key has been set.
    /// @retval true If the key has been set.
    /// @retval false If the key was not set, because of the given option.
    /// @see https://redis.io/commands/set
    bool
    set(const std::string &key, const std::string &val, UpdateType type = UpdateType::ALWAYS) {
        std::optional<std::string> opt;
        if (type != UpdateType::ALWAYS)
            opt = std::to_string(type);

        return static_cast<Derived &>(*this).template command<reply::set>("SET", key, val, opt).ok;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<reply::set> &&>, Derived &>
    set(Func &&func, const std::string &key, const std::string &val, UpdateType type = UpdateType::ALWAYS) {
        std::optional<std::string> opt;
        if (type != UpdateType::ALWAYS)
            opt = std::to_string(type);

        return static_cast<Derived &>(*this)
            .template command<reply::set>(std::forward<Func>(func), "SET", key, val, opt);
    }

    bool
    set(const std::string &key, const std::string &val, long long ttl, UpdateType type = UpdateType::ALWAYS) {
        std::optional<std::string> opt;
        if (type != UpdateType::ALWAYS)
            opt = std::to_string(type);
        return static_cast<Derived &>(*this).template command<reply::set>("SET", key, val, "PX", ttl, opt).ok;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<reply::set> &&>, Derived &>
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<reply::set> &&>, Derived &>
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

    /// @brief Set key-value pair with the given timeout in seconds.
    /// @param key Key.
    /// @param ttl Time-To-Live in seconds.
    /// @param val Value.
    /// @see https://redis.io/commands/setex
    bool
    setex(const std::string &key, long long ttl, const std::string &val) {
        return static_cast<Derived &>(*this).template command<void>("SETEX", key, ttl, val).ok;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<void> &&>, Derived &>
    setex(Func &&func, const std::string &key, long long ttl, const std::string &val) {
        return static_cast<Derived &>(*this).template command<void>(std::forward<Func>(func), "SETEX", key, ttl, val);
    }

    /// @brief Set key-value pair with the given timeout in seconds.
    /// @param key Key.
    /// @param ttl Time-To-Live in seconds.
    /// @param val Value.
    /// @see https://redis.io/commands/setex
    bool
    setex(const std::string &key, std::chrono::seconds const &ttl, const std::string &val) {
        return setex(key, ttl.count(), val);
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<void> &&>, Derived &>
    setex(Func &&func, const std::string &key, std::chrono::seconds const &ttl, const std::string &val) {
        return setex(std::forward<Func>(func), key, ttl.count(), val);
    }

    /// @brief Set the key if it does not exist.
    /// @param key Key.
    /// @param val Value.
    /// @return Whether the key has been set.
    /// @retval true If the key has been set.
    /// @retval false If the key was not set, i.e. the key already exists.
    /// @see https://redis.io/commands/setnx
    bool
    setnx(const std::string &key, const std::string &val) {
        return static_cast<Derived &>(*this).template command<bool>("SETNX", key, val).ok;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    setnx(Func &&func, const std::string &key, const std::string &val) {
        return static_cast<Derived &>(*this).template command<bool>(std::forward<Func>(func), "SETNX", key, val);
    }

    /// @brief Set the substring starting from `offset` to the given value.
    /// @param key Key.
    /// @param offset Offset.
    /// @param val Value.
    /// @return The length of the string after this operation.
    /// @see https://redis.io/commands/setrange
    long long
    setrange(const std::string &key, long long offset, const std::string &val) {
        return static_cast<Derived &>(*this).template command<long long>("SETRANGE", key, offset, val).result;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    setrange(Func &&func, const std::string &key, long long offset, const std::string &val) {
        return static_cast<Derived &>(*this)
            .template command<long long>(std::forward<Func>(func), "SETRANGE", key, offset, val);
    }

    /// @brief Sets or clears the bit at offset in the string value stored at key.
    /// @param key Key.
    /// @param offset Offset.
    /// @param val Value.
    /// @return the original bit value stored at offset.
    /// @see https://redis.io/commands/setbit
    long long
    setbit(const std::string &key, long long offset, long long value) {
        return static_cast<Derived &>(*this).template command<long long>("SETBIT", key, offset, value).result;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
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
        return static_cast<Derived &>(*this).ping([ret, func = std::forward<Func>(func)](auto &&) {
            func(redis::Reply<long long>{*ret == NB_BITS, *ret, nullptr});
            delete ret;
        });
    }

    /// @brief Get the length of the string stored at key.
    /// @param key Key.
    /// @return The length of the string.
    /// @note If key does not exist, `strlen` returns 0.
    /// @see https://redis.io/commands/strlen
    long long
    strlen(const std::string &key) {
        return static_cast<Derived &>(*this).template command<long long>("STRLEN", key).result;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    strlen(Func &&func, const std::string &key) {
        return static_cast<Derived &>(*this).template command<long long>(std::forward<Func>(func), "STRLEN", key);
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_STRING_COMMANDS_H
