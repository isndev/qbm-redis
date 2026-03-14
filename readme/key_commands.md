# `qbm-redis`: Key Commands

This document covers Redis commands related to managing keys, including existence checks, deletion, expiration, type information, scanning, and migration.

**API:** All commands support **coroutine** (`co_await redis.cmd(...)`) and **callback** (`redis.cmd(callback, ...)`).

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

*   **Coroutine:** `Reply<long long> del(const std::vector<std::string> &keys)`
*   **Callback:** `redis.del(callback, keys)`

### `DUMP key`

Returns a serialized representation of the value stored at `key`.

*   **Coroutine:** `Reply<std::optional<std::string>> dump(const std::string &key)`
*   **Callback:** `redis.dump(callback, key)`

### `EXISTS key [key ...]`

Returns the number of `keys` that exist among the ones specified.

*   **Coroutine:** `Reply<long long> exists(const std::vector<std::string> &keys)`
*   **Callback:** `redis.exists(callback, keys)`

### `EXPIRE key seconds`

Set a timeout on `key` in seconds. Returns `true` if the timeout was set, `false` otherwise (e.g., key doesn't exist).

*   **Coroutine:** `Reply<bool> expire(const std::string &key, long long timeout_s)`
*   **Coroutine (chrono):** `Reply<bool> expire(const std::string &key, const std::chrono::seconds &timeout)`
*   **Callback:** `redis.expire(callback, key, timeout_s)` or `redis.expire(callback, key, timeout)` (chrono)

### `EXPIREAT key timestamp`

Set the expiration for `key` as a Unix timestamp (seconds since epoch). Returns `true` if the timeout was set.

*   **Coroutine:** `Reply<bool> expireat(const std::string &key, long long timestamp_s)`
*   **Coroutine (chrono):** `Reply<bool> expireat(const std::string &key, const std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds> &tp)`
*   **Callback:** `redis.expireat(callback, key, timestamp_s)` or `redis.expireat(callback, key, tp)` (chrono)

### `KEYS pattern`

Returns all keys matching `pattern`.
**Warning:** Use with caution on production servers, can be slow.

*   **Coroutine:** `Reply<std::vector<std::string>> keys(const std::string &pattern)`
*   **Callback:** `redis.keys(callback, pattern)`

### `MOVE key db`

Moves `key` from the current database to the specified `db` index. Returns `true` if the key was moved.

*   **Coroutine:** `Reply<bool> move(const std::string &key, long long destination_db)`
*   **Callback:** `redis.move(callback, key, destination_db)`

### `PERSIST key`

Removes the existing timeout on `key`. Returns `true` if the timeout was removed.

*   **Coroutine:** `Reply<bool> persist(const std::string &key)`
*   **Callback:** `redis.persist(callback, key)`

### `PEXPIRE key milliseconds`

Set a timeout on `key` in milliseconds.

*   **Coroutine:** `Reply<bool> pexpire(const std::string &key, long long timeout_ms)`
*   **Coroutine (chrono):** `Reply<bool> pexpire(const std::string &key, const std::chrono::milliseconds &timeout)`
*   **Callback:** `redis.pexpire(callback, key, timeout_ms)` or `redis.pexpire(callback, key, timeout)` (chrono)

### `PEXPIREAT key milliseconds-timestamp`

Set the expiration for `key` as a Unix timestamp in milliseconds.

*   **Coroutine:** `Reply<bool> pexpireat(const std::string &key, long long timestamp_ms)`
*   **Coroutine (chrono):** `Reply<bool> pexpireat(const std::string &key, const std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds> &tp)`
*   **Callback:** `redis.pexpireat(callback, key, timestamp_ms)` or `redis.pexpireat(callback, key, tp)` (chrono)

### `PTTL key`

Returns the remaining time to live of a key in milliseconds.

*   **Coroutine:** `Reply<long long> pttl(const std::string &key)`
*   **Callback:** `redis.pttl(callback, key)`

### `RANDOMKEY`

Returns a random key from the currently selected database.

*   **Coroutine:** `Reply<std::optional<std::string>> randomkey()`
*   **Callback:** `redis.randomkey(callback)`

### `RENAME key newkey`

Renames `key` to `newkey`. Overwrites `newkey` if it already exists.

*   **Coroutine:** `status rename(const std::string &key, const std::string &new_key)`
*   **Callback:** `redis.rename(callback, key, new_key)`

### `RENAMENX key newkey`

Renames `key` to `newkey` only if `newkey` does not yet exist. Returns `true` if key was renamed.

*   **Coroutine:** `Reply<bool> renamenx(const std::string &key, const std::string &new_key)`
*   **Callback:** `redis.renamenx(callback, key, new_key)`

### `RESTORE key ttl serialized-value [REPLACE]`

Creates a key using the provided serialized value (obtained via `DUMP`).

*   **Coroutine:** `status restore(const std::string &key, const std::string &val, long long ttl, bool replace = false)`
*   **Callback:** `redis.restore(callback, key, val, ttl, replace)`

### `SCAN cursor [MATCH pattern] [COUNT count] [TYPE type]`

Iterates the set of keys in the current database.

*   **Coroutine:** `Reply<scan<std::vector<std::string>>> scan(long long cursor, const std::optional<std::string>& pattern = std::nullopt, const std::optional<long long>& count = std::nullopt)`
*   **Callback:** `redis.scan(callback, cursor, pattern, count)`
*   **Note:** The `scan` struct contains `cursor` (long long) and `items` (the vector of keys).
*   **Note:** `TYPE` option not directly exposed.

### `TOUCH key [key ...]`

Alters the last access time of a key(s). Returns the number of keys touched.

*   **Coroutine:** `Reply<long long> touch(const std::vector<std::string> &keys)`
*   **Callback:** `redis.touch(callback, keys)`

### `TTL key`

Returns the remaining time to live of a key in seconds.

*   **Coroutine:** `Reply<long long> ttl(const std::string &key)`
*   **Callback:** `redis.ttl(callback, key)`

### `TYPE key`

Returns the string representation of the type of the value stored at `key` (e.g., "string", "list", "set").

*   **Coroutine:** `Reply<std::string> type(const std::string &key)`
*   **Callback:** `redis.type(callback, key)`

### `UNLINK key [key ...]`

Removes the specified keys asynchronously (non-blocking delete). Returns the number of keys that were unlinked.

*   **Coroutine:** `Reply<long long> unlink(const std::vector<std::string> &keys)`
*   **Callback:** `redis.unlink(callback, keys)`

### `COPY source destination [DB db] [REPLACE]`

Copies the value stored at `source` to `destination`.

*   **Coroutine:** `Reply<bool> copyKey(source, destination, db, replace)`
*   **Callback:** `redis.copyKey(callback, source, destination, db, replace)`

### `EXPIRETIME key` / `PEXPIRETIME key`

Returns the expiration Unix timestamp of `key` (seconds or milliseconds). Returns -1 if no expiry.

*   **Coroutine:** `Reply<long long> expiretime(key)` or `pexpiretime(key)`
*   **Callback:** `redis.expiretime(callback, key)` or `redis.pexpiretime(callback, key)`

### `MIGRATE host port key destination-db timeout [COPY] [REPLACE]`

Atomically transfers a key from the source Redis instance to the destination.

*   **Coroutine:** `Reply<std::string> migrate(host, port, key, db, timeout, copy, replace)`
*   **Callback:** `redis.migrate(callback, host, port, key, db, timeout, copy, replace)`

### `OBJECT ENCODING|FREQ|IDLETIME|REFCOUNT key`

Returns internal object metadata.

*   **Coroutine:** `objectEncoding(key)`, `objectFreq(key)`, `objectIdletime(key)`, `objectRefcount(key)`
*   **Callback:** `redis.objectEncoding(callback, key)`, etc.

### `SORT key` / `SORT_RO` / `SORT STORE`

Sorts elements in a list, set, or sorted set.

*   **Coroutine:** `sortKey(key)`, `sortKeyRo(key)`, `sortKeyStore(key, destination)`
*   **Callback:** `redis.sortKey(callback, key)`, etc.

### `WAITAOF numlocal numreplicas timeout` (Redis 7.2+)

Waits for replicas to persist to AOF.

*   **Coroutine:** `Reply<long long> waitaof(num_local, num_replicas, timeout)`
*   **Callback:** `redis.waitaof(callback, num_local, num_replicas, timeout)`

### `WAIT numreplicas timeout`

Blocks the current client until all the previous write commands are successfully transferred and acknowledged by at least `numreplicas` slaves. Returns the number of slaves that acknowledged.

*   **Coroutine:** `Reply<long long> wait(num_slaves, timeout_ms)`
*   **Coroutine (chrono):** `Reply<long long> wait(num_slaves, timeout)`
*   **Callback:** `redis.wait(callback, num_slaves, timeout_ms)` or `redis.wait(callback, num_slaves, timeout)` (chrono) 