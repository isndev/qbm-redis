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

#ifndef QBM_REDIS_LIST_COMMANDS_H
#define QBM_REDIS_LIST_COMMANDS_H

#include <chrono>
#include "reply.h"

namespace qb::redis {

/**
 * @class list_commands
 * @brief Provides Redis list command implementations.
 *
 * This class implements Redis list operations, which provide an ordered collection
 * of strings. Each command has both synchronous and asynchronous versions.
 *
 * Redis lists are implemented as linked lists, which provide fast operations
 * when adding elements to the head or tail, as well as manipulating elements
 * at both ends.
 *
 * @tparam Derived The derived class type (CRTP pattern)
 */
template <typename Derived>
class list_commands {
private:
    constexpr Derived &
    derived() {
        return static_cast<Derived &>(*this);
    }

public:
    // =============== Basic List Operations ===============

    /**
     * @brief Get the length of the list.
     * @param key Key where the list is stored.
     * @return The length of the list.
     * @see https://redis.io/commands/llen
     */
    long long
    llen(const std::string &key) {
        if (key.empty()) {
            return 0;
        }
        return derived().template command<long long>("LLEN", key).result();
    }

    /**
     * @brief Get the length of the list asynchronously.
     * @param func Callback function to handle the result.
     * @param key Key where the list is stored.
     * @return Reference to the derived class.
     * @see https://redis.io/commands/llen
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    llen(Func &&func, const std::string &key) {
        return derived().template command<long long>(std::forward<Func>(func), "LLEN",
                                                     key);
    }

    // =============== Push Operations ===============

    /**
     * @brief Push multiple elements to the beginning of the list.
     * @param key Key where the list is stored.
     * @param values Values to be pushed.
     * @return The length of the list after the operation.
     * @see https://redis.io/commands/lpush
     */
    template <typename... Args>
    long long
    lpush(const std::string &key, Args &&...args) {
        if (key.empty() || sizeof...(args) == 0) {
            return 0;
        }
        return derived()
            .template command<long long>("LPUSH", key, std::forward<Args>(args)...)
            .result();
    }

    /**
     * @brief Push multiple elements to the beginning of the list asynchronously.
     * @param func Callback function to handle the result.
     * @param key Key where the list is stored.
     * @param values Values to be pushed.
     * @return Reference to the derived class.
     * @see https://redis.io/commands/lpush
     */
    template <typename Func, typename... Args>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    lpush(Func &&func, const std::string &key, Args &&...args) {
        return derived().template command<long long>(std::forward<Func>(func), "LPUSH",
                                                     key, std::forward<Args>(args)...);
    }

    /**
     * @brief Push an element to the beginning of the list, only if the list already
     * exists.
     * @param key Key where the list is stored.
     * @param val Element to be pushed.
     * @return The length of the list after the operation.
     * @see https://redis.io/commands/lpushx
     */
    template <typename... Args>
    long long
    lpushx(const std::string &key, Args &&...args) {
        if (key.empty() || sizeof...(args) == 0) {
            return 0;
        }
        return derived()
            .template command<long long>("LPUSHX", key, std::forward<Args>(args)...)
            .result();
    }

    /**
     * @brief Push an element to the beginning of the list asynchronously, only if the
     * list exists.
     * @param func Callback function to handle the result.
     * @param key Key where the list is stored.
     * @param val Element to be pushed.
     * @return Reference to the derived class.
     * @see https://redis.io/commands/lpushx
     */
    template <typename Func, typename... Args>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    lpushx(Func &&func, const std::string &key, Args &&...args) {
        return derived().template command<long long>(std::forward<Func>(func), "LPUSHX",
                                                     key, std::forward<Args>(args)...);
    }

    /**
     * @brief Push multiple elements to the end of the list.
     * @param key Key where the list is stored.
     * @param values Values to be pushed.
     * @return The length of the list after the operation.
     * @see https://redis.io/commands/rpush
     */
    template <typename... Args>
    long long
    rpush(const std::string &key, Args &&...args) {
        if (key.empty() || sizeof...(args) == 0) {
            return 0;
        }
        return derived()
            .template command<long long>("RPUSH", key, std::forward<Args>(args)...)
            .result();
    }

    /**
     * @brief Push multiple elements to the end of the list asynchronously.
     * @param func Callback function to handle the result.
     * @param key Key where the list is stored.
     * @param values Values to be pushed.
     * @return Reference to the derived class.
     * @see https://redis.io/commands/rpush
     */
    template <typename Func, typename... Args>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    rpush(Func &&func, const std::string &key, Args &&...args) {
        return derived().template command<long long>(std::forward<Func>(func), "RPUSH",
                                                     key, std::forward<Args>(args)...);
    }

    /**
     * @brief Push an element to the end of the list, only if the list already exists.
     * @param key Key where the list is stored.
     * @param val Element to be pushed.
     * @return The length of the list after the operation.
     * @note If the list does not exist, `rpushx` returns 0.
     * @see https://redis.io/commands/rpushx
     */
    long long
    rpushx(const std::string &key, const std::string &val) {
        if (key.empty() || val.empty()) {
            return 0;
        }
        return derived().template command<long long>("RPUSHX", key, val).result();
    }

    /**
     * @brief Push an element to the end of the list asynchronously, only if the list
     * exists.
     * @param func Callback function to handle the result.
     * @param key Key where the list is stored.
     * @param val Element to be pushed.
     * @return Reference to the derived class.
     * @see https://redis.io/commands/rpushx
     */
    template <typename Func, typename... Args>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    rpushx(Func &&func, const std::string &key, Args &&...args) {
        return derived().template command<long long>(std::forward<Func>(func), "RPUSHX",
                                                     key, std::forward<Args>(args)...);
    }

    // =============== Pop Operations ===============

    /**
     * @brief Pop the first element(s) of the list.
     * @param key Key where the list is stored.
     * @param count Number of elements to pop (optional).
     * @return The popped element(s).
     * @note If list is empty, i.e. key does not exist, returns empty vector.
     * @see https://redis.io/commands/lpop
     */
    std::vector<std::string>
    lpop(const std::string &key, long long count) {
        if (key.empty() || count < 1) {
            return {};
        }
        return derived()
            .template command<std::vector<std::string>>("LPOP", key, count)
            .result();
    }

    /**
     * @brief Pop the first element(s) of the list asynchronously.
     * @param func Callback function to handle the result.
     * @param key Key where the list is stored.
     * @param count Number of elements to pop (optional).
     * @return Reference to the derived class.
     * @see https://redis.io/commands/lpop
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>,
                     Derived &>
    lpop(Func &&func, const std::string &key, long long count) {
        return derived().template command<std::vector<std::string>>(
            std::forward<Func>(func), "LPOP", key, count);
    }

    /**
     * @brief Pop a single element from the left of the list.
     * @param key Key where the list is stored.
     * @return The popped element or nullopt if the list is empty.
     * @see https://redis.io/commands/lpop
     */
    std::optional<std::string>
    lpop(const std::string &key) {
        return derived()
            .template command<std::optional<std::string>>("LPOP", key)
            .result();
    }

    /**
     * @brief Pop a single element from the left of the list asynchronously.
     * @param func Callback function to handle the result.
     * @param key Key where the list is stored.
     * @return Reference to the derived class.
     * @see https://redis.io/commands/lpop
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::string>> &&>,
                     Derived &>
    lpop(Func &&func, const std::string &key) {
        return derived().template command<std::optional<std::string>>(
            std::forward<Func>(func), "LPOP", key);
    }

    /**
     * @brief Pop the last element(s) of the list.
     * @param key Key where the list is stored.
     * @param count Number of elements to pop (optional).
     * @return The popped element(s).
     * @note If list is empty, i.e. key does not exist, returns empty vector.
     * @see https://redis.io/commands/rpop
     */
    std::vector<std::string>
    rpop(const std::string &key, long long count) {
        if (key.empty() || count < 1) {
            return {};
        }
        return derived()
            .template command<std::vector<std::string>>("RPOP", key, count)
            .result();
    }

    /**
     * @brief Pop the last element(s) of the list asynchronously.
     * @param func Callback function to handle the result.
     * @param key Key where the list is stored.
     * @param count Number of elements to pop (optional).
     * @return Reference to the derived class.
     * @see https://redis.io/commands/rpop
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>,
                     Derived &>
    rpop(Func &&func, const std::string &key, long long count) {
        return derived().template command<std::vector<std::string>>(
            std::forward<Func>(func), "RPOP", key, count);
    }

    /**
     * @brief Pop a single element from the right of the list.
     * @param key Key where the list is stored.
     * @return The popped element or nullopt if the list is empty.
     * @see https://redis.io/commands/rpop
     */
    std::optional<std::string>
    rpop(const std::string &key) {
        return derived()
            .template command<std::optional<std::string>>("RPOP", key)
            .result();
    }

    /**
     * @brief Pop a single element from the right of the list asynchronously.
     * @param func Callback function to handle the result.
     * @param key Key where the list is stored.
     * @return Reference to the derived class.
     * @see https://redis.io/commands/rpop
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::string>> &&>,
                     Derived &>
    rpop(Func &&func, const std::string &key) {
        return derived().template command<std::optional<std::string>>(
            std::forward<Func>(func), "RPOP", key);
    }

    // =============== Blocking Operations ===============

    /**
     * @brief Pop the first element of the list in a blocking way.
     * @param keys List of keys to check.
     * @param timeout Timeout in seconds. 0 means block forever.
     * @return Key-element pair.
     * @note If list is empty and timeout reaches, return `std::nullopt`.
     * @see https://redis.io/commands/blpop
     */
    std::optional<std::pair<std::string, std::string>>
    blpop(const std::vector<std::string> &keys, long long timeout = 0) {
        if (keys.size() == 0) {
            return std::nullopt;
        }
        return derived()
            .template command<std::optional<std::pair<std::string, std::string>>>(
                "BLPOP", keys, timeout)
            .result();
    }

    /**
     * @brief Pop the first element of the list in a blocking way asynchronously.
     * @param func Callback function to handle the result.
     * @param keys List of keys to check.
     * @param timeout Timeout in seconds. 0 means block forever.
     * @return Reference to the derived class.
     * @see https://redis.io/commands/blpop
     */
    template <typename Func>
    std::enable_if_t<
        std::is_invocable_v<
            Func, Reply<std::optional<std::pair<std::string, std::string>>> &&>,
        Derived &>
    blpop(Func &&func, const std::vector<std::string> &keys, long long timeout) {
        return derived()
            .template command<std::optional<std::pair<std::string, std::string>>>(
                std::forward<Func>(func), "BLPOP", keys, timeout);
    }

    /**
     * @brief Pop the first element of the list in a blocking way.
     * @param keys List of keys to check.
     * @param timeout Timeout duration.
     * @return Key-element pair.
     * @note If list is empty and timeout reaches, return `std::nullopt`.
     * @see https://redis.io/commands/blpop
     */
    std::optional<std::pair<std::string, std::string>>
    blpop(const std::vector<std::string> &keys, const std::chrono::seconds &timeout) {
        return blpop(keys, timeout.count());
    }
    /**
     * @brief Pop the first element of the list in a blocking way asynchronously.
     * @param func Callback function to handle the result.
     * @param keys List of keys to check.
     * @param timeout Timeout duration.
     * @return Reference to the derived class.
     * @see https://redis.io/commands/blpop
     */
    template <typename Func>
    std::enable_if_t<
        std::is_invocable_v<
            Func, Reply<std::optional<std::pair<std::string, std::string>>> &&>,
        Derived &>
    blpop(Func &&func, const std::vector<std::string> &keys,
          const std::chrono::seconds &timeout) {
        return blpop(func, keys, timeout.count());
    }

    /**
     * @brief Pop the last element of the list in a blocking way.
     * @param keys List of keys to check.
     * @param timeout Timeout in seconds. 0 means block forever.
     * @return Key-element pair.
     * @note If list is empty and timeout reaches, return `std::nullopt`.
     * @see https://redis.io/commands/brpop
     */
    std::optional<std::pair<std::string, std::string>>
    brpop(const std::vector<std::string> &keys, long long timeout = 0) {
        if (keys.size() == 0) {
            return std::nullopt;
        }
        return derived()
            .template command<std::optional<std::pair<std::string, std::string>>>(
                "BRPOP", keys, timeout)
            .result();
    }

    /**
     * @brief Pop the last element of the list in a blocking way asynchronously.
     * @param func Callback function to handle the result.
     * @param keys List of keys to check.
     * @param timeout Timeout in seconds. 0 means block forever.
     * @return Reference to the derived class.
     * @see https://redis.io/commands/brpop
     */
    template <typename Func>
    std::enable_if_t<
        std::is_invocable_v<
            Func, Reply<std::optional<std::pair<std::string, std::string>>> &&>,
        Derived &>
    brpop(Func &&func, const std::vector<std::string> &keys, long long timeout) {
        return derived()
            .template command<std::optional<std::pair<std::string, std::string>>>(
                std::forward<Func>(func), "BRPOP", keys, timeout);
    }

    /**
     * @brief Pop the last element of the list in a blocking way.
     * @param keys List of keys to check.
     * @param timeout Timeout duration.
     * @return Key-element pair.
     * @note If list is empty and timeout reaches, return `std::nullopt`.
     * @see https://redis.io/commands/brpop
     */
    std::optional<std::pair<std::string, std::string>>
    brpop(const std::vector<std::string> &keys, const std::chrono::seconds &timeout) {
        return brpop(keys, timeout.count());
    }

    /**
     * @brief Pop the last element of the list in a blocking way asynchronously.
     * @param func Callback function to handle the result.
     * @param keys List of keys to check.
     * @param timeout Timeout duration.
     * @return Reference to the derived class.
     * @see https://redis.io/commands/brpop
     */
    template <typename Func>
    std::enable_if_t<
        std::is_invocable_v<
            Func, Reply<std::optional<std::pair<std::string, std::string>>> &&>,
        Derived &>
    brpop(Func &&func, const std::vector<std::string> &keys,
          const std::chrono::seconds &timeout) {
        return brpop(func, keys, timeout.count());
    }

    // =============== List Manipulation Operations ===============

    /**
     * @brief Get the element at the given index of the list.
     * @param key Key where the list is stored.
     * @param index Zero-base index, and -1 means the last element.
     * @return The element at the given index.
     * @see https://redis.io/commands/lindex
     */
    std::optional<std::string>
    lindex(const std::string &key, long long index) {
        if (key.empty()) {
            return std::nullopt;
        }
        return derived()
            .template command<std::optional<std::string>>("LINDEX", key, index)
            .result();
    }

    /**
     * @brief Get the element at the given index of the list asynchronously.
     * @param func Callback function to handle the result.
     * @param key Key where the list is stored.
     * @param index Zero-base index, and -1 means the last element.
     * @return Reference to the derived class.
     * @see https://redis.io/commands/lindex
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::string>> &&>,
                     Derived &>
    lindex(Func &&func, const std::string &key, long long index) {
        return derived().template command<std::optional<std::string>>(
            std::forward<Func>(func), "LINDEX", key, index);
    }

    /**
     * @brief Insert an element to a list before or after the pivot element.
     * @param key Key where the list is stored.
     * @param position Before or after the pivot element.
     * @param pivot The pivot value. The `pivot` is the value of the element, not the
     * index.
     * @param val Element to be inserted.
     * @return The length of the list after the operation.
     * @note If the pivot value is not found, `linsert` returns -1.
     * @see `InsertPosition`
     * @see https://redis.io/commands/linsert
     */
    long long
    linsert(const std::string &key, InsertPosition position, const std::string &pivot,
            const std::string &val) {
        if (key.empty() || pivot.empty() || val.empty()) {
            return -1;
        }
        return derived()
            .template command<long long>("LINSERT", key, std::to_string(position), pivot,
                                         val)
            .result();
    }

    /**
     * @brief Insert an element to a list before or after the pivot element
     * asynchronously.
     * @param func Callback function to handle the result.
     * @param key Key where the list is stored.
     * @param position Before or after the pivot element.
     * @param pivot The pivot value. The `pivot` is the value of the element, not the
     * index.
     * @param val Element to be inserted.
     * @return Reference to the derived class.
     * @see https://redis.io/commands/linsert
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    linsert(Func &&func, const std::string &key, InsertPosition position,
            const std::string &pivot, const std::string &val) {
        return derived().template command<long long>(std::forward<Func>(func), "LINSERT",
                                                     key, std::to_string(position),
                                                     pivot, val);
    }

    /**
     * @brief Get elements in the given range of the given list.
     * @param key Key where the list is stored.
     * @param start Start index of the range. Index can be negative, which mean index
     * from the end.
     * @param stop End index of the range.
     * @return All elements found in a std::vector<std::string>.
     * @see https://redis.io/commands/lrange
     */
    std::vector<std::string>
    lrange(const std::string &key, long long start, long long stop) {
        if (key.empty()) {
            return {};
        }
        return derived()
            .template command<std::vector<std::string>>("LRANGE", key, start, stop)
            .result();
    }

    /**
     * @brief Get elements in the given range of the given list asynchronously.
     * @param func Callback function to handle the result.
     * @param key Key where the list is stored.
     * @param start Start index of the range. Index can be negative, which mean index
     * from the end.
     * @param stop End index of the range.
     * @return Reference to the derived class.
     * @see https://redis.io/commands/lrange
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>,
                     Derived &>
    lrange(Func &&func, const std::string &key, long long start, long long stop) {
        return derived().template command<std::vector<std::string>>(
            std::forward<Func>(func), "LRANGE", key, start, stop);
    }

    /**
     * @brief Remove the first `count` occurrences of elements equal to `val`.
     * @param key Key where the list is stored.
     * @param count Number of occurrences to be removed.
     * @param val Value.
     * @return Number of elements removed.
     * @note `count` can be positive, negative and 0. Check the reference for detail.
     * @see https://redis.io/commands/lrem
     */
    long long
    lrem(const std::string &key, long long count, const std::string &val) {
        if (key.empty() || val.empty()) {
            return 0;
        }
        return derived().template command<long long>("LREM", key, count, val).result();
    }

    /**
     * @brief Remove the first `count` occurrences of elements equal to `val`
     * asynchronously.
     * @param func Callback function to handle the result.
     * @param key Key where the list is stored.
     * @param count Number of occurrences to be removed.
     * @param val Value.
     * @return Reference to the derived class.
     * @see https://redis.io/commands/lrem
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    lrem(Func &&func, const std::string &key, long long count, const std::string &val) {
        return derived().template command<long long>(std::forward<Func>(func), "LREM",
                                                     key, count, val);
    }

    /**
     * @brief Set the element at the given index to the specified value.
     * @param key Key where the list is stored.
     * @param index Index of the element to be set.
     * @param val Value.
     * @return status object indicating success or failure.
     * @see https://redis.io/commands/lset
     */
    status
    lset(const std::string &key, long long index, const std::string &val) {
        if (key.empty() || val.empty()) {
            return {};
        }
        return derived().template command<status>("LSET", key, index, val).result();
    }

    /**
     * @brief Set the element at the given index to the specified value asynchronously.
     * @param func Callback function to handle the result.
     * @param key Key where the list is stored.
     * @param index Index of the element to be set.
     * @param val Value.
     * @return Reference to the derived class.
     * @see https://redis.io/commands/lset
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    lset(Func &&func, const std::string &key, long long index, const std::string &val) {
        return derived().template command<status>(std::forward<Func>(func), "LSET", key,
                                                  index, val);
    }

    /**
     * @brief Trim a list to keep only element in the given range.
     * @param key Key where the key is stored.
     * @param start Start of the index.
     * @param stop End of the index.
     * @return status object indicating success or failure.
     * @see https://redis.io/commands/ltrim
     */
    status
    ltrim(const std::string &key, long long start, long long stop) {
        if (key.empty()) {
            return {};
        }
        return derived().template command<status>("LTRIM", key, start, stop).result();
    }

    /**
     * @brief Trim a list to keep only element in the given range asynchronously.
     * @param func Callback function to handle the result.
     * @param key Key where the key is stored.
     * @param start Start of the index.
     * @param stop End of the index.
     * @return Reference to the derived class.
     * @see https://redis.io/commands/ltrim
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    ltrim(Func &&func, const std::string &key, long long start, long long stop) {
        return derived().template command<status>(std::forward<Func>(func), "LTRIM", key,
                                                  start, stop);
    }

    // =============== Advanced List Operations ===============

    /**
     * @brief Pop last element of one list and push it to the left of another list.
     * @param source Key of the source list.
     * @param destination Key of the destination list.
     * @return The popped element.
     * @note If the source list does not exist, returns `std::nullopt`.
     * @see https://redis.io/commands/brpoplpush
     */
    std::optional<std::string>
    rpoplpush(const std::string &source, const std::string &destination) {
        if (source.empty() || destination.empty()) {
            return std::nullopt;
        }
        return derived()
            .template command<std::optional<std::string>>("RPOPLPUSH", source,
                                                          destination)
            .result();
    }

    /**
     * @brief Pop last element of one list and push it to the left of another list
     * asynchronously.
     * @param func Callback function to handle the result.
     * @param source Key of the source list.
     * @param destination Key of the destination list.
     * @return Reference to the derived class.
     * @see https://redis.io/commands/brpoplpush
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::string>> &&>,
                     Derived &>
    rpoplpush(Func &&func, const std::string &source, const std::string &destination) {
        return derived().template command<std::optional<std::string>>(
            std::forward<Func>(func), "RPOPLPUSH", source, destination);
    }

    /**
     * @brief Move an element from one list to another.
     * @param source Key of the source list.
     * @param destination Key of the destination list.
     * @param wherefrom Where to pop from (LEFT or RIGHT).
     * @param whereto Where to push to (LEFT or RIGHT).
     * @return The element being moved.
     * @note If source list is empty, returns `std::nullopt`.
     * @see https://redis.io/commands/lmove
     */
    std::optional<std::string>
    lmove(const std::string &source, const std::string &destination,
          ListPosition wherefrom, ListPosition whereto) {
        if (source.empty() || destination.empty()) {
            return std::nullopt;
        }
        return derived()
            .template command<std::optional<std::string>>("LMOVE", source, destination,
                                                          std::to_string(wherefrom),
                                                          std::to_string(whereto))
            .result();
    }

    /**
     * @brief Move an element from one list to another asynchronously.
     * @param func Callback function to handle the result.
     * @param source Key of the source list.
     * @param destination Key of the destination list.
     * @param wherefrom Where to pop from (LEFT or RIGHT).
     * @param whereto Where to push to (LEFT or RIGHT).
     * @return Reference to the derived class.
     * @see https://redis.io/commands/lmove
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::string>> &&>,
                     Derived &>
    lmove(Func &&func, const std::string &source, const std::string &destination,
          ListPosition wherefrom, ListPosition whereto) {
        return derived().template command<std::optional<std::string>>(
            std::forward<Func>(func), "LMOVE", source, destination,
            std::to_string(wherefrom), std::to_string(whereto));
    }

    /**
     * @brief Pop elements from the first non-empty list key from the list of provided
     * key names.
     * @param keys List of keys to check.
     * @param position Where to pop from (LEFT or RIGHT).
     * @param count Number of elements to pop (optional).
     * @return A pair containing the key name and the popped elements.
     * @note If all lists are empty, returns `std::nullopt`.
     * @see https://redis.io/commands/lmpop
     */
    // Todo : implement lmpop
    // std::optional<std::pair<std::string, std::vector<std::string>>>
    // lmpop(const std::vector<std::string> &keys, ListPosition position, long long
    // count) {
    //     if (keys.size() == 0 || count < 1) {
    //         return std::nullopt;
    //     }
    //     return derived()
    //         .template command<std::optional<std::pair<std::string,
    //         std::vector<std::string>>>>(
    //             "LMPOP",
    //             1,
    //             keys,
    //             std::to_string(position),
    //             count)
    //         .result();
    //// }
    ///**
    // * @brief Pop elements from the first non-empty list key asynchronously.
    // * @param func Callback function to handle the result.
    // * @param keys List of keys to check.
    // * @param position Where to pop from (LEFT or RIGHT).
    // * @param count Number of elements to pop (optional).
    // * @return Reference to the derived class.
    // */
    // template <typename Func>
    // std::enable_if_t<std::is_invocable_v<Func,
    // Reply<std::optional<std::pair<std::string, std::vector<std::string>>>> &&>,
    // Derived &> lmpop(Func &&func, const std::vector<std::string> &keys, ListPosition
    // position, long long count) {
    //    return derived()
    //        .template command<std::optional<std::pair<std::string,
    //        std::vector<std::string>>>>(
    //            std::forward<Func>(func),
    //            "LMPOP",
    //            1,
    //            keys,
    //            std::to_string(position),
    //            count);
    //}

    /**
     * @brief Get the position of an element in a list.
     * @param key Key where the list is stored.
     * @param element Element to search for.
     * @param rank Rank of the element (optional).
     * @param count Number of matches to return (optional).
     * @param maxlen Maximum number of elements to scan (optional).
     * @return Vector of positions where the element was found.
     * @note If element is not found, returns empty vector.
     * @see https://redis.io/commands/lpos
     */
    std::vector<long long>
    lpos(const std::string &key, const std::string &element,
         std::optional<long long> rank   = std::nullopt,
         std::optional<long long> count  = std::nullopt,
         std::optional<long long> maxlen = std::nullopt) {
        if (key.empty() || element.empty()) {
            return {};
        }
        std::vector<std::string> args;
        args.reserve(6); // Reserve space for all possible arguments

        if (rank) {
            args.push_back("RANK");
            args.push_back(std::to_string(*rank));
        }
        args.push_back("COUNT");
        args.push_back(count ? std::to_string(*count) : "0");
        if (maxlen) {
            args.push_back("MAXLEN");
            args.push_back(std::to_string(*maxlen));
        }

        return derived()
            .template command<std::vector<long long>>("LPOS", key, element, args)
            .result();
    }

    /**
     * @brief Get the position of an element in a list asynchronously.
     * @param func Callback function to handle the result.
     * @param key Key where the list is stored.
     * @param element Element to search for.
     * @param rank Rank of the element (optional).
     * @param count Number of matches to return (optional).
     * @param maxlen Maximum number of elements to scan (optional).
     * @return Reference to the derived class.
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<long long>> &&>,
                     Derived &>
    lpos(Func &&func, const std::string &key, const std::string &element,
         std::optional<long long> rank   = std::nullopt,
         std::optional<long long> count  = std::nullopt,
         std::optional<long long> maxlen = std::nullopt) {
        if (key.empty() || element.empty()) {
            return derived();
        }
        std::vector<std::string> args;
        args.reserve(6); // Reserve space for all possible arguments

        if (rank) {
            args.push_back("RANK");
            args.push_back(std::to_string(*rank));
        }
        args.push_back("COUNT");
        args.push_back(count ? std::to_string(*count) : "0");
        if (maxlen) {
            args.push_back("MAXLEN");
            args.push_back(std::to_string(*maxlen));
        }

        return derived().template command<std::vector<long long>>(
            std::forward<Func>(func), "LPOS", key, element, args);
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_LIST_COMMANDS_H