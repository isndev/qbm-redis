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

`qbm-redis` reports errors via `qb::redis::Reply<T>`. Commands **do not throw** for Redis command errors; they return a `Reply` with `ok() == false`.

**1. Coroutine (co_await):**

*   **Mechanism:** The command returns `Reply<T>`. Check `reply.ok()` before using `reply.result()`.
*   **Example:**
    ```cpp
    auto reply = co_await redis.incr("mykey");
    if (!reply.ok()) {
        std::cerr << "INCR failed: " << reply.error().what() << std::endl;
    } else {
        long long val = reply.result();
    }
    ```

**2. Callback:**

*   **Mechanism:** Errors are reported via the `Reply<T>` passed to the callback.
    *   **`reply.ok()`:** Returns `false` if a Redis command error or protocol/parsing error occurred.
    *   **`reply.error()`:** If `!reply.ok()`, returns `const qb::redis::error&` with `what()` for the message.
*   **Example:**
    ```cpp
    redis.incr([](qb::redis::Reply<long long>&& reply) {
        if (!reply.ok()) {
            std::cerr << "INCR failed: " << reply.error().what() << std::endl;
        } else {
            std::cout << "INCR succeeded: " << reply.result() << std::endl;
        }
    }, "mykey");
    ```
*   **Connection Errors:** If the connection drops, pending callbacks may be invoked with `ok() == false`.
*   **Callback Exceptions:** Exceptions thrown inside your callback are not caught by the library.

## Error Types (`qb::redis::Error`)

*(Defined in `qbm/redis/reply.h`)*

*   **`qb::redis::Error`:** Base class (inherits `std::exception`).
*   **`qb::redis::ProtoError`:** Indicates an issue with the Redis protocol itself.
*   **`qb::redis::ConnectionError`:** Thrown when establishing a connection fails.
*   **`qb::redis::AuthError`:** Thrown when Redis rejects authentication.
*   **`qb::redis::CommandError`:** Represents Redis server errors (e.g., WRONGTYPE).
*   **`qb::redis::ReplyParseError`:** Failure to parse a reply into the expected C++ type.

## Best Practices

*   **Prefer Coroutines:** Use `co_await redis.cmd(...)` for non-blocking async code; it suspends without blocking the event loop.
*   **Check `reply.ok()`:** Always check `reply.ok()` before using `reply.result()`.
*   **Handle Errors Gracefully:** Handle expected Redis errors (e.g., `WRONGTYPE`, `NOAUTH`) in your logic.
*   **Check `connect()` Return:** `co_await redis.connect()` returns `bool`; check it for success.

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
// --- Coroutine Error Handling ---
qb::redis::tcp::client redis{qb::io::uri{"tcp://127.0.0.1:6379"}};
bool connected = co_await redis.connect();
if (!connected) {
    // Connection failed
    return;
}

co_await redis.set("mykey", "not_a_number");
auto incr_reply = co_await redis.incr("mykey");

if (incr_reply.ok()) {
    qb::io::cout() << "INCR succeeded: " << incr_reply.result() << std::endl;
} else {
    const qb::redis::error& err = incr_reply.error();
    qb::io::cout() << "INCR failed: " << err.what() << std::endl;
}

// --- Callback Error Handling ---
redis.get([](qb::redis::Reply<std::optional<std::string>>&& reply) {
    if (reply.ok()) {
        if (reply.result().has_value()) {
            // Key existed
        } else {
            // Key didn't exist (nil reply)
            qb::io::cout() << "GET: Key does not exist (nil)." << std::endl;
        }
    } else {
        qb::io::cout() << "GET failed: " << reply.error().what() << std::endl;
    }
}, "non_existent_key");

redis.incr([](qb::redis::Reply<long long>&& reply) {
    if (!reply.ok()) {
        qb::io::cout() << "INCR failed: " << reply.error().what() << std::endl;
    }
}, "string_key");
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