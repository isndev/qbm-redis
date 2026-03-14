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

#ifndef QBM_REDIS_FUNCTION_COMMANDS_H
#define QBM_REDIS_FUNCTION_COMMANDS_H
#include "reply.h"

namespace qb::redis {

/**
 * @class function_commands
 * @brief Provides Redis Function command implementations.
 *
 * This class implements Redis commands for working with the Functions feature,
 * which allows management of server-side Lua functions.
 *
 * @tparam Derived The derived class type (CRTP pattern)
 */
template <typename Derived>
class function_commands {
private:
    constexpr Derived &
    derived() {
        return static_cast<Derived &>(*this);
    }

public:
    /**
     * @brief List all functions (coroutine awaitable)
     *
     * Returns a list of all functions stored in the function library as a structured JSON object.
     * Each entry contains information about a function, including name, code, and other details.
     *
     * @param library Optional library name to filter the results
     * @return redis_awaiter yielding Reply<qb::json>
     * @see https://redis.io/commands/function-list
     */
    auto function_list(const std::optional<std::string> &library = std::nullopt) {
        return derived().template make_coro_command<qb::json>(
            [this, library](auto&& callback) mutable {
                this->function_list(std::move(callback), library);
            }
        );
    }

    /**
     * @brief Asynchronous version of function_list
     *
     * @param func Callback function to handle the result
     * @param library Optional library name to filter the results
     * @return Reference to the derived class
     * @see https://redis.io/commands/function-list
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::json> &&>, Derived &>
    function_list(Func &&func, const std::optional<std::string> &library = std::nullopt) {
        if (library) {
            return derived().template command<qb::json>(std::forward<Func>(func), 
                                                       "FUNCTION", "LIST", "LIBRARYNAME", *library);
        } else {
            return derived().template command<qb::json>(std::forward<Func>(func), 
                                                       "FUNCTION", "LIST");
        }
    }

    /**
     * @brief Load a library into the function library (coroutine awaitable)
     *
     * Loads a library into the functions library. The code must contain a Lua script 
     * that defines a table containing at least one function.
     *
     * @param code Lua code for the library
     * @param options Optional parameters (REPLACE, CONFIG)
     * @return redis_awaiter yielding Reply<status>
     * @see https://redis.io/commands/function-load
     */
    template <typename... Args>
    auto function_load(const std::string &code, Args&&... options) {
        return derived().template make_coro_command<status>(
            [this, code, ...options = std::forward<Args>(options)](auto&& callback) mutable {
                this->function_load(std::move(callback), code, std::forward<Args>(options)...);
            }
        );
    }

    /**
     * @brief Asynchronous version of function_load
     *
     * @param func Callback function to handle the result
     * @param code Lua code for the library
     * @param options Optional parameters (REPLACE, CONFIG)
     * @return Reference to the derived class
     * @see https://redis.io/commands/function-load
     */
    template <typename Func, typename... Args>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    function_load(Func &&func, const std::string &code, Args&&... options) {
        return derived().template command<status>(std::forward<Func>(func), "FUNCTION", "LOAD", 
                                                 std::forward<Args>(options)..., code);
    }

    /**
     * @brief Delete a function from the library (coroutine awaitable)
     *
     * Deletes a library and all its functions from the function library.
     *
     * @param library Name of the library to delete
     * @return redis_awaiter yielding Reply<status>
     * @see https://redis.io/commands/function-delete
     */
    auto function_delete(const std::string &library) {
        return derived().template make_coro_command<status>(
            [this, library](auto&& callback) {
                this->function_delete(std::move(callback), library);
            }
        );
    }

    /**
     * @brief Asynchronous version of function_delete
     *
     * @param func Callback function to handle the result
     * @param library Name of the library to delete
     * @return Reference to the derived class
     * @see https://redis.io/commands/function-delete
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    function_delete(Func &&func, const std::string &library) {
        return derived().template command<status>(std::forward<Func>(func), "FUNCTION", "DELETE", library);
    }

    /**
     * @brief Remove all libraries from the function library (coroutine awaitable)
     *
     * Deletes all libraries and functions from the function library.
     *
     * @param mode Optional flush mode: ASYNC or SYNC (default SYNC)
     * @return redis_awaiter yielding Reply<status>
     * @see https://redis.io/commands/function-flush
     */
    auto function_flush(const std::string &mode = "SYNC") {
        return derived().template make_coro_command<status>(
            [this, mode](auto&& callback) {
                this->function_flush(std::move(callback), mode);
            }
        );
    }

    /**
     * @brief Asynchronous version of function_flush
     *
     * @param func Callback function to handle the result
     * @param mode Optional flush mode: ASYNC or SYNC (default SYNC)
     * @return Reference to the derived class
     * @see https://redis.io/commands/function-flush
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    function_flush(Func &&func, const std::string &mode = "SYNC") {
        return derived().template command<status>(std::forward<Func>(func), "FUNCTION", "FLUSH", mode);
    }

    /**
     * @brief Kill a function that is currently executing (coroutine awaitable)
     *
     * Kills a function that is currently executing. Works only on scripts that 
     * are currently running.
     *
     * @return redis_awaiter yielding Reply<status>
     * @see https://redis.io/commands/function-kill
     */
    auto function_kill() {
        return derived().template make_coro_command<status>(
            [this](auto&& callback) {
                this->function_kill(std::move(callback));
            }
        );
    }

    /**
     * @brief Asynchronous version of function_kill
     *
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     * @see https://redis.io/commands/function-kill
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    function_kill(Func &&func) {
        return derived().template command<status>(std::forward<Func>(func), "FUNCTION", "KILL");
    }

    /**
     * @brief Get statistics about the function runtime (coroutine awaitable)
     *
     * Returns information about the function runtime environment.
     *
     * @return redis_awaiter yielding Reply<qb::json>
     * @see https://redis.io/commands/function-stats
     */
    auto function_stats() {
        return derived().template make_coro_command<qb::json>(
            [this](auto&& callback) {
                this->function_stats(std::move(callback));
            }
        );
    }

    /**
     * @brief Asynchronous version of function_stats
     *
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     * @see https://redis.io/commands/function-stats
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::json> &&>, Derived &>
    function_stats(Func &&func) {
        return derived().template command<qb::json>(std::forward<Func>(func), "FUNCTION", "STATS");
    }

    /**
     * @brief Dump all functions (coroutine awaitable)
     *
     * Returns a serialized payload representing all the functions stored in the function library.
     * This payload can be used with the FUNCTION RESTORE command to restore the libraries.
     *
     * @return redis_awaiter yielding Reply<qb::json>
     * @see https://redis.io/commands/function-dump
     */
    auto function_dump() {
        return derived().template make_coro_command<qb::json>(
            [this](auto&& callback) {
                this->function_dump(std::move(callback));
            }
        );
    }

    /**
     * @brief Asynchronous version of function_dump
     *
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     * @see https://redis.io/commands/function-dump
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::json> &&>, Derived &>
    function_dump(Func &&func) {
        return derived().template command<qb::json>(std::forward<Func>(func), "FUNCTION", "DUMP");
    }

    /**
     * @brief Restore functions from the serialized payload (coroutine awaitable)
     *
     * Restores the libraries represented by the given serialized payload.
     * The payload should have been created using the FUNCTION DUMP command.
     *
     * @param payload Serialized payload from function_dump
     * @param policy Optional restore policy: APPEND, REPLACE or FLUSH (default APPEND)
     * @return redis_awaiter yielding Reply<status>
     * @see https://redis.io/commands/function-restore
     */
    auto function_restore(const std::string &payload, const std::string &policy = "APPEND") {
        return derived().template make_coro_command<status>(
            [this, payload, policy](auto&& callback) {
                this->function_restore(std::move(callback), payload, policy);
            }
        );
    }

    /**
     * @brief Asynchronous version of function_restore
     *
     * @param func Callback function to handle the result
     * @param payload Serialized payload from function_dump
     * @param policy Optional restore policy: APPEND, REPLACE or FLUSH (default APPEND)
     * @return Reference to the derived class
     * @see https://redis.io/commands/function-restore
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    function_restore(Func &&func, const std::string &payload, const std::string &policy = "APPEND") {
        return derived().template command<status>(std::forward<Func>(func), "FUNCTION", "RESTORE", payload, policy);
    }

    /**
     * @brief Get help information about function commands (coroutine awaitable)
     *
     * Returns an array of strings with help information about Redis FUNCTION commands.
     *
     * @return redis_awaiter yielding Reply<std::vector<std::string>>
     * @see https://redis.io/commands/function-help
     */
    auto function_help() {
        return derived().template make_coro_command<std::vector<std::string>>(
            [this](auto&& callback) {
                this->function_help(std::move(callback));
            }
        );
    }

    /**
     * @brief Asynchronous version of function_help
     *
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     * @see https://redis.io/commands/function-help
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>, Derived &>
    function_help(Func &&func) {
        return derived().template command<std::vector<std::string>>(std::forward<Func>(func), "FUNCTION", "HELP");
    }

    /**
     * @brief Invoke a function (coroutine awaitable).
     * @param name Function name (e.g. "mylib::myfunc").
     * @param keys Key arguments.
     * @param args Additional arguments.
     * @see https://redis.io/commands/fcall
     */
    template <typename Ret>
    auto fcall(const std::string &name, const std::vector<std::string> &keys = {},
               const std::vector<std::string> &args = {}) {
        return derived().template make_coro_command<Ret>(
            [this, name, keys, args](auto&& callback) mutable {
                this->template fcall<Ret>(std::move(callback), name, keys, args);
            }
        );
    }
    template <typename Ret, typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<Ret> &&>, Derived &>
    fcall(Func &&func, const std::string &name,
          const std::vector<std::string> &keys = {},
          const std::vector<std::string> &args = {}) {
        return derived().template command<Ret>(std::forward<Func>(func), "FCALL",
                                              name, keys.size(), keys, args);
    }

    /**
     * @brief Read-only variant of FCALL - function cannot perform writes.
     * @see https://redis.io/commands/fcall_ro
     */
    template <typename Ret>
    auto fcallRo(const std::string &name, const std::vector<std::string> &keys = {},
                 const std::vector<std::string> &args = {}) {
        return derived().template make_coro_command<Ret>(
            [this, name, keys, args](auto&& callback) mutable {
                this->template fcallRo<Ret>(std::move(callback), name, keys, args);
            }
        );
    }
    template <typename Ret, typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<Ret> &&>, Derived &>
    fcallRo(Func &&func, const std::string &name,
            const std::vector<std::string> &keys = {},
            const std::vector<std::string> &args = {}) {
        return derived().template command<Ret>(std::forward<Func>(func), "FCALL_RO",
                                              name, keys.size(), keys, args);
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_FUNCTION_COMMANDS_H
