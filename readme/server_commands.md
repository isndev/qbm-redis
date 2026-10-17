# Server administration commands

> **Audience:** Adopter · **Status:** stable · **Verified-against:** qbm-redis @ qb 3.2.1 (C++20 default, C++23
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

<!-- src: qbm/redis/src/qbm/redis/redis.h:1453 (public inheritance), qbm/redis/src/qbm/redis/redis.h:3326 (tcp::client alias) -->

Every command is exposed in two fully asynchronous forms, both shown throughout this page:

- a **coroutine** form (`auto`-returning) that yields a `Reply<T>` you `co_await`;
- a **callback** form that takes your handler **first** and returns `Derived&` for chaining.

There is no blocking variant — this module has never shipped "Sync" signatures, and there are no `_async`-suffixed
names. If you have seen those in older notes, they are wrong.

```cpp
#include <qbm/redis/redis.h>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>

// <!-- src: qbm/redis/tests/integration/server/server-introspection.cpp:267-306 -->
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
  or the `bool` conversion), not by comparing against arbitrary strings. <!-- src: qbm/redis/src/qbm/redis/types.h:553-604 -->
- **`long long`** — counts and timestamps (`DBSIZE`, `COMMAND COUNT`, `LASTSAVE`, `CLIENT ID`, `MEMORY USAGE`,
  `LATENCY RESET`).
- **`std::string`** — opaque human-readable blobs (`MEMORY DOCTOR`, `DEBUG OBJECT`, `CLIENT INFO`, `LATENCY DOCTOR`/
  `GRAPH`).
- **`std::vector<std::string>`** — flat string arrays (`MEMORY HELP`, `COMMAND GETKEYS`, `COMMAND LIST`).
- **`qb::json`** (alias of `nlohmann::json`, from `<qb/json.h>`) — the wrapper boxes the RESP reply into a JSON value:
  `INFO`, `ROLE`, `COMMAND`/`COMMAND DOCS`, `command_stats()`, `MEMORY STATS`, `SLOWLOG GET`, `LATENCY LATEST`/`HISTORY`/
  `HISTOGRAM`, `CLIENT TRACKINGINFO`, `CLIENT LIST`. How much structure you get depends on what the server sends: a RESP
  map or array (such as `CLIENT TRACKINGINFO` or `MEMORY STATS`) becomes a JSON object/array, but a bulk-string reply
  that is not itself JSON-shaped — notably `CLIENT LIST`, whose payload is a newline-delimited text block — arrives as a
  JSON **string** carrying the raw text, not a parsed structure. Test with `result().is_object()` / `is_array()` /
  `is_string()` before
  indexing. `role()` uses a command-specific conversion that keeps native positional arrays; `command_stats()`
  parses its INFO section into an object of raw text values. These exceptions do not change generic JSON
  conversion. <!-- src: qbm/redis/src/qbm/redis/reply.cpp:558-597 (qb::json parse), qbm/redis/tests/integration/server/server-introspection.cpp:65-88 -->
- **`std::vector<std::pair<std::string, std::string>>`** — `CONFIG GET` (parameter/value pairs).
- **`qb::json` from `command_info()`** — `COMMAND INFO` as an array with one entry per requested name: an object whose
  positions are named (`name`, `arity`, `flags`, `first_key`, `last_key`, `step`, then `acl_categories`, `tips`,
  `key_specifications` and `subcommands` as far as the server sends them), native types kept, or `null` for a name the
  server does not know (Huly QB-653).
- **`std::pair<long long, long long>`** — `TIME` (seconds, microseconds), in the coroutine form only (see below).

### Time units: one `qb::duration`, one native boundary

This group has exactly **two** time-bearing arguments, and they use **different** units by design:

- `debug_sleep(qb::duration)` takes the canonical [`qb::duration`](https://github.com/isndev/qb/blob/main/src/qb/system/time.h) (any
  `std::chrono` duration converts implicitly). The wrapper converts it to libev fractional seconds via
  `qb::detail::to_ev_seconds(delay)` before placing it on the wire, so `DEBUG SLEEP` receives a fractional-seconds
  argument. <!-- src: qbm/redis/src/qbm/redis/commands/server_commands.h:701-714 (to_ev_seconds at :714) -->
- `client_pause(long long timeout, ...)` takes a **raw `long long` of milliseconds** — the native `CLIENT PAUSE` unit —
  **not** a `qb::duration`. This native-unit boundary belongs to `client_pause`. <!-- src: qbm/redis/src/qbm/redis/commands/server_commands.h:272-292 -->

Do not pass the retired `qb::Duration`/`qb::Timestamp` types — they were removed from the framework. Use
`qb::duration` (a `std::chrono`-based span) for `debug_sleep`, and a plain integer count of milliseconds for
`client_pause`.

`LASTSAVE` and `TIME` *return* Unix timestamps; those are plain integers (`long long`), not durations.

### `TIME` reshapes its reply in the coroutine form only

The two `TIME` overloads return **different types**:

- the coroutine form `time()` yields `Reply<std::pair<long long, long long>>` — `{seconds, microseconds}`. When
  at least two fields are present, it converts the first two with `std::from_chars`; a conversion error sets
  `ok() == false` and `error() == "Malformed TIME reply"`. The adapter preserves a source failure
  unchanged. <!-- src: qbm/redis/src/qbm/redis/commands/server_commands.h:1292-1315 -->
- the callback form `time(func)` hands back the raw `Reply<std::vector<std::string>>` with no
  reshaping. <!-- src: qbm/redis/src/qbm/redis/commands/server_commands.h:1324-1328 -->

Both TIME forms preserve the source error and raw reply. CONFIG GET also retains its source error and raw
value while reshaping the parameter/value pairs; neither adapter replaces a server or decode failure with an
empty error string. <!-- src: qbm/redis/src/qbm/redis/commands/server_commands.h:377-391,1292-1315 -->

### `MONITOR` is callback-only and long-lived

`monitor(func)` has **no** coroutine overload. Use a dedicated client connection. The callback receives the initial
`+OK`, then one `Reply<std::string>` per command the server processes. It receives one failed reply when `RESET` or
`QUIT` ends monitoring, or when the connection drops. While the stream is active, other commands fail locally; the
`RESET`/`QUIT` reply goes to its own callback. If you disconnect from a line callback while either is pending, its
callback fails with `"disconnected"`; later frames from that read batch are discarded. Reconnect does not restart
monitoring. <!-- src: qbm/redis/src/qbm/redis/redis.h:2028-2069,1728-1777,1810-1823,1994-2023,2166-2177 -->

### `COMMAND`'s named overload actually issues `COMMAND INFO`

`command(func, names)` with a **non-empty** `names` vector issues `COMMAND INFO names`, not the top-level `COMMAND`
dump; with an **empty** vector it silently falls back to the all-commands `COMMAND` form. If you want structured
per-command metadata, this is expected; if you wanted the full catalog, pass no
names. <!-- src: qbm/redis/src/qbm/redis/commands/server_commands.h:604-611 -->

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
| `CLIENT UNBLOCK`      | `client_unblock(long long client_id, bool error = false)`                                                       | `bool`                       |
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
  for status" until the first test called it.) It emits `ADDR addr`, `ID id`, `TYPE type` only for non-default arguments, and always emits `SKIPME yes` or `SKIPME no` from the explicit `skipme` value.
  The default `true` excludes the calling connection; `false` allows it to be included. `type` is a raw string (`normal`/`master`/`replica`/`pubsub`) — it is not validated
  client-side. <!-- src: qbm/redis/src/qbm/redis/commands/server_commands.h:142-198 -->
- `client_pause`'s `timeout` is **milliseconds** (native unit), not a `qb::duration`. `mode` is `"ALL"` or
  `"WRITE"`. <!-- src: qbm/redis/src/qbm/redis/commands/server_commands.h:272-292 -->
- `client_unblock` replies `true` when the ID named a blocked client that is now unblocked (`:1`) and `false` when it
  named none (`:0`); a refusal is a failed reply. With `error = true` the unblocked command fails with Redis's
  `UNBLOCKED` error instead of behaving as if its timeout had expired. (The reply type was `status`, which cannot
  decode an integer: every call failed, Huly QB-652.)
  <!-- src: qbm/redis/src/qbm/redis/commands/server_commands.h:327-350 -->
- `client_tracking(true)` sends `CLIENT TRACKING ON`; `false` sends `OFF`. `client_caching`, `client_no_evict`,
  `client_no_touch` map `true`/`false` to `YES`/`NO` or `ON`/`OFF`
  respectively. <!-- src: qbm/redis/src/qbm/redis/commands/server_commands.h:312-316, 1692-1704, 1753-1786 -->
- `client_reply("ON")` waits for Redis's `+OK`. `"OFF"` and `"SKIP"` fail locally, before a byte is sent, in both
  callback and coroutine forms: Redis sends **no reply even for the mode command**, so a `Reply<status>` could not
  complete or prove server acceptance. The generic raw `command()` escape hatch does not manage no-reply mode; do not
  send these modes through it on a connection used for ordinary replies. A dedicated one-way API would need a distinct
  local-acceptance result and explicit reply-mode state. <!-- src: qbm/redis/src/qbm/redis/commands/server_commands.h:1791-1825 -->
- `client_list()` yields a `qb::json` **string** holding the raw `CLIENT LIST` text block (one client per line), not a
  parsed array — the payload is line-delimited text, not JSON, so the wrapper boxes it verbatim. Parse the lines
  yourself, or call `client_info()` for the current connection. `client_tracking_info()`, by contrast, is a RESP map and
  comes back as a JSON object. <!-- src: qbm/redis/src/qbm/redis/commands/server_commands.h:1338-1354 (client_list), qbm/redis/src/qbm/redis/reply.cpp:558-597 (qb::json string boxing) -->

```cpp
// <!-- src: qbm/redis/tests/integration/server/server-admin-commands.cpp:93-117,145-153 -->
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
// <!-- src: qbm/redis/src/qbm/redis/redis.h:2028-2069 -->
// The callback receives +OK, each command line, and one terminal failure.
redis.monitor([](qb::redis::Reply<std::string> &&line) {
    if (line.ok())
        qb::io::cout() << "MONITOR " << line.result() << std::endl;
    else
        qb::io::cout() << "MONITOR ended: " << line.error() << std::endl;
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
array into pairs for you. <!-- src: qbm/redis/src/qbm/redis/commands/server_commands.h:362-391 -->

```cpp
// <!-- src: qbm/redis/tests/integration/server/server-admin-commands.cpp:231-274 -->
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
| `COMMAND INFO`                      | `command_info(const std::vector<std::string> &command_names = {})`                               | `qb::json` (one object or `null` per name)        |
| `COMMAND COUNT`                     | `command_count()`                                                                                | `long long`                                       |
| `COMMAND GETKEYS`                   | `command_getkeys(const std::string &command, const std::vector<std::string> &args)`              | `std::vector<std::string>`                        |
| `COMMAND GETKEYSANDFLAGS`           | `command_getkeysandflags(const std::string &command_name, const std::vector<std::string> &args)` | `qb::json`                                        |
| `COMMAND DOCS`                      | `command_docs(const std::vector<std::string> &command_names = {})`                               | `qb::json`                                        |
| `COMMAND LIST`                      | `command_list(const std::vector<std::string> &filter = {})`                                      | `std::vector<std::string>`                        |
| `INFO commandstats`                | `command_stats()`                                                                                | `qb::json`                                        |

See the [`COMMAND` named-overload note](#commands-named-overload-actually-issues-command-info) above: `command(names)`
with a non-empty vector returns `COMMAND INFO` data, its entries the server's positional arrays through the generic JSON
conversion; `command_info(names)` returns the same entries with their positions named.

`command_stats()` reads `INFO commandstats` and returns a JSON object with the exact native `cmdstat_*`
keys. Each value is the complete text after the first colon, for example
`"calls=7,usec=13,usec_per_call=1.857,rejected_calls=0,failed_calls=0"`. Header and empty lines are omitted;
unknown future counter fields remain in the text. An empty section yields an empty object. Native command
errors and local decode errors retain their original error and raw reply. <!-- src: qbm/redis/src/qbm/redis/commands/server_commands.h:637-643 -->

```cpp
// <!-- src: qbm/redis/tests/integration/server/server-admin-commands.cpp:338-378,381-410 -->
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
bytes; a key that does not exist answers nil, delivered as `0` with `ok() == true` and kept in `raw()` (Huly QB-655). <!-- src: qbm/redis/src/qbm/redis/commands/server_commands.h:815-833 -->

```cpp
// <!-- src: qbm/redis/tests/integration/server/server-introspection.cpp:226-262 -->
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
one. <!-- src: qbm/redis/src/qbm/redis/commands/server_commands.h:1423-1444 -->

### Slowlog

| Command         | Coroutine signature                 | Reply `T`   |
|-----------------|-------------------------------------|-------------|
| `SLOWLOG GET`   | `slowlog_get(long long count = 10)` | `qb::json`  |
| `SLOWLOG LEN`   | `slowlog_len()`                     | `long long` |
| `SLOWLOG RESET` | `slowlog_reset()`                   | `status`    |

```cpp
// <!-- src: qbm/redis/tests/integration/server/server-introspection.cpp:159-220 -->
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
`qb::duration`](https://github.com/isndev/qb/blob/main/src/qb/system/time.h); any `std::chrono` duration converts implicitly.
`debug_segfault` **crashes the server** — it exists for fault-injection testing only.

```cpp
// <!-- src: qbm/redis/tests/integration/server/server-admin-commands.cpp:420-452 -->
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
save. <!-- src: qbm/redis/src/qbm/redis/commands/server_commands.h:1047-1153 -->

```cpp
// <!-- src: qbm/redis/tests/integration/server/server-introspection.cpp:338-363 -->
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
one. <!-- src: qbm/redis/src/qbm/redis/commands/server_commands.h:1191-1241 -->

```cpp
// <!-- src: qbm/redis/tests/integration/server/server-introspection.cpp:267-306 -->
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

`role()` preserves the native positional array in RESP2 and RESP3: `["master", offset, replicas]`,
`["slave", host, port, state, offset]`, or `["sentinel", names]`. Offsets remain integers and names remain
opaque strings, including names whose bytes look like JSON. The command does not apply the generic flat-map
heuristic to these arrays. <!-- src: qbm/redis/src/qbm/redis/commands/server_commands.h:867-873 -->

`info()` returns the textual `INFO` payload boxed as a `qb::json` **string** (`result().is_string()` is `true`) —
like `CLIENT LIST`. Use `result().get<std::string>()` and parse the lines yourself; pass a section name (`"memory"`,
`"replication"`, ...) to narrow the text through `info()`. <!-- src: qbm/redis/src/qbm/redis/commands/server_commands.h:1258-1279 (info), qbm/redis/src/qbm/redis/reply.cpp:558-597 (qb::json string boxing) -->
`time()`'s two overloads return **different** types — see the [
`TIME` note](#time-reshapes-its-reply-in-the-coroutine-form-only).

```cpp
// <!-- src: qbm/redis/src/qbm/redis/commands/server_commands.h:1258-1279 (info), :1292-1315 (time coroutine) -->
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
directly. <!-- src: qbm/redis/src/qbm/redis/commands/server_commands.h:921-939 (slaveof always emits host+port), :995 (sync), :1021 (psync), :1608 (failover), :887-908 (shutdown) -->

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
  `std::vector<std::string>`). A numeric conversion error reported by `std::from_chars` becomes `ok() == false`
  with `error() == "Malformed TIME reply"`; this is not a check of exact reply arity or trailing numeric bytes.
- **`monitor` is a callback stream.** It has no awaiter overload; use a dedicated connection and end it with `reset()`,
  `quit()` or `disconnect()`. The callback receives one terminal failure, and the stream is never replayed on reconnect.
- **Destructive admin has no guard rails.** `flushall`, `flushdb`, `shutdown`, `debug_segfault`, `config_set`, and the
  replication commands all do exactly what they say with no confirmation. Most wrappers do not validate subcommand
  strings (`client_kill` type, `shutdown` save-option) — invalid values fail only on the server. The typed
  `client_reply` wrapper rejects no-reply modes locally, as explained above.
  Restrict these with ACLs (see [acl_commands.md](./acl_commands.md)).
- **`memory_info` is not reachable here.** The header carries a private INFO-to-`memory_info` parser and a `memory_info`
  struct, but no public command in this group exposes them — `info()` returns `qb::json`. Do not write code against
  `memory_info` expecting `info()` to populate it. <!-- src: qbm/redis/src/qbm/redis/commands/server_commands.h:56-57, qbm/redis/src/qbm/redis/types.h:437 -->

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
