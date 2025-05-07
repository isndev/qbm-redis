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
     * @brief Get information about the Redis cluster
     *
     * Returns general information about the cluster as a JSON object.
     * The information includes the current state, size, statistics about
     * the communication between nodes, and more.
     *
     * @return qb::json with cluster information
     * @see https://redis.io/commands/cluster-info
     */
    qb::json
    cluster_info() {
        return derived().template command<qb::json>("CLUSTER", "INFO").result();
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
     * @brief Get information about all cluster nodes
     *
     * Returns information about all cluster nodes as a structured JSON object.
     * The information includes the node ID, IP address and port, flags, master ID if
     * the node is a replica, ping and pong timestamps, and more.
     *
     * @return qb::json with node information
     * @see https://redis.io/commands/cluster-nodes
     */
    qb::json
    cluster_nodes() {
        return derived().template command<qb::json>("CLUSTER", "NODES").result();
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
     * @brief Get mapping of hash slots to nodes
     *
     * Returns a mapping of hash slots to nodes as a structured JSON object.
     * This information is used for determining which nodes are responsible for
     * which hash slots.
     *
     * @return qb::json with hash slot mapping
     * @see https://redis.io/commands/cluster-slots
     */
    qb::json
    cluster_slots() {
        return derived().template command<qb::json>("CLUSTER", "SLOTS").result();
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
     * @brief Force a node to join the cluster
     *
     * Forces a Redis node to join the cluster by connecting to the specified node.
     *
     * @param ip IP address of the node to connect to
     * @param port Port of the node to connect to
     * @return status Success/failure status
     * @see https://redis.io/commands/cluster-meet
     */
    status
    cluster_meet(const std::string &ip, int port) {
        return derived().template command<status>("CLUSTER", "MEET", ip, port).result();
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
     * @brief Remove a node from the cluster
     *
     * Removes a node from the nodes table by ID.
     *
     * @param node_id ID of the node to remove
     * @return status Success/failure status
     * @see https://redis.io/commands/cluster-forget
     */
    status
    cluster_forget(const std::string &node_id) {
        return derived().template command<status>("CLUSTER", "FORGET", node_id).result();
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
     * @brief Reset a Redis cluster node
     *
     * Resets a Redis cluster node, making it forget all previously associated nodes
     * and assigned slots.
     *
     * @param mode Optional reset mode, "HARD" or "SOFT" (default is "SOFT")
     * @return status Success/failure status
     * @see https://redis.io/commands/cluster-reset
     */
    status
    cluster_reset(const std::string &mode = "SOFT") {
        return derived().template command<status>("CLUSTER", "RESET", mode).result();
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
     * @brief Forces a replica to initiate a manual failover
     *
     * Forces a replica to perform a manual failover of its master, taking over
     * its hash slots.
     *
     * @param option Optional failover mode: "FORCE", "TAKEOVER", or none for default behavior
     * @return status Success/failure status
     * @see https://redis.io/commands/cluster-failover
     */
    status
    cluster_failover(const std::string &option = "") {
        if (option.empty()) {
            return derived().template command<status>("CLUSTER", "FAILOVER").result();
        }
        return derived().template command<status>("CLUSTER", "FAILOVER", option).result();
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
     * @brief Reconfigure a node as a replica of another node
     *
     * Reconfigures a node to be a replica of the specified master node.
     *
     * @param node_id ID of the master node
     * @return status Success/failure status
     * @see https://redis.io/commands/cluster-replicate
     */
    status
    cluster_replicate(const std::string &node_id) {
        return derived().template command<status>("CLUSTER", "REPLICATE", node_id).result();
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
     * @brief Save the cluster configuration to disk
     *
     * Forces the node to save the cluster configuration to disk.
     *
     * @return status Success/failure status
     * @see https://redis.io/commands/cluster-saveconfig
     */
    status
    cluster_saveconfig() {
        return derived().template command<status>("CLUSTER", "SAVECONFIG").result();
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
     * @brief Set the configuration epoch for a node
     *
     * Sets the configuration epoch for a node.
     *
     * @param epoch Configuration epoch to set
     * @return status Success/failure status
     * @see https://redis.io/commands/cluster-set-config-epoch
     */
    status
    cluster_set_config_epoch(long long epoch) {
        return derived().template command<status>("CLUSTER", "SET-CONFIG-EPOCH", epoch).result();
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
     * @brief Advance the cluster config epoch
     *
     * Increments the cluster configuration epoch.
     *
     * @return status Success/failure status
     * @see https://redis.io/commands/cluster-bumpepoch
     */
    status
    cluster_bumpepoch() {
        return derived().template command<status>("CLUSTER", "BUMPEPOCH").result();
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
     * @brief Get the ID of the current node
     *
     * Returns the ID of the Redis node, as a 40 character string.
     *
     * @return std::string Node ID
     * @see https://redis.io/commands/cluster-myid
     */
    std::string
    cluster_myid() {
        return derived().template command<std::string>("CLUSTER", "MYID").result();
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
     * @brief Get the hash slot for a key
     *
     * Returns the hash slot number for the specified key.
     *
     * @param key The key to get the hash slot for
     * @return long long Hash slot number
     * @see https://redis.io/commands/cluster-keyslot
     */
    long long
    cluster_keyslot(const std::string &key) {
        return derived().template command<long long>("CLUSTER", "KEYSLOT", key).result();
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
     * @brief Count the number of keys in a hash slot
     *
     * Returns the number of keys in the specified hash slot.
     *
     * @param slot The hash slot to count keys for
     * @return long long Number of keys in the slot
     * @see https://redis.io/commands/cluster-countkeysinslot
     */
    long long
    cluster_countkeysinslot(int slot) {
        return derived().template command<long long>("CLUSTER", "COUNTKEYSINSLOT", slot).result();
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
     * @brief Get keys in a hash slot
     *
     * Returns a list of keys in the specified hash slot.
     *
     * @param slot The hash slot to get keys from
     * @param count Maximum number of keys to return
     * @return std::vector<std::string> List of keys in the slot
     * @see https://redis.io/commands/cluster-getkeysinslot
     */
    std::vector<std::string>
    cluster_getkeysinslot(int slot, int count) {
        return derived().template command<std::vector<std::string>>("CLUSTER", "GETKEYSINSLOT", slot, count).result();
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
};

} // namespace qb::redis

#endif // QBM_REDIS_CLUSTER_COMMANDS_H 