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
     * @brief List all ACL rules
     *
     * Returns all the ACL rules defined on the Redis server as a structured JSON array.
     * Each entry represents a user and their associated permissions.
     *
     * @return qb::json array of ACL rules
     * @see https://redis.io/commands/acl-list
     */
    qb::json
    acl_list() {
        return derived().template command<qb::json>("ACL", "LIST").result();
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
     * @brief Get ACL security events logs
     *
     * Returns a structured JSON array of denied commands due to ACL rules.
     * Each entry includes information about the denied command, the user that 
     * attempted to run it, the client IP address, and more.
     *
     * @param count Optional number of entries to return
     * @return qb::json array of ACL security events
     * @see https://redis.io/commands/acl-log
     */
    qb::json
    acl_log(std::optional<long long> count = std::nullopt) {
        if (count) {
            return derived().template command<qb::json>("ACL", "LOG", *count).result();
        } else {
            return derived().template command<qb::json>("ACL", "LOG").result();
        }
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
     * @brief List command categories for ACL
     *
     * Returns all the command categories that can be used with ACL rules.
     * When called with a category name parameter, returns all commands in that category.
     *
     * @param category Optional category name to list commands for
     * @return std::vector<std::string> of categories or commands
     * @see https://redis.io/commands/acl-cat
     */
    std::vector<std::string>
    acl_cat(const std::string &category = "") {
        if (category.empty()) {
            return derived().template command<std::vector<std::string>>("ACL", "CAT").result();
        } else {
            return derived().template command<std::vector<std::string>>("ACL", "CAT", category).result();
        }
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
     * @brief Get details about a Redis ACL user
     *
     * Returns a structured JSON object with information about the specified user,
     * including their flags, passwords, commands allowed, and key patterns.
     *
     * @param username Name of the user to get information for
     * @return qb::json object with user information
     * @see https://redis.io/commands/acl-getuser
     */
    qb::json
    acl_getuser(const std::string &username) {
        return derived().template command<qb::json>("ACL", "GETUSER", username).result();
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
     * @brief List all Redis ACL users
     *
     * Returns a list of all configured user names.
     *
     * @return std::vector<std::string> of user names
     * @see https://redis.io/commands/acl-users
     */
    std::vector<std::string>
    acl_users() {
        return derived().template command<std::vector<std::string>>("ACL", "USERS").result();
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
     * @brief Return the username of the current connection
     *
     * Returns the username that is currently authenticated for the connection.
     *
     * @return std::string with the username
     * @see https://redis.io/commands/acl-whoami
     */
    std::string
    acl_whoami() {
        return derived().template command<std::string>("ACL", "WHOAMI").result();
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
     * @brief Get help information about ACL commands
     *
     * Returns an array of strings with help information about ACL commands.
     *
     * @return std::vector<std::string> of help strings
     * @see https://redis.io/commands/acl-help
     */
    std::vector<std::string>
    acl_help() {
        return derived().template command<std::vector<std::string>>("ACL", "HELP").result();
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
     * @brief Delete an ACL user
     * 
     * Removes the specified user from the Redis ACL system.
     * 
     * @param username Name of the user to delete
     * @return long long Number of users removed (0 or 1)
     * @see https://redis.io/commands/acl-deluser
     */
    long long
    acl_deluser(const std::string &username) {
        return derived().template command<long long>("ACL", "DELUSER", username).result();
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
     * @brief Generate a random secure password
     * 
     * Generates a strong, secure password that can be used for Redis ACL users.
     * 
     * @param bits Optional number of bits of entropy (default 256)
     * @return std::string The generated password
     * @see https://redis.io/commands/acl-genpass
     */
    std::string
    acl_genpass(std::optional<long long> bits = std::nullopt) {
        if (bits) {
            return derived().template command<std::string>("ACL", "GENPASS", *bits).result();
        } else {
            return derived().template command<std::string>("ACL", "GENPASS").result();
        }
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
     * @brief Load ACL rules from the ACL file
     * 
     * Loads the ACL rules from the configured ACL file on disk.
     * 
     * @return status Success/failure status
     * @see https://redis.io/commands/acl-load
     */
    status
    acl_load() {
        return derived().template command<status>("ACL", "LOAD").result();
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
     * @brief Save ACL rules to the ACL file
     * 
     * Saves the current ACL rules to the configured ACL file on disk.
     * 
     * @return status Success/failure status
     * @see https://redis.io/commands/acl-save
     */
    status
    acl_save() {
        return derived().template command<status>("ACL", "SAVE").result();
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
     * @brief Create or modify an ACL user
     * 
     * Modifies the rules for a Redis ACL user.
     * 
     * @param username Name of the user to create/modify
     * @param rules Variable list of rules to apply
     * @return status Success/failure status
     * @see https://redis.io/commands/acl-setuser
     */
    template <typename... Args>
    status
    acl_setuser(const std::string &username, Args&&... rules) {
        return derived().template command<status>("ACL", "SETUSER", username, std::forward<Args>(rules)...).result();
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
};

} // namespace qb::redis

#endif // QBM_REDIS_ACL_COMMANDS_H 