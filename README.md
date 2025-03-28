# QB Redis Module

A comprehensive and type-safe C++ client for Redis, implementing the entire Redis API for both synchronous and asynchronous operations.

## Features

- Complete implementation of Redis commands organized by logical groups
- Type-safe return values using modern C++ features and templates
- Connection pooling and connection management
- Both synchronous and asynchronous APIs for all commands
- Support for transactions and pipelining
- Pub/Sub messaging support
- Lua scripting support
- Comprehensive error handling

## Architecture

The Redis module is designed using the Curiously Recurring Template Pattern (CRTP) to implement command groups without requiring virtual inheritance.

### Command Groups

The module organizes Redis commands into logical groups:

- `key_commands`: Key management operations (TTL, exists, etc.)
- `string_commands`: String operations (get, set, incr, etc.)
- `hash_commands`: Hash map operations (hget, hset, etc.)
- `list_commands`: List operations (lpush, lpop, etc.)
- `set_commands`: Set operations (sadd, sismember, etc.)
- `sorted_set_commands`: Sorted set operations (zadd, zrange, etc.)
- `geo_commands`: Geospatial operations (geoadd, geodist, etc.)
- `publish_commands`: Publishing operations (publish)
- `subscription_commands`: Subscription operations (subscribe, psubscribe, etc.)
- `connection_commands`: Connection operations (echo, ping, etc.)
- `server_commands`: Server operations (info, time, etc.)
- `transaction_commands`: Transaction operations (multi, exec, etc.)
- `scripting_commands`: Lua scripting operations (eval, evalsha, etc.)
- `pipeline_commands`: Command pipelining

### Redis Client Class

The main `Redis` class inherits from all command groups, providing access to all Redis commands. The class is parameterized by a connection type, allowing for different connection implementations to be used (TCP, Unix sockets, etc.).

## Usage

### Basic Connection

```cpp
#include "qbm/redis/redis.h"

// Create a Redis client
qb::redis::Redis redis("127.0.0.1", 6379);

// Connect to the server
if (!redis.connect()) {
    std::cerr << "Failed to connect to Redis server" << std::endl;
    return 1;
}
```

### String Operations

```cpp
// Set a string value
redis.set("key", "value");

// Get a string value
auto value = redis.get("key");
if (value) {
    std::cout << "Value: " << *value << std::endl;
} else {
    std::cout << "Key does not exist" << std::endl;
}

// Increment a counter
auto count = redis.incr("counter");
std::cout << "Counter value: " << count << std::endl;
```

### Hash Operations

```cpp
// Set hash fields
redis.hset("user:1", "name", "John");
redis.hset("user:1", "email", "john@example.com");

// Get all hash fields and values
auto user = redis.hgetall("user:1");
for (const auto& [field, value] : user) {
    std::cout << field << ": " << value << std::endl;
}
```

### List Operations

```cpp
// Push items to a list
redis.lpush("tasks", "task1", "task2", "task3");

// Get the length of the list
auto length = redis.llen("tasks");
std::cout << "Number of tasks: " << length << std::endl;

// Pop an item from the list
auto task = redis.lpop("tasks");
if (task) {
    std::cout << "Next task: " << *task << std::endl;
}
```

### Set Operations

```cpp
// Add members to a set
redis.sadd("tags", "redis", "database", "nosql");

// Check if a member exists in the set
bool exists = redis.sismember("tags", "redis");
std::cout << "Redis tag exists: " << (exists ? "yes" : "no") << std::endl;

// Get all members of the set
auto tags = redis.smembers("tags");
for (const auto& tag : tags) {
    std::cout << "Tag: " << tag << std::endl;
}
```

### Asynchronous Operations

```cpp
// Asynchronous SET
redis.set([](auto&& reply) {
    if (reply.ok()) {
        std::cout << "SET operation succeeded" << std::endl;
    } else {
        std::cerr << "SET operation failed: " << reply.error << std::endl;
    }
}, "key", "value");

// Asynchronous GET
redis.get([](auto&& reply) {
    if (reply.ok() && reply.result()) {
        std::cout << "Value: " << *reply.result() << std::endl;
    } else if (reply.ok()) {
        std::cout << "Key does not exist" << std::endl;
    } else {
        std::cerr << "GET operation failed: " << reply.error << std::endl;
    }
}, "key");
```

### Transactions

```cpp
// Start a transaction
redis.multi();

// Queue commands
redis.set("key1", "value1");
redis.set("key2", "value2");
redis.incr("counter");

// Execute the transaction
auto results = redis.exec();
if (results.ok()) {
    for (const auto& result : results.result()) {
        // Process each result
    }
} else {
    std::cerr << "Transaction failed: " << results.error << std::endl;
}
```

### Pipelining

```cpp
// Start pipelining commands
redis.pipeline_begin();

// Queue commands
redis.set("key1", "value1");
redis.set("key2", "value2");
redis.incr("counter");

// Execute the pipeline
auto results = redis.pipeline_exec();
for (const auto& result : results) {
    // Process each result
}
```

### Pub/Sub Messaging

```cpp
// Create a Redis consumer for subscriptions
qb::redis::RedisConsumer consumer("127.0.0.1", 6379);

// Set up a message handler
consumer.set_message_handler([](const std::string& channel, const std::string& message) {
    std::cout << "Received message from channel " << channel << ": " << message << std::endl;
});

// Subscribe to a channel
consumer.subscribe("news");

// Start the consumer loop in a separate thread
std::thread consumer_thread([&consumer]() {
    consumer.run();
});

// Publish a message from the main Redis client
redis.publish("news", "Hello, world!");

// Unsubscribe and stop the consumer
consumer.unsubscribe("news");
consumer_thread.join();
```

### Lua Scripting

```cpp
// Execute a Lua script
auto result = redis.eval(
    "return {KEYS[1], KEYS[2], ARGV[1], ARGV[2]}",
    2,  // Number of keys
    "key1", "key2",  // Keys
    "arg1", "arg2"   // Arguments
);

// Use script caching with EVALSHA
auto sha = redis.script_load("return {KEYS[1], ARGV[1]}");
auto cached_result = redis.evalsha(
    sha,
    1,  // Number of keys
    "key1",  // Keys
    "arg1"   // Arguments
);
```

## Error Handling

All commands return a `Reply` object that includes:

- `ok`: Boolean indicating if the command succeeded
- `result`: The result of the command (typed according to the command)
- `error`: Error message if the command failed

```cpp
auto reply = redis.get("nonexistent");
if (!reply.ok()) {
    std::cerr << "Error: " << reply.error << std::endl;
} else if (!reply.result()) {
    std::cout << "Key does not exist" << std::endl;
} else {
    std::cout << "Value: " << *reply.result() << std::endl;
}
```

## Thread Safety

The Redis module is not thread-safe. Each thread should use its own connection to the Redis server.

## License

Apache License, Version 2.0


