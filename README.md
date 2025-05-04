# QB Redis Module (`qbm-redis`)

This module provides a high-performance, asynchronous Redis client integrated with the QB C++ Actor Framework. It leverages the non-blocking I/O capabilities of `qb-io` to offer efficient communication with Redis servers.

## Features

*   **Asynchronous & Synchronous API:** Offers both non-blocking asynchronous operations (ideal for use within actors) and blocking synchronous counterparts.
*   **Comprehensive Command Coverage:** Supports a wide range of Redis commands across various data types (Strings, Lists, Hashes, Sets, Sorted Sets, Geo, HyperLogLog, Bitmaps, Streams, Pub/Sub, Scripting, Transactions).
*   **Type-Safe Replies:** Uses a `qb::redis::Reply<T>` wrapper for command results, providing type safety and clear error handling.
*   **Connection Management:** Handles connection establishment, authentication, and potential reconnections (depending on usage pattern).
*   **Pub/Sub Support:** Includes dedicated consumer classes for handling Redis Publish/Subscribe messages.
*   **Integration with QB:** Designed to work seamlessly within the QB actor model and event loop.

## Quick Start

```cpp
#include <qbm/redis/redis.h>
#include <qb/io.h> // For qb::io::cout
#include <string>
#include <vector>

// --- Synchronous Example ---
void sync_example() {
    try {
        qb::redis::tcp::client redis("tcp://127.0.0.1:6379"); // Connect

        // Simple SET and GET
        qb::redis::status set_reply = redis.set("mykey", "hello");
        if (set_reply) {
            qb::io::cout() << "SET status: " << set_reply.value() << std::endl;
        } else {
            qb::io::cout() << "SET failed: " << set_reply.error().what() << std::endl;
            return;
        }

        qb::redis::Reply<std::optional<std::string>> get_reply = redis.get("mykey");
        if (get_reply && get_reply.value().has_value()) {
            qb::io::cout() << "GET mykey: " << get_reply.value().value() << std::endl;
        } else if (get_reply) {
            qb::io::cout() << "GET mykey: (nil)" << std::endl;
        }
         else {
            qb::io::cout() << "GET failed: " << get_reply.error().what() << std::endl;
        }

        // PING
        qb::redis::status ping_reply = redis.ping();
        qb::io::cout() << "PING: " << (ping_reply ? "PONG" : "Failed") << std::endl;

    } catch (const qb::redis::connection_error& e) {
        qb::io::cout() << "Connection Error: " << e.what() << std::endl;
    } catch (const qb::redis::error& e) {
         qb::io::cout() << "Redis Error: " << e.what() << std::endl;
    }
}


// --- Asynchronous Example (within a QB Actor) ---
#include <qb/actor.h>
#include <qb/main.h>

class RedisActor : public qb::Actor {
    qb::redis::tcp::client _redis; // Async client instance

public:
    RedisActor() : _redis("tcp://127.0.0.1:6379") {}

    bool onInit() override {
        // Asynchronously connect
        _redis.connect_async([this](bool success) {
            if (success) {
                qb::io::cout() << "Actor " << id() << ": Redis connected asynchronously.
";
                // Start operations after connection
                perform_redis_operations();
            } else {
                qb::io::cout() << "Actor " << id() << ": Redis async connection failed.
";
                kill(); // Or implement retry logic
            }
        });
        registerEvent<qb::KillEvent>(*this);
        return true;
    }

    void perform_redis_operations() {
        // Asynchronous SET
        _redis.set_async("actor_key", "async_value", [this](qb::redis::status&& reply) {
            if (reply) {
                 qb::io::cout() << "Actor " << id() << ": Async SET OK.
";
                 // Chain operations: GET after successful SET
                 get_key_async();
            } else {
                 qb::io::cout() << "Actor " << id() << ": Async SET failed: " << reply.error().what() << "
";
                 kill();
            }
        });
    }

    void get_key_async() {
         // Asynchronous GET
        _redis.get_async("actor_key", [this](qb::redis::Reply<std::optional<std::string>>&& reply) {
             if (reply && reply.value().has_value()) {
                 qb::io::cout() << "Actor " << id() << ": Async GET actor_key: " << reply.value().value() << "
";
             } else if (reply) {
                 qb::io::cout() << "Actor " << id() << ": Async GET actor_key: (nil)
";
             } else {
                  qb::io::cout() << "Actor " << id() << ": Async GET failed: " << reply.error().what() << "
";
             }
             // Finished operations for this example
             kill();
        });
    }

    void on(const qb::KillEvent&) {
        _redis.disconnect_async([](){}); // Disconnect async before killing
        kill();
    }
};

void async_example() {
    qb::Main engine;
    engine.addActor<RedisActor>(0);
    engine.start(false); // Run synchronously for simple example
}

// int main() {
//     std::cout << "--- Sync Example ---" << std::endl;
//     sync_example();
//     std::cout << "
--- Async Example ---" << std::endl;
//     async_example();
//     return 0;
// }
```

## Documentation

Detailed documentation for specific command groups and features can be found in the `readme/` directory:

*   **[Connection](./readme/connection.md):** Connecting, Authentication, PING, SELECT.
*   **[Commands Overview](./readme/commands_overview.md):** General command execution, synchronous vs. asynchronous calls, `Reply<T>` handling.
*   **[Error Handling](./readme/error_handling.md):** Understanding and managing connection and command errors.
*   **[String Commands](./readme/string_commands.md):** GET, SET, INCR, APPEND, etc.
*   **[List Commands](./readme/list_commands.md):** LPUSH, RPOP, LRANGE, etc.
*   **[Hash Commands](./readme/hash_commands.md):** HSET, HGET, HGETALL, etc.
*   **[Set Commands](./readme/set_commands.md):** SADD, SMEMBERS, SINTER, etc.
*   **[Sorted Set Commands](./readme/sorted_set_commands.md):** ZADD, ZRANGE, ZRANK, etc.
*   **[Geo Commands](./readme/geo_commands.md):** GEOADD, GEODIST, GEORADIUS, etc.
*   **[HyperLogLog Commands](./readme/hyperloglog_commands.md):** PFADD, PFCOUNT, PFMERGE.
*   **[Bitmap Commands](./readme/bitmap_commands.md):** SETBIT, GETBIT, BITCOUNT, BITOP.
*   **[Stream Commands](./readme/stream_commands.md):** XADD, XREAD, XREADGROUP, XACK, etc.
*   **[Publish/Subscribe](./readme/publish_commands.md):** PUBLISH, SUBSCRIBE, PSUBSCRIBE.
*   **[Scripting Commands](./readme/scripting_commands.md):** EVAL, EVALSHA, SCRIPT LOAD/EXISTS/FLUSH.
*   **[Transaction Commands](./readme/transaction_commands.md):** MULTI, EXEC, DISCARD, WATCH, UNWATCH.
*   **[Key Commands](./readme/key_commands.md):** DEL, EXISTS, KEYS, SCAN, TYPE, EXPIRE, TTL, etc.
*   **[Server Commands](./readme/server_commands.md):** CLIENT, CONFIG, INFO, DBSIZE, FLUSHDB, etc.

## Building

Ensure `qb-core` is built or installed. Then, include this module in your CMake project:

```cmake
# Find the installed qbm-redis package
# find_package(qbm-redis REQUIRED)

# Or, if building alongside source:
# add_subdirectory(path/to/qbm/redis)

# Link your target against the redis module
# target_link_libraries(your_target PRIVATE qbm::redis)
```

## Dependencies

*   `qb-core`
*   `hiredis` (likely bundled or fetched by CMake)


