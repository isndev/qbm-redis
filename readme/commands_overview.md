# `qbm-redis`: Command Execution & Replies

This document explains the general mechanism for executing Redis commands and handling their replies using the `qbm-redis` module.

## Command Execution

Commands are executed by calling methods on a `qb::redis::tcp::client` (or `tcp::ssl::client`) instance. The methods are organized into traits representing Redis command groups (e.g., `string_commands`, `hash_commands`), and the `client` class inherits from all of them.

### Synchronous vs. Asynchronous

`qbm-redis` provides two primary ways to execute commands:

1.  **Synchronous:**
    *   Methods have standard names (e.g., `redis.set(...)`, `redis.get(...)`).
    *   These methods **block** the calling thread until the command completes and the reply is received from the Redis server.
    *   They directly return a `qb::redis::Reply<T>` object.
    *   **Suitable for:** Simple scripts, command-line tools, or situations outside the QB actor model where blocking is acceptable.
    *   **Not suitable for:** Use directly within QB actor event handlers (`on(...)`) or callbacks (`onCallback()`), as this will block the `VirtualCore` thread.

2.  **Asynchronous:**
    *   Methods are suffixed with `_async` (e.g., `redis.set_async(...)`, `redis.get_async(...)`).
    *   These methods **do not block**. They send the command request and return immediately.
    *   They require a **callback function** (lambda, function pointer, `std::function`) as the last argument.
    *   The callback is invoked later (on the QB event loop of the actor/thread that made the call) when the reply is received.
    *   The callback receives the `qb::redis::Reply<T>&&` as an argument (rvalue reference).
    *   **Suitable for:** Use within QB actors and other asynchronous contexts to maintain responsiveness.

### Command Arguments

*   Keys and values are typically passed as `const std::string&` or `std::string_view`.
*   Some commands accept multiple arguments (e.g., `MSET`, `SADD`, `LPUSH`). These often use variadic templates or `std::vector` / `std::initializer_list`.
*   Options (e.g., for `SET`, `ZADD`) are often passed using enums or optional arguments.

```cpp
// Synchronous
qb::redis::status reply_set = redis.set("mykey", "myvalue");

// Asynchronous
redis.set_async("mykey", "myvalue", [](qb::redis::status&& reply){
    if (!reply) {
        // Handle error
    }
});

// Synchronous with options
qb::redis::status reply_setex = redis.setex("tempkey", 60, "expire soon"); // Expires in 60s

// Asynchronous with options
redis.setex_async("tempkey", 60, "expire soon", [](qb::redis::status&& reply){
    // ...
});
```

## Handling Replies: `qb::redis::Reply<T>`

(`qbm/redis/reply.h`)

All command execution methods (sync and async callbacks) return or provide a `qb::redis::Reply<T>` object. This is a wrapper designed for type safety and robust error handling.

*   **Template Parameter `T`:** Represents the *expected successful result type* for the command (e.g., `std::optional<std::string>` for `GET`, `long long` for `INCR`, `std::vector<std::string>` for `LRANGE`, `qb::redis::status` for commands returning simple "OK").
*   **Checking for Success:** Use the boolean operator `if (reply)` or `reply.ok()`.
    *   `true`: The command was sent, a reply was received, and the reply did *not* represent a Redis error (e.g., WRONGTYPE).
    *   `false`: An error occurred.
*   **Accessing the Value:** If `ok()` is true, access the result using `reply.value()`. The type of `value()` matches the template parameter `T`.
    *   **Important:** For commands that might return `nil` (e.g., `GET` on a non-existent key), the type `T` is often `std::optional<...>`. You need to check `reply.value().has_value()` before accessing `reply.value().value()`.
*   **Accessing the Error:** If `ok()` is false, access the error details using `reply.error()`. This returns a `qb::redis::error` object (or a derived type like `connection_error`, `proto_error`).
    *   `error().what()`: Provides a descriptive error message.
    *   `error().type()`: Returns an enum indicating the error type (e.g., `ReplyErrorType::ERR` for Redis errors, `ReplyErrorType::IO` for connection issues).

```cpp
// --- Synchronous Reply Handling ---
qb::redis::Reply<std::optional<std::string>> get_reply = redis.get("some_key");

if (get_reply) { // or get_reply.ok()
    if (get_reply.value().has_value()) {
        std::string val = get_reply.value().value();
        qb::io::cout() << "GET succeeded: " << val << std::endl;
    } else {
        qb::io::cout() << "GET succeeded: key does not exist (nil)" << std::endl;
    }
} else {
    // Error occurred
    const qb::redis::error& err = get_reply.error();
    qb::io::cout() << "GET failed: " << err.what() << " (Type: " << (int)err.type() << ")
";
}

// --- Asynchronous Reply Handling (inside callback) ---
redis.incr_async("counter", [](qb::redis::Reply<long long>&& incr_reply) {
    if (incr_reply) {
        long long new_value = incr_reply.value();
        qb::io::cout() << "INCR succeeded: " << new_value << std::endl;
    } else {
        qb::io::cout() << "INCR failed: " << incr_reply.error().what() << std::endl;
    }
});
```

### Special Reply Types

*   **`qb::redis::status`:** A specialized `Reply<std::string>` used for commands that just return "OK" on success. `reply.ok()` checks if the string is "OK". `reply.value()` returns the actual status string (usually "OK").
*   **Container Types:** Commands returning multiple values (e.g., `LRANGE`, `HGETALL`, `SMEMBERS`) use `Reply<std::vector<...>>` or `Reply<qb::unordered_map<...>>`.
*   **Nil/Optional:** Commands that can return `nil` use `Reply<std::optional<...>>`.

Refer to the specific command documentation (e.g., `string_commands.md`) for the exact `Reply<T>` type expected for each command. 