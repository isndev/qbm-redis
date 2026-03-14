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

#ifndef QBM_REDIS_CONNECTION_COMMANDS_H
#define QBM_REDIS_CONNECTION_COMMANDS_H
#include "reply.h"

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
    auto hello(int version = 3) {
        return derived().template make_coro_command<qb::json>(
            [this, version](auto&& callback) {
                this->hello(std::move(callback), version);
            }
        );
    }

    /**
     * @brief Asynchronous version of hello
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::json> &&>, Derived &>
    hello(Func &&func, int version = 3) {
        return derived().template command<qb::json>(std::forward<Func>(func), "HELLO",
                                                    std::to_string(version));
    }

    /**
     * @brief Authenticates the client to the Redis server (coroutine awaitable)
     * @param password Authentication password
     * @return redis_awaiter yielding Reply<status>
     */
    auto auth(const std::string &password) {
        return derived().template make_coro_command<status>(
            [this, password](auto&& callback) {
                this->auth(std::move(callback), password);
            }
        );
    }

    /**
     * @brief Asynchronous version of auth
     * @tparam Func Callback function type
     * @param func Callback function
     * @param password Authentication password
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    auth(Func &&func, const std::string &password) {
        return derived().template command<status>(std::forward<Func>(func), "AUTH",
                                                  password);
    }

    /**
     * @brief Authenticates the client with username and password (coroutine awaitable)
     * @param user Username for authentication
     * @param password Password for authentication
     * @return redis_awaiter yielding Reply<status>
     */
    auto auth(const std::string &user, const std::string &password) {
        return derived().template make_coro_command<status>(
            [this, user, password](auto&& callback) {
                this->auth(std::move(callback), user, password);
            }
        );
    }

    /**
     * @brief Asynchronous version of auth with username and password
     * @tparam Func Callback function type
     * @param func Callback function
     * @param user Username for authentication
     * @param password Password for authentication
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    auth(Func &&func, const std::string &user, const std::string &password) {
        return derived().template command<status>(std::forward<Func>(func), "AUTH", user,
                                                  password);
    }

    /**
     * @brief Echoes the given message back (coroutine awaitable)
     * @param message Message to echo
     * @return redis_awaiter yielding Reply<std::string>
     */
    auto echo(const std::string &message) {
        return derived().template make_coro_command<std::string>(
            [this, message](auto&& callback) {
                this->echo(std::move(callback), message);
            }
        );
    }

    /**
     * @brief Asynchronous version of echo
     * @tparam Func Callback function type
     * @param func Callback function
     * @param message Message to echo
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::string> &&>, Derived &>
    echo(Func &&func, const std::string &message) {
        return derived().template command<std::string>(std::forward<Func>(func), "ECHO",
                                                       message);
    }

    /**
     * @brief Tests if the connection is still alive (coroutine awaitable)
     * @return redis_awaiter yielding Reply<std::string>
     */
    auto ping() {
        return derived().template make_coro_command<std::string>(
            [this](auto&& callback) {
                this->ping(std::move(callback));
            }
        );
    }

    /**
     * @brief Asynchronous version of ping
     * @tparam Func Callback function type
     * @param func Callback function
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::string> &&>, Derived &>
    ping(Func &&func) {
        return derived().template command<std::string>(std::forward<Func>(func), "PING");
    }

    /**
     * @brief Sends a custom message with PING (coroutine awaitable)
     * @param message Custom message to send
     * @return redis_awaiter yielding Reply<std::string>
     */
    auto ping(const std::string &message) {
        return derived().template make_coro_command<std::string>(
            [this, message](auto&& callback) {
                this->ping(std::move(callback), message);
            }
        );
    }

    /**
     * @brief Asynchronous version of ping with custom message
     * @tparam Func Callback function type
     * @param func Callback function
     * @param message Custom message to send
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::string> &&>, Derived &>
    ping(Func &&func, const std::string &message) {
        return derived().template command<std::string>(std::forward<Func>(func), "PING",
                                                       message);
    }

    /**
     * @brief Closes the connection (coroutine awaitable)
     * @return redis_awaiter yielding Reply<status>
     */
    auto quit() {
        return derived().template make_coro_command<status>(
            [this](auto&& callback) {
                this->quit(std::move(callback));
            }
        );
    }

    /**
     * @brief Asynchronous version of quit
     * @tparam Func Callback function type
     * @param func Callback function
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    quit(Func &&func) {
        return derived().template command<status>(std::forward<Func>(func), "QUIT");
    }

    /**
     * @brief Selects the Redis logical database (coroutine awaitable)
     * @param index Database index
     * @return redis_awaiter yielding Reply<status>
     */
    auto select(long long index) {
        return derived().template make_coro_command<status>(
            [this, index](auto&& callback) {
                this->select(std::move(callback), index);
            }
        );
    }

    /**
     * @brief Asynchronous version of select
     * @tparam Func Callback function type
     * @param func Callback function
     * @param index Database index
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    select(Func &&func, long long index) {
        return derived().template command<status>(std::forward<Func>(func), "SELECT",
                                                  index);
    }

    /**
     * @brief Swaps two Redis databases (coroutine awaitable)
     * @param index1 Index of the first database
     * @param index2 Index of the second database
     * @return redis_awaiter yielding Reply<status>
     */
    auto swapdb(long long index1, long long index2) {
        return derived().template make_coro_command<status>(
            [this, index1, index2](auto&& callback) {
                this->swapdb(std::move(callback), index1, index2);
            }
        );
    }

    /**
     * @brief Asynchronous version of swapdb
     * @tparam Func Callback function type
     * @param func Callback function
     * @param index1 Index of the first database
     * @param index2 Index of the second database
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    swapdb(Func &&func, long long index1, long long index2) {
        return derived().template command<status>(std::forward<Func>(func), "SWAPDB",
                                                  index1, index2);
    }

    /**
     * @brief Reset the connection (coroutine awaitable).
     * Resets the connection to a clean state, discarding any pending data.
     * @see https://redis.io/commands/reset
     */
    auto reset() {
        return derived().template make_coro_command<status>(
            [this](auto&& callback) {
                this->reset(std::move(callback));
            }
        );
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    reset(Func &&func) {
        return derived().template command<status>(std::forward<Func>(func), "RESET");
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_CONNECTION_COMMANDS_H
