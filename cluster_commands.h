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

#ifndef QBM_REDIS_CLUSTER_COMMANDS_H
#define QBM_REDIS_CLUSTER_COMMANDS_H
#include "reply.h"

namespace qb::redis {

/**
 * @class cluster_commands
 * @brief Provides Redis cluster command implementations.
 *
 * This class implements Redis commands for working with Redis clusters,
 * including operations for retrieving information about the cluster topology
 * and managing cluster nodes.
 *
 * @tparam Derived The derived class type (CRTP pattern)
 */
template <typename Derived>
class cluster_commands {
private:
    constexpr Derived &
    derived() {
        return static_cast<Derived &>(*this);
    }

public:
    /**
     * @brief Get information about the Redis cluster (coroutine awaitable)
     *
     * Returns general information about the cluster as a JSON object.
     * The information includes the current state, size, statistics about
     * the communication between nodes, and more.
     *
     * @return redis_awaiter yielding Reply<qb::json>
     * @see https://redis.io/commands/cluster-info
     */
    auto cluster_info() {
        return derived().template make_coro_command<qb::json>(
            [this](auto&& callback) {
                this->cluster_info(std::move(callback));
            }
        );
    }

    /**
     * @brief Asynchronous version of cluster_info
     *
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     * @see https://redis.io/commands/cluster-info
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::json> &&>, Derived &>
    cluster_info(Func &&func) {
        return derived().template command<qb::json>(std::forward<Func>(func), "CLUSTER", "INFO");
    }

    /**
     * @brief Get information about all cluster nodes (coroutine awaitable)
     *
     * Returns information about all cluster nodes as a structured JSON object.
     * The information includes the node ID, IP address and port, flags, master ID if
     * the node is a replica, ping and pong timestamps, and more.
     *
     * @return redis_awaiter yielding Reply<qb::json>
     * @see https://redis.io/commands/cluster-nodes
     */
    auto cluster_nodes() {
        return derived().template make_coro_command<qb::json>(
            [this](auto&& callback) {
                this->cluster_nodes(std::move(callback));
            }
        );
    }

    /**
     * @brief Asynchronous version of cluster_nodes
     *
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     * @see https://redis.io/commands/cluster-nodes
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::json> &&>, Derived &>
    cluster_nodes(Func &&func) {
        return derived().template command<qb::json>(std::forward<Func>(func), "CLUSTER", "NODES");
    }

    /**
     * @brief Get mapping of hash slots to nodes (coroutine awaitable)
     *
     * Returns a mapping of hash slots to nodes as a structured JSON object.
     * This information is used for determining which nodes are responsible for
     * which hash slots.
     *
     * @return redis_awaiter yielding Reply<qb::json>
     * @see https://redis.io/commands/cluster-slots
     */
    auto cluster_slots() {
        return derived().template make_coro_command<qb::json>(
            [this](auto&& callback) {
                this->cluster_slots(std::move(callback));
            }
        );
    }

    /**
     * @brief Asynchronous version of cluster_slots
     *
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     * @see https://redis.io/commands/cluster-slots
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::json> &&>, Derived &>
    cluster_slots(Func &&func) {
        return derived().template command<qb::json>(std::forward<Func>(func), "CLUSTER", "SLOTS");
    }

    /**
     * @brief Force a node to join the cluster (coroutine awaitable)
     *
     * Forces a Redis node to join the cluster by connecting to the specified node.
     *
     * @param ip IP address of the node to connect to
     * @param port Port of the node to connect to
     * @return redis_awaiter yielding Reply<status>
     * @see https://redis.io/commands/cluster-meet
     */
    auto cluster_meet(const std::string &ip, int port) {
        return derived().template make_coro_command<status>(
            [this, ip, port](auto&& callback) {
                this->cluster_meet(std::move(callback), ip, port);
            }
        );
    }

    /**
     * @brief Asynchronous version of cluster_meet
     *
     * @param func Callback function to handle the result
     * @param ip IP address of the node to connect to
     * @param port Port of the node to connect to
     * @return Reference to the derived class
     * @see https://redis.io/commands/cluster-meet
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    cluster_meet(Func &&func, const std::string &ip, int port) {
        return derived().template command<status>(std::forward<Func>(func), "CLUSTER", "MEET", ip, port);
    }

    /**
     * @brief Remove a node from the cluster (coroutine awaitable)
     *
     * Removes a node from the nodes table by ID.
     *
     * @param node_id ID of the node to remove
     * @return redis_awaiter yielding Reply<status>
     * @see https://redis.io/commands/cluster-forget
     */
    auto cluster_forget(const std::string &node_id) {
        return derived().template make_coro_command<status>(
            [this, node_id](auto&& callback) {
                this->cluster_forget(std::move(callback), node_id);
            }
        );
    }

    /**
     * @brief Asynchronous version of cluster_forget
     *
     * @param func Callback function to handle the result
     * @param node_id ID of the node to remove
     * @return Reference to the derived class
     * @see https://redis.io/commands/cluster-forget
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    cluster_forget(Func &&func, const std::string &node_id) {
        return derived().template command<status>(std::forward<Func>(func), "CLUSTER", "FORGET", node_id);
    }

    /**
     * @brief Reset a Redis cluster node (coroutine awaitable)
     *
     * Resets a Redis cluster node, making it forget all previously associated nodes
     * and assigned slots.
     *
     * @param mode Optional reset mode, "HARD" or "SOFT" (default is "SOFT")
     * @return redis_awaiter yielding Reply<status>
     * @see https://redis.io/commands/cluster-reset
     */
    auto cluster_reset(const std::string &mode = "SOFT") {
        return derived().template make_coro_command<status>(
            [this, mode](auto&& callback) {
                this->cluster_reset(std::move(callback), mode);
            }
        );
    }

    /**
     * @brief Asynchronous version of cluster_reset
     *
     * @param func Callback function to handle the result
     * @param mode Optional reset mode, "HARD" or "SOFT" (default is "SOFT")
     * @return Reference to the derived class
     * @see https://redis.io/commands/cluster-reset
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    cluster_reset(Func &&func, const std::string &mode = "SOFT") {
        return derived().template command<status>(std::forward<Func>(func), "CLUSTER", "RESET", mode);
    }

    /**
     * @brief Forces a replica to initiate a manual failover (coroutine awaitable)
     *
     * Forces a replica to perform a manual failover of its master, taking over
     * its hash slots.
     *
     * @param option Optional failover mode: "FORCE", "TAKEOVER", or none for default behavior
     * @return redis_awaiter yielding Reply<status>
     * @see https://redis.io/commands/cluster-failover
     */
    auto cluster_failover(const std::string &option = "") {
        return derived().template make_coro_command<status>(
            [this, option](auto&& callback) {
                this->cluster_failover(std::move(callback), option);
            }
        );
    }

    /**
     * @brief Asynchronous version of cluster_failover
     *
     * @param func Callback function to handle the result
     * @param option Optional failover mode: "FORCE", "TAKEOVER", or none for default behavior
     * @return Reference to the derived class
     * @see https://redis.io/commands/cluster-failover
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    cluster_failover(Func &&func, const std::string &option = "") {
        if (option.empty()) {
            return derived().template command<status>(std::forward<Func>(func), "CLUSTER", "FAILOVER");
        }
        return derived().template command<status>(std::forward<Func>(func), "CLUSTER", "FAILOVER", option);
    }

    /**
     * @brief Reconfigure a node as a replica of another node (coroutine awaitable)
     *
     * Reconfigures a node to be a replica of the specified master node.
     *
     * @param node_id ID of the master node
     * @return redis_awaiter yielding Reply<status>
     * @see https://redis.io/commands/cluster-replicate
     */
    auto cluster_replicate(const std::string &node_id) {
        return derived().template make_coro_command<status>(
            [this, node_id](auto&& callback) {
                this->cluster_replicate(std::move(callback), node_id);
            }
        );
    }

    /**
     * @brief Asynchronous version of cluster_replicate
     *
     * @param func Callback function to handle the result
     * @param node_id ID of the master node
     * @return Reference to the derived class
     * @see https://redis.io/commands/cluster-replicate
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    cluster_replicate(Func &&func, const std::string &node_id) {
        return derived().template command<status>(std::forward<Func>(func), "CLUSTER", "REPLICATE", node_id);
    }

    /**
     * @brief Save the cluster configuration to disk (coroutine awaitable)
     *
     * Forces the node to save the cluster configuration to disk.
     *
     * @return redis_awaiter yielding Reply<status>
     * @see https://redis.io/commands/cluster-saveconfig
     */
    auto cluster_saveconfig() {
        return derived().template make_coro_command<status>(
            [this](auto&& callback) {
                this->cluster_saveconfig(std::move(callback));
            }
        );
    }

    /**
     * @brief Asynchronous version of cluster_saveconfig
     *
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     * @see https://redis.io/commands/cluster-saveconfig
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    cluster_saveconfig(Func &&func) {
        return derived().template command<status>(std::forward<Func>(func), "CLUSTER", "SAVECONFIG");
    }

    /**
     * @brief Set the configuration epoch for a node (coroutine awaitable)
     *
     * Sets the configuration epoch for a node.
     *
     * @param epoch Configuration epoch to set
     * @return redis_awaiter yielding Reply<status>
     * @see https://redis.io/commands/cluster-set-config-epoch
     */
    auto cluster_set_config_epoch(long long epoch) {
        return derived().template make_coro_command<status>(
            [this, epoch](auto&& callback) {
                this->cluster_set_config_epoch(std::move(callback), epoch);
            }
        );
    }

    /**
     * @brief Asynchronous version of cluster_set_config_epoch
     *
     * @param func Callback function to handle the result
     * @param epoch Configuration epoch to set
     * @return Reference to the derived class
     * @see https://redis.io/commands/cluster-set-config-epoch
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    cluster_set_config_epoch(Func &&func, long long epoch) {
        return derived().template command<status>(std::forward<Func>(func), "CLUSTER", "SET-CONFIG-EPOCH", epoch);
    }

    /**
     * @brief Advance the cluster config epoch (coroutine awaitable)
     *
     * Increments the cluster configuration epoch.
     *
     * @return redis_awaiter yielding Reply<status>
     * @see https://redis.io/commands/cluster-bumpepoch
     */
    auto cluster_bumpepoch() {
        return derived().template make_coro_command<status>(
            [this](auto&& callback) {
                this->cluster_bumpepoch(std::move(callback));
            }
        );
    }

    /**
     * @brief Asynchronous version of cluster_bumpepoch
     *
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     * @see https://redis.io/commands/cluster-bumpepoch
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    cluster_bumpepoch(Func &&func) {
        return derived().template command<status>(std::forward<Func>(func), "CLUSTER", "BUMPEPOCH");
    }

    /**
     * @brief Get the ID of the current node (coroutine awaitable)
     *
     * Returns the ID of the Redis node, as a 40 character string.
     *
     * @return redis_awaiter yielding Reply<std::string>
     * @see https://redis.io/commands/cluster-myid
     */
    auto cluster_myid() {
        return derived().template make_coro_command<std::string>(
            [this](auto&& callback) {
                this->cluster_myid(std::move(callback));
            }
        );
    }

    /**
     * @brief Asynchronous version of cluster_myid
     *
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     * @see https://redis.io/commands/cluster-myid
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::string> &&>, Derived &>
    cluster_myid(Func &&func) {
        return derived().template command<std::string>(std::forward<Func>(func), "CLUSTER", "MYID");
    }

    /**
     * @brief Get the hash slot for a key (coroutine awaitable)
     *
     * Returns the hash slot number for the specified key.
     *
     * @param key The key to get the hash slot for
     * @return redis_awaiter yielding Reply<long long>
     * @see https://redis.io/commands/cluster-keyslot
     */
    auto cluster_keyslot(const std::string &key) {
        return derived().template make_coro_command<long long>(
            [this, key](auto&& callback) {
                this->cluster_keyslot(std::move(callback), key);
            }
        );
    }

    /**
     * @brief Asynchronous version of cluster_keyslot
     *
     * @param func Callback function to handle the result
     * @param key The key to get the hash slot for
     * @return Reference to the derived class
     * @see https://redis.io/commands/cluster-keyslot
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    cluster_keyslot(Func &&func, const std::string &key) {
        return derived().template command<long long>(std::forward<Func>(func), "CLUSTER", "KEYSLOT", key);
    }

    /**
     * @brief Count the number of keys in a hash slot (coroutine awaitable)
     *
     * Returns the number of keys in the specified hash slot.
     *
     * @param slot The hash slot to count keys for
     * @return redis_awaiter yielding Reply<long long>
     * @see https://redis.io/commands/cluster-countkeysinslot
     */
    auto cluster_countkeysinslot(int slot) {
        return derived().template make_coro_command<long long>(
            [this, slot](auto&& callback) {
                this->cluster_countkeysinslot(std::move(callback), slot);
            }
        );
    }

    /**
     * @brief Asynchronous version of cluster_countkeysinslot
     *
     * @param func Callback function to handle the result
     * @param slot The hash slot to count keys for
     * @return Reference to the derived class
     * @see https://redis.io/commands/cluster-countkeysinslot
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    cluster_countkeysinslot(Func &&func, int slot) {
        return derived().template command<long long>(std::forward<Func>(func), "CLUSTER", "COUNTKEYSINSLOT", slot);
    }

    /**
     * @brief Get keys in a hash slot (coroutine awaitable)
     *
     * Returns a list of keys in the specified hash slot.
     *
     * @param slot The hash slot to get keys from
     * @param count Maximum number of keys to return
     * @return redis_awaiter yielding Reply<std::vector<std::string>>
     * @see https://redis.io/commands/cluster-getkeysinslot
     */
    auto cluster_getkeysinslot(int slot, int count) {
        return derived().template make_coro_command<std::vector<std::string>>(
            [this, slot, count](auto&& callback) {
                this->cluster_getkeysinslot(std::move(callback), slot, count);
            }
        );
    }

    /**
     * @brief Asynchronous version of cluster_getkeysinslot
     *
     * @param func Callback function to handle the result
     * @param slot The hash slot to get keys from
     * @param count Maximum number of keys to return
     * @return Reference to the derived class
     * @see https://redis.io/commands/cluster-getkeysinslot
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>, Derived &>
    cluster_getkeysinslot(Func &&func, int slot, int count) {
        return derived().template command<std::vector<std::string>>(std::forward<Func>(func), "CLUSTER", "GETKEYSINSLOT", slot, count);
    }

    // =============== New Cluster Commands (TODO_COMMANDS.md) ===============

    /**
     * @brief Marks the connection to accept ASK redirections (coroutine awaitable)
     *
     * @return redis_awaiter yielding Reply<status>
     * @see https://redis.io/commands/asking
     */
    auto asking() {
        return derived().template make_coro_command<status>(
            [this](auto&& callback) { this->asking(std::move(callback)); });
    }

    /**
     * @brief Asynchronous version of asking
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     * @see https://redis.io/commands/asking
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    asking(Func &&func) {
        return derived().template command<status>(std::forward<Func>(func), "ASKING");
    }

    /**
     * @brief Enables read-only mode for cluster replicas (coroutine awaitable)
     *
     * @return redis_awaiter yielding Reply<status>
     * @see https://redis.io/commands/readonly
     */
    auto readonly() {
        return derived().template make_coro_command<status>(
            [this](auto&& callback) { this->readonly(std::move(callback)); });
    }

    /**
     * @brief Asynchronous version of readonly
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     * @see https://redis.io/commands/readonly
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    readonly(Func &&func) {
        return derived().template command<status>(std::forward<Func>(func), "READONLY");
    }

    /**
     * @brief Disables read-only mode for cluster replicas (coroutine awaitable)
     *
     * @return redis_awaiter yielding Reply<status>
     * @see https://redis.io/commands/readwrite
     */
    auto readwrite() {
        return derived().template make_coro_command<status>(
            [this](auto&& callback) { this->readwrite(std::move(callback)); });
    }

    /**
     * @brief Asynchronous version of readwrite
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     * @see https://redis.io/commands/readwrite
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    readwrite(Func &&func) {
        return derived().template command<status>(std::forward<Func>(func), "READWRITE");
    }

    /**
     * @brief Assigns slots to the receiving node (coroutine awaitable)
     *
     * @tparam Slots Slot numbers to add
     * @param slots Slot numbers
     * @return redis_awaiter yielding Reply<status>
     * @see https://redis.io/commands/cluster-addslots
     */
    template <typename... Slots>
    auto cluster_addslots(Slots &&...slots) {
        return derived().template make_coro_command<status>(
            [this, ...slots = std::forward<Slots>(slots)](auto&& callback) mutable {
                this->cluster_addslots(std::move(callback), std::forward<Slots>(slots)...);
            }
        );
    }

    /**
     * @brief Asynchronous version of cluster_addslots
     *
     * @tparam Func Callback function type
     * @tparam Slots Slot number types
     * @param func Callback function to handle the result
     * @param slots Slot numbers to add
     * @return Reference to the derived class
     * @see https://redis.io/commands/cluster-addslots
     */
    template <typename Func, typename... Slots>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    cluster_addslots(Func &&func, Slots &&...slots) {
        return derived().template command<status>(std::forward<Func>(func), "CLUSTER",
                                                  "ADDSLOTS", std::forward<Slots>(slots)...);
    }

    /**
     * @brief Assigns slot ranges to the receiving node (coroutine awaitable)
     *
     * @param ranges Vector of (start, end) slot range pairs
     * @return redis_awaiter yielding Reply<status>
     * @see https://redis.io/commands/cluster-addslotsrange
     */
    auto cluster_addslotsrange(const std::vector<std::pair<int, int>> &ranges) {
        return derived().template make_coro_command<status>(
            [this, ranges](auto&& callback) {
                this->cluster_addslotsrange(std::move(callback), ranges);
            }
        );
    }

    /**
     * @brief Asynchronous version of cluster_addslotsrange
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @param ranges Vector of (start, end) slot range pairs
     * @return Reference to the derived class
     * @see https://redis.io/commands/cluster-addslotsrange
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    cluster_addslotsrange(Func &&func,
                          const std::vector<std::pair<int, int>> &ranges) {
        std::vector<std::string> args;
        for (const auto &r : ranges) {
            args.push_back(std::to_string(r.first));
            args.push_back(std::to_string(r.second));
        }
        return derived().template command<status>(
            std::forward<Func>(func), "CLUSTER", "ADDSLOTSRANGE", args);
    }

    /**
     * @brief Returns the number of failure reports for a node (coroutine awaitable)
     *
     * @param node_id ID of the cluster node
     * @return redis_awaiter yielding Reply<long long>
     * @see https://redis.io/commands/cluster-count-failure-reports
     */
    auto cluster_count_failure_reports(const std::string &node_id) {
        return derived().template make_coro_command<long long>(
            [this, node_id](auto&& callback) {
                this->cluster_count_failure_reports(std::move(callback), node_id);
            }
        );
    }

    /**
     * @brief Asynchronous version of cluster_count_failure_reports
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @param node_id ID of the cluster node
     * @return Reference to the derived class
     * @see https://redis.io/commands/cluster-count-failure-reports
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    cluster_count_failure_reports(Func &&func, const std::string &node_id) {
        return derived().template command<long long>(
            std::forward<Func>(func), "CLUSTER", "COUNT-FAILURE-REPORTS", node_id);
    }

    /**
     * @brief Removes slots from the receiving node (coroutine awaitable)
     *
     * @tparam Slots Slot numbers to remove
     * @param slots Slot numbers
     * @return redis_awaiter yielding Reply<status>
     * @see https://redis.io/commands/cluster-delslots
     */
    template <typename... Slots>
    auto cluster_delslots(Slots &&...slots) {
        return derived().template make_coro_command<status>(
            [this, ...slots = std::forward<Slots>(slots)](auto&& callback) mutable {
                this->cluster_delslots(std::move(callback), std::forward<Slots>(slots)...);
            }
        );
    }

    /**
     * @brief Asynchronous version of cluster_delslots
     *
     * @tparam Func Callback function type
     * @tparam Slots Slot number types
     * @param func Callback function to handle the result
     * @param slots Slot numbers to remove
     * @return Reference to the derived class
     * @see https://redis.io/commands/cluster-delslots
     */
    template <typename Func, typename... Slots>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    cluster_delslots(Func &&func, Slots &&...slots) {
        return derived().template command<status>(std::forward<Func>(func), "CLUSTER",
                                                  "DELSLOTS", std::forward<Slots>(slots)...);
    }

    /**
     * @brief Removes slot ranges from the receiving node (coroutine awaitable)
     *
     * @param ranges Vector of (start, end) slot range pairs
     * @return redis_awaiter yielding Reply<status>
     * @see https://redis.io/commands/cluster-delslotsrange
     */
    auto cluster_delslotsrange(const std::vector<std::pair<int, int>> &ranges) {
        return derived().template make_coro_command<status>(
            [this, ranges](auto&& callback) {
                this->cluster_delslotsrange(std::move(callback), ranges);
            }
        );
    }

    /**
     * @brief Asynchronous version of cluster_delslotsrange
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @param ranges Vector of (start, end) slot range pairs
     * @return Reference to the derived class
     * @see https://redis.io/commands/cluster-delslotsrange
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    cluster_delslotsrange(Func &&func,
                          const std::vector<std::pair<int, int>> &ranges) {
        std::vector<std::string> args;
        for (const auto &r : ranges) {
            args.push_back(std::to_string(r.first));
            args.push_back(std::to_string(r.second));
        }
        return derived().template command<status>(
            std::forward<Func>(func), "CLUSTER", "DELSLOTSRANGE", args);
    }

    /**
     * @brief Deletes all slots from the receiving node (coroutine awaitable)
     *
     * @return redis_awaiter yielding Reply<status>
     * @see https://redis.io/commands/cluster-flushslots
     */
    auto cluster_flushslots() {
        return derived().template make_coro_command<status>(
            [this](auto&& callback) { this->cluster_flushslots(std::move(callback)); });
    }

    /**
     * @brief Asynchronous version of cluster_flushslots
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     * @see https://redis.io/commands/cluster-flushslots
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    cluster_flushslots(Func &&func) {
        return derived().template command<status>(
            std::forward<Func>(func), "CLUSTER", "FLUSHSLOTS");
    }

    /**
     * @brief Returns a list of cluster links (coroutine awaitable)
     *
     * @return redis_awaiter yielding Reply<qb::json>
     * @see https://redis.io/commands/cluster-links
     */
    auto cluster_links() {
        return derived().template make_coro_command<qb::json>(
            [this](auto&& callback) { this->cluster_links(std::move(callback)); });
    }

    /**
     * @brief Asynchronous version of cluster_links
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     * @see https://redis.io/commands/cluster-links
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::json> &&>, Derived &>
    cluster_links(Func &&func) {
        return derived().template command<qb::json>(
            std::forward<Func>(func), "CLUSTER", "LINKS");
    }

    /**
     * @brief Returns the shard ID of the current node (coroutine awaitable)
     *
     * @return redis_awaiter yielding Reply<std::string>
     * @see https://redis.io/commands/cluster-myshardid
     */
    auto cluster_myshardid() {
        return derived().template make_coro_command<std::string>(
            [this](auto&& callback) { this->cluster_myshardid(std::move(callback)); });
    }

    /**
     * @brief Asynchronous version of cluster_myshardid
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     * @see https://redis.io/commands/cluster-myshardid
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::string> &&>, Derived &>
    cluster_myshardid(Func &&func) {
        return derived().template command<std::string>(
            std::forward<Func>(func), "CLUSTER", "MYSHARDID");
    }

    /**
     * @brief Returns the list of replica nodes for a given node (coroutine awaitable)
     *
     * @param node_id ID of the master node
     * @return redis_awaiter yielding Reply<qb::json>
     * @see https://redis.io/commands/cluster-replicas
     */
    auto cluster_replicas(const std::string &node_id) {
        return derived().template make_coro_command<qb::json>(
            [this, node_id](auto&& callback) {
                this->cluster_replicas(std::move(callback), node_id);
            }
        );
    }

    /**
     * @brief Asynchronous version of cluster_replicas
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @param node_id ID of the master node
     * @return Reference to the derived class
     * @see https://redis.io/commands/cluster-replicas
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::json> &&>, Derived &>
    cluster_replicas(Func &&func, const std::string &node_id) {
        return derived().template command<qb::json>(
            std::forward<Func>(func), "CLUSTER", "REPLICAS", node_id);
    }

    /**
     * @brief Sets the state of a hash slot (coroutine awaitable)
     *
     * @param slot Hash slot number
     * @param subcommand Subcommand: MIGRATING, IMPORTING, NODE, STABLE
     * @param node_id Node ID (required for NODE subcommand)
     * @return redis_awaiter yielding Reply<status>
     * @see https://redis.io/commands/cluster-setslot
     */
    auto cluster_setslot(int slot, const std::string &subcommand,
                        const std::string &node_id = "") {
        return derived().template make_coro_command<status>(
            [this, slot, subcommand, node_id](auto&& callback) {
                this->cluster_setslot(std::move(callback), slot, subcommand, node_id);
            }
        );
    }

    /**
     * @brief Asynchronous version of cluster_setslot
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @param slot Hash slot number
     * @param subcommand Subcommand
     * @param node_id Node ID (optional)
     * @return Reference to the derived class
     * @see https://redis.io/commands/cluster-setslot
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    cluster_setslot(Func &&func, int slot, const std::string &subcommand,
                    const std::string &node_id = "") {
        if (node_id.empty()) {
            return derived().template command<status>(
                std::forward<Func>(func), "CLUSTER", "SETSLOT", slot, subcommand);
        }
        return derived().template command<status>(
            std::forward<Func>(func), "CLUSTER", "SETSLOT", slot, subcommand, node_id);
    }

    /**
     * @brief Returns details about the cluster shards (coroutine awaitable)
     *
     * @return redis_awaiter yielding Reply<qb::json>
     * @see https://redis.io/commands/cluster-shards
     */
    auto cluster_shards() {
        return derived().template make_coro_command<qb::json>(
            [this](auto&& callback) { this->cluster_shards(std::move(callback)); });
    }

    /**
     * @brief Asynchronous version of cluster_shards
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     * @see https://redis.io/commands/cluster-shards
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::json> &&>, Derived &>
    cluster_shards(Func &&func) {
        return derived().template command<qb::json>(
            std::forward<Func>(func), "CLUSTER", "SHARDS");
    }

    /**
     * @brief Returns the list of replica nodes (deprecated, use cluster_replicas) (coroutine awaitable)
     *
     * @param node_id ID of the master node
     * @return redis_awaiter yielding Reply<qb::json>
     * @see https://redis.io/commands/cluster-slaves
     */
    auto cluster_slaves(const std::string &node_id) {
        return derived().template make_coro_command<qb::json>(
            [this, node_id](auto&& callback) {
                this->cluster_slaves(std::move(callback), node_id);
            }
        );
    }

    /**
     * @brief Asynchronous version of cluster_slaves
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @param node_id ID of the master node
     * @return Reference to the derived class
     * @see https://redis.io/commands/cluster-slaves
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::json> &&>, Derived &>
    cluster_slaves(Func &&func, const std::string &node_id) {
        return derived().template command<qb::json>(
            std::forward<Func>(func), "CLUSTER", "SLAVES", node_id);
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_CLUSTER_COMMANDS_H
