# `qbm-redis`: Connection & Authentication

This document describes how to establish and manage connections to a Redis server using the `qbm-redis` client.

## Connecting to Redis

The `qb::redis::tcp::client` (or `tcp::ssl::client`) handles the connection. The client is **constructed with a URI** but does **not** connect automatically. You must call `connect()` or `co_await client.connect()`.

### Connection URI

Supported schemes:

*   `tcp://<host>:<port>` (e.g., `tcp://127.0.0.1:6379`, `tcp://myredis.example.com:6379`)
*   `redis://<host>:<port>` (Alias for `tcp://`)
*   `unix://<path_to_socket>` (e.g., `unix:///var/run/redis/redis.sock`)
*   `rediss://<host>:<port>` (For SSL/TLS connections, requires `tcp::ssl::client`)

### Coroutine Connection

```cpp
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include "../redis.h"

qb::redis::tcp::client redis{qb::io::uri{"tcp://127.0.0.1:6379"}};

// Connect (coroutine)
bool connected = co_await redis.connect();
if (connected) {
    auto r = co_await redis.ping();
    // ...
}
```

### Blocking Connection (e.g. tests)

```cpp
#include <qb/io/async.h>
#include "../redis.h"

qb::redis::tcp::client redis{qb::io::uri{"tcp://127.0.0.1:6379"}};

qb::io::async::init();
bool connected = qb::io::async::run_sync(redis.connect());
if (!connected) {
    throw std::runtime_error("Connection failed");
}
```

### Callback Connection

```cpp
#include <qb/io/async.h>
#include "../redis.h"

qb::redis::tcp::client redis{qb::io::uri{"tcp://127.0.0.1:6379"}};

qb::io::async::init();
redis.connect([](bool success) {
    if (success) {
        redis.ping([](auto&& reply) {
            if (reply.ok()) { /* connected */ }
        });
    }
});
```

### RESP2 / RESP3 Protocol

By default, Redis starts in RESP2. To use RESP3:

```cpp
auto hello = co_await redis.hello(3);  // Switch to RESP3
if (hello.ok()) {
    // Server info map returned in RESP3
}
```

**Note:** After a reconnection, the connection is fresh and Redis defaults to RESP2. Call `hello(3)` again after reconnect if you need RESP3.

## Connection Commands

*   **`PING [message]`**
    *   **Coroutine:** `Reply<std::string> r = co_await redis.ping()` or `co_await redis.ping(message)`
    *   **Callback:** `redis.ping(callback)` or `redis.ping(callback, message)`
    *   Returns `PONG` or the `message` if provided.
*   **`ECHO message`**
    *   **Coroutine:** `Reply<std::string> r = co_await redis.echo(message)`
    *   **Callback:** `redis.echo(callback, message)`
*   **`SELECT index`**
    *   **Coroutine:** `Reply<status> r = co_await redis.select(db_index)`
    *   **Callback:** `redis.select(callback, db_index)`
*   **`SWAPDB index1 index2`**
    *   **Coroutine:** `Reply<status> r = co_await redis.swapdb(index1, index2)`
    *   **Callback:** `redis.swapdb(callback, index1, index2)`
*   **`AUTH password` / `AUTH username password`**
    *   **Coroutine:** `Reply<status> r = co_await redis.auth(password)` or `co_await redis.auth(user, password)`
    *   **Callback:** `redis.auth(callback, password)` or `redis.auth(callback, user, password)`
*   **`HELLO version`**
    *   **Coroutine:** `Reply<qb::json> r = co_await redis.hello(3)` (RESP3 handshake)
    *   **Callback:** `redis.hello(callback, 3)`
*   **`RESET`**
    *   **Coroutine:** `Reply<status> r = co_await redis.reset()`
    *   **Callback:** `redis.reset(callback)`
    *   Resets the connection to a clean state; connection stays open.
*   **`QUIT`**
    *   **Coroutine:** `Reply<status> r = co_await redis.quit()`
    *   **Callback:** `redis.quit(callback)`

## Disconnection

*   **Explicit:** Call `redis.disconnect()`. No callback; the connection is closed immediately.
*   **Implicit:** Connections are closed when the client object is destroyed (RAII).
*   **Auto-reconnect:** Use `redis.enable_auto_reconnect(RetryPolicy{})` to automatically reconnect on disconnect. After reconnect, call `hello(3)` again if using RESP3.

## Auto-Reconnect

```cpp
redis.enable_auto_reconnect(RetryPolicy{}
    .with_initial_delay(50ms)
    .with_max_delay(2s)
    .with_connect_timeout(2.0)
    .with_jitter(true));

redis.disconnect();  // Triggers reconnect in background
// redis.is_reconnecting() == true until reconnect completes
```
