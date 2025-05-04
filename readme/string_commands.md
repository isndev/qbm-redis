# `qbm-redis`: String Commands

This document covers Redis commands operating on String values.

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

*   **Sync:** `Reply<long long> append(const std::string &key, const std::string &val)`
*   **Async:** `void append_async(const std::string &key, const std::string &val, Callback<long long> cb)`

```cpp
// Sync
auto reply = redis.append("mykey", " World");
if (reply) { std::cout << "New length: " << reply.value() << std::endl; }

// Async
redis.append_async("mykey", "!", [](qb::redis::Reply<long long>&& r){
    if (r) { /* ... */ }
});
```

### `DECR key`

Decrements the integer value of `key` by one. Returns the value after decrementing.

*   **Sync:** `Reply<long long> decr(const std::string &key)`
*   **Async:** `void decr_async(const std::string &key, Callback<long long> cb)`

### `DECRBY key decrement`

Decrements the integer value of `key` by `decrement`. Returns the value after decrementing.

*   **Sync:** `Reply<long long> decrby(const std::string &key, long long decrement)`
*   **Async:** `void decrby_async(const std::string &key, long long decrement, Callback<long long> cb)`

### `GET key`

Gets the value of `key`.

*   **Sync:** `Reply<std::optional<std::string>> get(const std::string &key)`
*   **Async:** `void get_async(const std::string &key, Callback<std::optional<std::string>> cb)`

```cpp
// Sync
auto reply = redis.get("mykey");
if (reply && reply.value().has_value()) {
    std::cout << "Value: " << reply.value().value() << std::endl;
} else if (reply) {
    std::cout << "Key not found." << std::endl;
}

// Async
redis.get_async("mykey", [](qb::redis::Reply<std::optional<std::string>>&& r) {
    if (r && r.value()) {
        std::cout << "Async Value: " << *r.value() << std::endl;
    }
});
```

### `GETSET key value` (Deprecated, use SET with GET option)

Sets `key` to `value` and returns the old value.

*   **Sync:** `Reply<std::optional<std::string>> getset(const std::string &key, const std::string &val)`
*   **Async:** `void getset_async(const std::string &key, const std::string &val, Callback<std::optional<std::string>> cb)`

### `INCR key`

Increments the integer value of `key` by one. Returns the value after incrementing.

*   **Sync:** `Reply<long long> incr(const std::string &key)`
*   **Async:** `void incr_async(const std::string &key, Callback<long long> cb)`

### `INCRBY key increment`

Increments the integer value of `key` by `increment`. Returns the value after incrementing.

*   **Sync:** `Reply<long long> incrby(const std::string &key, long long increment)`
*   **Async:** `void incrby_async(const std::string &key, long long increment, Callback<long long> cb)`

### `INCRBYFLOAT key increment`

Increments the float value of `key` by `increment`. Returns the value after incrementing.

*   **Sync:** `Reply<double> incrbyfloat(const std::string &key, double increment)`
*   **Async:** `void incrbyfloat_async(const std::string &key, double increment, Callback<double> cb)`

### `MGET key [key ...]`

Gets the values of all specified keys.

*   **Sync:** `Reply<std::vector<std::optional<std::string>>> mget(const std::vector<std::string> &keys)`
*   **Async:** `void mget_async(const std::vector<std::string> &keys, Callback<std::vector<std::optional<std::string>>> cb)`

### `MSET key value [key value ...]`

Sets multiple keys to multiple values.

*   **Sync:** `status mset(const std::vector<std::pair<std::string, std::string>> &keys)`
*   **Async:** `void mset_async(const std::vector<std::pair<std::string, std::string>> &keys, Callback<status> cb)`

### `MSETNX key value [key value ...]`

Sets multiple keys to multiple values only if none of the keys exist. Returns `true` if all keys were set, `false` otherwise.

*   **Sync:** `Reply<bool> msetnx(const std::vector<std::pair<std::string, std::string>> &keys)`
*   **Async:** `void msetnx_async(const std::vector<std::pair<std::string, std::string>> &keys, Callback<bool> cb)`

### `PSETEX key milliseconds value`

Sets `key` to `value` with an expiration time in milliseconds.

*   **Sync:** `status psetex(const std::string &key, long long ttl_ms, const std::string &val)`
*   **Sync (chrono):** `status psetex(const std::string &key, std::chrono::milliseconds const &ttl, const std::string &val)`
*   **Async:** `void psetex_async(const std::string &key, long long ttl_ms, const std::string &val, Callback<status> cb)`
*   **Async (chrono):** `void psetex_async(const std::string &key, std::chrono::milliseconds const &ttl, const std::string &val, Callback<status> cb)`

### `SET key value [EX seconds|PX milliseconds|EXAT timestamp|PXAT timestamp] [NX|XX] [GET]`

Sets `key` to `value`, with optional expiration and conditions.

*   **Simple Sync:** `status set(const std::string &key, const std::string &val, UpdateType type = UpdateType::ALWAYS)`
*   **Sync with TTL (seconds):** `status set(const std::string &key, const std::string &val, long long ttl_s, UpdateType type = UpdateType::ALWAYS)`
*   **Sync with TTL (chrono seconds):** `status set(const std::string &key, const std::string &val, const std::chrono::seconds &ttl, UpdateType type = UpdateType::ALWAYS)`
*   **Sync with TTL (milliseconds):** `status set(const std::string &key, const std::string &val, const std::chrono::milliseconds &ttl, UpdateType type = UpdateType::ALWAYS)`
*   **Sync with GET:** `Reply<std::optional<std::string>> set_get(const std::string &key, const std::string &val, ...)` (various TTL options)
*   **Async Simple:** `void set_async(const std::string &key, const std::string &val, Callback<status> cb)` (add `UpdateType` as needed)
*   **Async with TTL:** Similar variations to sync, taking callback as last argument.
*   **Async with GET:** Similar variations to sync, taking callback as last argument.
*   **`UpdateType` Enum:** `ALWAYS` (default), `NX` (Set only if key does not exist), `XX` (Set only if key exists).

### `SETEX key seconds value`

Sets `key` to `value` with an expiration time in seconds.

*   **Sync:** `status setex(const std::string &key, long long ttl_s, const std::string &val)`
*   **Sync (chrono):** `status setex(const std::string &key, std::chrono::seconds const &ttl, const std::string &val)`
*   **Async:** `void setex_async(const std::string &key, long long ttl_s, const std::string &val, Callback<status> cb)`
*   **Async (chrono):** `void setex_async(const std::string &key, std::chrono::seconds const &ttl, const std::string &val, Callback<status> cb)`

### `SETNX key value`

Sets `key` to `value` only if `key` does not exist. Returns `true` if the key was set, `false` otherwise.

*   **Sync:** `Reply<bool> setnx(const std::string &key, const std::string &val)`
*   **Async:** `void setnx_async(const std::string &key, const std::string &val, Callback<bool> cb)`

### `SETRANGE key offset value`

Overwrites part of the string stored at `key`, starting at the specified `offset`. Returns the length of the string after modification.

*   **Sync:** `Reply<long long> setrange(const std::string &key, long long offset, const std::string &val)`
*   **Async:** `void setrange_async(const std::string &key, long long offset, const std::string &val, Callback<long long> cb)`

### `STRLEN key`

Returns the length of the string value stored at `key`.

*   **Sync:** `Reply<long long> strlen(const std::string &key)`
*   **Async:** `void strlen_async(const std::string &key, Callback<long long> cb)`

## Core Operations

*   **`set(key, value, [ttl_ms], [UpdateType])` / `set(key, value, [ttl_seconds], [UpdateType])`**
    *   Sets the string value of a key, overwriting any existing value.
    *   **Options:**
        *   `ttl_ms` / `ttl_seconds`: Optional expiration time (milliseconds or seconds).
        *   `UpdateType::EXIST`: Only set the key if it already exists.
        *   `UpdateType::NOT_EXIST`: Only set the key if it does not already exist.
    *   **Async:** `set(callback, key, value, ...)`
    *   **Returns (Sync):** `qb::redis::status` ("OK")
    *   **Returns (Async):** `Reply<qb::redis::status>`
    *   **Example:** `redis.set("mykey", "hello", 10000); // Set with 10s TTL (ms)`

*   **`get(key)`**
    *   Retrieves the string value of a key.
    *   **Async:** `get(callback, key)`
    *   **Returns (Sync):** `std::optional<std::string>` (empty if key doesn't exist)
    *   **Returns (Async):** `Reply<std::optional<std::string>>`
    *   **Example:** `auto val = redis.get("mykey");`

*   **`append(key, value)`**
    *   Appends the given value to the end of the string at `key`. If `key` does not exist, it's created (like `SET`).
    *   **Async:** `append(callback, key, value)`
    *   **Returns (Sync):** `long long` (length of the string after append)
    *   **Returns (Async):** `Reply<long long>`
    *   **Example:** `redis.append("message", " world");`

*   **`strlen(key)`**
    *   Returns the length of the string value stored at `key`.
    *   **Async:** `strlen(callback, key)`
    *   **Returns (Sync):** `long long` (length, or 0 if key doesn't exist)
    *   **Returns (Async):** `Reply<long long>`
    *   **Example:** `long len = redis.strlen("mykey");`

*   **`getdel(key)` (Redis >= 6.2.0)**
    *   Atomically gets the value of a key and deletes the key.
    *   **Async:** `getdel(callback, key)`
    *   **Returns (Sync):** `std::optional<std::string>` (value before deletion)
    *   **Returns (Async):** `Reply<std::optional<std::string>>`
    *   **Example:** `auto old_val = redis.getdel("temp_key");`

*   **`getex(key, ttl_seconds)` / `getex(key, ttl_milliseconds)` (Redis >= 6.2.0)**
    *   Atomically gets the value of a key and sets its expiration.
    *   **Async:** `getex(callback, key, ttl_...)`
    *   **Returns (Sync):** `std::optional<std::string>` (value)
    *   **Returns (Async):** `Reply<std::optional<std::string>>`
    *   **Example:** `auto current_val = redis.getex("session_key", 3600); // Get and set 1h TTL`

## Atomic Operations

*   **`getset(key, value)`**
    *   Atomically sets `key` to `value` and returns the old value stored at `key`.
    *   **Async:** `getset(callback, key, value)`
    *   **Returns (Sync):** `std::optional<std::string>` (old value, empty if key didn't exist)
    *   **Returns (Async):** `Reply<std::optional<std::string>>`
    *   **Example:** `auto previous_val = redis.getset("config", "new_config");`

*   **`setnx(key, value)`**
    *   Sets `key` to `value` only if `key` does not already exist.
    *   **Async:** `setnx(callback, key, value)`
    *   **Returns (Sync):** `bool` (`true` if set, `false` if key already existed)
    *   **Returns (Async):** `Reply<bool>`
    *   **Example:** `bool was_set = redis.setnx("lock_key", "process_123");`

## Expiration Variants

*   **`setex(key, ttl_seconds, value)`**
    *   Sets `key` to `value` with an expiration time in seconds.
    *   Equivalent to `SET key value EX ttl_seconds`.
    *   **Async:** `setex(callback, key, ttl_seconds, value)`
    *   **Returns (Sync):** `qb::redis::status`
    *   **Returns (Async):** `Reply<qb::redis::status>`
    *   **Example:** `redis.setex("cache_key", 60, "cached_data"); // Expires in 60s`

*   **`psetex(key, ttl_milliseconds, value)`**
    *   Sets `key` to `value` with an expiration time in milliseconds.
    *   Equivalent to `SET key value PX ttl_milliseconds`.
    *   **Async:** `psetex(callback, key, ttl_milliseconds, value)`
    *   **Returns (Sync):** `qb::redis::status`
    *   **Returns (Async):** `Reply<qb::redis::status>`
    *   **Example:** `redis.psetex("short_cache", 500, "data"); // Expires in 500ms`

## Multiple Key Operations

*   **`mget(keys)`**
    *   Gets the values of all specified keys.
    *   `keys`: `std::vector<std::string>`
    *   **Async:** `mget(callback, keys)`
    *   **Returns (Sync):** `std::vector<std::optional<std::string>>` (order corresponds to input keys)
    *   **Returns (Async):** `Reply<std::vector<std::optional<std::string>>>`
    *   **Example:** `auto values = redis.mget({"key1", "key2", "non_existent"});`

*   **`mset(key_value_pairs)`**
    *   Sets multiple keys to their respective values atomically.
    *   `key_value_pairs`: `std::vector<std::pair<std::string, std::string>>`
    *   **Async:** `mset(callback, key_value_pairs)`
    *   **Returns (Sync):** `qb::redis::status`
    *   **Returns (Async):** `Reply<qb::redis::status>`
    *   **Example:** `redis.mset({{"key1", "val1"}, {"key2", "val2"}});`

*   **`msetnx(key_value_pairs)`**
    *   Sets multiple keys to their respective values atomically, but only if **none** of the specified keys already exist.
    *   `key_value_pairs`: `std::vector<std::pair<std::string, std::string>>`
    *   **Async:** `msetnx(callback, key_value_pairs)`
    *   **Returns (Sync):** `bool` (`true` if all keys were set, `false` otherwise)
    *   **Returns (Async):** `Reply<bool>`
    *   **Example:** `bool all_set = redis.msetnx({{"newkey1", "v1"}, {"newkey2", "v2"}});`

## Numeric Operations

These commands treat the string value as a number.

*   **`incr(key)`**
    *   Increments the integer value of `key` by 1.
    *   **Async:** `incr(callback, key)`
    *   **Returns (Sync):** `long long` (value after increment)
    *   **Returns (Async):** `Reply<long long>`
    *   **Example:** `long new_val = redis.incr("page_views");`

*   **`incrby(key, increment)`**
    *   Increments the integer value of `key` by `increment`.
    *   **Async:** `incrby(callback, key, increment)`
    *   **Returns (Sync):** `long long` (value after increment)
    *   **Returns (Async):** `Reply<long long>`
    *   **Example:** `redis.incrby("user_score", 10);`

*   **`incrbyfloat(key, increment)`**
    *   Increments the floating-point value of `key` by `increment`.
    *   **Async:** `incrbyfloat(callback, key, increment)`
    *   **Returns (Sync):** `double` (value after increment)
    *   **Returns (Async):** `Reply<double>`
    *   **Example:** `double new_rating = redis.incrbyfloat("product_rating", 0.5);`

*   **`decr(key)`**
    *   Decrements the integer value of `key` by 1.
    *   **Async:** `decr(callback, key)`
    *   **Returns (Sync):** `long long` (value after decrement)
    *   **Returns (Async):** `Reply<long long>`
    *   **Example:** `redis.decr("items_in_stock");`

*   **`decrby(key, decrement)`**
    *   Decrements the integer value of `key` by `decrement`.
    *   **Async:** `decrby(callback, key, decrement)`
    *   **Returns (Sync):** `long long` (value after decrement)
    *   **Returns (Async):** `Reply<long long>`
    *   **Example:** `redis.decrby("available_tickets", 5);`

## Substring Operations

*   **`getrange(key, start, end)`**
    *   Returns the substring specified by the start and end offsets (inclusive).
    *   Offsets can be negative (e.g., -1 is the last character).
    *   **Async:** `getrange(callback, key, start, end)`
    *   **Returns (Sync):** `std::string` (the substring)
    *   **Returns (Async):** `Reply<std::string>`
    *   **Example:** `std::string last_5 = redis.getrange("log_message", -5, -1);`

*   **`setrange(key, offset, value)`**
    *   Overwrites part of the string at `key` starting at the specified `offset`.
    *   If `key` doesn't exist, it's created. If the offset is beyond the end, the string is padded with null bytes.
    *   **Async:** `setrange(callback, key, offset, value)`
    *   **Returns (Sync):** `long long` (length of the string after modification)
    *   **Returns (Async):** `Reply<long long>`
    *   **Example:** `redis.setrange("my_email", 0, "new_"); // Changes my@domain to new_@domain`

## Advanced

*   **`lcs(key1, key2, [options...])` (Redis >= 7.0.0)**
    *   Finds the longest common subsequence between two strings.
    *   Supports options like `LEN` (return length only), `IDX` (return match indices), `MINMATCHLEN`, `WITHMATCHLEN`.
    *   **Async:** `lcs(callback, key1, key2, ...)`
    *   **Returns (Sync):** `std::string` (LCS string) or `long long` (LCS length) or complex structure (with IDX).
    *   **Returns (Async):** `Reply<T>` where T depends on options.
    *   **Example:** `std::string common = redis.lcs("string1", "string2");` 