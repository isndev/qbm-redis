# qbm-redis documentation map

> **Audience:** Adopter · **Status:** stable · **Verified-against:** qbm-redis @ qb 2.0.0 (C++20 default, C++23
> supported)

This is the table of contents for the qbm-redis narrative documentation: an asynchronous Redis client built on the qb-io
event loop, covering connections, the full command surface, pipelining, pub/sub, transactions, scripting, clustering,
and server administration.

**Prerequisites:** working knowledge of the qb framework — see [`qb/README.md`](../../../qb/README.md) and the qb [
`readme/`](../../../qb/readme/) docs for `qb-io` async, coroutines, and `run_sync`. **See also:** the module front
door [`../README.md`](../README.md) for positioning, the build matrix, and a quickstart.

## What this module is

qbm-redis is an asynchronous Redis client that speaks the RESP protocol directly over a single non-blocking TCP (or TLS)
session on the qb-io event loop. It implements its own RESP2/RESP3 parser, so it carries no external Redis dependency.
The public surface lives in the `qb::redis` namespace; `qb::redis::detail` holds the implementation. The umbrella header
is `<redis/redis.h>`.

The module is a **compiled library**, not header-only. The build registers it through `qb_register_module`, producing an
archive under the alias `qbm::redis` (`qbm/redis/CMakeLists.txt`). Only `redis.cpp` and `reply.cpp` compile into that
archive; the 200-plus command methods are template code pulled in through the umbrella header — but you still link the
target to get those two translation units and the `PUBLIC` include directory. It compiles at C++20 by default and at
C++23 when `QB_CXX_STANDARD=23`; the standard is governed by the framework, not the module, and propagates to consumers
as a compile feature. The coroutine API requires it.

Every command has two interchangeable completion models with the same method name — there is no `_async` suffix:

| Model         | How work finishes                                                                                                                      | Drive it with                                                                                            |
|---------------|----------------------------------------------------------------------------------------------------------------------------------------|----------------------------------------------------------------------------------------------------------|
| **Coroutine** | Overloads *without* a callback return an awaiter; `co_await` yields `Reply<T>`.                                                        | `co_await` inside a coroutine, or `qb::io::async::run_sync(...)` from synchronous code (tests, scripts). |
| **Callback**  | Overloads *with* a callback take a `Func` invocable with `Reply<T>&&` as the first argument and return the client for fluent chaining. | `qb::io::async::run` / `run_once`, optionally `await()` to drain pending replies.                        |

Use one style per call stack. `Redis<QB_IO_>` and the consumers are not thread-safe; drive a client from a single I/O
thread or strand.

## Integration in one place

You consume qbm-redis through the qb module loader, not `find_package`:

<!-- src: qbm/redis/README.md:34-35 -->

```cmake
add_subdirectory(qb)                                   # the framework first
qb_load_modules("${CMAKE_CURRENT_SOURCE_DIR}/qbm")     # discovers and adds qbm modules
# ...
target_link_libraries(your_app PRIVATE qbm::redis)     # links qb::core PUBLIC, qb::io transitively
```

```cpp
#include <redis/redis.h>   // client aliases, the command surface, Reply<T>, RetryPolicy
```

The module's `CMakeLists.txt` guards on `QB_FOUND` and returns early if the framework is absent, so
`add_subdirectory(qb)` must come first. Do not hand-add the include directory; the `redis/` headers arrive through the
`PUBLIC` include attached to `qbm::redis`.

## TLS and time

- **TLS** — there is no redis-specific SSL option. Transport security follows the framework-wide `QB_HAS_SSL` (derived
  from OpenSSL detection). With SSL on, the `qb::redis::tcp::ssl::client` alias exists (`redis.h:1449-1460`); with it
  off, the build emits a status note and only cleartext TCP is available. For `rediss://`, certificate and hostname
  verification is on by default; `set_verify_peer(false)` disables it and must be set before `connect()`.
- **Time — framework side.** Connect and command timeouts and the `RetryPolicy` delays are `qb::duration`. Defaults:
  `RetryPolicy.initial_delay` 100 ms, `max_delay` 30 s, `connect_timeout` 3 s; `connect()` default timeout 3 s;
  `command_timeout` is `qb::duration::zero()` (disabled). `set_command_timeout` is a connection-health watchdog, not a
  per-command timer: on deadline it drops the whole connection (`redis.h:209,870,882`). `debug_sleep` also takes
  `qb::duration`.
- **Time — Redis-protocol side (a documented boundary, not a bug).** Redis command time arguments keep their native wire
  units, exposed through `std::chrono`-unit overloads, and are **not** forced onto `qb::duration`:

  | Granularity | Commands | Argument type |
    |---|---|---|
  | Seconds | `EXPIRE`, `EXPIREAT`, `SETEX`, `GETEX` (`EX`) | `std::chrono::seconds` (or the raw `long long` overload) |
  | Milliseconds | `PEXPIRE`, `PEXPIREAT`, `PSETEX`, `GETEX` (`PX`), `WAIT`, `RESTORE` TTL, `MIGRATE` | `std::chrono::milliseconds` (or the raw `long long` overload) |

  So `expire(key, 60s)` sets 60 seconds and `pexpire(key, 60ms)` sets 60 milliseconds (`key_commands.h:241,416`;
  `string_commands.h:537,730,935`). Reply TTL values (`ttl`, `pttl`, `expiretime`, `pexpiretime`) are plain integers —
  the unit lives in the method name, not a wrapped type (`key_commands.h:488,701,871,896`). Stream blocking and
  idle-time arguments (`XREAD`/`XREADGROUP` `block`, `XCLAIM`/`XAUTOCLAIM` `min_idle_time`) are raw `long long`
  milliseconds with no chrono overload (`stream_commands.h:388,414,849`). Blocking-list timeouts (`BLPOP`,
  `BLMOVE`, ...) are seconds.

## API at a glance

- **Connect.** `qb::redis::tcp::client redis;` then `co_await redis.connect(uri)` (or the callback form). `connect()`
  does not negotiate RESP3 for you — the server stays on RESP2 until you issue `hello(3)` yourself (the `hello()`
  helper's version argument defaults to 3; the method is not auto-invoked, `connection_commands.h:52`). Call
  `co_await redis.hello(3)` as the first command after connect if you need RESP3 maps/push frames. Auto-reconnect and
  `RetryPolicy` cover transient failures, but in-flight commands and subscriptions are not replayed across a reconnect.
- **Run a command.** `Reply<T> r = co_await redis.get("key");` suspends without blocking the event loop. The callback
  form is `redis.get([](qb::redis::Reply<std::optional<std::string>>&& r){ ... }, "key");`.
- **Check the result.** `Reply<T>` carries a status and a typed value: `r.ok()`, `r.result()`, `r.error()`. Redis
  command errors are reported as `ok() == false`; they are not thrown.
- **Pipeline.** Issue several callback-form commands without awaiting between them; each enqueues one handler and
  replies return in FIFO order. Drain with the client's `await()` (`redis.h:858`), or `flush()` on the `tcp::pipeline`
  wrapper (`redis.h:947`, which itself calls `client().await()`). `pending_reply_count()` reports the queue depth.
- **Generic escape hatch.** For a command without a typed wrapper, call `command<T>` with the verb and arguments:

  ```cpp
  #include <redis/redis.h>
  // inside a coroutine, with a connected qb::redis::tcp::client redis;
  qb::redis::Reply<qb::json> r =
      co_await redis.command<qb::json>("COMMAND", "GETKEYS", "SET", "mykey", "value");
  ```

## Core concepts

| Concept                                                                                 | Page                                                |
|-----------------------------------------------------------------------------------------|-----------------------------------------------------|
| Connecting, URIs, `hello(3)` for RESP3, `reset()`, auto-reconnect, TLS verification     | [Connection](./connection.md)                       |
| Coroutine versus callback, `Reply<T>`, `ok()` / `result()` / `error()`                  | [Command execution](./commands_overview.md)         |
| Callback batching, `pending_reply_count()`, `tcp::pipeline`, event-loop drain semantics | [Pipelining and `await()`](./pipeline_and_await.md) |
| `ok() == false` on Redis errors; no exceptions for command errors                       | [Error handling](./error_handling.md)               |

## Command groups

Each group is a CRTP mixin composed into the client. Method names are lowercase Redis verbs, with a few deliberate
renames to avoid C++ standard-library and keyword collisions: `COPY` → `copyKey`, `SORT` → `sortKey` / `sortKeyStore` /
`sortKeyRo`, `MOVE` → `move` (Redis `MOVE`, distinct from `copyKey`).

| Group        | Page                                                   | Commands                                                                                                                                                                                                                                                                                                                                                                                                                                       |
|--------------|--------------------------------------------------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| Connection   | [connection.md](./connection.md)                       | `AUTH`, `ECHO`, `HELLO`, `PING`, `QUIT`, `RESET`, `SELECT`, `SWAPDB`                                                                                                                                                                                                                                                                                                                                                                           |
| String       | [string_commands.md](./string_commands.md)             | `APPEND`, `DECR`, `DECRBY`, `GET`, `GETDEL`, `GETEX`, `GETRANGE`, `GETSET`, `INCR`, `INCRBY`, `INCRBYFLOAT`, `LCS`, `MGET`, `MSET`, `MSETNX`, `PSETEX`, `SET`, `SETEX`, `SETNX`, `SETRANGE`, `STRLEN`, `SUBSTR`                                                                                                                                                                                                                                |
| Key          | [key_commands.md](./key_commands.md)                   | `COPY` (`copyKey`), `DEL`, `DUMP`, `EXISTS`, `EXPIRE`, `EXPIREAT`, `EXPIRETIME`, `KEYS`, `MIGRATE`, `MOVE`, `OBJECT`, `PERSIST`, `PEXPIRE`, `PEXPIREAT`, `PEXPIRETIME`, `PTTL`, `RANDOMKEY`, `RENAME`, `RENAMENX`, `RESTORE`, `SCAN`, `SORT` (`sortKey`/`sortKeyStore`/`sortKeyRo`), `TOUCH`, `TTL`, `TYPE`, `UNLINK`, `WAIT`, `WAITAOF`                                                                                                       |
| List         | [list_commands.md](./list_commands.md)                 | `BLMOVE`, `BLMPOP`, `BLPOP`, `BRPOP`, `BRPOPLPUSH`, `LINDEX`, `LINSERT`, `LLEN`, `LMOVE`, `LMPOP`, `LPOP`, `LPOS`, `LPUSH`, `LPUSHX`, `LRANGE`, `LREM`, `LSET`, `LTRIM`, `RPOP`, `RPOPLPUSH`, `RPUSH`, `RPUSHX`                                                                                                                                                                                                                                |
| Hash         | [hash_commands.md](./hash_commands.md)                 | `HDEL`, `HEXISTS`, `HGET`, `HGETALL`, `HINCRBY`, `HINCRBYFLOAT`, `HKEYS`, `HLEN`, `HMGET`, `HMSET`, `HSCAN`, `HSET`, `HSETNX`, `HSTRLEN`, `HVALS`                                                                                                                                                                                                                                                                                              |
| Set          | [set_commands.md](./set_commands.md)                   | `SADD`, `SCARD`, `SDIFF`, `SDIFFSTORE`, `SINTER`, `SINTERCARD`, `SINTERSTORE`, `SISMEMBER`, `SMEMBERS`, `SMISMEMBER`, `SMOVE`, `SPOP`, `SRANDMEMBER`, `SREM`, `SSCAN`, `SUNION`, `SUNIONSTORE`                                                                                                                                                                                                                                                 |
| Sorted set   | [sorted_set_commands.md](./sorted_set_commands.md)     | `BZMPOP`, `BZPOPMAX`, `BZPOPMIN`, `ZADD`, `ZCARD`, `ZCOUNT`, `ZDIFF`, `ZDIFFSTORE`, `ZINCRBY`, `ZINTER`, `ZINTERCARD`, `ZINTERSTORE`, `ZLEXCOUNT`, `ZMPOP`, `ZMSCORE`, `ZPOPMAX`, `ZPOPMIN`, `ZRANDMEMBER`, `ZRANGE`, `ZRANGEBYLEX`, `ZRANGEBYSCORE`, `ZRANGESTORE`, `ZRANK`, `ZREM`, `ZREMRANGEBYLEX`, `ZREMRANGEBYRANK`, `ZREMRANGEBYSCORE`, `ZREVRANGE`, `ZREVRANGEBYLEX`, `ZREVRANGEBYSCORE`, `ZREVRANK`, `ZSCAN`, `ZSCORE`, `ZUNIONSTORE` |
| HyperLogLog  | [hyperloglog_commands.md](./hyperloglog_commands.md)   | `PFADD`, `PFCOUNT`, `PFMERGE`                                                                                                                                                                                                                                                                                                                                                                                                                  |
| Bitmap       | [bitmap_commands.md](./bitmap_commands.md)             | `BITCOUNT`, `BITFIELD`, `BITFIELD_RO`, `BITOP`, `BITPOS`, `GETBIT`, `SETBIT`                                                                                                                                                                                                                                                                                                                                                                   |
| Stream       | [stream_commands.md](./stream_commands.md)             | `XACK`, `XADD`, `XAUTOCLAIM`, `XCLAIM`, `XDEL`, `XGROUP CREATE`, `XGROUP CREATECONSUMER`, `XGROUP DELCONSUMER`, `XGROUP DESTROY`, `XGROUP SETID`, `XINFO`, `XLEN`, `XPENDING`, `XRANGE`, `XREAD`, `XREADGROUP`, `XREVRANGE`, `XTRIM`                                                                                                                                                                                                           |
| Pub/Sub      | [publish_commands.md](./publish_commands.md)           | `PUBLISH`                                                                                                                                                                                                                                                                                                                                                                                                                                      |
| Subscription | [subscription_commands.md](./subscription_commands.md) | `SUBSCRIBE`, `PSUBSCRIBE`, `UNSUBSCRIBE`, `PUNSUBSCRIBE` — drive through `tcp::cb_consumer` or `tcp::co_consumer`                                                                                                                                                                                                                                                                                                                              |
| Transaction  | [transaction_commands.md](./transaction_commands.md)   | `MULTI`, `EXEC`, `DISCARD`, `WATCH`, `UNWATCH`                                                                                                                                                                                                                                                                                                                                                                                                 |
| Scripting    | [scripting_commands.md](./scripting_commands.md)       | `EVAL`, `EVALSHA`, `EVAL_RO` (`evalRo`), `EVALSHA_RO` (`evalshaRo`), `SCRIPT DEBUG`, `SCRIPT EXISTS`, `SCRIPT FLUSH`, `SCRIPT KILL`, `SCRIPT LOAD`                                                                                                                                                                                                                                                                                             |
| Function     | [function_commands.md](./function_commands.md)         | `FCALL`, `FCALL_RO`, `FUNCTION DELETE`, `FUNCTION DUMP`, `FUNCTION FLUSH`, `FUNCTION KILL`, `FUNCTION LIST`, `FUNCTION LOAD`, `FUNCTION RESTORE`, `FUNCTION STATS`                                                                                                                                                                                                                                                                             |
| Server       | [server_commands.md](./server_commands.md)             | `CLIENT`, `COMMAND`, `CONFIG`, `DBSIZE`, `DEBUG`, `FAILOVER`, `FLUSHALL`, `FLUSHDB`, `INFO`, `LATENCY`, `MEMORY`, `MONITOR`, `ROLE`, `SHUTDOWN`, `SLOWLOG`, `TIME`, and more                                                                                                                                                                                                                                                                   |
| ACL          | [acl_commands.md](./acl_commands.md)                   | `ACL CAT`, `ACL DELUSER`, `ACL DRYRUN`, `ACL GENPASS`, `ACL GETUSER`, `ACL LIST`, `ACL LOAD`, `ACL LOG`, `ACL SAVE`, `ACL SETUSER`, `ACL USERS`, `ACL WHOAMI`                                                                                                                                                                                                                                                                                  |
| Cluster      | [cluster_commands.md](./cluster_commands.md)           | `ASKING`, `CLUSTER` (ADDSLOTS, ADDSLOTSRANGE, COUNT-FAILURE-REPORTS, DELSLOTS, DELSLOTSRANGE, FLUSHSLOTS, INFO, KEYSLOT, LINKS, MEET, MYID, NODES, REPLICAS, RESET, SETSLOT, SHARDS, SLAVES, SLOTS), `READONLY`, `READWRITE`                                                                                                                                                                                                                   |
| Module       | [module_commands.md](./module_commands.md)             | `MODULE LIST`, `MODULE LOAD`, `MODULE UNLOAD`                                                                                                                                                                                                                                                                                                                                                                                                  |
| Geo          | [geo_commands.md](./geo_commands.md)                   | `GEOADD`, `GEODIST`, `GEOHASH`, `GEOPOS`, `GEORADIUS`, `GEORADIUSBYMEMBER`, `GEOSEARCH`                                                                                                                                                                                                                                                                                                                                                        |

## Pitfalls

- **Auto-reconnect does not replay work.** On disconnect, all pending replies fail and predicted subscription state is
  cleared. After a reconnect you must re-subscribe and re-issue any in-flight commands yourself (`redis.h:774,1204`).
- **`set_command_timeout` drops the connection.** It is a health watchdog, not a per-command deadline: because FIFO
  pipelining cannot fail one mid-queue command without desyncing later replies, tripping the deadline disconnects and
  fails every pending command (`redis.h:870`).
- **Auto-iterating scanners and `hvals` are callback-only.** The no-cursor `sscan` / `zscan` / `hscan` overloads and
  multi-key `hvals` buffer the whole result and fire the callback once; there is no coroutine form, and a throwing
  callback is caught and logged, not propagated (`set_commands.h:786`, `hash_commands.h:620,831`).
- **Some callbacks silently no-op.** Several set and sorted-set callback overloads return without issuing a command (so
  the callback never fires) when a required argument is empty — for example `sadd` / `srem` with no members (
  `set_commands.h:162`).
- **Multi-stream `xread` / `xreadgroup` throw synchronously.** They throw `std::invalid_argument` from the callback body
  when `keys` is empty or `keys.size() != ids.size()` — catch it; it is not delivered as a `Reply` error (
  `stream_commands.h:466,553`).
- **`GETEX` unit asymmetry.** The integer overload uses `EX` (seconds) while the `std::chrono::milliseconds` overload
  uses `PX` (milliseconds) — unlike `SET`, whose integer and chrono overloads both use milliseconds (
  `string_commands.h:935`).

## Examples as specification

The integration tests under [`../tests/`](../tests/) are executable documentation and run in both RESP2 and RESP3 modes.
When a signature or behavior is unclear, grep a test and read it — for example `test-pipeline.cpp` drives `connect` /
`flushall` through `qb::io::async::run_sync` (`tests/test-pipeline.cpp:48`).

## See also

- [`../README.md`](../README.md) — module positioning, build matrix, and quickstart.
- [`qb/README.md`](../../../qb/README.md) — the qb framework this module builds on.
- The qb framework [`readme/`](../../../qb/readme/) — `qb-io` async, coroutines, and `run_sync`.
