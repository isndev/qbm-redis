# `qbm-redis`: Set Commands

This document covers Redis commands operating on Set values. Redis Sets are unordered collections of unique strings.

Reference: [Redis Set Commands](https://redis.io/commands/?group=set)

## Common Reply Types

*   `qb::redis::Reply<long long>`: For counts (e.g., `SADD`, `SCARD`, `SREM`).
*   `qb::redis::Reply<bool>`: For membership checks (`SISMEMBER`) or moves (`SMOVE`).
*   `qb::redis::Reply<std::vector<std::string>>`: For commands returning multiple members (e.g., `SMEMBERS`, `SDIFF`, `SINTER`, `SUNION`, `SRANDMEMBER count`).
*   `qb::redis::Reply<std::optional<std::string>>`: For `SPOP` (single) and `SRANDMEMBER` (single).
*   `qb::redis::Reply<std::vector<std::optional<std::string>>>`: For `SPOP count`.
*   `qb::redis::Reply<qb::redis::scan<std::vector<std::string>>>`: For `SSCAN`.

## Commands

### `SADD key member [member ...]`

Adds the specified members to the set stored at `key`. Returns the number of members that were added (excluding members already present).

*   **Sync:** `Reply<long long> sadd(const std::string &key, Members &&...members)` (Variadic template)
*   **Async:** `void sadd_async(const std::string &key, std::vector<std::string> members, Callback<long long> cb)`

### `SCARD key`

Returns the set cardinality (number of members).

*   **Sync:** `Reply<long long> scard(const std::string &key)`
*   **Async:** `void scard_async(const std::string &key, Callback<long long> cb)`

### `SDIFF key [key ...]`

Returns the members of the set resulting from the difference between the first set and all the successive sets.

*   **Sync:** `Reply<std::vector<std::string>> sdiff(const std::vector<std::string> &keys)`
*   **Async:** `void sdiff_async(const std::vector<std::string> &keys, Callback<std::vector<std::string>> cb)`

### `SDIFFSTORE destination key [key ...]`

Stores the members of the set resulting from the difference between the first set and all the successive sets in `destination`. Returns the number of members in the resulting set.

*   **Sync:** `Reply<long long> sdiffstore(const std::string &destination, const std::vector<std::string> &keys)`
*   **Async:** `void sdiffstore_async(const std::string &destination, const std::vector<std::string> &keys, Callback<long long> cb)`

### `SINTER key [key ...]`

Returns the members of the set resulting from the intersection of all the given sets.

*   **Sync:** `Reply<std::vector<std::string>> sinter(const std::vector<std::string> &keys)`
*   **Async:** `void sinter_async(const std::vector<std::string> &keys, Callback<std::vector<std::string>> cb)`

### `SINTERCARD numkeys key [key ...] [LIMIT limit]`

Returns the cardinality of the intersection of multiple sets. `LIMIT` (optional) can stop computation early if the cardinality reaches the limit, saving time (0 means no limit).

*   **Sync:** `Reply<long long> sintercard(const std::vector<std::string> &keys, std::optional<long long> limit = std::nullopt)`
*   **Async:** `void sintercard_async(const std::vector<std::string> &keys, Callback<long long> cb, std::optional<long long> limit = std::nullopt)`

### `SINTERSTORE destination key [key ...]`

Stores the members of the set resulting from the intersection of all the given sets in `destination`. Returns the number of members in the resulting set.

*   **Sync:** `Reply<long long> sinterstore(const std::string &destination, const std::vector<std::string> &keys)`
*   **Async:** `void sinterstore_async(const std::string &destination, const std::vector<std::string> &keys, Callback<long long> cb)`

### `SISMEMBER key member`

Returns if `member` is a member of the set stored at `key`.

*   **Sync:** `Reply<bool> sismember(const std::string &key, const std::string &member)`
*   **Async:** `void sismember_async(const std::string &key, const std::string &member, Callback<bool> cb)`

### `SMISMEMBER key member [member ...]`

Returns an array indicating if each corresponding `member` is a member of the set at `key` (1 for member, 0 for non-member).

*   **Sync:** `Reply<std::vector<bool>> smismember(const std::string &key, const std::vector<std::string> &members)`
*   **Async:** `void smismember_async(const std::string &key, const std::vector<std::string> &members, Callback<std::vector<bool>> cb)`

### `SMEMBERS key`

Returns all the members of the set stored at `key`.

*   **Sync:** `Reply<std::vector<std::string>> smembers(const std::string &key)`
*   **Async:** `void smembers_async(const std::string &key, Callback<std::vector<std::string>> cb)`

### `SMOVE source destination member`

Moves `member` from set `source` to set `destination`. Returns `true` if moved, `false` otherwise.

*   **Sync:** `Reply<bool> smove(const std::string &source, const std::string &destination, const std::string &member)`
*   **Async:** `void smove_async(const std::string &source, const std::string &destination, const std::string &member, Callback<bool> cb)`

### `SPOP key [count]`

Removes and returns one or more random members from the set stored at `key`.

*   **Sync (Single):** `Reply<std::optional<std::string>> spop(const std::string &key)`
*   **Sync (Multiple):** `Reply<std::vector<std::optional<std::string>>> spop(const std::string &key, long long count)`
*   **Async (Single):** `void spop_async(const std::string &key, Callback<std::optional<std::string>> cb)`
*   **Async (Multiple):** `void spop_async(const std::string &key, long long count, Callback<std::vector<std::optional<std::string>>> cb)`

### `SRANDMEMBER key [count]`

Returns one or more random members from the set without removing them.

*   **Sync (Single):** `Reply<std::optional<std::string>> srandmember(const std::string &key)`
*   **Sync (Multiple):** `Reply<std::vector<std::string>> srandmember(const std::string &key, long long count)`
*   **Async (Single):** `void srandmember_async(const std::string &key, Callback<std::optional<std::string>> cb)`
*   **Async (Multiple):** `void srandmember_async(const std::string &key, long long count, Callback<std::vector<std::string>> cb)`

### `SREM key member [member ...]`

Removes the specified members from the set stored at `key`. Returns the number of members that were removed.

*   **Sync:** `Reply<long long> srem(const std::string &key, Members &&...members)` (Variadic template)
*   **Async:** `void srem_async(const std::string &key, std::vector<std::string> members, Callback<long long> cb)`

### `SSCAN key cursor [MATCH pattern] [COUNT count]`

Iterates members of Set types.

*   **Sync:** `Reply<scan<std::vector<std::string>>> sscan(const std::string &key, long long cursor, const std::optional<std::string>& pattern = std::nullopt, const std::optional<long long>& count = std::nullopt)`
*   **Async:** `void sscan_async(const std::string &key, long long cursor, Callback<scan<std::vector<std::string>>> cb, const std::optional<std::string>& pattern = std::nullopt, const std::optional<long long>& count = std::nullopt)`
*   **Note:** The `scan` struct contains `cursor` (long long) and `elements` (the vector).

### `SUNION key [key ...]`

Returns the members of the set resulting from the union of all the given sets.

*   **Sync:** `Reply<std::vector<std::string>> sunion(const std::vector<std::string> &keys)`
*   **Async:** `void sunion_async(const std::vector<std::string> &keys, Callback<std::vector<std::string>> cb)`

### `SUNIONSTORE destination key [key ...]`

Stores the members of the set resulting from the union of all the given sets in `destination`. Returns the number of members in the resulting set.

*   **Sync:** `Reply<long long> sunionstore(const std::string &destination, const std::vector<std::string> &keys)`
*   **Async:** `void sunionstore_async(const std::string &destination, const std::vector<std::string> &keys, Callback<long long> cb)` 