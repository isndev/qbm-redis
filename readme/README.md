# QB Redis Module (`qbm-redis`) - Detailed Documentation

Welcome to the detailed documentation for the `qbm-redis` module of the QB C++ Actor Framework. This module provides a high-performance, fully asynchronous C++ client for Redis, designed for seamless integration with the QB ecosystem.

This documentation covers the module's architecture, key classes, command groups, and usage patterns.

## Core Concepts

*   **[Client Architecture & Connection](./connection.md):** Understanding `qb::redis::tcp::client`, connection URIs, and asynchronous connection handling.
*   **[Command Execution & Replies](./commands_overview.md):** How commands are structured (CRTP), synchronous vs. asynchronous execution (`.await()`), and processing results with `qb::redis::Reply<T>`.
*   **[Error Handling](./error_handling.md):** Handling Redis errors and connection issues.

## Command Groups

Detailed documentation for Redis commands, grouped by functionality:

*   **[String Commands](./string_commands.md):** (`GET`, `SET`, `INCR`, `APPEND`, etc.)
*   **[Hash Commands](./hash_commands.md):** (`HGET`, `HSET`, `HGETALL`, `HINCRBY`, etc.)
*   **[List Commands](./list_commands.md):** (`LPUSH`, `RPOP`, `LRANGE`, `BLPOP`, etc.)
*   **[Set Commands](./set_commands.md):** (`SADD`, `SISMEMBER`, `SMEMBERS`, `SUNION`, etc.)
*   **[Sorted Set Commands](./sorted_set_commands.md):** (`ZADD`, `ZRANGE`, `ZSCORE`, `ZINTERSTORE`, etc.)
*   **[Geospatial Commands](./geo_commands.md):** (`GEOADD`, `GEODIST`, `GEORADIUS`, etc.)
*   **[HyperLogLog Commands](./hyperloglog_commands.md):** (`PFADD`, `PFCOUNT`, `PFMERGE`)
*   **[Bitmap Commands](./bitmap_commands.md):** (`SETBIT`, `GETBIT`, `BITCOUNT`, `BITOP`)
*   **[Stream Commands](./stream_commands.md):** (`XADD`, `XREAD`, `XREADGROUP`, `XACK`, etc.)
*   **[Pub/Sub](./pubsub.md):** (`SUBSCRIBE`, `PUBLISH`, `PSUBSCRIBE`, `qb::redis::tcp::cb_consumer`)
*   **[Transactions](./transactions.md):** (`MULTI`, `EXEC`, `DISCARD`, `WATCH`)
*   **[Lua Scripting](./scripting.md):** (`EVAL`, `EVALSHA`, `SCRIPT LOAD`/`EXISTS`/`FLUSH`)
*   **[Server & Connection Commands](./server_connection.md):** (`PING`, `ECHO`, `INFO`, `CONFIG SET`/`GET`, `CLIENT LIST`, etc.)
*   **[Key Management Commands](./key_commands.md):** (`DEL`, `EXISTS`, `EXPIRE`, `KEYS`, `SCAN`, etc.)

## Examples

Refer to the unit tests in `qbm/redis/tests/` for practical examples of using each command group. 