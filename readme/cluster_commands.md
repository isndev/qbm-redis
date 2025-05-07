# `qbm-redis`: Cluster Commands

This document covers Redis commands related to managing and inspecting a Redis Cluster.

Reference: [Redis Cluster Commands](https://redis.io/commands/?group=cluster)

## Common Reply Types

*   `qb::json`: For commands returning structured information about the cluster (e.g., `CLUSTER INFO`, `CLUSTER NODES`, `CLUSTER SLOTS`).
*   `std::string`: For commands returning a single string value (e.g., `CLUSTER MYID`).
*   `long long`: For commands returning an integer (e.g., `CLUSTER KEYSLOT`, `CLUSTER COUNTKEYSINSLOT`).
*   `std::vector<std::string>`: For commands returning a list of strings (e.g., `CLUSTER GETKEYSINSLOT`).
*   `qb::redis::status`: For commands that modify cluster state or return a simple "OK" (e.g., `CLUSTER MEET`, `CLUSTER RESET`, `CLUSTER SAVECONFIG`).

## Commands

All Cluster commands are prefixed with `CLUSTER`.

### `CLUSTER ADDSLOTS slot [slot ...]` / `CLUSTER DELSLOTS slot [slot ...]`

These commands are typically used by cluster management tools and are not directly exposed as individual methods in `cluster_commands.h`. Use the generic `command<status>()` method if needed:
`redis.command<status>("CLUSTER", "ADDSLOTS", slot1, slot2);`
`redis.command<status>("CLUSTER", "DELSLOTS", slot1, slot2);`

### `CLUSTER BUMPEPOCH`

Advances the cluster config epoch.

*   **Sync:** `status cluster_bumpepoch()`
*   **Async:** `Derived& cluster_bumpepoch(Func &&func)`
*   **Reply:** `Reply<status>`
*   **Example (from `test-cluster-commands.cpp`, adapted for expected failure in non-cluster mode):**
    ```cpp
    try {
        auto bump_status = redis.cluster_bumpepoch();
        // SUCCEED(); // This would be for a cluster environment
    } catch (const std::exception& e) {
        // Expected to fail or return error in non-cluster mode
        std::string error = e.what();
        EXPECT_TRUE(error.find("cluster") != std::string::npos || error.find("ERR") != std::string::npos);
    }
    ```

### `CLUSTER COUNTKEYSINSLOT slot`

Returns the number of keys in the specified hash slot.

*   **Sync:** `long long cluster_countkeysinslot(int slot)`
*   **Async:** `Derived& cluster_countkeysinslot(Func &&func, int slot)`
*   **Reply:** `Reply<long long>`
*   **Example (from `test-cluster-commands.cpp`):**
    ```cpp
    // Test with slot 0
    auto count = redis.cluster_countkeysinslot(0);
    // In a non-cluster Redis, this might return 0 or an error depending on server version.
    // The test expects it not to throw an unrelated exception.
    EXPECT_GE(count, 0); 
    ```

### `CLUSTER FAILOVER [FORCE|TAKEOVER]`

Forces a replica to perform a manual failover of its master.

*   **Sync:** `status cluster_failover(const std::string &option = "")`
*   **Async:** `Derived& cluster_failover(Func &&func, const std::string &option = "")`
*   **Reply:** `Reply<status>`
*   **`option`:** Can be `"FORCE"`, `"TAKEOVER"`, or empty for default.

### `CLUSTER FORGET node-id`

Removes a node from the nodes table.

*   **Sync:** `status cluster_forget(const std::string &node_id)`
*   **Async:** `Derived& cluster_forget(Func &&func, const std::string &node_id)`
*   **Reply:** `Reply<status>`

### `CLUSTER GETKEYSINSLOT slot count`

Returns an array of keys in the specified hash slot, up to `count` keys.

*   **Sync:** `std::vector<std::string> cluster_getkeysinslot(int slot, int count)`
*   **Async:** `Derived& cluster_getkeysinslot(Func &&func, int slot, int count)`
*   **Reply:** `Reply<std::vector<std::string>>`
*   **Example (from `test-cluster-commands.cpp`):**
    ```cpp
    // Test with slot 0 and count 10
    auto keys = redis.cluster_getkeysinslot(0, 10);
    // In non-cluster mode, this will likely be empty or error. Test checks for valid, possibly empty, response.
    EXPECT_TRUE(keys.empty() || !keys.empty());
    ```

### `CLUSTER INFO`

Provides INFO output for the cluster.

*   **Sync:** `qb::json cluster_info()`
*   **Async:** `Derived& cluster_info(Func &&func)`
*   **Reply:** `Reply<qb::json>` (Redis 7+ returns a structured reply, older versions a string; the C++ API normalizes to `qb::json` if possible, or a string within the JSON if not parseable as a Redis bulk string that is itself JSON).
*   **Example (from `test-cluster-commands.cpp`):**
    ```cpp
    auto info = redis.cluster_info();
    // Response can be a JSON object (cluster mode) or a string (standalone mode)
    EXPECT_TRUE(info.is_object() || info.is_string());
    if (info.is_string()) {
        std::string info_str = info.get<std::string>();
        EXPECT_TRUE(info_str.find("cluster_enabled:0") != std::string::npos || info_str.find("cluster_state:fail") != std::string::npos);
    } else if (info.is_object()) {
         EXPECT_TRUE(info.contains("cluster_state"));
    }
    ```

### `CLUSTER KEYSLOT key`

Returns the hash slot for the given key.

*   **Sync:** `long long cluster_keyslot(const std::string &key)`
*   **Async:** `Derived& cluster_keyslot(Func &&func, const std::string &key)`
*   **Reply:** `Reply<long long>`
*   **Example (from `test-cluster-commands.cpp`):**
    ```cpp
    std::string key = "{mykey}incluster"; // Key with a hash tag
    auto slot = redis.cluster_keyslot(key);
    EXPECT_GE(slot, 0);
    EXPECT_LE(slot, 16383);
    ```

### `CLUSTER MEET ip port`

Forces a node to handshake with another node.

*   **Sync:** `status cluster_meet(const std::string &ip, int port)`
*   **Async:** `Derived& cluster_meet(Func &&func, const std::string &ip, int port)`
*   **Reply:** `Reply<status>`

### `CLUSTER MYID`

Returns the node ID of the currently connected node.

*   **Sync:** `std::string cluster_myid()`
*   **Async:** `Derived& cluster_myid(Func &&func)`
*   **Reply:** `Reply<std::string>`
*   **Example (from `test-cluster-commands.cpp`):**
    ```cpp
    auto node_id = redis.cluster_myid();
    // Might be empty or fail in non-cluster mode. If not empty, it's a 40-char string.
    if (!node_id.empty()) {
        EXPECT_EQ(node_id.length(), 40);
    }
    ```

### `CLUSTER NODES`

Returns the cluster configuration from the perspective of the connected node.

*   **Sync:** `qb::json cluster_nodes()`
*   **Async:** `Derived& cluster_nodes(Func &&func)`
*   **Reply:** `Reply<qb::json>` (Redis 7+ returns a structured reply, older versions a string; C++ API normalizes to `qb::json`).
*   **Example (from `test-cluster-commands.cpp`):**
    ```cpp
    auto nodes = redis.cluster_nodes();
    // Can be JSON object/array (cluster) or string (standalone)
    EXPECT_TRUE(nodes.is_object() || nodes.is_string() || nodes.is_array());
    ```

### `CLUSTER REPLICATE node-id`

Reconfigures a node as a replica of the specified master node.

*   **Sync:** `status cluster_replicate(const std::string &node_id)`
*   **Async:** `Derived& cluster_replicate(Func &&func, const std::string &node_id)`
*   **Reply:** `Reply<status>`

### `CLUSTER RESET [HARD|SOFT]`

Resets a Redis Cluster node.

*   **Sync:** `status cluster_reset(const std::string &mode = "SOFT")`
*   **Async:** `Derived& cluster_reset(Func &&func, const std::string &mode = "SOFT")`
*   **Reply:** `Reply<status>`
*   **`mode`:** `"HARD"` or `"SOFT"` (default).

### `CLUSTER SAVECONFIG`

Forces the node to save its cluster configuration to disk.

*   **Sync:** `status cluster_saveconfig()`
*   **Async:** `Derived& cluster_saveconfig(Func &&func)`
*   **Reply:** `Reply<status>`

### `CLUSTER SET-CONFIG-EPOCH config-epoch`

Sets the configuration epoch for a new node.

*   **Sync:** `status cluster_set_config_epoch(long long epoch)`
*   **Async:** `Derived& cluster_set_config_epoch(Func &&func, long long epoch)`
*   **Reply:** `Reply<status>`

### `CLUSTER SLOTS`

Returns the mapping of hash slots to Custer master nodes.

*   **Sync:** `qb::json cluster_slots()`
*   **Async:** `Derived& cluster_slots(Func &&func)`
*   **Reply:** `Reply<qb::json>` (Redis 7+ returns structured reply, older versions might differ; C++ API normalizes).
*   **Example (from `test-cluster-commands.cpp`):**
    ```cpp
    auto slots = redis.cluster_slots();
    // Expected to be an array or object (for older versions, might be parsed from string)
    EXPECT_TRUE(slots.is_array() || slots.is_object());
    ``` 