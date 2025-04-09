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

#ifndef QBM_REDIS_STRING_COMMANDS_H
#define QBM_REDIS_STRING_COMMANDS_H
#include <bitset>
#include "reply.h"

namespace qb::redis {

/**
 * @class string_commands
 * @brief Provides Redis string command implementations for manipulating string values.
 *
 * This class implements all Redis string-related commands, providing a comprehensive
 * set of operations for working with string values in Redis. Redis strings are the
 * most basic data type and can store text, serialized objects, counters, bitmaps,
 * or any binary data up to 512MB in size.
 *
 * Key features of Redis strings:
 * - Binary-safe: can store any kind of data, including binary data
 * - Maximum size of 512MB
 * - Support for atomic operations like increment/decrement
 * - Ability to set expiration times on string values
 * - Bit-level operations (handled by bitmap_commands class)
 *
 * Each command is available in both synchronous and asynchronous versions:
 * - Synchronous: Returns the result directly and blocks until completed
 * - Asynchronous: Takes a callback function that will be invoked when the operation
 * completes
 *
 * @tparam Derived The derived class type (CRTP pattern)
 */
template <typename Derived>
class string_commands {
private:
    constexpr Derived &
    derived() {
        return static_cast<Derived &>(*this);
    }

public:
    /**
     * @brief Append a value to the end of a string stored at key.
     *
     * If the key exists and is a string, this command appends the specified value
     * to the end of the string. If the key does not exist, it is created with an
     * empty string value before appending.
     *
     * @param key The key storing the string value
     * @param val The string to append to the existing value
     * @return The length of the string after the append operation
     * @note If the key exists but is not a string, Redis will return an error
     * @note Time complexity: O(1) - Constant time complexity regardless of the size of
     * the strings
     * @see https://redis.io/commands/append
     */
    long long
    append(const std::string &key, const std::string &val) {
        return derived().template command<long long>("APPEND", key, val).result();
    }

    /**
     * @brief Asynchronous version of the APPEND command.
     *
     * @param func Callback function to be invoked when the operation completes
     * @param key The key storing the string value
     * @param val The string to append to the existing value
     * @return Reference to the derived Redis client for method chaining
     * @see https://redis.io/commands/append
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    append(Func &&func, const std::string &key, const std::string &val) {
        return derived().template command<long long>(std::forward<Func>(func), "APPEND",
                                                     key, val);
    }

    /**
     * @brief Decrement the integer value stored at key by one.
     *
     * Decrements the number stored at key by one. If the key does not exist, it is set
     * to 0 before performing the operation. If the key contains a value that cannot be
     * represented as an integer, Redis will return an error.
     *
     * @param key The key storing the numeric string value
     * @return The value after the decrement operation
     * @note If the key does not exist, it is initialized as 0 before decrementing
     * @note If the value at key is not an integer, Redis returns an error
     * @note This operation is limited to 64-bit signed integers
     * @note Time complexity: O(1)
     * @see https://redis.io/commands/decr
     */
    long long
    decr(const std::string &key) {
        return derived().template command<long long>("DECR", key).result();
    }

    /**
     * @brief Asynchronous version of the DECR command.
     *
     * @param func Callback function to be invoked when the operation completes
     * @param key The key storing the numeric string value
     * @return Reference to the derived Redis client for method chaining
     * @see https://redis.io/commands/decr
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    decr(Func &&func, const std::string &key) {
        return derived().template command<long long>(std::forward<Func>(func), "DECR",
                                                     key);
    }

    /**
     * @brief Decrement the integer value stored at key by the specified amount.
     *
     * Decrements the number stored at key by decrement. If the key does not exist,
     * it is set to 0 before performing the operation. If the key contains a value
     * that cannot be represented as an integer, Redis will return an error.
     *
     * @param key The key storing the numeric string value
     * @param decrement The amount to decrement the value by
     * @return The value after the decrement operation
     * @note If the key does not exist, it is initialized as 0 before decrementing
     * @note If the value at key is not an integer, Redis returns an error
     * @note This operation is limited to 64-bit signed integers
     * @note Time complexity: O(1)
     * @see https://redis.io/commands/decrby
     */
    long long
    decrby(const std::string &key, long long decrement) {
        return derived().template command<long long>("DECRBY", key, decrement).result();
    }

    /**
     * @brief Asynchronous version of the DECRBY command.
     *
     * @param func Callback function to be invoked when the operation completes
     * @param key The key storing the numeric string value
     * @param decrement The amount to decrement the value by
     * @return Reference to the derived Redis client for method chaining
     * @see https://redis.io/commands/decrby
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    decrby(Func &&func, const std::string &key, long long decrement) {
        return derived().template command<long long>(std::forward<Func>(func), "DECRBY",
                                                     key, decrement);
    }

    /**
     * @brief Get the string value stored at key.
     *
     * Returns the value of the string stored at key. If the key does not exist,
     * a null optional is returned (std::nullopt).
     *
     * @param key The key to retrieve the value for
     * @return An optional containing the string value if the key exists, or std::nullopt
     * if it doesn't
     * @note If the key exists but holds a non-string value, Redis will return an error
     * @note Maximum string size that can be retrieved is 512MB
     * @note Time complexity: O(1) for small strings, O(N) for large strings
     * @see https://redis.io/commands/get
     */
    std::optional<std::string>
    get(const std::string &key) {
        return derived()
            .template command<std::optional<std::string>>("GET", key)
            .result();
    }

    /**
     * @brief Asynchronous version of the GET command.
     *
     * @param func Callback function to be invoked when the operation completes
     * @param key The key to retrieve the value for
     * @return Reference to the derived Redis client for method chaining
     * @see https://redis.io/commands/get
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::string>> &&>,
                     Derived &>
    get(Func &&func, const std::string &key) {
        return derived().template command<std::optional<std::string>>(
            std::forward<Func>(func), "GET", key);
    }

    /**
     * @brief Get a substring of the string stored at key.
     *
     * Returns the substring of the string value stored at key, determined by the offsets
     * start and end (both inclusive). Negative offsets can be used to specify positions
     * from the end of the string.
     *
     * @param key The key storing the string value
     * @param start Start offset (inclusive), 0-based. Negative values count from the end
     * of the string
     * @param end End offset (inclusive). Negative values count from the end of the
     * string
     * @return The substring extracted from the string value
     * @note If the key does not exist, an empty string is returned
     * @note If start/end are out of range, they are limited to the actual string
     * boundaries
     * @note Time complexity: O(N) where N is the length of the returned string
     * @see https://redis.io/commands/getrange
     */
    std::string
    getrange(const std::string &key, long long start, long long end) {
        return derived()
            .template command<std::string>("GETRANGE", key, start, end)
            .result();
    }

    /**
     * @brief Asynchronous version of the GETRANGE command.
     *
     * @param func Callback function to be invoked when the operation completes
     * @param key The key storing the string value
     * @param start Start offset (inclusive), 0-based
     * @param end End offset (inclusive)
     * @return Reference to the derived Redis client for method chaining
     * @see https://redis.io/commands/getrange
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::string> &&>, Derived &>
    getrange(Func &&func, const std::string &key, long long start, long long end) {
        return derived().template command<std::string>(std::forward<Func>(func),
                                                       "GETRANGE", key, start, end);
    }

    /**
     * @brief Atomically set a string value and return the old value.
     *
     * Sets the string value at key and returns the old value. If the key did not exist,
     * a null optional (std::nullopt) is returned.
     *
     * @param key The key to set
     * @param val The new string value to set
     * @return An optional containing the previous string value if the key existed, or
     * std::nullopt if it didn't
     * @note This operation is atomic, so it can be used to implement leader election
     * @note If the key exists but holds a non-string value, Redis will return an error
     * @note Time complexity: O(1) for small strings, O(N) for large strings where N is
     * the length of the string
     * @see https://redis.io/commands/getset
     */
    std::optional<std::string>
    getset(const std::string &key, const std::string &val) {
        return derived()
            .template command<std::optional<std::string>>("GETSET", key, val)
            .result();
    }

    /**
     * @brief Asynchronous version of the GETSET command.
     *
     * @param func Callback function to be invoked when the operation completes
     * @param key The key to set
     * @param val The new string value to set
     * @return Reference to the derived Redis client for method chaining
     * @see https://redis.io/commands/getset
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::string>> &&>,
                     Derived &>
    getset(Func &&func, const std::string &key, const std::string &val) {
        return derived().template command<std::optional<std::string>>(
            std::forward<Func>(func), "GETSET", key, val);
    }

    /**
     * @brief Increment the integer value stored at key by one.
     *
     * Increments the number stored at key by one. If the key does not exist, it is set
     * to 0 before performing the operation. If the key contains a value that cannot be
     * represented as an integer, Redis will return an error.
     *
     * @param key The key storing the numeric string value
     * @return The value after the increment operation
     * @note If the key does not exist, it is initialized as 0 before incrementing
     * @note If the value at key is not an integer, Redis returns an error
     * @note This operation is limited to 64-bit signed integers
     * @note Time complexity: O(1)
     * @see https://redis.io/commands/incr
     */
    long long
    incr(const std::string &key) {
        return derived().template command<long long>("INCR", key).result();
    }

    /**
     * @brief Asynchronous version of the INCR command.
     *
     * @param func Callback function to be invoked when the operation completes
     * @param key The key storing the numeric string value
     * @return Reference to the derived Redis client for method chaining
     * @see https://redis.io/commands/incr
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    incr(Func &&func, const std::string &key) {
        return derived().template command<long long>(std::forward<Func>(func), "INCR",
                                                     key);
    }

    /**
     * @brief Increment the integer value stored at key by the specified amount.
     *
     * Increments the number stored at key by increment. If the key does not exist,
     * it is set to 0 before performing the operation. If the key contains a value
     * that cannot be represented as an integer, Redis will return an error.
     *
     * @param key The key storing the numeric string value
     * @param increment The amount to increment the value by
     * @return The value after the increment operation
     * @note If the key does not exist, it is initialized as 0 before incrementing
     * @note If the value at key is not an integer, Redis returns an error
     * @note This operation is limited to 64-bit signed integers
     * @note Time complexity: O(1)
     * @see https://redis.io/commands/incrby
     */
    long long
    incrby(const std::string &key, long long increment) {
        return derived().template command<long long>("INCRBY", key, increment).result();
    }

    /**
     * @brief Asynchronous version of the INCRBY command.
     *
     * @param func Callback function to be invoked when the operation completes
     * @param key The key storing the numeric string value
     * @param increment The amount to increment the value by
     * @return Reference to the derived Redis client for method chaining
     * @see https://redis.io/commands/incrby
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    incrby(Func &&func, const std::string &key, long long increment) {
        return derived().template command<long long>(std::forward<Func>(func), "INCRBY",
                                                     key, increment);
    }

    /**
     * @brief Increment the floating point value stored at key by the specified amount.
     *
     * Increments the floating point number stored at key by increment. If the key does
     * not exist, it is set to 0 before performing the operation. If the key contains a
     * value that cannot be represented as a floating point number, Redis will return an
     * error.
     *
     * @param key The key storing the numeric string value
     * @param increment The amount to increment the value by (can be negative for
     * decrement)
     * @return The value after the increment operation as a double
     * @note If the key does not exist, it is initialized as 0 before incrementing
     * @note The precision of the floating point operations follows IEEE 754 standard
     * @note Time complexity: O(1)
     * @see https://redis.io/commands/incrbyfloat
     */
    double
    incrbyfloat(const std::string &key, double increment) {
        return derived()
            .template command<double>("INCRBYFLOAT", key, increment)
            .result();
    }

    /**
     * @brief Asynchronous version of the INCRBYFLOAT command.
     *
     * @param func Callback function to be invoked when the operation completes
     * @param key The key storing the numeric string value
     * @param increment The amount to increment the value by
     * @return Reference to the derived Redis client for method chaining
     * @see https://redis.io/commands/incrbyfloat
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<double> &&>, Derived &>
    incrbyfloat(Func &&func, const std::string &key, double increment) {
        return derived().template command<double>(std::forward<Func>(func),
                                                  "INCRBYFLOAT", key, increment);
    }

    /**
     * @brief Get the values of multiple keys atomically.
     *
     * Returns the values of all specified keys. For every key that does not exist
     * or is not a string, a null value is returned.
     *
     * @param keys Keys to retrieve values for
     * @return Vector of optional strings containing the values
     * @note Time complexity: O(N) where N is the number of keys requested
     * @see https://redis.io/commands/mget
     */
    std::vector<std::optional<std::string>>
    mget(const std::vector<std::string> &keys) {
        if (keys.size() == 0) {
            return {};
        }
        return derived()
            .template command<std::vector<std::optional<std::string>>>("MGET", keys)
            .result();
    }

    /**
     * @brief Asynchronous version of the MGET command.
     *
     * @param func Callback function to be invoked when the operation completes
     * @param keys Keys to retrieve values for
     * @return Reference to the derived Redis client for method chaining
     * @see https://redis.io/commands/mget
     */
    template <typename Func>
    std::enable_if_t<
        std::is_invocable_v<Func, Reply<std::vector<std::optional<std::string>>> &&>,
        Derived &>
    mget(Func &&func, const std::vector<std::string> &keys) {
        return derived().template command<std::vector<std::optional<std::string>>>(
            std::forward<Func>(func), "MGET", keys);
    }

    /**
     * @brief Set multiple key-value pairs.
     *
     * Sets multiple key-value pairs in a single atomic operation. This command
     * overwrites existing values for the keys being set.
     *
     * @param keys Vector of key-value pairs to set
     * @return status object indicating success or failure
     * @note Time complexity: O(N) where N is the number of key-value pairs
     * @see https://redis.io/commands/mset
     */
    status
    mset(const std::vector<std::pair<std::string, std::string>> &keys) {
        return derived().template command<status>("MSET", keys).result();
    }

    /**
     * @brief Asynchronous version of the MSET command.
     *
     * @param func Callback function to be invoked when the operation completes
     * @param keys Vector of key-value pairs to set
     * @return Reference to the derived Redis client for method chaining
     * @see https://redis.io/commands/mset
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    mset(Func &&func, const std::vector<std::pair<std::string, std::string>> &keys) {
        return derived().template command<status>(std::forward<Func>(func), "MSET",
                                                  keys);
    }

    /**
     * @brief Set multiple key-value pairs only if none of the keys exist.
     *
     * Sets the given keys to their respective values, only if all the keys do not exist.
     * If any of the specified keys already exist, none of the operations are performed.
     *
     * @param keys Key-value pairs to set
     * @return true if all keys were set, false if no operation was performed because at
     * least one key exists
     * @see https://redis.io/commands/msetnx
     */
    bool
    msetnx(const std::vector<std::pair<std::string, std::string>> &keys) {
        if (keys.size() == 0) {
            return false;
        }
        return derived().template command<long long>("MSETNX", keys).result();
    }

    /**
     * @brief Asynchronous version of the MSETNX command.
     *
     * @param func Callback function to be invoked when the operation completes
     * @param keys Key-value pairs to set
     * @return Reference to the derived Redis client for method chaining
     * @see https://redis.io/commands/msetnx
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    msetnx(Func &&func, const std::vector<std::pair<std::string, std::string>> &keys) {
        return derived().template command<long long>(std::forward<Func>(func), "MSETNX",
                                                     keys);
    }

    /**
     * @brief Set a key-value pair with a millisecond precision timeout.
     *
     * Sets the string value at key with a millisecond precision expiration time.
     * If the key already exists, it is overwritten and its TTL is reset.
     *
     * @param key The key to set
     * @param ttl Time-to-live in milliseconds
     * @param val The string value to set
     * @return status object indicating success or failure
     * @note Time complexity: O(1)
     * @see https://redis.io/commands/psetex
     */
    status
    psetex(const std::string &key, long long ttl, const std::string &val) {
        return derived().template command<status>("PSETEX", key, ttl, val).result();
    }

    /**
     * @brief Asynchronous version of the PSETEX command.
     *
     * @param func Callback function to be invoked when the operation completes
     * @param key The key to set
     * @param ttl Time-to-live in milliseconds
     * @param val The string value to set
     * @return Reference to the derived Redis client for method chaining
     * @see https://redis.io/commands/psetex
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    psetex(Func &&func, const std::string &key, long long ttl, const std::string &val) {
        return derived().template command<status>(std::forward<Func>(func), "PSETEX",
                                                  key, ttl, val);
    }

    /**
     * @brief Set a key-value pair with a millisecond precision timeout using
     * std::chrono::milliseconds.
     *
     * Sets the string value at key with a millisecond precision expiration time
     * specified as a std::chrono::milliseconds object.
     *
     * @param key The key to set
     * @param ttl Time-to-live as a std::chrono::milliseconds object
     * @param val The string value to set
     * @return status object indicating success or failure
     * @note Time complexity: O(1)
     * @see https://redis.io/commands/psetex
     */
    status
    psetex(const std::string &key, std::chrono::milliseconds const &ttl,
           const std::string &val) {
        return psetex(key, ttl.count(), val);
    }

    /**
     * @brief Asynchronous version of the PSETEX command with std::chrono::milliseconds.
     *
     * @param func Callback function to be invoked when the operation completes
     * @param key The key to set
     * @param ttl Time-to-live as a std::chrono::milliseconds object
     * @param val The string value to set
     * @return Reference to the derived Redis client for method chaining
     * @see https://redis.io/commands/psetex
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    psetex(Func &&func, const std::string &key, std::chrono::milliseconds const &ttl,
           const std::string &val) {
        return psetex(std::forward<Func>(func), key, ttl.count(), val);
    }

    /**
     * @brief Set a key-value pair with optional conditions.
     *
     * Sets the string value at key with optional update conditions. The command supports
     * various options to control how and when the key is updated.
     *
     * @param key The key to set
     * @param val The string value to set
     * @param type Update condition:
     *             - UpdateType::EXIST: Only set if key already exists
     *             - UpdateType::NOT_EXIST: Only set if key does not exist
     *             - UpdateType::ALWAYS: Always set (default)
     * @return status object indicating success or failure
     * @note Time complexity: O(1)
     * @see https://redis.io/commands/set
     */
    status
    set(const std::string &key, const std::string &val,
        UpdateType type = UpdateType::ALWAYS) {
        std::optional<std::string> opt;
        if (type != UpdateType::ALWAYS)
            opt = std::to_string(type);

        return derived().template command<status>("SET", key, val, opt).result();
    }

    /**
     * @brief Asynchronous version of the SET command with conditions.
     *
     * @param func Callback function to be invoked when the operation completes
     * @param key The key to set
     * @param val The string value to set
     * @param type Update condition (EXIST, NOT_EXIST, or ALWAYS)
     * @return Reference to the derived Redis client for method chaining
     * @see https://redis.io/commands/set
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    set(Func &&func, const std::string &key, const std::string &val,
        UpdateType type = UpdateType::ALWAYS) {
        std::optional<std::string> opt;
        if (type != UpdateType::ALWAYS)
            opt = std::to_string(type);

        return derived().template command<status>(std::forward<Func>(func), "SET", key,
                                                  val, opt);
    }

    /**
     * @brief Set a key-value pair with millisecond precision timeout and conditions.
     *
     * Sets the string value at key with a millisecond precision expiration time
     * and optional update conditions.
     *
     * @param key The key to set
     * @param val The string value to set
     * @param ttl Time-to-live in milliseconds
     * @param type Update condition (EXIST, NOT_EXIST, or ALWAYS)
     * @return status object indicating success or failure
     * @note Time complexity: O(1)
     * @see https://redis.io/commands/set
     */
    status
    set(const std::string &key, const std::string &val, long long ttl,
        UpdateType type = UpdateType::ALWAYS) {
        std::optional<std::string> opt;
        if (type != UpdateType::ALWAYS)
            opt = std::to_string(type);
        return derived()
            .template command<status>("SET", key, val, "PX", ttl, opt)
            .result();
    }

    /**
     * @brief Asynchronous version of the SET command with timeout and conditions.
     *
     * @param func Callback function to be invoked when the operation completes
     * @param key The key to set
     * @param val The string value to set
     * @param ttl Time-to-live in milliseconds
     * @param type Update condition (EXIST, NOT_EXIST, or ALWAYS)
     * @return Reference to the derived Redis client for method chaining
     * @see https://redis.io/commands/set
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    set(Func &&func, const std::string &key, const std::string &val, long long ttl,
        UpdateType type = UpdateType::ALWAYS) {
        std::optional<std::string> opt;
        if (type != UpdateType::ALWAYS)
            opt = std::to_string(type);
        return derived().template command<status>(std::forward<Func>(func), "SET", key,
                                                  val, "PX", ttl, opt);
    }

    /**
     * @brief Set a key-value pair with chrono millisecond precision timeout and
     * conditions.
     *
     * Sets the string value at key with a std::chrono::milliseconds precision expiration
     * time and optional update conditions.
     *
     * @param key The key to set
     * @param val The string value to set
     * @param ttl Time-to-live as a std::chrono::milliseconds object
     * @param type Update condition (EXIST, NOT_EXIST, or ALWAYS)
     * @return status object indicating success or failure
     * @note Time complexity: O(1)
     * @see https://redis.io/commands/set
     */
    status
    set(const std::string &key, const std::string &val,
        const std::chrono::milliseconds &ttl, UpdateType type = UpdateType::ALWAYS) {
        return set(key, val, static_cast<long long>(ttl.count()), type);
    }

    /**
     * @brief Asynchronous version of the SET command with chrono timeout and conditions.
     *
     * @param func Callback function to be invoked when the operation completes
     * @param key The key to set
     * @param val The string value to set
     * @param ttl Time-to-live as a std::chrono::milliseconds object
     * @param type Update condition (EXIST, NOT_EXIST, or ALWAYS)
     * @return Reference to the derived Redis client for method chaining
     * @see https://redis.io/commands/set
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    set(Func &&func, const std::string &key, const std::string &val,
        const std::chrono::milliseconds &ttl, UpdateType type = UpdateType::ALWAYS) {
        return set(std::forward<Func>(func), key, val,
                   static_cast<long long>(ttl.count()), type);
    }

    /**
     * @brief Set a key-value pair with a second precision timeout.
     *
     * Sets the string value at key with a second precision expiration time.
     * If the key already exists, it is overwritten and its TTL is reset.
     *
     * @param key The key to set
     * @param ttl Time-to-live in seconds
     * @param val The string value to set
     * @return status object indicating success or failure
     * @note Time complexity: O(1)
     * @see https://redis.io/commands/setex
     */
    status
    setex(const std::string &key, long long ttl, const std::string &val) {
        return derived().template command<status>("SETEX", key, ttl, val).result();
    }

    /**
     * @brief Asynchronous version of the SETEX command.
     *
     * @param func Callback function to be invoked when the operation completes
     * @param key The key to set
     * @param ttl Time-to-live in seconds
     * @param val The string value to set
     * @return Reference to the derived Redis client for method chaining
     * @see https://redis.io/commands/setex
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    setex(Func &&func, const std::string &key, long long ttl, const std::string &val) {
        return derived().template command<status>(std::forward<Func>(func), "SETEX", key,
                                                  ttl, val);
    }

    /**
     * @brief Set a key-value pair with a chrono second precision timeout.
     *
     * Sets the string value at key with a std::chrono::seconds precision expiration
     * time. If the key already exists, it is overwritten and its TTL is reset.
     *
     * @param key The key to set
     * @param ttl Time-to-live as a std::chrono::seconds object
     * @param val The string value to set
     * @return status object indicating success or failure
     * @note Time complexity: O(1)
     * @see https://redis.io/commands/setex
     */
    status
    setex(const std::string &key, std::chrono::seconds const &ttl,
          const std::string &val) {
        return setex(key, ttl.count(), val);
    }

    /**
     * @brief Asynchronous version of the SETEX command with chrono seconds.
     *
     * @param func Callback function to be invoked when the operation completes
     * @param key The key to set
     * @param ttl Time-to-live as a std::chrono::seconds object
     * @param val The string value to set
     * @return Reference to the derived Redis client for method chaining
     * @see https://redis.io/commands/setex
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    setex(Func &&func, const std::string &key, std::chrono::seconds const &ttl,
          const std::string &val) {
        return setex(std::forward<Func>(func), key, ttl.count(), val);
    }

    /**
     * @brief Set a key-value pair only if the key does not exist.
     *
     * Sets the string value at key, only if the key does not already exist.
     * This is an atomic operation, useful for implementing locks.
     *
     * @param key The key to set
     * @param val The string value to set
     * @return true if the key was set, false if the key was not set (already exists)
     * @note Time complexity: O(1)
     * @see https://redis.io/commands/setnx
     */
    bool
    setnx(const std::string &key, const std::string &val) {
        return derived().template command<bool>("SETNX", key, val).result();
    }

    /**
     * @brief Asynchronous version of the SETNX command.
     *
     * @param func Callback function to be invoked when the operation completes
     * @param key The key to set
     * @param val The string value to set
     * @return Reference to the derived Redis client for method chaining
     * @see https://redis.io/commands/setnx
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    setnx(Func &&func, const std::string &key, const std::string &val) {
        return derived().template command<bool>(std::forward<Func>(func), "SETNX", key,
                                                val);
    }

    /**
     * @brief Overwrite part of a string at a specific offset.
     *
     * Overwrites part of the string stored at key, starting at the specified offset.
     * If the offset is larger than the current length of the string, the string is
     * padded with zero-bytes to make offset fit. If key does not exist, it is created
     * with an empty string value.
     *
     * @param key The key to modify
     * @param offset The zero-based offset at which to start overwriting
     * @param val The string to write at the specified offset
     * @return The length of the string after it has been modified
     * @note Time complexity: O(1) for small strings, O(M) for larger updates where M is
     * the length of val
     * @see https://redis.io/commands/setrange
     */
    long long
    setrange(const std::string &key, long long offset, const std::string &val) {
        return derived()
            .template command<long long>("SETRANGE", key, offset, val)
            .result();
    }

    /**
     * @brief Asynchronous version of the SETRANGE command.
     *
     * @param func Callback function to be invoked when the operation completes
     * @param key The key to modify
     * @param offset The zero-based offset at which to start overwriting
     * @param val The string to write at the specified offset
     * @return Reference to the derived Redis client for method chaining
     * @see https://redis.io/commands/setrange
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    setrange(Func &&func, const std::string &key, long long offset,
             const std::string &val) {
        return derived().template command<long long>(std::forward<Func>(func),
                                                     "SETRANGE", key, offset, val);
    }

    /**
     * @brief Get the length of the string value stored at key.
     *
     * Returns the length of the string value stored at key. If the key does not exist,
     * 0 is returned. An error is returned if the value stored at key is not a string.
     *
     * @param key The key to get the string length for
     * @return The length of the string at key, or 0 if the key does not exist
     * @note Time complexity: O(1)
     * @see https://redis.io/commands/strlen
     */
    long long
    strlen(const std::string &key) {
        return derived().template command<long long>("STRLEN", key).result();
    }

    /**
     * @brief Asynchronous version of the STRLEN command.
     *
     * @param func Callback function to be invoked when the operation completes
     * @param key The key to get the string length for
     * @return Reference to the derived Redis client for method chaining
     * @see https://redis.io/commands/strlen
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    strlen(Func &&func, const std::string &key) {
        return derived().template command<long long>(std::forward<Func>(func), "STRLEN",
                                                     key);
    }

    /**
     * @brief Get the value of a key and delete it.
     *
     * Returns the value of the key and deletes it. This command is atomic.
     * If the key does not exist, returns null.
     *
     * @param key The key to get and delete
     * @return The value of the key if it exists, nullopt otherwise
     * @note Time complexity: O(1)
     * @note Available since Redis 6.2.0
     * @see https://redis.io/commands/getdel
     */
    std::optional<std::string>
    getdel(const std::string &key) {
        return derived()
            .template command<std::optional<std::string>>("GETDEL", key)
            .result();
    }

    /**
     * @brief Asynchronous version of the GETDEL command.
     *
     * @param func Callback function to be invoked when the operation completes
     * @param key The key to get and delete
     * @return Reference to the derived Redis client for method chaining
     * @note Available since Redis 6.2.0
     * @see https://redis.io/commands/getdel
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::string>> &&>,
                     Derived &>
    getdel(Func &&func, const std::string &key) {
        return derived().template command<std::optional<std::string>>(
            std::forward<Func>(func), "GETDEL", key);
    }

    /**
     * @brief Get the value of a key and set its expiration.
     *
     * Returns the value of the key and sets its expiration. This command is atomic.
     * If the key does not exist, returns null.
     *
     * @param key The key to get and set expiration for
     * @param ttl Time-to-live in seconds
     * @return The value of the key if it exists, nullopt otherwise
     * @note Time complexity: O(1)
     * @note Available since Redis 6.2.0
     * @see https://redis.io/commands/getex
     */
    std::optional<std::string>
    getex(const std::string &key, long long ttl) {
        return derived()
            .template command<std::optional<std::string>>("GETEX", key, "EX", ttl)
            .result();
    }

    /**
     * @brief Get the value of a key and set its expiration in milliseconds.
     *
     * Returns the value of the key and sets its expiration in milliseconds.
     * This command is atomic. If the key does not exist, returns null.
     *
     * @param key The key to get and set expiration for
     * @param ttl Time-to-live in milliseconds
     * @return The value of the key if it exists, nullopt otherwise
     * @note Time complexity: O(1)
     * @note Available since Redis 6.2.0
     * @see https://redis.io/commands/getex
     */
    std::optional<std::string>
    getex(const std::string &key, std::chrono::milliseconds const &ttl) {
        return derived()
            .template command<std::optional<std::string>>("GETEX", key, "PX",
                                                          ttl.count())
            .result();
    }

    /**
     * @brief Asynchronous version of the GETEX command.
     *
     * @param func Callback function to be invoked when the operation completes
     * @param key The key to get and set expiration for
     * @param ttl Time-to-live in seconds
     * @return Reference to the derived Redis client for method chaining
     * @note Available since Redis 6.2.0
     * @see https://redis.io/commands/getex
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::string>> &&>,
                     Derived &>
    getex(Func &&func, const std::string &key, long long ttl) {
        return derived().template command<std::optional<std::string>>(
            std::forward<Func>(func), "GETEX", key, "EX", ttl);
    }

    /**
     * @brief Asynchronous version of the GETEX command with millisecond precision.
     *
     * @param func Callback function to be invoked when the operation completes
     * @param key The key to get and set expiration for
     * @param ttl Time-to-live in milliseconds
     * @return Reference to the derived Redis client for method chaining
     * @note Available since Redis 6.2.0
     * @see https://redis.io/commands/getex
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::string>> &&>,
                     Derived &>
    getex(Func &&func, const std::string &key, std::chrono::milliseconds const &ttl) {
        return derived().template command<std::optional<std::string>>(
            std::forward<Func>(func), "GETEX", key, "PX", ttl.count());
    }

    /**
     * @brief Find the longest common subsequence between two strings.
     *
     * Returns the longest common subsequence between two strings.
     * This command is useful for finding similar strings or implementing diff-like
     * functionality.
     *
     * @param key1 First string key
     * @param key2 Second string key
     * @return The longest common subsequence or its length if len is true
     * @note Time complexity: O(N*M) where N and M are the lengths of the strings
     * @note Available since Redis 7.0.0
     * @see https://redis.io/commands/lcs
     */
    std::string
    lcs(const std::string &key1, const std::string &key2) {
        return derived().template command<std::string>("LCS", key1, key2).result();
    }

    /**
     * @brief Asynchronous version of the LCS command.
     *
     * @param func Callback function to be invoked when the operation completes
     * @param key1 First string key
     * @param key2 Second string key
     * @return Reference to the derived Redis client for method chaining
     * @note Available since Redis 7.0.0
     * @see https://redis.io/commands/lcs
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::string> &&>, Derived &>
    lcs(Func &&func, const std::string &key1, const std::string &key2) {
        return derived().template command<std::string>(std::forward<Func>(func), "LCS",
                                                       key1, key2);
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_STRING_COMMANDS_H
