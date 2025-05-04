# `qbm-redis`: HyperLogLog Commands

This document covers Redis commands operating on HyperLogLog data structures, used for probabilistic cardinality estimation of large sets.

Reference: [Redis HyperLogLog Commands](https://redis.io/commands/?group=hyperloglog)

## Common Reply Types

*   `qb::redis::Reply<bool>`: For `PFADD` (returns true if at least one internal register was altered).
*   `qb::redis::Reply<long long>`: For `PFCOUNT` (returns the estimated cardinality).
*   `qb::redis::status`: For `PFMERGE`.

## Commands

### `PFADD key element [element ...]`

Adds the specified elements to the HyperLogLog data structure stored at `key`. Returns `true` if at least one internal HyperLogLog register was altered, `false` otherwise.

*   **Sync:** `Reply<bool> pfadd(const std::string &key, Elements &&...elements)` (Variadic template)
*   **Async:** `void pfadd_async(const std::string &key, std::vector<std::string> elements, Callback<bool> cb)`

### `PFCOUNT key [key ...]`

Returns the approximate cardinality of the set observed by the HyperLogLog at `key`, or the approximate cardinality of the union of the sets observed by the HyperLogLogs at multiple `keys`.

*   **Sync:** `Reply<long long> pfcount(const std::vector<std::string> &keys)`
*   **Async:** `void pfcount_async(const std::vector<std::string> &keys, Callback<long long> cb)`

### `PFMERGE destkey sourcekey [sourcekey ...]`

Merges multiple HyperLogLog values into a single unique HyperLogLog structure stored at `destkey`.

*   **Sync:** `status pfmerge(const std::string &destkey, const std::vector<std::string> &sourcekeys)`
*   **Async:** `void pfmerge_async(const std::string &destkey, const std::vector<std::string> &sourcekeys, Callback<status> cb)` 