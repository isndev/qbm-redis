/**
 * @file qbm/redis/commands/module_commands.h
 * @brief Redis Modules management command mixin for the qb Redis module.
 *
 * Provides the @ref qb::redis::module_commands CRTP mixin exposing the Redis
 * MODULE management commands (LIST, LOAD, UNLOAD, HELP) for inspecting and
 * controlling dynamically loaded Redis modules. Each command is offered in both
 * a coroutine-awaitable form and an asynchronous callback-based form.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2026 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)
 * @ingroup Redis
 */
#ifndef QBM_REDIS_MODULE_COMMANDS_H
#define QBM_REDIS_MODULE_COMMANDS_H
#include "../reply.h"

namespace qb::redis {

/**
 * @class module_commands
 * @brief Provides Redis Module command implementations.
 *
 * This class implements Redis commands for working with Redis modules,
 * allowing management and inspection of loaded modules.
 *
 * @tparam Derived The derived class type (CRTP pattern)
 * @ingroup Redis
 */
template <typename Derived>
class module_commands {
private:
    /**
     * @brief Access the CRTP-derived instance.
     * @return Reference to the derived class.
     */
    constexpr Derived &
    derived() {
        return static_cast<Derived &>(*this);
    }

public:
    /**
     * @brief List all loaded modules (coroutine awaitable)
     *
     * Returns a list of modules loaded in the Redis server as a structured JSON array.
     * Each entry contains information about a module, including name, version, and
     * other details.
     *
     * @return redis_awaiter yielding Reply<qb::json>
     * @see https://redis.io/commands/module-list
     */
    auto
    module_list() {
        return derived().template make_coro_command<qb::json>([this](auto &&callback) { this->module_list(std::move(callback)); });
    }

    /**
     * @brief Asynchronous version of module_list
     *
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     * @see https://redis.io/commands/module-list
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::json> &&>, Derived &>
    module_list(Func &&func) {
        return derived().template command<qb::json>(std::forward<Func>(func), "MODULE", "LIST");
    }

    /**
     * @brief Load a module into Redis (coroutine awaitable)
     *
     * Loads a module from a shared library file into the Redis server.
     * Optionally accepts additional arguments to be passed to the module.
     *
     * @param path Path to the module's shared library file
     * @param args Optional additional arguments to pass to the module
     * @return redis_awaiter yielding Reply<status>
     * @see https://redis.io/commands/module-load
     */
    template <typename... Args>
    auto
    module_load(const std::string &path, Args &&...args) {
        return derived().template make_coro_command<status>([this, path, ... args = std::forward<Args>(args)](auto &&callback) mutable {
            this->module_load(std::move(callback), path, std::forward<Args>(args)...);
        });
    }

    /**
     * @brief Asynchronous version of module_load
     *
     * @param func Callback function to handle the result
     * @param path Path to the module's shared library file
     * @param args Optional additional arguments to pass to the module
     * @return Reference to the derived class
     * @see https://redis.io/commands/module-load
     */
    template <typename Func, typename... Args>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    module_load(Func &&func, const std::string &path, Args &&...args) {
        return derived().template command<status>(std::forward<Func>(func), "MODULE", "LOAD", path, std::forward<Args>(args)...);
    }

    /**
     * @brief Unload a module from Redis (coroutine awaitable)
     *
     * Unloads a module from the Redis server, removing all commands registered by the module.
     *
     * @param name Name of the module to unload
     * @return redis_awaiter yielding Reply<status>
     * @see https://redis.io/commands/module-unload
     */
    auto
    module_unload(const std::string &name) {
        return derived().template make_coro_command<status>([this, name](auto &&callback) { this->module_unload(std::move(callback), name); });
    }

    /**
     * @brief Asynchronous version of module_unload
     *
     * @param func Callback function to handle the result
     * @param name Name of the module to unload
     * @return Reference to the derived class
     * @see https://redis.io/commands/module-unload
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    module_unload(Func &&func, const std::string &name) {
        return derived().template command<status>(std::forward<Func>(func), "MODULE", "UNLOAD", name);
    }

    /**
     * @brief Get help information about module commands (coroutine awaitable)
     *
     * Returns an array of strings with help information about Redis module commands.
     *
     * @return redis_awaiter yielding Reply<std::vector<std::string>>
     * @see https://redis.io/commands/module-help
     */
    auto
    module_help() {
        return derived().template make_coro_command<std::vector<std::string>>(
            [this](auto &&callback) { this->module_help(std::move(callback)); });
    }

    /**
     * @brief Asynchronous version of module_help
     *
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     * @see https://redis.io/commands/module-help
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>, Derived &>
    module_help(Func &&func) {
        return derived().template command<std::vector<std::string>>(std::forward<Func>(func), "MODULE", "HELP");
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_MODULE_COMMANDS_H
