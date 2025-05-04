# `qbm-redis`: Key Commands

This document covers Redis commands related to managing keys themselves, including existence checks, deletion, expiration, type information, and scanning.

Reference: [Redis Generic Commands](https://redis.io/commands/?group=generic)

## Common Reply Types

*   `qb::redis::status`: For commands returning simple "OK" (e.g., `RENAME`).
*   `qb::redis::Reply<long long>`: For counts (`DEL`, `EXISTS`, `TOUCH`, `UNLINK`) or TTL values (`TTL`, `PTTL`).
*   `qb::redis::Reply<bool>`: For boolean results (`EXPIRE`, `EXPIREAT`, `PERSIST`, `RENAMENX`).
*   `qb::redis::Reply<std::optional<std::string>>`: For `RANDOMKEY`, `DUMP`.
*   `qb::redis::Reply<std::vector<std::string>>`: For `KEYS`.
*   `qb::redis::Reply<std::string>`: For `TYPE`.
*   `qb::redis::Reply<qb::redis::scan<std::vector<std::string>>>`: For `SCAN`.

## Commands

### `DEL key [key ...]`

Removes the specified keys. Returns the number of keys that were removed.

*   **Sync:** `Reply<long long> del(const std::vector<std::string> &keys)`
*   **Async:** `void del_async(const std::vector<std::string> &keys, Callback<long long> cb)`

### `DUMP key`

Returns a serialized representation of the value stored at `key`.

*   **Sync:** `Reply<std::optional<std::string>> dump(const std::string &key)`
*   **Async:** `void dump_async(const std::string &key, Callback<std::optional<std::string>> cb)`

### `EXISTS key [key ...]`

Returns the number of `keys` that exist among the ones specified.

*   **Sync:** `Reply<long long> exists(const std::vector<std::string> &keys)`
*   **Async:** `void exists_async(const std::vector<std::string> &keys, Callback<long long> cb)`

### `EXPIRE key seconds`

Set a timeout on `key` in seconds. Returns `true` if the timeout was set, `false` otherwise (e.g., key doesn't exist).

*   **Sync:** `Reply<bool> expire(const std::string &key, long long timeout_s)`
*   **Sync (chrono):** `Reply<bool> expire(const std::string &key, const std::chrono::seconds &timeout)`
*   **Async:** `void expire_async(const std::string &key, long long timeout_s, Callback<bool> cb)`
*   **Async (chrono):** `void expire_async(const std::string &key, const std::chrono::seconds &timeout, Callback<bool> cb)`

### `EXPIREAT key timestamp`

Set the expiration for `key` as a Unix timestamp (seconds since epoch). Returns `true` if the timeout was set.

*   **Sync:** `Reply<bool> expireat(const std::string &key, long long timestamp_s)`
*   **Sync (chrono):** `Reply<bool> expireat(const std::string &key, const std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds> &tp)`
*   **Async:** `void expireat_async(const std::string &key, long long timestamp_s, Callback<bool> cb)`
*   **Async (chrono):** `void expireat_async(const std::string &key, const std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds> &tp, Callback<bool> cb)`

### `KEYS pattern`

Returns all keys matching `pattern`.
**Warning:** Use with caution on production servers, can be slow.

*   **Sync:** `Reply<std::vector<std::string>> keys(const std::string &pattern)`
*   **Async:** `void keys_async(const std::string &pattern, Callback<std::vector<std::string>> cb)`

### `MOVE key db`

Moves `key` from the current database to the specified `db` index. Returns `true` if the key was moved.

*   **Sync:** `Reply<bool> move(const std::string &key, long long destination_db)`
*   **Async:** `void move_async(const std::string &key, long long destination_db, Callback<bool> cb)`

### `PERSIST key`

Removes the existing timeout on `key`. Returns `true` if the timeout was removed.

*   **Sync:** `Reply<bool> persist(const std::string &key)`
*   **Async:** `void persist_async(const std::string &key, Callback<bool> cb)`

### `PEXPIRE key milliseconds`

Set a timeout on `key` in milliseconds.

*   **Sync:** `Reply<bool> pexpire(const std::string &key, long long timeout_ms)`
*   **Sync (chrono):** `Reply<bool> pexpire(const std::string &key, const std::chrono::milliseconds &timeout)`
*   **Async:** `void pexpire_async(const std::string &key, long long timeout_ms, Callback<bool> cb)`
*   **Async (chrono):** `void pexpire_async(const std::string &key, const std::chrono::milliseconds &timeout, Callback<bool> cb)`

### `PEXPIREAT key milliseconds-timestamp`

Set the expiration for `key` as a Unix timestamp in milliseconds.

*   **Sync:** `Reply<bool> pexpireat(const std::string &key, long long timestamp_ms)`
*   **Sync (chrono):** `Reply<bool> pexpireat(const std::string &key, const std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds> &tp)`
*   **Async:** `void pexpireat_async(const std::string &key, long long timestamp_ms, Callback<bool> cb)`
*   **Async (chrono):** `void pexpireat_async(const std::string &key, const std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds> &tp, Callback<bool> cb)`

### `PTTL key`

Returns the remaining time to live of a key in milliseconds.

*   **Sync:** `Reply<long long> pttl(const std::string &key)`
*   **Async:** `void pttl_async(const std::string &key, Callback<long long> cb)`

### `RANDOMKEY`

Returns a random key from the currently selected database.

*   **Sync:** `Reply<std::optional<std::string>> randomkey()`
*   **Async:** `void randomkey_async(Callback<std::optional<std::string>> cb)`

### `RENAME key newkey`

Renames `key` to `newkey`. Overwrites `newkey` if it already exists.

*   **Sync:** `status rename(const std::string &key, const std::string &new_key)`
*   **Async:** `void rename_async(const std::string &key, const std::string &new_key, Callback<status> cb)`

### `RENAMENX key newkey`

Renames `key` to `newkey` only if `newkey` does not yet exist. Returns `true` if key was renamed.

*   **Sync:** `Reply<bool> renamenx(const std::string &key, const std::string &new_key)`
*   **Async:** `void renamenx_async(const std::string &key, const std::string &new_key, Callback<bool> cb)`

### `RESTORE key ttl serialized-value [REPLACE]`

Creates a key using the provided serialized value (obtained via `DUMP`).

*   **Sync:** `status restore(const std::string &key, const std::string &val, long long ttl, bool replace = false)`
*   **Async:** `void restore_async(const std::string &key, const std::string &val, long long ttl, Callback<status> cb, bool replace = false)`

### `SCAN cursor [MATCH pattern] [COUNT count] [TYPE type]`

Iterates the set of keys in the current database.

*   **Sync:** `Reply<scan<std::vector<std::string>>> scan(long long cursor, const std::optional<std::string>& pattern = std::nullopt, const std::optional<long long>& count = std::nullopt)`
*   **Async:** `void scan_async(long long cursor, Callback<scan<std::vector<std::string>>> cb, const std::optional<std::string>& pattern = std::nullopt, const std::optional<long long>& count = std::nullopt)`
*   **Note:** The `scan` struct contains `cursor` (long long) and `elements` (the vector of keys).
*   **Note:** `TYPE` option not directly exposed.

### `TOUCH key [key ...]`

Alters the last access time of a key(s). Returns the number of keys touched.

*   **Sync:** `Reply<long long> touch(const std::vector<std::string> &keys)`
*   **Async:** `void touch_async(const std::vector<std::string> &keys, Callback<long long> cb)`

### `TTL key`

Returns the remaining time to live of a key in seconds.

*   **Sync:** `Reply<long long> ttl(const std::string &key)`
*   **Async:** `void ttl_async(const std::string &key, Callback<long long> cb)`

### `TYPE key`

Returns the string representation of the type of the value stored at `key` (e.g., "string", "list", "set").

*   **Sync:** `Reply<std::string> type(const std::string &key)`
*   **Async:** `void type_async(const std::string &key, Callback<std::string> cb)`

### `UNLINK key [key ...]`

Removes the specified keys asynchronously (non-blocking delete). Returns the number of keys that were unlinked.

*   **Sync:** `Reply<long long> unlink(const std::vector<std::string> &keys)`
*   **Async:** `void unlink_async(const std::vector<std::string> &keys, Callback<long long> cb)`

### `WAIT numreplicas timeout`

Blocks the current client until all the previous write commands are successfully transferred and acknowledged by at least `numreplicas` slaves. Returns the number of slaves that acknowledged.

*   **Sync:** `Reply<long long> wait(long long num_slaves, long long timeout_ms)`
*   **Sync (chrono):** `Reply<long long> wait(long long num_slaves, const std::chrono::milliseconds &timeout = std::chrono::milliseconds{0})`
*   **Async:** `void wait_async(long long num_slaves, long long timeout_ms, Callback<long long> cb)`
*   **Async (chrono):** `void wait_async(long long num_slaves, const std::chrono::milliseconds &timeout, Callback<long long> cb)` 