# QB Redis Module (`qbm-redis`)

**High-Performance, Asynchronous Redis Client for the QB Actor Framework**

<p align="center">
  <img src="https://img.shields.io/badge/Redis-6%2B-red.svg" alt="Redis"/>
  <img src="https://img.shields.io/badge/C%2B%2B-23-blue.svg" alt="C++23"/>
  <img src="https://img.shields.io/badge/Cross--Platform-Linux%20%7C%20macOS%20%7C%20Windows-lightgrey.svg" alt="Cross Platform"/>
  <img src="https://img.shields.io/badge/Arch-x86__64%20%7C%20ARM64-lightgrey.svg" alt="Architecture"/>
  <img src="https://img.shields.io/badge/SSL-TLS-green.svg" alt="SSL/TLS"/>
  <img src="https://img.shields.io/badge/Protocol-RESP2%20%7C%20RESP3-orange.svg" alt="RESP2/RESP3"/>
  <img src="https://img.shields.io/badge/License-Apache%202.0-green.svg" alt="License"/>
</p>

`qbm-redis` delivers comprehensive Redis capabilities to the QB Actor Framework. It provides a **coroutine-first** API with full C++23 support, native RESP2/RESP3 protocol parsing (zero external Redis dependencies), and callback fallback for legacy code. Built on QB's non-blocking I/O, it enables high-performance Redis interactions without blocking the event loop.

**There is no simpler or more complete Redis client in C++ today** — zero external dependencies, full protocol support, and a single, elegant API.

## Key Features

- **Native Protocol Parser**: Zero-copy RESP2/RESP3 parsing, no hiredis or external Redis libraries
- **Coroutine-First API**: `co_await redis.get("key")` — true async without blocking
- **Dual API**: Coroutines (`co_await`) and callbacks (`redis.cmd(callback, ...)`) — same method names
- **RESP2 & RESP3**: Full support; use `hello(3)` to switch to RESP3
- **Auto-Reconnect**: Configurable retry policy with backoff
- **Type-Safe**: `qb::redis::Reply<T>` with `ok()`, `result()`, `error()`
- **Elegant API**: `if (r)` instead of `if (r.ok())`; `r.value_or("")` for optional results — no verbose `.has_value()` checks

## Quick Integration

### CMake Setup

```cmake
qb_load_modules("${CMAKE_CURRENT_SOURCE_DIR}/qbm")
target_link_libraries(your_target PRIVATE qbm::redis)
```

### Include

```cpp
#include <redis/redis.h>
```

## Quick Start: Coroutine-Based

```cpp
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <redis/redis.h>

qb::io::async::task<void> redis_example() {
    qb::redis::tcp::client redis{qb::io::uri{"tcp://localhost:6379"}};

    if (!co_await redis.connect()) {
        qb::io::cerr() << "Connection failed" << std::endl;
        co_return;
    }

    // Optional: switch to RESP3
    // co_await redis.hello(3);

    auto set_r = co_await redis.set("greeting", "Hello Redis!");
    if (!set_r) { /* handle error */ }

    auto val = (co_await redis.get("greeting")).value_or("");
    if (!val.empty()) qb::io::cout() << "Retrieved: " << val << std::endl;

    co_await redis.del(std::vector<std::string>{"greeting"});
}
```

## Blocking Usage (Tests, Scripts)

```cpp
#include <qb/io/async.h>
#include <redis/redis.h>

int main() {
    qb::io::async::init();

    qb::redis::tcp::client redis{qb::io::uri{"tcp://localhost:6379"}};
    if (!qb::io::async::run_sync(redis.connect())) {
        return 1;
    }

    auto set_r = qb::io::async::run_sync(redis.set("key", "value"));
    auto get_r = qb::io::async::run_sync(redis.get("key"));

    return 0;
}
```

## Real-World Examples

The following examples demonstrate the power and simplicity of `qbm-redis` for common Redis patterns. All run asynchronously without blocking the event loop.

### Pub/Sub — Callback Consumer

Use `cb_consumer` for event-driven message handling. Messages are delivered via callbacks; ideal for actors or event loops.

```cpp
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <redis/redis.h>

qb::redis::tcp::cb_consumer consumer{qb::io::uri{"tcp://localhost:6379"},
    [](qb::redis::message&& msg) {
        qb::io::cout() << "[" << msg.channel << "] " << msg.payload << "\n";
    }};

co_await consumer.connect();
co_await consumer.hello(3);  // RESP3 for pub/sub
co_await consumer.subscribe("alerts");
co_await consumer.psubscribe("user:*:updates");  // Pattern: user.123.updates

// Publisher (separate client)
qb::redis::tcp::client publisher{qb::io::uri{"tcp://localhost:6379"}};
co_await publisher.connect();
co_await publisher.publish("alerts", "Server restarted");
co_await publisher.publish("user:42:updates", R"({"event":"login"})");
```

### Pub/Sub — Coroutine Consumer

Use `co_consumer` when you prefer `co_await` for receiving messages. Clean, sequential flow.

```cpp
qb::redis::tcp::co_consumer consumer{qb::io::uri{"tcp://localhost:6379"}};
co_await consumer.connect();
co_await consumer.hello(3);
co_await consumer.subscribe("notifications");

while (true) {
    auto msg = co_await consumer.receive();
    if (!msg) break;  // Disconnected
    qb::io::cout() << "Notification: " << msg->payload << "\n";
}
```

### Session Store (Hash + TTL)

Store user sessions with automatic expiration. One hash per session, TTL for cleanup.

```cpp
std::string session_id = "sess:" + user_id;
co_await redis.hset(session_id, "user_id", user_id);
co_await redis.hset(session_id, "email", email);
co_await redis.hset(session_id, "created_at", std::to_string(now));
co_await redis.expire(session_id, 3600);  // 1 hour

// Retrieve
if (auto email = (co_await redis.hget(session_id, "email")).value_or(""); !email.empty()) {
    // Session valid
}
```

### Leaderboard (Sorted Set)

Real-time rankings with `ZADD` and `ZREVRANGE`. Scores = points; members = player IDs.

```cpp
// Update score
co_await redis.zadd("leaderboard:global", {{score, player_id}});

// Top 10 (with scores)
if (auto top = co_await redis.zrevrange("leaderboard:global", 0, 9); top) {
    for (const auto& sm : top.result())
        qb::io::cout() << sm.member << ": " << sm.score << "\n";
}

// Player rank (1-based)
long long rank = (co_await redis.zrevrank("leaderboard:global", player_id)).value_or(-1) + 1;
```

### Event Stream (Redis Streams)

Append events and consume with consumer groups. Ideal for activity feeds, audit logs, job queues.

```cpp
// Producer
auto id = co_await redis.xadd("events:orders",
    {{"user", "42"}, {"amount", "99.99"}, {"ts", std::to_string(now)}});

// Consumer — read entries (block 5s if empty)
if (auto entries = co_await redis.xread("events:orders", "0", 10, 5000); entries) {
    // entries.result() is qb::json; parse stream data as needed
}
```

### Atomic Transaction (MULTI/EXEC)

Batch commands atomically. All succeed or all fail.

```cpp
co_await redis.multi();
co_await redis.set("key1", "value1");
co_await redis.set("key2", "value2");
co_await redis.lpush("history", event);

if (auto exec_r = co_await redis.exec<std::string>(); exec_r) {
    // All commands applied; exec_r.result() = vector of per-command replies
}
```

### Generic Command (Unwrapped)

For commands without a dedicated wrapper, use `command<T>()`:

```cpp
auto r = co_await redis.command<qb::json>("JSON.GET", "user:1", "$.name");
auto keys = co_await redis.command<std::vector<std::string>>(
    "COMMAND", "GETKEYS", "SET", "mykey", "value");
```

## Comprehensive Command Support

### Connection
`AUTH`, `ECHO`, `HELLO`, `PING`, `QUIT`, `RESET`, `SELECT`, `SWAPDB`

### String
`APPEND`, `DECR`, `DECRBY`, `GET`, `GETDEL`, `GETEX`, `GETRANGE`, `GETSET`, `INCR`, `INCRBY`, `INCRBYFLOAT`, `LCS`, `MGET`, `MSET`, `MSETNX`, `PSETEX`, `SET`, `SETEX`, `SETNX`, `SETRANGE`, `STRLEN`, `SUBSTR`

### Key
`COPY`, `DEL`, `DUMP`, `EXISTS`, `EXPIRE`, `EXPIREAT`, `EXPIRETIME`, `KEYS`, `MIGRATE`, `MOVE`, `OBJECT` (ENCODING, FREQ, IDLETIME, REFCOUNT), `PERSIST`, `PEXPIRE`, `PEXPIREAT`, `PEXPIRETIME`, `PTTL`, `RANDOMKEY`, `RENAME`, `RENAMENX`, `RESTORE`, `SCAN`, `SORT`, `SORT_RO`, `SORT STORE`, `TOUCH`, `TTL`, `TYPE`, `UNLINK`, `WAIT`, `WAITAOF`

### List
`BLMOVE`, `BLMPOP`, `BLPOP`, `BRPOP`, `BRPOPLPUSH`, `LINDEX`, `LINSERT`, `LLEN`, `LMOVE`, `LMPOP`, `LPOP`, `LPOS`, `LPUSH`, `LPUSHX`, `LRANGE`, `LREM`, `LSET`, `LTRIM`, `RPOP`, `RPOPLPUSH`, `RPUSH`, `RPUSHX`

### Hash
`HDEL`, `HEXISTS`, `HGET`, `HGETALL`, `HINCRBY`, `HKEYS`, `HLEN`, `HMGET`, `HMSET`, `HSCAN`, `HSET`, `HSETNX`, `HSTRLEN`, `HVALS`

### Set
`SADD`, `SCARD`, `SDIFF`, `SDIFFSTORE`, `SINTER`, `SINTERCARD`, `SINTERSTORE`, `SISMEMBER`, `SMEMBERS`, `SMOVE`, `SPOP`, `SRANDMEMBER`, `SREM`, `SSCAN`, `SUNION`, `SUNIONSTORE`

### Sorted Set
`BZMPOP`, `BZPOPMAX`, `BZPOPMIN`, `ZADD`, `ZCARD`, `ZCOUNT`, `ZDIFF`, `ZDIFFSTORE`, `ZINCRBY`, `ZINTER`, `ZINTERCARD`, `ZINTERSTORE`, `ZLEXCOUNT`, `ZMPOP`, `ZMSCORE`, `ZPOPMAX`, `ZPOPMIN`, `ZRANDMEMBER`, `ZRANGE`, `ZRANGEBYLEX`, `ZRANGEBYSCORE`, `ZRANGESTORE`, `ZRANK`, `ZREM`, `ZREMRANGEBYLEX`, `ZREMRANGEBYRANK`, `ZREMRANGEBYSCORE`, `ZREVRANGE`, `ZREVRANGEBYLEX`, `ZREVRANGEBYSCORE`, `ZREVRANK`, `ZSCAN`, `ZSCORE`, `ZUNIONSTORE`

### HyperLogLog
`PFADD`, `PFCOUNT`, `PFMERGE`

### Bitmap
`BITCOUNT`, `BITFIELD`, `BITFIELD_RO`, `BITOP`, `BITPOS`, `GETBIT`, `SETBIT`

### Stream
`XACK`, `XADD`, `XAUTOCLAIM`, `XCLAIM`, `XDEL`, `XGROUP CREATECONSUMER`, `XGROUP CREATE`, `XGROUP DELCONSUMER`, `XGROUP DESTROY`, `XGROUP SETID`, `XLEN`, `XPENDING`, `XRANGE`, `XREAD`, `XREADGROUP`, `XREVRANGE`, `XTRIM`

### Pub/Sub
`PUBLISH`, `PSUBSCRIBE`, `SUBSCRIBE`, `UNSUBSCRIBE`, `UNSUBSCRIBE` (pattern)

### Transaction
`DISCARD`, `EXEC`, `MULTI`, `UNWATCH`, `WATCH`

### Scripting
`EVAL`, `EVALSHA`, `EVAL_RO`, `EVALSHA_RO`, `SCRIPT DEBUG`, `SCRIPT EXISTS`, `SCRIPT FLUSH`, `SCRIPT KILL`, `SCRIPT LOAD`

### Function (Redis 7+)
`FCALL`, `FCALL_RO`, `FUNCTION DELETE`, `FUNCTION DUMP`, `FUNCTION FLUSH`, `FUNCTION KILL`, `FUNCTION LIST`, `FUNCTION LOAD`, `FUNCTION RESTORE`, `FUNCTION STATS`

### Server
`ACL`, `BGREWRITEAOF`, `BGSAVE`, `CLIENT` (ID, CACHING, GETREDIR, INFO, NO-EVICT, NO-TOUCH, REPLY, SETINFO, UNPAUSE, etc.), `COMMAND`, `CONFIG`, `DBSIZE`, `FAILOVER`, `FLUSHALL`, `FLUSHDB`, `INFO`, `LATENCY` (DOCTOR, GRAPH, HISTOGRAM), `LASTSAVE`, `MEMORY`, `MODULE`, `SAVE`, `SHUTDOWN`, `SLAVEOF`, `SLOWLOG`, `SYNC`, `TIME`

### Cluster
`ASKING`, `CLUSTER` (ADDSLOTS, ADDSLOTSRANGE, COUNT-FAILURE-REPORTS, DELSLOTS, DELSLOTSRANGE, FLUSHSLOTS, INFO, KEYSLOT, LINKS, MEET, MYID, NODES, REPLICAS, RESET, SETSLOT, SHARDS, SLAVES, SLOTS), `READONLY`, `READWRITE`

## Consumer Types

- **`qb::redis::tcp::client`** — Standard client for commands
- **`qb::redis::tcp::cb_consumer`** — Callback-based pub/sub consumer
- **`qb::redis::tcp::co_consumer`** — Coroutine-based pub/sub consumer

## Auto-Reconnect

```cpp
redis.enable_auto_reconnect(RetryPolicy{}
    .with_initial_delay(50ms)
    .with_max_delay(2s)
    .with_connect_timeout(2.0));
redis.disconnect();  // Triggers background reconnect
// After reconnect in RESP3: co_await redis.hello(3);
```

## Why qbm-redis?

| Feature | qbm-redis | Other C++ Redis clients |
|---------|-----------|--------------------------|
| **Dependencies** | Zero — no hiredis, no libuv | hiredis, redis-plus-plus, etc. |
| **Protocol** | Native RESP2/RESP3 parser | Often RESP2 only |
| **Async model** | C++23 coroutines + callbacks | Callbacks or threads |
| **API style** | `co_await redis.get("k")` | Verbose or sync-only |
| **Pub/Sub** | `cb_consumer` / `co_consumer` | Manual or limited |
| **Completeness** | 200+ commands, all groups | Partial coverage |
| **Integration** | QB Actor Framework | Standalone |

**One include, one link, one API.** No external Redis libraries. No blocking. No thread pools.

## Build Requirements

- **QB Framework** with `qb-io` (async I/O)
- **C++23** compiler
- **CMake 3.14+**
- **OpenSSL** (for SSL/TLS; `QB_IO_WITH_SSL=ON`)

**No hiredis** — uses native C++23 Redis protocol parser.

## Documentation

**📖 [Complete Documentation](./readme/README.md)**

- [Connection](./readme/connection.md) — Connect, HELLO, RESET, auto-reconnect
- [Commands Overview](./readme/commands_overview.md) — Coroutine vs callback API
- [Error Handling](./readme/error_handling.md) — `reply.ok()`, `reply.error()`
- Per-command docs: [string](./readme/string_commands.md), [key](./readme/key_commands.md), [list](./readme/list_commands.md), [hash](./readme/hash_commands.md), [set](./readme/set_commands.md), [sorted set](./readme/sorted_set_commands.md), [hyperloglog](./readme/hyperloglog_commands.md), [bitmap](./readme/bitmap_commands.md), [stream](./readme/stream_commands.md), [publish](./readme/publish_commands.md), [subscription](./readme/subscription_commands.md), [transaction](./readme/transaction_commands.md), [scripting](./readme/scripting_commands.md), [function](./readme/function_commands.md), [server](./readme/server_commands.md), [acl](./readme/acl_commands.md), [cluster](./readme/cluster_commands.md), [module](./readme/module_commands.md), [geo](./readme/geo_commands.md)

## License

Apache License 2.0. Part of the [QB Actor Framework](https://github.com/isndev/qb) ecosystem.
