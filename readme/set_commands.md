# Set commands

> **Audience:** Adopter · **Status:** stable · **Verified-against:** qbm-redis @ qb 2.6.0 (C++20 default, C++23
> supported)

Reference for the Redis set command group exposed by `qb::redis::set_commands<Derived>` — unordered collections of
unique strings, with each command listed by its exact signature, arguments, reply type, and a minimal coroutine and
callback snippet.

**Prerequisites:** [../README.md](../README.md) (install, `qb_load_modules`,
`qbm::redis`), [connection.md](./connection.md), [commands_overview.md](./commands_overview.md) — **See also:
** [sorted_set_commands.md](./sorted_set_commands.md), [hash_commands.md](./hash_commands.md), [key_commands.md](./key_commands.md), [error_handling.md](./error_handling.md), [pipeline_and_await.md](./pipeline_and_await.md)

---

## Summary

A Redis set is an unordered collection of unique strings — the natural shape for membership tests, tag sets, and the
algebraic operations (difference, intersection, union) on top of them. The commands here are defined in
`qbm/redis/commands/set_commands.h` as a CRTP mixin (`set_commands<Derived>`) that the client inherits, so you call them directly
on a connected `qb::redis::tcp::client`: `redis.sadd(...)`, `redis.smembers(...)`, and so on. There is no `_async`
suffix and no separate sync/async class — the overload you pick is what selects the calling style.

Each command exposes two overloads:

- a **coroutine** form (no callback argument) that returns an awaiter yielding `Reply<T>` — drive it with `co_await`
  inside a `qb::io::async::task<...>`, or with `qb::io::async::run_sync(...)` from synchronous code;
- a **callback** form whose **first** argument is the handler and which returns `Derived&` for chaining. Your callback
  must be invocable with `Reply<T>&&` for the command's `T` (a `Reply<T>&&`, `const Reply<T>&`, or `auto&&` parameter
  all qualify). Every callback overload carries an explicit
  `std::enable_if_t<std::is_invocable_v<Func, Reply<T>&&>, Derived&>` return type, so the compiler can tell the callback
  overload apart from the coroutine overload — including for the variadic and multi-arg commands (`sadd`, `srem`,
  `smismember`, `sdiff`, `sinter`, `sunion`, `sscan`, …).

<!-- src: qbm/redis/commands/set_commands.h:121-157 -->

One operation is **callback-only** and has no coroutine form: the auto-iterating `sscan(func, key, pattern)` that walks
every page internally. It is covered below.

**No time-unit boundary applies to this group.** Set commands carry no TTL or expiry arguments, so none of the
`std::chrono` / `qb::duration` unit concerns from the key and string groups apply here. (Connect and command deadlines
remain `qb::duration` at the client level — see [connection.md](./connection.md).) Counts and cardinalities are native
`long long`.

---

## Reply types at a glance

`Reply<T>` is the uniform envelope (`qbm/redis/reply.h:1102-1177`): `reply.ok()` reports success, `reply.result()` (alias
`reply.value()`) holds the parsed payload, `reply.error()` holds the server error string, and `Reply<T>` is contextually
convertible to `bool` (explicit). Container payloads use **qb-core** containers, not `std::`.

| Command(s)                                                                        | Reply payload `T`                                                    |
|-----------------------------------------------------------------------------------|----------------------------------------------------------------------|
| `sadd`, `srem`, `scard`, `sdiffstore`, `sinterstore`, `sunionstore`, `sintercard` | `long long`                                                          |
| `sismember`, `smove`                                                              | `bool`                                                               |
| `smismember`                                                                      | `std::vector<bool>`                                                  |
| `smembers`                                                                        | `qb::unordered_set<std::string>`                                     |
| `sdiff`, `sinter`, `sunion`                                                       | `std::vector<std::string>`                                           |
| `spop(key)`, `srandmember(key)`                                                   | `std::optional<std::string>`                                         |
| `spop(key, count)`, `srandmember(key, count)`                                     | `std::vector<std::string>`                                           |
| `sscan` (cursor form)                                                             | `qb::redis::scan<Out>`, `Out` defaults to `std::vector<std::string>` |

<!-- src: qbm/redis/commands/set_commands.h:132, 167, 200, 233, 268, 301, 340, 374, 410, 445, 478, 511, 543, 576, 609, 644, 684, 744, 780; qbm/redis/types.h:534 -->

> **`smembers` yields a set, not a vector.** The reply payload is `qb::unordered_set<std::string>` (
`set_commands.h:445`), iterable but unordered with no index access. The `sdiff` / `sinter` / `sunion` family return
`std::vector<std::string>` instead. `spop(key, count)` and `srandmember(key, count)` return a *
*`std::vector<std::string>`** — not a vector of `std::optional`. The single-member `spop(key)` / `srandmember(key)`
> return `std::optional<std::string>` because the source set may be empty.

The `scan<Out>` struct has two fields — `cursor` (a `std::size_t`; `0` means iteration is complete) and `items` (the
`Out` page, here `std::vector<std::string>`).

<!-- src: qbm/redis/types.h:533-537 -->

---

## Commands

The snippets assume a connected client inside a coroutine; see [connection.md](./connection.md) for setup.

```cpp
#include <redis/redis.h>            // namespace qb::redis
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
```

### `SADD key member [member ...]` — `sadd`

Adds one or more members to the set at `key`. Returns the number of members **actually added** (members already present
do not count).

```cpp
template <typename... Members>
auto sadd(const std::string &key, Members &&...members);                       // coroutine

template <typename Func, typename... Members>
Derived &sadd(Func &&func, const std::string &key, Members &&...members);       // callback
```

The members are forwarded straight into the wire command, so any type the codec serializes works (typically
`std::string` / string literals).

```cpp
auto added = co_await redis.sadd("tags:42", "red", "green");
if (added.ok())
    std::cout << added.result() << " new members\n";   // 2 on a fresh set
```

```cpp
redis.sadd([](qb::redis::Reply<long long> &&r) {
    if (r.ok()) { /* r.result() members added */ }
}, "tags:42", "blue");
```

<!-- src: qbm/redis/commands/set_commands.h:121-157; qbm/redis/tests/integration/set/set-commands.cpp:56-64 -->

### `SCARD key` — `scard`

Returns the cardinality (member count) of the set at `key`; `0` if the key does not exist.

```cpp
auto scard(const std::string &key);                                            // coroutine
template <typename Func> Derived &scard(Func &&func, const std::string &key);   // callback
```

```cpp
auto n = co_await redis.scard("tags:42");
if (n.ok()) std::cout << n.result() << " members\n";
```

<!-- src: qbm/redis/commands/set_commands.h:159-187; qbm/redis/tests/integration/set/set-commands.cpp:66-68 -->

### `SREM key member [member ...]` — `srem`

Removes one or more members from the set at `key`. Returns the number of members **actually removed** (members not
present do not count).

```cpp
template <typename... Members>
auto srem(const std::string &key, Members &&...members);                        // coroutine

template <typename Func, typename... Members>
Derived &srem(Func &&func, const std::string &key, Members &&...members);        // callback
```

```cpp
auto removed = co_await redis.srem("tags:42", "red", "blue");
if (removed.ok()) std::cout << removed.result() << " removed\n";
```

<!-- src: qbm/redis/commands/set_commands.h:633-669 -->

### `SISMEMBER key member` — `sismember`

Tests whether `member` is in the set at `key`. Returns `true` / `false`.

```cpp
auto sismember(const std::string &key, const std::string &member);             // coroutine
template <typename Func>
Derived &sismember(Func &&func, const std::string &key, const std::string &member); // callback
```

```cpp
auto present = co_await redis.sismember("tags:42", "green");
if (present.ok() && present.result()) { /* member exists */ }
```

<!-- src: qbm/redis/commands/set_commands.h:364-396 -->

### `SMISMEMBER key member [member ...]` — `smismember`

Tests several members at once. Returns a `std::vector<bool>` positionally aligned with the members you passed — `true`
for present, `false` for absent.

```cpp
template <typename... Members>
auto smismember(const std::string &key, Members &&...members);                  // coroutine

template <typename Func, typename... Members>
Derived &smismember(Func &&func, const std::string &key, Members &&...members);  // callback
```

```cpp
auto flags = co_await redis.smismember("tags:42", "red", "green", "nope");
if (flags.ok()) {
    // flags.result() == { false, true, false }
}
```

<!-- src: qbm/redis/commands/set_commands.h:398-435; qbm/redis/tests/integration/set/set-commands.cpp:97-105 -->

### `SMEMBERS key` — `smembers`

Returns every member of the set at `key` as a `qb::unordered_set<std::string>` (unordered, no duplicates).

```cpp
auto smembers(const std::string &key);                                         // coroutine
template <typename Func> Derived &smembers(Func &&func, const std::string &key); // callback
```

```cpp
auto members = co_await redis.smembers("tags:42");
if (members.ok())
    for (auto const &m : members.result())
        std::cout << m << '\n';
```

> For large sets, prefer `sscan` (below) to avoid blocking the server on a single bulk reply.

<!-- src: qbm/redis/commands/set_commands.h:437-466; qbm/redis/tests/integration/set/set-commands.cpp:70-73 -->

### `SMOVE source destination member` — `smove`

Atomically moves `member` from set `source` to set `destination`. Returns `true` if the member was moved, `false` if it
was not a member of `source`.

```cpp
auto smove(const std::string &source, const std::string &destination,
           const std::string &member);                                         // coroutine
template <typename Func>
Derived &smove(Func &&func, const std::string &source,
               const std::string &destination, const std::string &member);      // callback
```

```cpp
auto moved = co_await redis.smove("inbox", "archive", "msg:7");
if (moved.ok() && moved.result()) { /* relocated */ }
```

<!-- src: qbm/redis/commands/set_commands.h:468-501 -->

### `SPOP key` / `SPOP key count` — `spop`

Removes and returns random members. The single-member form returns `std::optional<std::string>` (`std::nullopt` when the
set is empty); the count form returns a `std::vector<std::string>` of up to `count` removed members.

```cpp
auto spop(const std::string &key);                                             // coroutine, single
auto spop(const std::string &key, long long count);                            // coroutine, count

template <typename Func> Derived &spop(Func &&func, const std::string &key);    // callback, single
template <typename Func>
Derived &spop(Func &&func, const std::string &key, long long count);            // callback, count
```

A `count < 1` is rejected by the callback form (it returns `Derived&` without issuing the command —
see [Pitfalls](#pitfalls)).

```cpp
auto one = co_await redis.spop("deck");
if (one.ok() && one.result().has_value())
    std::cout << "drew " << *one.result() << '\n';

auto hand = co_await redis.spop("deck", 2);
if (hand.ok()) std::cout << hand.result().size() << " cards\n";  // up to 2
```

<!-- src: qbm/redis/commands/set_commands.h:503-565; qbm/redis/tests/integration/set/set-commands.cpp:151-183 -->

### `SRANDMEMBER key` / `SRANDMEMBER key count` — `srandmember`

Returns random members **without** removing them. The single form returns `std::optional<std::string>`; the count form
returns a `std::vector<std::string>`. A negative `count` allows repeats and a result longer than the set (standard Redis
semantics).

```cpp
auto srandmember(const std::string &key);                                      // coroutine, single
auto srandmember(const std::string &key, long long count);                     // coroutine, count

template <typename Func>
Derived &srandmember(Func &&func, const std::string &key);                      // callback, single
template <typename Func>
Derived &srandmember(Func &&func, const std::string &key, long long count);     // callback, count
```

```cpp
auto pick = co_await redis.srandmember("deck");
if (pick.ok() && pick.result().has_value()) { /* *pick.result() */ }

auto sample = co_await redis.srandmember("deck", 3);
if (sample.ok()) { /* sample.result() — up to 3, set unchanged */ }
```

<!-- src: qbm/redis/commands/set_commands.h:567-631; qbm/redis/tests/integration/set/set-commands.cpp:202-234 -->

### `SDIFF key [key ...]` — `sdiff`

Returns the members in the first set but in none of the rest, as `std::vector<std::string>`.

```cpp
auto sdiff(const std::vector<std::string> &keys);                              // coroutine
template <typename Func>
Derived &sdiff(Func &&func, const std::vector<std::string> &keys);              // callback
```

```cpp
auto only_in_a = co_await redis.sdiff({"a", "b"});   // a − b
if (only_in_a.ok()) { /* only_in_a.result() */ }
```

<!-- src: qbm/redis/commands/set_commands.h:191-222 -->

### `SDIFFSTORE destination key [key ...]` — `sdiffstore`

Computes the same difference and stores it in `destination` (overwriting it). Returns the cardinality of the stored set
as `long long`.

```cpp
auto sdiffstore(const std::string &destination, const std::vector<std::string> &keys); // coroutine
template <typename Func>
Derived &sdiffstore(Func &&func, const std::string &destination,
                    const std::vector<std::string> &keys);                     // callback
```

```cpp
auto n = co_await redis.sdiffstore("a_minus_b", {"a", "b"});
if (n.ok()) std::cout << n.result() << " stored\n";
```

<!-- src: qbm/redis/commands/set_commands.h:224-256 -->

### `SINTER key [key ...]` — `sinter`

Returns the intersection of all the given sets as `std::vector<std::string>`.

```cpp
auto sinter(const std::vector<std::string> &keys);                             // coroutine
template <typename Func>
Derived &sinter(Func &&func, const std::vector<std::string> &keys);             // callback
```

```cpp
auto common = co_await redis.sinter({"a", "b"});
if (common.ok()) { /* common.result() */ }
```

<!-- src: qbm/redis/commands/set_commands.h:258-290 -->

### `SINTERSTORE destination key [key ...]` — `sinterstore`

Computes the intersection and stores it in `destination`. Returns the cardinality of the stored set.

```cpp
auto sinterstore(const std::string &destination, const std::vector<std::string> &keys); // coroutine
template <typename Func>
Derived &sinterstore(Func &&func, const std::string &destination,
                     const std::vector<std::string> &keys);                    // callback
```

<!-- src: qbm/redis/commands/set_commands.h:331-362 -->

### `SINTERCARD numkeys key [key ...] [LIMIT limit]` — `sintercard`

Returns the cardinality of the intersection **without** materializing it. When you pass a `limit`, the client appends
`LIMIT <limit>` so the server can stop counting early; pass `std::nullopt` (the default) to omit the clause. The client
emits the clause whenever the optional holds a value — it does **not** special-case `0` — so leave `limit` unset rather
than passing `0LL` when you want no cap. The number of keys is supplied to the server automatically.

```cpp
auto sintercard(const std::vector<std::string> &keys,
                std::optional<long long> limit = std::nullopt);                // coroutine
template <typename Func>
Derived &sintercard(Func &&func, const std::vector<std::string> &keys,
                    std::optional<long long> limit = std::nullopt);            // callback
```

```cpp
auto full = co_await redis.sintercard({"a", "b"});       // exact intersection size
if (full.ok()) std::cout << full.result() << '\n';

auto capped = co_await redis.sintercard({"a", "b"}, 2LL); // stop once it reaches 2
if (capped.ok()) std::cout << capped.result() << '\n';    // <= 2
```

<!-- src: qbm/redis/commands/set_commands.h:292-329; qbm/redis/tests/integration/set/set-commands.cpp:299-301, 353-367 -->

### `SUNION key [key ...]` — `sunion`

Returns the union of all the given sets as `std::vector<std::string>`.

```cpp
auto sunion(const std::vector<std::string> &keys);                             // coroutine
template <typename Func>
Derived &sunion(Func &&func, const std::vector<std::string> &keys);             // callback
```

```cpp
auto all = co_await redis.sunion({"a", "b"});
if (all.ok()) { /* all.result() */ }
```

<!-- src: qbm/redis/commands/set_commands.h:735-768 -->

### `SUNIONSTORE destination key [key ...]` — `sunionstore`

Computes the union and stores it in `destination`. Returns the cardinality of the stored set.

```cpp
auto sunionstore(const std::string &destination, const std::vector<std::string> &keys); // coroutine
template <typename Func>
Derived &sunionstore(Func &&func, const std::string &destination,
                     const std::vector<std::string> &keys);                    // callback
```

<!-- src: qbm/redis/commands/set_commands.h:770-802 -->

### `SSCAN key cursor [MATCH pattern] [COUNT count]` — `sscan`

Incrementally iterates the members of one set. Returns `Reply<scan<>>`, whose `result().cursor` is the next cursor (`0`
ends iteration) and `result().items` is the page (`std::vector<std::string>`). `pattern` defaults to `"*"` and `count` (
a server hint, not a hard page size) defaults to `10`.

```cpp
auto sscan(const std::string &key, long long cursor,
           const std::string &pattern = "*", long long count = 10);            // coroutine
template <typename Func>
Derived &sscan(Func &&func, const std::string &key, long long cursor,
               const std::string &pattern = "*", long long count = 10);        // callback
```

Drive the cursor yourself, re-issuing until it returns to `0`:

```cpp
long long cursor = 0;
do {
    auto page = co_await redis.sscan("tags:42", cursor, "*", 100);
    if (!page.ok()) break;
    for (auto const &m : page.result().items) { /* ... */ }
    cursor = static_cast<long long>(page.result().cursor);
} while (cursor != 0);
```

<!-- src: qbm/redis/commands/set_commands.h:673-708; qbm/redis/tests/integration/set/set-commands.cpp:384-388 -->

### `SSCAN` (auto-iterating, callback-only) — `sscan(func, key, pattern)`

A convenience overload that walks **every** page internally and fires your callback **once** with the fully collected
result. It has **no coroutine form** — it is a callback-only entry point. Internally it spins a `shared_ptr`-managed
`scanner` that keeps itself alive across the cursor round-trips, and it hardcodes a per-page `COUNT` of `100` (you
cannot tune the page size on this overload).

```cpp
template <typename Func>
Derived &sscan(Func &&func, const std::string &key, const std::string &pattern = "*");
```

```cpp
redis.sscan([](qb::redis::Reply<qb::redis::scan<>> &&all) {
    if (all.ok())
        for (auto const &m : all.result().items) { /* every match */ }
}, "tags:42", "tag:*");
```

<!-- src: qbm/redis/commands/set_commands.h:723-731, 42-116 -->

---

## Pitfalls

- **Empty arguments silently no-op the callback form.** The callback overloads return `Derived&` *without issuing a
  command* — so your callback never fires — when required arguments are empty: `sadd` / `srem` / `smismember` when `key`
  is empty or no members are passed; `sismember` when `key` or `member` is empty; `smove` when any of `source` /
  `destination` / `member` is empty; `scard` / `smembers` / `spop` / `srandmember` / `sscan` when `key` is empty;
  `sdiff` / `sinter` / `sunion` and the `*store` / `sintercard` variants when the key list is empty (and the `*store`
  ones when `destination` is empty); `spop(key, count)` when `count < 1`. Do not assume your callback always runs —
  validate inputs
  first. <!-- src: qbm/redis/commands/set_commands.h:153, 183, 217, 251, 285, 319, 358, 392, 431, 462, 497, 528, 561, 593, 627, 665, 704, 726, 761, 798 -->

- **`smembers` returns a set, the algebra returns vectors.** `smembers` yields `qb::unordered_set<std::string>` (no
  index access, unordered); `sdiff` / `sinter` / `sunion` yield `std::vector<std::string>`. Pick the right container in
  your callback signature or the overload will not match. <!-- src: qbm/redis/commands/set_commands.h:445, 200, 268, 744 -->

- **`spop`/`srandmember` count forms return `std::vector<std::string>`, not optionals.** Only the single-member forms
  return `std::optional<std::string>`. Reaching for `std::vector<std::optional<std::string>>` (as older docs showed)
  will fail to compile. <!-- src: qbm/redis/commands/set_commands.h:543, 609, 511, 576 -->

- **The auto-iterating `sscan` is callback-only and fixes `COUNT` at 100.** There is no `co_await` form of the no-cursor
  `sscan`, and its page size is not user-tunable. For a tunable page size or a coroutine flow, drive the cursor-form
  `sscan` yourself. <!-- src: qbm/redis/commands/set_commands.h:723-731, 82 -->

- **`smembers` materializes the whole set.** On a large set this builds one bulk reply server-side; iterate with `sscan`
  instead to bound memory and avoid stalling the server.

---

## See also

- [sorted_set_commands.md](./sorted_set_commands.md) — the scored counterpart (`ZADD`, `ZRANGE`, `ZSCAN`, …).
- [hash_commands.md](./hash_commands.md) — field/value maps under one key, same overload model.
- [key_commands.md](./key_commands.md) — `DEL`, `EXISTS`, `EXPIRE` and the key-level TTL boundary.
- [commands_overview.md](./commands_overview.md) — the full command surface and how the CRTP mixins compose.
- [error_handling.md](./error_handling.md) — interpreting `reply.ok()` / `reply.error()`.
- [pipeline_and_await.md](./pipeline_and_await.md) — batching commands and the coroutine/await model.
