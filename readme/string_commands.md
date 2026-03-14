# `qbm-redis`: String Commands

This document covers Redis commands operating on String values.

**API:** All commands support **coroutine** (`Reply<T> r = co_await redis.cmd(...)`) and **callback** (`redis.cmd(callback, ...)`). No `_async` suffix.

Reference: [Redis String Commands](https://redis.io/commands/?group=string)

## Common Reply Types

*   `qb::redis::status`: For commands returning simple "OK". Check `reply.ok()`.
*   `qb::redis::Reply<long long>`: For numeric results (e.g., `INCR`, `STRLEN`). Access via `reply.value()`.
*   `qb::redis::Reply<double>`: For float results (e.g., `INCRBYFLOAT`). Access via `reply.value()`.
*   `qb::redis::Reply<std::optional<std::string>>`: For commands like `GET` that might return `nil`. Check `reply.ok()` then `reply.value().has_value()` before using `reply.value().value()`.
*   `qb::redis::Reply<bool>`: For commands returning 0 or 1 (e.g., `SETNX`, `MSETNX`). Access via `reply.value()`.
*   `qb::redis::Reply<std::vector<std::optional<std::string>>>`: For `MGET`.

## Commands

### `APPEND key value`

Appends `value` to the string at `key`. Returns the length of the string after appending.

*   **Coroutine:** `Reply<long long> r = co_await redis.append(key, val)`
*   **Callback:** `redis.append(callback, key, val)`

```cpp
// Coroutine
auto reply = co_await redis.append("mykey", " World");
if (reply.ok()) { std::cout << "New length: " << reply.result() << std::endl; }

// Callback
redis.append([](qb::redis::Reply<long long>&& r){ if (r.ok()) { /* ... */ } }, "mykey", "!");
```

### `DECR key`

Decrements the integer value of `key` by one. Returns the value after decrementing.

*   **Coroutine:** `Reply<long long> r = co_await redis.decr(key)`
*   **Callback:** `redis.decr(callback, key)`

### `DECRBY key decrement`

Decrements the integer value of `key` by `decrement`. Returns the value after decrementing.

*   **Coroutine:** `Reply<long long> r = co_await redis.decrby(key, decrement)`
*   **Callback:** `redis.decrby(callback, key, decrement)`

### `GET key`

Gets the value of `key`.

*   **Coroutine:** `Reply<std::optional<std::string>> r = co_await redis.get(key)`
*   **Callback:** `redis.get(callback, key)`

```cpp
// Coroutine
auto reply = co_await redis.get("mykey");
if (reply.ok() && reply.result().has_value()) {
    std::cout << "Value: " << *reply.result() << std::endl;
} else if (reply.ok()) {
    std::cout << "Key not found." << std::endl;
}

// Callback
redis.get([](qb::redis::Reply<std::optional<std::string>>&& r) {
    if (r.ok() && r.result().has_value()) {
        std::cout << "Value: " << *r.result() << std::endl;
    }
}, "mykey");
```

### `GETSET key value` (Deprecated, use SET with GET option)

Sets `key` to `value` and returns the old value.

*   **Coroutine:** `Reply<std::optional<std::string>> getset(const std::string &key, const std::string &val)`
*   **Callback:** `redis.getset(callback, key, val)`

### `INCR key`

Increments the integer value of `key` by one. Returns the value after incrementing.

*   **Coroutine:** `Reply<long long> incr(const std::string &key)`
*   **Callback:** `redis.incr(callback, key)`

### `INCRBY key increment`

Increments the integer value of `key` by `increment`. Returns the value after incrementing.

*   **Coroutine:** `Reply<long long> incrby(const std::string &key, long long increment)`
*   **Callback:** `redis.incrby(callback, key, increment)`

### `INCRBYFLOAT key increment`

Increments the float value of `key` by `increment`. Returns the value after incrementing.

*   **Coroutine:** `Reply<double> incrbyfloat(const std::string &key, double increment)`
*   **Callback:** `redis.incrbyfloat(callback, key, increment)`

### `MGET key [key ...]`

Gets the values of all specified keys.

*   **Coroutine:** `Reply<std::vector<std::optional<std::string>>> mget(const std::vector<std::string> &keys)`
*   **Callback:** `redis.mget(callback, keys)`

### `MSET key value [key value ...]`

Sets multiple keys to multiple values.

*   **Coroutine:** `status mset(const std::vector<std::pair<std::string, std::string>> &keys)`
*   **Callback:** `redis.mset(callback, keys)`

### `MSETNX key value [key value ...]`

Sets multiple keys to multiple values only if none of the keys exist. Returns `true` if all keys were set, `false` otherwise.

*   **Coroutine:** `Reply<bool> msetnx(const std::vector<std::pair<std::string, std::string>> &keys)`
*   **Callback:** `redis.msetnx(callback, keys)`

### `PSETEX key milliseconds value`

Sets `key` to `value` with an expiration time in milliseconds.

*   **Coroutine:** `status psetex(const std::string &key, long long ttl_ms, const std::string &val)`
*   **Sync (chrono):** `status psetex(const std::string &key, std::chrono::milliseconds const &ttl, const std::string &val)`
*   **Callback:** `redis.psetex(callback, key, ttl_ms, val)` or `redis.psetex(callback, key, ttl, val)` (chrono)

### `SET key value [EX seconds|PX milliseconds|EXAT timestamp|PXAT timestamp] [NX|XX] [GET]`

Sets `key` to `value`, with optional expiration and conditions.

*   **Simple Sync:** `status set(const std::string &key, const std::string &val, UpdateType type = UpdateType::ALWAYS)`
*   **Sync with TTL (seconds):** `status set(const std::string &key, const std::string &val, long long ttl_s, UpdateType type = UpdateType::ALWAYS)`
*   **Sync with TTL (chrono seconds):** `status set(const std::string &key, const std::string &val, const std::chrono::seconds &ttl, UpdateType type = UpdateType::ALWAYS)`
*   **Sync with TTL (milliseconds):** `status set(const std::string &key, const std::string &val, const std::chrono::milliseconds &ttl, UpdateType type = UpdateType::ALWAYS)`
*   **Sync with GET:** `Reply<std::optional<std::string>> set_get(const std::string &key, const std::string &val, ...)` (various TTL options)
*   **Callback Simple:** `redis.set(callback, key, val)` (add `UpdateType` as needed)
*   **Async with TTL:** Similar variations to sync, taking callback as last argument.
*   **Async with GET:** Similar variations to sync, taking callback as last argument.
*   **`UpdateType` Enum:** `ALWAYS` (default), `NX` (Set only if key does not exist), `XX` (Set only if key exists).

### `SETEX key seconds value`

Sets `key` to `value` with an expiration time in seconds.

*   **Coroutine:** `status setex(const std::string &key, long long ttl_s, const std::string &val)`
*   **Sync (chrono):** `status setex(const std::string &key, std::chrono::seconds const &ttl, const std::string &val)`
*   **Callback:** `redis.setex(callback, key, ttl_s, val)` or `redis.setex(callback, key, ttl, val)` (chrono)

### `SETNX key value`

Sets `key` to `value` only if `key` does not exist. Returns `true` if the key was set, `false` otherwise.

*   **Coroutine:** `Reply<bool> setnx(const std::string &key, const std::string &val)`
*   **Callback:** `redis.setnx(callback, key, val)`

### `SETRANGE key offset value`

Overwrites part of the string stored at `key`, starting at the specified `offset`. Returns the length of the string after modification.

*   **Coroutine:** `Reply<long long> setrange(const std::string &key, long long offset, const std::string &val)`
*   **Callback:** `redis.setrange(callback, key, offset, val)`

### `STRLEN key`

Returns the length of the string value stored at `key`.

*   **Coroutine:** `Reply<long long> strlen(const std::string &key)`
*   **Callback:** `redis.strlen(callback, key)`

## Core Operations

*   **`set(key, value, [ttl_ms], [UpdateType])` / `set(key, value, [ttl_seconds], [UpdateType])`**
    *   Sets the string value of a key, overwriting any existing value.
    *   **Options:**
        *   `ttl_ms` / `ttl_seconds`: Optional expiration time (milliseconds or seconds).
        *   `UpdateType::EXIST`: Only set the key if it already exists.
        *   `UpdateType::NOT_EXIST`: Only set the key if it does not already exist.
    *   **Callback:** `set(callback, key, value, ...)`
    *   **Returns (Sync):** `qb::redis::status` ("OK")
    *   **Returns (Async):** `Reply<qb::redis::status>`
    *   **Example:** `redis.set("mykey", "hello", 10000); // Set with 10s TTL (ms)`

*   **`get(key)`**
    *   Retrieves the string value of a key.
    *   **Callback:** `get(callback, key)`
    *   **Returns (Sync):** `std::optional<std::string>` (empty if key doesn't exist)
    *   **Returns (Async):** `Reply<std::optional<std::string>>`
    *   **Example:** `auto val = redis.get("mykey");`

*   **`append(key, value)`**
    *   Appends the given value to the end of the string at `key`. If `key` does not exist, it's created (like `SET`).
    *   **Callback:** `append(callback, key, value)`
    *   **Returns (Sync):** `long long` (length of the string after append)
    *   **Returns (Async):** `Reply<long long>`
    *   **Example:** `redis.append("message", " world");`

*   **`strlen(key)`**
    *   Returns the length of the string value stored at `key`.
    *   **Callback:** `strlen(callback, key)`
    *   **Returns (Sync):** `long long` (length, or 0 if key doesn't exist)
    *   **Returns (Async):** `Reply<long long>`
    *   **Example:** `long len = redis.strlen("mykey");`

*   **`getdel(key)` (Redis >= 6.2.0)**
    *   Atomically gets the value of a key and deletes the key.
    *   **Callback:** `getdel(callback, key)`
    *   **Returns (Sync):** `std::optional<std::string>` (value before deletion)
    *   **Returns (Async):** `Reply<std::optional<std::string>>`
    *   **Example:** `auto old_val = redis.getdel("temp_key");`

*   **`getex(key, ttl_seconds)` / `getex(key, ttl_milliseconds)` (Redis >= 6.2.0)**
    *   Atomically gets the value of a key and sets its expiration.
    *   **Callback:** `getex(callback, key, ttl_...)`
    *   **Returns (Sync):** `std::optional<std::string>` (value)
    *   **Returns (Async):** `Reply<std::optional<std::string>>`
    *   **Example:** `auto current_val = redis.getex("session_key", 3600); // Get and set 1h TTL`

## Atomic Operations

*   **`getset(key, value)`**
    *   Atomically sets `key` to `value` and returns the old value stored at `key`.
    *   **Callback:** `getset(callback, key, value)`
    *   **Returns (Sync):** `std::optional<std::string>` (old value, empty if key didn't exist)
    *   **Returns (Async):** `Reply<std::optional<std::string>>`
    *   **Example:** `auto previous_val = redis.getset("config", "new_config");`

*   **`setnx(key, value)`**
    *   Sets `key` to `value` only if `key` does not already exist.
    *   **Callback:** `setnx(callback, key, value)`
    *   **Returns (Sync):** `bool` (`true` if set, `false` if key already existed)
    *   **Returns (Async):** `Reply<bool>`
    *   **Example:** `bool was_set = redis.setnx("lock_key", "process_123");`

## Expiration Variants

*   **`setex(key, ttl_seconds, value)`**
    *   Sets `key` to `value` with an expiration time in seconds.
    *   Equivalent to `SET key value EX ttl_seconds`.
    *   **Callback:** `setex(callback, key, ttl_seconds, value)`
    *   **Returns (Sync):** `qb::redis::status`
    *   **Returns (Async):** `Reply<qb::redis::status>`
    *   **Example:** `redis.setex("cache_key", 60, "cached_data"); // Expires in 60s`

*   **`psetex(key, ttl_milliseconds, value)`**
    *   Sets `key` to `value` with an expiration time in milliseconds.
    *   Equivalent to `SET key value PX ttl_milliseconds`.
    *   **Callback:** `psetex(callback, key, ttl_milliseconds, value)`
    *   **Returns (Sync):** `qb::redis::status`
    *   **Returns (Async):** `Reply<qb::redis::status>`
    *   **Example:** `redis.psetex("short_cache", 500, "data"); // Expires in 500ms`

## Multiple Key Operations

*   **`mget(keys)`**
    *   Gets the values of all specified keys.
    *   `keys`: `std::vector<std::string>`
    *   **Callback:** `mget(callback, keys)`
    *   **Returns (Sync):** `std::vector<std::optional<std::string>>` (order corresponds to input keys)
    *   **Returns (Async):** `Reply<std::vector<std::optional<std::string>>>`
    *   **Example:** `auto values = redis.mget({"key1", "key2", "non_existent"});`

*   **`mset(key_value_pairs)`**
    *   Sets multiple keys to their respective values atomically.
    *   `key_value_pairs`: `std::vector<std::pair<std::string, std::string>>`
    *   **Callback:** `mset(callback, key_value_pairs)`
    *   **Returns (Sync):** `qb::redis::status`
    *   **Returns (Async):** `Reply<qb::redis::status>`
    *   **Example:** `redis.mset({{"key1", "val1"}, {"key2", "val2"}});`

*   **`msetnx(key_value_pairs)`**
    *   Sets multiple keys to their respective values atomically, but only if **none** of the specified keys already exist.
    *   `key_value_pairs`: `std::vector<std::pair<std::string, std::string>>`
    *   **Callback:** `msetnx(callback, key_value_pairs)`
    *   **Returns (Sync):** `bool` (`true` if all keys were set, `false` otherwise)
    *   **Returns (Async):** `Reply<bool>`
    *   **Example:** `bool all_set = redis.msetnx({{"newkey1", "v1"}, {"newkey2", "v2"}});`

## Numeric Operations

These commands treat the string value as a number.

*   **`incr(key)`**
    *   Increments the integer value of `key` by 1.
    *   **Callback:** `incr(callback, key)`
    *   **Returns (Sync):** `long long` (value after increment)
    *   **Returns (Async):** `Reply<long long>`
    *   **Example:** `long new_val = redis.incr("page_views");`

*   **`incrby(key, increment)`**
    *   Increments the integer value of `key` by `increment`.
    *   **Callback:** `incrby(callback, key, increment)`
    *   **Returns (Sync):** `long long` (value after increment)
    *   **Returns (Async):** `Reply<long long>`
    *   **Example:** `redis.incrby("user_score", 10);`

*   **`incrbyfloat(key, increment)`**
    *   Increments the floating-point value of `key` by `increment`.
    *   **Callback:** `incrbyfloat(callback, key, increment)`
    *   **Returns (Sync):** `double` (value after increment)
    *   **Returns (Async):** `Reply<double>`
    *   **Example:** `double new_rating = redis.incrbyfloat("product_rating", 0.5);`

*   **`decr(key)`**
    *   Decrements the integer value of `key` by 1.
    *   **Callback:** `decr(callback, key)`
    *   **Returns (Sync):** `long long` (value after decrement)
    *   **Returns (Async):** `Reply<long long>`
    *   **Example:** `redis.decr("items_in_stock");`

*   **`decrby(key, decrement)`**
    *   Decrements the integer value of `key` by `decrement`.
    *   **Callback:** `decrby(callback, key, decrement)`
    *   **Returns (Sync):** `long long` (value after decrement)
    *   **Returns (Async):** `Reply<long long>`
    *   **Example:** `redis.decrby("available_tickets", 5);`

## Substring Operations

*   **`getrange(key, start, end)`**
    *   Returns the substring specified by the start and end offsets (inclusive).
    *   Offsets can be negative (e.g., -1 is the last character).
    *   **Callback:** `getrange(callback, key, start, end)`
    *   **Returns (Sync):** `std::string` (the substring)
    *   **Returns (Async):** `Reply<std::string>`
    *   **Example:** `std::string last_5 = redis.getrange("log_message", -5, -1);`

*   **`setrange(key, offset, value)`**
    *   Overwrites part of the string at `key` starting at the specified `offset`.
    *   **Coroutine:** `co_await redis.setrange(key, offset, value)`
    *   **Callback:** `redis.setrange(callback, key, offset, value)`
    *   **Returns:** `Reply<long long>` (length after modification)

*   **`substr(key, start, end)`** (deprecated alias for `getrange`)
    *   Returns the substring specified by start and end offsets (inclusive).
    *   **Coroutine:** `co_await redis.substr(key, start, end)`
    *   **Callback:** `redis.substr(callback, key, start, end)`
    *   **Returns:** `Reply<std::string>`

## Advanced

*   **`lcs(key1, key2, [options...])` (Redis >= 7.0.0)**
    *   Finds the longest common subsequence between two strings.
    *   Supports options like `LEN` (return length only), `IDX` (return match indices), `MINMATCHLEN`, `WITHMATCHLEN`.
    *   **Callback:** `lcs(callback, key1, key2, ...)`
    *   **Returns (Sync):** `std::string` (LCS string) or `long long` (LCS length) or complex structure (with IDX).
    *   **Returns (Async):** `Reply<T>` where T depends on options.
    *   **Example:** `std::string common = redis.lcs("string1", "string2");` 