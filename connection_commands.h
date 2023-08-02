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

template <typename Derived>
class connection_commands {
public:
    /// @brief Send password to Redis.
    /// @param password Password.
    /// @note Normally, you should not call this method.
    ///       Instead, you should set password with `ConnectionOptions` or URI.
    /// @see https://redis.io/commands/auth
    bool
    auth(const std::string &password) {
        return static_cast<Derived &>(*this).template command<void>("AUTH", password).ok;
    }
    template <typename Func>
    Derived &
    auth(Func &&func, const std::string &password) {
        return static_cast<Derived &>(*this).template command<void>(
            std::forward<Func>(func),
            "AUTH",
            password);
    }

    /// @brief Send user and password to Redis.
    /// @param user User name.
    /// @param password Password.
    /// @note Normally, you should not call this method.
    ///       Instead, you should set password with `ConnectionOptions` or URI.
    ///       Also this overload only works with Redis 6.0 or later.
    /// @see https://redis.io/commands/auth
    bool
    auth(const std::string &user, const std::string &password) {
        return static_cast<Derived &>(*this).template command<void>("AUTH", user, password).ok;
    }
    template <typename Func>
    Derived &
    auth(Func &&func, const std::string &user, const std::string &password) {
        return static_cast<Derived &>(*this)
            .template command<void>(std::forward<Func>(func), "AUTH", user, password);
    }

    /// @brief Ask Redis to return the given message.
    /// @param msg Message to be sent.
    /// @return Return the given message.
    /// @see https://redis.io/commands/echo
    std::string
    echo(const std::string &msg) {
        return static_cast<Derived &>(*this).template command<std::string>("ECHO", msg).result;
    }
    template <typename Func>
    Derived &
    echo(Func &&func, const std::string &msg) {
        return static_cast<Derived &>(*this).template command<std::string>(
            std::forward<Func>(func),
            "ECHO",
            msg);
    }

    /// @brief Test if the connection is alive.
    /// @return Always return *PONG*.
    /// @see https://redis.io/commands/ping
    std::string
    ping() {
        return static_cast<Derived &>(*this).template command<std::string>("PING").result;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::string> &&>, Derived &>
    ping(Func &&func) {
        return static_cast<Derived &>(*this).template command<std::string>(std::forward<Func>(func), "PING");
    }

    /// @brief Test if the connection is alive.
    /// @param msg Message sent to Redis.
    /// @return Return the given message.
    /// @see https://redis.io/commands/ping
    std::string
    ping(const std::string &msg) {
        return static_cast<Derived &>(*this).template command<std::string>("PING", msg).result;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::string> &&>, Derived &>
    ping(Func &&func, const std::string &msg) {
        return static_cast<Derived &>(*this).template command<std::string>(
            std::forward<Func>(func),
            "PING",
            msg);
    }

    /// @brief Select a database.
    /// @param index Database index.
    /// @return Return msg.ok.
    /// @see https://redis.io/commands/select
    bool
    select(long long index) {
        return static_cast<Derived &>(*this).template command<void>("SELECT", index).ok;
    }
    template <typename Func>
    Derived &
    select(Func &&func, long long index) {
        return static_cast<Derived &>(*this).template command<void>(
            std::forward<Func>(func),
            "SELECT",
            index);
    }

    /// @brief Swap two Redis databases.
    /// @param index1 The index of the first database.
    /// @param index2 The index of the second database.
    /// @see https://redis.io/commands/swapdb
    bool
    swapdb(long long index1, long long index2) {
        return static_cast<Derived &>(*this).template command<void>("SWAPDB", index1, index2).ok;
    }
    template <typename Func>
    Derived &
    swapdb(Func &&func, long long index1, long long index2) {
        return static_cast<Derived &>(*this)
            .template command<void>(std::forward<Func>(func), "SWAPDB", index1, index2);
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_CONNECTION_COMMANDS_H
