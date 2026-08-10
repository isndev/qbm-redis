# HyperLogLog commands

> **Audience:** Adopter · **Status:** stable · **Verified-against:** qbm-redis @ qb 3.0.0 (C++20 default, C++23
> supported)

Reference for the HyperLogLog command group — `PFADD`, `PFCOUNT`, and `PFMERGE` — which estimate the cardinality of very
large sets in a small, fixed amount of memory.

**Prerequisites:** [../README.md](../README.md) (install, `qb_load_modules`,
`qbm::redis`), [connection.md](./connection.md), [commands_overview.md](./commands_overview.md) (the `Reply<T>` model,
coroutine vs. callback forms) — **See also:** [set_commands.md](./set_commands.md) (exact membership, when you can
afford it), [error_handling.md](./error_handling.md)

---

## Summary

A HyperLogLog is a probabilistic data structure: it answers "how many *distinct* elements have I seen?" using roughly 12
KB per key regardless of how many elements you add, at the cost of a standard error around 0.81%. The three commands in
this group write elements into a HyperLogLog (`PFADD`), read back the estimated cardinality of one or more of them (
`PFCOUNT`), and fold several HyperLogLogs into one (`PFMERGE`). On the wire a HyperLogLog is stored as an ordinary Redis
string, so the key-level commands in [key_commands.md](./key_commands.md) (`DEL`, `EXISTS`, `EXPIRE`, …) apply to the
same key.

The `hyperloglog_commands<Derived>` mixin is one of the command groups inherited by `qb::redis::tcp::client`. Every
command is exposed in two forms, both fully asynchronous:

- a **coroutine** form (`auto`-returning) that yields a `Reply<T>` you `co_await`;
- a **callback** form that takes your handler first and returns `Derived&` for chaining.

There is no blocking variant — the older "Sync" signatures and the `*_async` method names never existed in this module.
None of these commands carry a `qb::duration`: their only arguments are keys and string elements, so the
`qb::duration` / native-unit boundary documented for `EXPIRE` in [commands_overview.md](./commands_overview.md) does *
*not** apply here.

```cpp
#include <qbm/redis/redis.h>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>

qb::io::async::task<void> hll_demo(qb::redis::tcp::client &redis) {
    auto visitors = std::string{"page:home:visitors"};

    // Record three distinct visitors; PFADD is variadic.
    auto changed = co_await redis.pfadd(visitors, "alice", "bob", "carol");
    if (changed)                                  // Reply<bool> is contextually bool (== ok())
        qb::io::cout() << "registers altered: "
                       << std::boolalpha << changed.result() << std::endl;

    // Re-adding a known element does not change the estimate.
    auto again = co_await redis.pfadd(visitors, "alice");   // Reply<bool>, result() == false

    // Estimated unique count.
    auto n = co_await redis.pfcount(visitors);              // Reply<long long>
    if (n)
        qb::io::cout() << "approx unique visitors: " << n.result() << std::endl;
}
```

<!-- src: qbm/redis/tests/integration/hyperloglog/hyperloglog-commands.cpp -->

---

## Concepts

### One mixin, two forms per command

`hyperloglog_commands<Derived>` is a CRTP mixin (`template <typename Derived>`): it is not a standalone object and
cannot be instantiated on its own. It reaches the connection through `Derived` — it calls
`derived().command<T>(func, ...)` for the callback form and `derived().make_coro_command<T>(...)` for the coroutine
form — so you only ever use it through the composed client,
`qb::redis::tcp::client`. <!-- src: qbm/redis/src/qbm/redis/commands/hyperloglog_commands.h:38-44,59-61,79 -->

Each command therefore exists as a matched pair of overloads:

- the **coroutine** overload returns `auto` (a `redis_awaiter`) and yields `Reply<T>` when you `co_await` it;
- the **callback** overload is selected by an `std::enable_if_t<std::is_invocable_v<Func, Reply<T> &&>, Derived &>`
  constraint, takes the handler as its **first** argument, and returns `Derived&` so calls can be chained.

### No `qb::duration` in this group

A HyperLogLog stores counts, not times. Every argument to these commands is a key name or a string element, and the only
reply payloads are `bool`, `long long`, and `status`. There is no expiry, no timeout, and no `std::chrono` value on any
signature here. The `qb::duration` values in qbm-redis live exclusively on the connection and retry path (
connect/command timeouts, `RetryPolicy` delays — see [connection.md](./connection.md)); the native-unit `std::chrono`
split that applies to `EXPIRE` (seconds) versus `PEXPIRE` (milliseconds) is a property of the key-expiry commands
in [key_commands.md](./key_commands.md), not of this group.

### Reply types at a glance

| Command   | Reply payload (`T` in `Reply<T>`) | Meaning                                                                                                 |
|-----------|-----------------------------------|---------------------------------------------------------------------------------------------------------|
| `PFADD`   | `bool`                            | `true` if at least one internal register was altered (the estimate may have changed), `false` otherwise |
| `PFCOUNT` | `long long`                       | estimated cardinality of the set, or of the union of several sets                                       |
| `PFMERGE` | `qb::redis::status`               | simple-string reply, `"OK"` on success                                                                  |

`qb::redis::status` is the simple-string reply wrapper from `types.h`; it is contextually convertible to `bool` (`true`
when the string is exactly `"OK"`) and exposes `.ok()` and `.str()`. <!-- src: qbm/redis/src/qbm/redis/types.h:470-527 -->

---

## Commands

### `PFADD key [element ...]` — `pfadd`

Add zero or more elements to the HyperLogLog at `key`, creating the key if it does not exist. The reply is `true` when
at least one internal register changed as a result of the call — equivalently, when the estimated cardinality may have
moved — and `false` when every element was already accounted for.

```cpp
// Coroutine (awaitable)
template <typename... Elements>
auto pfadd(const std::string &key, Elements &&...elements);            // yields Reply<bool>

// Callback
template <typename Func, typename... Elements>
std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
pfadd(Func &&func, const std::string &key, Elements &&...elements);
```

<!-- src: qbm/redis/src/qbm/redis/commands/hyperloglog_commands.h:53-78 -->

Elements are passed variadically; each must be a value the client can serialize as a command argument (a string literal,
`std::string`, or any type with the module's argument conversion). Calling `pfadd(key)` with no elements compiles and
issues a bare `PFADD key`, which Redis accepts: it replies `true` (a register changed) when this creates a new key, and
`false` when the key already existed.

```cpp
// Coroutine
auto r = co_await redis.pfadd("hll:users", "u1", "u2", "u3");   // Reply<bool>
if (r && r.result())
    qb::io::cout() << "estimate changed" << std::endl;

// Callback (first argument is the handler)
redis.pfadd(
    [](qb::redis::Reply<bool> &&reply) {
        if (reply)
            qb::io::cout() << "altered: " << reply.result() << std::endl;
    },
    "hll:users", "u4");
```

<!-- src: qbm/redis/tests/integration/hyperloglog/hyperloglog-commands.cpp:43-66 -->

### `PFCOUNT key [key ...]` — `pfcount`

Return the estimated number of distinct elements. With a single key, you get the cardinality of that HyperLogLog; with
several keys, you get the cardinality of their *union* — Redis computes the merge transiently without writing anything.

```cpp
// Coroutine (awaitable)
template <typename... Keys>
auto pfcount(Keys &&...keys);                                          // yields Reply<long long>

// Callback
template <typename Func, typename... Keys>
std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
pfcount(Func &&func, Keys &&...keys);
```

<!-- src: qbm/redis/src/qbm/redis/commands/hyperloglog_commands.h:88-112 -->

The keys are taken purely variadically — there is no mandatory leading key parameter — so `pfcount()` with zero keys
compiles. It never reaches the server: the callback overload rejects an empty pack **client-side** with
`fail_client<long long>(func, "PFCOUNT requires at least one key")` and returns, so the handler fires (and the awaiting
coroutine resumes) with `.ok() == false` and that text in `.error()` (`hyperloglog_commands.h:111-114`). Always pass at
least one key. The result is a plain integer; it is never wrapped in `std::optional`, and a non-existent key counts as
cardinality `0` rather than an error.

```cpp
// Coroutine — union of two keys
auto total = co_await redis.pfcount("hll:a", "hll:b");   // Reply<long long>
if (total)
    qb::io::cout() << "approx union size: " << total.result() << std::endl;

// Callback
redis.pfcount(
    [](qb::redis::Reply<long long> &&reply) {
        if (reply)
            qb::io::cout() << "count: " << reply.result() << std::endl;
    },
    "hll:a");
```

<!-- src: qbm/redis/tests/integration/hyperloglog/hyperloglog-commands.cpp:69-92 -->

### `PFMERGE destkey [sourcekey ...]` — `pfmerge`

Merge one or more source HyperLogLogs into the destination key, storing the union there. The destination is included in
the merge if it already exists, so `pfmerge(dest, dest, other)` accumulates into `dest`. The reply is a `status` —
`"OK"` on success.

```cpp
// Coroutine (awaitable)
template <typename... Keys>
auto pfmerge(const std::string &destination, Keys &&...keys);         // yields Reply<status>

// Callback
template <typename Func, typename... Keys>
std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
pfmerge(Func &&func, const std::string &destination, Keys &&...keys);
```

<!-- src: qbm/redis/src/qbm/redis/commands/hyperloglog_commands.h:123-149 -->

The source keys are variadic and follow the required `destination`. As with `pfcount`, passing zero source keys (
`pfmerge(dest)`) compiles — and, as with `pfcount`, it never reaches the server: the callback overload rejects an empty
`destination` **or** an empty source pack client-side with
`fail_client<status>(func, "PFMERGE requires a destination and at least one source key")`
(`hyperloglog_commands.h:149-152`), so you get a failed `Reply<status>` rather than an empty HyperLogLog at `dest`.

```cpp
// Coroutine
auto merged = co_await redis.pfmerge("hll:all", "hll:a", "hll:b");   // Reply<status>
if (merged && merged.result())                  // status is contextually bool (== "OK")
    qb::io::cout() << "merged: " << merged.result().str() << std::endl;

auto size = co_await redis.pfcount("hll:all");                       // Reply<long long>

// Callback
redis.pfmerge(
    [](qb::redis::Reply<qb::redis::status> &&reply) {
        if (reply.ok())
            qb::io::cout() << reply.result().str() << std::endl;     // "OK"
    },
    "hll:all", "hll:a", "hll:b");
```

<!-- src: qbm/redis/tests/integration/hyperloglog/hyperloglog-commands.cpp:95-118 -->

---

## Pitfalls

- **No blocking API.** These methods are coroutine- or callback-based only. A call without `co_await` (or a callback)
  builds and queues the command, and the result reaches you asynchronously. The `Reply<bool> pfadd(...)` "Sync"
  signatures and the `pfadd_async` / `pfcount_async` / `pfmerge_async` method names in older docs do not exist — the
  async form is the same `pfadd` / `pfcount` / `pfmerge` overload that takes the handler first.
- **No `std::vector` parameters.** Earlier docs showed `pfcount(const std::vector<std::string> &keys)` and
  `pfmerge(dest, const std::vector<std::string> &sourcekeys)`. The real signatures take keys *variadically* (
  `Keys &&...keys`); pass the keys directly as arguments, not as a vector.
- **`pfcount()` and `pfmerge(dest)` with no keys compile, and fail *client-side*.** The variadic packs have no mandatory
  leading key beyond `pfmerge`'s `destination`, so there is no compile-time guard — but neither call ever reaches Redis:
  each callback overload tests the pack and routes an empty one through `fail_client`, which resolves the callback (and
  therefore the awaiter) with a failed `Reply<T>` carrying
  `"PFCOUNT requires at least one key"` (`hyperloglog_commands.h:111-114`) or
  `"PFMERGE requires a destination and at least one source key"` (`hyperloglog_commands.h:149-152`).
  Always supply at least one key.
- **`PFADD` returning `false` is success, not failure.** Check `.ok()` to detect a transport or protocol error. A
  `false` *result* means the elements were already represented — the call succeeded and changed nothing. Do not treat
  `result() == false` as an error.
- **`PFCOUNT` of a single key may write.** Reading one key can lazily cache its computed cardinality back into the key (
  a documented Redis side effect on the first count after writes), so a single-key `PFCOUNT` is not strictly read-only.
  The multi-key union form does not persist anything. This matters only if you depend on a HyperLogLog key being
  byte-stable across reads.
- **Estimates, not exact counts.** Every `PFCOUNT` carries a standard error near 0.81%. If you need exact distinct
  counts at small scale, use a set ([set_commands.md](./set_commands.md)) and `SCARD` instead; HyperLogLog trades
  accuracy for fixed ~12 KB memory.
- **A HyperLogLog is just a string.** `DEL`, `EXISTS`, `TYPE` (it reports `string`), and `EXPIRE`
  from [key_commands.md](./key_commands.md) all work on the key. Be aware that `GET`/`SET` on the same key will read or
  clobber the raw HyperLogLog representation.
- **No `qb::duration` here.** These commands take only keys and string elements; do not substitute the framework time
  types onto any argument. The only `std::chrono`-shaped values in qbm-redis live on the connection and retry path (
  see [connection.md](./connection.md)).

---

## See also

- [set_commands.md](./set_commands.md) — exact set membership and `SCARD`, the precise (but memory-heavy) alternative to
  HyperLogLog cardinality.
- [key_commands.md](./key_commands.md) — `DEL`, `EXISTS`, `EXPIRE`, and `TYPE`, all of which apply to a HyperLogLog
  key (stored as a Redis string).
- [commands_overview.md](./commands_overview.md) — the `Reply<T>` model, coroutine vs. callback dispatch, and the
  time-unit boundary that does *not* apply to these commands.
- [error_handling.md](./error_handling.md) — interpreting `Reply<T>::ok()`, `error()`, and error categories.
- [connection.md](./connection.md) — where the real `qb::duration` values live (connect/command timeouts,
  `RetryPolicy`).
- [Redis HyperLogLog commands](https://redis.io/commands/?group=hyperloglog) — upstream command semantics.
