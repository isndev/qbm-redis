# Server administration commands

> **Audience:** Adopter · **Status:** stable · **Verified-against:** qbm-redis @ qb 3.0.0 (C++20 default, C++23
> supported)

Reference for the server administration command group — the `CLIENT`, `CONFIG`, `COMMAND`, `DEBUG`, `MEMORY`, `LATENCY`,
`SLOWLOG`, `MONITOR`, persistence (`SAVE`/`BGSAVE`/`BGREWRITEAOF`), database (`DBSIZE`/`FLUSHDB`/`FLUSHALL`),
replication (`SLAVEOF`/`SYNC`/`PSYNC`/`FAILOVER`), and lifecycle (`SHUTDOWN`) wrappers exposed by
`server_commands<Derived>`.

**Prerequisites:** [../README.md](../README.md) (install, `qb_load_modules`,
`qbm::redis`), [connection.md](./connection.md), [commands_overview.md](./commands_overview.md) (the `Reply<T>` model,
coroutine vs. callback forms) — **See also:** [cluster_commands.md](./cluster_commands.md) (`CLUSTER`
topology), [acl_commands.md](./acl_commands.md) (`ACL` users), [error_handling.md](./error_handling.md)

---

## Summary

These commands administer the *server*, not your data: list and kill connections, read and write the running
configuration, inspect memory and latency, drive persistence, and reconfigure replication. They are thin RESP wrappers.
None of them validate intent — `debug_segfault()` crashes the server, `shutdown()` stops it, `flushall()` erases every
database — so guard them behind ACLs and treat them as privileged operations.

The `server_commands<Derived>` mixin is one of the command groups inherited by the concrete client
`qb::redis::tcp::client` (and `qb::redis::tcp::ssl::client`). You never instantiate the mixin directly; you call these
methods on a client instance.

<!-- src: qbm/redis/src/qbm/redis/redis.h:703 (public inheritance), redis.h:1618 (tcp::client alias) -->

Every command is exposed in two fully asynchronous forms, both shown throughout this page:

- a **coroutine** form (`auto`-returning) that yields a `Reply<T>` you `co_await`;
- a **callback** form that takes your handler **first** and returns `Derived&` for chaining.

There is no blocking variant — this module has never shipped "Sync" signatures, and there are no `_async`-suffixed
names. If you have seen those in older notes, they are wrong.

```cpp
#include <qbm/redis/redis.h>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>

// <!-- src: qbm/redis/tests/integration/server/server-introspection.cpp:235-274 -->
qb::io::async::task<void> admin_demo(qb::redis::tcp::client &redis) {
    auto size = co_await redis.dbsize();              // Reply<long long>
    if (size)                                         // Reply<T> is contextually bool (== ok())
        qb::io::cout() << "keys: " << size.result() << std::endl;

    auto cleared = co_await redis.flushdb();          // Reply<qb::redis::status>
    qb::io::cout() << "flushdb ok: " << cleared.ok() << std::endl;
}
```

---

## Concepts

### Reply types you will meet here

- **`qb::redis::status`** — the `OK`-style simple-string reply. It converts implicitly to `std::string` and to `bool`,
  where the `bool`/`ok()` value is `true` **only** when the server string is exactly `"OK"`. Test success with `.ok()` (
  or the `bool` conversion), not by comparing against arbitrary strings. <!-- src: qbm/redis/src/qbm/redis/types.h:468-514 -->
- **`long long`** — counts and timestamps (`DBSIZE`, `COMMAND COUNT`, `LASTSAVE`, `CLIENT ID`, `MEMORY USAGE`,
  `LATENCY RESET`).
- **`std::string`** — opaque human-readable blobs (`MEMORY DOCTOR`, `DEBUG OBJECT`, `CLIENT INFO`, `LATENCY DOCTOR`/
  `GRAPH`).
- **`std::vector<std::string>`** — flat string arrays (`MEMORY HELP`, `COMMAND GETKEYS`, `COMMAND LIST`).
- **`qb::json`** (alias of `nlohmann::json`, from `<qb/json.h>`) — the wrapper boxes the RESP reply into a JSON value:
  `INFO`, `ROLE`, `COMMAND`/`COMMAND DOCS`/`COMMAND STATS`, `MEMORY STATS`, `SLOWLOG GET`, `LATENCY LATEST`/`HISTORY`/
  `HISTOGRAM`, `CLIENT TRACKINGINFO`, `CLIENT LIST`. How much structure you get depends on what the server sends: a RESP
  map or array (such as `CLIENT TRACKINGINFO` or `MEMORY STATS`) becomes a JSON object/array, but a bulk-string reply
  that is not itself JSON-shaped — notably `CLIENT LIST`, whose payload is a newline-delimited text block — arrives as a
  JSON **string** carrying the raw text, not a parsed structure. Test with `result().is_object()` / `is_array()` /
  `is_string()` before
  indexing. <!-- src: qbm/redis/src/qbm/redis/reply.cpp:546-585 (qb::json parse), qbm/redis/tests/integration/server/server-introspection.cpp:65-88 -->
- **`std::vector<std::pair<std::string, std::string>>`** — `CONFIG GET` (parameter/value pairs).
- **`std::vector<std::map<std::string, std::string>>`** — `COMMAND INFO` (one map per command).
- **`std::pair<long long, long long>`** — `TIME` (seconds, microseconds), in the coroutine form only (see below).

### Time units: one `qb::duration`, one native boundary

This group has exactly **two** time-bearing arguments, and they use **different** units by design:

- `debug_sleep(qb::duration)` takes the canonical [`qb::duration`](https://github.com/isndev/qb/blob/main/include/qb/system/time.h) (any
  `std::chrono` duration converts implicitly). The wrapper converts it to libev fractional seconds via
  `qb::detail::to_ev_seconds(delay)` before placing it on the wire, so `DEBUG SLEEP` receives a fractional-seconds
  argument. <!-- src: qbm/redis/src/qbm/redis/commands/server_commands.h:698-711 (to_ev_seconds at :711) -->
- `client_pause(long long timeout, ...)` takes a **raw `long long` of milliseconds** — the native `CLIENT PAUSE` unit —
  **not** a `qb::duration`. This is a deliberate native-unit boundary, distinct from
  `debug_sleep`. <!-- src: qbm/redis/src/qbm/redis/commands/server_commands.h:273-293 -->

Do not pass the retired `qb::Duration`/`qb::Timestamp` types — they were removed from the framework. Use
`qb::duration` (a `std::chrono`-based span) for `debug_sleep`, and a plain integer count of milliseconds for
`client_pause`.

`LASTSAVE` and `TIME` *return* Unix timestamps; those are plain integers (`long long`), not durations.

### `TIME` reshapes its reply in the coroutine form only

The two `TIME` overloads return **different types**:

- the coroutine form `time()` yields `Reply<std::pair<long long, long long>>` — `{seconds, microseconds}` — and parses
  the two RESP fields with `std::from_chars`. When the reply carries two fields that are not both numeric (a malformed
  or proxied reply), it sets `ok() == false` and `error() == "Malformed TIME reply"` instead of throwing, because
  throwing out of the reply handler would propagate into the libev callback and terminate the
  process. <!-- src: qbm/redis/src/qbm/redis/commands/server_commands.h:1299-1323 -->
- the callback form `time(func)` hands back the raw `Reply<std::vector<std::string>>` with no
  reshaping. <!-- src: qbm/redis/src/qbm/redis/commands/server_commands.h:1332-1336 -->

### `MONITOR` is callback-only and long-lived

`monitor(func)` has **no** coroutine overload. It enters monitoring mode and invokes your callback **repeatedly** — one
`Reply<std::string>` per command the server processes — for the life of the `MONITOR` stream. Treat it like a
subscription, not a one-shot request, and dedicate a connection to it. <!-- src: qbm/redis/src/qbm/redis/commands/server_commands.h:841-845 -->

### `COMMAND`'s named overload actually issues `COMMAND INFO`

`command(func, names)` with a **non-empty** `names` vector issues `COMMAND INFO names`, not the top-level `COMMAND`
dump; with an **empty** vector it silently falls back to the all-commands `COMMAND` form. If you want structured
per-command metadata, this is expected; if you wanted the full catalog, pass no
names. <!-- src: qbm/redis/src/qbm/redis/commands/server_commands.h:606-613 -->

---

## Command reference

Signatures below are the public methods on `server_commands<Derived>`. For each, the **coroutine** form returns an
awaiter yielding `Reply<T>`; the **callback** form is the `Func`-first, SFINAE-gated overload returning `Derived&`. Read
`Reply<T>` as "yields `Reply<T>`". The callback signature is uniformly `void(Reply<T>&&)`.

### Client management

| Command               | Coroutine signature                                                                                             | Reply `T`                    |
|-----------------------|-----------------------------------------------------------------------------------------------------------------|------------------------------|
| `CLIENT ID`           | `client_id()`                                                                                                   | `long long`                  |
| `CLIENT GETNAME`      | `client_getname()`                                                                                              | `std::optional<std::string>` |
| `CLIENT SETNAME`      | `client_setname(const std::string &name)`                                                                       | `status`                     |
| `CLIENT KILL`         | `client_kill(const std::string &addr = "", long long id = 0, const std::string &type = "", bool skipme = true)` | `long long`                  |
| `CLIENT PAUSE`        | `client_pause(long long timeout, const std::string &mode = "ALL")`                                              | `status`                     |
| `CLIENT UNPAUSE`      | `client_unpause()`                                                                                              | `status`                     |
| `CLIENT UNBLOCK`      | `client_unblock(long long client_id, bool error = false)`                                                       | `status`                     |
| `CLIENT TRACKING`     | `client_tracking(bool enabled = true)`                                                                          | `status`                     |
| `CLIENT TRACKINGINFO` | `client_tracking_info()`                                                                                        | `qb::json`                   |
| `CLIENT CACHING`      | `client_caching(bool yes)`                                                                                      | `status`                     |
| `CLIENT GETREDIR`     | `client_getredir()`                                                                                             | `long long`                  |
| `CLIENT INFO`         | `client_info()`                                                                                                 | `std::string`                |
| `CLIENT LIST`         | `client_list()`                                                                                                 | `qb::json`                   |
| `CLIENT NO-EVICT`     | `client_no_evict(bool on)`                                                                                      | `status`                     |
| `CLIENT NO-TOUCH`     | `client_no_touch(bool on)`                                                                                      | `status`                     |
| `CLIENT REPLY`        | `client_reply(const std::string &mode)`                                                                         | `status`                     |
| `CLIENT SETINFO`      | `client_setinfo(const std::string &attr, const std::string &value)`                                             | `status`                     |

Notes:

- `client_kill` always emits the FILTER form, so Redis answers with the NUMBER of connections killed, not `+OK`: the reply
  type is `long long`. (It was `status`, which cannot decode an integer — every call failed with "STRING or ERROR required
  for status" until the first test called it.) It emits `ADDR addr`, `ID id`, `TYPE type` only for non-default arguments, and `SKIPME yes` when `skipme`
  is `true`. `type` is a raw string (`normal`/`master`/`replica`/`pubsub`) — it is not validated
  client-side. <!-- src: qbm/redis/src/qbm/redis/commands/server_commands.h:140-193 -->
- `client_pause`'s `timeout` is **milliseconds** (native unit), not a `qb::duration`. `mode` is `"ALL"` or
  `"WRITE"`. <!-- src: qbm/redis/src/qbm/redis/commands/server_commands.h:273-293 -->
- `client_tracking(true)` sends `CLIENT TRACKING ON`; `false` sends `OFF`. `client_caching`, `client_no_evict`,
  `client_no_touch` map `true`/`false` to `YES`/`NO` or `ON`/`OFF`
  respectively. <!-- src: qbm/redis/src/qbm/redis/commands/server_commands.h:312-329, 1745-1785 -->
- `client_reply`'s `mode` (`ON`/`OFF`/`SKIP`) is passed through
  unvalidated. <!-- src: qbm/redis/src/qbm/redis/commands/server_commands.h:1796-1818 -->
- `client_list()` yields a `qb::json` **string** holding the raw `CLIENT LIST` text block (one client per line), not a
  parsed array — the payload is line-delimited text, not JSON, so the wrapper boxes it verbatim. Parse the lines
  yourself, or call `client_info()` for the current connection. `client_tracking_info()`, by contrast, is a RESP map and
  comes back as a JSON object. <!-- src: qbm/redis/src/qbm/redis/commands/server_commands.h:1347-1363 (client_list), qbm/redis/src/qbm/redis/reply.cpp:546-585 (qb::json string boxing) -->

```cpp
// <!-- src: qbm/redis/tests/integration/server/server-admin-commands.cpp:51-75 -->
qb::io::async::task<void> clients(qb::redis::tcp::client &redis) {
    co_await redis.client_setname("worker-1");
    auto name = co_await redis.client_getname();      // Reply<std::optional<std::string>>
    if (name && name.result())
        qb::io::cout() << "name: " << *name.result() << std::endl;

    auto id = co_await redis.client_id();             // Reply<long long>
    qb::io::cout() << "id: " << id.result() << std::endl;

    // CLIENT PAUSE — timeout is MILLISECONDS (native unit), not qb::duration.
    co_await redis.client_pause(100, "WRITE");
    co_await redis.client_unpause();                  // restore normal processing
}
```

Callback form (`client_id` shown; every command above has the symmetric overload):

```cpp
// Func is the FIRST argument; the call returns Derived& for chaining.
redis.client_id([](qb::redis::Reply<long long> &&r) {
    if (r.ok())
        qb::io::cout() << "id: " << r.result() << std::endl;
});
```

`MONITOR` (callback-only, streaming):

```cpp
// <!-- src: qbm/redis/src/qbm/redis/commands/server_commands.h:841-845 -->
// The callback fires once per command the server processes, for the stream's life.
redis.monitor([](qb::redis::Reply<std::string> &&line) {
    if (line.ok())
        qb::io::cout() << "MONITOR " << line.result() << std::endl;
});
```

### Configuration

| Command            | Coroutine signature                                                  | Reply `T`                                          |
|--------------------|----------------------------------------------------------------------|----------------------------------------------------|
| `CONFIG GET`       | `config_get(const std::string &parameter)`                           | `std::vector<std::pair<std::string, std::string>>` |
| `CONFIG SET`       | `config_set(const std::string &parameter, const std::string &value)` | `status`                                           |
| `CONFIG RESETSTAT` | `config_resetstat()`                                                 | `status`                                           |
| `CONFIG REWRITE`   | `config_rewrite()`                                                   | `status`                                           |

`config_get` accepts a glob (`"*max*"`) and returns one pair per matched parameter. The wrapper folds the flat RESP
array into pairs for you. <!-- src: qbm/redis/src/qbm/redis/commands/server_commands.h:377-415 -->

```cpp
// <!-- src: qbm/redis/tests/integration/server/server-admin-commands.cpp:145-188 -->
qb::io::async::task<void> config(qb::redis::tcp::client &redis) {
    auto cur = co_await redis.config_get("maxmemory");   // Reply<vector<pair<string,string>>>
    if (cur && !cur.result().empty()) {
        co_await redis.config_set("maxmemory", cur.result()[0].second);
    }
    auto many = co_await redis.config_get("*max*");      // glob: many pairs
    for (const auto &[k, v] : many.result())
        qb::io::cout() << k << " = " << v << std::endl;
}
```

### Command introspection

| Command                             | Coroutine signature                                                                              | Reply `T`                                         |
|-------------------------------------|--------------------------------------------------------------------------------------------------|---------------------------------------------------|
| `COMMAND`                           | `command()`                                                                                      | `qb::json`                                        |
| `COMMAND INFO` (via named overload) | `command(const std::vector<std::string> &command_names)`                                         | `qb::json`                                        |
| `COMMAND INFO`                      | `command_info(const std::vector<std::string> &command_names = {})`                               | `std::vector<std::map<std::string, std::string>>` |
| `COMMAND COUNT`                     | `command_count()`                                                                                | `long long`                                       |
| `COMMAND GETKEYS`                   | `command_getkeys(const std::string &command, const std::vector<std::string> &args)`              | `std::vector<std::string>`                        |
| `COMMAND GETKEYSANDFLAGS`           | `command_getkeysandflags(const std::string &command_name, const std::vector<std::string> &args)` | `qb::json`                                        |
| `COMMAND DOCS`                      | `command_docs(const std::vector<std::string> &command_names = {})`                               | `qb::json`                                        |
| `COMMAND LIST`                      | `command_list(const std::vector<std::string> &filter = {})`                                      | `std::vector<std::string>`                        |
| `COMMAND STATS`                     | `command_stats()`                                                                                | `qb::json`                                        |

See the [`COMMAND` named-overload note](#commands-named-overload-actually-issues-command-info) above: `command(names)`
with a non-empty vector returns `COMMAND INFO` data.

```cpp
// <!-- src: qbm/redis/tests/integration/server/server-admin-commands.cpp:211-249 -->
qb::io::async::task<void> introspect(qb::redis::tcp::client &redis) {
    auto n = co_await redis.command_count();          // Reply<long long>
    qb::io::cout() << "command count: " << n.result() << std::endl;

    auto keys = co_await redis.command_getkeys("SET", {"k", "v"});  // -> {"k"}
    for (const auto &k : keys.result())
        qb::io::cout() << "key: " << k << std::endl;
}
```

### Memory

| Command               | Coroutine signature                                           | Reply `T`                  |
|-----------------------|---------------------------------------------------------------|----------------------------|
| `MEMORY USAGE`        | `memory_usage(const std::string &key, long long samples = 0)` | `long long`                |
| `MEMORY STATS`        | `memory_stats()`                                              | `qb::json`                 |
| `MEMORY DOCTOR`       | `memory_doctor()`                                             | `std::string`              |
| `MEMORY MALLOC-STATS` | `memory_malloc_stats()`                                       | `std::string`              |
| `MEMORY PURGE`        | `memory_purge()`                                              | `status`                   |
| `MEMORY HELP`         | `memory_help()`                                               | `std::vector<std::string>` |

`memory_usage` appends `SAMPLES n` only when `samples > 0`. The reply is the estimated size of `key` in
bytes. <!-- src: qbm/redis/src/qbm/redis/commands/server_commands.h:812-830 -->

```cpp
// <!-- src: qbm/redis/tests/integration/server/server-introspection.cpp:194-230 -->
qb::io::async::task<void> mem(qb::redis::tcp::client &redis) {
    auto used = co_await redis.memory_usage("mykey");      // Reply<long long> (bytes)
    qb::io::cout() << "bytes: " << used.result() << std::endl;
    auto sampled = co_await redis.memory_usage("mykey", 5); // SAMPLES 5
    qb::io::cout() << "sampled bytes: " << sampled.result() << std::endl;
}
```

### Latency

| Command             | Coroutine signature                                                | Reply `T`     |
|---------------------|--------------------------------------------------------------------|---------------|
| `LATENCY LATEST`    | `latency_latest()`                                                 | `qb::json`    |
| `LATENCY HISTORY`   | `latency_history(const std::string &event)`                        | `qb::json`    |
| `LATENCY HISTOGRAM` | `latency_histogram(const std::vector<std::string> &commands = {})` | `qb::json`    |
| `LATENCY RESET`     | `latency_reset(const std::string &event_name = "")`                | `long long`   |
| `LATENCY DOCTOR`    | `latency_doctor()`                                                 | `std::string` |
| `LATENCY GRAPH`     | `latency_graph(const std::string &event)`                          | `std::string` |

`latency_reset()` with no event resets all events and returns the count reset; with an event name it resets that
one. <!-- src: qbm/redis/src/qbm/redis/commands/server_commands.h:1432-1453 -->

### Slowlog

| Command         | Coroutine signature                 | Reply `T`   |
|-----------------|-------------------------------------|-------------|
| `SLOWLOG GET`   | `slowlog_get(long long count = 10)` | `qb::json`  |
| `SLOWLOG LEN`   | `slowlog_len()`                     | `long long` |
| `SLOWLOG RESET` | `slowlog_reset()`                   | `status`    |

```cpp
// <!-- src: qbm/redis/tests/integration/server/server-introspection.cpp:155-188 -->
qb::io::async::task<void> slowlog(qb::redis::tcp::client &redis) {
    auto len = co_await redis.slowlog_len();          // Reply<long long>
    auto entries = co_await redis.slowlog_get(5);     // Reply<qb::json> (newest 5)
    qb::io::cout() << "entries: " << entries.result().size() << std::endl;
    co_await redis.slowlog_reset();                   // Reply<status>
}
```

### Debugging

| Command          | Coroutine signature                    | Reply `T`     |
|------------------|----------------------------------------|---------------|
| `DEBUG OBJECT`   | `debug_object(const std::string &key)` | `std::string` |
| `DEBUG SLEEP`    | `debug_sleep(qb::duration delay)`      | `status`      |
| `DEBUG SEGFAULT` | `debug_segfault()`                     | `status`      |

`debug_sleep` is the **only** command in this group that takes a [
`qb::duration`](https://github.com/isndev/qb/blob/main/include/qb/system/time.h); any `std::chrono` duration converts implicitly.
`debug_segfault` **crashes the server** — it exists for fault-injection testing only.

```cpp
// <!-- src: qbm/redis/tests/integration/server/server-admin-commands.cpp:334-366 -->
qb::io::async::task<void> debug(qb::redis::tcp::client &redis) {
    // qb::duration accepts any std::chrono unit.
    auto r = co_await redis.debug_sleep(std::chrono::milliseconds(10));  // Reply<status>
    qb::io::cout() << "slept ok: " << r.ok() << std::endl;
}
```

### Persistence

| Command        | Coroutine signature             | Reply `T`   |
|----------------|---------------------------------|-------------|
| `SAVE`         | `save()`                        | `status`    |
| `BGSAVE`       | `bgsave(bool schedule = false)` | `status`    |
| `BGREWRITEAOF` | `bgrewriteaof()`                | `status`    |
| `LASTSAVE`     | `lastsave()`                    | `long long` |

`save()` blocks the server until the RDB snapshot is written; prefer `bgsave()` in production. `bgsave(true)` sends
`BGSAVE SCHEDULE`. `lastsave()` returns the Unix timestamp (seconds) of the last successful
save. <!-- src: qbm/redis/src/qbm/redis/commands/server_commands.h:1055-1169 -->

```cpp
// <!-- src: qbm/redis/tests/integration/server/server-introspection.cpp:304-327 -->
qb::io::async::task<void> persist(qb::redis::tcp::client &redis) {
    co_await redis.bgsave();                           // background snapshot
    co_await redis.bgsave(true);                       // BGSAVE SCHEDULE
    auto when = co_await redis.lastsave();             // Reply<long long> (unix seconds)
    qb::io::cout() << "last save at: " << when.result() << std::endl;
}
```

### Database

| Command    | Coroutine signature            | Reply `T`   |
|------------|--------------------------------|-------------|
| `DBSIZE`   | `dbsize()`                     | `long long` |
| `FLUSHDB`  | `flushdb(bool async = false)`  | `status`    |
| `FLUSHALL` | `flushall(bool async = false)` | `status`    |

`flushdb(true)` / `flushall(true)` append `ASYNC` so the server reclaims memory in the background; the default is
synchronous. These are **destructive** — `flushall` erases every database, not just the selected
one. <!-- src: qbm/redis/src/qbm/redis/commands/server_commands.h:1199-1249 -->

```cpp
// <!-- src: qbm/redis/tests/integration/server/server-introspection.cpp:235-274 -->
qb::io::async::task<void> db(qb::redis::tcp::client &redis) {
    auto n = co_await redis.dbsize();                  // Reply<long long>
    co_await redis.flushdb();                          // synchronous wipe of current db
    co_await redis.flushdb(true);                      // FLUSHDB ASYNC
}
```

### Server information

| Command            | Coroutine signature                     | Reply `T`                                           |
|--------------------|-----------------------------------------|-----------------------------------------------------|
| `INFO`             | `info(const std::string &section = "")` | `qb::json`                                          |
| `TIME` (coroutine) | `time()`                                | `std::pair<long long, long long>`                   |
| `TIME` (callback)  | `time(Func &&func)`                     | callback receives `Reply<std::vector<std::string>>` |
| `ROLE`             | `role()`                                | `qb::json`                                          |

`info()` returns the textual `INFO` payload boxed as a `qb::json` **string** (`result().is_string()` is `true`) —
like `CLIENT LIST`. Use `result().get<std::string>()` and parse the lines yourself; pass a section name (`"memory"`,
`"replication"`, ...) to narrow the text. `time()`'s two overloads return **different** types — see the [
`TIME` note](#time-reshapes-its-reply-in-the-coroutine-form-only). <!-- src: qbm/redis/src/qbm/redis/commands/server_commands.h:1266-1285 (info), qbm/redis/src/qbm/redis/reply.cpp:546-585 (qb::json string boxing) -->

```cpp
// <!-- src: qbm/redis/src/qbm/redis/commands/server_commands.h:1266-1285 (info), 1290-1314 (time coroutine) -->
qb::io::async::task<void> server_info(qb::redis::tcp::client &redis) {
    auto info = co_await redis.info("server");         // Reply<qb::json>
    qb::io::cout() << info.result().dump() << std::endl;

    auto t = co_await redis.time();                    // Reply<std::pair<long long,long long>>
    if (t)                                             // {seconds, microseconds}
        qb::io::cout() << "server time: " << t.result().first << std::endl;
}
```

### Replication and lifecycle

| Command                 | Coroutine signature                                          | Reply `T` |
|-------------------------|--------------------------------------------------------------|-----------|
| `SLAVEOF` / `REPLICAOF` | `slaveof(const std::string &host, long long port)`           | `status`  |
| `SYNC`                  | `sync()`                                                     | `status`  |
| `PSYNC`                 | `psync(const std::string &replication_id, long long offset)` | `status`  |
| `FAILOVER`              | `failover(const std::vector<std::string> &options = {})`     | `status`  |
| `SHUTDOWN`              | `shutdown(const std::string &save_option = "")`              | `status`  |

`slaveof(host, port)` makes the server a replica of `host:port`; it always emits `SLAVEOF <host> <port>`. Because `port`
is typed `long long`, this overload **cannot** express `SLAVEOF NO ONE` (promote to master) — that variant takes the
literal token `ONE` where a port number is required, so there is no public method for it in this group; reach for the
cluster API ([cluster_commands.md](./cluster_commands.md)) for managed topology, or send the raw command yourself.
`shutdown()` with no argument sends `SHUTDOWN`; pass `"SAVE"` or `"NOSAVE"` to control the final snapshot. `SHUTDOWN` *
*stops the server**: the connection drops and you will typically see a connection error rather than a status reply.
`SYNC`/`PSYNC` are internal replication primitives and are rarely called
directly. <!-- src: qbm/redis/src/qbm/redis/commands/server_commands.h:929-947 (slaveof always emits host+port), 1002 (sync), 1028 (psync), 1616 (failover), 895-916 (shutdown) -->

```cpp
qb::io::async::task<void> replication(qb::redis::tcp::client &redis) {
    co_await redis.slaveof("10.0.0.2", 6379);          // Reply<status> -> becomes a replica
    // ... later, to save before stopping:
    co_await redis.shutdown("SAVE");                    // connection will drop
}
```

---

## Pitfalls

- **No `_async` methods, no `Reply<...>` sync returns.** The API is coroutine-or-callback. Older notes that show
  `client_getname_async(...)` or a blocking `Reply<...> dbsize()` are describing an API that does not exist. Use
  `co_await redis.dbsize()` or `redis.dbsize(callback)`.
- **`client_pause` is milliseconds, `debug_sleep` is `qb::duration`.** These two live in the same class but use
  different units on purpose. Passing a `qb::duration` to `client_pause` will not compile; for `debug_sleep` prefer an
  explicit `std::chrono::milliseconds(...)`. Never reach for the retired `qb::Duration`/`qb::Timestamp`.
- **`command(names)` returns `COMMAND INFO`, not the catalog.** If you want the full command dump, call `command()` with
  no arguments.
- **`time()`'s coroutine and callback forms return different types** (`std::pair<long long,long long>` vs
  `std::vector<std::string>`). The coroutine form also degrades gracefully (`ok() == false`,
  `error() == "Malformed TIME reply"`) on a malformed reply instead of throwing.
- **`monitor` never resolves.** It is callback-only and streams indefinitely; do not `co_await` it (there is no awaiter
  overload) and do not run it on a shared command connection.
- **Destructive admin has no guard rails.** `flushall`, `flushdb`, `shutdown`, `debug_segfault`, `config_set`, and the
  replication commands all do exactly what they say with no confirmation. The wrappers do not validate subcommand
  strings (`client_reply` mode, `client_kill` type, `shutdown` save-option) — invalid values fail only on the server.
  Restrict these with ACLs (see [acl_commands.md](./acl_commands.md)).
- **`memory_info` is not reachable here.** The header carries a private INFO-to-`memory_info` parser and a `memory_info`
  struct, but no public command in this group exposes them — `info()` returns `qb::json`. Do not write code against
  `memory_info` expecting `info()` to populate it. <!-- src: qbm/redis/src/qbm/redis/commands/server_commands.h:52, types.h -->

---

## See also

- [commands_overview.md](./commands_overview.md) — the `Reply<T>` contract, coroutine vs. callback dispatch, and the
  framework-wide time-unit boundary.
- [cluster_commands.md](./cluster_commands.md) — `CLUSTER` topology administration (a separate mixin).
- [acl_commands.md](./acl_commands.md) — `ACL` user and permission management; pair with these admin commands to
  restrict who can run them.
- [connection.md](./connection.md) — connecting, connect/command timeouts (`qb::duration`), and `RetryPolicy`.
- [error_handling.md](./error_handling.md) — how `Reply<T>` carries `ok()`/`error()` and why reply handlers never throw
  across the libev boundary.
