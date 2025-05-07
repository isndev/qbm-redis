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

#ifndef QBM_REDIS_MODULE_COMMANDS_H
#define QBM_REDIS_MODULE_COMMANDS_H
#include "reply.h"

namespace qb::redis {

/**
 * @class module_commands
 * @brief Provides Redis Module command implementations.
 *
 * This class implements Redis commands for working with Redis modules,
 * allowing management and inspection of loaded modules.
 *
 * @tparam Derived The derived class type (CRTP pattern)
 */
template <typename Derived>
class module_commands {
private:
    constexpr Derived &
    derived() {
        return static_cast<Derived &>(*this);
    }

public:
    /**
     * @brief List all loaded modules
     *
     * Returns a list of modules loaded in the Redis server as a structured JSON array.
     * Each entry contains information about a module, including name, version, and
     * other details.
     *
     * @return qb::json array of loaded modules
     * @see https://redis.io/commands/module-list
     */
    qb::json
    module_list() {
        return derived().template command<qb::json>("MODULE", "LIST").result();
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
     * @brief Load a module into Redis
     *
     * Loads a module from a shared library file into the Redis server.
     * Optionally accepts additional arguments to be passed to the module.
     *
     * @param path Path to the module's shared library file
     * @param args Optional additional arguments to pass to the module
     * @return status Success/failure status
     * @see https://redis.io/commands/module-load
     */
    template <typename... Args>
    status
    module_load(const std::string &path, Args&&... args) {
        return derived().template command<status>("MODULE", "LOAD", path, std::forward<Args>(args)...).result();
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
    module_load(Func &&func, const std::string &path, Args&&... args) {
        return derived().template command<status>(std::forward<Func>(func), "MODULE", "LOAD", path, std::forward<Args>(args)...);
    }

    /**
     * @brief Unload a module from Redis
     *
     * Unloads a module from the Redis server, removing all commands registered by the module.
     *
     * @param name Name of the module to unload
     * @return status Success/failure status
     * @see https://redis.io/commands/module-unload
     */
    status
    module_unload(const std::string &name) {
        return derived().template command<status>("MODULE", "UNLOAD", name).result();
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
     * @brief Get help information about module commands
     *
     * Returns an array of strings with help information about Redis module commands.
     *
     * @return std::vector<std::string> of help strings
     * @see https://redis.io/commands/module-help
     */
    std::vector<std::string>
    module_help() {
        return derived().template command<std::vector<std::string>>("MODULE", "HELP").result();
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