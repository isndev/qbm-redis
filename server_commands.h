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

#ifndef QBM_REDIS_SERVER_COMMANDS_H
#define QBM_REDIS_SERVER_COMMANDS_H
#include "reply.h"

namespace qb::redis {

template <typename Derived>
class server_commands {
public:
    /// @brief Rewrite AOF in the background.
    /// @see https://redis.io/commands/bgrewriteaof
    bool
    bgrewriteaof() {
        return static_cast<Derived &>(*this).template command<void>("BGREWRITEAOF").ok;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    bgrewriteaof(Func &&func) {
        return static_cast<Derived &>(*this).template command<void>(std::forward<Func>(func), "BGREWRITEAOF");
    }

    /// @brief Save database in the background.
    /// @return *Background saving started* if BGSAVE started correctly.
    /// @see https://redis.io/commands/bgsave
    std::string
    bgsave() {
        return static_cast<Derived &>(*this).template command<std::string>("BGSAVE").result;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::string> &&>, Derived &>
    bgsave(Func &&func) {
        return static_cast<Derived &>(*this).template command<std::string>(std::forward<Func>(func), "BGSAVE");
    }

    /// @brief Get the size of the currently selected database.
    /// @return Number of keys in currently selected database.
    /// @see https://redis.io/commands/dbsize
    long long
    dbsize() {
        return static_cast<Derived &>(*this).template command<long long>("DBSIZE").result;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    dbsize(Func &&func) {
        return static_cast<Derived &>(*this).template command<long long>(std::forward<Func>(func), "DBSIZE");
    }

    /// @brief Remove keys of all databases.
    /// @param async Whether flushing databases asynchronously, i.e. without blocking the server.
    /// @see https://redis.io/commands/flushall
    bool
    flushall(bool async = false) {
        std::optional<std::string> opt;
        if (async)
            opt = "ASYNC";
        return static_cast<Derived &>(*this).template command<void>("FLUSHALL", opt).ok;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    flushall(Func &&func, bool async = false) {
        std::optional<std::string> opt;
        if (async)
            opt = "ASYNC";
        return static_cast<Derived &>(*this).template command<void>(std::forward<Func>(func), "FLUSHALL", opt);
    }

    /// @brief Remove keys of current databases.
    /// @param async Whether flushing databases asynchronously, i.e. without blocking the server.
    /// @see https://redis.io/commands/flushdb
    bool
    flushdb(bool async = false) {
        std::optional<std::string> opt;
        if (async)
            opt = "ASYNC";
        return static_cast<Derived &>(*this).template command<void>("FLUSHDB", opt).ok;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    flushdb(Func &&func, bool async = false) {
        std::optional<std::string> opt;
        if (async)
            opt = "ASYNC";
        return static_cast<Derived &>(*this).template command<void>(std::forward<Func>(func), "FLUSHDB", opt);
    }

    /// @brief Get the info about the server.
    /// @return Server info.
    /// @see https://redis.io/commands/info
    std::string
    info() {
        return static_cast<Derived &>(*this).template command<std::string>("INFO").result;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::string> &&>, Derived &>
    info(Func &&func) {
        return static_cast<Derived &>(*this).template command<std::string>(std::forward<Func>(func), "INFO");
    }

    /// @brief Get the info about the server on the given section.
    /// @param section Section.
    /// @return Server info.
    /// @see https://redis.io/commands/info
    std::string
    info(const std::string &section) {
        return static_cast<Derived &>(*this).template command<std::string>("INFO", section).result;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::string> &&>, Derived &>
    info(Func &&func, const std::string &section) {
        return static_cast<Derived &>(*this).template command<std::string>(std::forward<Func>(func), "INFO", section);
    }

    /// @brief Get the UNIX timestamp in seconds, at which the database was saved successfully.
    /// @return The last saving time.
    /// @see https://redis.io/commands/lastsave
    long long
    lastsave() {
        return static_cast<Derived &>(*this).template command<long long>("LASTSAVE").result;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    lastsave(Func &&func) {
        return static_cast<Derived &>(*this).template command<long long>(std::forward<Func>(func), "LASTSAVE");
    }

    /// @brief Save databases into RDB file **synchronously**, i.e. block the server during saving.
    /// @see https://redis.io/commands/save
    bool
    save() {
        return static_cast<Derived &>(*this).template command<void>("SAVE").ok;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    save(Func &&func) {
        return static_cast<Derived &>(*this).template command<void>(std::forward<Func>(func), "SAVE");
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_SERVER_COMMANDS_H
