# Module commands reference

> **Audience:** Adopter · **Status:** stable · **Verified-against:** qbm-redis @ qb 2.0.0 (C++20 default, C++23 supported)

Reference for the module command group — `MODULE LIST`, `MODULE LOAD`, `MODULE UNLOAD`, and `MODULE HELP` — exposed by every connected `qb::redis` client through the `module_commands<Derived>` CRTP mixin.

**Prerequisites:** [connection.md](./connection.md) (open a connection, `qb_load_modules`, `qbm::redis`), [commands_overview.md](./commands_overview.md) (the coroutine/callback dual API and `Reply<T>`) — **See also:** [function_commands.md](./function_commands.md), [server_commands.md](./server_commands.md), [error_handling.md](./error_handling.md)

---

## Summary

These four commands manage Redis *modules* — native shared libraries (`.so` files) that extend the server with new commands and data types. They let you list the modules currently loaded, load a module from a path, unload one by name, and fetch the server's built-in help text. They are defined in `module_commands.h` as a CRTP mixin that `qb::redis::detail::Redis<QB_IO_>` (and therefore `qb::redis::tcp::client`) inherits, so a connected client calls them as ordinary member functions.

Every command ships in two forms:

- a **coroutine** overload that returns a `redis_awaiter` you `co_await` (the idiomatic form), and
- a **callback** overload, selected when the first argument is invocable with the matching `Reply<T> &&`, that returns `Derived &` for chaining.

```cpp
#include <redis/redis.h>           // namespace qb::redis
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>

qb::io::async::task<void> example(qb::redis::tcp::client &redis) {
    auto reply = co_await redis.module_list();     // Reply<qb::json>
    if (reply.ok() && reply.result().is_array())
        qb::io::cout() << reply.result().size() << " module(s) loaded" << std::endl;
}
```
<!-- src: derived from qbm/redis/tests/test-module-commands.cpp:41-80 -->

This command group carries **no time arguments** — none of its parameters are durations, so the seconds-versus-milliseconds boundary documented for key-expiry commands does not apply here. Connect and command *deadlines* (which are `qb::duration`) live on the client itself; see [connection.md](./connection.md).

> `MODULE LOAD` **executes server-side native code.** A loaded module runs in the server's address space with no sandbox — it can register commands, allocate memory, and crash or compromise the server. Treat `module_load` like any other privileged admin operation: the module is a thin RESP wrapper and performs no authorization of its own; gate it behind Redis ACLs and trusted input. (`module_commands.h:85`; FACTBOOK invariant on destructive admin commands.)

> The client is **not thread-safe.** Issue module commands from a single I/O thread / strand, like every other command.

---

## Concepts

### Where these methods come from

`module_commands<Derived>` is a header-only CRTP mixin (`module_commands.h:34`). It is one of the bases of `qb::redis::detail::Redis<QB_IO_>` (`redis.h:613`), so `qb::redis::tcp::client` and `qb::redis::tcp::ssl::client` expose these four methods directly. The pub/sub consumers (`tcp::cb_consumer` / `tcp::co_consumer`) do **not** — they inherit only `connection_commands` and `subscription_commands`. Each method forwards to the inherited command machinery:

- the coroutine overload calls `derived().make_coro_command<T>(...)`, returning a `redis_awaiter<T>` that resolves to `Reply<T>`;
- the callback overload calls `derived().command<T>(func, "MODULE", "SUBCMD", args...)` and returns `Derived &`.

The reply type `T` differs per command (`qb::json`, `status`, `std::vector<std::string>`); the table below lists each.

### Reading a `Reply<T>`

Every command resolves to `qb::redis::Reply<T>` (`reply.h:1052`):

- `reply.ok()` / `explicit operator bool` — `true` when the command round-tripped successfully.
- `reply.result()` (or its alias `reply.value()`) — the typed payload `T`.
- `reply.error()` — the server or transport error string when `!ok()`.

A rejected command does **not** throw from the coroutine path and does **not** close the connection — it resolves with `ok() == false` and the message in `error()`. The tests rely on exactly this: loading an empty path or unloading a missing module yields `reply.ok() == false`, never an exception (`test-module-commands.cpp:88-96`, `:112-120`). See [error_handling.md](./error_handling.md) for the typed error hierarchy.

> Module commands are **optional server features.** A server built without the modules API answers with an `unknown command 'MODULE'` error, surfaced as `!reply.ok()`. Probe before you depend on it, and handle the not-supported case explicitly.

### The `status` reply type

`module_load` and `module_unload` reply with `qb::redis::status` (`types.h:334`), a thin wrapper over the server's simple-string reply. `status::ok()` and its `operator bool` are `true` only when the string is exactly `"OK"`. So with `Reply<status>` there are two layers to check — `reply.ok()` (did the command round-trip) and `reply.result().ok()` (was the status `"OK"`):

```cpp
auto r = co_await redis.module_load("/opt/redis/modules/rejson.so");
if (r.ok() && r.result().ok()) {
    // module loaded
}
```

### The `qb::json` reply type

`module_list` replies with `qb::json` (which is `nlohmann::json`; `qb/json.h:104`) — an array of objects, one per loaded module. The reply is the server's own `MODULE LIST` payload passed through verbatim, so the available fields are server-version-dependent. The only field the test asserts is `name`, a string (`test-module-commands.cpp:57-58`); a typical server also reports a numeric version under `ver`. Read it with the standard `nlohmann::json`-style API (`is_array()`, `contains()`, `operator[]`, `get<T>()`), and probe with `contains()` before reading any field other than `name`.

---

## Command reference

The signatures below are copied verbatim from `module_commands.h`. For each command the coroutine overload is listed first, then the callback overload. `Func` is constrained with `std::enable_if_t<std::is_invocable_v<Func, Reply<T> &&>, Derived &>`, so the callback overload is only viable when your callable accepts the matching `Reply<T> &&`.

| Command | Method | Reply type | Purpose |
| --- | --- | --- | --- |
| `MODULE LIST`   | `module_list`   | `Reply<qb::json>`                  | Enumerate loaded modules (array of `{name, ver, ...}`) |
| `MODULE LOAD`   | `module_load`   | `Reply<status>`                    | Load a module from a `.so` path, with optional args |
| `MODULE UNLOAD` | `module_unload` | `Reply<status>`                    | Unload a module by registered name |
| `MODULE HELP`   | `module_help`   | `Reply<std::vector<std::string>>`  | Fetch the server's help text for `MODULE` |

### `module_list` — enumerate loaded modules

```cpp
// Coroutine — module_commands.h:52
auto module_list();                                // -> redis_awaiter yielding Reply<qb::json>

// Callback — module_commands.h:67
template <typename Func>
std::enable_if_t<std::is_invocable_v<Func, Reply<qb::json> &&>, Derived &>
module_list(Func &&func);
```

Returns an array of objects describing every module loaded in the server. The test asserts each entry has a `name` field of string type (`test-module-commands.cpp:57-58`); the remaining fields are the server's own `MODULE LIST` payload and vary by version (a numeric version is typically reported under `ver`). The array is empty (but still an array) when no modules are loaded.

```cpp
auto reply = co_await redis.module_list();
if (reply.ok()) {
    const auto &modules = reply.result();          // qb::json
    if (modules.is_array()) {
        for (const auto &module : modules) {
            if (module.contains("name"))
                qb::io::cout() << module["name"].get<std::string>() << std::endl;
        }
    }
}
```
<!-- src: qbm/redis/tests/test-module-commands.cpp:46-66 -->

Callback form:

```cpp
redis.module_list([](qb::redis::Reply<qb::json> &&reply) {
    if (reply.ok() && reply.result().is_array())
        qb::io::cout() << reply.result().size() << " module(s)" << std::endl;
});
```
<!-- src: derived from module_commands.h:67-71 -->

### `module_load` — load a module

```cpp
// Coroutine — module_commands.h:85
template <typename... Args>
auto module_load(const std::string &path, Args&&... args);
                                                   // -> redis_awaiter yielding Reply<status>

// Callback — module_commands.h:102
template <typename Func, typename... Args>
std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
module_load(Func &&func, const std::string &path, Args&&... args);
```

Loads the shared library at `path` into the server, optionally passing module-specific `args` (each forwarded verbatim as a RESP argument). The variadic `Args...` map to the module's own configuration or positional options; pass them as strings or any type the serializer accepts.

A successful load replies `status == "OK"`. An invalid path, a missing file, or a module that refuses to initialize replies with an error — surfaced as `!reply.ok()`, **not** an exception. The test passes an empty path and asserts the failure path (`test-module-commands.cpp:88-96`):

```cpp
auto reply = co_await redis.module_load("");       // empty path -> error reply
if (!reply.ok()) {
    std::string error{reply.error()};              // e.g. "ERR Error loading module..."
    // handle the failure; the connection stays open
}
```
<!-- src: qbm/redis/tests/test-module-commands.cpp:88-96 -->

With a real module and arguments:

```cpp
auto r = co_await redis.module_load("/opt/redis/modules/rejson.so");
if (r.ok() && r.result().ok())
    qb::io::cout() << "rejson loaded" << std::endl;

// passing module arguments
auto r2 = co_await redis.module_load(
    "/opt/redis/modules/redisearch.so", "MAXSEARCHRESULTS", "100000");
```
<!-- src: derived from module_commands.h:85-91 -->

> `module_load` runs untrusted native code in the server process. See the security note above.

### `module_unload` — unload a module

```cpp
// Coroutine — module_commands.h:117
auto module_unload(const std::string &name);       // -> redis_awaiter yielding Reply<status>

// Callback — module_commands.h:133
template <typename Func>
std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
module_unload(Func &&func, const std::string &name);
```

Unloads the module registered under `name` (the `name` field reported by `module_list`, **not** the file path), removing all commands and data types it registered. Unloading a module that is not loaded — or one the server refuses to unload — replies with an error surfaced as `!reply.ok()`:

```cpp
auto reply = co_await redis.module_unload("nonexistent_module");
if (!reply.ok()) {
    std::string error{reply.error()};              // e.g. "ERR Error unloading module: no such module..."
    // handle the failure; the connection stays open
}
```
<!-- src: qbm/redis/tests/test-module-commands.cpp:112-120 -->

Successful unload:

```cpp
auto r = co_await redis.module_unload("ReJSON");
if (r.ok() && r.result().ok())
    qb::io::cout() << "ReJSON unloaded" << std::endl;
```
<!-- src: derived from module_commands.h:117-123 -->

### `module_help` — fetch help text

```cpp
// Coroutine — module_commands.h:147
auto module_help();                                // -> redis_awaiter yielding Reply<std::vector<std::string>>

// Callback — module_commands.h:162
template <typename Func>
std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>, Derived &>
module_help(Func &&func);
```

Returns the server's built-in help text for the `MODULE` command family as a vector of lines (one string per line). Useful for tooling and diagnostics; the exact lines are server-version-dependent.

```cpp
auto reply = co_await redis.module_help();
if (reply.ok()) {
    for (const auto &line : reply.result())        // std::vector<std::string>
        qb::io::cout() << line << std::endl;
}
```
<!-- src: qbm/redis/tests/test-module-commands.cpp:136-147 -->

---

## Time-unit boundary

**None.** No method in this group takes a duration argument. The seconds-versus-milliseconds split that applies to key-expiry commands (`EXPIRE`/`PEXPIRE`) is irrelevant here. The only durations involved are the client-wide connect and command deadlines, which are `qb::duration` and configured on the client, not on these calls (see [connection.md](./connection.md)).

---

## Pitfalls

- **`MODULE` may be unsupported.** A server compiled without the modules API rejects the whole family with an `unknown command 'MODULE'` error. That surfaces as `!reply.ok()` (or, on the rare path where the driver raises, a caught `std::exception`); always handle the not-supported branch, as the tests do (`test-module-commands.cpp:64-72`).
- **Two-layer success check for `status` replies.** For `module_load`/`module_unload`, `reply.ok()` only tells you the command round-tripped. You must also check `reply.result().ok()` to confirm the server returned `"OK"`. A loaded-but-init-failed module can round-trip with a non-`"OK"` status.
- **`module_unload` takes the module *name*, not the path.** Use the `name` field from `module_list`. Passing the `.so` path will not match.
- **Failures are reply errors, not exceptions.** On the coroutine path, an empty path, missing file, or missing module resolves with `ok() == false`; it does not throw and does not close the connection. Branch on `reply.ok()` rather than wrapping in `try/catch` (`test-module-commands.cpp:88-96`, `:112-120`).
- **`module_load` is privileged.** It executes native code in the server. Restrict it via ACLs and never feed it untrusted paths or arguments.
- **No protocol-mode difference.** These commands behave identically under RESP2 and RESP3; the module test suite runs every case in both modes (`INSTANTIATE_PROTOCOL_MODES`, `test-module-commands.cpp:34`).

---

## See also

- [function_commands.md](./function_commands.md) — `FUNCTION LOAD`/`FCALL`, the Lua-based server-side extension mechanism (the modern alternative to native modules).
- [server_commands.md](./server_commands.md) — server administration and introspection (`INFO`, `CONFIG`, `DEBUG`).
- [commands_overview.md](./commands_overview.md) — the coroutine/callback dual API, `Reply<T>`, and the full mixin stack.
- [error_handling.md](./error_handling.md) — the typed error hierarchy and how rejected commands surface.
- [connection.md](./connection.md) — opening a connection, `qb_load_modules`, linking `qbm::redis`, and the `qb::duration` connect/command deadlines.

<!-- Verified against: qbm/redis/module_commands.h, qbm/redis/types.h:334 (status), qbm/redis/reply.h:1052 (Reply), qbm/redis/tests/test-module-commands.cpp; FACTBOOK.json module_commands @ qb 2.0.0 -->
