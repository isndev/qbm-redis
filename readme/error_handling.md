# `qbm-redis`: Error Handling

This document describes how errors are reported and handled in the `qbm-redis` module.

## Sources of Errors

Errors can originate from several places:

1.  **Connection Errors:** Issues establishing or maintaining the TCP/SSL connection to the Redis server (e.g., host unreachable, connection refused, SSL handshake failure). These are typically handled by the underlying `qb-io` transport and result in disconnection events.
2.  **Protocol Errors:** Problems parsing the Redis reply stream (RESP - REdis Serialization Protocol). This might happen due to corrupted data or unexpected replies from the server. These usually result in a `qb::redis::ProtoError`.
3.  **Redis Command Errors:** Errors returned explicitly by the Redis server for a specific command (e.g., `WRONGTYPE`, `NOAUTH`, `ERR syntax error`). These are indicated by a reply type of `REDIS_REPLY_ERROR`.
4.  **Parsing/Conversion Errors:** Errors during the conversion of a valid Redis reply to the expected C++ type (e.g., trying to parse "hello" as an integer). These usually result in a `qb::redis::ParseError`.
5.  **Client-Side Errors:** Exceptions thrown from within user-provided asynchronous callbacks.

## Handling Mechanisms

`qbm-redis` uses different mechanisms depending on whether you use the synchronous or asynchronous API.

**1. Synchronous Commands:**

*   **Mechanism:** If a Redis command error occurs (`REDIS_REPLY_ERROR`) or if a protocol/parsing error happens during reply processing, the synchronous command method throws a `std::runtime_error`. The `what()` message of this exception contains the error description from Redis or the parsing error details.
*   **Example:**
    ```cpp
    try {
        // Attempting to increment a non-numeric string
        redis.set("mykey", "not a number");
        redis.incr("mykey"); // This will throw
    } catch (const std::runtime_error& e) {
        std::cerr << "Synchronous command failed: " << e.what() << std::endl;
        // e.what() will likely contain something like:
        // "ERR value is not an integer or out of range"
    }
    ```
*   **Connection Errors:** Connection failures during the initial synchronous `connect()` call might also throw or return `false` depending on the exact point of failure (DNS vs. TCP connect vs. auth).

**2. Asynchronous Commands:**

*   **Mechanism:** Errors are reported via the `qb::redis::Reply<T>` object passed to the callback function.
    *   **`reply.ok()`:** Returns `false` if a Redis command error (`REDIS_REPLY_ERROR`) occurred or if there was a protocol/parsing error during reply processing.
    *   **`reply.error()`:** If `!reply.ok()`, this `std::string_view` contains the error message from Redis or the description of the parsing error.
*   **Example:**
    ```cpp
    redis.incr([&](qb::redis::Reply<long long>&& reply) {
        if (!reply.ok()) {
            std::cerr << "Async INCR failed: " << reply.error() << std::endl;
            // Handle the error - e.g., log it, notify another actor
        } else {
            std::cout << "Async INCR succeeded: " << reply.result() << std::endl;
        }
    }, "mykey_that_holds_a_string");
    ```
*   **Connection Errors:** If the underlying connection drops, the `on(qb::io::async::event::disconnected &&)` handler in the `connector` base class is triggered. It attempts to complete any pending callbacks with an error state (`ok = false`, `error = "Connection lost"` or similar).
*   **Callback Exceptions:** Exceptions thrown *inside* your callback function are **not** caught by the `qbm-redis` library itself. They will propagate according to standard C++ exception handling rules. In a QB actor context, an uncaught exception in a callback might terminate the actor or its `VirtualCore`.

## Error Types (`qb::redis::Error`)

*(Defined in `qbm/redis/reply.h`)*

The module defines a hierarchy, although primarily `std::runtime_error` is used for synchronous calls, and `reply.error()` provides the message for async calls.

*   **`qb::redis::Error`:** Base class (inherits `std::exception`).
*   **`qb::redis::ProtoError`:** Indicates an issue with the Redis protocol itself (e.g., unexpected reply format).
*   **`qb::redis::ParseError`:** Indicates a failure to parse a valid Redis reply into the expected C++ type.

## Best Practices

*   **Prefer Asynchronous API:** Especially within QB actors, use the asynchronous command variants with callbacks to avoid blocking.
*   **Check `reply.ok()`:** Always check `reply.ok()` within asynchronous callbacks before attempting to use `reply.result()`.
*   **Handle Errors Gracefully:** Implement logic in error callbacks or `catch` blocks to handle expected Redis errors (e.g., `WRONGTYPE`, `NOAUTH`, connection issues) appropriately for your application.
*   **Check `connect()` Return:** For synchronous `connect()`, check the boolean return value for immediate failures.
*   **Use `.await()` Judiciously:** Only use `.await()` in contexts where blocking is acceptable (tests, simple scripts).

## Error Types

Errors are generally represented by exceptions derived from `qb::redis::error`. Key types include:

*   **`qb::redis::error` (Base Class):** General Redis-related error.
*   **`qb::redis::connection_error`:** Problems establishing or maintaining the TCP/SSL/Unix socket connection (e.g., host not found, connection refused, network unreachable). These are often thrown synchronously during connection attempts (e.g., in the client constructor) or reported asynchronously via the `on(qb::io::async::event::disconnected&&)` handler.
*   **`qb::redis::proto_error`:** Indicates a problem parsing the Redis protocol response from the server. This usually suggests a bug in the client library or unexpected data from the server.
*   **`qb::redis::command_error`:** Represents errors returned *by the Redis server* for a specific command (e.g., `WRONGTYPE`, `ERR syntax error`).

## Handling Errors via `qb::redis::Reply<T>`

The `Reply<T>` object is the primary way to check for command errors.

1.  **Check `ok()`:** Always check `if (reply)` or `if (reply.ok())` first.
    *   If `true`, the command was successfully sent, a reply was received, and the reply *was not* a Redis error (like `WRONGTYPE`). Proceed to check `reply.value()`.
    *   If `false`, an error occurred. Proceed to check `reply.error()`.

2.  **Check `error()`:** If `!reply.ok()`, access the error details:
    *   `reply.error()`: Returns a `const qb::redis::error&`.
    *   `reply.error().what()`: Gets the error message string.
    *   `reply.error().type()`: Gets the `qb::redis::ReplyErrorType` enum (`ERR`, `MOVED`, `ASK`) indicating the category of Redis error (if it was a server error). `MOVED` and `ASK` are relevant for Redis Cluster, which `qbm-redis` doesn't explicitly handle automatically – the application would need to parse the error message for redirection info.

```cpp
// --- Synchronous Error Handling Example ---
try {
    qb::redis::tcp::client redis("tcp://127.0.0.1:6379");

    // Example: Trying to increment a non-integer key
    redis.set("mykey", "not_a_number");
    qb::redis::Reply<long long> incr_reply = redis.incr("mykey");

    if (incr_reply) {
        // This branch won't be taken if 'mykey' is not an integer
        qb::io::cout() << "INCR succeeded (unexpectedly): " << incr_reply.value() << std::endl;
    } else {
        // Handle the error
        const qb::redis::error& err = incr_reply.error();
        qb::io::cout() << "INCR failed: " << err.what() << std::endl;
        // Example: Check specific Redis error type
        if (err.type() == qb::redis::ReplyErrorType::ERR && std::string(err.what()).find("value is not an integer") != std::string::npos) {
            qb::io::cout() << "(Detected WRONGTYPE error as expected)" << std::endl;
        }
    }

} catch (const qb::redis::connection_error& e) {
    // Catch errors during initial connection
    qb::io::cout() << "Connection Error: " << e.what() << std::endl;
} catch (const qb::redis::error& e) {
    // Catch other potential errors during command execution (less common for sync)
    qb::io::cout() << "Redis Error: " << e.what() << std::endl;
}


// --- Asynchronous Error Handling Example (inside callback) ---
redis.get_async("non_existent_key", [](qb::redis::Reply<std::optional<std::string>>&& reply) {
    if (reply) {
        if (reply.value().has_value()) {
            // Key existed - unexpected in this specific case
        } else {
            // Key didn't exist (nil reply) - this is considered success by ok()
            qb::io::cout() << "Async GET: Key does not exist (nil reply)." << std::endl;
        }
    } else {
        // An actual error occurred (e.g., connection dropped before reply, protocol error)
        qb::io::cout() << "Async GET failed: " << reply.error().what() << std::endl;
    }
});

redis.incr_async("string_key", [](qb::redis::Reply<long long>&& reply){
    if (!reply) {
         qb::io::cout() << "Async INCR failed as expected: " << reply.error().what() << std::endl;
    }
});
```

## Connection Errors

*   **Synchronous:** Thrown as `qb::redis::connection_error` during client construction or synchronous command execution if the connection drops mid-command.
*   **Asynchronous:** Reported via the `on(qb::io::async::event::disconnected&&)` handler if the client is integrated into an async component (e.g., actor). The `reason` field might provide some system-level error code. Subsequent async command callbacks might fire with `ok() == false` and an error indicating connection loss.
*   **Recovery:** Implement reconnection logic, potentially with backoff delays, usually triggered by the `disconnected` event or failed connection attempts.

## Protocol Errors

*   These are generally indicative of bugs or unexpected server behavior.
*   Reported via `Reply<T>` with `ok() == false` and an appropriate error message, often mentioning parsing issues. The specific error type might be `qb::redis::proto_error`.
*   Recovery typically involves logging the error and potentially disconnecting/reconnecting.

## Redis Command Errors (`ERR`)

*   Reported by Redis itself (e.g., `WRONGTYPE`, `NOAUTH`, syntax errors in scripts).
*   Handled via `Reply<T>` where `ok() == false` and `error().type() == qb::redis::ReplyErrorType::ERR`.
*   Application logic needs to interpret `error().what()` to determine the specific cause and react accordingly (e.g., correct the command, report to user). 