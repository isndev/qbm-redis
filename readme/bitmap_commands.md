# `qbm-redis`: Bitmap Commands

This document covers Redis commands operating on Bitmaps, which are essentially manipulations of String values at the bit level.

Reference: [Redis Bitmap Commands](https://redis.io/commands/?group=bitmap)

## Common Reply Types

*   `qb::redis::Reply<long long>`: For `BITCOUNT`, `BITPOS`, `BITFIELD`.
*   `qb::redis::Reply<bool>`: For `GETBIT`, `SETBIT` (returns the original bit value).

## Commands

### `BITCOUNT key [start end [BYTE|BIT]]`

Count the number of set bits (population count) in a string.

*   **Sync:** `Reply<long long> bitcount(const std::string &key, long long start = 0, long long end = -1)`
*   **Async:** `void bitcount_async(const std::string &key, Callback<long long> cb, long long start = 0, long long end = -1)`
*   **Note:** The `BYTE|BIT` option (Redis 7+) is not currently exposed in this API.

### `BITFIELD key [GET type offset] [SET type offset value] [INCRBY type offset increment] [OVERFLOW WRAP|SAT|FAIL]`

Performs arbitrary bitfield integer operations on a string value.

*   **Sync:** `Reply<std::vector<std::optional<long long>>> bitfield(const std::string &key, const std::vector<std::string> &operations)`
*   **Async:** `void bitfield_async(const std::string &key, const std::vector<std::string> &operations, Callback<std::vector<std::optional<long long>>> cb)`
*   **Note:** The `operations` vector should contain strings representing the subcommands (e.g., `"GET u8 100"`, `"SET i16 200 5"`, `"INCRBY i8 300 1"`). Building these operation strings is the user's responsibility.

### `BITOP operation destkey key [key ...]`

Perform bitwise operations between strings.

*   **Sync:** `Reply<long long> bitop(const std::string &operation, const std::string &destkey, const std::vector<std::string> &keys)`
*   **Async:** `void bitop_async(const std::string &operation, const std::string &destkey, const std::vector<std::string> &keys, Callback<long long> cb)`
*   **`operation`:** Can be `AND`, `OR`, `XOR`, `NOT`. Note `NOT` only takes one source key.

### `BITPOS key bit [start [end [BYTE|BIT]]]`

Find the first bit set or clear in a string.

*   **Sync:** `Reply<long long> bitpos(const std::string &key, bool bit, long long start = 0, long long end = -1)`
*   **Async:** `void bitpos_async(const std::string &key, bool bit, Callback<long long> cb, long long start = 0, long long end = -1)`
*   **Note:** The `BYTE|BIT` option (Redis 7+) is not currently exposed in this API.

### `GETBIT key offset`

Returns the bit value at `offset` in the string value stored at `key`.

*   **Sync:** `Reply<bool> getbit(const std::string &key, long long offset)`
*   **Async:** `void getbit_async(const std::string &key, long long offset, Callback<bool> cb)`

### `SETBIT key offset value`

Sets or clears the bit at `offset` in the string value stored at `key`. Returns the original bit value.

*   **Sync:** `Reply<bool> setbit(const std::string &key, long long offset, bool value)`
*   **Async:** `void setbit_async(const std::string &key, long long offset, bool value, Callback<bool> cb)` 