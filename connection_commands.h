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
public:
    /**
     * @brief Authenticates the client to the Redis server
     *
     * @param password Authentication password
     * @return true if authentication was successful, false otherwise
     */
    bool
    auth(const std::string &password) {
        return static_cast<Derived &>(*this).template command<void>("AUTH", password).ok;
    }
    
    /**
     * @brief Asynchronous version of auth
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param password Authentication password
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    auth(Func &&func, const std::string &password) {
        return static_cast<Derived &>(*this).template command<void>(std::forward<Func>(func), "AUTH", password);
    }

    /**
     * @brief Authenticates the client to the Redis server with username and password
     *
     * @param user Username for authentication
     * @param password Password for authentication
     * @return true if authentication was successful, false otherwise
     */
    bool
    auth(const std::string &user, const std::string &password) {
        return static_cast<Derived &>(*this).template command<void>("AUTH", user, password).ok;
    }
    
    /**
     * @brief Asynchronous version of auth with username and password
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param user Username for authentication
     * @param password Password for authentication
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    auth(Func &&func, const std::string &user, const std::string &password) {
        return static_cast<Derived &>(*this).template command<void>(std::forward<Func>(func), "AUTH", user, password);
    }

    /**
     * @brief Echoes the given message back
     *
     * @param message Message to echo
     * @return The same message
     */
    std::string
    echo(const std::string &message) {
        return static_cast<Derived &>(*this).template command<std::string>("ECHO", message).result;
    }
    
    /**
     * @brief Asynchronous version of echo
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param message Message to echo
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::string> &&>, Derived &>
    echo(Func &&func, const std::string &message) {
        return static_cast<Derived &>(*this).template command<std::string>(std::forward<Func>(func), "ECHO", message);
    }

    /**
     * @brief Tests if the connection is still alive
     *
     * @return "PONG" if the connection is alive
     */
    std::string
    ping() {
        return static_cast<Derived &>(*this).template command<std::string>("PING").result;
    }
    
    /**
     * @brief Asynchronous version of ping
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::string> &&>, Derived &>
    ping(Func &&func) {
        return static_cast<Derived &>(*this).template command<std::string>(std::forward<Func>(func), "PING");
    }

    /**
     * @brief Sends a custom message with PING
     *
     * @param message Custom message to send
     * @return The message that was sent
     */
    std::string
    ping(const std::string &message) {
        return static_cast<Derived &>(*this).template command<std::string>("PING", message).result;
    }
    
    /**
     * @brief Asynchronous version of ping with custom message
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param message Custom message to send
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::string> &&>, Derived &>
    ping(Func &&func, const std::string &message) {
        return static_cast<Derived &>(*this).template command<std::string>(std::forward<Func>(func), "PING", message);
    }

    /**
     * @brief Closes the connection
     *
     * @return true if the command was acknowledged, false otherwise
     */
    bool
    quit() {
        return static_cast<Derived &>(*this).template command<void>("QUIT").ok;
    }
    
    /**
     * @brief Asynchronous version of quit
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<void> &&>, Derived &>
    quit(Func &&func) {
        return static_cast<Derived &>(*this).template command<void>(std::forward<Func>(func), "QUIT");
    }

    /**
     * @brief Selects the Redis logical database
     *
     * @param index Database index
     * @return true if the database was selected, false otherwise
     */
    bool
    select(long long index) {
        return static_cast<Derived &>(*this).template command<void>("SELECT", index).ok;
    }
    
    /**
     * @brief Asynchronous version of select
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param index Database index
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    select(Func &&func, long long index) {
        return static_cast<Derived &>(*this).template command<void>(std::forward<Func>(func), "SELECT", index);
    }

    /**
     * @brief Swaps two Redis databases
     *
     * @param index1 Index of the first database
     * @param index2 Index of the second database
     * @return true if the databases were swapped successfully, false otherwise
     */
    bool
    swapdb(long long index1, long long index2) {
        return static_cast<Derived &>(*this).template command<void>("SWAPDB", index1, index2).ok;
    }
    
    /**
     * @brief Asynchronous version of swapdb
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param index1 Index of the first database
     * @param index2 Index of the second database
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    swapdb(Func &&func, long long index1, long long index2) {
        return static_cast<Derived &>(*this).template command<void>(std::forward<Func>(func), "SWAPDB", index1, index2);
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_CONNECTION_COMMANDS_H
