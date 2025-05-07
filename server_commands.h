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

#ifndef QBM_REDIS_SERVER_COMMANDS_H
#define QBM_REDIS_SERVER_COMMANDS_H
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include "reply.h"

namespace qb::redis {

/**
 * @class server_commands
 * @brief Provides Redis server administration command implementations.
 *
 * This class implements Redis commands for server administration,
 * including server information, client management, configuration, and more.
 * Each command has both synchronous and asynchronous versions.
 *
 * The commands are organized into several categories:
 * - Client Management Commands
 * - Configuration Commands
 * - Command Information Commands
 * - Debug Commands
 * - Memory Commands
 * - Monitor Commands
 * - Role Commands
 * - Shutdown Commands
 * - Slave Commands
 * - Slowlog Commands
 * - Sync Commands
 * - Persistence Commands
 * - Database Commands
 * - Server Information Commands
 *
 * @tparam Derived The derived class type (CRTP pattern)
 */
template <typename Derived>
class server_commands {
private:
    constexpr Derived &
    derived() {
        return static_cast<Derived &>(*this);
    }

    /**
     * @brief Helper method to parse INFO command output into memory_info structure
     *
     * @param info_str The raw INFO command output
     * @return Structured memory_info object with parsed data
     */
    static qb::redis::memory_info
    parse_info_to_memory_info(const std::string &info_str) {
        qb::redis::memory_info info;
        std::istringstream     iss(info_str);
        std::string            line;

        while (std::getline(iss, line)) {
            // Skip empty lines and comments
            if (line.empty() || line[0] == '#') {
                continue;
            }

            // Parse key-value pairs
            size_t pos = line.find(':');
            if (pos != std::string::npos) {
                std::string key = line.substr(0, pos);
                std::string val = line.substr(pos + 1);

                try {
                    if (key == "used_memory") {
                        info.used_memory = std::stoull(val);
                    } else if (key == "used_memory_peak") {
                        info.used_memory_peak = std::stoull(val);
                    } else if (key == "used_memory_lua") {
                        info.used_memory_lua = std::stoull(val);
                    } else if (key == "used_memory_scripts") {
                        info.used_memory_scripts = std::stoull(val);
                    } else if (key == "connected_clients") {
                        info.number_of_connected_clients = std::stoull(val);
                    } else if (key == "connected_slaves") {
                        info.number_of_slaves   = std::stoull(val);
                        info.number_of_replicas = std::stoull(val);
                    } else if (key == "total_commands_processed") {
                        info.total_commands_processed     = std::stoull(val);
                        info.number_of_commands_processed = std::stoull(val);
                    } else if (key == "total_connections_received") {
                        info.total_connections_received = std::stoull(val);
                    } else if (key == "instantaneous_ops_per_sec") {
                        info.instantaneous_ops_per_sec = std::stoull(val);
                    } else if (key == "total_net_input_bytes") {
                        info.total_net_input_bytes = std::stoull(val);
                    } else if (key == "total_net_output_bytes") {
                        info.total_net_output_bytes = std::stoull(val);
                    } else if (key == "instantaneous_input_kbps") {
                        info.instantaneous_input_kbps = std::stoull(val);
                    } else if (key == "instantaneous_output_kbps") {
                        info.instantaneous_output_kbps = std::stoull(val);
                    } else if (key.find("db") == 0 && key.length() > 2) {
                        // Extract number of keys from db0:keys=1,expires=0,avg_ttl=0
                        size_t key_pos = val.find("keys=");
                        if (key_pos != std::string::npos) {
                            size_t      comma_pos = val.find(',', key_pos);
                            std::string keys_count =
                                val.substr(key_pos + 5, comma_pos - (key_pos + 5));
                            info.number_of_keys += std::stoull(keys_count);
                        }

                        // Extract number of expires from db0:keys=1,expires=0,avg_ttl=0
                        size_t expires_pos = val.find("expires=");
                        if (expires_pos != std::string::npos) {
                            size_t      comma_pos     = val.find(',', expires_pos);
                            std::string expires_count = val.substr(
                                expires_pos + 8, comma_pos - (expires_pos + 8));
                            info.number_of_expires += std::stoull(expires_count);
                        }
                    }
                } catch (const std::exception &) {
                    // Ignore conversion errors
                }
            }
        }

        return info;
    }

public:
    // =============== Client Management Commands ===============

    /**
     * @brief Kills the client at the specified address
     *
     * @param addr Client address to kill
     * @param id Client ID to kill
     * @param type Client type to kill
     * @param skipme Whether to skip killing the current client
     * @return status object with the result
     */
    status
    client_kill(const std::string &addr = "", long long id = 0,
                const std::string &type = "", bool skipme = true) {
        std::vector<std::string> args;
        if (!addr.empty()) {
            args.push_back("ADDR");
            args.push_back(addr);
        }
        if (id != 0) {
            args.push_back("ID");
            args.push_back(std::to_string(id));
        }
        if (!type.empty()) {
            args.push_back("TYPE");
            args.push_back(type);
        }
        if (skipme) {
            args.push_back("SKIPME");
            args.push_back("yes");
        }
        return derived().template command<status>("CLIENT", "KILL", args).result();
    }

    /**
     * @brief Asynchronous version of client_kill
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @param addr Client address to kill
     * @param id Client ID to kill
     * @param type Client type to kill
     * @param skipme Whether to skip killing the current client
     * @return Reference to the derived class
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    client_kill(Func &&func, const std::string &addr = "", long long id = 0,
                const std::string &type = "", bool skipme = true) {
        std::vector<std::string> args;
        if (!addr.empty()) {
            args.push_back("ADDR");
            args.push_back(addr);
        }
        if (id != 0) {
            args.push_back("ID");
            args.push_back(std::to_string(id));
        }
        if (!type.empty()) {
            args.push_back("TYPE");
            args.push_back(type);
        }
        if (skipme) {
            args.push_back("SKIPME");
            args.push_back("yes");
        }
        return derived().template command<status>(std::forward<Func>(func), "CLIENT",
                                                  "KILL", args);
    }

    /**
     * @brief Gets the current client name
     *
     * @return Optional client name
     */
    std::optional<std::string>
    client_getname() {
        return derived()
            .template command<std::optional<std::string>>("CLIENT", "GETNAME")
            .result();
    }

    /**
     * @brief Asynchronous version of client_getname
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::string>> &&>,
                     Derived &>
    client_getname(Func &&func) {
        return derived().template command<std::optional<std::string>>(
            std::forward<Func>(func), "CLIENT", "GETNAME");
    }

    /**
     * @brief Sets the current client name
     *
     * @param name New client name
     * @return status object with the result
     */
    status
    client_setname(const std::string &name) {
        return derived().template command<status>("CLIENT", "SETNAME", name).result();
    }

    /**
     * @brief Asynchronous version of client_setname
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    client_setname(Func &&func, const std::string &name) {
        return derived().template command<status>(std::forward<Func>(func), "CLIENT",
                                                  "SETNAME", name);
    }

    /**
     * @brief Stops processing commands from clients for specified milliseconds
     *
     * @param timeout Time in milliseconds to block commands
     * @param mode Type of commands to block (WRITE or ALL)
     * @return status object with the result
     */
    status
    client_pause(long long timeout, const std::string &mode = "ALL") {
        return derived()
            .template command<status>("CLIENT", "PAUSE", timeout, mode)
            .result();
    }

    /**
     * @brief Asynchronous version of client_pause
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @param timeout Time in milliseconds to block commands
     * @param mode Type of commands to block (WRITE or ALL)
     * @return Reference to the derived class
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    client_pause(Func &&func, long long timeout, const std::string &mode = "ALL") {
        return derived().template command<status>(std::forward<Func>(func), "CLIENT",
                                                  "PAUSE", timeout, mode);
    }

    /**
     * @brief Enables or disables client tracking
     *
     * @param enabled Whether to enable tracking
     * @return status object with the result
     */
    status
    client_tracking(bool enabled = true) {
        return derived()
            .template command<status>("CLIENT", "TRACKING", enabled ? "ON" : "OFF")
            .result();
    }

    /**
     * @brief Asynchronous version of client_tracking
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    client_tracking(Func &&func, bool enabled = true) {
        return derived().template command<status>(std::forward<Func>(func), "CLIENT",
                                                  "TRACKING", enabled ? "ON" : "OFF");
    }

    /**
     * @brief Unblocks a client by ID
     *
     * @param client_id ID of client to unblock
     * @param error Whether to unblock with an error (default: false)
     * @return status object with the result
     */
    status
    client_unblock(long long client_id, bool error = false) {
        std::vector<std::string> args{"UNBLOCK", std::to_string(client_id)};
        if (error)
            return derived().template command<status>("CLIENT", args).result();

        args.push_back("ERROR");
        try {
            return derived().template command<status>("CLIENT", args).result();
        } catch (...) {
            return {};
        }
    }

    /**
     * @brief Asynchronous version of client_unblock
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @param client_id ID of client to unblock
     * @param error Whether to unblock with an error (default: false)
     * @return Reference to the derived class
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    client_unblock(Func &&func, long long client_id, bool error = false) {
        std::vector<std::string> args{"UNBLOCK", std::to_string(client_id)};
        if (error) {
            args.push_back("ERROR");
        }
        return derived().template command<status>(std::forward<Func>(func), "CLIENT",
                                                  args);
    }

    // =============== Configuration Commands ===============

    /**
     * @brief Gets a configuration parameter
     *
     * @param parameter Parameter name
     * @return Vector of parameter-value pairs
     * @see https://redis.io/commands/config-get
     */
    std::vector<std::pair<std::string, std::string>>
    config_get(const std::string &parameter) {
        auto result =
            derived()
                .template command<std::vector<std::string>>("CONFIG", "GET", parameter)
                .result();
        std::vector<std::pair<std::string, std::string>> pairs;
        for (size_t i = 0; i < result.size(); i += 2) {
            if (i + 1 < result.size()) {
                pairs.emplace_back(result[i], result[i + 1]);
            }
        }
        return pairs;
    }

    /**
     * @brief Asynchronous version of config_get
     *
     * @param func Callback function to handle the result
     * @param parameter Parameter name
     * @return Reference to the derived class
     * @see https://redis.io/commands/config-get
     */
    template <typename Func>
    std::enable_if_t<
        std::is_invocable_v<Func,
                            Reply<std::vector<std::pair<std::string, std::string>>> &&>,
        Derived &>
    config_get(Func &&func, const std::string &parameter) {
        return derived().template command<std::vector<std::string>>(
            [f = std::forward<Func>(func)](auto &&reply) mutable {
                Reply<std::vector<std::pair<std::string, std::string>>> r;
                r.ok() = reply.ok();
                if (reply.ok()) {
                    for (size_t i = 0; i < reply.result().size(); i += 2) {
                        if (i + 1 < reply.result().size()) {
                            r.result().emplace_back(reply.result()[i],
                                                    reply.result()[i + 1]);
                        }
                    }
                }
                f(std::move(r));
            },
            "CONFIG", "GET", parameter);
    }

    /**
     * @brief Sets a configuration parameter
     *
     * @param parameter Parameter name
     * @param value Parameter value
     * @return status object with the result
     */
    status
    config_set(const std::string &parameter, const std::string &value) {
        return derived()
            .template command<status>("CONFIG", "SET", parameter, value)
            .result();
    }

    /**
     * @brief Asynchronous version of config_set
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    config_set(Func &&func, const std::string &parameter, const std::string &value) {
        return derived().template command<status>(std::forward<Func>(func), "CONFIG",
                                                  "SET", parameter, value);
    }

    /**
     * @brief Resets server statistics
     *
     * @return status object with the result
     */
    status
    config_resetstat() {
        return derived().template command<status>("CONFIG", "RESETSTAT").result();
    }

    /**
     * @brief Asynchronous version of config_resetstat
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    config_resetstat(Func &&func) {
        return derived().template command<status>(std::forward<Func>(func), "CONFIG",
                                                  "RESETSTAT");
    }

    /**
     * @brief Rewrites the configuration file
     *
     * @return status object with the result
     */
    status
    config_rewrite() {
        return derived().template command<status>("CONFIG", "REWRITE").result();
    }

    /**
     * @brief Asynchronous version of config_rewrite
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    config_rewrite(Func &&func) {
        return derived().template command<status>(std::forward<Func>(func), "CONFIG",
                                                  "REWRITE");
    }

    // =============== Command Information Commands ===============

    /**
     * @brief Gets information about Redis commands
     *
     * @param command_names Optional list of command names to get info about
     * @return Vector of command information
     * @see https://redis.io/commands/command-info
     */
    std::vector<std::map<std::string, std::string>>
    command_info(const std::vector<std::string> &command_names = {}) {
        auto result = derived()
                          .template command<std::vector<std::vector<std::string>>>(
                              "COMMAND", "INFO", command_names)
                          .result();
        std::vector<std::map<std::string, std::string>> info;
        for (const auto &cmd : result) {
            std::map<std::string, std::string> cmd_info;
            for (size_t i = 0; i < cmd.size(); i += 2) {
                if (i + 1 < cmd.size()) {
                    cmd_info[cmd[i]] = cmd[i + 1];
                }
            }
            info.push_back(std::move(cmd_info));
        }
        return info;
    }

    /**
     * @brief Asynchronous version of command_info
     *
     * @param func Callback function to handle the result
     * @param command_names Optional list of command names to get info about
     * @return Reference to the derived class
     * @see https://redis.io/commands/command-info
     */
    template <typename Func>
    std::enable_if_t<
        std::is_invocable_v<Func,
                            Reply<std::vector<std::map<std::string, std::string>>> &&>,
        Derived &>
    command_info(Func &&func, const std::vector<std::string> &command_names = {}) {
        return derived().template command<std::vector<std::vector<std::string>>>(
            [f = std::forward<Func>(func)](auto &&reply) mutable {
                Reply<std::vector<std::map<std::string, std::string>>> r;
                r.ok() = reply.ok();
                if (reply.ok()) {
                    for (const auto &cmd : reply.result()) {
                        std::map<std::string, std::string> cmd_info;
                        for (size_t i = 0; i < cmd.size(); i += 2) {
                            if (i + 1 < cmd.size()) {
                                cmd_info[cmd[i]] = cmd[i + 1];
                            }
                        }
                        r.result().push_back(std::move(cmd_info));
                    }
                }
                f(std::move(r));
            },
            "COMMAND", "INFO", command_names);
    }

    /**
     * @brief Get the number of commands in the command list
     *
     * @return The number of commands
     */
    long long
    command_count() {
        return derived().template command<long long>("COMMAND", "COUNT").result();
    }

    /**
     * @brief Asynchronous version of command_count
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    command_count(Func &&func) {
        return derived().template command<long long>(std::forward<Func>(func), "COMMAND",
                                                     "COUNT");
    }

    /**
     * @brief Gets the keys from a command
     *
     * @param command Command to get keys from
     * @param args Command arguments
     * @return Vector of key names
     */
    std::vector<std::string>
    command_getkeys(const std::string &command, const std::vector<std::string> &args) {
        return derived()
            .template command<std::vector<std::string>>("COMMAND", "GETKEYS", command,
                                                        args)
            .result();
    }

    /**
     * @brief Asynchronous version of command_getkeys
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>,
                     Derived &>
    command_getkeys(Func &&func, const std::string &command,
                    const std::vector<std::string> &args) {
        return derived().template command<std::vector<std::string>>(
            std::forward<Func>(func), "COMMAND", "GETKEYS", command, args);
    }

    /**
     * @brief Gets information about Redis commands as JSON
     *
     * This command returns detailed information about all Redis commands
     * as a structured JSON object.
     *
     * @return Structured qb::json with command information
     * @see https://redis.io/commands/command
     */
    qb::json
    command() {
        return derived().template command<qb::json>("COMMAND").result();
    }

    /**
     * @brief Gets information about Redis commands as JSON
     *
     * This command returns detailed information about specific Redis commands
     * as a structured JSON object.
     *
     * @param command_names List of command names to get info about
     * @return Structured qb::json with command information
     * @see https://redis.io/commands/command
     */
    qb::json
    command(const std::vector<std::string> &command_names) {
        if (command_names.empty()) { // Should ideally not happen if called with this overload
            return command();
        }
        return derived().template command<qb::json>("COMMAND", "INFO", command_names).result();
    }

    /**
     * @brief Asynchronous version of command (all commands)
     *
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     * @see https://redis.io/commands/command
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::json> &&>, Derived &>
    command(Func &&func) {
        return derived().template command<qb::json>(std::forward<Func>(func), "COMMAND");
    }

    /**
     * @brief Asynchronous version of command (specific commands)
     *
     * @param func Callback function to handle the result
     * @param command_names List of command names to get info about
     * @return Reference to the derived class
     * @see https://redis.io/commands/command
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::json> &&>, Derived &>
    command(Func &&func, const std::vector<std::string> &command_names) {
        if (command_names.empty()) { // Should ideally not happen
            return command(std::forward<Func>(func));
        }
        return derived().template command<qb::json>(std::forward<Func>(func), "COMMAND", "INFO", command_names);
    }

    /**
     * @brief Gets statistics about command usage as JSON
     *
     * This command returns statistics about command usage as a structured JSON object.
     *
     * @return Structured qb::json with command statistics
     * @see https://redis.io/commands/command-stats
     */
    qb::json
    command_stats() {
        return derived().template command<qb::json>("COMMAND", "STATS").result();
    }

    /**
     * @brief Asynchronous version of command_stats
     *
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     * @see https://redis.io/commands/command-stats
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::json> &&>, Derived &>
    command_stats(Func &&func) {
        return derived().template command<qb::json>(std::forward<Func>(func), "COMMAND", "STATS");
    }

    // =============== Debug Commands ===============

    /**
     * @brief Gets debugging information about a key
     *
     * @param key Key to debug
     * @return Debug information
     * @see https://redis.io/commands/debug-object
     */
    std::string
    debug_object(const std::string &key) {
        return derived().template command<std::string>("DEBUG", "OBJECT", key).result();
    }

    /**
     * @brief Asynchronous version of debug_object
     *
     * @param func Callback function to handle the result
     * @param key Key to debug
     * @return Reference to the derived class
     * @see https://redis.io/commands/debug-object
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::string> &&>, Derived &>
    debug_object(Func &&func, const std::string &key) {
        return derived().template command<std::string>(std::forward<Func>(func), "DEBUG",
                                                       "OBJECT", key);
    }

    /**
     * @brief Forces a segfault (for testing)
     *
     * @return status object with the result
     */
    status
    debug_segfault() {
        return derived().template command<status>("DEBUG", "SEGFAULT").result();
    }

    /**
     * @brief Asynchronous version of debug_segfault
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    debug_segfault(Func &&func) {
        return derived().template command<status>(std::forward<Func>(func), "DEBUG",
                                                  "SEGFAULT");
    }

    /**
     * @brief Sleeps for a specified time (for testing)
     *
     * @param delay Delay in seconds
     * @return status object with the result
     */
    status
    debug_sleep(double delay) {
        return derived().template command<status>("DEBUG", "SLEEP", delay).result();
    }

    /**
     * @brief Asynchronous version of debug_sleep
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    debug_sleep(Func &&func, double delay) {
        return derived().template command<status>(std::forward<Func>(func), "DEBUG",
                                                  "SLEEP", delay);
    }

    // =============== Memory Commands ===============

    /**
     * @brief Gets a report about memory usage
     *
     * @return Memory report
     * @see https://redis.io/commands/memory-doctor
     */
    std::string
    memory_doctor() {
        return derived().template command<std::string>("MEMORY", "DOCTOR").result();
    }

    /**
     * @brief Asynchronous version of memory_doctor
     *
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     * @see https://redis.io/commands/memory-doctor
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::string> &&>, Derived &>
    memory_doctor(Func &&func) {
        return derived().template command<std::string>(std::forward<Func>(func),
                                                       "MEMORY", "DOCTOR");
    }

    /**
     * @brief Gets help about memory commands
     *
     * @return Help text
     */
    std::vector<std::string>
    memory_help() {
        return derived()
            .template command<std::vector<std::string>>("MEMORY", "HELP")
            .result();
    }

    /**
     * @brief Asynchronous version of memory_help
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>,
                     Derived &>
    memory_help(Func &&func) {
        return derived().template command<std::vector<std::string>>(
            std::forward<Func>(func), "MEMORY", "HELP");
    }

    /**
     * @brief Gets malloc statistics
     *
     * @return Malloc statistics
     */
    std::string
    memory_malloc_stats() {
        return derived()
            .template command<std::string>("MEMORY", "MALLOC-STATS")
            .result();
    }

    /**
     * @brief Asynchronous version of memory_malloc_stats
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::string> &&>, Derived &>
    memory_malloc_stats(Func &&func) {
        return derived().template command<std::string>(std::forward<Func>(func),
                                                       "MEMORY", "MALLOC-STATS");
    }

    /**
     * @brief Purges memory
     *
     * @return status object with the result
     */
    status
    memory_purge() {
        return derived().template command<status>("MEMORY", "PURGE").result();
    }

    /**
     * @brief Asynchronous version of memory_purge
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    memory_purge(Func &&func) {
        return derived().template command<status>(std::forward<Func>(func), "MEMORY",
                                                  "PURGE");
    }

    /**
     * @brief Gets memory usage of a key
     *
     * @param key Key to check
     * @param samples Number of samples for sampling
     * @return Memory usage in bytes
     */
    long long
    memory_usage(const std::string &key, long long samples = 0) {
        std::vector<std::string> args;
        if (samples > 0) {
            args.push_back("SAMPLES");
            args.push_back(std::to_string(samples));
        }
        return derived()
            .template command<long long>("MEMORY", "USAGE", key, args)
            .result();
    }

    /**
     * @brief Asynchronous version of memory_usage
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    memory_usage(Func &&func, const std::string &key, long long samples = 0) {
        std::vector<std::string> args;
        if (samples > 0) {
            args.push_back("SAMPLES");
            args.push_back(std::to_string(samples));
        }
        return derived().template command<long long>(std::forward<Func>(func), "MEMORY",
                                                     "USAGE", key, args);
    }

    // =============== Monitor Commands ===============

    /**
     * @brief Monitors Redis commands in real-time
     *
     * @param func Callback function to receive commands
     * @return Reference to the Redis handler for chaining
     * @see https://redis.io/commands/monitor
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::string> &&>, Derived &>
    monitor(Func &&func) {
        return derived().template command<std::string>(std::forward<Func>(func),
                                                       "MONITOR");
    }

    // =============== Role Commands ===============

    /**
     * @brief Gets the role of the server
     *
     * @return Server role information
     * @see https://redis.io/commands/role
     */
    std::vector<std::string>
    role() {
        return derived().template command<std::vector<std::string>>("ROLE").result();
    }

    /**
     * @brief Asynchronous version of role
     *
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     * @see https://redis.io/commands/role
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>,
                     Derived &>
    role(Func &&func) {
        return derived().template command<std::vector<std::string>>(
            std::forward<Func>(func), "ROLE");
    }

    // =============== Shutdown Commands ===============

    /**
     * @brief Synchronously saves the dataset to disk and then shuts down the server
     *
     * This command instructs the Redis server to stop accepting commands from clients
     * and save the dataset to disk before shutting down.
     *
     * @param save_option "SAVE" to save before shutdown, "NOSAVE" to not save
     * @return status object with the result
     */
    status
    shutdown(const std::string &save_option = "") {
        if (save_option.empty()) {
            return derived().template command<status>("SHUTDOWN").result();
        }
        return derived().template command<status>("SHUTDOWN", save_option).result();
    }

    /**
     * @brief Asynchronous version of shutdown
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @param save_option "SAVE" to save before shutdown, "NOSAVE" to not save
     * @return Reference to the derived class
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    shutdown(Func &&func, const std::string &save_option = "") {
        if (save_option.empty()) {
            return derived().template command<status>(std::forward<Func>(func),
                                                      "SHUTDOWN");
        }
        return derived().template command<status>(std::forward<Func>(func), "SHUTDOWN",
                                                  save_option);
    }

    // =============== Slave Commands ===============

    /**
     * @brief Configures replication
     *
     * @param host Master host
     * @param port Master port
     * @return status object with the result
     * @see https://redis.io/commands/slaveof
     */
    status
    slaveof(const std::string &host, long long port) {
        return derived().template command<status>("SLAVEOF", host, port).result();
    }

    /**
     * @brief Asynchronous version of slaveof
     *
     * @param func Callback function to handle the result
     * @param host Master host
     * @param port Master port
     * @return Reference to the derived class
     * @see https://redis.io/commands/slaveof
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    slaveof(Func &&func, const std::string &host, long long port) {
        return derived().template command<status>(std::forward<Func>(func), "SLAVEOF",
                                                  host, port);
    }

    // =============== Slowlog Commands ===============

    /**
     * @brief Gets the length of the slowlog
     *
     * @return Number of entries in the slowlog
     */
    long long
    slowlog_len() {
        return derived().template command<long long>("SLOWLOG", "LEN").result();
    }

    /**
     * @brief Asynchronous version of slowlog_len
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    slowlog_len(Func &&func) {
        return derived().template command<long long>(std::forward<Func>(func), "SLOWLOG",
                                                     "LEN");
    }

    /**
     * @brief Resets the slowlog
     *
     * @return status object with the result
     */
    status
    slowlog_reset() {
        return derived().template command<status>("SLOWLOG", "RESET").result();
    }

    /**
     * @brief Asynchronous version of slowlog_reset
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    slowlog_reset(Func &&func) {
        return derived().template command<status>(std::forward<Func>(func), "SLOWLOG",
                                                  "RESET");
    }

    // =============== Sync Commands ===============

    /**
     * @brief Synchronizes with a master
     *
     * @return status object with the result
     * @see https://redis.io/commands/sync
     */
    status
    sync() {
        return derived().template command<status>("SYNC").result();
    }

    /**
     * @brief Asynchronous version of sync
     *
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     * @see https://redis.io/commands/sync
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    sync(Func &&func) {
        return derived().template command<status>(std::forward<Func>(func), "SYNC");
    }

    /**
     * @brief Partially synchronizes with a master
     *
     * @param replication_id Replication ID
     * @param offset Replication offset
     * @return status object with the result
     */
    status
    psync(const std::string &replication_id, long long offset) {
        return derived()
            .template command<status>("PSYNC", replication_id, offset)
            .result();
    }

    /**
     * @brief Asynchronous version of psync
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    psync(Func &&func, const std::string &replication_id, long long offset) {
        return derived().template command<status>(std::forward<Func>(func), "PSYNC",
                                                  replication_id, offset);
    }

    // =============== Persistence Commands ===============

    /**
     * @brief Asynchronously rewrites the append-only file
     *
     * This command initiates an AOF rewrite operation in the background.
     * The Redis server will create a new AOF file while still serving clients.
     *
     * @return status object with the result
     */
    status
    bgrewriteaof() {
        return derived().template command<status>("BGREWRITEAOF").result();
    }

    /**
     * @brief Asynchronous version of bgrewriteaof
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    bgrewriteaof(Func &&func) {
        return derived().template command<status>(std::forward<Func>(func),
                                                  "BGREWRITEAOF");
    }

    /**
     * @brief Asynchronously saves the dataset to disk
     *
     * This command initiates a background save operation to persist the current
     * Redis dataset to disk.
     *
     * @param schedule Whether to schedule the save operation
     * @return status object with the result
     */
    status
    bgsave(bool schedule = false) {
        if (schedule) {
            return derived().template command<status>("BGSAVE", "SCHEDULE").result();
        }
        return derived().template command<status>("BGSAVE").result();
    }

    /**
     * @brief Asynchronous version of bgsave
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @param schedule Whether to schedule the save operation
     * @return Reference to the derived class
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    bgsave(Func &&func, bool schedule = false) {
        if (schedule) {
            return derived().template command<status>(std::forward<Func>(func), "BGSAVE",
                                                      "SCHEDULE");
        }
        return derived().template command<status>(std::forward<Func>(func), "BGSAVE");
    }

    /**
     * @brief Synchronously saves the dataset to disk
     *
     * This command initiates a synchronous save operation to persist the current
     * Redis dataset to disk, blocking the server until the operation completes.
     *
     * @return status object with the result
     */
    status
    save() {
        return derived().template command<status>("SAVE").result();
    }

    /**
     * @brief Asynchronous version of save
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    save(Func &&func) {
        return derived().template command<status>(std::forward<Func>(func), "SAVE");
    }

    /**
     * @brief Gets the Unix timestamp of the last successful save to disk
     *
     * This command returns the Unix timestamp of the last successful save to disk.
     * It can be used to check the last time when the dataset was saved either
     * by an explicit command or by a background save.
     *
     * @return Unix timestamp of the last successful save
     * @see https://redis.io/commands/lastsave
     */
    long long
    lastsave() {
        return derived().template command<long long>("LASTSAVE").result();
    }

    /**
     * @brief Asynchronous version of lastsave
     *
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     * @see https://redis.io/commands/lastsave
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    lastsave(Func &&func) {
        return derived().template command<long long>(std::forward<Func>(func),
                                                     "LASTSAVE");
    }

    // =============== Database Commands ===============

    /**
     * @brief Gets the number of keys in the current database
     *
     * @return Number of keys in the current database
     * @see https://redis.io/commands/dbsize
     */
    long long
    dbsize() {
        return derived().template command<long long>("DBSIZE").result();
    }

    /**
     * @brief Asynchronous version of dbsize
     *
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     * @see https://redis.io/commands/dbsize
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    dbsize(Func &&func) {
        return derived().template command<long long>(std::forward<Func>(func), "DBSIZE");
    }

    /**
     * @brief Removes all keys from all databases
     *
     * This command removes all keys from all existing databases, not just the
     * currently selected one.
     *
     * @param async Whether to perform the operation asynchronously on the server side
     * @return status object with the result
     */
    status
    flushall(bool async = false) {
        if (async) {
            return derived().template command<status>("FLUSHALL", "ASYNC").result();
        }
        return derived().template command<status>("FLUSHALL").result();
    }

    /**
     * @brief Asynchronous version of flushall
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @param async Whether to perform the operation asynchronously on the server side
     * @return Reference to the derived class
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    flushall(Func &&func, bool async = false) {
        if (async) {
            return derived().template command<status>(std::forward<Func>(func),
                                                      "FLUSHALL", "ASYNC");
        }
        return derived().template command<status>(std::forward<Func>(func), "FLUSHALL");
    }

    /**
     * @brief Removes all keys from the current database
     *
     * This command removes all keys from the currently selected database.
     *
     * @param async Whether to perform the operation asynchronously on the server side
     * @return status object with the result
     */
    status
    flushdb(bool async = false) {
        if (async) {
            return derived().template command<status>("FLUSHDB", "ASYNC").result();
        }
        return derived().template command<status>("FLUSHDB").result();
    }

    /**
     * @brief Asynchronous version of flushdb
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @param async Whether to perform the operation asynchronously on the server side
     * @return Reference to the derived class
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    flushdb(Func &&func, bool async = false) {
        if (async) {
            return derived().template command<status>(std::forward<Func>(func),
                                                      "FLUSHDB", "ASYNC");
        }
        return derived().template command<status>(std::forward<Func>(func), "FLUSHDB");
    }

    // =============== Server Information Commands ===============

    /**
     * @brief Gets information and statistics about the server as JSON
     *
     * This command returns information and statistics about the Redis server as a qb::json object,
     * preserving the full hierarchical structure of the information.
     *
     * @param section Optional section name to get specific information
     * @return Server information as a qb::json object
     * @see https://redis.io/commands/info
     */
    qb::json
    info(const std::string &section = "") {
        std::optional<std::string> param;
        if (!section.empty())
            param = section;

        return derived().template command<qb::json>("INFO", param).result();
    }

    /**
     * @brief Asynchronous version of info_json
     *
     * @param func Callback function to handle the result
     * @param section Optional section name to get specific information
     * @return Reference to the derived class
     * @see https://redis.io/commands/info
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::json> &&>, Derived &>
    info(Func &&func, const std::string &section = "") {
        std::optional<std::string> param;
        if (!section.empty())
            param = section;

        return derived().template command<qb::json>(std::forward<Func>(func), "INFO", param);
    }

    /**
     * @brief Gets the current server time
     *
     * This command returns the current server time as a pair of Unix timestamp
     * and the number of microseconds. This can be used for measuring performance
     * or for features that need high precision time measurements.
     *
     * @return Pair containing Unix timestamp and microseconds
     * @see https://redis.io/commands/time
     */
    std::pair<long long, long long>
    time() {
        auto res = derived().template command<std::vector<std::string>>("TIME");
        if (res.ok() && res.result().size() == 2)
            return std::make_pair(std::stoll(res.result()[0]),
                                  std::stoll(res.result()[1]));
        return std::make_pair(0, 0);
    }

    /**
     * @brief Asynchronous version of time
     *
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     * @see https://redis.io/commands/time
     */
    template <typename Func>
    std::enable_if_t<
        std::is_invocable_v<Func, Reply<std::pair<long long, long long>> &&>, Derived &>
    time(Func &&func) {
        return derived().template command<std::vector<std::string>>(
            [f = std::forward<Func>(func)](auto &&reply) mutable {
                Reply<std::pair<long long, long long>> r;
                r.ok() = reply.ok();
                if (reply.ok() && reply.result().size() == 2)
                    r.result() = std::make_pair(std::stoll(reply.result()[0]),
                                                std::stoll(reply.result()[1]));
                f(std::move(r));
            },
            "TIME");
    }

    /**
     * @brief Lists client connections as structured JSON
     *
     * This command returns information about client connections as a JSON array,
     * with each client connection represented as a JSON object with proper types.
     *
     * @return qb::json array of client information
     * @see https://redis.io/commands/client-list
     */
    qb::json
    client_list() {
        return derived().template command<qb::json>("CLIENT", "LIST").result();
    }

    /**
     * @brief Asynchronous version of client_list
     *
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     * @see https://redis.io/commands/client-list
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::json> &&>, Derived &>
    client_list(Func &&func) {
        return derived().template command<qb::json>(std::forward<Func>(func), "CLIENT", "LIST");
    }

    // =============== Latency Commands ===============

    /**
     * @brief Gets information about the latest latency events
     *
     * This command returns information about the latest latency spikes
     * experienced by Redis as a JSON array of events.
     *
     * @return qb::json array of latency events
     * @see https://redis.io/commands/latency-latest
     */
    qb::json
    latency_latest() {
        return derived().template command<qb::json>("LATENCY", "LATEST").result();
    }

    /**
     * @brief Asynchronous version of latency_latest
     *
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     * @see https://redis.io/commands/latency-latest
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::json> &&>, Derived &>
    latency_latest(Func &&func) {
        return derived().template command<qb::json>(std::forward<Func>(func), "LATENCY", "LATEST");
    }

    /**
     * @brief Gets latency history for an event
     *
     * Returns the latency history for a specific event as a JSON array.
     * Each entry represents a latency spike with its timestamp and duration.
     *
     * @param event The event name to get history for
     * @return qb::json array of latency history entries
     * @see https://redis.io/commands/latency-history
     */
    qb::json
    latency_history(const std::string &event) {
        return derived().template command<qb::json>("LATENCY", "HISTORY", event).result();
    }

    /**
     * @brief Asynchronous version of latency_history
     *
     * @param func Callback function to handle the result
     * @param event The event name to get history for
     * @return Reference to the derived class
     * @see https://redis.io/commands/latency-history
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::json> &&>, Derived &>
    latency_history(Func &&func, const std::string &event) {
        return derived().template command<qb::json>(std::forward<Func>(func), "LATENCY", "HISTORY", event);
    }
    
    /**
     * @brief Resets latency statistics
     *
     * @param event_name Optional specific event to reset (or all if empty)
     * @return status object with the result
     * @see https://redis.io/commands/latency-reset
     */
    status
    latency_reset(const std::string &event_name = "") {
        if (event_name.empty()) {
            return derived().template command<status>("LATENCY", "RESET").result();
        } else {
            return derived().template command<status>("LATENCY", "RESET", event_name).result();
        }
    }

    /**
     * @brief Asynchronous version of latency_reset
     *
     * @param func Callback function to handle the result
     * @param event_name Optional specific event to reset (or all if empty)
     * @return Reference to the derived class
     * @see https://redis.io/commands/latency-reset
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    latency_reset(Func &&func, const std::string &event_name = "") {
        if (event_name.empty()) {
            return derived().template command<status>(std::forward<Func>(func), "LATENCY", "RESET");
        } else {
            return derived().template command<status>(std::forward<Func>(func), "LATENCY", "RESET", event_name);
        }
    }

    /**
     * @brief Gets memory statistics as structured JSON
     *
     * This command returns detailed statistics about memory usage in Redis
     * as a structured JSON object.
     *
     * @return qb::json object with memory stats
     * @see https://redis.io/commands/memory-stats
     */
    qb::json
    memory_stats() {
        return derived().template command<qb::json>("MEMORY", "STATS").result();
    }

    /**
     * @brief Asynchronous version of memory_stats
     *
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     * @see https://redis.io/commands/memory-stats
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::json> &&>, Derived &>
    memory_stats(Func &&func) {
        return derived().template command<qb::json>(std::forward<Func>(func), "MEMORY", "STATS");
    }

    /**
     * @brief Gets slowlog entries as structured JSON
     *
     * This command returns the slow log entries with proper typing
     * and structure as a JSON array.
     *
     * @param count Number of entries to retrieve
     * @return qb::json array of slowlog entries
     * @see https://redis.io/commands/slowlog-get
     */
    qb::json
    slowlog_get(long long count = 10) {
        return derived().template command<qb::json>("SLOWLOG", "GET", count).result();
    }

    /**
     * @brief Asynchronous version of slowlog_get
     *
     * @param func Callback function to handle the result
     * @param count Number of entries to retrieve
     * @return Reference to the derived class
     * @see https://redis.io/commands/slowlog-get
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::json> &&>, Derived &>
    slowlog_get(Func &&func, long long count = 10) {
        return derived().template command<qb::json>(std::forward<Func>(func), "SLOWLOG", "GET", count);
    }

    /**
     * @brief Gets information about client tracking as JSON
     *
     * This command returns detailed information about the client tracking system
     * as a structured JSON object.
     *
     * @return qb::json object with client tracking information
     * @see https://redis.io/commands/client-tracking-info
     */
    qb::json
    client_tracking_info() {
        return derived().template command<qb::json>("CLIENT", "TRACKING", "INFO").result();
    }

    /**
     * @brief Asynchronous version of client_tracking_info
     *
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     * @see https://redis.io/commands/client-tracking-info
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::json> &&>, Derived &>
    client_tracking_info(Func &&func) {
        return derived().template command<qb::json>(std::forward<Func>(func), "CLIENT", "TRACKING", "INFO");
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_SERVER_COMMANDS_H
