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
    /// @brief Pop the first element of the list in a blocking way.
    /// @param timeout Timeout in seconds. 0 means block forever.
    /// @param keys Keys, variadic(could be a string, or a container of keys)... .
    /// @return Key-element pair.
    /// @note If list is empty and timeout reaches, return `OptionalStringPair{}` (`std::nullopt`).
    /// @see `Redis::lpop`
    /// @see https://redis.io/commands/blpop
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::pair<std::string, std::string>>> &&>, Derived &>
    blpop(Func &&func, long long timeout, Keys &&...keys) {
        return static_cast<Derived &>(*this).template command<std::optional<std::pair<std::string, std::string>>>(
            std::forward<Func>(func),
            "BLPOP",
            std::forward<Keys>(keys)...,
            timeout);
    }

    /// @brief Pop the first element of the list in a blocking way.
    /// @param timeout Timeout in seconds. 0 means block forever.
    /// @param keys Keys, variadic(could be a string, or a container of keys)... .
    /// @return Key-element pair.
    /// @note If list is empty and timeout reaches, return `OptionalStringPair{}` (`std::nullopt`).
    /// @see `Redis::lpop`
    /// @see https://redis.io/commands/blpop
    template <typename... Keys>
    std::optional<std::pair<std::string, std::string>>
    blpop(const std::chrono::seconds &timeout, Keys &&...keys) {
        return blpop(timeout.count(), std::forward<Keys>(keys)...);
    }
    template <typename Func, typename... Keys>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::pair<std::string, std::string>>> &&>, Derived &>
    blpop(Func &&func, const std::chrono::seconds &timeout, Keys &&...keys) {
        return blpop(std::forward<Func>(func), timeout.count(), std::forward<Keys>(keys)...);
    }

    /// @brief Pop the last element of the list in a blocking way.
    /// @param timeout Timeout in seconds. 0 means block forever.
    /// @param keys Keys, variadic(could be a string, or a container of keys)... .
    /// @return Key-element pair.
    /// @note If list is empty and timeout reaches, return `OptionalStringPair{}` (`std::nullopt`).
    /// @see `Redis::rpop`
    /// @see https://redis.io/commands/brpop
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::pair<std::string, std::string>>> &&>, Derived &>
    brpop(Func &&func, long long timeout, Keys &&...keys) {
        return static_cast<Derived &>(*this).template command<std::optional<std::pair<std::string, std::string>>>(
            std::forward<Func>(func),
            "BRPOP",
            std::forward<Keys>(keys)...,
            timeout);
    }

    /// @brief Pop the last element of the list in a blocking way.
    /// @param timeout Timeout in seconds. 0 means block forever.
    /// @param keys Keys, variadic(could be a string, or a container of keys)... .
    /// @return Key-element pair.
    /// @note If list is empty and timeout reaches, return `OptionalStringPair{}` (`std::nullopt`).
    /// @see `Redis::rpop`
    /// @see https://redis.io/commands/brpop
    template <typename... Keys>
    std::optional<std::pair<std::string, std::string>>
    brpop(const std::chrono::seconds &timeout, Keys &&...keys) {
        return brpop(timeout.count(), std::forward<Keys>(keys)...);
    }
    template <typename Func, typename... Keys>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::pair<std::string, std::string>>> &&>, Derived &>
    brpop(Func &&func, const std::chrono::seconds &timeout, Keys &&...keys) {
        return brpop(std::forward<Func>(func), timeout.count(), std::forward<Keys>(keys)...);
    }

    /// @brief Pop last element of one list and push it to the left of another list in blocking way.
    /// @param source Key of the source list.
    /// @param destination Key of the destination list.
    /// @param timeout Timeout. 0 means block forever.
    /// @return The popped element.
    /// @note If the source list does not exist, `brpoplpush` returns `OptionalString{}` (`std::nullopt`).
    /// @see `Redis::rpoplpush`
    /// @see https://redis.io/commands/brpoplpush
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

    /// @brief Pop last element of one list and push it to the left of another list in blocking way.
    /// @param source Key of the source list.
    /// @param destination Key of the destination list.
    /// @param timeout Timeout. 0 means block forever.
    /// @return The popped element.
    /// @note If the source list does not exist, `brpoplpush` returns `OptionalString{}` (`std::nullopt`).
    /// @see `Redis::rpoplpush`
    /// @see https://redis.io/commands/brpoplpush
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

    /// @brief Get the element at the given index of the list.
    /// @param key Key where the list is stored.
    /// @param index Zero-base index, and -1 means the last element.
    /// @return The element at the given index.
    /// @see https://redis.io/commands/lindex
    std::optional<std::string>
    lindex(const std::string &key, long long index) {
        return static_cast<Derived &>(*this).template command<std::optional<std::string>>("LINDEX", key, index).result;
    }
    template <typename Func>
    Derived &
    lindex(Func &&func, const std::string &key, long long index) {
        return static_cast<Derived &>(*this)
            .template command<std::optional<std::string>>(std::forward<Func>(func), "LINDEX", key, index);
    }

    /// @brief Insert an element to a list before or after the pivot element.
    ///
    /// Example:
    /// @code{.cpp}
    /// // Insert 'hello' before 'world'
    /// auto len = redis.linsert("list", InsertPosition::BEFORE, "world", "hello");
    /// if (len == -1)
    ///     std::cout << "there's no 'world' in the list" << std::endl;
    /// else
    ///     std::cout << "after the operation, the length of the list is " << len << std::endl;
    /// @endcode
    /// @param key Key where the list is stored.
    /// @param position Before or after the pivot element.
    /// @param pivot The pivot value. The `pivot` is the value of the element, not the index.
    /// @param val Element to be inserted.
    /// @return The length of the list after the operation.
    /// @note If the pivot value is not found, `linsert` returns -1.
    /// @see `InsertPosition`
    /// @see https://redis.io/commands/linsert
    long long
    linsert(const std::string &key, InsertPosition position, const std::string &pivot, const std::string &val) {
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

    /// @brief Get the length of the list.
    /// @param key Key where the list is stored.
    /// @return The length of the list.
    /// @see https://redis.io/commands/llen
    long long
    llen(const std::string &key) {
        return static_cast<Derived &>(*this).template command<long long>("LLEN", key).result;
    }
    template <typename Func>
    Derived &
    llen(Func &&func, const std::string &key) {
        return static_cast<Derived &>(*this).template command<long long>(std::forward<Func>(func), "LLEN", key);
    }

    /// @brief Pop the first element of the list.
    ///
    /// Example:
    /// @code{.cpp}
    /// auto element = redis.lpop("list");
    /// if (element)
    ///     std::cout << *element << std::endl;
    /// else
    ///     std::cout << "list is empty, i.e. list does not exist" << std::endl;
    /// @endcode
    /// @param key Key where the list is stored.
    /// @return The popped element.
    /// @note If list is empty, i.e. key does not exist, return `OptionalString{}` (`std::nullopt`).
    /// @see https://redis.io/commands/lpop
    std::optional<std::string>
    lpop(const std::string &key) {
        return static_cast<Derived &>(*this).template command<std::optional<std::string>>("LPOP", key).result;
    }
    template <typename Func>
    Derived &
    lpop(Func &&func, const std::string &key) {
        return static_cast<Derived &>(*this).template command<std::optional<std::string>>(
            std::forward<Func>(func),
            "LPOP",
            key);
    }

    // @brief Push an element to the beginning of the list.
    /// @param key Key where the list is stored.
    /// @param values Values, variadic(could be a string, or a container of values)... .
    /// @return The length of the list after the operation.
    /// @see https://redis.io/commands/lpush
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
        return static_cast<Derived &>(*this)
            .template command<long long>(std::forward<Func>(func), "LPUSH", key, std::forward<Values>(values)...);
    }

    /// @brief Push an element to the beginning of the list, only if the list already exists.
    /// @param key Key where the list is stored.
    /// @param val Element to be pushed.
    /// @return The length of the list after the operation.
    /// @see https://redis.io/commands/lpushx
    long long
    lpushx(const std::string &key, const std::string &val) {
        return static_cast<Derived &>(*this).template command<long long>("LPUSHX", key, val).result;
    }
    template <typename Func>
    Derived &
    lpushx(Func &&func, const std::string &key, const std::string &val) {
        return static_cast<Derived &>(*this).template command<long long>(std::forward<Func>(func), "LPUSHX", key, val);
    }

    /// @brief Get elements in the given range of the given list.
    ///
    /// Example:
    /// @code{.cpp}
    /// std::vector<std::string> elements;
    /// // Save all elements of a Redis list to a vector of string.
    /// elements = redis.lrange("list", 0, -1);
    /// @endcode
    /// @param key Key where the list is stored.
    /// @param start Start index of the range. Index can be negative, which mean index from the end.
    /// @param stop End index of the range.
    /// @return all elements found in a std::vector<std::string>.
    /// @see https://redis.io/commands/lrange
    std::vector<std::string>
    lrange(const std::string &key, long long start, long long stop) {
        return static_cast<Derived &>(*this)
            .template command<std::vector<std::string>>("LRANGE", key, start, stop)
            .result;
    }
    template <typename Func>
    Derived &
    lrange(Func &&func, const std::string &key, long long start, long long stop) {
        return static_cast<Derived &>(*this)
            .template command<std::vector<std::string>>(std::forward<Func>(func), "LRANGE", key, start, stop);
    }

    /// @brief Remove the first `count` occurrences of elements equal to `val`.
    /// @param key Key where the list is stored.
    /// @param count Number of occurrences to be removed.
    /// @param val Value.
    /// @return Number of elements removed.
    /// @note `count` can be positive, negative and 0. Check the reference for detail.
    /// @see https://redis.io/commands/lrem
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

    /// @brief Set the element at the given index to the specified value.
    /// @param key Key where the list is stored.
    /// @param index Index of the element to be set.
    /// @param val Value.
    /// @see https://redis.io/commands/lset
    bool
    lset(const std::string &key, long long index, const std::string &val) {
        return static_cast<Derived &>(*this).template command<void>("LSET", key, index, val).ok;
    }
    template <typename Func>
    Derived &
    lset(Func &&func, const std::string &key, long long index, const std::string &val) {
        return static_cast<Derived &>(*this).template command<void>(std::forward<Func>(func), "LSET", key, index, val);
    }

    /// @brief Trim a list to keep only element in the given range.
    /// @param key Key where the key is stored.
    /// @param start Start of the index.
    /// @param stop End of the index.
    /// @see https://redis.io/commands/ltrim
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

    /// @brief Pop the last element of a list.
    /// @param key Key where the list is stored.
    /// @return The popped element.
    /// @note If the list is empty, i.e. key does not exist, `rpop` returns `OptionalString{}` (`std::nullopt`).
    /// @see https://redis.io/commands/rpop
    std::optional<std::string>
    rpop(const std::string &key) {
        return static_cast<Derived &>(*this).template command<std::optional<std::string>>("RPOP", key).result;
    }
    template <typename Func>
    Derived &
    rpop(Func &&func, const std::string &key) {
        return static_cast<Derived &>(*this).template command<std::optional<std::string>>(
            std::forward<Func>(func),
            "RPOP",
            key);
    }

    /// @brief Pop last element of one list and push it to the left of another list.
    /// @param source Key of the source list.
    /// @param destination Key of the destination list.
    /// @return The popped element.
    /// @note If the source list does not exist, `rpoplpush` returns `OptionalString{}` (`std::nullopt`).
    /// @see https://redis.io/commands/brpoplpush
    std::optional<std::string>
    rpoplpush(const std::string &source, const std::string &destination) {
        return static_cast<Derived &>(*this)
            .template command<std::optional<std::string>>("RPOPLPUSH", source, destination)
            .result;
    }
    template <typename Func>
    Derived &
    rpoplpush(Func &&func, const std::string &source, const std::string &destination) {
        return static_cast<Derived &>(*this)
            .template command<std::optional<std::string>>(std::forward<Func>(func), "RPOPLPUSH", source, destination);
    }

    /// @brief Push an element to the end of the list.
    /// @param key Key where the list is stored.
    /// @param values Values, variadic(could be a string, or a container of values)... .
    /// @return The length of the list after the operation.
    /// @see https://redis.io/commands/rpush
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
        return static_cast<Derived &>(*this)
            .template command<long long>(std::forward<Func>(func), "RPUSH", key, std::forward<Values>(values)...);
    }

    /// @brief Push an element to the end of the list, only if the list already exists.
    /// @param key Key where the list is stored.
    /// @param val Element to be pushed.
    /// @return The length of the list after the operation.
    /// @see https://redis.io/commands/rpushx
    long long
    rpushx(const std::string &key, const std::string &val) {
        return static_cast<Derived &>(*this).template command<long long>("RPUSHX", key, val).result;
    }
    template <typename Func>
    Derived &
    rpushx(Func &&func, const std::string &key, const std::string &val) {
        return static_cast<Derived &>(*this).template command<long long>(std::forward<Func>(func), "RPUSHX", key, val);
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_LIST_COMMANDS_H
