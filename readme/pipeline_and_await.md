# Pipelining, `await()`, and `RedisPipeline`

## Model

The Redis client keeps a **FIFO queue of reply handlers**. Each callback-based command:

1. Pushes one handler.
2. Serializes and sends one request on the connection.

Redis returns replies **in request order**, so the queue stays consistent.

Coroutine commands (`co_await redis.get(...)`) enqueue one handler per command as well; each `co_await` suspends until that single reply arrives.

## Pipelining (callback API)

Issue several commands **without** waiting between sends, then drain:

```cpp
redis.set([&](auto&& r) { /* ... */ }, "k1", "a");
redis.set([&](auto&& r) { /* ... */ }, "k2", "b");
redis.get([&](auto&& r) { /* ... */ }, "k1");
redis.await();  // runs EVRUN_NOWAIT until all three callbacks ran
```

This reduces **round trips** compared to awaiting each command in sequence.

## `await()`

- Implements: `while (reply_queue_not_empty) run(EVRUN_NOWAIT);`
- **Not** a blocking `recv()`; it **polls** the libev loop.
- The **calling stack** is blocked until pending replies are done — same idea as “joining” async work on that loop.
- On **disconnect**, pending handlers run with `ok() == false` and error `"disconnected"` (see `TReply` in `reply.h`).

## `RedisPipeline`

Optional wrapper around `Redis::command<Ret>(callback, name, args...)`. Mixin methods (`set`, `get`, …) are used via `client()`:

```cpp
qb::redis::tcp::pipeline pipe{redis};
pipe.client().set(cb, "k", "v");
pipe.flush();  // == client().await(); not Redis FLUSHDB/FLUSHALL
```

Type alias: `qb::redis::tcp::pipeline`.

## Thread safety

The client is **not** thread-safe: one accessor at a time on the I/O thread / strand.

## Tests

See `qbm/redis/tests/test-pipeline.cpp` and CTest target `qbm-redis-test-pipeline`.
