/*
 * qb - C++ Actor Framework
 * Copyright (C) 2011-2026 isndev (cpp.actor). All rights reserved.
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
 * of strings. Commands return awaiters for coroutine-first async I/O.
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
     * @brief Get the length of the list (coroutine awaitable).
     * @param key Key where the list is stored.
     * @return redis_awaiter yielding Reply<long long>
     * @see https://redis.io/commands/llen
     */
    auto
    llen(const std::string &key) {
        return derived().template make_coro_command<long long>([this, key](auto &&callback) { this->llen(std::move(callback), key); });
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
        return derived().template command<long long>(std::forward<Func>(func), "LLEN", key);
    }

    // =============== Push Operations ===============

    /**
     * @brief Push multiple elements to the beginning of the list (coroutine awaitable).
     * @param key Key where the list is stored.
     * @param values Values to be pushed.
     * @return redis_awaiter yielding Reply<long long>
     * @see https://redis.io/commands/lpush
     */
    template <typename... Args>
    auto
    lpush(const std::string &key, Args &&...args) {
        return derived().template make_coro_command<long long>([this, key, ... args = std::forward<Args>(args)](auto &&callback) mutable {
            this->lpush(std::move(callback), key, std::forward<decltype(args)>(args)...);
        });
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
        return derived().template command<long long>(std::forward<Func>(func), "LPUSH", key, std::forward<Args>(args)...);
    }

    /**
     * @brief Push an element to the beginning of the list, only if the list already exists (coroutine awaitable).
     * @param key Key where the list is stored.
     * @param val Element to be pushed.
     * @return redis_awaiter yielding Reply<long long>
     * @see https://redis.io/commands/lpushx
     */
    template <typename... Args>
    auto
    lpushx(const std::string &key, Args &&...args) {
        return derived().template make_coro_command<long long>([this, key, ... args = std::forward<Args>(args)](auto &&callback) mutable {
            this->lpushx(std::move(callback), key, std::forward<decltype(args)>(args)...);
        });
    }

    /**
     * @brief Push an element to the beginning of the list asynchronously, only if the list exists.
     * @param func Callback function to handle the result.
     * @param key Key where the list is stored.
     * @param val Element to be pushed.
     * @return Reference to the derived class.
     * @see https://redis.io/commands/lpushx
     */
    template <typename Func, typename... Args>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    lpushx(Func &&func, const std::string &key, Args &&...args) {
        return derived().template command<long long>(std::forward<Func>(func), "LPUSHX", key, std::forward<Args>(args)...);
    }

    /**
     * @brief Push multiple elements to the end of the list (coroutine awaitable).
     * @param key Key where the list is stored.
     * @param values Values to be pushed.
     * @return redis_awaiter yielding Reply<long long>
     * @see https://redis.io/commands/rpush
     */
    template <typename... Args>
    auto
    rpush(const std::string &key, Args &&...args) {
        return derived().template make_coro_command<long long>([this, key, ... args = std::forward<Args>(args)](auto &&callback) mutable {
            this->rpush(std::move(callback), key, std::forward<decltype(args)>(args)...);
        });
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
        return derived().template command<long long>(std::forward<Func>(func), "RPUSH", key, std::forward<Args>(args)...);
    }

    /**
     * @brief Push element(s) to the end of the list, only if the list already exists (coroutine awaitable).
     * @param key Key where the list is stored.
     * @param args Elements to be pushed.
     * @return redis_awaiter yielding Reply<long long>
     * @see https://redis.io/commands/rpushx
     */
    template <typename... Args>
    auto
    rpushx(const std::string &key, Args &&...args) {
        return derived().template make_coro_command<long long>([this, key, ... args = std::forward<Args>(args)](auto &&callback) mutable {
            this->rpushx(std::move(callback), key, std::forward<decltype(args)>(args)...);
        });
    }

    /**
     * @brief Push an element to the end of the list asynchronously, only if the list exists.
     * @param func Callback function to handle the result.
     * @param key Key where the list is stored.
     * @param val Element to be pushed.
     * @return Reference to the derived class.
     * @see https://redis.io/commands/rpushx
     */
    template <typename Func, typename... Args>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    rpushx(Func &&func, const std::string &key, Args &&...args) {
        return derived().template command<long long>(std::forward<Func>(func), "RPUSHX", key, std::forward<Args>(args)...);
    }

    // =============== Pop Operations ===============

    /**
     * @brief Pop the first element(s) of the list (coroutine awaitable).
     * @param key Key where the list is stored.
     * @param count Number of elements to pop (required). The single-element
     *              overload (no count argument) returns std::optional<std::string>;
     *              this count overload returns std::vector<std::string>.
     * @return redis_awaiter yielding Reply<std::vector<std::string>>
     * @see https://redis.io/commands/lpop
     */
    auto
    lpop(const std::string &key, long long count) {
        return derived().template make_coro_command<std::vector<std::string>>(
            [this, key, count](auto &&callback) { this->lpop(std::move(callback), key, count); });
    }

    /**
     * @brief Pop the first element(s) of the list asynchronously.
     * @param func Callback function to handle the result.
     * @param key Key where the list is stored.
     * @param count Number of elements to pop (required). The single-element
     *              overload (no count argument) returns std::optional<std::string>;
     *              this count overload returns std::vector<std::string>.
     * @return Reference to the derived class.
     * @see https://redis.io/commands/lpop
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>, Derived &>
    lpop(Func &&func, const std::string &key, long long count) {
        return derived().template command<std::vector<std::string>>(std::forward<Func>(func), "LPOP", key, count);
    }

    /**
     * @brief Pop a single element from the left of the list (coroutine awaitable).
     * @param key Key where the list is stored.
     * @return redis_awaiter yielding Reply<std::optional<std::string>>
     * @see https://redis.io/commands/lpop
     */
    auto
    lpop(const std::string &key) {
        return derived().template make_coro_command<std::optional<std::string>>(
            [this, key](auto &&callback) { this->lpop(std::move(callback), key); });
    }

    /**
     * @brief Pop a single element from the left of the list asynchronously.
     * @param func Callback function to handle the result.
     * @param key Key where the list is stored.
     * @return Reference to the derived class.
     * @see https://redis.io/commands/lpop
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::string>> &&>, Derived &>
    lpop(Func &&func, const std::string &key) {
        return derived().template command<std::optional<std::string>>(std::forward<Func>(func), "LPOP", key);
    }

    /**
     * @brief Pop the last element(s) of the list (coroutine awaitable).
     * @param key Key where the list is stored.
     * @param count Number of elements to pop (required). The single-element
     *              overload (no count argument) returns std::optional<std::string>;
     *              this count overload returns std::vector<std::string>.
     * @return redis_awaiter yielding Reply<std::vector<std::string>>
     * @see https://redis.io/commands/rpop
     */
    auto
    rpop(const std::string &key, long long count) {
        return derived().template make_coro_command<std::vector<std::string>>(
            [this, key, count](auto &&callback) { this->rpop(std::move(callback), key, count); });
    }

    /**
     * @brief Pop the last element(s) of the list asynchronously.
     * @param func Callback function to handle the result.
     * @param key Key where the list is stored.
     * @param count Number of elements to pop (required). The single-element
     *              overload (no count argument) returns std::optional<std::string>;
     *              this count overload returns std::vector<std::string>.
     * @return Reference to the derived class.
     * @see https://redis.io/commands/rpop
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>, Derived &>
    rpop(Func &&func, const std::string &key, long long count) {
        return derived().template command<std::vector<std::string>>(std::forward<Func>(func), "RPOP", key, count);
    }

    /**
     * @brief Pop a single element from the right of the list (coroutine awaitable).
     * @param key Key where the list is stored.
     * @return redis_awaiter yielding Reply<std::optional<std::string>>
     * @see https://redis.io/commands/rpop
     */
    auto
    rpop(const std::string &key) {
        return derived().template make_coro_command<std::optional<std::string>>(
            [this, key](auto &&callback) { this->rpop(std::move(callback), key); });
    }

    /**
     * @brief Pop a single element from the right of the list asynchronously.
     * @param func Callback function to handle the result.
     * @param key Key where the list is stored.
     * @return Reference to the derived class.
     * @see https://redis.io/commands/rpop
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::string>> &&>, Derived &>
    rpop(Func &&func, const std::string &key) {
        return derived().template command<std::optional<std::string>>(std::forward<Func>(func), "RPOP", key);
    }

    // =============== Blocking Operations ===============

    /**
     * @brief Pop the first element of the list in a blocking way (coroutine awaitable).
     * @param keys List of keys to check.
     * @param timeout Timeout in seconds. 0 means block forever.
     * @return redis_awaiter yielding Reply<std::optional<std::pair<std::string, std::string>>>
     * @see https://redis.io/commands/blpop
     */
    auto
    blpop(const std::vector<std::string> &keys, long long timeout = 0) {
        return derived().template make_coro_command<std::optional<std::pair<std::string, std::string>>>(
            [this, keys, timeout](auto &&callback) { this->blpop(std::move(callback), keys, timeout); });
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::pair<std::string, std::string>>> &&>, Derived &>
    blpop(Func &&func, const std::vector<std::string> &keys, long long timeout) {
        return derived().template command<std::optional<std::pair<std::string, std::string>>>(std::forward<Func>(func), "BLPOP", keys, timeout);
    }

    /**
     * @brief Pop the first element of the list in a blocking way (chrono version, coroutine awaitable).
     * @param keys List of keys to check.
     * @param timeout Timeout duration.
     * @return redis_awaiter yielding Reply<std::optional<std::pair<std::string, std::string>>>
     * @see https://redis.io/commands/blpop
     */
    auto
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::pair<std::string, std::string>>> &&>, Derived &>
    blpop(Func &&func, const std::vector<std::string> &keys, const std::chrono::seconds &timeout) {
        return blpop(std::forward<Func>(func), keys, timeout.count());
    }

    /**
     * @brief Pop the last element of the list in a blocking way (coroutine awaitable).
     * @param keys List of keys to check.
     * @param timeout Timeout in seconds. 0 means block forever.
     * @return redis_awaiter yielding Reply<std::optional<std::pair<std::string, std::string>>>
     * @see https://redis.io/commands/brpop
     */
    auto
    brpop(const std::vector<std::string> &keys, long long timeout = 0) {
        return derived().template make_coro_command<std::optional<std::pair<std::string, std::string>>>(
            [this, keys, timeout](auto &&callback) { this->brpop(std::move(callback), keys, timeout); });
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::pair<std::string, std::string>>> &&>, Derived &>
    brpop(Func &&func, const std::vector<std::string> &keys, long long timeout) {
        return derived().template command<std::optional<std::pair<std::string, std::string>>>(std::forward<Func>(func), "BRPOP", keys, timeout);
    }

    /**
     * @brief Pop the last element of the list in a blocking way (chrono version, coroutine awaitable).
     * @param keys List of keys to check.
     * @param timeout Timeout duration.
     * @return redis_awaiter yielding Reply<std::optional<std::pair<std::string, std::string>>>
     * @see https://redis.io/commands/brpop
     */
    auto
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::pair<std::string, std::string>>> &&>, Derived &>
    brpop(Func &&func, const std::vector<std::string> &keys, const std::chrono::seconds &timeout) {
        return brpop(std::forward<Func>(func), keys, timeout.count());
    }

    // =============== List Manipulation Operations ===============

    /**
     * @brief Get the element at the given index of the list (coroutine awaitable).
     * @param key Key where the list is stored.
     * @param index Zero-base index, and -1 means the last element.
     * @return redis_awaiter yielding Reply<std::optional<std::string>>
     * @see https://redis.io/commands/lindex
     */
    auto
    lindex(const std::string &key, long long index) {
        return derived().template make_coro_command<std::optional<std::string>>(
            [this, key, index](auto &&callback) { this->lindex(std::move(callback), key, index); });
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::string>> &&>, Derived &>
    lindex(Func &&func, const std::string &key, long long index) {
        return derived().template command<std::optional<std::string>>(std::forward<Func>(func), "LINDEX", key, index);
    }

    /**
     * @brief Insert an element to a list before or after the pivot element (coroutine awaitable).
     * @param key Key where the list is stored.
     * @param position Before or after the pivot element.
     * @param pivot The pivot value.
     * @param val Element to be inserted.
     * @return redis_awaiter yielding Reply<long long>
     * @see https://redis.io/commands/linsert
     */
    auto
    linsert(const std::string &key, InsertPosition position, const std::string &pivot, const std::string &val) {
        return derived().template make_coro_command<long long>(
            [this, key, position, pivot, val](auto &&callback) { this->linsert(std::move(callback), key, position, pivot, val); });
    }

    /**
     * @brief Insert an element to a list asynchronously.
     * @param func Callback function to handle the result.
     * @param key Key where the list is stored.
     * @param position Before or after the pivot element.
     * @param pivot The pivot value.
     * @param val Element to be inserted.
     * @return Reference to the derived class.
     * @see https://redis.io/commands/linsert
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    linsert(Func &&func, const std::string &key, InsertPosition position, const std::string &pivot, const std::string &val) {
        return derived().template command<long long>(std::forward<Func>(func), "LINSERT", key, to_string(position), pivot, val);
    }

    /**
     * @brief Get elements in the given range of the given list (coroutine awaitable).
     * @param key Key where the list is stored.
     * @param start Start index of the range.
     * @param stop End index of the range.
     * @return redis_awaiter yielding Reply<std::vector<std::string>>
     * @see https://redis.io/commands/lrange
     */
    auto
    lrange(const std::string &key, long long start, long long stop) {
        return derived().template make_coro_command<std::vector<std::string>>(
            [this, key, start, stop](auto &&callback) { this->lrange(std::move(callback), key, start, stop); });
    }

    /**
     * @brief Get elements in the given range of the given list asynchronously.
     * @param func Callback function to handle the result.
     * @param key Key where the list is stored.
     * @param start Start index of the range.
     * @param stop End index of the range.
     * @return Reference to the derived class.
     * @see https://redis.io/commands/lrange
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>, Derived &>
    lrange(Func &&func, const std::string &key, long long start, long long stop) {
        return derived().template command<std::vector<std::string>>(std::forward<Func>(func), "LRANGE", key, start, stop);
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
    auto
    lrem(const std::string &key, long long count, const std::string &val) {
        return derived().template make_coro_command<long long>(
            [this, key, count, val](auto &&callback) { this->lrem(std::move(callback), key, count, val); });
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
        return derived().template command<long long>(std::forward<Func>(func), "LREM", key, count, val);
    }

    /**
     * @brief Set the element at the given index to the specified value.
     * @param key Key where the list is stored.
     * @param index Index of the element to be set.
     * @param val Value.
     * @return status object indicating success or failure.
     * @see https://redis.io/commands/lset
     */
    auto
    lset(const std::string &key, long long index, const std::string &val) {
        return derived().template make_coro_command<status>(
            [this, key, index, val](auto &&callback) { this->lset(std::move(callback), key, index, val); });
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
        return derived().template command<status>(std::forward<Func>(func), "LSET", key, index, val);
    }

    /**
     * @brief Trim a list to keep only element in the given range.
     * @param key Key where the key is stored.
     * @param start Start of the index.
     * @param stop End of the index.
     * @return status object indicating success or failure.
     * @see https://redis.io/commands/ltrim
     */
    auto
    ltrim(const std::string &key, long long start, long long stop) {
        return derived().template make_coro_command<status>(
            [this, key, start, stop](auto &&callback) { this->ltrim(std::move(callback), key, start, stop); });
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
        return derived().template command<status>(std::forward<Func>(func), "LTRIM", key, start, stop);
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
    auto
    rpoplpush(const std::string &source, const std::string &destination) {
        return derived().template make_coro_command<std::optional<std::string>>(
            [this, source, destination](auto &&callback) { this->rpoplpush(std::move(callback), source, destination); });
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::string>> &&>, Derived &>
    rpoplpush(Func &&func, const std::string &source, const std::string &destination) {
        return derived().template command<std::optional<std::string>>(std::forward<Func>(func), "RPOPLPUSH", source, destination);
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
    auto
    lmove(const std::string &source, const std::string &destination, ListPosition wherefrom, ListPosition whereto) {
        return derived().template make_coro_command<std::optional<std::string>>(
            [this, source, destination, wherefrom, whereto](auto &&callback) {
                this->lmove(std::move(callback), source, destination, wherefrom, whereto);
            });
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::string>> &&>, Derived &>
    lmove(Func &&func, const std::string &source, const std::string &destination, ListPosition wherefrom, ListPosition whereto) {
        return derived().template command<std::optional<std::string>>(std::forward<Func>(func), "LMOVE", source, destination,
                                                                      to_string(wherefrom), to_string(whereto));
    }

    /**
     * @brief Pop elements from the first non-empty list key from the list of provided
     * key names.
     * @param keys List of keys to check.
     * @param position Where to pop from (LEFT or RIGHT).
     * @param count Number of elements to pop (optional, default 1).
     * @return A pair containing the key name and the popped elements.
     * @note If all lists are empty, returns `std::nullopt`.
     * @see https://redis.io/commands/lmpop
     */
    auto
    lmpop(const std::vector<std::string> &keys, ListPosition position, long long count = 1) {
        return derived().template make_coro_command<std::optional<std::pair<std::string, std::vector<std::string>>>>(
            [this, keys, position, count](auto &&callback) { this->lmpop(std::move(callback), keys, position, count); });
    }

    /**
     * @brief Asynchronous version of lmpop
     * @see https://redis.io/commands/lmpop
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::pair<std::string, std::vector<std::string>>>> &&>, Derived &>
    lmpop(Func &&func, const std::vector<std::string> &keys, ListPosition position, long long count = 1) {
        if (keys.empty()) {
            return derived();
        }
        std::vector<std::string> opt;
        if (count > 1) {
            opt.push_back("COUNT");
            opt.push_back(std::to_string(count));
        }
        return derived().template command<std::optional<std::pair<std::string, std::vector<std::string>>>>(
            std::forward<Func>(func), "LMPOP", keys.size(), keys, to_string(position), opt);
    }

    /**
     * @brief Blocking variant of LMPOP (coroutine awaitable).
     * @param keys List of keys to check.
     * @param position Where to pop from (LEFT or RIGHT).
     * @param timeout Timeout in seconds. 0 means block forever.
     * @param count Number of elements to pop (required). The single-element
     *              overload (no count argument) returns std::optional<std::string>;
     *              this count overload returns std::vector<std::string>.
     * @see https://redis.io/commands/blmpop
     */
    auto
    blmpop(const std::vector<std::string> &keys, ListPosition position, long long timeout, long long count = 1) {
        return derived().template make_coro_command<std::optional<std::pair<std::string, std::vector<std::string>>>>(
            [this, keys, position, timeout, count](auto &&callback) { this->blmpop(std::move(callback), keys, position, timeout, count); });
    }

    /**
     * @brief Asynchronous version of blmpop
     * @see https://redis.io/commands/blmpop
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::pair<std::string, std::vector<std::string>>>> &&>, Derived &>
    blmpop(Func &&func, const std::vector<std::string> &keys, ListPosition position, long long timeout, long long count = 1) {
        if (keys.empty()) {
            return derived();
        }
        std::vector<std::string> opt;
        if (count > 1) {
            opt.push_back("COUNT");
            opt.push_back(std::to_string(count));
        }
        return derived().template command<std::optional<std::pair<std::string, std::vector<std::string>>>>(
            std::forward<Func>(func), "BLMPOP", timeout, keys.size(), keys, to_string(position), opt);
    }

    /**
     * @brief Blocking variant of LMOVE (coroutine awaitable).
     * @param source Source list key.
     * @param destination Destination list key.
     * @param wherefrom Where to pop from (LEFT or RIGHT).
     * @param whereto Where to push to (LEFT or RIGHT).
     * @param timeout Timeout in seconds. 0 means block forever.
     * @see https://redis.io/commands/blmove
     */
    auto
    blmove(const std::string &source, const std::string &destination, ListPosition wherefrom, ListPosition whereto, long long timeout) {
        return derived().template make_coro_command<std::optional<std::string>>(
            [this, source, destination, wherefrom, whereto, timeout](auto &&callback) {
                this->blmove(std::move(callback), source, destination, wherefrom, whereto, timeout);
            });
    }

    /**
     * @brief Asynchronous version of blmove
     * @see https://redis.io/commands/blmove
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::string>> &&>, Derived &>
    blmove(Func &&func, const std::string &source, const std::string &destination, ListPosition wherefrom, ListPosition whereto,
           long long timeout) {
        return derived().template command<std::optional<std::string>>(std::forward<Func>(func), "BLMOVE", source, destination,
                                                                      to_string(wherefrom), to_string(whereto), timeout);
    }

    /**
     * @brief Pop element from source list, push to destination, blocking (coroutine awaitable).
     * @deprecated Use blmove instead.
     * @see https://redis.io/commands/brpoplpush
     */
    auto
    brpoplpush(const std::string &source, const std::string &destination, long long timeout) {
        return derived().template make_coro_command<std::optional<std::string>>(
            [this, source, destination, timeout](auto &&callback) { this->brpoplpush(std::move(callback), source, destination, timeout); });
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::string>> &&>, Derived &>
    brpoplpush(Func &&func, const std::string &source, const std::string &destination, long long timeout) {
        return derived().template command<std::optional<std::string>>(std::forward<Func>(func), "BRPOPLPUSH", source, destination, timeout);
    }

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
    auto
    lpos(const std::string &key, const std::string &element, std::optional<long long> rank = std::nullopt,
         std::optional<long long> count = std::nullopt, std::optional<long long> maxlen = std::nullopt) {
        return derived().template make_coro_command<std::vector<long long>>([this, key, element, rank, count, maxlen](auto &&callback) mutable {
            this->lpos(std::move(callback), key, element, std::move(rank), std::move(count), std::move(maxlen));
        });
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
     * @see https://redis.io/commands/lpos
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<long long>> &&>, Derived &>
    lpos(Func &&func, const std::string &key, const std::string &element, std::optional<long long> rank = std::nullopt,
         std::optional<long long> count = std::nullopt, std::optional<long long> maxlen = std::nullopt) {
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

        return derived().template command<std::vector<long long>>(std::forward<Func>(func), "LPOS", key, element, args);
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_LIST_COMMANDS_H