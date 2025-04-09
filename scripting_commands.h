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

#ifndef QBM_REDIS_SCRIPTING_COMMANDS_H
#define QBM_REDIS_SCRIPTING_COMMANDS_H
#include "reply.h"

namespace qb::redis {

/**
 * @class scripting_commands
 * @brief Provides Redis Lua scripting command implementations.
 *
 * This class implements Redis Lua scripting commands for executing scripts
 * on the Redis server. Lua scripting allows for more complex operations
 * to be performed atomically and can reduce network roundtrips.
 *
 * @tparam Derived The derived class type (CRTP pattern)
 */
template <typename Derived>
class scripting_commands {
private:
    constexpr Derived &
    derived() {
        return static_cast<Derived &>(*this);
    }

public:
    /**
     * @brief Executes a Lua script on the Redis server
     *
     * @tparam Ret Return type of the script execution
     * @param script Lua script to execute
     * @param keys Vector of key names that the script will access
     * @param args Vector of additional arguments to the script
     * @return Result of the script execution, typed as Ret
     */
    template <typename Ret>
    auto
    eval(const std::string &script, const std::vector<std::string> &keys = {},
         const std::vector<std::string> &args = {}) {
        return derived()
            .template command<Ret>("EVAL", script, keys.size(), keys, args)
            .result();
    }

    /**
     * @brief Asynchronous version of eval
     *
     * @tparam Func Callback function type
     * @tparam Ret Return type of the script execution
     * @param func Callback function
     * @param script Lua script to execute
     * @param keys Vector of key names that the script will access
     * @param args Vector of additional arguments to the script
     * @return Reference to the Redis handler for chaining
     */
    template <typename Ret, typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<Ret> &&>, Derived &>
    eval(Func &&func, const std::string &script,
         const std::vector<std::string> &keys = {},
         const std::vector<std::string> &args = {}) {
        return derived().template command<Ret>(std::forward<Func>(func), "EVAL", script,
                                               keys.size(), keys, args);
    }

    /**
     * @brief Executes a pre-loaded Lua script on the Redis server using its SHA1 hash
     *
     * @tparam Ret Return type of the script execution
     * @param script SHA1 hash of the script previously loaded with SCRIPT LOAD
     * @param keys Vector of key names that the script will access
     * @param args Vector of additional arguments to the script
     * @return Result of the script execution, typed as Ret
     */
    template <typename Ret>
    Ret
    evalsha(const std::string &script, const std::vector<std::string> &keys = {},
            const std::vector<std::string> &args = {}) {
        return derived()
            .template command<Ret>("EVALSHA", script, keys.size(), keys, args)
            .result();
    }

    /**
     * @brief Asynchronous version of evalsha
     *
     * @tparam Func Callback function type
     * @tparam Ret Return type of the script execution
     * @param func Callback function
     * @param script SHA1 hash of the script previously loaded with SCRIPT LOAD
     * @param keys Vector of key names that the script will access
     * @param args Vector of additional arguments to the script
     * @return Reference to the Redis handler for chaining
     */
    template <typename Ret, typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<Ret> &&>, Derived &>
    evalsha(Func &&func, const std::string &script,
            const std::vector<std::string> &keys = {},
            const std::vector<std::string> &args = {}) {
        return derived().template command<Ret>(std::forward<Func>(func), "EVALSHA",
                                               script, keys.size(), keys, args);
    }

    /**
     * @brief Checks if scripts exist in the script cache by their SHA1 hashes
     *
     * @tparam Keys Variadic types for script SHA1 hashes
     * @param keys SHA1 hashes of scripts to check
     * @return Vector of booleans indicating whether each script exists in the cache
     */
    template <typename... Keys>
    std::vector<bool>
    script_exists(Keys &&...keys) {
        return derived()
            .template command<std::vector<bool>>("SCRIPT", "EXISTS",
                                                 std::forward<Keys>(keys)...)
            .result();
    }

    /**
     * @brief Asynchronous version of script_exists
     *
     * @tparam Func Callback function type
     * @tparam Keys Variadic types for script SHA1 hashes
     * @param func Callback function
     * @param keys SHA1 hashes of scripts to check
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func, typename... Keys>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<bool>> &&>, Derived &>
    script_exists(Func &&func, Keys &&...keys) {
        return derived().template command<std::vector<bool>>(
            std::forward<Func>(func), "SCRIPT", "EXISTS", std::forward<Keys>(keys)...);
    }

    /**
     * @brief Removes all scripts from the script cache
     *
     * @return status object with the result
     */
    status
    script_flush() {
        return derived().template command<status>("SCRIPT", "FLUSH").result();
    }

    /**
     * @brief Asynchronous version of script_flush
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    script_flush(Func &&func) {
        return derived().template command<status>(std::forward<Func>(func), "SCRIPT",
                                                  "FLUSH");
    }

    /**
     * @brief Kills the currently executing Lua script
     *
     * @return status object with the result
     */
    status
    script_kill() {
        return derived().template command<status>("SCRIPT", "KILL").result();
    }

    /**
     * @brief Asynchronous version of script_kill
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    script_kill(Func &&func) {
        return derived().template command<status>(std::forward<Func>(func), "SCRIPT",
                                                  "KILL");
    }

    /**
     * @brief Loads a Lua script into the script cache
     *
     * @param script Lua script to load
     * @return SHA1 hash of the loaded script
     */
    inline std::string
    script_load(std::string const &script) {
        return derived()
            .template command<std::string>("SCRIPT", "LOAD", script)
            .result();
    }

    /**
     * @brief Asynchronous version of script_load
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param script Lua script to load
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::string> &&>, Derived &>
    script_load(Func &&func, std::string const &script) {
        return derived().template command<std::string>(std::forward<Func>(func),
                                                       "SCRIPT", "LOAD", script);
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_SCRIPTING_COMMANDS_H
