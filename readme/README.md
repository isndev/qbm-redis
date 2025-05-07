# QB Redis Module (`qbm-redis`) - Detailed Documentation

Welcome to the detailed documentation for the `qbm-redis` module of the QB C++ Actor Framework. This module provides a high-performance, fully asynchronous C++ client for Redis, designed for seamless integration with the QB ecosystem.

This documentation covers the module's architecture, key classes, command groups, and usage patterns.

## Core Concepts

*   **[Client Architecture & Connection](./connection.md):** Understanding `qb::redis::tcp::client`, connection URIs, and asynchronous connection handling.
*   **[Command Execution & Replies](./commands_overview.md):** How commands are structured (CRTP), synchronous vs. asynchronous execution (`.await()`), and processing results with `qb::redis::Reply<T>`.
*   **[Error Handling](./error_handling.md):** Handling Redis errors and connection issues.

## Command Groups

Detailed documentation for Redis commands, grouped by functionality:

*   **[ACL Commands](./acl_commands.md):** (`ACL CAT`, `ACL GETUSER`, `ACL SETUSER`, etc.)
*   **[Bitmap Commands](./bitmap_commands.md):** (`SETBIT`, `GETBIT`, `BITCOUNT`, `BITOP`)
*   **[Cluster Commands](./cluster_commands.md):** (`CLUSTER INFO`, `CLUSTER NODES`, `CLUSTER MEET`, etc.)
*   **[Connection Commands](./connection.md):** (`AUTH`, `PING`, `ECHO`, `SELECT`, `QUIT`)
*   **[Function Commands](./function_commands.md):** (`FUNCTION LOAD`, `FUNCTION LIST`, `FUNCTION CALL`, etc.)
*   **[Geospatial Commands](./geo_commands.md):** (`GEOADD`, `GEODIST`, `GEORADIUS`, etc.)
*   **[Hash Commands](./hash_commands.md):** (`HGET`, `HSET`, `HGETALL`, `HINCRBY`, etc.)
*   **[HyperLogLog Commands](./hyperloglog_commands.md):** (`PFADD`, `PFCOUNT`, `PFMERGE`)
*   **[Key Commands](./key_commands.md):** (`DEL`, `EXISTS`, `KEYS`, `SCAN`, `TYPE`, `EXPIRE`, `TTL`, etc.)
*   **[List Commands](./list_commands.md):** (`LPUSH`, `RPOP`, `LRANGE`, `BLPOP`, etc.)
*   **[Module Commands](./module_commands.md):** (`MODULE LOAD`, `MODULE LIST`, etc.)
*   **[Publish/Subscribe](./publish_commands.md):** (`PUBLISH`, `SUBSCRIBE`, `PSUBSCRIBE` - also see Subscription Commands)
*   **[Scripting Commands](./scripting_commands.md):** (`EVAL`, `EVALSHA`, `SCRIPT LOAD`/`EXISTS`/`FLUSH`)
*   **[Server Commands](./server_commands.md):** (`CLIENT`, `CONFIG`, `INFO`, `DBSIZE`, `FLUSHDB`, etc.)
*   **[Set Commands](./set_commands.md):** (`SADD`, `SISMEMBER`, `SMEMBERS`, `SUNION`, etc.)
*   **[Sorted Set Commands](./sorted_set_commands.md):** (`ZADD`, `ZRANGE`, `ZSCORE`, `ZINTERSTORE`, etc.)
*   **[Stream Commands](./stream_commands.md):** (`XADD`, `XREAD`, `XREADGROUP`, `XACK`, etc.)
*   **[String Commands](./string_commands.md):** (`GET`, `SET`, `INCR`, `APPEND`, etc.)
*   **[Subscription Commands](./subscription_commands.md):** (`SUBSCRIBE`, `PSUBSCRIBE`, `UNSUBSCRIBE` - often used with `cb_consumer`)
*   **[Transaction Commands](./transaction_commands.md):** (`MULTI`, `EXEC`, `DISCARD`, `WATCH`)

## Examples

Refer to the unit tests in `qbm/redis/tests/` for practical examples of using each command group. 