# `qbm-redis`: Transaction Commands

This document covers Redis commands for managing transactions (MULTI/EXEC).

**API:** All commands support **coroutine** (`co_await redis.cmd(...)`) and **callback** (`redis.cmd(callback, ...)`).

Reference: [Redis Transaction Commands](https://redis.io/commands/?group=transactions)

## Key Concepts

*   **Atomicity:** Commands within a MULTI/EXEC block are executed sequentially and atomically. No other client command can run between them.
*   **Queuing:** Commands issued after `MULTI` are queued server-side and not executed immediately.
*   **`EXEC`:** Executes all queued commands.
*   **`DISCARD`:** Flushes the queue without executing.
*   **`WATCH`:** Provides optimistic locking. If a watched key is modified by another client before `EXEC` is called, the transaction fails.

## Client State

The `qbm-redis` client keeps a **client-side** boolean `in_multi_` (implementation detail of `transaction_commands`) to know whether the connection is in MULTI mode after a successful `MULTI`.

*   **Entering MULTI:** A successful `multi()` sets `in_multi_`.
*   **Exiting MULTI:** `exec()` and `discard()` clear `in_multi_` when the operation is issued (callbacks / coroutine completion follow the usual reply path).
*   **Disconnect:** If the connection drops, `Redis` drains pending reply handlers and calls `reset_transaction_state()` so `in_multi_` is cleared. Do not assume the server still has a transaction open after a reconnect.
*   **Behavior:** While `in_multi_` is true, commands you send are part of the transaction; server replies for queued commands are typically simple `QUEUED` statuses until `EXEC` returns the array of results.

## Common Reply Types

*   `qb::redis::status`: For `MULTI`, `DISCARD`, `WATCH`, `UNWATCH`.
*   `qb::redis::Reply<std::vector<T>>` (or your chosen `T` per `exec<T>()`): For `EXEC`, one element per queued command. Parse each element according to the command you queued.

## Commands

### `MULTI`

Marks the start of a transaction block. Subsequent commands are queued.

*   **Coroutine:** `co_await redis.multi()` → `Reply<status>`
*   **Callback:** `redis.multi(callback)` with `Callback<status>`

### `EXEC`

Executes all commands queued since `MULTI`. Returns an array of replies, one for each command.

*   **Coroutine:** `co_await redis.exec<Result>()` → `Reply<std::vector<Result>>`
*   **Callback:** `redis.exec<Result>(callback)`

```cpp
// Coroutine-style sketch
co_await redis.multi();
co_await redis.set("a", "1");  // typically QUEUED / status
co_await redis.set("b", "2");
auto exec_reply = co_await redis.exec<qb::redis::status>();

if (exec_reply) {
    const auto& results = exec_reply.value();
    // Inspect each Reply<Result> in results for per-command outcomes
} else {
    qb::io::cout() << "EXEC failed: " << exec_reply.error().what() << std::endl;
}
```

### `DISCARD`

Discards all commands queued since `MULTI`.

*   **Coroutine:** `co_await redis.discard()`
*   **Callback:** `redis.discard(callback)`

### `WATCH key [key ...]`

Marks keys for optimistic execution. If any watched key changes before `EXEC`, the transaction aborts.

*   **Coroutine / callback:** `watch`, `watch(keys)`, `watch_async`, etc. (see `transaction_commands.h`)

### `UNWATCH`

Clears watched keys for the connection.

*   **Coroutine / callback:** `unwatch`, `unwatch_async`
