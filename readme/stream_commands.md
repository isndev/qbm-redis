# `qbm-redis`: Stream Commands

This document covers Redis commands operating on Stream values. Redis Streams are append-only logs.

Reference: [Redis Stream Commands](https://redis.io/commands/?group=stream)

## Common Types & Reply Types

*   **`qb::redis::stream_id` Struct:** Represents a stream entry ID (e.g., "1678886400000-0").
*   **`qb::redis::stream_entry` Struct:** `{ stream_id id; qb::unordered_map<std::string, std::string> values; }`
*   **`qb::redis::stream_entry_list` Type:** `std::vector<stream_entry>`
*   **`qb::redis::map_stream_entry_list` Type:** `qb::unordered_map<std::string, stream_entry_list>` (Maps stream key to list of entries).
*   **`qb::redis::stream_consumer` Struct:** `{ std::string name; long long pending; }`
*   **`qb::redis::stream_group` Struct:** `{ std::string name; std::string last_delivered_id; long long pending; long long consumers; }`
*   **`qb::redis::stream_pending_summary` Struct:** `{ long long total; std::optional<stream_id> first_id; std::optional<stream_id> last_id; std::optional<qb::unordered_map<std::string, long long>> consumers; }`
*   **`qb::redis::stream_pending_entry` Struct:** `{ stream_id id; std::string consumer_name; long long idle_time_ms; long long delivery_count; }`
*   `qb::redis::status`: For commands returning simple "OK" (e.g., `XGROUP CREATE`).
*   `qb::redis::Reply<long long>`: For counts (`XLEN`, `XDEL`, `XACK`, `XGROUP DESTROY`, `XGROUP DELCONSUMER`).
*   `qb::redis::Reply<std::optional<stream_id>>`: For `XADD`.
*   `qb::redis::Reply<map_stream_entry_list>`: For `XREAD`, `XREADGROUP`.
*   `qb::redis::Reply<stream_pending_summary>`: For `XPENDING` summary.
*   `qb::redis::Reply<std::vector<stream_pending_entry>>`: For `XPENDING` range.

## Commands

### `XADD key [NOMKSTREAM] [MAXLEN|MINID [=|~] threshold [LIMIT count]] *|id field value [field value ...]`

Appends a new entry to the stream. Returns the ID of the added entry.

*   **Sync:** `Reply<std::optional<stream_id>> xadd(const std::string &key, const std::vector<std::pair<std::string, std::string>> &entries, const std::optional<std::string> &id = std::nullopt)`
*   **Async:** `void xadd_async(const std::string &key, const std::vector<std::pair<std::string, std::string>> &entries, Callback<std::optional<stream_id>> cb, const std::optional<std::string> &id = std::nullopt)`
*   **Note:** Trim options (`MAXLEN`, `MINID`) and `NOMKSTREAM` are not directly exposed; build the command manually if needed.

### `XLEN key`

Returns the number of entries in a stream.

*   **Sync:** `Reply<long long> xlen(const std::string &key)`
*   **Async:** `void xlen_async(const std::string &key, Callback<long long> cb)`

### `XDEL key id [id ...]`

Removes the specified entries from a stream. Returns the number of entries deleted.

*   **Sync:** `Reply<long long> xdel(const std::string &key, Ids &&...ids)` (Variadic template takes `stream_id` or `std::string`)
*   **Async:** `void xdel_async(const std::string &key, std::vector<std::string> ids, Callback<long long> cb)`

### `XGROUP CREATE key groupname id|$ [MKSTREAM] [ENTRIESREAD entries_read]`

Creates a consumer group.

*   **Sync:** `status xgroup_create(const std::string &key, const std::string &group, const std::string &id, bool mkstream = false)`
*   **Async:** `void xgroup_create_async(const std::string &key, const std::string &group, const std::string &id, Callback<status> cb, bool mkstream = false)`

### `XGROUP DESTROY key groupname`

Destroys a consumer group.

*   **Sync:** `Reply<long long> xgroup_destroy(const std::string &key, const std::string &group)`
*   **Async:** `void xgroup_destroy_async(const std::string &key, const std::string &group, Callback<long long> cb)`

### `XGROUP DELCONSUMER key groupname consumername`

Deletes a consumer from a consumer group.

*   **Sync:** `Reply<long long> xgroup_delconsumer(const std::string &key, const std::string &group, const std::string &consumer)`
*   **Async:** `void xgroup_delconsumer_async(const std::string &key, const std::string &group, const std::string &consumer, Callback<long long> cb)`

### `XGROUP SETID key groupname id|$ [ENTRIESREAD entries_read]`

Sets the last delivered ID for a consumer group.

*   **Sync:** `status xgroup_setid(const std::string &key, const std::string &group, const std::string &id)`
*   **Async:** `void xgroup_setid_async(const std::string &key, const std::string &group, const std::string &id, Callback<status> cb)`

### `XACK key group id [id ...]`

Acknowledges the processing of one or more messages for a consumer group.

*   **Sync:** `Reply<long long> xack(const std::string &key, const std::string &group, Ids &&...ids)` (Variadic template takes `stream_id` or `std::string`)
*   **Async:** `void xack_async(const std::string &key, const std::string &group, std::vector<std::string> ids, Callback<long long> cb)`

### `XTRIM key MAXLEN|MINID [=|~] threshold [LIMIT count]`

Trims the stream to a given number of items, deleting older items.

*   **Sync:** `Reply<long long> xtrim(const std::string &key, long long maxlen, bool approximate = false)`
*   **Async:** `void xtrim_async(const std::string &key, long long maxlen, Callback<long long> cb, bool approximate = false)`
*   **Note:** `MINID` trimming strategy is not directly exposed.

### `XPENDING key group [IDLE min-idle-time] [start end count] [consumer]`

Inspects the list of pending messages for a consumer group.

*   **Sync (Summary):** `Reply<stream_pending_summary> xpending(const std::string &key, const std::string &group)`
*   **Sync (Range):** `Reply<std::vector<stream_pending_entry>> xpending(const std::string &key, const std::string &group, const std::string &start, const std::string &end, long long count, const std::optional<std::string> &consumer = std::nullopt)`
*   **Async (Summary):** `void xpending_async(const std::string &key, const std::string &group, Callback<stream_pending_summary> cb)`
*   **Async (Range):** `void xpending_async(const std::string &key, const std::string &group, const std::string &start, const std::string &end, long long count, Callback<std::vector<stream_pending_entry>> cb, const std::optional<std::string> &consumer = std::nullopt)`

### `XREAD [COUNT count] [BLOCK milliseconds] STREAMS key [key ...] id [id ...]`

Read data from one or multiple streams, only returning entries with IDs greater than the specified ones.

*   **Sync:** `Reply<map_stream_entry_list> xread(const std::vector<std::string> &keys, const std::vector<std::string> &ids, std::optional<long long> count = std::nullopt, std::optional<long long> block = std::nullopt)`
*   **Sync (Single Stream):** Overload available taking `const std::string &key, const std::string &id`.
*   **Async:** `void xread_async(const std::vector<std::string> &keys, const std::vector<std::string> &ids, Callback<map_stream_entry_list> cb, std::optional<long long> count = std::nullopt, std::optional<long long> block = std::nullopt)`
*   **Async (Single Stream):** Overload available.

### `XREADGROUP GROUP group consumer [COUNT count] [BLOCK milliseconds] [NOACK] STREAMS key [key ...] id [id ...]`

Read data from one or multiple streams via a consumer group.

*   **Sync:** `Reply<map_stream_entry_list> xreadgroup(const std::string &group, const std::string &consumer, const std::vector<std::string> &keys, const std::vector<std::string> &ids, std::optional<long long> count = std::nullopt, std::optional<long long> block = std::nullopt)`
*   **Sync (Single Stream):** Overload available.
*   **Async:** `void xreadgroup_async(const std::string &group, const std::string &consumer, const std::vector<std::string> &keys, const std::vector<std::string> &ids, Callback<map_stream_entry_list> cb, std::optional<long long> count = std::nullopt, std::optional<long long> block = std::nullopt)`
*   **Async (Single Stream):** Overload available.
*   **Note:** `NOACK` option is not directly exposed.

(... Add XRANGE, XREVRANGE, XCLAIM, XAUTOCLAIM, XINFO if implemented ...) 