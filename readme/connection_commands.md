# Connection commands reference

> **Audience:** Adopter · **Status:** stable · **Verified-against:** qbm-redis @ qb 3.0.0 (C++20 default, C++23
> supported)

Reference for the connection command group — `HELLO`, `AUTH`, `ECHO`, `PING`, `QUIT`, `SELECT`, `SWAPDB`, and `RESET` —
exposed by every connected `qb::redis` client through the `connection_commands<Derived>` CRTP mixin.

**Prerequisites:** [connection.md](./connection.md) (open a connection, `qb_load_modules`,
`qbm::redis`), [commands_overview.md](./commands_overview.md) (the coroutine/callback dual API and `Reply<T>`) — **See
also:** [error_handling.md](./error_handling.md), [subscription_commands.md](./subscription_commands.md)

---

## Summary

These are the commands that operate on the *connection* rather than on keys: protocol negotiation (`HELLO`),
authentication (`AUTH`), liveness (`PING`, `ECHO`), database selection (`SELECT`, `SWAPDB`), and connection-state
control (`RESET`, `QUIT`). They are defined in `connection_commands.h` as a CRTP mixin that
`qb::redis::detail::Redis<QB_IO_>` (and therefore `qb::redis::tcp::client`) inherits, so a connected client calls them
as ordinary member functions.

Every command ships in two forms:

- a **coroutine** overload that returns a `redis_awaiter` you `co_await` (the idiomatic form), and
- a **callback** overload, selected when the first argument is invocable with the matching `Reply<T> &&`, that returns
  `Derived &` for chaining.

```cpp
#include <qbm/redis/redis.h>           // namespace qb::redis
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>

qb::io::async::task<void> example(qb::redis::tcp::client &redis) {
    auto pong = co_await redis.ping();         // Reply<std::string>
    if (pong)                                  // Reply<T> is contextually bool
        qb::io::cout() << pong.result() << std::endl;  // "PONG"
}
```

<!-- src: qbm/redis/tests/integration/connection/connection-commands.cpp:106-117 -->

This command group carries **no time arguments** — none of its parameters are durations, so the seconds-vs-milliseconds
boundary documented for key-expiry commands does not apply here. Connect and command *deadlines* (which are
`qb::duration`) live on the client itself; see [connection.md](./connection.md).

> The client is **not thread-safe.** Issue connection commands from a single I/O thread / strand, like every other
> command.

---

## Concepts

### Where these methods come from

`connection_commands<Derived>` is a header-only CRTP mixin (`connection_commands.h:33`). It is one of the bases of
`qb::redis::detail::Redis<QB_IO_>`, so `qb::redis::tcp::client`, `qb::redis::tcp::ssl::client`, and the pub/sub
consumers all expose these eight methods directly. Each method forwards to the inherited command machinery:

- the coroutine overload calls `derived().make_coro_command<T>(...)`, returning a `redis_awaiter<T>` that resolves to
  `Reply<T>`;
- the callback overload calls `derived().command<T>(func, "CMD", args...)` and returns `Derived &`.

The reply type `T` differs per command (`status`, `std::string`, `qb::json`); the table below lists each.

### Reading a `Reply<T>`

Every command resolves to `qb::redis::Reply<T>`:

- `reply.ok()` / `explicit operator bool` — `true` when the command succeeded.
- `reply.result()` (or its alias `reply.value()`) — the typed payload `T`.
- `reply.error()` — the server or transport error string when `!ok()`.

A rejected command (for example a bad `AUTH` password) resolves with `ok() == false` and the message in `error()`; it
does **not** throw and does **not** close the connection. See [error_handling.md](./error_handling.md) for the typed
error hierarchy.

`status` (the reply type of `AUTH`, `SELECT`, `SWAPDB`, `QUIT`, `RESET`) is a thin wrapper over the server's
simple-string reply: `status::ok()` and its `operator bool` are `true` only when the string is exactly `"OK"`. So with
`Reply<status>` there are two layers — `reply.ok()` (did the command round-trip) and `reply.result().ok()` (was the
status `"OK"`).

```cpp
auto r = co_await redis.select(1);
if (r.ok() && r.result().ok()) {
    // database switched
}
```

<!-- src: qbm/redis/tests/integration/connection/connection-commands.cpp:206-223 -->

---

## Command reference

The signatures below are copied from `connection_commands.h`. For each command the coroutine overload is listed first,
then the callback overload. `Func` is constrained with
`std::enable_if_t<std::is_invocable_v<Func, Reply<T> &&>, Derived &>`, so the callback overload is only viable when your
callable accepts the matching `Reply<T> &&`.

| Command  | Reply type           | Purpose                                                |
|----------|----------------------|--------------------------------------------------------|
| `hello`  | `Reply<qb::json>`    | Negotiate the RESP protocol version; fetch server info |
| `auth`   | `Reply<status>`      | Authenticate (legacy password, or ACL user + password) |
| `echo`   | `Reply<std::string>` | Echo a message back (round-trip check)                 |
| `ping`   | `Reply<std::string>` | Liveness probe; optional custom payload                |
| `quit`   | `Reply<status>`      | Ask the server to close the connection after replying  |
| `select` | `Reply<status>`      | Switch the active logical database                     |
| `swapdb` | `Reply<status>`      | Swap two logical databases                             |
| `reset`  | `Reply<status>`      | Reset the connection to a clean state (RESP3)          |

### `hello` — protocol negotiation

```cpp
// Coroutine — connection_commands.h:52
auto hello(int version = 3);                       // -> redis_awaiter yielding Reply<qb::json>

// Callback — connection_commands.h:67
template <typename Func>
std::enable_if_t<std::is_invocable_v<Func, Reply<qb::json> &&>, Derived &>
hello(Func &&func, int version = 3);
```

`HELLO` switches the connection's protocol version (`2` for RESP2, `3` for RESP3) and returns a server-info structure.
In RESP3 the reply is a `qb::json` **object** (map) carrying `server`, `version`, `proto`, `id`, and more; in RESP2 it
is a `qb::json` **array**. `version` defaults to `3`. Issue `hello(3)` as the first command after `connect()` if you
want RESP3 — the server stays on RESP2 until you ask.

```cpp
auto reply = co_await redis.hello(3);
if (reply.ok()) {
    const auto &info = reply.result();             // qb::json
    if (info.is_object() && info.contains("server"))
        qb::io::cout() << info["server"].get<std::string>() << std::endl;  // "redis"
}
```

<!-- src: qbm/redis/tests/integration/connection/connection-commands.cpp:147-163 -->

### `auth` — authentication

```cpp
// Legacy password-only (the "default" user) — connection_commands.h:79 / :94
auto auth(const std::string &password);            // -> Reply<status>
template <typename Func>
std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
auth(Func &&func, const std::string &password);

// ACL user + password (Redis 6+) — connection_commands.h:107 / :124
auto auth(const std::string &user, const std::string &password);   // -> Reply<status>
template <typename Func>
std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
auth(Func &&func, const std::string &user, const std::string &password);
```

Two argument shapes: one string is the legacy `requirepass` form (authenticates as the `default` user); two strings are
the ACL `user` + `password` form. A rejected credential resolves with `ok() == false` and the server message (
`WRONGPASS`, `NOPERM`) in `error()`. A `qb::redis::AuthError` class is declared (`reply.h:118`) but nothing in the
module ever constructs or throws it — a rejected `auth` reaches you only as `reply.error()` text, so do not write a
`catch` clause for it. Credentials are **not** stored
on the client, so auto-reconnect does not re-authenticate — re-issue `auth(...)` yourself after a reconnect (
see [connection.md](./connection.md)).

```cpp
auto a = co_await redis.auth("app_user", "s3cr3t");   // ACL form
if (!a)
    co_return;                                        // rejected; check a.error()

// Legacy form:
// co_await redis.auth("s3cr3t");
```

<!-- src: qbm/redis/tests/integration/connection/connection-commands.cpp:52-62 -->

### `echo` — round-trip check

```cpp
// Coroutine — connection_commands.h:136
auto echo(const std::string &message);             // -> Reply<std::string>

// Callback — connection_commands.h:152
template <typename Func>
std::enable_if_t<std::is_invocable_v<Func, Reply<std::string> &&>, Derived &>
echo(Func &&func, const std::string &message);
```

Returns `message` verbatim. Useful as a connectivity / round-trip probe that carries a distinguishable payload.

```cpp
auto r = co_await redis.echo("hello");
if (r.ok())
    assert(r.result() == "hello");
```

<!-- src: qbm/redis/tests/integration/connection/connection-commands.cpp:92-104 -->

### `ping` — liveness probe

```cpp
// No payload — connection_commands.h:163 / :177
auto ping();                                       // -> Reply<std::string> ("PONG")
template <typename Func>
std::enable_if_t<std::is_invocable_v<Func, Reply<std::string> &&>, Derived &>
ping(Func &&func);

// Custom payload — connection_commands.h:189 / :205
auto ping(const std::string &message);             // -> Reply<std::string> (echoes message)
template <typename Func>
std::enable_if_t<std::is_invocable_v<Func, Reply<std::string> &&>, Derived &>
ping(Func &&func, const std::string &message);
```

With no argument the reply is the string `"PONG"`. With a `message` argument the reply is that message echoed back (like
a lightweight `ECHO`). This is the cheapest way to confirm the connection is alive.

Coroutine form:

```cpp
auto r = co_await redis.ping();
if (r.ok())
    assert(r.result() == "PONG");
```

<!-- src: qbm/redis/tests/integration/connection/connection-commands.cpp:106-117 -->

Callback form:

```cpp
redis.ping([](qb::redis::Reply<std::string> &&reply) {
    if (reply) { /* reply.result() == "PONG" */ }
});
```

<!-- src: qbm/redis/src/qbm/redis/commands/connection_commands.h:175-179 -->

### `quit` — close the connection

```cpp
// Coroutine — connection_commands.h:216
auto quit();                                       // -> Reply<status>

// Callback — connection_commands.h:230
template <typename Func>
std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
quit(Func &&func);
```

`QUIT` asks the server to close the connection cleanly after sending its reply. It is distinct from the client-side
`disconnect()` method on the connector (which tears the local transport down without a server round-trip); `quit()` is
the wire-level `QUIT` command. Expect the connection to drop after the reply — do not issue further commands on it.

```cpp
auto r = co_await redis.quit();
// r.ok() && r.result().ok(); the server closes the link afterward.
```

<!-- src: qbm/redis/src/qbm/redis/commands/connection_commands.h:215-232 -->

### `select` — switch logical database

```cpp
// Coroutine — connection_commands.h:242
auto select(long long index);                      // -> Reply<status>

// Callback — connection_commands.h:257
template <typename Func>
std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
select(Func &&func, long long index);
```

Selects the connection's active logical database by zero-based `index`. The selection is per-connection state and is *
*not** restored across a reconnect — a fresh socket is back on database 0, so re-issue `select(...)` after
auto-reconnect.

```cpp
auto r = co_await redis.select(1);
assert(r.ok() && r.result().ok());
```

<!-- src: qbm/redis/tests/integration/connection/connection-commands.cpp:206-223 -->

### `swapdb` — swap two databases

```cpp
// Coroutine — connection_commands.h:270
auto swapdb(long long index1, long long index2);   // -> Reply<status>

// Callback — connection_commands.h:287
template <typename Func>
std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
swapdb(Func &&func, long long index1, long long index2);
```

Atomically swaps the entire contents of the two logical databases at `index1` and `index2`. Unlike `select`, this is a
server-side data operation, not per-connection state.

```cpp
auto r = co_await redis.swapdb(0, 1);
assert(r.ok() && r.result().ok());
```

<!-- src: qbm/redis/tests/integration/connection/connection-commands.cpp:225-239 -->

### `reset` — reset connection state

```cpp
// Coroutine — connection_commands.h:300
auto reset();                                      // -> Reply<status>

// Callback — connection_commands.h:314
template <typename Func>
std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
reset(Func &&func);
```

The Redis `RESET` command (Redis 6.2+) returns the connection to a clean baseline — it aborts any `MULTI`, unsubscribes
from all channels, deselects any `MONITOR`, switches back to RESP2 / the `default` user, and resets the selected
database to 0 — **without closing the socket**. The connection stays open, so you can keep issuing commands afterward.

```cpp
auto reply = co_await redis.reset();
if (reply.ok()) {
    auto ping_r = co_await redis.ping();           // connection still usable
    assert(ping_r.ok() && ping_r.result() == "PONG");
}
```

<!-- src: qbm/redis/tests/integration/connection/connection-commands.cpp:187-204 -->

> Three unrelated APIs share the name `reset`/`reset_*` in this module — the protocol parser's `redis<IO_>::reset()` (
`redis.h:198`), the transaction mixin's internal `reset_transaction_state()` (`transaction_commands.h:273`), and this user-facing
`RESET` command. This page documents only the last one.

---

## Examples

### Full handshake (coroutine)

```cpp
#include <qbm/redis/redis.h>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>

qb::io::async::task<void> handshake(qb::redis::tcp::client &redis) {
    if (!co_await redis.connect())                 // see connection.md
        co_return;

    co_await redis.hello(3);                        // opt into RESP3

    auto a = co_await redis.auth("app_user", "s3cr3t");
    if (!a)                                          // WRONGPASS / NOPERM
        co_return;

    auto s = co_await redis.select(2);
    if (!(s.ok() && s.result().ok()))
        co_return;

    auto pong = co_await redis.ping();
    if (pong.ok())
        qb::io::cout() << pong.result() << std::endl;  // "PONG"
}
```

<!-- src: qbm/redis/src/qbm/redis/commands/connection_commands.h; qbm/redis/tests/integration/connection/connection-commands.cpp:166-183 (HELLO+PING, composed) -->

### Callback chaining

The callback overloads return `Derived &`, so you can chain. Each callback receives a `Reply<T> &&`:

```cpp
qb::io::async::init();

qb::redis::tcp::client redis{qb::io::uri{"tcp://localhost:6379"}};
redis.connect([&redis](bool connected) {
    if (!connected) return;
    redis.ping([](qb::redis::Reply<std::string> &&reply) {
        if (reply) { /* reply.result() == "PONG" */ }
    });
});
```

<!-- src: qbm/redis/src/qbm/redis/commands/connection_commands.h:175-179 -->

---

## Pitfalls

- **`status` has two `ok()` layers.** For `Reply<status>` commands (`auth`, `select`, `swapdb`, `quit`, `reset`),
  `reply.ok()` only tells you the command round-tripped without a transport/server error. To confirm the server returned
  the literal `"OK"`, also check `reply.result().ok()`.
- **`hello` returns `qb::json`, not a string.** In RESP3 it is an object; in RESP2 an array. Test `info.is_object()` /
  `info.is_array()` before indexing, and use `contains(...)` before reading a field.
- **Authentication is a command, not a connect parameter.** The URI carries no credentials, and the client never stores
  them. After auto-reconnect you must re-`auth`, re-`hello(3)`, re-`select`, and re-`subscribe` — none of this
  connection state survives a reconnect.
- **`quit` closes the link; `reset` does not.** `RESET` clears connection state and leaves the socket open for reuse;
  `QUIT` asks the server to hang up. Do not keep issuing commands after `quit()`.
- **No time arguments here.** This group takes only `int` / `long long` / `std::string`. The `qb::duration` vs
  native-units (`EXPIRE` seconds / `PEXPIRE` milliseconds) boundary applies to key and stream commands, not to
  connection commands. Connect and command *deadlines* are `qb::duration` and live on the client —
  see [connection.md](./connection.md). Do not reach for the retired `qb::Duration` / `qb::Timestamp` aliases; they are
  removed framework-wide.
- **Don't confuse the three `reset` surfaces.** Only `connection_commands::reset()` is the wire-level `RESET` command;
  the parser and transaction `reset` helpers are internal.

---

## See also

- [connection.md](./connection.md) — opening the connection, connect/command deadlines (`qb::duration`), auto-reconnect,
  and why connection state is not replayed
- [commands_overview.md](./commands_overview.md) — the coroutine/callback dual API, `Reply<T>`, and the native time-unit
  boundary for command arguments
- [error_handling.md](./error_handling.md) — `Reply<T>`, `AuthError`, `ConnectionError`, the disconnect signal
- [subscription_commands.md](./subscription_commands.md) — pub/sub and re-subscription after `RESET` or reconnect
- [../README.md](../README.md) — install, `qb_load_modules`, `qbm::redis`, quick start
