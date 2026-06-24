/**
 * @file qbm/redis/commands/string_commands.h
 * @brief Redis string command mixin for the qb Redis module.
 *
 * Provides the @ref qb::redis::string_commands CRTP mixin, which exposes the
 * Redis string command family (GET/SET, APPEND, INCR/DECR family, GETRANGE,
 * MGET/MSET, GETDEL, GETEX, LCS, ...) in both coroutine-awaitable and
 * callback-based forms. Every command is available as a coroutine-awaitable
 * form for co_await and as a callback-based async form that returns a reference
 * to the derived handler for chaining.
 *
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2026 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)
 * @ingroup Redis
 */
#ifndef QBM_REDIS_STRING_COMMANDS_H
#define QBM_REDIS_STRING_COMMANDS_H
#include <bitset>
#include "../reply.h"

namespace qb::redis {

/**
 * @class string_commands
 * @brief Provides Redis string command implementations for manipulating string values.
 *
 * This class implements all Redis string-related commands with C++20 coroutine support.
 * All commands return awaitables that can be co_awaited for true async I/O without
 * blocking the event loop.
 *
 * Key features:
 * - Coroutine-first: All commands return redis_awaiter for co_await
 * - Callback fallback: Async callback versions still available for legacy code
 * - True async: co_await suspends the coroutine, allowing other I/O to proceed
 *
 * Usage:
 * @code
 * task<void> handler() {
 *     auto r = co_await redis.get("mykey");     // Suspends until response
 *     co_await redis.set("mykey", "value");      // Suspends until confirmation
 *     auto val = co_await redis.incr("counter"); // Atomic increment
 * }
 * @endcode
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
     * @brief Append a value to the end of a string stored at key (coroutine awaitable).
     *
     * If the key exists and is a string, this command appends the specified value
     * to the end of the string. If the key does not exist, it is created with an
     * empty string value before appending.
     *
     * @param key The key storing the string value
     * @param val The string to append to the existing value
     * @return redis_awaiter yielding Reply<long long>
     * @note If the key exists but is not a string, Redis will return an error
     * @note Time complexity: O(1)
     * @see https://redis.io/commands/append
     */
    auto
    append(const std::string &key, const std::string &val) {
        return derived().template make_coro_command<long long>(
            [this, key, val](auto &&callback) { this->append(std::move(callback), key, val); });
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
        return derived().template command<long long>(std::forward<Func>(func), "APPEND", key, val);
    }

    /**
     * @brief Decrement the integer value stored at key by one (coroutine awaitable).
     *
     * @param key The key storing the numeric string value
     * @return redis_awaiter yielding Reply<long long>
     * @see https://redis.io/commands/decr
     */
    auto
    decr(const std::string &key) {
        return derived().template make_coro_command<long long>([this, key](auto &&callback) { this->decr(std::move(callback), key); });
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
        return derived().template command<long long>(std::forward<Func>(func), "DECR", key);
    }

    /**
     * @brief Decrement the integer value stored at key by the specified amount (coroutine awaitable).
     *
     * @param key The key storing the numeric string value
     * @param decrement The amount to decrement the value by
     * @return redis_awaiter yielding Reply<long long>
     * @see https://redis.io/commands/decrby
     */
    auto
    decrby(const std::string &key, long long decrement) {
        return derived().template make_coro_command<long long>(
            [this, key, decrement](auto &&callback) { this->decrby(std::move(callback), key, decrement); });
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
        return derived().template command<long long>(std::forward<Func>(func), "DECRBY", key, decrement);
    }

    /**
     * @brief Get the string value stored at key (coroutine awaitable).
     *
     * @param key The key to retrieve the value for
     * @return redis_awaiter yielding Reply<std::optional<std::string>>
     * @see https://redis.io/commands/get
     */
    auto
    get(const std::string &key) {
        return derived().template make_coro_command<std::optional<std::string>>(
            [this, key](auto &&callback) { this->get(std::move(callback), key); });
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::string>> &&>, Derived &>
    get(Func &&func, const std::string &key) {
        return derived().template command<std::optional<std::string>>(std::forward<Func>(func), "GET", key);
    }

    /**
     * @brief Get a substring of the string stored at key (coroutine awaitable).
     *
     * @param key The key storing the string value
     * @param start Start offset (inclusive), 0-based
     * @param end End offset (inclusive)
     * @return redis_awaiter yielding Reply<std::string>
     * @see https://redis.io/commands/getrange
     */
    auto
    getrange(const std::string &key, long long start, long long end) {
        return derived().template make_coro_command<std::string>(
            [this, key, start, end](auto &&callback) { this->getrange(std::move(callback), key, start, end); });
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
        return derived().template command<std::string>(std::forward<Func>(func), "GETRANGE", key, start, end);
    }

    /**
     * @brief Get a substring of the string stored at key (deprecated alias for GETRANGE).
     * @param key The key storing the string value
     * @param start Start offset (inclusive), 0-based
     * @param end End offset (inclusive)
     * @return redis_awaiter yielding Reply<std::string>
     * @see https://redis.io/commands/substr
     */
    auto
    substr(const std::string &key, long long start, long long end) {
        return derived().template make_coro_command<std::string>(
            [this, key, start, end](auto &&callback) { this->substr(std::move(callback), key, start, end); });
    }

    /**
     * @brief Asynchronous version of the SUBSTR command (deprecated alias for GETRANGE).
     *
     * @param func Callback function to be invoked when the operation completes
     * @param key The key storing the string value
     * @param start Start offset (inclusive), 0-based
     * @param end End offset (inclusive)
     * @return Reference to the derived Redis client for method chaining
     * @see https://redis.io/commands/substr
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::string> &&>, Derived &>
    substr(Func &&func, const std::string &key, long long start, long long end) {
        return derived().template command<std::string>(std::forward<Func>(func), "SUBSTR", key, start, end);
    }

    /**
     * @brief Atomically set a string value and return the old value (coroutine awaitable).
     *
     * @param key The key to set
     * @param val The new string value to set
     * @return redis_awaiter yielding Reply<std::optional<std::string>>
     * @see https://redis.io/commands/getset
     */
    auto
    getset(const std::string &key, const std::string &val) {
        return derived().template make_coro_command<std::optional<std::string>>(
            [this, key, val](auto &&callback) { this->getset(std::move(callback), key, val); });
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::string>> &&>, Derived &>
    getset(Func &&func, const std::string &key, const std::string &val) {
        return derived().template command<std::optional<std::string>>(std::forward<Func>(func), "GETSET", key, val);
    }

    /**
     * @brief Increment the integer value stored at key by one (coroutine awaitable).
     *
     * @param key The key storing the numeric string value
     * @return redis_awaiter yielding Reply<long long>
     * @see https://redis.io/commands/incr
     */
    auto
    incr(const std::string &key) {
        return derived().template make_coro_command<long long>([this, key](auto &&callback) { this->incr(std::move(callback), key); });
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
        return derived().template command<long long>(std::forward<Func>(func), "INCR", key);
    }

    /**
     * @brief Increment the integer value stored at key by the specified amount (coroutine awaitable).
     *
     * @param key The key storing the numeric string value
     * @param increment The amount to increment the value by
     * @return redis_awaiter yielding Reply<long long>
     * @see https://redis.io/commands/incrby
     */
    auto
    incrby(const std::string &key, long long increment) {
        return derived().template make_coro_command<long long>(
            [this, key, increment](auto &&callback) { this->incrby(std::move(callback), key, increment); });
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
        return derived().template command<long long>(std::forward<Func>(func), "INCRBY", key, increment);
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
    auto
    incrbyfloat(const std::string &key, double increment) {
        return derived().template make_coro_command<double>(
            [this, key, increment](auto &&callback) { this->incrbyfloat(std::move(callback), key, increment); });
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
        return derived().template command<double>(std::forward<Func>(func), "INCRBYFLOAT", key, increment);
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
    auto
    mget(const std::vector<std::string> &keys) {
        return derived().template make_coro_command<std::vector<std::optional<std::string>>>(
            [this, keys](auto &&callback) { this->mget(std::move(callback), keys); });
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::optional<std::string>>> &&>, Derived &>
    mget(Func &&func, const std::vector<std::string> &keys) {
        return derived().template command<std::vector<std::optional<std::string>>>(std::forward<Func>(func), "MGET", keys);
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
    auto
    mset(const std::vector<std::pair<std::string, std::string>> &keys) {
        return derived().template make_coro_command<status>([this, keys](auto &&callback) { this->mset(std::move(callback), keys); });
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
        return derived().template command<status>(std::forward<Func>(func), "MSET", keys);
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
    auto
    msetnx(const std::vector<std::pair<std::string, std::string>> &keys) {
        return derived().template make_coro_command<bool>([this, keys](auto &&callback) { this->msetnx(std::move(callback), keys); });
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
        return derived().template command<bool>(std::forward<Func>(func), "MSETNX", keys);
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
    auto
    psetex(const std::string &key, long long ttl, const std::string &val) {
        return derived().template make_coro_command<status>(
            [this, key, ttl, val](auto &&callback) { this->psetex(std::move(callback), key, ttl, val); });
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
        return derived().template command<status>(std::forward<Func>(func), "PSETEX", key, ttl, val);
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
    auto
    psetex(const std::string &key, std::chrono::milliseconds const &ttl, const std::string &val) {
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
    psetex(Func &&func, const std::string &key, std::chrono::milliseconds const &ttl, const std::string &val) {
        return psetex(std::forward<Func>(func), key, ttl.count(), val);
    }

    /**
     * @brief Set a key-value pair with optional conditions (coroutine awaitable).
     *
     * @param key The key to set
     * @param val The string value to set
     * @param type Update condition (EXIST, NOT_EXIST, or ALWAYS)
     * @return redis_awaiter yielding Reply<status>
     * @see https://redis.io/commands/set
     */
    auto
    set(const std::string &key, const std::string &val, UpdateType type = UpdateType::ALWAYS) {
        return derived().template make_coro_command<status>(
            [this, key, val, type](auto &&callback) { this->set(std::move(callback), key, val, type); });
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
    set(Func &&func, const std::string &key, const std::string &val, UpdateType type = UpdateType::ALWAYS) {
        std::optional<std::string> opt;
        if (type != UpdateType::ALWAYS)
            opt = to_string(type);

        return derived().template command<status>(std::forward<Func>(func), "SET", key, val, opt);
    }

    /**
     * @brief Set a key-value pair with millisecond precision timeout and conditions (coroutine awaitable).
     *
     * @param key The key to set
     * @param val The string value to set
     * @param ttl Time-to-live in milliseconds
     * @param type Update condition (EXIST, NOT_EXIST, or ALWAYS)
     * @return redis_awaiter yielding Reply<status>
     * @see https://redis.io/commands/set
     */
    auto
    set(const std::string &key, const std::string &val, long long ttl, UpdateType type = UpdateType::ALWAYS) {
        return derived().template make_coro_command<status>(
            [this, key, val, ttl, type](auto &&callback) { this->set(std::move(callback), key, val, ttl, type); });
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
    set(Func &&func, const std::string &key, const std::string &val, long long ttl, UpdateType type = UpdateType::ALWAYS) {
        std::optional<std::string> opt;
        if (type != UpdateType::ALWAYS)
            opt = to_string(type);
        return derived().template command<status>(std::forward<Func>(func), "SET", key, val, "PX", ttl, opt);
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
    auto
    set(const std::string &key, const std::string &val, const std::chrono::milliseconds &ttl, UpdateType type = UpdateType::ALWAYS) {
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
    set(Func &&func, const std::string &key, const std::string &val, const std::chrono::milliseconds &ttl,
        UpdateType type = UpdateType::ALWAYS) {
        return set(std::forward<Func>(func), key, val, static_cast<long long>(ttl.count()), type);
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
    auto
    setex(const std::string &key, long long ttl, const std::string &val) {
        return derived().template make_coro_command<status>(
            [this, key, ttl, val](auto &&callback) { this->setex(std::move(callback), key, ttl, val); });
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
        return derived().template command<status>(std::forward<Func>(func), "SETEX", key, ttl, val);
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
    auto
    setex(const std::string &key, std::chrono::seconds const &ttl, const std::string &val) {
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
    setex(Func &&func, const std::string &key, std::chrono::seconds const &ttl, const std::string &val) {
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
    auto
    setnx(const std::string &key, const std::string &val) {
        return derived().template make_coro_command<bool>([this, key, val](auto &&callback) { this->setnx(std::move(callback), key, val); });
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
        return derived().template command<bool>(std::forward<Func>(func), "SETNX", key, val);
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
    auto
    setrange(const std::string &key, long long offset, const std::string &val) {
        return derived().template make_coro_command<long long>(
            [this, key, offset, val](auto &&callback) { this->setrange(std::move(callback), key, offset, val); });
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
    setrange(Func &&func, const std::string &key, long long offset, const std::string &val) {
        return derived().template command<long long>(std::forward<Func>(func), "SETRANGE", key, offset, val);
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
    auto
    strlen(const std::string &key) {
        return derived().template make_coro_command<long long>([this, key](auto &&callback) { this->strlen(std::move(callback), key); });
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
        return derived().template command<long long>(std::forward<Func>(func), "STRLEN", key);
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
    auto
    getdel(const std::string &key) {
        return derived().template make_coro_command<std::optional<std::string>>(
            [this, key](auto &&callback) { this->getdel(std::move(callback), key); });
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::string>> &&>, Derived &>
    getdel(Func &&func, const std::string &key) {
        return derived().template command<std::optional<std::string>>(std::forward<Func>(func), "GETDEL", key);
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
    auto
    getex(const std::string &key, long long ttl) {
        return derived().template make_coro_command<std::optional<std::string>>(
            [this, key, ttl](auto &&callback) { this->getex(std::move(callback), key, ttl); });
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
    auto
    getex(const std::string &key, std::chrono::milliseconds const &ttl) {
        return derived().template make_coro_command<std::optional<std::string>>(
            [this, key, ttl](auto &&callback) { this->getex(std::move(callback), key, ttl); });
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::string>> &&>, Derived &>
    getex(Func &&func, const std::string &key, long long ttl) {
        return derived().template command<std::optional<std::string>>(std::forward<Func>(func), "GETEX", key, "EX", ttl);
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::string>> &&>, Derived &>
    getex(Func &&func, const std::string &key, std::chrono::milliseconds const &ttl) {
        return derived().template command<std::optional<std::string>>(std::forward<Func>(func), "GETEX", key, "PX", ttl.count());
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
    auto
    lcs(const std::string &key1, const std::string &key2) {
        return derived().template make_coro_command<std::string>(
            [this, key1, key2](auto &&callback) { this->lcs(std::move(callback), key1, key2); });
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
        return derived().template command<std::string>(std::forward<Func>(func), "LCS", key1, key2);
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_STRING_COMMANDS_H
