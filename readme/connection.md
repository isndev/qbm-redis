# `qbm-redis`: Connection & Authentication

This document describes how to establish and manage connections to a Redis server using the `qbm-redis` client.

## Connecting to Redis

The `qb::redis::tcp::client` (or `tcp::ssl::client`) handles the connection.

### Connection URI

The primary way to specify connection details is via a URI string passed to the client constructor or `connect`/`connect_async` methods. Supported schemes:

*   `tcp://<host>:<port>` (e.g., `tcp://127.0.0.1:6379`, `tcp://myredis.example.com:6379`)
*   `redis://<host>:<port>` (Alias for `tcp://`)
*   `unix://<path_to_socket>` (e.g., `unix:///var/run/redis/redis.sock`)
*   `rediss://<host>:<port>` (For SSL/TLS connections, requires `tcp::ssl::client`)

### Constructor

Connections are typically established **synchronously** in the client's constructor.

```cpp
#include <qbm/redis/redis.h>
#include <qb/io.h>

try {
    // Connect via TCP
    qb::redis::tcp::client redis_tcp("tcp://127.0.0.1:6379");
    qb::io::cout() << "TCP connection successful." << std::endl;

    // Connect via Unix socket
    // qb::redis::tcp::client redis_unix("unix:///tmp/redis.sock");
    // qb::io::cout() << "Unix socket connection successful." << std::endl;

#ifdef QB_IO_WITH_SSL
    // Connect via SSL/TLS (ensure Redis server is configured for SSL)
    // Assumes appropriate SSL context setup if needed beyond default.
    qb::redis::tcp::ssl::client redis_ssl("rediss://your-secure-redis-host:6380");
    qb::io::cout() << "SSL connection successful." << std::endl;
#endif

} catch (const qb::redis::connection_error& e) {
    qb::io::cout() << "Connection failed: " << e.what() << std::endl;
} catch (const qb::redis::error& e) {
    qb::io::cout() << "Redis error during connect: " << e.what() << std::endl;
}
```

If the constructor fails to connect, it throws `qb::redis::connection_error`.

### Asynchronous Connection

For use within QB actors or other asynchronous code, use `connect_async`.

```cpp
#include <qbm/redis/redis.h>
#include <qb/actor.h>

class MyActor : public qb::Actor {
    qb::redis::tcp::client _redis;
    bool _connected = false;

public:
    MyActor() : _redis() {} // Construct without connecting

    bool onInit() override {
        qb::io::cout() << "Initiating async connect..." << std::endl;
        _redis.connect_async("tcp://127.0.0.1:6379", [this](bool success){
            if (success) {
                _connected = true;
                qb::io::cout() << "Async connect successful!" << std::endl;
                // Now safe to send commands
                send_ping_async();
            } else {
                 qb::io::cout() << "Async connect failed!" << std::endl;
                 // Handle error, maybe schedule retry or kill actor
                 kill();
            }
        });
        registerEvent<qb::KillEvent>(*this);
        return true;
    }

    void send_ping_async() {
        if (!_connected) return;
        _redis.ping_async([](qb::redis::status&& reply){
            qb::io::cout() << "Async PING result: " << (reply ? reply.value() : reply.error().what()) << std::endl;
        });
    }
    // ... other methods / KillEvent handler ...
};
```

## Connection Commands

These commands manage the connection state or test the connection.

*   **`PING [message]`**
    *   **Sync:** `qb::redis::status redis.ping()` or `qb::redis::Reply<std::optional<std::string>> redis.ping(message)`
    *   **Async:** `redis.ping_async(callback)` or `redis.ping_async(message, callback)`
    *   **Description:** Checks connection health. Returns `PONG` or the `message` if provided.
    *   **Reply:** `qb::redis::status` (for no message) or `Reply<std::optional<std::string>>` (with message).
*   **`ECHO message`**
    *   **Sync:** `qb::redis::Reply<std::optional<std::string>> redis.echo(message)`
    *   **Async:** `redis.echo_async(message, callback)`
    *   **Description:** Returns the provided `message` string.
    *   **Reply:** `Reply<std::optional<std::string>>`.
*   **`SELECT index`**
    *   **Sync:** `qb::redis::status redis.select(db_index)`
    *   **Async:** `redis.select_async(db_index, callback)`
    *   **Description:** Changes the currently selected Redis database for the *current connection*.
    *   **Reply:** `qb::redis::status`.
*   **`SWAPDB index1 index2`**
    *   **Sync:** `qb::redis::status redis.swapdb(index1, index2)`
    *   **Async:** `redis.swapdb_async(index1, index2, callback)`
    *   **Description:** Swaps two Redis databases.
    *   **Reply:** `qb::redis::status`.
*   **`AUTH password` / `AUTH username password`**
    *   **Sync:** `qb::redis::status redis.auth(password)` or `redis.auth(user, password)`
    *   **Async:** `redis.auth_async(password, callback)` or `redis.auth_async(user, password, callback)`
    *   **Description:** Authenticates the connection to the Redis server if it requires a password (ACLs).
    *   **Reply:** `qb::redis::status`.
    *   **Note:** Authentication usually happens implicitly during connection if credentials are provided in the URI (e.g., `redis://user:password@host:port`), but can be called explicitly.
*   **`QUIT`**
    *   **Sync:** `qb::redis::status redis.quit()`
    *   **Async:** `redis.quit_async(callback)`
    *   **Description:** Requests the server to close the connection. The client should typically close the connection from its side afterwards or handle the `disconnected` event.
    *   **Reply:** `qb::redis::status` (usually returns "OK" before the server closes).

## Disconnection

*   **Implicit:** Connections are closed when the `qb::redis::tcp::client` object is destroyed (RAII).
*   **Explicit (Async):** Call `redis.disconnect_async(callback)`. This signals the underlying transport to close. The callback is invoked *after* the disconnection attempt completes. This is the preferred way to close connections cleanly within actors before they terminate.
*   **Handling:** Network errors or server-initiated closures will trigger the `on(qb::io::async::event::disconnected&&)` handler if the client is integrated into an asynchronous component (like an actor inheriting from `qb::io::use<...>::tcp::client`). Implement this handler for cleanup or reconnection logic.

```cpp
// Inside an actor inheriting from qb::io::use<MyActor>::tcp::client<>
void MyActor::on(const qb::io::async::event::disconnected& event) {
    qb::io::cout() << "Disconnected from Redis. Reason code: " << event.reason << std::endl;
    _connected = false;
    // Optional: Schedule a reconnection attempt
    // schedule_reconnect();
}

void MyActor::on(const qb::KillEvent&) {
    if (_connected) {
        _redis.disconnect_async([this](){
            // Disconnect finished, now safe to kill actor fully
            kill();
        });
    } else {
        kill();
    }
}
``` 