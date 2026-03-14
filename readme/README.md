# QB Redis Module (`qbm-redis`) - Detailed Documentation

Welcome to the detailed documentation for the `qbm-redis` module of the QB C++ Actor Framework. This module provides a high-performance, fully asynchronous C++ client for Redis with **native RESP2/RESP3 support**, **zero external Redis dependencies** (no hiredis), and a **coroutine-first API** designed for seamless integration with the QB ecosystem.

## API Overview

- **Coroutine**: `Reply<T> r = co_await redis.cmd(...)` — suspends without blocking the event loop
- **Callback**: `redis.cmd(callback, ...)` — callback receives `Reply<T>&&`
- **No `_async` suffix** — same method name for both styles
- **Blocking (tests/scripts)**: `qb::io::async::run_sync(redis.connect())`, `run_sync(redis.get("key"))`

## Core Concepts

*   **[Connection](./connection.md):** `qb::redis::tcp::client`, URIs, `connect()`, `hello(3)` for RESP3, `reset()`, auto-reconnect
*   **[Command Execution](./commands_overview.md):** Coroutine vs callback, `qb::redis::Reply<T>`, `reply.ok()`, `reply.result()`, `reply.error()`
*   **[Error Handling](./error_handling.md):** Commands return `Reply` with `ok()==false` on error; no exceptions for Redis command errors

## Command Groups

| Group | Commands |
|-------|----------|
| **[Connection](./connection.md)** | `AUTH`, `ECHO`, `HELLO`, `PING`, `QUIT`, `RESET`, `SELECT`, `SWAPDB` |
| **[String](./string_commands.md)** | `APPEND`, `DECR`, `DECRBY`, `GET`, `GETDEL`, `GETEX`, `GETRANGE`, `GETSET`, `INCR`, `INCRBY`, `INCRBYFLOAT`, `LCS`, `MGET`, `MSET`, `MSETNX`, `PSETEX`, `SET`, `SETEX`, `SETNX`, `SETRANGE`, `STRLEN`, `SUBSTR` |
| **[Key](./key_commands.md)** | `COPY`, `DEL`, `DUMP`, `EXISTS`, `EXPIRE`, `EXPIREAT`, `EXPIRETIME`, `KEYS`, `MIGRATE`, `MOVE`, `OBJECT`, `PERSIST`, `PEXPIRE`, `PEXPIREAT`, `PEXPIRETIME`, `PTTL`, `RANDOMKEY`, `RENAME`, `RENAMENX`, `RESTORE`, `SCAN`, `SORT`, `SORT_RO`, `SORT STORE`, `TOUCH`, `TTL`, `TYPE`, `UNLINK`, `WAIT`, `WAITAOF` |
| **[List](./list_commands.md)** | `BLMOVE`, `BLMPOP`, `BLPOP`, `BRPOP`, `BRPOPLPUSH`, `LINDEX`, `LINSERT`, `LLEN`, `LMOVE`, `LMPOP`, `LPOP`, `LPOS`, `LPUSH`, `LPUSHX`, `LRANGE`, `LREM`, `LSET`, `LTRIM`, `RPOP`, `RPOPLPUSH`, `RPUSH`, `RPUSHX` |
| **[Hash](./hash_commands.md)** | `HDEL`, `HEXISTS`, `HGET`, `HGETALL`, `HINCRBY`, `HKEYS`, `HLEN`, `HMGET`, `HMSET`, `HSCAN`, `HSET`, `HSETNX`, `HSTRLEN`, `HVALS` |
| **[Set](./set_commands.md)** | `SADD`, `SCARD`, `SDIFF`, `SDIFFSTORE`, `SINTER`, `SINTERCARD`, `SINTERSTORE`, `SISMEMBER`, `SMEMBERS`, `SMOVE`, `SPOP`, `SRANDMEMBER`, `SREM`, `SSCAN`, `SUNION`, `SUNIONSTORE` |
| **[Sorted Set](./sorted_set_commands.md)** | `BZMPOP`, `BZPOPMAX`, `BZPOPMIN`, `ZADD`, `ZCARD`, `ZCOUNT`, `ZDIFF`, `ZDIFFSTORE`, `ZINCRBY`, `ZINTER`, `ZINTERCARD`, `ZINTERSTORE`, `ZLEXCOUNT`, `ZMPOP`, `ZMSCORE`, `ZPOPMAX`, `ZPOPMIN`, `ZRANDMEMBER`, `ZRANGE`, `ZRANGEBYLEX`, `ZRANGEBYSCORE`, `ZRANGESTORE`, `ZRANK`, `ZREM`, `ZREMRANGEBYLEX`, `ZREMRANGEBYRANK`, `ZREMRANGEBYSCORE`, `ZREVRANGE`, `ZREVRANGEBYLEX`, `ZREVRANGEBYSCORE`, `ZREVRANK`, `ZSCAN`, `ZSCORE`, `ZUNIONSTORE` |
| **[HyperLogLog](./hyperloglog_commands.md)** | `PFADD`, `PFCOUNT`, `PFMERGE` |
| **[Bitmap](./bitmap_commands.md)** | `BITCOUNT`, `BITFIELD`, `BITFIELD_RO`, `BITOP`, `BITPOS`, `GETBIT`, `SETBIT` |
| **[Stream](./stream_commands.md)** | `XACK`, `XADD`, `XAUTOCLAIM`, `XCLAIM`, `XDEL`, `XGROUP CREATECONSUMER`, `XGROUP CREATE`, `XGROUP DELCONSUMER`, `XGROUP DESTROY`, `XGROUP SETID`, `XLEN`, `XPENDING`, `XRANGE`, `XREAD`, `XREADGROUP`, `XREVRANGE`, `XTRIM` |
| **[Pub/Sub](./publish_commands.md)** | `PUBLISH` |
| **[Subscription](./subscription_commands.md)** | `SUBSCRIBE`, `PSUBSCRIBE`, `UNSUBSCRIBE` — use `cb_consumer` or `co_consumer` |
| **[Transaction](./transaction_commands.md)** | `MULTI`, `EXEC`, `DISCARD`, `WATCH`, `UNWATCH` |
| **[Scripting](./scripting_commands.md)** | `EVAL`, `EVALSHA`, `EVAL_RO`, `EVALSHA_RO`, `SCRIPT DEBUG`, `SCRIPT EXISTS`, `SCRIPT FLUSH`, `SCRIPT KILL`, `SCRIPT LOAD` |
| **[Function](./function_commands.md)** | `FCALL`, `FCALL_RO`, `FUNCTION DELETE`, `FUNCTION DUMP`, `FUNCTION FLUSH`, `FUNCTION KILL`, `FUNCTION LIST`, `FUNCTION LOAD`, `FUNCTION RESTORE`, `FUNCTION STATS` |
| **[Server](./server_commands.md)** | `ACL`, `CLIENT`, `COMMAND`, `CONFIG`, `DBSIZE`, `FAILOVER`, `FLUSHALL`, `FLUSHDB`, `INFO`, `LATENCY`, `MEMORY`, `SLOWLOG`, `TIME`, etc. |
| **[ACL](./acl_commands.md)** | `ACL CAT`, `ACL DRYRUN`, `ACL GETUSER`, `ACL LIST`, `ACL SETUSER`, etc. |
| **[Cluster](./cluster_commands.md)** | `ASKING`, `CLUSTER` (ADDSLOTS, ADDSLOTSRANGE, COUNT-FAILURE-REPORTS, DELSLOTS, DELSLOTSRANGE, FLUSHSLOTS, INFO, KEYSLOT, LINKS, MEET, MYID, NODES, REPLICAS, RESET, SETSLOT, SHARDS, SLAVES, SLOTS), `READONLY`, `READWRITE` |
| **[Module](./module_commands.md)** | `MODULE LOAD`, `MODULE LIST`, `MODULE UNLOAD` |
| **[Geo](./geo_commands.md)** | `GEOADD`, `GEODIST`, `GEOHASH`, `GEOPOS`, `GEORADIUS`, `GEORADIUSBYMEMBER`, `GEOSEARCH` |

## Generic Command API

For commands not yet wrapped:

```cpp
auto r = co_await redis.command<qb::json>("COMMAND", "GETKEYS", "SET", "mykey", "value");
```

## Examples

Refer to the unit tests in `qbm/redis/tests/` for practical examples of each command group. All tests run in both RESP2 and RESP3 modes.
