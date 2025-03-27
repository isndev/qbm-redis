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

/**
 * @class server_commands
 * @brief Provides Redis server administration command implementations.
 *
 * This class implements Redis commands for server administration,
 * including server information, client management, configuration, and more.
 *
 * @tparam Derived The derived class type (CRTP pattern)
 */
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

    /**
     * @brief Gets information and statistics about the server
     *
     * @param section Optional section argument to filter the information
     * @return Server information as a string
     */
    std::string
    info(const std::string &section = "") {
        std::optional<std::string> param;
        if (!section.empty())
            param = section;
        return static_cast<Derived &>(*this).template command<std::string>("INFO", param).result;
    }

    /**
     * @brief Asynchronous version of info
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param section Optional section argument to filter the information
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::string> &&>, Derived &>
    info(Func &&func, const std::string &section = "") {
        std::optional<std::string> param;
        if (!section.empty())
            param = section;
        return static_cast<Derived &>(*this).template command<std::string>(
            std::forward<Func>(func), "INFO", param);
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

    /**
     * @brief Gets the current server time
     *
     * @return A pair containing the UNIX timestamp and microseconds
     */
    std::pair<long long, long long>
    time() {
        auto res = static_cast<Derived &>(*this).template command<std::vector<std::string>>("TIME");
        if (res.ok && res.result.size() == 2)
            return std::make_pair(std::stoll(res.result[0]), std::stoll(res.result[1]));
        return std::make_pair(0, 0);
    }

    /**
     * @brief Asynchronous version of time
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::pair<long long, long long>> &&>, Derived &>
    time(Func &&func) {
        return static_cast<Derived &>(*this).template command<std::vector<std::string>>(
            [f = std::forward<Func>(func)](auto &&reply) mutable {
                Reply<std::pair<long long, long long>> r;
                r.ok = reply.ok;
                if (r.ok && reply.result.size() == 2)
                    r.result = std::make_pair(std::stoll(reply.result[0]), std::stoll(reply.result[1]));
                std::move(f)(std::move(r));
            },
            "TIME");
    }

    /**
     * @brief Gets the current server time (asynchronous version)
     *
     * @tparam Func Callback function type with vector of strings
     * @param func Callback function
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>, Derived &>
    time_raw(Func &&func) {
        return static_cast<Derived &>(*this).template command<std::vector<std::string>>(std::forward<Func>(func), "TIME");
    }

    /**
     * @brief Returns the role of the instance in the context of replication
     *
     * @return A vector containing role information
     */
    std::vector<std::string>
    role() {
        return static_cast<Derived &>(*this).template command<std::vector<std::string>>("ROLE").result;
    }

    /**
     * @brief Asynchronous version of role
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>, Derived &>
    role(Func &&func) {
        return static_cast<Derived &>(*this).template command<std::vector<std::string>>(std::forward<Func>(func), "ROLE");
    }

    /**
     * @brief Sets the client's connection name
     *
     * @param name The name to set for the connection
     * @return true if the name was set successfully, false otherwise
     */
    bool
    client_setname(const std::string &name) {
        return static_cast<Derived &>(*this).template command<void>("CLIENT", "SETNAME", name).ok;
    }

    /**
     * @brief Asynchronous version of client_setname
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param name The name to set for the connection
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    client_setname(Func &&func, const std::string &name) {
        return static_cast<Derived &>(*this).template command<void>(std::forward<Func>(func), "CLIENT", "SETNAME", name);
    }

    /**
     * @brief Gets the name of the current connection
     *
     * @return The name of the connection or empty string if no name is set
     */
    std::string
    client_getname() {
        auto res = static_cast<Derived &>(*this).template command<std::string>("CLIENT", "GETNAME");
        return res.ok ? res.result : "";
    }

    /**
     * @brief Asynchronous version of client_getname
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::string> &&>, Derived &>
    client_getname(Func &&func) {
        return static_cast<Derived &>(*this).template command<std::string>(std::forward<Func>(func), "CLIENT", "GETNAME");
    }

    /**
     * @brief Gets the list of client connections
     *
     * @return A string containing client connection details
     */
    std::string
    client_list() {
        return static_cast<Derived &>(*this).template command<std::string>("CLIENT", "LIST").result;
    }

    /**
     * @brief Asynchronous version of client_list
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::string> &&>, Derived &>
    client_list(Func &&func) {
        return static_cast<Derived &>(*this).template command<std::string>(std::forward<Func>(func), "CLIENT", "LIST");
    }

    /**
     * @brief Gets the current configuration parameters of a Redis server
     *
     * @param parameter Configuration parameter pattern to match
     * @return A vector of vectors containing parameter names and values
     */
    std::vector<std::vector<std::string>>
    config_get(const std::string &parameter) {
        return static_cast<Derived &>(*this)
            .template command<std::vector<std::vector<std::string>>>("CONFIG", "GET", parameter)
            .result;
    }

    /**
     * @brief Asynchronous version of config_get
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param parameter Configuration parameter pattern to match
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::vector<std::string>>> &&>, Derived &>
    config_get(Func &&func, const std::string &parameter) {
        return static_cast<Derived &>(*this).template command<std::vector<std::vector<std::string>>>(
            std::forward<Func>(func), "CONFIG", "GET", parameter);
    }

    /**
     * @brief Sets a configuration parameter to the given value
     *
     * @param parameter The configuration parameter to set
     * @param value The value to set
     * @return true if the parameter was set successfully, false otherwise
     */
    bool
    config_set(const std::string &parameter, const std::string &value) {
        return static_cast<Derived &>(*this).template command<void>("CONFIG", "SET", parameter, value).ok;
    }

    /**
     * @brief Asynchronous version of config_set
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param parameter The configuration parameter to set
     * @param value The value to set
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    config_set(Func &&func, const std::string &parameter, const std::string &value) {
        return static_cast<Derived &>(*this).template command<void>(
            std::forward<Func>(func), "CONFIG", "SET", parameter, value);
    }

    /**
     * @brief Resets the statistics reported by Redis using the INFO command
     *
     * @return true if the statistics were reset successfully, false otherwise
     */
    bool
    config_resetstat() {
        return static_cast<Derived &>(*this).template command<void>("CONFIG", "RESETSTAT").ok;
    }

    /**
     * @brief Asynchronous version of config_resetstat
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    config_resetstat(Func &&func) {
        return static_cast<Derived &>(*this).template command<void>(std::forward<Func>(func), "CONFIG", "RESETSTAT");
    }

    /**
     * @brief Rewrites the configuration file with the current configuration
     *
     * @return true if the configuration was rewritten successfully, false otherwise
     */
    bool
    config_rewrite() {
        return static_cast<Derived &>(*this).template command<void>("CONFIG", "REWRITE").ok;
    }

    /**
     * @brief Asynchronous version of config_rewrite
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    config_rewrite(Func &&func) {
        return static_cast<Derived &>(*this).template command<void>(std::forward<Func>(func), "CONFIG", "REWRITE");
    }

    /**
     * @brief Returns statistical information about commands processed by the server
     *
     * @return A vector of vectors containing command statistics
     */
    std::vector<std::vector<std::string>>
    command_info() {
        return static_cast<Derived &>(*this)
            .template command<std::vector<std::vector<std::string>>>("COMMAND")
            .result;
    }

    /**
     * @brief Asynchronous version of command_info
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::vector<std::string>>> &&>, Derived &>
    command_info(Func &&func) {
        return static_cast<Derived &>(*this).template command<std::vector<std::vector<std::string>>>(
            std::forward<Func>(func), "COMMAND");
    }

    /**
     * @brief Returns the total number of commands in the Redis command table
     *
     * @return The number of commands
     */
    long long
    command_count() {
        return static_cast<Derived &>(*this).template command<long long>("COMMAND", "COUNT").result;
    }

    /**
     * @brief Asynchronous version of command_count
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    command_count(Func &&func) {
        return static_cast<Derived &>(*this).template command<long long>(std::forward<Func>(func), "COMMAND", "COUNT");
    }

    /**
     * @brief Gets array of Redis command details
     *
     * @param command_names Vector of command names to get details for
     * @return A vector of vectors containing command details
     */
    std::vector<std::vector<std::string>>
    command_info(const std::vector<std::string> &command_names) {
        return static_cast<Derived &>(*this)
            .template command_argv<std::vector<std::vector<std::string>>>("COMMAND", "INFO", command_names)
            .result;
    }

    /**
     * @brief Asynchronous version of command_info with specific command names
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param command_names Vector of command names to get details for
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::vector<std::string>>> &&>, Derived &>
    command_info(Func &&func, const std::vector<std::string> &command_names) {
        return static_cast<Derived &>(*this).template command_argv<std::vector<std::vector<std::string>>>(
            std::forward<Func>(func), "COMMAND", "INFO", command_names);
    }

    /**
     * @brief Gets all command names in a vector
     *
     * @return A vector of command names
     */
    std::vector<std::string>
    command_getkeys() {
        return static_cast<Derived &>(*this).template command<std::vector<std::string>>("COMMAND", "GETKEYS").result;
    }

    /**
     * @brief Asynchronous version of command_getkeys
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>, Derived &>
    command_getkeys(Func &&func) {
        return static_cast<Derived &>(*this).template command<std::vector<std::string>>(
            std::forward<Func>(func), "COMMAND", "GETKEYS");
    }

    /**
     * @brief Tells the Redis server to stop processing commands from clients for the specified number of seconds
     *
     * @param delay Number of seconds to stop processing commands
     * @return true if successful, false otherwise
     */
    bool
    debug_sleep(double delay) {
        return static_cast<Derived &>(*this).template command<void>("DEBUG", "SLEEP", delay).ok;
    }

    /**
     * @brief Asynchronous version of debug_sleep
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param delay Number of seconds to stop processing commands
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    debug_sleep(Func &&func, double delay) {
        return static_cast<Derived &>(*this).template command<void>(std::forward<Func>(func), "DEBUG", "SLEEP", delay);
    }

    /**
     * @brief Gets the slow log entries
     *
     * @param count Maximum number of entries to return
     * @return A vector of vectors containing slow log entries
     */
    std::vector<std::vector<std::string>>
    slowlog_get(std::optional<long long> count = std::nullopt) {
         return static_cast<Derived &>(*this)
            .template command<std::vector<std::vector<std::string>>>("SLOWLOG", "GET", count)
            .result;
    }

    /**
     * @brief Asynchronous version of slowlog_get
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param count Maximum number of entries to return
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::vector<std::string>>> &&>, Derived &>
    slowlog_get(Func &&func, std::optional<long long> count = std::nullopt) {
        return static_cast<Derived &>(*this).template command<std::vector<std::vector<std::string>>>(
            std::forward<Func>(func), "SLOWLOG", "GET", count);
    }

    /**
     * @brief Gets the length of the slow log
     *
     * @return The length of the slow log
     */
    long long
    slowlog_len() {
        return static_cast<Derived &>(*this).template command<long long>("SLOWLOG", "LEN").result;
    }

    /**
     * @brief Asynchronous version of slowlog_len
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    slowlog_len(Func &&func) {
        return static_cast<Derived &>(*this).template command<long long>(std::forward<Func>(func), "SLOWLOG", "LEN");
    }

    /**
     * @brief Resets the slow log
     *
     * @return true if successful, false otherwise
     */
    bool
    slowlog_reset() {
        return static_cast<Derived &>(*this).template command<void>("SLOWLOG", "RESET").ok;
    }

    /**
     * @brief Asynchronous version of slowlog_reset
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    slowlog_reset(Func &&func) {
        return static_cast<Derived &>(*this).template command<void>(std::forward<Func>(func), "SLOWLOG", "RESET");
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_SERVER_COMMANDS_H
