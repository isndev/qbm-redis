# String commands

> **Audience:** Adopter · **Status:** stable · **Verified-against:** qbm-redis @ qb 2.6.0 (C++20 default, C++23
> supported)

Reference for the `qb::redis::string_commands` group: the `SET`/`GET` families, atomic counters, multi-key reads and
writes, range and length operations, and the expiry-aware variants, each in both coroutine and callback form.

**Prerequisites:** [connection.md](./connection.md) (open a client
first), [commands_overview.md](./commands_overview.md) (Reply, callbacks vs coroutines, the time-unit boundary) — **See
also:** [key_commands.md](./key_commands.md) (TTL, EXPIRE, type), [bitmap_commands.md](./bitmap_commands.md) (the same
string keys at bit granularity), [error_handling.md](./error_handling.md)

---

## What this group is

`string_commands<Derived>` is a CRTP mixin (`<redis/string_commands.h>`) that injects the Redis string command surface
into the client. It is never instantiated on its own: the concrete client `qb::redis::detail::Redis<QB_IO_>` derives
from it (alongside the other command-group mixins), so every method below is called on a live client through its public
alias such as `qb::redis::tcp::client`. The mixin contributes only the typed command surface — argument serialization,
the reply type for each command, and the coroutine/callback split. The I/O, connection lifetime, and threading model
belong to the derived client (see [connection.md](./connection.md)).

```cpp
#include <redis/redis.h>            // umbrella header; pulls in string_commands.h

qb::redis::tcp::client redis{qb::io::uri{"tcp://127.0.0.1:6379"}};
redis.connect();                    // see connection.md
```

Every command exists in two forms, with no `_async` suffix:

- **Coroutine** — `auto reply = co_await redis.<cmd>(args...)`. The overload with no callback argument returns a
  `redis_awaiter` that yields `Reply<T>` when `co_await`-ed. Use this inside a `qb::io::async::task<>`.
- **Callback** — `redis.<cmd>([](qb::redis::Reply<T>&& r){ ... }, args...)`. The first argument is an invocable taking
  `Reply<T>&&`; it returns the derived client by reference so calls can chain. The callback overload is SFINAE-gated on
  the exact reply type — a lambda whose parameter is not `Reply<T>&&` for that command's `T` simply does not match the
  overload (it will not silently call the wrong one).

In both forms `T` is the command's reply payload. Read it through `qb::redis::Reply<T>`:

- `reply.ok()` — `true` when the server returned a non-error reply.
- `reply.result()` (alias `reply.value()`) — the decoded payload of type `T`.
- `reply.error()` — the error message string when `!reply.ok()`.

<!-- src: qbm/redis/reply.h:1058-1091 (ok/result/value/error) -->

---

## Reply types at a glance

| Reply `T`                                 | Commands                                                           |
|:------------------------------------------|:-------------------------------------------------------------------|
| `qb::redis::status`                       | `set` (no-GET), `mset`, `setex`, `psetex`                          |
| `long long`                               | `append`, `incr`, `incrby`, `decr`, `decrby`, `setrange`, `strlen` |
| `double`                                  | `incrbyfloat`                                                      |
| `bool`                                    | `setnx`, `msetnx`                                                  |
| `std::string`                             | `getrange`, `substr`, `lcs`                                        |
| `std::optional<std::string>`              | `get`, `getset`, `getdel`, `getex`                                 |
| `std::vector<std::optional<std::string>>` | `mget`                                                             |

`qb::redis::status` wraps a RESP simple string. It converts implicitly to `std::string` and to `bool`, but the `bool`/
`ok()` value is `true` only when the server string equals exactly `"OK"`; test success with `.ok()`, not a string
comparison. For `std::optional<std::string>` payloads (a key that may be absent), check `reply.ok()` first, then
`reply.result().has_value()` before dereferencing.

<!-- src: qbm/redis/types.h:334-352 (status), qbm/redis/commands/string_commands.h (per-command R) -->

---

## Time-unit boundary

Redis string commands keep **native** time units by design — this is a documented boundary, not a candidate for
`qb::duration`:

- `setex` takes **seconds** (`SETEX`). The `std::chrono::seconds` overload forwards `.count()` to the `long long` form.
- `psetex` takes **milliseconds** (`PSETEX`). The `std::chrono::milliseconds` overload forwards `.count()`.
- `set(..., long long ttl, ...)` and `set(..., std::chrono::milliseconds, ...)` send **`PX`** (milliseconds). There is
  no `std::chrono::seconds` overload of `set` (and no second-granularity `set` TTL on the wire): a
  `std::chrono::seconds` argument is *implicitly converted* to the `std::chrono::milliseconds` overload (
  `seconds → ms`), so it too sends `PX`.
- `getex` is **asymmetric**: `getex(key, long long)` emits **`EX`** (seconds), while
  `getex(key, std::chrono::milliseconds)` emits **`PX`** (milliseconds). So `getex(key, 5)` sets a 5-second TTL but
  `getex(key, 5ms)` sets a 5-millisecond TTL — the two overloads do not share a unit. Always pass a `std::chrono`
  literal to make the intent unambiguous.

The connection and command **timeouts** (`set_command_timeout`, connect timeout) and the `RetryPolicy` delays are
`qb::duration` — that is the framework time model and is separate from these on-the-wire command arguments. Do not
substitute `qb::duration` for the TTL arguments here. (The retired tokens `qb::Timestamp`, `qb::Duration`,
`qb::TimePoint`, `to_timestamp(`, and `to_time_point(` no longer exist — never use them.)

<!-- src: qbm/redis/commands/string_commands.h:627-694 (setex), 448-516 (psetex), 553-587 (set PX), 829-897 (getex EX vs PX) -->

---

## Set and read a value

### `set`

`SET key value [PX ms] [NX|XX]`. Returns `status` (`"OK"` on success). The optional `UpdateType` controls the
conditional-set flag.

```cpp
// qb::redis::UpdateType { EXIST, NOT_EXIST, ALWAYS };  // types.h:49
auto set(const std::string &key, const std::string &val,
         UpdateType type = UpdateType::ALWAYS);                          // -> Reply<status>
auto set(const std::string &key, const std::string &val, long long ttl_ms,
         UpdateType type = UpdateType::ALWAYS);                          // -> Reply<status>  (PX)
auto set(const std::string &key, const std::string &val,
         const std::chrono::milliseconds &ttl,
         UpdateType type = UpdateType::ALWAYS);                          // -> Reply<status>  (PX)
```

There is no `std::chrono::seconds` overload of `set`. Passing a `std::chrono::seconds`
argument (e.g. `set(key, val, std::chrono::seconds{30})`) compiles only because it
implicitly converts to the `std::chrono::milliseconds` parameter above, so it still
sends `PX` (`30s` → `PX 30000`). For seconds-granularity TTL on the wire, use `setex`.

- `UpdateType::NOT_EXIST` → `NX` (set only if the key is absent); `UpdateType::EXIST` → `XX` (set only if the key
  exists); `UpdateType::ALWAYS` (default) sends no flag. When the `NX`/`XX` condition fails, the server replies nil and
  `reply.ok()` is `false`.

```cpp
// Coroutine
auto r = co_await redis.set("session:42", "active");
if (r.ok()) { /* stored */ }

// With a 30-second TTL (passed as chrono so the unit is explicit)
co_await redis.set("session:42", "active", std::chrono::seconds{30});

// Conditional: acquire a lock only if the key does not exist
auto lock = co_await redis.set("lock:job", "owner-1", UpdateType::NOT_EXIST);
if (lock.ok()) { /* acquired */ }

// Callback form
redis.set([](qb::redis::Reply<qb::redis::status>&& r) {
    if (r.ok()) { /* stored */ }
}, "session:42", "active");
```

<!-- src: qbm/redis/commands/string_commands.h:518-625; tests/integration/string/string-commands.cpp:402-413 -->

> The previous version of this page documented a `set_get` method for the `SET ... GET` form. **No such method exists**
> in `string_commands.h`. To read-then-overwrite atomically, use [`getset`](#getset) (or run `GET` and `SET` as separate
> commands).

### `get`

`GET key`. Returns `std::optional<std::string>` — empty when the key is absent.

```cpp
auto get(const std::string &key);                                       // -> Reply<std::optional<std::string>>
```

```cpp
auto r = co_await redis.get("session:42");
if (r.ok() && r.result().has_value())
    std::cout << *r.result() << '\n';
else if (r.ok())
    std::cout << "key absent\n";
```

<!-- src: qbm/redis/commands/string_commands.h:147-172; tests/integration/string/string-commands.cpp:121-131 -->

### `getset`

`GETSET key value`. Atomically sets `key` to `value` and returns the previous value (`std::optional<std::string>`, empty
if the key did not exist). Redis marks `GETSET` deprecated in favor of `SET ... GET`, which this client does not expose;
`getset` remains available.

```cpp
auto getset(const std::string &key, const std::string &val);           // -> Reply<std::optional<std::string>>
```

```cpp
auto prev = co_await redis.getset("config", "v2");
if (prev.ok() && prev.result().has_value())
    std::cout << "old config: " << *prev.result() << '\n';
```

<!-- src: qbm/redis/commands/string_commands.h:235-262; tests/integration/string/string-commands.cpp:162-173 -->

### `getdel`

`GETDEL key` (Redis ≥ 6.2). Atomically returns the value and deletes the key. Returns `std::optional<std::string>` (the
value before deletion, empty if absent).

```cpp
auto getdel(const std::string &key);                                   // -> Reply<std::optional<std::string>>
```

```cpp
auto taken = co_await redis.getdel("one_shot_token");
if (taken.ok() && taken.result().has_value()) { /* consume token */ }
```

<!-- src: qbm/redis/commands/string_commands.h:796-827; tests/integration/string/string-commands.cpp:537-552 -->

### `getex`

`GETEX key [EX s | PX ms]` (Redis ≥ 6.2). Returns the value and resets its expiry. Returns `std::optional<std::string>`.
**Unit-asymmetric** (see the boundary section above): the `long long` overload sends `EX` (seconds), the
`std::chrono::milliseconds` overload sends `PX` (milliseconds).

```cpp
auto getex(const std::string &key, long long ttl_seconds);             // -> Reply<...>  (EX, seconds)
auto getex(const std::string &key, std::chrono::milliseconds const &ttl); // -> Reply<...>  (PX, ms)
```

```cpp
auto a = co_await redis.getex("k", 5000LL);                       // 5000 SECONDS
auto b = co_await redis.getex("k", std::chrono::milliseconds{10000}); // 10000 ms = 10 s
```

<!-- src: qbm/redis/commands/string_commands.h:829-897; tests/integration/string/string-commands.cpp:567-589 -->

---

## Expiry-aware writes

### `setex`

`SETEX key seconds value`. Returns `status`. **Seconds.**

```cpp
auto setex(const std::string &key, long long ttl_seconds, const std::string &val); // -> Reply<status>
auto setex(const std::string &key, std::chrono::seconds const &ttl,
           const std::string &val);                                                // -> Reply<status>
```

```cpp
co_await redis.setex("cache:home", std::chrono::seconds{60}, payload); // expires in 60 s
```

<!-- src: qbm/redis/commands/string_commands.h:627-694; tests/integration/string/string-commands.cpp:340-355 -->

### `psetex`

`PSETEX key milliseconds value`. Returns `status`. **Milliseconds.**

```cpp
auto psetex(const std::string &key, long long ttl_ms, const std::string &val);     // -> Reply<status>
auto psetex(const std::string &key, std::chrono::milliseconds const &ttl,
            const std::string &val);                                               // -> Reply<status>
```

```cpp
co_await redis.psetex("cache:flash", 500, "data");                     // expires in 500 ms
co_await redis.psetex("cache:flash", std::chrono::milliseconds{500}, "data");
```

<!-- src: qbm/redis/commands/string_commands.h:448-516; tests/integration/string/string-commands.cpp:370-385 -->

### `setnx`

`SETNX key value`. Sets only if the key is absent. Returns `bool` (`true` if set, `false` if the key already existed).
Equivalent to `set(key, val, UpdateType::NOT_EXIST)` but with a plain boolean reply.

```cpp
auto setnx(const std::string &key, const std::string &val);            // -> Reply<bool>
```

```cpp
auto got = co_await redis.setnx("lock:report", "worker-7");
if (got.ok() && got.result()) { /* lock acquired */ }
```

<!-- src: qbm/redis/commands/string_commands.h:696-726; tests/integration/string/string-commands.cpp:463-477 -->

---

## Multi-key read and write

### `mget`

`MGET key [key ...]`. Returns `std::vector<std::optional<std::string>>`, positionally aligned to the input keys; an
absent (or non-string) key yields an empty optional at that index.

```cpp
auto mget(const std::vector<std::string> &keys);                       // -> Reply<std::vector<std::optional<std::string>>>
```

```cpp
auto r = co_await redis.mget({"a", "b", "missing"});
if (r.ok()) {
    for (auto const& v : r.result())
        std::cout << (v ? *v : std::string{"<nil>"}) << '\n';
}
```

<!-- src: qbm/redis/commands/string_commands.h:357-386; tests/integration/string/string-commands.cpp:264-285 -->

### `mset`

`MSET key value [key value ...]`. Atomically sets every pair, overwriting existing values. Returns `status` (always
`"OK"`).

```cpp
auto mset(const std::vector<std::pair<std::string, std::string>> &keys); // -> Reply<status>
```

```cpp
co_await redis.mset({{"a", "1"}, {"b", "2"}, {"c", "3"}});
```

<!-- src: qbm/redis/commands/string_commands.h:388-416; tests/integration/string/string-commands.cpp:260-262 -->

### `msetnx`

`MSETNX key value [key value ...]`. Sets every pair **only if none** of the keys exist. Returns `bool` — `true` if all
were set, `false` (and nothing written) if at least one key already existed.

```cpp
auto msetnx(const std::vector<std::pair<std::string, std::string>> &keys); // -> Reply<bool>
```

```cpp
auto r = co_await redis.msetnx({{"x", "1"}, {"y", "2"}});
if (r.ok() && r.result()) { /* all created */ }
```

<!-- src: qbm/redis/commands/string_commands.h:418-446; tests/integration/string/string-commands.cpp:301-324 -->

---

## Counters

These commands interpret the stored string as a number. A missing key is treated as `0` before the operation. A
non-numeric value yields a server-side error (`reply.ok()` is `false`).

### `incr` / `incrby` / `incrbyfloat`

```cpp
auto incr(const std::string &key);                                     // INCR        -> Reply<long long>
auto incrby(const std::string &key, long long increment);              // INCRBY      -> Reply<long long>
auto incrbyfloat(const std::string &key, double increment);            // INCRBYFLOAT -> Reply<double>
```

```cpp
auto n  = co_await redis.incr("page:views");          // +1
auto m  = co_await redis.incrby("user:score", 10);    // +10
auto f  = co_await redis.incrbyfloat("price", 0.5);   // +0.5  (result() is double)
```

<!-- src: qbm/redis/commands/string_commands.h:264-355; tests/integration/string/string-commands.cpp:195-244 -->

### `decr` / `decrby`

```cpp
auto decr(const std::string &key);                                     // DECR   -> Reply<long long>
auto decrby(const std::string &key, long long decrement);              // DECRBY -> Reply<long long>
```

```cpp
auto a = co_await redis.decr("stock");        // -1
auto b = co_await redis.decrby("tickets", 5); // -5

// Callback form
redis.decrby([](qb::redis::Reply<long long>&& r) {
    if (r.ok()) std::cout << "remaining: " << r.result() << '\n';
}, "tickets", 5);
```

<!-- src: qbm/redis/commands/string_commands.h:92-145; tests/integration/string/string-commands.cpp:82-105 -->

There is no `decrbyfloat`; subtract by passing a negative `increment` to `incrbyfloat`.

---

## Append, range, and length

### `append`

`APPEND key value`. Appends to the value (creating the key from an empty string if absent). Returns the resulting length
as `long long`.

```cpp
auto append(const std::string &key, const std::string &val);          // -> Reply<long long>
```

```cpp
co_await redis.append("log", "Hello");
auto r = co_await redis.append("log", " World");
// r.result() == 11
```

<!-- src: qbm/redis/commands/string_commands.h:57-90; tests/integration/string/string-commands.cpp:53-67 -->

### `getrange` / `substr`

`GETRANGE key start end` returns the inclusive substring as `std::string`. Offsets may be negative (`-1` is the last
byte). `substr` is a deprecated Redis alias for the same operation with an identical signature and reply type.

```cpp
auto getrange(const std::string &key, long long start, long long end); // -> Reply<std::string>
auto substr(const std::string &key, long long start, long long end);   // -> Reply<std::string>  (alias)
```

```cpp
auto head = co_await redis.getrange("msg", 0, 4);   // first 5 bytes
auto tail = co_await redis.getrange("msg", -5, -1);  // last 5 bytes
```

<!-- src: qbm/redis/commands/string_commands.h:174-233; tests/integration/string/string-commands.cpp:133-148 -->

### `setrange`

`SETRANGE key offset value`. Overwrites starting at `offset`, zero-padding if `offset` exceeds the current length,
creating the key if absent. Returns the resulting length as `long long`.

```cpp
auto setrange(const std::string &key, long long offset, const std::string &val); // -> Reply<long long>
```

```cpp
co_await redis.set("greeting", "Hello World");
auto len = co_await redis.setrange("greeting", 6, "Redis");
// "greeting" is now "Hello Redis"; len.result() == 11
```

<!-- src: qbm/redis/commands/string_commands.h:728-764; tests/integration/string/string-commands.cpp:492-502 -->

### `strlen`

`STRLEN key`. Returns the string length as `long long` (`0` if the key is absent).

```cpp
auto strlen(const std::string &key);                                   // -> Reply<long long>
```

```cpp
auto n = co_await redis.strlen("greeting");
```

<!-- src: qbm/redis/commands/string_commands.h:766-794; tests/integration/string/string-commands.cpp:517-523 -->

---

## Longest common subsequence

### `lcs`

`LCS key1 key2` (Redis ≥ 7.0). Returns the longest common subsequence of the two stored strings as `std::string`.

```cpp
auto lcs(const std::string &key1, const std::string &key2);            // -> Reply<std::string>
```

```cpp
co_await redis.set("a", "ohmytext");
co_await redis.set("b", "mynewtext");
auto r = co_await redis.lcs("a", "b");
// r.result() == "mytext"
```

<!-- src: qbm/redis/commands/string_commands.h:899-933; tests/integration/string/string-commands.cpp:603-608 -->

> The `LEN`, `IDX`, `MINMATCHLEN`, and `WITHMATCHLEN` options of `LCS` are **not** exposed by this method — only the
> plain two-key subsequence form is available. A prior version of this page described those options; they are not in
`string_commands.h`. Use the raw command path on the client if you need them.

---

## Pitfalls

- **`getex` unit asymmetry.** `getex(key, 5)` is `EX 5` (5 seconds); `getex(key, 5ms)` is `PX 5` (5 milliseconds).
  Prefer the `std::chrono` overload so the unit is visible at the call site.
- **`set` TTL is always `PX`.** Every TTL-bearing `set` overload serializes milliseconds. There is no
  `std::chrono::seconds` overload of `set`; a `std::chrono::seconds` argument is implicitly converted to the
  `std::chrono::milliseconds` overload before sending (so `set(key, val, 30s)` still sends `PX 30000`). Use `setex` when
  you specifically want `SETEX`/seconds-granularity semantics on the wire.
- **Conditional `set` "failure" is not an error.** When `NX`/`XX` is not satisfied, the server returns nil and
  `reply.ok()` is `false` — that is the documented signal that nothing was written, not an exception or a connection
  error. The same applies to `setnx`/`msetnx`, which report it as `result() == false`.
- **Optional payloads need two checks.** For `get`, `getset`, `getdel`, and `getex`, check `reply.ok()` (no
  protocol/connection error) and then `reply.result().has_value()` (key present) before dereferencing.
- **`status` truthiness is exact.** `qb::redis::status` is `true` only for `"OK"`. Use `.ok()`; do not compare against
  arbitrary strings.
- **Callback signature must match exactly.** The callback overload is enabled only when your invocable accepts
  `Reply<T>&&` for that command's `T`. A wrong-typed lambda fails to select the overload rather than calling a different
  command — read the reply-type table above before writing the callback parameter.
- **Counters are server-validated.** `incr`/`decr` on a non-numeric value, or `incrbyfloat` on a non-float value,
  surface as `!reply.ok()` with a Redis error message in `reply.error()`, not a client-side check.

---

## See also

- [commands_overview.md](./commands_overview.md) — the command-dispatch model, `Reply<T>`, callbacks vs coroutines, and
  the module-wide time-unit boundary.
- [key_commands.md](./key_commands.md) — `EXPIRE`/`PEXPIRE`, `TTL`/`PTTL`, `TYPE`, and `DEL` for the keys these commands
  write.
- [bitmap_commands.md](./bitmap_commands.md) — `GETBIT`/`SETBIT`/`BITCOUNT`/`BITOP` over the same string keys at bit
  granularity.
- [connection.md](./connection.md) — opening a `qb::redis::tcp::client`, connect and command timeouts (`qb::duration`),
  and the `RetryPolicy`.
- [error_handling.md](./error_handling.md) — `Reply::ok()`/`error()`, error categories, and timeouts.
