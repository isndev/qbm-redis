# `qbm-redis`: Server Commands

This document covers Redis commands related to server management, monitoring, and configuration.

Reference: [Redis Server Commands](https://redis.io/commands/?group=server)

## Common Reply Types

*   `qb::redis::status`: For commands returning simple "OK" or status strings.
*   `qb::redis::Reply<long long>`: For counts (`DBSIZE`, `LASTSAVE`, `COMMAND COUNT`).
*   `qb::redis::Reply<bool>`: Not common in this group.
*   `qb::redis::Reply<std::string>`: For informational commands (`INFO`, `CLIENT GETNAME`, `ROLE`, `CONFIG GET`).
*   `qb::redis::Reply<std::vector<std::string>>`: For list results (`CLIENT LIST`, `COMMAND`, `COMMAND GETKEYS`, `SLOWLOG GET`).
*   `qb::redis::Reply<qb::redis::memory_info>`: For `MEMORY STATS`.
*   `qb::redis::Reply<qb::redis::cluster_node>`: For `CLUSTER NODES` (partial parsing).

## Commands

### Client Management

*   **`CLIENT GETNAME`**: Returns the name of the current connection.
    *   **Sync:** `Reply<std::optional<std::string>> client_getname()`
    *   **Async:** `void client_getname_async(Callback<std::optional<std::string>> cb)`
*   **`CLIENT ID`**: Returns the client ID for the current connection.
    *   **Sync:** `Reply<long long> client_id()`
    *   **Async:** `void client_id_async(Callback<long long> cb)`
*   **`CLIENT KILL [ip:port] [ID client-id] [TYPE normal|master|slave|pubsub] [USER username] [ADDR ip:port] [LADDR ip:port] [SKIPME yes|no]`**: Kills connections.
    *   **Sync:** `Reply<long long> client_kill(const std::string &addr = "", long long id = 0, const std::string &type = "", const std::string &user = "", const std::string &laddr = "", bool skipme = true)`
    *   **Async:** `void client_kill_async(Callback<long long> cb, const std::string &addr = "", long long id = 0, const std::string &type = "", const std::string &user = "", const std::string &laddr = "", bool skipme = true)`
*   **`CLIENT LIST [TYPE normal|master|replica|pubsub]`**: Returns information about client connections.
    *   **Sync:** `Reply<std::string> client_list(const std::string &type = "")` (Returns raw string)
    *   **Async:** `void client_list_async(Callback<std::string> cb, const std::string &type = "")`
*   **`CLIENT PAUSE timeout [WRITE|ALL]`**: Suspends processing of commands from clients.
    *   **Sync:** `status client_pause(long long timeout_ms, const std::string &mode = "ALL")`
    *   **Async:** `void client_pause_async(long long timeout_ms, Callback<status> cb, const std::string &mode = "ALL")`
*   **`CLIENT SETNAME connection-name`**: Sets the current connection name.
    *   **Sync:** `status client_setname(const std::string &name)`
    *   **Async:** `void client_setname_async(const std::string &name, Callback<status> cb)`
*   **`CLIENT TRACKING ON|OFF ...`**: Enables/disables server-assisted client side caching.
    *   **Sync:** `status client_tracking(bool enabled = true)`
    *   **Async:** `void client_tracking_async(Callback<status> cb, bool enabled = true)`
*   **`CLIENT UNBLOCK client-id [TIMEOUT|ERROR]`**: Unblocks a client blocked in a blocking command.
    *   **Sync:** `Reply<long long> client_unblock(long long client_id, bool error = false)`
    *   **Async:** `void client_unblock_async(long long client_id, Callback<long long> cb, bool error = false)`

### Configuration

*   **`CONFIG GET parameter`**: Get the value of a configuration parameter.
    *   **Sync:** `Reply<qb::unordered_map<std::string, std::string>> config_get(const std::string &parameter)`
    *   **Async:** `void config_get_async(const std::string &parameter, Callback<qb::unordered_map<std::string, std::string>> cb)`
*   **`CONFIG RESETSTAT`**: Resets the statistics reported by INFO.
    *   **Sync:** `status config_resetstat()`
    *   **Async:** `void config_resetstat_async(Callback<status> cb)`
*   **`CONFIG REWRITE`**: Rewrites the redis.conf file with the current configuration.
    *   **Sync:** `status config_rewrite()`
    *   **Async:** `void config_rewrite_async(Callback<status> cb)`
*   **`CONFIG SET parameter value`**: Sets a configuration parameter to the given value.
    *   **Sync:** `status config_set(const std::string &parameter, const std::string &value)`
    *   **Async:** `void config_set_async(const std::string &parameter, const std::string &value, Callback<status> cb)`

### Server Information & Management

*   **`COMMAND`**: Returns array reply of details about all Redis commands.
    *   **Sync:** `Reply<std::vector<std::string>> command()` (Returns raw string array)
    *   **Async:** `void command_async(Callback<std::vector<std::string>> cb)`
*   **`COMMAND COUNT`**: Returns the total number of commands in this Redis server.
    *   **Sync:** `Reply<long long> command_count()`
    *   **Async:** `void command_count_async(Callback<long long> cb)`
*   **`COMMAND GETKEYS command [arg ...]`**: Returns array reply of keys from a full Redis command.
    *   **Sync:** `Reply<std::vector<std::string>> command_getkeys(const std::vector<std::string> &command_args)`
    *   **Async:** `void command_getkeys_async(const std::vector<std::string> &command_args, Callback<std::vector<std::string>> cb)`
*   **`COMMAND INFO command-name [command-name ...]`**: Returns array reply of details about the specified Redis commands.
    *   **Sync:** `Reply<std::vector<std::string>> command_info(const std::vector<std::string> &commands)` (Returns raw string array)
    *   **Async:** `void command_info_async(const std::vector<std::string> &commands, Callback<std::vector<std::string>> cb)`
*   **`DBSIZE`**: Returns the number of keys in the selected database.
    *   **Sync:** `Reply<long long> dbsize()`
    *   **Async:** `void dbsize_async(Callback<long long> cb)`
*   **`INFO [section]`**: Returns information and statistics about the server.
    *   **Sync:** `Reply<std::string> info(const std::string &section = "default")` (Returns raw info string)
    *   **Async:** `void info_async(Callback<std::string> cb, const std::string &section = "default")`
*   **`LASTSAVE`**: Returns the Unix timestamp of the last successful save to disk.
    *   **Sync:** `Reply<long long> lastsave()`
    *   **Async:** `void lastsave_async(Callback<long long> cb)`
*   **`ROLE`**: Returns the replication role of the instance.
    *   **Sync:** `Reply<std::string> role()` (Returns raw string array reply)
    *   **Async:** `void role_async(Callback<std::string> cb)`
*   **`TIME`**: Returns the current server time (Unix timestamp and microseconds).
    *   **Sync:** `Reply<std::vector<std::string>> time()`
    *   **Async:** `void time_async(Callback<std::vector<std::string>> cb)`

### Persistence & Database

*   **`BGSAVE [SCHEDULE]`**: Asynchronously saves the dataset to disk.
    *   **Sync:** `status bgsave(bool schedule = false)`
    *   **Async:** `void bgsave_async(Callback<status> cb, bool schedule = false)`
*   **`BGREWRITEAOF`**: Asynchronously rewrites the append-only file.
    *   **Sync:** `status bgrewriteaof()`
    *   **Async:** `void bgrewriteaof_async(Callback<status> cb)`
*   **`FLUSHALL [ASYNC|SYNC]`**: Removes all keys from all databases.
    *   **Sync:** `status flushall(bool async = false)`
    *   **Async:** `void flushall_async(Callback<status> cb, bool async = false)`
*   **`FLUSHDB [ASYNC|SYNC]`**: Removes all keys from the current database.
    *   **Sync:** `status flushdb(bool async = false)`
    *   **Async:** `void flushdb_async(Callback<status> cb, bool async = false)`
*   **`SAVE`**: Synchronously saves the dataset to disk.
    *   **Sync:** `status save()`
    *   **Async:** `void save_async(Callback<status> cb)`

### Debugging & Monitoring

*   **`DEBUG OBJECT key`**: Get debugging information about a key.
    *   **Sync:** `Reply<std::string> debug_object(const std::string &key)`
    *   **Async:** `void debug_object_async(const std::string &key, Callback<std::string> cb)`
*   **`DEBUG SEGFAULT`**: Make the server crash (for testing).
    *   **Sync:** `status debug_segfault()`
    *   **Async:** `void debug_segfault_async(Callback<status> cb)`
*   **`MEMORY DOCTOR`**: Outputs memory problems report.
    *   **Sync:** `Reply<std::string> memory_doctor()`
    *   **Async:** `void memory_doctor_async(Callback<std::string> cb)`
*   **`MEMORY MALLOC-STATS`**: Show allocator internal stats.
    *   **Sync:** `Reply<std::string> memory_malloc_stats()`
    *   **Async:** `void memory_malloc_stats_async(Callback<std::string> cb)`
*   **`MEMORY PURGE`**: Ask the allocator to release memory.
    *   **Sync:** `status memory_purge()`
    *   **Async:** `void memory_purge_async(Callback<status> cb)`
*   **`MEMORY STATS`**: Show memory usage details.
    *   **Sync:** `Reply<memory_info> memory_stats()`
    *   **Async:** `void memory_stats_async(Callback<memory_info> cb)`
*   **`MEMORY USAGE key [SAMPLES count]`**: Estimate memory usage of a key.
    *   **Sync:** `Reply<long long> memory_usage(const std::string &key, long long samples = 0)`
    *   **Async:** `void memory_usage_async(const std::string &key, Callback<long long> cb, long long samples = 0)`
*   **`MONITOR`**: Listen for all requests received by the server in real time (enters monitoring mode).
    *   **Note:** Like `SUBSCRIBE`, this changes connection state. Not directly supported by the standard client, requires a dedicated monitoring connection/handler.
*   **`SLOWLOG GET [count]`**: Get entries from the slow log.
    *   **Sync:** `Reply<std::vector<std::string>> slowlog_get(long long count = -1)` (Raw reply)
    *   **Async:** `void slowlog_get_async(Callback<std::vector<std::string>> cb, long long count = -1)`
*   **`SLOWLOG LEN`**: Get the length of the slow log.
    *   **Sync:** `Reply<long long> slowlog_len()`
    *   **Async:** `void slowlog_len_async(Callback<long long> cb)`
*   **`SLOWLOG RESET`**: Reset the slow log.
    *   **Sync:** `status slowlog_reset()`
    *   **Async:** `void slowlog_reset_async(Callback<status> cb)`

### Replication

*   **`REPLICAOF host port` / `SLAVEOF host port`**: Make the server a replica of another instance.
    *   **Sync:** `status slaveof(const std::string &host, long long port)`
    *   **Async:** `void slaveof_async(const std::string &host, long long port, Callback<status> cb)`
*   **`REPLICAOF NO ONE` / `SLAVEOF NO ONE`**: Promote a replica to a master.
    *   **Sync:** `status replicaof_no_one()`
    *   **Async:** `void replicaof_no_one_async(Callback<status> cb)`
*   **`PSYNC replicationid offset`**: Used internally for partial resynchronization.
    *   **Sync:** `status psync(const std::string &replication_id, long long offset)`
    *   **Async:** `void psync_async(const std::string &replication_id, long long offset, Callback<status> cb)`
*   **`SYNC`**: Used internally for full synchronization.
    *   **Sync:** `status sync()`
    *   **Async:** `void sync_async(Callback<status> cb)`

(... Add CLUSTER commands if implemented ...)

(... Add LATENCY commands if implemented ...) 