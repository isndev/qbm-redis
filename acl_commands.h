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

#ifndef QBM_REDIS_ACL_COMMANDS_H
#define QBM_REDIS_ACL_COMMANDS_H
#include "reply.h"

namespace qb::redis {

/**
 * @class acl_commands
 * @brief Provides Redis ACL (Access Control List) command implementations.
 *
 * This class implements Redis commands for working with the Access Control List
 * system, allowing management of user permissions and access to Redis commands.
 *
 * @tparam Derived The derived class type (CRTP pattern)
 */
template <typename Derived>
class acl_commands {
private:
    constexpr Derived &
    derived() {
        return static_cast<Derived &>(*this);
    }

public:
    /**
     * @brief List all ACL rules (coroutine awaitable)
     *
     * Returns all the ACL rules defined on the Redis server as a structured JSON array.
     * Each entry represents a user and their associated permissions.
     *
     * @return redis_awaiter yielding Reply<qb::json>
     * @see https://redis.io/commands/acl-list
     */
    auto acl_list() {
        return derived().template make_coro_command<qb::json>(
            [this](auto&& callback) {
                this->acl_list(std::move(callback));
            }
        );
    }

    /**
     * @brief Asynchronous version of acl_list
     *
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     * @see https://redis.io/commands/acl-list
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::json> &&>, Derived &>
    acl_list(Func &&func) {
        return derived().template command<qb::json>(std::forward<Func>(func), "ACL", "LIST");
    }

    /**
     * @brief Get ACL security events logs (coroutine awaitable)
     *
     * Returns a structured JSON array of denied commands due to ACL rules.
     * Each entry includes information about the denied command, the user that 
     * attempted to run it, the client IP address, and more.
     *
     * @param count Optional number of entries to return
     * @return redis_awaiter yielding Reply<qb::json>
     * @see https://redis.io/commands/acl-log
     */
    auto acl_log(std::optional<long long> count = std::nullopt) {
        return derived().template make_coro_command<qb::json>(
            [this, count](auto&& callback) mutable {
                this->acl_log(std::move(callback), count);
            }
        );
    }

    /**
     * @brief Asynchronous version of acl_log
     *
     * @param func Callback function to handle the result
     * @param count Optional number of entries to return
     * @return Reference to the derived class
     * @see https://redis.io/commands/acl-log
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::json> &&>, Derived &>
    acl_log(Func &&func, std::optional<long long> count = std::nullopt) {
        if (count) {
            return derived().template command<qb::json>(std::forward<Func>(func), 
                                                        "ACL", "LOG", *count);
        } else {
            return derived().template command<qb::json>(std::forward<Func>(func), 
                                                        "ACL", "LOG");
        }
    }

    /**
     * @brief List command categories for ACL (coroutine awaitable)
     *
     * Returns all the command categories that can be used with ACL rules.
     * When called with a category name parameter, returns all commands in that category.
     *
     * @param category Optional category name to list commands for
     * @return redis_awaiter yielding Reply<std::vector<std::string>>
     * @see https://redis.io/commands/acl-cat
     */
    auto acl_cat(const std::string &category = "") {
        return derived().template make_coro_command<std::vector<std::string>>(
            [this, category](auto&& callback) {
                this->acl_cat(std::move(callback), category);
            }
        );
    }

    /**
     * @brief Asynchronous version of acl_cat
     *
     * @param func Callback function to handle the result
     * @param category Optional category name to list commands for
     * @return Reference to the derived class
     * @see https://redis.io/commands/acl-cat
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>, Derived &>
    acl_cat(Func &&func, const std::string &category = "") {
        if (category.empty()) {
            return derived().template command<std::vector<std::string>>(std::forward<Func>(func), "ACL", "CAT");
        } else {
            return derived().template command<std::vector<std::string>>(std::forward<Func>(func), "ACL", "CAT", category);
        }
    }

    /**
     * @brief Get details about a Redis ACL user (coroutine awaitable)
     *
     * Returns a structured JSON object with information about the specified user,
     * including their flags, passwords, commands allowed, and key patterns.
     *
     * @param username Name of the user to get information for
     * @return redis_awaiter yielding Reply<qb::json>
     * @see https://redis.io/commands/acl-getuser
     */
    auto acl_getuser(const std::string &username) {
        return derived().template make_coro_command<qb::json>(
            [this, username](auto&& callback) {
                this->acl_getuser(std::move(callback), username);
            }
        );
    }

    /**
     * @brief Asynchronous version of acl_getuser
     *
     * @param func Callback function to handle the result
     * @param username Name of the user to get information for
     * @return Reference to the derived class
     * @see https://redis.io/commands/acl-getuser
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::json> &&>, Derived &>
    acl_getuser(Func &&func, const std::string &username) {
        return derived().template command<qb::json>(std::forward<Func>(func), 
                                                   "ACL", "GETUSER", username);
    }

    /**
     * @brief List all Redis ACL users (coroutine awaitable)
     *
     * Returns a list of all configured user names.
     *
     * @return redis_awaiter yielding Reply<std::vector<std::string>>
     * @see https://redis.io/commands/acl-users
     */
    auto acl_users() {
        return derived().template make_coro_command<std::vector<std::string>>(
            [this](auto&& callback) {
                this->acl_users(std::move(callback));
            }
        );
    }

    /**
     * @brief Asynchronous version of acl_users
     *
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     * @see https://redis.io/commands/acl-users
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>, Derived &>
    acl_users(Func &&func) {
        return derived().template command<std::vector<std::string>>(std::forward<Func>(func), "ACL", "USERS");
    }

    /**
     * @brief Return the username of the current connection (coroutine awaitable)
     *
     * Returns the username that is currently authenticated for the connection.
     *
     * @return redis_awaiter yielding Reply<std::string>
     * @see https://redis.io/commands/acl-whoami
     */
    auto acl_whoami() {
        return derived().template make_coro_command<std::string>(
            [this](auto&& callback) {
                this->acl_whoami(std::move(callback));
            }
        );
    }

    /**
     * @brief Asynchronous version of acl_whoami
     *
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     * @see https://redis.io/commands/acl-whoami
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::string> &&>, Derived &>
    acl_whoami(Func &&func) {
        return derived().template command<std::string>(std::forward<Func>(func), "ACL", "WHOAMI");
    }

    /**
     * @brief Get help information about ACL commands (coroutine awaitable)
     *
     * Returns an array of strings with help information about ACL commands.
     *
     * @return redis_awaiter yielding Reply<std::vector<std::string>>
     * @see https://redis.io/commands/acl-help
     */
    auto acl_help() {
        return derived().template make_coro_command<std::vector<std::string>>(
            [this](auto&& callback) {
                this->acl_help(std::move(callback));
            }
        );
    }

    /**
     * @brief Asynchronous version of acl_help
     *
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     * @see https://redis.io/commands/acl-help
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>, Derived &>
    acl_help(Func &&func) {
        return derived().template command<std::vector<std::string>>(std::forward<Func>(func), "ACL", "HELP");
    }

    /**
     * @brief Delete an ACL user (coroutine awaitable)
     * 
     * Removes the specified user from the Redis ACL system.
     * 
     * @param username Name of the user to delete
     * @return redis_awaiter yielding Reply<long long>
     * @see https://redis.io/commands/acl-deluser
     */
    auto acl_deluser(const std::string &username) {
        return derived().template make_coro_command<long long>(
            [this, username](auto&& callback) {
                this->acl_deluser(std::move(callback), username);
            }
        );
    }

    /**
     * @brief Asynchronous version of acl_deluser
     *
     * @param func Callback function to handle the result
     * @param username Name of the user to delete
     * @return Reference to the derived class
     * @see https://redis.io/commands/acl-deluser
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    acl_deluser(Func &&func, const std::string &username) {
        return derived().template command<long long>(std::forward<Func>(func), "ACL", "DELUSER", username);
    }

    /**
     * @brief Generate a random secure password (coroutine awaitable)
     * 
     * Generates a strong, secure password that can be used for Redis ACL users.
     * 
     * @param bits Optional number of bits of entropy (default 256)
     * @return redis_awaiter yielding Reply<std::string>
     * @see https://redis.io/commands/acl-genpass
     */
    auto acl_genpass(std::optional<long long> bits = std::nullopt) {
        return derived().template make_coro_command<std::string>(
            [this, bits](auto&& callback) mutable {
                this->acl_genpass(std::move(callback), bits);
            }
        );
    }

    /**
     * @brief Asynchronous version of acl_genpass
     *
     * @param func Callback function to handle the result
     * @param bits Optional number of bits of entropy (default 256)
     * @return Reference to the derived class
     * @see https://redis.io/commands/acl-genpass
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::string> &&>, Derived &>
    acl_genpass(Func &&func, std::optional<long long> bits = std::nullopt) {
        if (bits) {
            return derived().template command<std::string>(std::forward<Func>(func), "ACL", "GENPASS", *bits);
        } else {
            return derived().template command<std::string>(std::forward<Func>(func), "ACL", "GENPASS");
        }
    }

    /**
     * @brief Load ACL rules from the ACL file (coroutine awaitable)
     * 
     * Loads the ACL rules from the configured ACL file on disk.
     * 
     * @return redis_awaiter yielding Reply<status>
     * @see https://redis.io/commands/acl-load
     */
    auto acl_load() {
        return derived().template make_coro_command<status>(
            [this](auto&& callback) {
                this->acl_load(std::move(callback));
            }
        );
    }

    /**
     * @brief Asynchronous version of acl_load
     *
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     * @see https://redis.io/commands/acl-load
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    acl_load(Func &&func) {
        return derived().template command<status>(std::forward<Func>(func), "ACL", "LOAD");
    }

    /**
     * @brief Save ACL rules to the ACL file (coroutine awaitable)
     * 
     * Saves the current ACL rules to the configured ACL file on disk.
     * 
     * @return redis_awaiter yielding Reply<status>
     * @see https://redis.io/commands/acl-save
     */
    auto acl_save() {
        return derived().template make_coro_command<status>(
            [this](auto&& callback) {
                this->acl_save(std::move(callback));
            }
        );
    }

    /**
     * @brief Asynchronous version of acl_save
     *
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     * @see https://redis.io/commands/acl-save
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    acl_save(Func &&func) {
        return derived().template command<status>(std::forward<Func>(func), "ACL", "SAVE");
    }

    /**
     * @brief Create or modify an ACL user (coroutine awaitable)
     * 
     * Modifies the rules for a Redis ACL user.
     * 
     * @param username Name of the user to create/modify
     * @param rules Variable list of rules to apply
     * @return redis_awaiter yielding Reply<status>
     * @see https://redis.io/commands/acl-setuser
     */
    template <typename... Args>
    auto acl_setuser(const std::string &username, Args&&... rules) {
        return derived().template make_coro_command<status>(
            [this, username, ...rules = std::forward<Args>(rules)](auto&& callback) mutable {
                this->acl_setuser(std::move(callback), username, std::forward<Args>(rules)...);
            }
        );
    }

    /**
     * @brief Asynchronous version of acl_setuser
     *
     * @param func Callback function to handle the result
     * @param username Name of the user to create/modify
     * @param rules Variable list of rules to apply
     * @return Reference to the derived class
     * @see https://redis.io/commands/acl-setuser
     */
    template <typename Func, typename... Args>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    acl_setuser(Func &&func, const std::string &username, Args&&... rules) {
        return derived().template command<status>(std::forward<Func>(func), "ACL", "SETUSER", 
                                                 username, std::forward<Args>(rules)...);
    }

    /**
     * @brief Simulate if a user can execute a command (coroutine awaitable)
     *
     * Returns whether the specified user would be allowed to run the given command.
     *
     * @param username User to check
     * @param command Command name
     * @param args Command arguments (default empty)
     * @return redis_awaiter yielding Reply<qb::json>
     * @see https://redis.io/commands/acl-dryrun
     */
    auto acl_dryrun(const std::string &username, const std::string &command,
                   const std::vector<std::string> &args = {}) {
        return derived().template make_coro_command<qb::json>(
            [this, username, command, args](auto&& callback) {
                this->acl_dryrun(std::move(callback), username, command, args);
            }
        );
    }

    /**
     * @brief Asynchronous version of acl_dryrun
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @param username User to check
     * @param command Command name
     * @param args Command arguments (default empty)
     * @return Reference to the derived class
     * @see https://redis.io/commands/acl-dryrun
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::json> &&>, Derived &>
    acl_dryrun(Func &&func, const std::string &username, const std::string &command,
               const std::vector<std::string> &args = {}) {
        return derived().template command<qb::json>(
            std::forward<Func>(func), "ACL", "DRYRUN", username, command, args);
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_ACL_COMMANDS_H
