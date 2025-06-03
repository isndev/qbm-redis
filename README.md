# QB Redis Module (`qbm-redis`)

**High-Performance, Asynchronous Redis Client for the QB Actor Framework**

<p align="center">
  <img src="https://img.shields.io/badge/Redis-6%2B-red.svg" alt="Redis"/>
  <img src="https://img.shields.io/badge/C%2B%2B-17-blue.svg" alt="C++17"/>
  <img src="https://img.shields.io/badge/Cross--Platform-Linux%20%7C%20macOS%20%7C%20Windows-lightgrey.svg" alt="Cross Platform"/>
  <img src="https://img.shields.io/badge/Arch-x86__64%20%7C%20ARM64-lightgrey.svg" alt="Architecture"/>
  <img src="https://img.shields.io/badge/SSL-TLS-green.svg" alt="SSL/TLS"/>
  <img src="https://img.shields.io/badge/License-Apache%202.0-green.svg" alt="License"/>
</p>

`qbm-redis` delivers comprehensive Redis capabilities to the QB Actor Framework, providing both synchronous and asynchronous APIs for all major Redis operations. Built on QB's non-blocking I/O foundation, it enables high-performance Redis interactions without actor blocking, perfect for caching, pub/sub messaging, and real-time data processing.

## Quick Integration with QB

### Adding to Your QB Project

```bash
# Add the module as a submodule
git submodule add https://github.com/isndev/qbm-redis qbm/redis
```

### CMake Setup

```cmake
# QB framework setup
add_subdirectory(qb)
include_directories(${QB_PATH}/include)

# Load QB modules (automatically discovers qbm-redis)
qb_load_modules("${CMAKE_CURRENT_SOURCE_DIR}/qbm")

# Link against the Redis module
target_link_libraries(your_target PRIVATE qbm::redis)
```

### Include and Use

```cpp
#include <redis/redis.h>
```

## Why Choose `qbm-redis`?

**Complete Redis Coverage**: Supports all major Redis data types, commands, and features including pub/sub, streams, scripting, and clustering.

**Dual API Design**: Both asynchronous (perfect for actors) and synchronous APIs for maximum flexibility across different use cases.

**Type-Safe Operations**: Strongly typed Redis operations with automatic serialization/deserialization for C++ types.

**Performance Optimized**: Connection pooling, pipelining support, and zero-copy operations where possible.

**Cross-Platform**: Same code runs on Linux, macOS, Windows (x86_64, ARM64) with identical performance.

## Quick Start: Your First Redis Actor

```cpp
#include <redis/redis.h>
#include <qb/main.h>

class CacheActor : public qb::Actor {
    qb::redis::tcp::client _redis{{"tcp://localhost:6379"}};
    
public:
    bool onInit() override {
        if (!_redis.connect()) {
            qb::io::cout() << "Failed to connect to Redis" << std::endl;
            return false;
        }
        
        qb::io::cout() << "Connected to Redis successfully!" << std::endl;
        
        // Basic SET operation
        if (!_redis.set("greeting", "Hello Redis!")) {
            qb::io::cout() << "SET operation failed" << std::endl;
            return false;
        }
        qb::io::cout() << "Key set successfully!" << std::endl;
        
        // Basic GET operation
        auto value = _redis.get("greeting");
        if (value.has_value()) {
            qb::io::cout() << "Retrieved: " << *value << std::endl;
        } else {
            qb::io::cout() << "Key not found" << std::endl;
        }
        
        // Cleanup
        _redis.del("greeting");
        kill(); // Done
        
        return true;
    }
};

int main() {
    qb::Main engine;
    engine.addActor<CacheActor>(0);
    engine.start();
    return 0;
}
```

That's it! Clean, actor-based Redis operations with full synchronous support.

## Real-World Examples

### Session Store with Expiration

```cpp
#include <redis/redis.h>
#include <qb/main.h>

class SessionManager : public qb::Actor {
    qb::redis::tcp::client _redis{{"tcp://localhost:6379"}};

public:
    bool onInit() override {
        if (!_redis.connect()) {
            return false;
        }
        
        // Demo: Create and manage user sessions
        create_session("user:123", "session_data_here");
        return true;
    }

private:
    void create_session(const std::string& user_id, const std::string& session_data) {
        // Create session with 1 hour expiration
        std::string session_key = "session:" + user_id;
        
        if (_redis.setex(session_key, 3600, session_data)) {
            qb::io::cout() << "Session created for user: " << user_id << std::endl;
            check_session_ttl(user_id);
                    } else {
            qb::io::cout() << "Failed to create session" << std::endl;
            kill();
        }
    }
    
    void check_session_ttl(const std::string& user_id) {
        std::string session_key = "session:" + user_id;
        
        auto ttl = _redis.ttl(session_key);
        qb::io::cout() << "Session TTL for " << user_id << ": " << ttl << " seconds" << std::endl;
        
        if (ttl > 3000) {
            extend_session(user_id);
            } else {
            qb::io::cout() << "Session will expire soon" << std::endl;
                kill();
            }
    }
    
    void extend_session(const std::string& user_id) {
        std::string session_key = "session:" + user_id;
        
        auto result = _redis.expire(session_key, 7200); // Extend to 2 hours
        if (result == 1) {
            qb::io::cout() << "Session extended successfully" << std::endl;
        } else {
            qb::io::cout() << "Failed to extend session" << std::endl;
        }
        kill();
    }
};

int main() {
    qb::Main engine;
    engine.addActor<SessionManager>(0);
    engine.start();
    return 0;
}
```

### Real-time Leaderboard System

```cpp
#include <redis/redis.h>
#include <qb/main.h>

class LeaderboardActor : public qb::Actor {
    qb::redis::tcp::client _redis{{"tcp://localhost:6379"}};
    
public:
    bool onInit() override {
        if (!_redis.connect()) {
            return false;
        }
        
        // Demo: Gaming leaderboard operations
        setup_leaderboard();
        return true;
    }
    
private:
    void setup_leaderboard() {
        // Add some sample scores
        _redis.zadd("game:leaderboard", 1000, "player1");
        _redis.zadd("game:leaderboard", 1500, "player2");
        _redis.zadd("game:leaderboard", 2000, "player3");
        _redis.zadd("game:leaderboard", 750, "player4");
        
        qb::io::cout() << "Added players to leaderboard" << std::endl;
        show_top_players();
    }
    
    void show_top_players() {
        // Get top 3 players with scores
        auto results = _redis.zrevrange_withscores("game:leaderboard", 0, 2);
        
        qb::io::cout() << "=== Top Players ===" << std::endl;
        for (size_t i = 0; i < results.size(); i += 2) {
            std::string player = results[i];
            std::string score = results[i + 1];
            qb::io::cout() << "#" << (i/2 + 1) << ": " << player 
                           << " (Score: " << score << ")" << std::endl;
        }
        
        update_player_score();
    }
    
    void update_player_score() {
        // Player achieves new high score
        _redis.zadd("game:leaderboard", 2500, "player1");
        qb::io::cout() << "Updated player1 score!" << std::endl;
        
        // Check new rank
        auto rank = _redis.zrevrank("game:leaderboard", "player1");
        if (rank.has_value()) {
            qb::io::cout() << "player1 new rank: #" << (*rank + 1) << std::endl;
        }
        kill();
    }
};

int main() {
    qb::Main engine;
    engine.addActor<LeaderboardActor>(0);
    engine.start();
    return 0;
}
```

### Pub/Sub Messaging System

```cpp
#include <redis/redis.h>
#include <qb/main.h>

class ChatPublisher : public qb::Actor {
    qb::redis::tcp::client _redis{{"tcp://localhost:6379"}};
    
public:
    bool onInit() override {
        if (!_redis.connect()) {
            return false;
        }
        
        // Publish messages to chat channels
        publish_messages();
        return true;
    }
    
private:
    void publish_messages() {
        auto count1 = _redis.publish("chat:general", "Hello everyone!");
        qb::io::cout() << "Published to " << count1 << " subscribers" << std::endl;
        
        auto count2 = _redis.publish("chat:announcements", "Server maintenance in 1 hour");
        qb::io::cout() << "Announcement sent to " << count2 << " subscribers" << std::endl;
        
        kill();
    }
};

class ChatSubscriber : public qb::Actor {
    qb::redis::tcp::cb_consumer _consumer{{"tcp://localhost:6379"}, [this](auto&& msg) {
        // Callback called when message is received
        qb::io::cout() << "[" << msg.channel << "] " << msg.message << std::endl;
    }};
    
public:
    bool onInit() override {
        if (!_consumer.connect()) {
            return false;
        }
        
        // Subscribe to chat channels
        auto result1 = _consumer.subscribe("chat:general");
        auto result2 = _consumer.subscribe("chat:announcements");
        
        if (result1.channel.has_value() && result2.channel.has_value()) {
            qb::io::cout() << "Successfully subscribed to chat channels" << std::endl;
        } else {
            qb::io::cout() << "Failed to subscribe" << std::endl;
            return false;
        }
        
        return true;
    }
};

int main() {
    qb::Main engine;
    
    // Start subscriber first
    engine.addActor<ChatSubscriber>(0);
    
    // Publisher sends messages after delay
    qb::io::async::callback([&engine]() {
        engine.addActor<ChatPublisher>(0);
    }, 1.0); // 1 second delay
    
    engine.start();
    return 0;
}
```

### Synchronous Usage (for Scripts)

    ```cpp
    #include <redis/redis.h> 

int main() {
    qb::io::async::init(); // Required for sync usage
    
    qb::redis::tcp::client redis{{"tcp://localhost:6379"}};
    
    if (!redis.connect()) {
        qb::io::cerr() << "Connection failed" << std::endl;
        return 1;
    }
    
    qb::io::cout() << "Connected to Redis successfully!" << std::endl;
    
    // Synchronous operations
    if (redis.set("sync_key", "sync_value")) {
        qb::io::cout() << "Key set successfully" << std::endl;
    }
    
    auto get_result = redis.get("sync_key");
    if (get_result.has_value()) {
        qb::io::cout() << "Retrieved: " << *get_result << std::endl;
    }
    
    // Work with lists
    redis.rpush("my_list", "item1");
    redis.rpush("my_list", "item2");
    redis.rpush("my_list", "item3");
    
    auto list_result = redis.lrange("my_list", 0, -1);
    qb::io::cout() << "List contents:" << std::endl;
    for (const auto& item : list_result) {
        qb::io::cout() << "  - " << item << std::endl;
    }
    
    // Hash operations
    redis.hset("user:1001", "name", "Alice");
    redis.hset("user:1001", "email", "alice@example.com");
    redis.hset("user:1001", "age", "25");
    
    auto hash_result = redis.hgetall("user:1001");
    qb::io::cout() << "User data:" << std::endl;
    for (const auto& [field, value] : hash_result) {
        qb::io::cout() << "  " << field << ": " << value << std::endl;
    }
    
    // Cleanup
    redis.del("sync_key", "my_list", "user:1001");
    
    return 0;
}
```

## Comprehensive Redis Command Support

**String Operations**: `GET`, `SET`, `INCR`, `APPEND`, `GETRANGE`, `SETEX`, `SETNX`, and more.

**Hash Operations**: `HGET`, `HSET`, `HGETALL`, `HINCRBY`, `HEXISTS`, `HDEL`, field operations.

**List Operations**: `LPUSH`, `RPUSH`, `LPOP`, `RPOP`, `LRANGE`, `LINDEX`, blocking operations.

**Set Operations**: `SADD`, `SREM`, `SMEMBERS`, `SISMEMBER`, `SUNION`, `SINTER`, `SDIFF`.

**Sorted Set Operations**: `ZADD`, `ZRANGE`, `ZREVRANGE`, `ZSCORE`, `ZRANK`, `ZINTERSTORE`.

**Advanced Features**:
- **Streams**: `XADD`, `XREAD`, `XREADGROUP`, `XACK`, consumer groups
- **Pub/Sub**: `PUBLISH`, `SUBSCRIBE`, `PSUBSCRIBE`, pattern matching
- **Transactions**: `MULTI`, `EXEC`, `DISCARD`, `WATCH`
- **Scripting**: `EVAL`, `EVALSHA`, Lua script execution
- **Clustering**: `CLUSTER` commands for distributed setups

## Features

**Dual API**: Both asynchronous (callback-based) and synchronous (blocking) operations.

**Type Safety**: Automatic serialization for C++ types with `qb::redis::Reply<T>` wrappers.

**Connection Management**: Automatic reconnection, connection pooling, and SSL support.

**Performance**: Pipelining support, connection reuse, and optimized data structures.

**Error Handling**: Comprehensive Redis error reporting with detailed diagnostics.

**Actor Integration**: Perfect integration with QB's actor model and event loop.

## Build Information

### Requirements
- **QB Framework**: This module requires the QB Actor Framework as its foundation
- **C++17** compatible compiler
- **CMake 3.14+**
- **hiredis**: Redis C client library (automatically managed by CMake)

### Optional Dependencies
- **OpenSSL**: For secure Redis connections (SSL/TLS). Enable with `QB_IO_WITH_SSL=ON`

### Building with QB
When using the QB project template, simply add this module as shown in the integration section above. The `qb_load_modules()` function will automatically handle the configuration.

### Manual Build (Advanced)
```cmake
# If building outside QB framework context
find_package(qb REQUIRED)
target_link_libraries(your_target PRIVATE qbm-redis)
```

## Advanced Documentation

For in-depth technical documentation, implementation details, and comprehensive API reference:

**📖 [Complete Redis Module Documentation](./readme/README.md)**

This detailed documentation covers:
- **[Connection Management](./readme/connection.md)** - Connection handling, pooling, and configuration
- **[Error Handling](./readme/error_handling.md)** - Comprehensive error management strategies
- **[Commands Overview](./readme/commands_overview.md)** - Complete command reference and usage patterns
- **[String Commands](./readme/string_commands.md)** - All string operations with examples
- **[Hash Commands](./readme/hash_commands.md)** - Hash data structure operations
- **[List Commands](./readme/list_commands.md)** - List manipulation and blocking operations
- **[Set Commands](./readme/set_commands.md)** - Set operations and intersections
- **[Sorted Set Commands](./readme/sorted_set_commands.md)** - Ranked data operations
- **[Key Commands](./readme/key_commands.md)** - Key management and expiration
- **[Pub/Sub Commands](./readme/publish_commands.md)** - Publish/subscribe messaging
- **[Stream Commands](./readme/stream_commands.md)** - Redis Streams for event processing
- **[Transaction Commands](./readme/transaction_commands.md)** - MULTI/EXEC transactions
- **[Scripting Commands](./readme/scripting_commands.md)** - Lua script execution
- **[Server Commands](./readme/server_commands.md)** - Server administration
- **[ACL Commands](./readme/acl_commands.md)** - Access control lists
- **[Cluster Commands](./readme/cluster_commands.md)** - Redis cluster operations
- **[Subscription Commands](./readme/subscription_commands.md)** - Advanced pub/sub patterns

## Documentation & Examples

For comprehensive examples and detailed usage patterns:

- **[QB Examples Repository](https://github.com/isndev/qb-examples):** Real-world Redis integration patterns
- **Unit Tests**: The `qbm/redis/tests/` directory contains extensive test coverage for all command groups

**Example Categories:**
- Caching and session management
- Real-time leaderboards and analytics
- Pub/sub messaging systems
- Stream processing patterns
- Lua scripting integration
- Clustering and high availability

## Contributing

We welcome contributions! Please see the main [QB Contributing Guidelines](https://github.com/isndev/qb/blob/master/CONTRIBUTING.md) for details.

## License

Licensed under the Apache License, Version 2.0. See [LICENSE](./LICENSE) for details.

## Acknowledgments

The QB Redis Module builds upon the excellent work of:

- **[hiredis](https://github.com/redis/hiredis)** - For Redis protocol parsing structures (I/O handled by qb-io)

This library enables the module to efficiently parse Redis protocol responses while maintaining QB's high-performance asynchronous I/O capabilities.

---

**Part of the [QB Actor Framework](https://github.com/isndev/qb) ecosystem - Build the future of concurrent C++ applications.**


