# `qbm-redis`: Transaction Commands

This document covers Redis commands for managing transactions (MULTI/EXEC).

Reference: [Redis Transaction Commands](https://redis.io/commands/?group=transactions)

## Key Concepts

*   **Atomicity:** Commands within a MULTI/EXEC block are executed sequentially and atomically. No other client command can run between them.
*   **Queuing:** Commands issued after `MULTI` are queued server-side and not executed immediately.
*   **`EXEC`:** Executes all queued commands.
*   **`DISCARD`:** Flushes the queue without executing.
*   **`WATCH`:** Provides optimistic locking. If a watched key is modified by another client before `EXEC` is called, the transaction fails.

## Client State

The `qbm-redis` client maintains an internal flag (`_in_multi`) to track whether it's currently inside a `MULTI` block.

*   **Entering MULTI:** Calling `multi()` sets the flag.
*   **Exiting MULTI:** Calling `exec()` or `discard()` clears the flag.
*   **Behavior:** When `_in_multi` is true, commands sent to Redis return a simple "QUEUED" status immediately. The actual results are returned as an array reply only when `exec()` is called.

## Common Reply Types

*   `qb::redis::status`: For `MULTI`, `DISCARD`, `WATCH`, `UNWATCH`.
*   `qb::redis::Reply<qb::redis::pipeline_result>`: For `EXEC`. The `pipeline_result` struct contains a `std::vector<redisReply*>` (or similar representation) holding the individual replies for each queued command.
    *   **Note:** Parsing the results within the `pipeline_result` requires careful handling, as the types depend on the commands queued in the transaction. The library currently returns the raw replies for `EXEC`.

## Commands

### `MULTI`

Marks the start of a transaction block. Subsequent commands are queued.

*   **Sync:** `status multi()`
*   **Async:** `void multi_async(Callback<status> cb)`

### `EXEC`

Executes all commands queued since `MULTI`. Returns an array of replies, one for each command.

*   **Sync:** `Reply<pipeline_result> exec()`
*   **Async:** `void exec_async(Callback<pipeline_result> cb)`

```cpp
// Sync Example
redis.multi();
redis.set("a", "1"); // Reply is likely QUEUED status
redis.set("b", "2"); // Reply is likely QUEUED status
auto exec_reply = redis.exec();

if (exec_reply) {
    // exec_reply.value() is a pipeline_result
    // Iterate through raw replies and parse manually if needed
    const auto& results = exec_reply.value().replies;
    if (results.size() == 2) {
        // Assuming hiredis redisReply*
        if (results[0]->type == REDIS_REPLY_STATUS && strcmp(results[0]->str, "OK") == 0) { ... }
        if (results[1]->type == REDIS_REPLY_STATUS && strcmp(results[1]->str, "OK") == 0) { ... }
    }
} else {
    // Transaction failed (e.g., due to WATCH)
    qb::io::cout() << "EXEC failed: " << exec_reply.error().what() << std::endl;
}
```

### `DISCARD`

Discards all commands queued since `MULTI`.

*   **Sync:** `status discard()`
*   **Async:** `void discard_async(Callback<status> cb)`

### `WATCH key [key ...]`

Marks the given keys to be watched for conditional execution of a transaction. If any watched key is modified before `EXEC`, the transaction aborts.

*   **Sync (Single):** `status watch(const std::string &key)`
*   **Sync (Multiple):** `status watch(const std::vector<std::string> &keys)`
*   **Async (Single):** `void watch_async(const std::string &key, Callback<status> cb)`
*   **Async (Multiple):** `void watch_async(const std::vector<std::string> &keys, Callback<status> cb)`

### `UNWATCH`

Flushes all the previously watched keys for the current connection.

*   **Sync:** `status unwatch()`
*   **Async:** `void unwatch_async(Callback<status> cb)` 