/**
 * @file qbm/redis/commands/connection_commands.h
 * @brief Redis connection-management command mixin (HELLO, AUTH, PING, SELECT, ...)
 *
 * Defines the @ref qb::redis::connection_commands CRTP mixin, which exposes the
 * Redis connection command family. Each command is offered in two forms: a
 * coroutine-awaitable overload returning a @c redis_awaiter, and a callback
 * overload returning a reference to the derived handler for chaining.
 *
 *            SPDX-License-Identifier: Apache-2.0
 *
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2026 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)
 * @ingroup Redis
 */
#ifndef QBM_REDIS_CONNECTION_COMMANDS_H
#define QBM_REDIS_CONNECTION_COMMANDS_H
#include "../reply.h"

namespace qb::redis {

/**
 * @class connection_commands
 * @brief Provides Redis connection command implementations.
 *
 * This class implements Redis commands for managing connections to the Redis server,
 * including authentication, database selection, and connection status commands.
 *
 * @tparam Derived The derived class type (CRTP pattern)
 */
template <typename Derived>
class connection_commands {
private:
    constexpr Derived &
    derived() {
        return static_cast<Derived &>(*this);
    }

public:
    /**
     * @brief Sends HELLO to switch protocol version (RESP2/RESP3).
     *
     * In RESP3 mode Redis returns a map with server info (version, capabilities, etc.).
     * Use this as the first command after connect to enable RESP3.
     *
     * @param version Protocol version: 2 for RESP2, 3 for RESP3
     * @return redis_awaiter yielding Reply<qb::json> (server info map in RESP3)
     * @see https://redis.io/commands/hello
     */
    auto
    hello(int version = 3) {
        return derived().template make_coro_command<qb::json>([this, version](auto &&callback) { this->hello(std::move(callback), version); });
    }

    /**
     * @brief Asynchronous version of hello
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @param version Protocol version (2 or 3)
     * @return Reference to the derived class
     * @see https://redis.io/commands/hello
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::json> &&>, Derived &>
    hello(Func &&func, int version = 3) {
        return derived().template command<qb::json>(std::forward<Func>(func), "HELLO", std::to_string(version));
    }

    /**
     * @brief Authenticates the client to the Redis server (coroutine awaitable)
     *
     * @param password Authentication password
     * @return redis_awaiter yielding Reply<status>
     * @see https://redis.io/commands/auth
     */
    auto
    auth(const std::string &password) {
        return derived().template make_coro_command<status>([this, password](auto &&callback) { this->auth(std::move(callback), password); });
    }

    /**
     * @brief Asynchronous version of auth
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param password Authentication password
     * @return Reference to the Redis handler for chaining
     * @see https://redis.io/commands/auth
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    auth(Func &&func, const std::string &password) {
        return derived().template command<status>(std::forward<Func>(func), "AUTH", password);
    }

    /**
     * @brief Authenticates the client with username and password (coroutine awaitable)
     *
     * @param user Username for authentication
     * @param password Password for authentication
     * @return redis_awaiter yielding Reply<status>
     * @see https://redis.io/commands/auth
     */
    auto
    auth(const std::string &user, const std::string &password) {
        return derived().template make_coro_command<status>(
            [this, user, password](auto &&callback) { this->auth(std::move(callback), user, password); });
    }

    /**
     * @brief Asynchronous version of auth with username and password
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param user Username for authentication
     * @param password Password for authentication
     * @return Reference to the Redis handler for chaining
     * @see https://redis.io/commands/auth
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    auth(Func &&func, const std::string &user, const std::string &password) {
        return derived().template command<status>(std::forward<Func>(func), "AUTH", user, password);
    }

    /**
     * @brief Echoes the given message back (coroutine awaitable)
     *
     * @param message Message to echo
     * @return redis_awaiter yielding Reply<std::string>
     * @see https://redis.io/commands/echo
     */
    auto
    echo(const std::string &message) {
        return derived().template make_coro_command<std::string>(
            [this, message](auto &&callback) { this->echo(std::move(callback), message); });
    }

    /**
     * @brief Asynchronous version of echo
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param message Message to echo
     * @return Reference to the Redis handler for chaining
     * @see https://redis.io/commands/echo
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::string> &&>, Derived &>
    echo(Func &&func, const std::string &message) {
        return derived().template command<std::string>(std::forward<Func>(func), "ECHO", message);
    }

    /**
     * @brief Tests if the connection is still alive (coroutine awaitable)
     *
     * @return redis_awaiter yielding Reply<std::string>
     * @see https://redis.io/commands/ping
     */
    auto
    ping() {
        return derived().template make_coro_command<std::string>([this](auto &&callback) { this->ping(std::move(callback)); });
    }

    /**
     * @brief Asynchronous version of ping
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @return Reference to the Redis handler for chaining
     * @see https://redis.io/commands/ping
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::string> &&>, Derived &>
    ping(Func &&func) {
        return derived().template command<std::string>(std::forward<Func>(func), "PING");
    }

    /**
     * @brief Sends a custom message with PING (coroutine awaitable)
     *
     * @param message Custom message to send
     * @return redis_awaiter yielding Reply<std::string>
     * @see https://redis.io/commands/ping
     */
    auto
    ping(const std::string &message) {
        return derived().template make_coro_command<std::string>(
            [this, message](auto &&callback) { this->ping(std::move(callback), message); });
    }

    /**
     * @brief Asynchronous version of ping with custom message
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param message Custom message to send
     * @return Reference to the Redis handler for chaining
     * @see https://redis.io/commands/ping
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::string> &&>, Derived &>
    ping(Func &&func, const std::string &message) {
        return derived().template command<std::string>(std::forward<Func>(func), "PING", message);
    }

    /**
     * @brief Closes the connection (coroutine awaitable)
     *
     * @return redis_awaiter yielding Reply<status>
     * @see https://redis.io/commands/quit
     */
    auto
    quit() {
        return derived().template make_coro_command<status>([this](auto &&callback) { this->quit(std::move(callback)); });
    }

    /**
     * @brief Asynchronous version of quit
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @return Reference to the Redis handler for chaining
     * @see https://redis.io/commands/quit
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    quit(Func &&func) {
        return derived().template command<status>(std::forward<Func>(func), "QUIT");
    }

    /**
     * @brief Selects the Redis logical database (coroutine awaitable)
     *
     * @param index Database index
     * @return redis_awaiter yielding Reply<status>
     * @see https://redis.io/commands/select
     */
    auto
    select(long long index) {
        return derived().template make_coro_command<status>([this, index](auto &&callback) { this->select(std::move(callback), index); });
    }

    /**
     * @brief Asynchronous version of select
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param index Database index
     * @return Reference to the Redis handler for chaining
     * @see https://redis.io/commands/select
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    select(Func &&func, long long index) {
        return derived().template command<status>(std::forward<Func>(func), "SELECT", index);
    }

    /**
     * @brief Swaps two Redis databases (coroutine awaitable)
     *
     * @param index1 Index of the first database
     * @param index2 Index of the second database
     * @return redis_awaiter yielding Reply<status>
     * @see https://redis.io/commands/swapdb
     */
    auto
    swapdb(long long index1, long long index2) {
        return derived().template make_coro_command<status>(
            [this, index1, index2](auto &&callback) { this->swapdb(std::move(callback), index1, index2); });
    }

    /**
     * @brief Asynchronous version of swapdb
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param index1 Index of the first database
     * @param index2 Index of the second database
     * @return Reference to the Redis handler for chaining
     * @see https://redis.io/commands/swapdb
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    swapdb(Func &&func, long long index1, long long index2) {
        return derived().template command<status>(std::forward<Func>(func), "SWAPDB", index1, index2);
    }

    /**
     * @brief Reset the connection (coroutine awaitable)
     *
     * Resets the connection to a clean state, discarding any pending data.
     *
     * @return redis_awaiter yielding Reply<status>
     * @see https://redis.io/commands/reset
     */
    auto
    reset() {
        return derived().template make_coro_command<status>([this](auto &&callback) { this->reset(std::move(callback)); });
    }

    /**
     * @brief Asynchronous version of reset
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     * @see https://redis.io/commands/reset
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    reset(Func &&func) {
        return derived().template command<status>(std::forward<Func>(func), "RESET");
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_CONNECTION_COMMANDS_H
