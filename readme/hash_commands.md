# `qbm-redis`: Hash Commands

This document covers Redis commands operating on Hash values. Redis Hashes are maps between string fields and string values.

Reference: [Redis Hash Commands](https://redis.io/commands/?group=hash)

## Common Reply Types

*   `qb::redis::status`: For commands returning simple "OK" (e.g., `HSET` on older Redis versions, though newer versions return integer).
*   `qb::redis::Reply<long long>`: For counts or integer results (e.g., `HDEL`, `HLEN`, `HSTRLEN`).
*   `qb::redis::Reply<double>`: For float results (e.g., `HINCRBYFLOAT`).
*   `qb::redis::Reply<std::optional<std::string>>`: For `HGET` which might return `nil`.
*   `qb::redis::Reply<bool>`: For commands returning 0 or 1 (e.g., `HEXISTS`, `HSETNX`).
*   `qb::redis::Reply<std::vector<std::string>>`: For `HKEYS`, `HVALS`.
*   `qb::redis::Reply<std::vector<std::optional<std::string>>>`: For `HMGET`.
*   `qb::redis::Reply<qb::unordered_map<std::string, std::string>>`: For `HGETALL`.
*   `qb::redis::Reply<qb::redis::scan<qb::unordered_map<std::string, std::string>>>`: For `HSCAN`.

## Commands

### `HDEL key field [field ...]`

Removes the specified fields from the hash stored at `key`. Returns the number of fields that were removed.

*   **Sync:** `Reply<long long> hdel(const std::string &key, Fields &&...fields)` (Variadic template)
*   **Async:** `void hdel_async(const std::string &key, std::vector<std::string> fields, Callback<long long> cb)`

### `HEXISTS key field`

Returns if `field` is an existing field in the hash stored at `key`.

*   **Sync:** `Reply<bool> hexists(const std::string &key, const std::string &field)`
*   **Async:** `void hexists_async(const std::string &key, const std::string &field, Callback<bool> cb)`

### `HGET key field`

Returns the value associated with `field` in the hash stored at `key`.

*   **Sync:** `Reply<std::optional<std::string>> hget(const std::string &key, const std::string &field)`
*   **Async:** `void hget_async(const std::string &key, const std::string &field, Callback<std::optional<std::string>> cb)`

### `HGETALL key`

Returns all fields and values of the hash stored at `key`.

*   **Sync:** `Reply<qb::unordered_map<std::string, std::string>> hgetall(const std::string &key)`
*   **Async:** `void hgetall_async(const std::string &key, Callback<qb::unordered_map<std::string, std::string>> cb)`

### `HINCRBY key field increment`

Increments the integer value of `field` in the hash by `increment`. Returns the value after incrementing.

*   **Sync:** `Reply<long long> hincrby(const std::string &key, const std::string &field, long long increment)`
*   **Async:** `void hincrby_async(const std::string &key, const std::string &field, long long increment, Callback<long long> cb)`

### `HINCRBYFLOAT key field increment`

Increments the float value of `field` in the hash by `increment`. Returns the value after incrementing.

*   **Sync:** `Reply<double> hincrbyfloat(const std::string &key, const std::string &field, double increment)`
*   **Async:** `void hincrbyfloat_async(const std::string &key, const std::string &field, double increment, Callback<double> cb)`

### `HKEYS key`

Returns all field names in the hash stored at `key`.

*   **Sync:** `Reply<std::vector<std::string>> hkeys(const std::string &key)`
*   **Async:** `void hkeys_async(const std::string &key, Callback<std::vector<std::string>> cb)`

### `HLEN key`

Returns the number of fields contained in the hash stored at `key`.

*   **Sync:** `Reply<long long> hlen(const std::string &key)`
*   **Async:** `void hlen_async(const std::string &key, Callback<long long> cb)`

### `HMGET key field [field ...]`

Returns the values associated with the specified fields in the hash stored at `key`.

*   **Sync:** `Reply<std::vector<std::optional<std::string>>> hmget(const std::string &key, const std::vector<std::string> &fields)`
*   **Async:** `void hmget_async(const std::string &key, const std::vector<std::string> &fields, Callback<std::vector<std::optional<std::string>>> cb)`

### `HMSET key field value [field value ...]` (Deprecated, use HSET multiple times or with multiple pairs)

Sets multiple fields to multiple values.

*   **Sync:** `status hmset(const std::string &key, const std::vector<std::pair<std::string, std::string>> &items)`
*   **Async:** `void hmset_async(const std::string &key, const std::vector<std::pair<std::string, std::string>> &items, Callback<status> cb)`

### `HSCAN key cursor [MATCH pattern] [COUNT count]`

Iterates fields of Hash types and their associated values.

*   **Sync:** `Reply<scan<qb::unordered_map<std::string, std::string>>> hscan(const std::string &key, long long cursor, const std::optional<std::string>& pattern = std::nullopt, const std::optional<long long>& count = std::nullopt)`
*   **Async:** `void hscan_async(const std::string &key, long long cursor, Callback<scan<qb::unordered_map<std::string, std::string>>> cb, const std::optional<std::string>& pattern = std::nullopt, const std::optional<long long>& count = std::nullopt)`
*   **Note:** The `scan` struct contains `cursor` (long long) and `elements` (the map).

### `HSET key field value [field value ...]`

Sets the specified fields to their respective values in the hash stored at `key`. Returns the number of fields that were added.

*   **Sync (Single):** `Reply<long long> hset(const std::string &key, const std::string &field, const std::string &val)`
*   **Sync (Single Pair):** `Reply<bool> hset(const std::string &key, const std::pair<std::string, std::string> &item)`
*   **Sync (Multiple Pairs):** `Reply<long long> hset(const std::string &key, const std::vector<std::pair<std::string, std::string>> &items)`
*   **Async (Single):** `void hset_async(const std::string &key, const std::string &field, const std::string &val, Callback<long long> cb)`
*   **Async (Single Pair):** `void hset_async(const std::string &key, const std::pair<std::string, std::string> &item, Callback<bool> cb)`
*   **Async (Multiple Pairs):** `void hset_async(const std::string &key, const std::vector<std::pair<std::string, std::string>> &items, Callback<long long> cb)`

### `HSETNX key field value`

Sets `field` in the hash stored at `key` to `value`, only if `field` does not yet exist. Returns `true` if field was set, `false` otherwise.

*   **Sync (Single):** `Reply<bool> hsetnx(const std::string &key, const std::string &field, const std::string &val)`
*   **Sync (Single Pair):** `Reply<bool> hsetnx(const std::string &key, const std::pair<std::string, std::string> &item)`
*   **Async (Single):** `void hsetnx_async(const std::string &key, const std::string &field, const std::string &val, Callback<bool> cb)`
*   **Async (Single Pair):** `void hsetnx_async(const std::string &key, const std::pair<std::string, std::string> &item, Callback<bool> cb)`

### `HSTRLEN key field`

Returns the string length of the value associated with `field` in the hash stored at `key`.

*   **Sync:** `Reply<long long> hstrlen(const std::string &key, const std::string &field)`
*   **Async:** `void hstrlen_async(const std::string &key, const std::string &field, Callback<long long> cb)`

### `HVALS key`

Returns all values in the hash stored at `key`.

*   **Sync:** `Reply<std::vector<std::string>> hvals(const std::string &key)`
*   **Async:** `void hvals_async(const std::string &key, Callback<std::vector<std::string>> cb)` 