# `qbm-redis`: List Commands

This document covers Redis commands operating on List values. Redis Lists are implemented as linked lists, making head/tail additions (LPUSH/RPUSH) very fast (O(1)), while index-based access (LINDEX) is slower (O(N)).

Reference: [Redis List Commands](https://redis.io/commands/?group=list)

## Common Reply Types

*   `qb::redis::status`: For commands returning simple "OK" (e.g., `LSET`, `LTRIM`).
*   `qb::redis::Reply<long long>`: For counts or indices (e.g., `LPUSH`, `LLEN`, `LREM`).
*   `qb::redis::Reply<std::optional<std::string>>`: For commands that return a single element (e.g., `LPOP`, `RPOP`, `LINDEX`). Check `has_value()`.
*   `qb::redis::Reply<std::vector<std::string>>`: For commands returning multiple elements (e.g., `LRANGE`).
*   `qb::redis::Reply<std::optional<std::vector<std::string>>>`: For blocking commands like `BLPOP` (returns optional vector: `[key, element]`).
*   `qb::redis::Reply<std::optional<qb::redis::list_move_result>>`: For `BLMOVE`.
*   `qb::redis::Reply<std::optional<qb::redis::list_pop_result>>`: For `BLMPOP`.

## Commands

### `LPUSH key element [element ...]`

Inserts elements at the head (left side) of the list. Returns the length of the list after the push.

*   **Sync:** `Reply<long long> lpush(const std::string &key, Args &&...args)`
*   **Async:** `void lpush_async(const std::string &key, std::vector<std::string> elements, Callback<long long> cb)` (Note: Async takes vector)

### `LPUSHX key element [element ...]`

Inserts elements at the head only if the key already exists and holds a list. Returns the length of the list after the push.

*   **Sync:** `Reply<long long> lpushx(const std::string &key, Args &&...args)`
*   **Async:** `void lpushx_async(const std::string &key, std::vector<std::string> elements, Callback<long long> cb)`

### `RPUSH key element [element ...]`

Inserts elements at the tail (right side) of the list. Returns the length of the list after the push.

*   **Sync:** `Reply<long long> rpush(const std::string &key, Args &&...args)`
*   **Async:** `void rpush_async(const std::string &key, std::vector<std::string> elements, Callback<long long> cb)`

### `RPUSHX key element [element ...]`

Inserts elements at the tail only if the key already exists and holds a list. Returns the length of the list after the push.

*   **Sync:** `Reply<long long> rpushx(const std::string &key, const std::string &val)` (Note: Sync currently only supports single value for RPUSHX)
*   **Async:** `void rpushx_async(const std::string &key, std::vector<std::string> elements, Callback<long long> cb)`

### `LPOP key [count]`

Removes and returns the first element (head) of the list.

*   **Sync (Single):** `Reply<std::optional<std::string>> lpop(const std::string &key)`
*   **Sync (Multiple):** `Reply<std::vector<std::string>> lpop(const std::string &key, long long count)`
*   **Async (Single):** `void lpop_async(const std::string &key, Callback<std::optional<std::string>> cb)`
*   **Async (Multiple):** `void lpop_async(const std::string &key, long long count, Callback<std::vector<std::string>> cb)`

### `RPOP key [count]`

Removes and returns the last element (tail) of the list.

*   **Sync (Single):** `Reply<std::optional<std::string>> rpop(const std::string &key)`
*   **Sync (Multiple):** `Reply<std::vector<std::string>> rpop(const std::string &key, long long count)`
*   **Async (Single):** `void rpop_async(const std::string &key, Callback<std::optional<std::string>> cb)`
*   **Async (Multiple):** `void rpop_async(const std::string &key, long long count, Callback<std::vector<std::string>> cb)`

### `LINDEX key index`

Gets the element at `index` (0-based, negative indices count from the end).

*   **Sync:** `Reply<std::optional<std::string>> lindex(const std::string &key, long long index)`
*   **Async:** `void lindex_async(const std::string &key, long long index, Callback<std::optional<std::string>> cb)`

### `LINSERT key BEFORE|AFTER pivot element`

Inserts `element` into the list either before or after the `pivot` element. Returns the length of the list after insertion, or -1 if pivot not found.

*   **Sync:** `Reply<long long> linsert(const std::string &key, InsertPosition position, const std::string &pivot, const std::string &val)`
*   **Async:** `void linsert_async(const std::string &key, InsertPosition position, const std::string &pivot, const std::string &val, Callback<long long> cb)`
*   **`InsertPosition` Enum:** `BEFORE`, `AFTER`.

### `LLEN key`

Returns the length of the list.

*   **Sync:** `Reply<long long> llen(const std::string &key)`
*   **Async:** `void llen_async(const std::string &key, Callback<long long> cb)`

### `LRANGE key start stop`

Returns the specified range of elements from the list.

*   **Sync:** `Reply<std::vector<std::string>> lrange(const std::string &key, long long start, long long stop)`
*   **Async:** `void lrange_async(const std::string &key, long long start, long long stop, Callback<std::vector<std::string>> cb)`

### `LREM key count element`

Removes occurrences of `element` from the list.

*   `count > 0`: Remove elements equal to `element` moving from head to tail.
*   `count < 0`: Remove elements equal to `element` moving from tail to head.
*   `count = 0`: Remove all elements equal to `element`.
*   Returns the number of removed elements.

*   **Sync:** `Reply<long long> lrem(const std::string &key, long long count, const std::string &val)`
*   **Async:** `void lrem_async(const std::string &key, long long count, const std::string &val, Callback<long long> cb)`

### `LSET key index element`

Sets the list element at `index` to `element`.

*   **Sync:** `status lset(const std::string &key, long long index, const std::string &val)`
*   **Async:** `void lset_async(const std::string &key, long long index, const std::string &val, Callback<status> cb)`

### `LTRIM key start stop`

Trims the list so that it only contains elements in the specified range.

*   **Sync:** `status ltrim(const std::string &key, long long start, long long stop)`
*   **Async:** `void ltrim_async(const std::string &key, long long start, long long stop, Callback<status> cb)`

### Blocking Operations

These commands block the *connection* until an element is available or a timeout occurs. They are useful for implementing reliable queues. **Use the asynchronous versions within actors.**

*   **`BLPOP key [key ...] timeout`**: Blocks until an element can be popped from the *head* of one of the specified lists. `timeout` is in seconds (0 means block indefinitely).
    *   **Sync:** `Reply<std::optional<std::vector<std::string>>> blpop(const std::vector<std::string> &keys, long long timeout)`
    *   **Async:** `void blpop_async(const std::vector<std::string> &keys, long long timeout, Callback<std::optional<std::vector<std::string>>> cb)`
*   **`BRPOP key [key ...] timeout`**: Blocks until an element can be popped from the *tail* of one of the specified lists.
    *   **Sync:** `Reply<std::optional<std::vector<std::string>>> brpop(const std::vector<std::string> &keys, long long timeout)`
    *   **Async:** `void brpop_async(const std::vector<std::string> &keys, long long timeout, Callback<std::optional<std::vector<std::string>>> cb)`
*   **`BRPOPLPUSH source destination timeout`**: Atomically pops an element from the tail of `source` and pushes it to the head of `destination`.
    *   **Sync:** `Reply<std::optional<std::string>> brpoplpush(const std::string &source, const std::string &destination, long long timeout)`
    *   **Async:** `void brpoplpush_async(const std::string &source, const std::string &destination, long long timeout, Callback<std::optional<std::string>> cb)`
*   **`BLMOVE source destination LEFT|RIGHT LEFT|RIGHT timeout`**: Atomically pops element from `source` (from specified side) and pushes to `destination` (to specified side).
    *   **Sync:** `Reply<std::optional<std::string>> blmove(const std::string &source, const std::string &destination, ListPosition src_pos, ListPosition dest_pos, long long timeout)`
    *   **Async:** `void blmove_async(const std::string &source, const std::string &destination, ListPosition src_pos, ListPosition dest_pos, long long timeout, Callback<std::optional<std::string>> cb)`
    *   **`ListPosition` Enum:** `LEFT`, `RIGHT`.

### `LMOVE source destination LEFT|RIGHT LEFT|RIGHT`

Atomically moves an element from `source` list to `destination` list.

*   **Sync:** `Reply<std::optional<std::string>> lmove(const std::string &source, const std::string &destination, ListPosition src_pos, ListPosition dest_pos)`
*   **Async:** `void lmove_async(const std::string &source, const std::string &destination, ListPosition src_pos, ListPosition dest_pos, Callback<std::optional<std::string>> cb)`

### `LPOS key element [RANK rank] [COUNT num_matches] [MAXLEN len]`

Finds the index of matching elements in a list.

*   **Sync (Single):** `Reply<std::optional<long long>> lpos(const std::string &key, const std::string &element, const std::optional<long long>& rank = std::nullopt, const std::optional<long long>& maxlen = std::nullopt)`
*   **Sync (Multiple):** `Reply<std::vector<long long>> lpos(const std::string &key, const std::string &element, long long count, const std::optional<long long>& rank = std::nullopt, const std::optional<long long>& maxlen = std::nullopt)`
*   **Async (Single):** `void lpos_async(const std::string &key, const std::string &element, Callback<std::optional<long long>> cb, const std::optional<long long>& rank = std::nullopt, const std::optional<long long>& maxlen = std::nullopt)`
*   **Async (Multiple):** `void lpos_async(const std::string &key, const std::string &element, long long count, Callback<std::vector<long long>> cb, const std::optional<long long>& rank = std::nullopt, const std::optional<long long>& maxlen = std::nullopt)`

### `LMPOP numkeys key [key ...] LEFT|RIGHT [COUNT count]`

Atomically pops elements from the first non-empty list key from the list of provided keys.

*   **Sync:** `Reply<std::optional<list_pop_result>> lmpop(const std::vector<std::string> &keys, ListPosition position, long long count = 1)`
*   **Async:** `void lmpop_async(const std::vector<std::string> &keys, ListPosition position, long long count, Callback<std::optional<list_pop_result>> cb)`
*   **`list_pop_result` Struct:** Contains `key_popped_from` and `elements` (vector).

### `BLMPOP timeout numkeys key [key ...] LEFT|RIGHT [COUNT count]`

Blocking version of `LMPOP`.

*   **Sync:** `Reply<std::optional<list_pop_result>> blmpop(long long timeout, const std::vector<std::string> &keys, ListPosition position, long long count = 1)`
*   **Async:** `void blmpop_async(long long timeout, const std::vector<std::string> &keys, ListPosition position, long long count, Callback<std::optional<list_pop_result>> cb)` 