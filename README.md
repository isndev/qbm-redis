# QB Redis Module (`qbm-redis`)

Welcome to `qbm-redis`, a high-performance, asynchronous Redis client meticulously crafted for the QB C++ Actor Framework. This module empowers your QB applications with robust, type-safe, and efficient communication with Redis, covering an extensive range of its functionalities.

Whether you're building a simple caching layer, a complex real-time data processing pipeline, or leveraging Redis's advanced data structures, `qbm-redis` provides a seamless and powerful interface.

## Why `qbm-redis`?

*   **Deep QB Integration:** Designed from the ground up to work harmoniously within the QB actor model and its asynchronous event loop, ensuring non-blocking operations and optimal performance in actor-based systems.
*   **Comprehensive Command Coverage:** Implements a vast majority of Redis commands across all major data types and server functionalities. From basic string operations to complex Stream manipulations, Pub/Sub, Lua scripting, and Cluster management, `qbm-redis` has you covered.
*   **Asynchronous & Synchronous APIs:** Provides both non-blocking asynchronous methods (ideal for actors) and traditional blocking synchronous calls (for simpler scripts or non-actor contexts), offering flexibility for various use cases.
*   **Type-Safe Replies:** Utilizes `qb::redis::Reply<T>` to wrap command results, ensuring compile-time type checking and clear, robust error handling mechanisms.
*   **Modern C++:** Leverages modern C++ features (C++17/20) for a clean, expressive, and efficient codebase.
*   **Ease of Use:** Simplified connection management and a consistent API design across command groups make interacting with Redis intuitive.

## Features at a Glance

*   **All Key Data Types:** Strings, Lists, Hashes, Sets, Sorted Sets.
*   **Advanced Data Structures:** Bitmaps, HyperLogLogs, Geospatial Indexes, Streams.
*   **Core Functionality:** Keyspace management, Transactions (MULTI/EXEC), Lua Scripting, Publish/Subscribe.
*   **Server & Cluster Management:** ACLs, Server Configuration, Client Management, Cluster Commands, Function and Module management.
*   **Detailed JSON Parsing:** Many commands that return complex data (like `INFO`, `CLIENT LIST`, `XINFO`, `FUNCTION LIST`) are parsed into `qb::json` objects for easy C++ manipulation.

## Quick Start: A Taste of `qbm-redis`

```cpp
#include <redis/redis.h> // Correct include path for QB modules
#include <qb/io.h>      // For qb::io::cout
#include <qb/actor.h>   // For QB Actor example
#include <qb/main.h>    // For QB Main engine

#include <string>
#include <vector>
#include <optional>
#include <iostream> // For std::cout in sync example

// --- Synchronous Example: Quick & Direct --- 
void sync_redis_example() {
    try {
        qb::redis::tcp::client redis("tcp://localhost:6379"); // Connects on construction
        std::cout << "Connected to Redis (sync)!" << std::endl;

        // 1. Simple SET & GET
        redis.set("greeting", "Hello from qbm-redis!");
        qb::redis::Reply<std::optional<std::string>> get_reply = redis.get("greeting");
        if (get_reply && get_reply.result().has_value()) {
            std::cout << "Retrieved: " << get_reply.result().value() << std::endl;
        }

        // 2. Increment a counter
        redis.set("mycounter", "100");
        qb::redis::Reply<long long> incr_reply = redis.incrby("mycounter", 50);
        if (incr_reply) {
            std::cout << "Counter is now: " << incr_reply.result() << std::endl;
        }

        // 3. Working with a List
        redis.del("mylist"); // Clean up previous list
        redis.rpush("mylist", "apple", "banana", "cherry");
        qb::redis::Reply<std::vector<std::string>> lrange_reply = redis.lrange("mylist", 0, -1);
        if (lrange_reply) {
            std::cout << "List items: ";
            for (const auto& item : lrange_reply.result()) {
                std::cout << item << " ";
            }
            std::cout << std::endl;
        }

        // 4. PING the server
        qb::redis::Reply<std::string> ping_reply = redis.ping();
        if (ping_reply.ok()) {
            std::cout << "Server PING response: " << ping_reply.result() << std::endl;
        } else {
            std::cout << "PING failed: " << ping_reply.error() << std::endl;
        }

    } catch (const qb::redis::connection_error& e) {
        std::cerr << "[Sync Example] Connection Error: " << e.what() << std::endl;
    } catch (const std::runtime_error& e) { // Catching general runtime_error for sync command failures
        std::cerr << "[Sync Example] Redis Command Error: " << e.what() << std::endl;
    } catch (const qb::redis::error& e) {
        std::cerr << "[Sync Example] Generic Redis Error: " << e.what() << std::endl;
    }
}

// --- Asynchronous Example: QB Actor Integration --- 
class RedisActor : public qb::Actor {
    qb::redis::tcp::client _redis; // Async client instance
    qb::io::timer _timer;

public:
    RedisActor() : _redis() {} // Does not connect in constructor for async setup

    bool onInit() override {
        qb::io::cout() << "Actor " << id() << ": Initializing and connecting to Redis..." << std::endl;
        _redis.connect("tcp://localhost:6379", [this](bool success) {
            if (success) {
                qb::io::cout() << "Actor " << id() << ": Redis connected asynchronously!" << std::endl;
                perform_sample_operations();
            } else {
                qb::io::cout() << "Actor " << id() << ": Redis async connection FAILED." << std::endl;
                kill(); // Or handle retry
            }
        });
        registerEvent<qb::KillEvent>(*this);
        return true;
    }

    void perform_sample_operations() {
        // 1. Asynchronous SET
        _redis.set("actor_message", "Hello from QB Actor via Redis!",
            [this](qb::redis::Reply<qb::redis::status>&& reply) {
            if (reply.ok()) {
                qb::io::cout() << "Actor " << id() << ": Async SET successful." << std::endl;
                // 2. Chain to an asynchronous GET
                this->_redis.get([this](qb::redis::Reply<std::optional<std::string>>&& get_reply) {
                    if (get_reply.ok() && get_reply.result().has_value()) {
                        qb::io::cout() << "Actor " << id() << ": Async GET: " << get_reply.result().value() << std::endl;
                    } else if (get_reply.ok()) {
                        qb::io::cout() << "Actor " << id() << ": Async GET: Key not found." << std::endl;
                    } else {
                        qb::io::cout() << "Actor " << id() << ": Async GET failed: " << get_reply.error() << std::endl;
                    }
                    // 3. Example of HSET and HGETALL
                    this->hash_operations();
                }, "actor_message");
            } else {
                qb::io::cout() << "Actor " << id() << ": Async SET failed: " << reply.error() << std::endl;
                kill();
            }
        });
    }

    void hash_operations() {
        std::string hash_key = "actor_hash";
        _redis.hset([this, hash_key](qb::redis::Reply<long long>&& hset_reply){
            if(hset_reply.ok()){
                qb::io::cout() << "Actor " << id() << ": Async HSET field1 successful (" << hset_reply.result() << " fields added)." << std::endl;
                _redis.hset([this, hash_key](qb::redis::Reply<long long>&& hset_reply2){
                     if(hset_reply2.ok()){
                        qb::io::cout() << "Actor " << id() << ": Async HSET field2 successful (" << hset_reply2.result() << " fields added)." << std::endl;
                        _redis.hgetall([this, hash_key](qb::redis::Reply<qb::unordered_map<std::string, std::string>>&& hgetall_reply){
                            if(hgetall_reply.ok()){
                                qb::io::cout() << "Actor " << id() << ": Async HGETALL " << hash_key << ": ";
                                for(const auto& pair : hgetall_reply.result()){
                                    qb::io::cout() << pair.first << "->" << pair.second << " ";
                                }
                                qb::io::cout() << std::endl;
                            }
                            // Signal completion
                            this->schedule_kill(); 
                        }, hash_key);
                     }
                }, hash_key, "field2", "another async value");
            }
        }, hash_key, "field1", "async hash value");
    }
    
    void schedule_kill(){
        _timer.schedule(id(), 100_ms, [this](){
            qb::io::cout() << "Actor " << id() << ": Operations complete, shutting down." << std::endl;
            kill();
        });
    }

    void on(const qb::KillEvent&) override {
        if (_redis.is_open()) { // Check if connection was even established
            _redis.disconnect_async([this](){
                qb::io::cout() << "Actor " << id() << ": Redis disconnected. Actor terminating." << std::endl;
                Actor::on(qb::KillEvent()); // Proceed with actor kill
            });
        } else {
             Actor::on(qb::KillEvent()); // Proceed if never connected
        }
    }
};

void async_actor_example() {
    qb::Main engine(1); // Run with 1 VirtualCore
    engine.addActor<RedisActor>(0);
    engine.start(true); // Run engine in a separate thread and wait for it to finish
    engine.join();
}

// To run these examples:
// int main() {
//     qb::io::async::init(); // Initialize async system for sync_example if it uses await()

//     std::cout << "--- Synchronous Redis Example ---" << std::endl;
//     sync_redis_example();
//     std::cout << std::endl;

//     std::cout << "--- Asynchronous QB Actor Redis Example ---" << std::endl;
//     async_actor_example();
//     std::cout << std::endl;
    
//     qb::io::async::shutdown();
//     return 0;
// }

## Diving Deeper: Full Documentation

Explore the comprehensive capabilities of `qbm-redis` through detailed documentation for each command group and core concepts:

*   **Core Concepts:**
    *   **[Client Architecture & Connection](./connection.md):** Understanding `qb::redis::tcp::client`, connection URIs, and asynchronous connection handling.
    *   **[Command Execution & Replies](./commands_overview.md):** How commands are structured (CRTP), synchronous vs. asynchronous execution, and processing results with `qb::redis::Reply<T>`.
    *   **[Error Handling](./error_handling.md):** Managing Redis errors and connection issues.

*   **Command Groups:**
    *   **[ACL Commands](./acl_commands.md):** (`ACL CAT`, `ACL GETUSER`, `ACL SETUSER`, etc.)
    *   **[Bitmap Commands](./bitmap_commands.md):** (`SETBIT`, `GETBIT`, `BITCOUNT`, `BITOP`)
    *   **[Cluster Commands](./cluster_commands.md):** (`CLUSTER INFO`, `CLUSTER NODES`, `CLUSTER MEET`, etc.)
    *   **[Connection Commands](./connection.md):** (Covers `AUTH`, `PING`, `ECHO`, `SELECT`, `QUIT` - detailed in the Connection doc)
    *   **[Function Commands](./function_commands.md):** (`FUNCTION LOAD`, `FUNCTION LIST`, `FUNCTION KILL`, etc.)
    *   **[Geospatial Commands](./geo_commands.md):** (`GEOADD`, `GEODIST`, `GEORADIUS`, etc.)
    *   **[Hash Commands](./hash_commands.md):** (`HGET`, `HSET`, `HGETALL`, `HINCRBY`, etc.)
    *   **[HyperLogLog Commands](./hyperloglog_commands.md):** (`PFADD`, `PFCOUNT`, `PFMERGE`)
    *   **[Key Commands](./key_commands.md):** (`DEL`, `EXISTS`, `KEYS`, `SCAN`, `TYPE`, `EXPIRE`, `TTL`, etc.)
    *   **[List Commands](./list_commands.md):** (`LPUSH`, `RPOP`, `LRANGE`, `BLPOP`, etc.)
    *   **[Module Commands](./module_commands.md):** (`MODULE LOAD`, `MODULE LIST`, `MODULE UNLOAD`, etc.)
    *   **[Publish/Subscribe (Publishing)](./publish_commands.md):** (`PUBLISH`)
    *   **[Publish/Subscribe (Subscription)](./subscription_commands.md):** (`SUBSCRIBE`, `PSUBSCRIBE`, `UNSUBSCRIBE`, `PUNSUBSCRIBE` - via `cb_consumer`)
    *   **[Scripting Commands](./scripting_commands.md):** (`EVAL`, `EVALSHA`, `SCRIPT LOAD`/`EXISTS`/`FLUSH`)
    *   **[Server Commands](./server_commands.md):** (`CLIENT` subcommands, `CONFIG`, `INFO`, `DBSIZE`, `FLUSHDB`, etc.)
    *   **[Set Commands](./set_commands.md):** (`SADD`, `SISMEMBER`, `SMEMBERS`, `SUNION`, etc.)
    *   **[Sorted Set Commands](./sorted_set_commands.md):** (`ZADD`, `ZRANGE`, `ZSCORE`, `ZINTERSTORE`, etc.)
    *   **[Stream Commands](./stream_commands.md):** (`XADD`, `XREAD`, `XREADGROUP`, `XACK`, etc.)
    *   **[String Commands](./string_commands.md):** (`GET`, `SET`, `INCR`, `APPEND`, etc.)
    *   **[Transaction Commands](./transaction_commands.md):** (`MULTI`, `EXEC`, `DISCARD`, `WATCH`)

## Building & Integration

To use `qbm-redis` in your QB project:

1.  Ensure `qb-core` (and its `qb-io` dependency) is built and available.
2.  The `qbm-redis` module depends on `hiredis` client library. Our CMake setup typically handles fetching or finding `hiredis`.
3.  In your project's `CMakeLists.txt`:

    ```cmake
    # If qbm-redis is installed system-wide or via CMake package config:
    # find_package(qbm-redis REQUIRED)

    # Or, if qbm-redis is a subdirectory in your project (e.g., via git submodule):
    # add_subdirectory(path/to/qbm-redis)

    # Link your executable or library target against qbm-redis
    # This also brings in transitive dependencies like qb-core and hiredis.
    target_link_libraries(your_target_name PRIVATE qbm::redis)
    ```

4.  Include the main header in your C++ files:

    ```cpp
    #include <redis/redis.h> 
    ```
    (Assuming `qbm` is an include directory, as is common in QB framework projects).

## Examples in Action

For comprehensive, runnable examples demonstrating the usage of nearly every command, please refer to the unit tests located in the `qbm/redis/tests/` directory of this module. Each `test-*-commands.cpp` file corresponds to a specific command group.


