# `qbm-redis`: Sorted Set Commands

This document covers Redis commands operating on Sorted Set values (also known as ZSETs). Sorted Sets are collections of unique strings (members) where each member is associated with a floating-point score. Members are ordered by their score.

Reference: [Redis Sorted Set Commands](https://redis.io/commands/?group=sorted-set)

## Common Types & Reply Types

*   **`qb::redis::score_member` Struct:** `{ double score; std::string member; }`
*   **`qb::redis::Interval` Struct:** Represents score or lexicographical ranges (e.g., `Interval("(1", "(5")`, `Interval("[alpha", "[omega")`). See `types.h` for constructors.
*   `qb::redis::status`: For commands returning simple "OK".
*   `qb::redis::Reply<long long>`: For counts (`ZCARD`, `ZADD`, `ZREM`, etc.) or ranks (`ZRANK`).
*   `qb::redis::Reply<std::optional<double>>`: For `ZSCORE`.
*   `qb::redis::Reply<double>`: For `ZINCRBY`.
*   `qb::redis::Reply<std::vector<std::string>>`: For commands returning members only (e.g., `ZRANGE` without scores).
*   `qb::redis::Reply<std::vector<qb::redis::score_member>>`: For commands returning members with scores (e.g., `ZRANGE WITHSCORES`).
*   `qb::redis::Reply<std::optional<std::vector<std::string>>>`: For blocking pops (`BZPOP*`, `BZMP*`).
*   `qb::redis::Reply<std::optional<qb::redis::sorted_set_pop_result>>`: For `ZMP*`, `BZMP*`. Contains `key`, `elements` (vector of `score_member`).
*   `qb::redis::Reply<qb::redis::scan<std::vector<qb::redis::score_member>>>`: For `ZSCAN`.

## Commands

### `ZADD key [NX|XX] [GT|LT] [CH] [INCR] score member [score member ...]`

Adds members with scores to the sorted set. Returns the number of elements added (not updated).

*   **Sync:** `Reply<long long> zadd(const std::string &key, const std::vector<score_member> &members, UpdateType type = UpdateType::ALWAYS, bool changed = false)`
*   **Async:** `void zadd_async(const std::string &key, const std::vector<score_member> &members, Callback<long long> cb, UpdateType type = UpdateType::ALWAYS, bool changed = false)`
*   **Options (via `UpdateType` enum):**
    *   `ALWAYS` (Default)
    *   `NX`: Only add new elements, don't update existing ones.
    *   `XX`: Only update existing elements, don't add new ones.
    *   `GT`: Only update existing elements if the new score is greater.
    *   `LT`: Only update existing elements if the new score is less.
*   **Options (bool flags):**
    *   `changed`: Modifies the return value to number of *changed* elements.
    *   `INCR` flag is handled by `ZINCRBY`.

### `ZCARD key`

Returns the sorted set cardinality (number of members).

*   **Sync:** `Reply<long long> zcard(const std::string &key)`
*   **Async:** `void zcard_async(const std::string &key, Callback<long long> cb)`

### `ZCOUNT key min max`

Counts the members in a sorted set within a given score range.

*   **Sync:** `Reply<long long> zcount(const std::string &key, const Interval &interval)`
*   **Async:** `void zcount_async(const std::string &key, const Interval &interval, Callback<long long> cb)`
*   **`Interval`:** Use constructors like `Interval(min_score, max_score)` or `Interval("(1.0", "[5.0")`.

### `ZINCRBY key increment member`

Increments the score of `member` in the sorted set by `increment`. Returns the new score.

*   **Sync:** `Reply<double> zincrby(const std::string &key, double increment, const std::string &member)`
*   **Async:** `void zincrby_async(const std::string &key, double increment, const std::string &member, Callback<double> cb)`

### `ZINTERSTORE destination numkeys key [key ...] [WEIGHTS weight [weight ...]] [AGGREGATE SUM|MIN|MAX]`

Computes the intersection of `numkeys` sorted sets and stores the result in `destination`. Returns the number of elements in the resulting sorted set.

*   **Sync:** `Reply<long long> zinterstore(const std::string &destination, const std::vector<std::string> &keys, const std::vector<double> &weights = {}, Aggregation aggregate = Aggregation::SUM)`
*   **Async:** `void zinterstore_async(const std::string &destination, const std::vector<std::string> &keys, Callback<long long> cb, const std::vector<double> &weights = {}, Aggregation aggregate = Aggregation::SUM)`
*   **`Aggregation` Enum:** `SUM`, `MIN`, `MAX`.

### `ZLEXCOUNT key min max`

Counts the members in a sorted set between a given lexicographical range.

*   **Sync:** `Reply<long long> zlexcount(const std::string &key, const Interval &interval)`
*   **Async:** `void zlexcount_async(const std::string &key, const Interval &interval, Callback<long long> cb)`
*   **`Interval`:** Use constructors like `Interval("[alpha", "(omega")`.

### `ZPOPMAX key [count]`

Removes and returns the members with the highest scores.

*   **Sync:** `Reply<std::vector<score_member>> zpopmax(const std::string &key, long long count = 1)`
*   **Async:** `void zpopmax_async(const std::string &key, long long count, Callback<std::vector<score_member>> cb)`

### `ZPOPMIN key [count]`

Removes and returns the members with the lowest scores.

*   **Sync:** `Reply<std::vector<score_member>> zpopmin(const std::string &key, long long count = 1)`
*   **Async:** `void zpopmin_async(const std::string &key, long long count, Callback<std::vector<score_member>> cb)`

### `ZRANGE key start stop [BYSCORE|BYLEX] [REV] [LIMIT offset count] [WITHSCORES]`

Returns members in a sorted set within a range of indices, scores, or lexicographical values.

*   **Range by Index Sync:** `Reply<std::vector<std::string>> zrange(const std::string &key, long long start, long long stop)`
*   **Range by Index Sync (Scores):** `Reply<std::vector<score_member>> zrange_withscores(const std::string &key, long long start, long long stop)`
*   **Range by Score Sync:** `Reply<std::vector<std::string>> zrangebyscore(const std::string &key, const Interval &interval, const std::optional<limit> &lim = std::nullopt)`
*   **Range by Score Sync (Scores):** `Reply<std::vector<score_member>> zrangebyscore_withscores(const std::string &key, const Interval &interval, const std::optional<limit> &lim = std::nullopt)`
*   **Range by Lex Sync:** `Reply<std::vector<std::string>> zrangebylex(const std::string &key, const Interval &interval, const std::optional<limit> &lim = std::nullopt)`
*   **(Reverse versions available: `zrevrange`, `zrevrangebyscore`, `zrevrangebylex`)**
*   **Async versions:** Add `_async` suffix and callback argument.
*   **`limit` Struct:** `{ long long offset; long long count; }`

### `ZRANK key member`

Returns the rank (0-based index) of `member` in the sorted set (ordered by score, lowest first).

*   **Sync:** `Reply<std::optional<long long>> zrank(const std::string &key, const std::string &member)`
*   **Async:** `void zrank_async(const std::string &key, const std::string &member, Callback<std::optional<long long>> cb)`

### `ZREVRANK key member`

Returns the rank of `member` with scores ordered highest to lowest.

*   **Sync:** `Reply<std::optional<long long>> zrevrank(const std::string &key, const std::string &member)`
*   **Async:** `void zrevrank_async(const std::string &key, const std::string &member, Callback<std::optional<long long>> cb)`

### `ZREM key member [member ...]`

Removes members from the sorted set. Returns the number of members removed.

*   **Sync:** `Reply<long long> zrem(const std::string &key, const std::vector<std::string> &members)`
*   **Async:** `void zrem_async(const std::string &key, const std::vector<std::string> &members, Callback<long long> cb)`

### `ZREMRANGEBYLEX key min max`

Removes members in a lexicographical range.

*   **Sync:** `Reply<long long> zremrangebylex(const std::string &key, Interval const &interval)`
*   **Async:** `void zremrangebylex_async(const std::string &key, const Interval &interval, Callback<long long> cb)`

### `ZREMRANGEBYRANK key start stop`

Removes members within the given rank range.

*   **Sync:** `Reply<long long> zremrangebyrank(const std::string &key, long long start, long long stop)`
*   **Async:** `void zremrangebyrank_async(const std::string &key, long long start, long long stop, Callback<long long> cb)`

### `ZREMRANGEBYSCORE key min max`

Removes members within the given score range.

*   **Sync:** `Reply<long long> zremrangebyscore(const std::string &key, Interval const &interval)`
*   **Async:** `void zremrangebyscore_async(const std::string &key, const Interval &interval, Callback<long long> cb)`

### `ZSCAN key cursor [MATCH pattern] [COUNT count]`

Iterates elements of Sorted Set types and their associated scores.

*   **Sync:** `Reply<scan<std::vector<score_member>>> zscan(const std::string &key, long long cursor, const std::optional<std::string>& pattern = std::nullopt, const std::optional<long long>& count = std::nullopt)`
*   **Async:** `void zscan_async(const std::string &key, long long cursor, Callback<scan<std::vector<score_member>>> cb, const std::optional<std::string>& pattern = std::nullopt, const std::optional<long long>& count = std::nullopt)`
*   **Note:** The `scan` struct contains `cursor` (long long) and `elements` (the vector of `score_member`).

### `ZSCORE key member`

Returns the score of `member` in the sorted set.

*   **Sync:** `Reply<std::optional<double>> zscore(const std::string &key, const std::string &member)`
*   **Async:** `void zscore_async(const std::string &key, const std::string &member, Callback<std::optional<double>> cb)`

### `ZUNIONSTORE destination numkeys key [key ...] [WEIGHTS weight [weight ...]] [AGGREGATE SUM|MIN|MAX]`

Computes the union of `numkeys` sorted sets and stores the result in `destination`. Returns the number of elements in the resulting sorted set.

*   **Sync:** `Reply<long long> zunionstore(const std::string &destination, const std::vector<std::string> &keys, const std::vector<double> &weights = {}, Aggregation aggregate = Aggregation::SUM)`
*   **Async:** `void zunionstore_async(const std::string &destination, const std::vector<std::string> &keys, Callback<long long> cb, const std::vector<double> &weights = {}, Aggregation aggregate = Aggregation::SUM)`

### Blocking Operations

*   **`BZPOPMAX key [key ...] timeout`**: Blocks until a member with the highest score can be popped.
    *   **Sync:** `Reply<std::optional<std::vector<std::string>>> bzpopmax(const std::vector<std::string> &keys, long long timeout)`
    *   **Async:** `void bzpopmax_async(const std::vector<std::string> &keys, long long timeout, Callback<std::optional<std::vector<std::string>>> cb)`
*   **`BZPOPMIN key [key ...] timeout`**: Blocks until a member with the lowest score can be popped.
    *   **Sync:** `Reply<std::optional<std::vector<std::string>>> bzpopmin(const std::vector<std::string> &keys, long long timeout)`
    *   **Async:** `void bzpopmin_async(const std::vector<std::string> &keys, long long timeout, Callback<std::optional<std::vector<std::string>>> cb)`

(... Add ZMP* and BZMP* if implemented ...) 