# ACL commands

> **Audience:** Adopter · **Status:** stable · **Verified-against:** qbm-redis @ qb 3.0.0 (C++20 default, C++23
> supported)

Reference for the Access Control List command group — the `ACL` subcommands that create, modify, inspect, and delete
Redis users, list command categories, audit denied commands, and generate passwords.

**Prerequisites:** [../README.md](../README.md) (install, `qb_load_modules`,
`qbm::redis`), [connection.md](./connection.md), [commands_overview.md](./commands_overview.md) (the `Reply<T>` model,
`status`, coroutine vs. callback forms) — **See also:** [server_commands.md](./server_commands.md) (`CONFIG`, `CLIENT`,
server administration), [connection.md](./connection.md) (`AUTH`, which authenticates as an ACL
user), [error_handling.md](./error_handling.md)

---

## Summary

Redis ACLs are the server's per-user authorization model: each user has a set of rules that decide which commands it may
run, which keys it may touch, and which pub/sub channels it may use. The `acl_commands<Derived>` mixin wraps the `ACL`
command family so you can administer those users from a `qb::redis` client — `ACL SETUSER` to create or amend a user,
`ACL GETUSER`/`ACL LIST`/`ACL USERS` to read the current state, `ACL DELUSER` to remove users, `ACL CAT` to enumerate
command categories, `ACL LOG` to audit denials, `ACL DRYRUN` to test a permission before granting it, and `ACL GENPASS`/
`ACL WHOAMI`/`ACL HELP`/`ACL LOAD`/`ACL SAVE` for the remaining housekeeping.

The `acl_commands<Derived>` mixin is one of the command groups inherited by `qb::redis::tcp::client` (and
`qb::redis::tcp::ssl::client`). Every command is exposed in two forms, both fully asynchronous:

- a **coroutine** form (`auto`-returning) that yields a `Reply<T>` you `co_await`;
- a **callback** form that takes your handler first and returns `Derived&` for chaining.

There is no blocking variant — this module has never shipped "Sync" signatures. None of these commands carry a
`qb::duration`: ACL is a configuration surface with no client-side timeout argument and no time-valued reply. The
`qb::duration` / native-unit boundary documented for `EXPIRE` in [commands_overview.md](./commands_overview.md) does *
*not** apply here.

```cpp
#include <qbm/redis/redis.h>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>

// <!-- src: qbm/redis/tests/integration/admin/acl-commands.cpp:130-163 -->
qb::io::async::task<void> who_am_i(qb::redis::tcp::client &redis) {
    auto reply = co_await redis.acl_whoami();             // Reply<std::string>
    if (reply)                                            // Reply<T> is contextually bool (== ok())
        qb::io::cout() << "connected as " << reply.result() << std::endl;  // e.g. "default"
}
```

---

## Concepts

### Reply types by command

ACL replies do not share a single shape. Three reply payloads cover the whole group, and the right one is wired in per
command — you never name it yourself except when you reach for the generic `command<T>(...)` escape hatch.

| Command method | Wire command         | `Reply<T>` payload                                |
|----------------|----------------------|---------------------------------------------------|
| `acl_list`     | `ACL LIST`           | `qb::json` (array of rule lines)                  |
| `acl_getuser`  | `ACL GETUSER`        | `qb::json` (object)                               |
| `acl_log`      | `ACL LOG [count]`    | `qb::json` (array of entries)                     |
| `acl_dryrun`   | `ACL DRYRUN`         | `qb::json` (`"OK"` string, or the denial message) |
| `acl_cat`      | `ACL CAT [category]` | `std::vector<std::string>`                        |
| `acl_users`    | `ACL USERS`          | `std::vector<std::string>`                        |
| `acl_help`     | `ACL HELP`           | `std::vector<std::string>`                        |
| `acl_whoami`   | `ACL WHOAMI`         | `std::string`                                     |
| `acl_genpass`  | `ACL GENPASS [bits]` | `std::string`                                     |
| `acl_deluser`  | `ACL DELUSER`        | `long long` (users deleted)                       |
| `acl_setuser`  | `ACL SETUSER`        | `status`                                          |
| `acl_load`     | `ACL LOAD`           | `status`                                          |
| `acl_save`     | `ACL SAVE`           | `status`                                          |

A `status` reply (`qb::redis::status`, [types.h:476](../src/qbm/redis/types.h)) wraps the server's simple-string acknowledgement; it
is truthy when the server answered `OK`. `Reply<status>` itself is truthy when the command did not error, so check both
layers if you need to distinguish a transport error from a non-`OK` server answer —
see [error_handling.md](./error_handling.md).

<!-- src: qbm/redis/src/qbm/redis/reply.h:1125-1127 (the Reply bool conversion), types.h:476 (status) -->

### Structured replies decode to `qb::json`

`ACL LIST`, `ACL GETUSER`, `ACL LOG`, and `ACL DRYRUN` return structured data that the module decodes into `qb::json`
rather than flattening to strings. Inspect the value with the usual `qb::json` accessors — `is_array()`, `is_object()`,
`contains(key)`, `operator[]`, and `get<T>()`:

```cpp
// <!-- src: qbm/redis/tests/integration/admin/acl-commands.cpp:107-127 -->
auto reply = co_await redis.acl_getuser("default");      // Reply<qb::json>
if (reply) {
    auto user = reply.result();
    if (user.is_object() && user.contains("flags")) {
        for (const auto &flag : user["flags"])
            if (flag.get<std::string>() == "on") { /* user is enabled */ }
    }
}
```

`qb::json` is the framework JSON type (defined outside this module and used by `reply.h`); this page does not restate
its API.

### `ACL SETUSER` forwards rule tokens verbatim

`acl_setuser` is variadic. The `username` is the first argument; every argument after it is appended to the command as
one ACL rule token — `on`/`off`, a password directive such as `>password` or `#<sha256>`, a key pattern like `~user:*`
or `allkeys`, a channel pattern like `&channel:*`, and command grants like `+@all`, `+get`, or `-@dangerous`. The module
does not parse or validate these tokens; it forwards them to the server, which is the single source of truth for ACL
grammar. A malformed rule is rejected by Redis, surfacing as a non-`OK` `status` (or an error on the `Reply`), not at
compile time.

<!-- src: qbm/redis/src/qbm/redis/commands/acl_commands.h:375-396 -->

### `ACL DRYRUN` tests a grant without applying it

`acl_dryrun(username, command, args)` asks the server whether `username` *would* be permitted to run `command` with
`args`, without executing it. The reply decodes to `qb::json`: the string `"OK"` when the user would be allowed, or an
explanation when it would be denied (a string on most servers, occasionally a structured object on newer ones — test for
both, as the test suite does). Use it to validate a rule set before you rely on it.

<!-- src: qbm/redis/tests/integration/admin/acl-commands.cpp:235-267 -->

### Availability

The whole group requires a server with ACL support (Redis 6.0+); `ACL DRYRUN` requires 7.0+. On an older server the
command comes back as an `unknown command` error rather than a parse failure — handle it the same way you handle any
per-command error (the test suite guards each call in a `try`/`catch` for exactly this reason).

---

## Commands

All signatures below are the public methods of `acl_commands<Derived>` (header `qbm/redis/src/qbm/redis/commands/acl_commands.h`). The callback
overloads are SFINAE-gated on `std::is_invocable_v<Func, Reply<T>&&>` for that command's `T`; a handler with the wrong
`Reply<T>` signature drops out of overload resolution, so a mismatch fails to compile rather than misbehaving at
runtime. The callback handler is invoked with an rvalue `Reply<T>&&`.

### `acl_setuser` — create or modify a user

```cpp
// coroutine: yields Reply<status>
template <typename... Args>
auto acl_setuser(const std::string &username, Args &&...rules);

// callback: returns Derived& for chaining
template <typename Func, typename... Args>
Derived &acl_setuser(Func &&func, const std::string &username, Args &&...rules);
```

<!-- src: qbm/redis/src/qbm/redis/commands/acl_commands.h:375-396 -->

Creates `username` if it does not exist, then applies each rule token in order. Returns `status` (`OK` on success).

```cpp
// coroutine — <!-- src: qbm/redis/src/qbm/redis/commands/acl_commands.h:377 -->
auto set = co_await redis.acl_setuser(
    "alice", "on", ">s3cr3t", "~app:*", "+@read", "+@write", "-@dangerous");
if (set && set.result())             // Reply<status> ok AND status == "OK"
    qb::io::cout() << "alice configured" << std::endl;
```

```cpp
// callback
redis.acl_setuser(
    [](qb::redis::Reply<qb::redis::status> &&reply) {
        if (reply && reply.result()) { /* user is live */ }
    },
    "alice", "on", ">s3cr3t", "~app:*", "+@read");
```

### `acl_getuser` — read one user's rules

```cpp
// coroutine: yields Reply<qb::json>
auto acl_getuser(const std::string &username);

// callback: returns Derived&
template <typename Func>
Derived &acl_getuser(Func &&func, const std::string &username);
```

<!-- src: qbm/redis/src/qbm/redis/commands/acl_commands.h:152-170 -->

Returns a `qb::json` object describing `username` — its `flags`, `passwords`, `commands`, `keys`, and `channels`.

```cpp
// coroutine — <!-- src: qbm/redis/tests/integration/admin/acl-commands.cpp:107-127 -->
auto reply = co_await redis.acl_getuser("default");
if (reply && reply.result().is_object())
    qb::io::cout() << reply.result().dump() << std::endl;
```

### `acl_list` — list every rule line

```cpp
// coroutine: yields Reply<qb::json>
auto acl_list();

// callback: returns Derived&
template <typename Func>
Derived &acl_list(Func &&func);
```

<!-- src: qbm/redis/src/qbm/redis/commands/acl_commands.h:51-70 -->

Returns a `qb::json` array, one entry per user, each a full ACL rule line (e.g. `"user default on nopass ~* &* +@all"`).

```cpp
// coroutine — <!-- src: qbm/redis/tests/integration/admin/acl-commands.cpp:130-163 -->
auto reply = co_await redis.acl_list();
if (reply && reply.result().is_array())
    for (const auto &line : reply.result())
        qb::io::cout() << line.get<std::string>() << std::endl;
```

### `acl_users` — list user names

```cpp
// coroutine: yields Reply<std::vector<std::string>>
auto acl_users();

// callback: returns Derived&
template <typename Func>
Derived &acl_users(Func &&func);
```

<!-- src: qbm/redis/src/qbm/redis/commands/acl_commands.h:180-197 -->

Returns the configured user names. `default` is always present.

```cpp
// coroutine — <!-- src: qbm/redis/tests/integration/admin/acl-commands.cpp:130-163 -->
auto reply = co_await redis.acl_users();
if (reply)
    for (const auto &user : reply.result())
        qb::io::cout() << user << std::endl;
```

### `acl_deluser` — delete a user

```cpp
// coroutine: yields Reply<long long>
auto acl_deluser(const std::string &username);

// callback: returns Derived&
template <typename Func>
Derived &acl_deluser(Func &&func, const std::string &username);
```

<!-- src: qbm/redis/src/qbm/redis/commands/acl_commands.h:260-278 -->

Removes `username` and terminates its open connections. Returns the number of users actually deleted (`0` if the name
did not exist). This overload deletes a single user; to delete several in one round trip, drop to the generic escape
hatch: `redis.command<long long>(cb, "ACL", "DELUSER", "u1", "u2", "u3")`.

```cpp
// coroutine
auto reply = co_await redis.acl_deluser("alice");
if (reply && reply.result() == 1) { /* deleted */ }
```

```cpp
// callback
redis.acl_deluser(
    [](qb::redis::Reply<long long> &&reply) {
        if (reply) { /* reply.result() users removed */ }
    },
    "alice");
```

### `acl_whoami` — current user name

```cpp
// coroutine: yields Reply<std::string>
auto acl_whoami();

// callback: returns Derived&
template <typename Func>
Derived &acl_whoami(Func &&func);
```

<!-- src: qbm/redis/src/qbm/redis/commands/acl_commands.h:207-223 -->

Returns the user name this connection is authenticated as (`default` until you `AUTH` as someone else;
see [connection.md](./connection.md)).

```cpp
// coroutine — <!-- src: qbm/redis/tests/integration/admin/acl-commands.cpp:130-163 -->
auto reply = co_await redis.acl_whoami();
if (reply)
    qb::io::cout() << reply.result() << std::endl;   // "default"
```

### `acl_cat` — list categories or commands in a category

```cpp
// coroutine: yields Reply<std::vector<std::string>>
auto acl_cat(const std::string &category = "");

// callback: returns Derived&
template <typename Func>
Derived &acl_cat(Func &&func, const std::string &category = "");
```

<!-- src: qbm/redis/src/qbm/redis/commands/acl_commands.h:118-134 -->

With no argument, returns every ACL command category (`string`, `keyspace`, `read`, `write`, `dangerous`, …). With a
`category`, returns the command names in it. The module decodes both forms to `std::vector<std::string>`; if you want
the raw structured form a newer server may return, reach for `redis.command<qb::json>("ACL", "CAT", category)`.

```cpp
// coroutine — <!-- src: qbm/redis/tests/integration/admin/acl-commands.cpp:61-104 -->
auto reply = co_await redis.acl_cat();                 // all categories
if (reply)
    for (const auto &cat : reply.result())
        qb::io::cout() << cat << std::endl;            // "string", "keyspace", ...

// commands in one category
auto commands = co_await redis.acl_cat("string");      // Reply<std::vector<std::string>>
```

```cpp
// callback
redis.acl_cat(
    [](qb::redis::Reply<std::vector<std::string>> &&reply) {
        if (reply) { /* reply.result() holds the category names */ }
    });
```

### `acl_log` — audit denied commands

```cpp
// coroutine: yields Reply<qb::json>
auto acl_log(std::optional<long long> count = std::nullopt);

// callback: returns Derived&
template <typename Func>
Derived &acl_log(Func &&func, std::optional<long long> count = std::nullopt);
```

<!-- src: qbm/redis/src/qbm/redis/commands/acl_commands.h:83-109 -->

Returns the security log — a `qb::json` array of recent ACL denials, each entry naming the offending command, the user,
the client address, and a reason. Pass `count` to cap the number of entries. To clear the log, use the generic form
`redis.command<qb::json>("ACL", "LOG", "RESET")`.

```cpp
// coroutine — <!-- src: qbm/redis/tests/integration/admin/acl-commands.cpp:166-183 -->
auto all     = co_await redis.acl_log();      // entire log
auto last_5  = co_await redis.acl_log(5);     // at most 5 entries
if (last_5 && last_5.result().is_array())
    for (const auto &entry : last_5.result()) { /* inspect entry */ }
```

### `acl_genpass` — generate a secure password

```cpp
// coroutine: yields Reply<std::string>
auto acl_genpass(std::optional<long long> bits = std::nullopt);

// callback: returns Derived&
template <typename Func>
Derived &acl_genpass(Func &&func, std::optional<long long> bits = std::nullopt);
```

<!-- src: qbm/redis/src/qbm/redis/commands/acl_commands.h:289-310 -->

Asks the server to generate a cryptographically secure random password, returned as a hex string. With no argument the
server uses its default entropy (256 bits); pass `bits` to request a specific strength.

```cpp
// coroutine — <!-- src: qbm/redis/tests/integration/admin/acl-commands.cpp:209-229 -->
auto pw      = co_await redis.acl_genpass();      // default entropy
auto pw_128  = co_await redis.acl_genpass(128);   // 128 bits
if (pw)
    qb::io::cout() << pw.result() << std::endl;
```

### `acl_dryrun` — test a permission without running it

```cpp
// coroutine: yields Reply<qb::json>
auto acl_dryrun(const std::string &username, const std::string &command,
                const std::vector<std::string> &args = {});

// callback: returns Derived&
template <typename Func>
Derived &acl_dryrun(Func &&func, const std::string &username,
                    const std::string &command,
                    const std::vector<std::string> &args = {});
```

<!-- src: qbm/redis/src/qbm/redis/commands/acl_commands.h:409-429 -->

Returns `"OK"` (as a `qb::json` string) if `username` would be allowed to run `command` with `args`, or a denial message
otherwise. The command is never executed. Requires Redis 7.0+.

```cpp
// coroutine — <!-- src: qbm/redis/tests/integration/admin/acl-commands.cpp:235-267 -->
auto reply = co_await redis.acl_dryrun("default", "GET", {"nonexistent_key"});
// "OK" (string) when allowed; a denial string — or, on some server versions,
// an object — when not. Mirror the test and accept both.
if (reply && (reply.result().is_string() || reply.result().is_object()))
    qb::io::cout() << reply.result().dump() << std::endl;  // "OK" or denial
```

### `acl_help` — built-in help text

```cpp
// coroutine: yields Reply<std::vector<std::string>>
auto acl_help();

// callback: returns Derived&
template <typename Func>
Derived &acl_help(Func &&func);
```

<!-- src: qbm/redis/src/qbm/redis/commands/acl_commands.h:233-249 -->

Returns the server's `ACL` help lines as a string vector.

```cpp
// coroutine — <!-- src: qbm/redis/tests/integration/admin/acl-commands.cpp:186-206 -->
auto reply = co_await redis.acl_help();
if (reply)
    for (const auto &line : reply.result())
        qb::io::cout() << line << std::endl;
```

### `acl_load` and `acl_save` — sync with the ACL file

```cpp
// coroutine: yields Reply<status>
auto acl_load();
auto acl_save();

// callback: returns Derived&
template <typename Func> Derived &acl_load(Func &&func);
template <typename Func> Derived &acl_save(Func &&func);
```

<!-- src: qbm/redis/src/qbm/redis/commands/acl_commands.h:321-363 -->

`acl_load` reloads the rule set from the server's configured ACL file, discarding in-memory changes; `acl_save` writes
the current in-memory rules back to that file. Both return `status` (`OK`). Both require the server to be configured
with an external ACL file (`aclfile` in `redis.conf`); without one, the server returns an error rather than `OK`.

```cpp
// coroutine
auto saved = co_await redis.acl_save();
if (saved && saved.result()) { /* rules persisted to disk */ }
```

---

## Pitfalls

- **`Reply<status>` truthiness is two layers, not one.** `if (reply)` only tells you the command did not error in
  transit; the server's `OK`/non-`OK` answer lives in `reply.result()` (a `status`, truthy on `OK`). For `acl_setuser`,
  `acl_load`, and `acl_save`, check `reply && reply.result()` to confirm the operation actually succeeded.
  See [error_handling.md](./error_handling.md).
- **`acl_setuser` rules are not validated client-side.** Tokens are forwarded verbatim; a typo like `+@reed` is accepted
  by the API and rejected by the server, surfacing as a non-`OK` `status`. Use `acl_dryrun` to confirm the resulting
  grant matches your intent.
- **`acl_deluser` takes exactly one user.** The convenience overload deletes a single name. For batch deletes, use
  `redis.command<long long>(..., "ACL", "DELUSER", u1, u2, ...)`; the return value is the count actually removed, which
  may be less than the number you passed.
- **`acl_load`/`acl_save` need an `aclfile`.** On a server with no external ACL file configured these return a server
  error, not `OK`. They are not a substitute for `CONFIG REWRITE`.
- **The group is version-gated.** ACL needs Redis 6.0+ and `acl_dryrun` needs 7.0+. On an older server you get an
  `unknown command` error on the `Reply`, not a parse exception — branch on `reply.ok()` instead of assuming the call
  lands.
- **`acl_log(count)` caps, it does not page.** There is no cursor; you always get the most recent `count` entries (or
  all of them with no argument). To clear the log, send `ACL LOG RESET` via the generic `command` escape hatch.

---

## See also

- [commands_overview.md](./commands_overview.md) — the `Reply<T>` model, `status`, the coroutine/callback duality, and
  the `qb::duration` vs. native-unit boundary.
- [connection.md](./connection.md) — `AUTH`, which authenticates the connection as an ACL user; `acl_whoami` reports the
  result.
- [server_commands.md](./server_commands.md) — `CONFIG`, `CLIENT`, and the rest of the server-administration surface.
- [error_handling.md](./error_handling.md) — distinguishing transport errors, server errors, and non-`OK` status
  replies.
- [Redis ACL documentation](https://redis.io/docs/management/security/acl/) — the authoritative reference for rule-token
  grammar.
