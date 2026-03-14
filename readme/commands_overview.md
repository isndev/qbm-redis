# `qbm-redis`: Command Execution & Replies

This document explains the general mechanism for executing Redis commands and handling their replies using the `qbm-redis` module.

## Command Execution

Commands are executed by calling methods on a `qb::redis::tcp::client` (or `tcp::ssl::client`) instance. The methods are organized into traits representing Redis command groups (e.g., `string_commands`, `hash_commands`), and the `client` class inherits from all of them.

### Coroutine vs. Callback

`qbm-redis` provides two ways to execute commands. All commands use the **same method name**; the overload is determined by whether you pass a callback:

1.  **Coroutine (recommended):**
    *   Call the method without a callback: `redis.set("mykey", "myvalue")` returns a `redis_awaiter<T>`.
    *   Use `co_await` to suspend until the reply is received: `auto r = co_await redis.set("mykey", "myvalue");`
    *   Returns `qb::redis::Reply<T>` directly.
    *   **Suitable for:** Async code, QB actors, non-blocking I/O. The coroutine suspends without blocking the event loop.

2.  **Callback:**
    *   Pass a callback as the **first** argument: `redis.set([](Reply<status>&& r){ ... }, "mykey", "myvalue");`
    *   The method returns immediately; the callback is invoked when the reply arrives.
    *   The callback receives `qb::redis::Reply<T>&&` (rvalue reference).
    *   **Suitable for:** Legacy code or when coroutines are not available.

**Note:** There is no `_async` suffix. The callback overload provides the non-blocking variant.

### Command Arguments

*   Keys and values are typically passed as `const std::string&` or `std::string_view`.
*   Some commands accept multiple arguments (e.g., `MSET`, `SADD`, `LPUSH`). These often use variadic templates or `std::vector` / `std::initializer_list`.
*   Options (e.g., for `SET`, `ZADD`) are often passed using enums or optional arguments.

```cpp
// Coroutine (inside a task/coroutine)
auto reply_set = co_await redis.set("mykey", "myvalue");

// Callback
redis.set([](qb::redis::Reply<qb::redis::status>&& reply){
    if (reply.ok()) { /* success */ }
}, "mykey", "myvalue");

// Coroutine with options
auto reply_setex = co_await redis.setex("tempkey", 60, "expire soon");

// Blocking run (e.g. in tests): run_sync runs the event loop until the coroutine completes
bool ok = qb::io::async::run_sync(redis.connect());
auto r = qb::io::async::run_sync(redis.get("key"));
```

## Handling Replies: `qb::redis::Reply<T>`

(`qbm/redis/reply.h`)

All command execution methods (sync and async callbacks) return or provide a `qb::redis::Reply<T>` object. This is a wrapper designed for type safety and robust error handling.

*   **Template Parameter `T`:** Represents the *expected successful result type* for the command (e.g., `std::optional<std::string>` for `GET`, `long long` for `INCR`, `std::vector<std::string>` for `LRANGE`, `qb::redis::status` for commands returning simple "OK").
*   **Checking for Success:** Use `if (reply)` or `reply.ok()`.
   *   `true`: The command was sent, a reply was received, and the reply did *not* represent a Redis error (e.g., WRONGTYPE).
   *   `false`: An error occurred.
*   **Accessing the Value:** If `ok()` is true, access the result using `reply.result()` or `reply.value()` (aliases). The type matches the template parameter `T`.
*   **Optional Results:** For `Reply<std::optional<T>>`, use `reply.value_or(default)` — returns the value if present, else `default`. No need for `.has_value()` or `*reply.result()`.
*   **Accessing the Error:** If `ok()` is false, access the error details using `reply.error()`. This returns a `qb::redis::error` object.
    *   `error().what()`: Provides a descriptive error message.
    *   `error().type()`: Returns an enum indicating the error type (e.g., `ReplyErrorType::ERR` for Redis errors).

```cpp
// --- Coroutine Reply Handling ---
auto get_reply = co_await redis.get("some_key");

if (auto val = get_reply.value_or(""); !val.empty()) {
    qb::io::cout() << "GET succeeded: " << val << std::endl;
} else if (get_reply) {
    qb::io::cout() << "GET succeeded: key does not exist (nil)" << std::endl;
} else {
    qb::io::cout() << "GET failed: " << get_reply.error() << std::endl;
}

// --- Callback Reply Handling ---
redis.incr([](qb::redis::Reply<long long>&& incr_reply) {
    if (incr_reply.ok()) {
        long long new_value = incr_reply.result();
        qb::io::cout() << "INCR succeeded: " << new_value << std::endl;
    } else {
        qb::io::cout() << "INCR failed: " << incr_reply.error().what() << std::endl;
    }
}, "counter");
```

### Special Reply Types

*   **`qb::redis::status`:** A specialized type for commands returning "OK". `reply.ok()` checks success. `reply.result()` returns the status string.
*   **Container Types:** Commands returning multiple values (e.g., `LRANGE`, `HGETALL`, `SMEMBERS`) use `Reply<std::vector<...>>` or `Reply<qb::unordered_map<...>>`.
*   **Nil/Optional:** Commands that can return `nil` use `Reply<std::optional<...>>`.

Refer to the specific command documentation (e.g., `string_commands.md`) for the exact `Reply<T>` type expected for each command. 