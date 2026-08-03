# Key commands

> **Audience:** Adopter · **Status:** stable · **Verified-against:** qbm-redis @ qb 3.0.0 (C++20 default, C++23
> supported)

Reference for the generic key-management command group — existence, deletion, expiry, type inspection, renaming,
scanning, dumping/restoring, copying, sorting, and replication waits — each with its exact signature, reply type, and a
minimal `co_await` and callback snippet.

**Prerequisites:** [Command API model](./commands_overview.md) · [Connection](./connection.md) — **See also:
** [String commands](./string_commands.md) · [Error handling](./error_handling.md) · [Pipelining and
`await()`](./pipeline_and_await.md)

## Summary

The key commands are defined by the `qb::redis::key_commands<Derived>` CRTP mixin (`key_commands.h:37`), which
`qb::redis::tcp::client` inherits along with every other command group. You call these methods directly on a connected
client. Each command exists in two forms that share one method name: a **coroutine** form you `co_await` to get a
`qb::redis::Reply<T>`, and a **callback** form that takes the handler as its first argument and returns the client for
chaining. The dispatch and the `Reply<T>` decoding contract are covered in [Command API model](./commands_overview.md);
this page lists the methods.

Two unit boundaries apply here and are deliberate, not accidental:

- **Relative expiry** — `EXPIRE` and the chrono overload of `expire(...)` take **seconds**; `PEXPIRE` and the chrono
  overload of `pexpire(...)` take **milliseconds**. This is the native Redis protocol unit per command, surfaced through
  `std::chrono`-unit overloads. It is **not** a `qb::duration` boundary — do not substitute qb time types here.
- **TTL replies are plain integers** — `ttl()`/`expiretime()` return seconds, `pttl()`/`pexpiretime()` return
  milliseconds, all as `Reply<long long>`. The unit lives in the method name; the value carries no unit tag.

(The retired tokens `qb::Timestamp`, `qb::Duration`, `qb::TimePoint`, `to_timestamp(...)`, and `to_time_point(...)` do *
*not** appear in this API and must not be used — they were removed from the framework. The `std::chrono`-unit overloads
below are the correct way to express expiry.)

## Concepts

### Reply types you will see in this group

| Reply `T`                    | Meaning                                | Commands                                                                                                          |
|------------------------------|----------------------------------------|-------------------------------------------------------------------------------------------------------------------|
| `long long`                  | a count or a TTL/timestamp integer     | `del`, `exists`, `touch`, `unlink`, `ttl`, `pttl`, `expiretime`, `pexpiretime`, `wait`, `sortKeyStore`           |
| `std::vector<long long>`     | the per-target fsync counts            | `waitaof`                                                                                                         |
| `bool`                       | success/failure of a keyed mutation    | `expire`, `expireat`, `pexpire`, `pexpireat`, `persist`, `move`, `renamenx`, `copyKey`                            |
| `qb::redis::status`          | a `+OK` status reply                   | `rename`, `restore`, `migrate`                                                                                    |
| `std::string`                | the type name                          | `type`                                                                                                            |
| `std::optional<std::string>` | a possibly-absent key/value            | `dump`, `randomkey`, `objectEncoding`                                                                             |
| `std::optional<long long>`   | a possibly-absent metadata integer     | `objectFreq`, `objectIdletime`, `objectRefcount`                                                                  |
| `std::vector<std::string>`   | a key/value list                       | `keys`, `sortKey`, `sortKeyRo`                                                                                    |
| `qb::redis::scan<>`          | one SCAN round-trip: `{cursor, items}` | `scan`                                                                                                            |

`qb::redis::Reply<T>` (`reply.h:1102`) exposes `ok()`, `result()` (alias `value()`), `value_or(default)`, `error()`, and
an explicit `operator bool()`. Reads of TTL/count replies go through `reply.result()`; optional replies are tested with
`reply.result().has_value()`. See [Command API model](./commands_overview.md) for the full reply surface.

### `qb::redis::status`

`status` (`types.h:475`) wraps a status string. It converts to `bool` (true when the string is `"OK"`), to
`std::string`, and compares against string literals — so `if (reply.result())` reads as "the server said OK".

### `qb::redis::scan<Out>`

`scan<Out>` (`types.h:534`) is `{ std::size_t cursor; Out items; }` with `Out` defaulting to `std::vector<std::string>`.
`SCAN` returns one of these per round-trip; a returned `cursor` of `0` signals the iteration is complete. You loop on
the cursor yourself (see [`scan`](#scan) below).

### Method-name renames

Three Redis verbs are renamed to avoid clashing with the standard library: `COPY` → `copyKey` (`key_commands.h:755`),
`SORT` → `sortKey`/`sortKeyStore`/`sortKeyRo` (`key_commands.h:961`, `:987`, `:1014`). Note that `move()` is the Redis
`MOVE` command (`key_commands.h:313`) and does not collide with the `std::move` you use in callbacks. Every other method
matches its Redis verb in lowercase.

## Setup for the examples

```cpp
#include <qbm/redis/redis.h>                 // namespace qb::redis
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>

using namespace std::chrono_literals;    // for 30s / 30000ms, etc.

// Inside a coroutine:
qb::redis::tcp::client redis{qb::io::uri{"tcp://localhost:6379"}};
if (!co_await redis.connect())
    co_return;
```

<!-- src: qbm/redis/tests/integration/key/key-commands.cpp; qbm/redis/readme/connection.md -->

Each command shows its coroutine signature, its callback overload, and a snippet. The callback form always returns
`Derived&` (the client) for chaining and is SFINAE-gated on the callback accepting `Reply<T>&&` (`key_commands.h:142`).

## Existence and deletion

### `del`

`DEL key [key ...]` — remove keys, returning how many were actually removed. Variadic: pass individual keys or a
container.

```cpp
// Coroutine
template <typename... Keys> auto del(Keys &&...keys);                 // -> Reply<long long>
// Callback
template <typename Func, typename... Keys> Derived &del(Func &&, Keys &&...);
```

```cpp
auto r = co_await redis.del("k1", "k2", "k3");
if (r.ok())
    qb::io::cout() << "removed " << r.result() << " keys\n";

// Callback form
redis.del([](qb::redis::Reply<long long> &&r) { /* r.result() */ }, "k1");
```

<!-- src: qbm/redis/tests/integration/key/key-commands.cpp:52-86 -->

### `unlink`

`UNLINK key [key ...]` — non-blocking deletion; reclaims memory on a background thread. Same shape as `del`, returns the
count unlinked.

```cpp
template <typename... Keys> auto unlink(Keys &&...keys);              // -> Reply<long long>
template <typename Func, typename... Keys> Derived &unlink(Func &&, Keys &&...);
```

```cpp
auto r = co_await redis.unlink("big_key");
```

### `exists`

`EXISTS key [key ...]` — number of the listed keys that exist (a key listed twice counts twice).

```cpp
template <typename... Keys> auto exists(Keys &&...keys);             // -> Reply<long long>
template <typename Func, typename... Keys> Derived &exists(Func &&, Keys &&...);
```

```cpp
auto r = co_await redis.exists("k1", "k2", "k3");
// r.result() == number present
```

<!-- src: qbm/redis/tests/integration/key/key-commands.cpp:120-144 -->

### `touch`

`TOUCH key [key ...]` — update the last-access time of each key; returns the number that existed.

```cpp
template <typename... Keys> auto touch(Keys &&...keys);             // -> Reply<long long>
template <typename Func, typename... Keys> Derived &touch(Func &&, Keys &&...);
```

```cpp
auto r = co_await redis.touch("k1", "k2", "k3");
```

<!-- src: qbm/redis/tests/integration/key/key-commands.cpp:228-245 -->

## Expiration

> **Unit boundary.** `EXPIRE`/`EXPIREAT` work in **seconds**; `PEXPIRE`/`PEXPIREAT` work in **milliseconds**. The chrono
> overloads enforce this at the type level: `expire`/`expireat` accept only `std::chrono::seconds`, `pexpire`/
`pexpireat`
> accept only `std::chrono::milliseconds`, each forwarding `.count()` to the `long long` form (`key_commands.h:224`,
`:269`, `:380`, `:425`). The raw `long long` overloads bypass the check, so prefer the chrono overloads for
> self-documenting code. These are `std::chrono` protocol values, **not** `qb::duration`.

### `expire`

`EXPIRE key seconds` — set a relative timeout in seconds. Returns `true` if the timeout was set, `false` if the key does
not exist.

```cpp
// Coroutine
auto expire(const std::string &key, long long timeout);                       // -> Reply<bool>
auto expire(const std::string &key, const std::chrono::seconds &timeout);     // -> Reply<bool>
// Callback
template <typename Func> Derived &expire(Func &&, const std::string &key, long long timeout);
template <typename Func> Derived &expire(Func &&, const std::string &key, const std::chrono::seconds &timeout);
```

```cpp
auto r = co_await redis.expire("session", 30s);   // seconds
if (r.ok() && r.result()) { /* timeout applied */ }
```

<!-- src: qbm/redis/tests/integration/key/key-commands.cpp:321-370 (EXPIRE_LIFECYCLE: expire(key, std::chrono::seconds{30})) -->

### `pexpire`

`PEXPIRE key milliseconds` — set a relative timeout in milliseconds.

```cpp
auto pexpire(const std::string &key, long long timeout);                          // -> Reply<bool>
auto pexpire(const std::string &key, const std::chrono::milliseconds &timeout);   // -> Reply<bool>
template <typename Func> Derived &pexpire(Func &&, const std::string &key, long long timeout);
template <typename Func> Derived &pexpire(Func &&, const std::string &key, const std::chrono::milliseconds &timeout);
```

```cpp
auto r = co_await redis.pexpire("session", 30000ms);   // milliseconds
```

<!-- src: qbm/redis/tests/integration/key/key-commands.cpp:321-370 (EXPIRE_LIFECYCLE: pexpire(key, std::chrono::milliseconds{30000})) -->

### `expireat`

`EXPIREAT key unix-seconds` — set an absolute expiry as a Unix timestamp in **seconds**.

```cpp
auto expireat(const std::string &key, long long timestamp);                       // -> Reply<bool>
auto expireat(const std::string &key,
              const std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds> &tp);  // -> Reply<bool>
template <typename Func> Derived &expireat(Func &&, const std::string &key, long long timestamp);
template <typename Func> Derived &expireat(Func &&, const std::string &key,
              const std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds> &tp);
```

```cpp
long long now_s = std::chrono::duration_cast<std::chrono::seconds>(
                      std::chrono::system_clock::now().time_since_epoch()).count();
auto r = co_await redis.expireat("session", now_s + 60);   // expire 60s from now
```

<!-- src: qbm/redis/tests/integration/key/key-commands.cpp:321-370 -->

### `pexpireat`

`PEXPIREAT key unix-milliseconds` — set an absolute expiry as a Unix timestamp in **milliseconds**.

```cpp
auto pexpireat(const std::string &key, long long timestamp);                       // -> Reply<bool>
auto pexpireat(const std::string &key,
               const std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds> &tp); // -> Reply<bool>
template <typename Func> Derived &pexpireat(Func &&, const std::string &key, long long timestamp);
template <typename Func> Derived &pexpireat(Func &&, const std::string &key,
               const std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds> &tp);
```

```cpp
long long now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch()).count();
auto r = co_await redis.pexpireat("session", now_ms + 60000);
```

<!-- src: qbm/redis/tests/integration/key/key-commands.cpp:321-370 -->

### `persist`

`PERSIST key` — remove any timeout, making the key persistent. Returns `true` if a timeout was removed.

```cpp
auto persist(const std::string &key);                                  // -> Reply<bool>
template <typename Func> Derived &persist(Func &&, const std::string &key);
```

```cpp
auto r = co_await redis.persist("session");
```

### `ttl`

`TTL key` — remaining time to live in **seconds**. Reply is a plain `long long` (no unit tag): a positive value is the
TTL, `-1` means no expiry, `-2` means the key is absent.

```cpp
auto ttl(const std::string &key);                                      // -> Reply<long long>  (seconds)
template <typename Func> Derived &ttl(Func &&, const std::string &key);
```

```cpp
auto r = co_await redis.ttl("session");
if (r.ok() && r.result() > 0) { /* r.result() seconds left */ }
```

<!-- src: qbm/redis/tests/integration/key/key-commands.cpp:321-370 -->

### `pttl`

`PTTL key` — remaining time to live in **milliseconds**, as `Reply<long long>` with the same `-1`/`-2` sentinels.

```cpp
auto pttl(const std::string &key);                                     // -> Reply<long long>  (milliseconds)
template <typename Func> Derived &pttl(Func &&, const std::string &key);
```

```cpp
auto r = co_await redis.pttl("session");   // milliseconds
```

### `expiretime` / `pexpiretime`

`EXPIRETIME key` / `PEXPIRETIME key` (Redis 7.0+) — the absolute Unix expiry timestamp of a key, in seconds /
milliseconds respectively. `Reply<long long>`; `-1` means no expiry, `-2` means absent.

```cpp
auto expiretime(const std::string &key);                               // -> Reply<long long>  (seconds)
auto pexpiretime(const std::string &key);                              // -> Reply<long long>  (milliseconds)
template <typename Func> Derived &expiretime(Func &&, const std::string &key);
template <typename Func> Derived &pexpiretime(Func &&, const std::string &key);
```

```cpp
auto r = co_await redis.pexpiretime("session");   // unix ms, or -1 / -2
```

## Inspection

### `type`

`TYPE key` — the value's type name (`"string"`, `"list"`, `"set"`, `"zset"`, `"hash"`, `"stream"`), or `"none"` if the
key is absent.

```cpp
auto type(const std::string &key);                                     // -> Reply<std::string>
template <typename Func> Derived &type(Func &&, const std::string &key);
```

```cpp
auto r = co_await redis.type("mylist");
// r.result() == "list"
```

<!-- src: qbm/redis/tests/integration/key/key-commands.cpp:247-279 -->

### `randomkey`

`RANDOMKEY` — a random key from the current database, or an empty optional when the database is empty.

```cpp
auto randomkey();                                                      // -> Reply<std::optional<std::string>>
template <typename Func> Derived &randomkey(Func &&);
```

```cpp
auto r = co_await redis.randomkey();
if (r.ok() && r.result().has_value())
    qb::io::cout() << *r.result() << '\n';
```

<!-- src: qbm/redis/tests/integration/key/key-commands.cpp:172-198 -->

### `objectEncoding` / `objectFreq` / `objectIdletime` / `objectRefcount`

`OBJECT ENCODING|FREQ|IDLETIME|REFCOUNT key` — internal object metadata. `objectEncoding` yields an optional string (
e.g. `"embstr"`, `"listpack"`); the other three yield `std::optional<long long>` (empty when the key is absent).
`objectIdletime` is in **seconds**.

```cpp
auto objectEncoding(const std::string &key);   // -> Reply<std::optional<std::string>>
auto objectFreq(const std::string &key);       // -> Reply<std::optional<long long>>
auto objectIdletime(const std::string &key);   // -> Reply<std::optional<long long>>  (seconds)
auto objectRefcount(const std::string &key);   // -> Reply<std::optional<long long>>
// each has a callback overload Derived &objectXxx(Func &&, const std::string &key);
```

```cpp
auto enc = co_await redis.objectEncoding("counter");
if (enc.ok() && enc.result().has_value())
    qb::io::cout() << "encoding=" << *enc.result() << '\n';
```

## Scanning

### `keys`

`KEYS pattern` — every key matching a glob pattern (default `"*"`). Returns the full list in one reply.

> **Pitfall.** `KEYS` scans the whole keyspace synchronously inside Redis and can stall the server. Use [`scan`](#scan)
> on production instances.

```cpp
auto keys(const std::string &pattern = "*");                           // -> Reply<std::vector<std::string>>
template <typename Func> Derived &keys(Func &&, const std::string &pattern = "*");
```

```cpp
auto r = co_await redis.keys("user:*");
for (auto const &k : r.result()) { /* ... */ }
```

<!-- src: qbm/redis/tests/integration/key/key-commands.cpp:146-170 -->

### `scan`

`SCAN cursor [MATCH pattern] [COUNT count]` — cursor-based incremental iteration. The default (explicit-cursor) overload
performs **one** round-trip and returns a `qb::redis::scan<>` of `{cursor, items}`; you drive the loop until the
returned cursor is `0`.

```cpp
// Single-iteration coroutine
auto scan(long long cursor, const std::string &pattern = "*", long long count = 10);  // -> Reply<scan<>>
// Single-iteration callback
template <typename Func> Derived &scan(Func &&, long long cursor,
                                       const std::string &pattern = "*", long long count = 10);
// Auto-iterating callback (no cursor): buffers all matches, calls Func once at cursor 0
template <typename Func> Derived &scan(Func &&, const std::string &pattern = "*");
```

```cpp
std::vector<std::string> all;
long long cursor = 0;
do {
    auto r = co_await redis.scan(cursor, "user:*", 100);
    cursor = r.result().cursor;
    all.insert(all.end(), r.result().items.begin(), r.result().items.end());
} while (cursor != 0);
```

<!-- src: qbm/redis/tests/integration/key/key-commands.cpp:200-226 -->

> **Pitfalls for the auto-iterating callback overload `scan(func, pattern)`.** It accumulates the **entire** matched
> keyspace in memory and invokes `func` exactly once after the cursor returns to `0` (`key_commands.h:97-117`, `:598`) —
> it is not a per-batch stream. Its internal SCAN calls use `MATCH` only, with no `COUNT` hint (`key_commands.h:84`,
`:102`). And if your callback throws, the scanner catches `std::exception` and only logs a warning (
`key_commands.h:106-107`) — the exception does not surface to the caller. For large keyspaces and for back-pressure, prefer
> the explicit-cursor loop above.

## Renaming and copying

### `rename`

`RENAME key newkey` — rename, overwriting `newkey` if it exists. Errors if `key` is absent.

```cpp
auto rename(const std::string &key, const std::string &new_key);       // -> Reply<status>
template <typename Func> Derived &rename(Func &&, const std::string &key, const std::string &new_key);
```

```cpp
auto r = co_await redis.rename("old", "new");
if (r.ok() && r.result()) { /* "OK" */ }
```

<!-- src: qbm/redis/tests/integration/key/key-commands.cpp:372-410 -->

### `renamenx`

`RENAMENX key newkey` — rename only if `newkey` does not already exist. Returns `true` when the rename happened.

```cpp
auto renamenx(const std::string &key, const std::string &new_key);     // -> Reply<bool>
template <typename Func> Derived &renamenx(Func &&, const std::string &key, const std::string &new_key);
```

```cpp
auto r = co_await redis.renamenx("old", "new");   // r.result() == false if "new" existed
```

<!-- src: qbm/redis/tests/integration/key/key-commands.cpp:372-410 -->

### `copyKey`

`COPY source destination [DB db] [REPLACE]` (Redis 6.2+) — copy a value. `db` optionally targets another database;
`replace` overwrites an existing destination. Returns `true` when the copy succeeded.

```cpp
auto copyKey(const std::string &source, const std::string &destination,
             std::optional<long long> db = std::nullopt, bool replace = false);   // -> Reply<bool>
template <typename Func> Derived &copyKey(Func &&, const std::string &source,
             const std::string &destination, std::optional<long long> db = std::nullopt,
             bool replace = false);
```

```cpp
auto r = co_await redis.copyKey("src", "dst");
auto r2 = co_await redis.copyKey("src", "dst", std::nullopt, /*replace=*/true);
```

<!-- src: qbm/redis/tests/integration/key/key-commands.cpp:438-478 -->

### `move`

`MOVE key db` — move a key into another database index on the same instance. Returns `true` when moved (false if the key
was absent or already present in the target).

```cpp
auto move(const std::string &key, long long destination_db);           // -> Reply<bool>
template <typename Func> Derived &move(Func &&, const std::string &key, long long destination_db);
```

```cpp
auto r = co_await redis.move("session", 1LL);
```

<!-- src: qbm/redis/tests/integration/key/key-commands.cpp:412-436 -->

## Serialization and migration

### `dump`

`DUMP key` — RDB-serialized representation of the value, or an empty optional if the key is absent. Pair with `restore`.
The blob is binary and not human-readable.

```cpp
auto dump(const std::string &key);                                     // -> Reply<std::optional<std::string>>
template <typename Func> Derived &dump(Func &&, const std::string &key);
```

```cpp
auto d = co_await redis.dump("k");
if (d.ok() && d.result().has_value()) { /* *d.result() is the blob */ }
```

<!-- src: qbm/redis/tests/integration/key/key-commands.cpp:88-118 -->

### `restore`

`RESTORE key ttl serialized-value [REPLACE]` — recreate a key from a `dump` blob. `ttl` is in **milliseconds** (`0`
means no expiry). Set `replace = true` to overwrite an existing key.

```cpp
auto restore(const std::string &key, const std::string &val, long long ttl,
             bool replace = false);                                    // -> Reply<status>
template <typename Func> Derived &restore(Func &&, const std::string &key,
             const std::string &val, long long ttl, bool replace = false);
```

```cpp
auto d = co_await redis.dump("k");
auto r = co_await redis.restore("k_copy", *d.result(), /*ttl_ms=*/0);
```

<!-- src: qbm/redis/tests/integration/key/key-commands.cpp:88-118 -->

### `migrate`

`MIGRATE host port key destination-db timeout [COPY] [REPLACE] [AUTH password]` — atomically transfer a key to another
Redis instance. `timeout` is in **milliseconds**. Returns a `status` (`"OK"`, or `"NOKEY"` when the source key is
absent).

```cpp
auto migrate(const std::string &host, int port, const std::string &key, long long db,
             long long timeout, bool copy = false, bool replace = false,
             std::optional<std::string> auth = std::nullopt);          // -> Reply<status>
template <typename Func> Derived &migrate(Func &&, const std::string &host, int port,
             const std::string &key, long long db, long long timeout, bool copy = false,
             bool replace = false, std::optional<std::string> auth = std::nullopt);
```

```cpp
auto r = co_await redis.migrate("10.0.0.2", 6379, "k", /*db=*/0, /*timeout_ms=*/5000);
```

## Sorting

`SORT` is exposed under three names to avoid colliding with `std::sort`. `options` is a raw token list appended
verbatim — e.g. `{"LIMIT", "0", "10", "ALPHA", "DESC"}` or `{"BY", "weight_*", "GET", "data_*"}`.

### `sortKey` / `sortKeyRo`

`SORT key [...]` / `SORT_RO key [...]` — return sorted elements. `sortKeyRo` is the read-only variant safe on replicas.

```cpp
auto sortKey(const std::string &key, const std::vector<std::string> &options = {});    // -> Reply<std::vector<std::string>>
auto sortKeyRo(const std::string &key, const std::vector<std::string> &options = {});  // -> Reply<std::vector<std::string>>
// callback overloads: Derived &sortKey(Func &&, key, options); Derived &sortKeyRo(Func &&, key, options);
```

```cpp
auto r = co_await redis.sortKey("mylist", {"ALPHA", "DESC"});
for (auto const &e : r.result()) { /* ... */ }
```

### `sortKeyStore`

`SORT key ... STORE destination` — sort and store the result into `destination`, returning the element count stored.

```cpp
auto sortKeyStore(const std::string &key, const std::string &destination,
                  const std::vector<std::string> &options = {});       // -> Reply<long long>
template <typename Func> Derived &sortKeyStore(Func &&, const std::string &key,
                  const std::string &destination, const std::vector<std::string> &options = {});
```

```cpp
auto r = co_await redis.sortKeyStore("mylist", "sorted_out", {"ALPHA"});
// r.result() == number of elements written to "sorted_out"
```

## Replication waits

### `wait`

`WAIT numreplicas timeout` — block until the prior writes are acknowledged by at least `numreplicas` replicas, or the
timeout elapses. `timeout` is in **milliseconds** (`0` = wait forever). Returns the number of replicas that
acknowledged (may be fewer than requested if the timeout hits).

```cpp
auto wait(long long num_slaves, long long timeout);                                       // -> Reply<long long>
auto wait(long long num_slaves, const std::chrono::milliseconds &ttl = std::chrono::milliseconds{0}); // -> Reply<long long>
template <typename Func> Derived &wait(Func &&, long long num_slaves, long long timeout);
template <typename Func> Derived &wait(Func &&, long long num_slaves,
             const std::chrono::milliseconds &ttl = std::chrono::milliseconds{0});
```

```cpp
auto r = co_await redis.wait(1, 1000ms);   // wait up to 1s for 1 replica
```

### `waitaof`

`WAITAOF numlocal numreplicas timeout` (Redis 7.2+) — block until the prior writes are fsynced to the AOF locally and on
`numreplicas` replicas. `timeout` is in **milliseconds**. Returns the two fsync counts `[numlocal, numreplicas]` as a vector.

```cpp
auto waitaof(long long num_local, long long num_replicas, long long timeout);   // -> Reply<std::vector<long long>>
template <typename Func> Derived &waitaof(Func &&, long long num_local,
             long long num_replicas, long long timeout);
```

```cpp
auto r = co_await redis.waitaof(1, 0, 1000);   // local fsync, 1s timeout (ms)
```

## Pitfalls

- **Seconds vs milliseconds is per command, by design.** `expire`/`expireat`/`ttl`/`expiretime`/`objectIdletime` are
  seconds; `pexpire`/`pexpireat`/`pttl`/`pexpiretime`/`restore` ttl/`migrate` timeout/`wait`/`waitaof` are milliseconds.
  The chrono overloads (`std::chrono::seconds` vs `std::chrono::milliseconds`) make the unit explicit and reject the
  wrong one at compile time — prefer them over the raw `long long` overloads. These are Redis-protocol units, not
  `qb::duration`; do not convert them through qb time types.
- **TTL replies are integers with sentinels.** `ttl`/`pttl`/`expiretime`/`pexpiretime` return `Reply<long long>`: `-1` (
  no expiry) and `-2` (key absent) are valid values, not errors. Always check `r.result()` against these before treating
  it as a duration.
- **`keys()` can block the server.** Use the explicit-cursor `scan(cursor, pattern, count)` loop for production
  keyspaces.
- **The auto-iterating `scan(func, pattern)` buffers everything and swallows callback exceptions.** It calls back once
  at the end, not per batch, uses no `COUNT` hint, and logs (does not propagate) a throwing callback. Prefer the cursor
  loop when memory or error visibility matters.
- **`del`/`exists`/`touch`/`unlink` are variadic but reply with one integer.** They return the count, not a per-key
  list. Use `exists` with a single key to get a 0/1 presence check.
- **Renamed verbs.** Reach for `copyKey` (not `copy`) and `sortKey`/`sortKeyRo`/`sortKeyStore` (not `sort`); `move` is
  the Redis `MOVE` command.

## See also

- [Command API model](./commands_overview.md) — dispatch, `Reply<T>`, coroutine vs callback forms.
- [String commands](./string_commands.md) — `SETEX`/`PSETEX`/`GETEX` share the same seconds-vs-milliseconds boundary.
- [Connection](./connection.md) — connect, `RetryPolicy`, and command-timeout deadlines (those *are* `qb::duration`).
- [Error handling](./error_handling.md) — interpreting `reply.ok()` and `reply.error()`.
- [Pipelining and `await()`](./pipeline_and_await.md) — batching key commands on one round-trip.
