# List commands

> **Audience:** Adopter · **Status:** stable · **Verified-against:** qbm-redis @ qb 3.0.0 (C++20 default, C++23
> supported)

Reference for the Redis List command group — head/tail pushes and pops, indexed access, range and trim operations,
element moves between lists, blocking variants, and positional search — each with its exact signature, reply type, and a
minimal `co_await` and callback snippet.

**Prerequisites:** [Command API model](./commands_overview.md) · [Connection](./connection.md) — **See also:
** [Key commands](./key_commands.md) · [String commands](./string_commands.md) · [Error handling](./error_handling.md) · [Pipelining and
`await()`](./pipeline_and_await.md)

## Summary

The list commands are defined by the `qb::redis::list_commands<Derived>` CRTP mixin (`list_commands.h:36`), which
`qb::redis::tcp::client` inherits along with every other command group (`redis.h:769`, `redis.h:1695`). You call these
methods directly on a connected client. Each command exists in two forms that share one method name: a **coroutine**
form you `co_await` to get a `qb::redis::Reply<T>`, and a **callback** form that takes the handler as its first argument
and returns the client for chaining. The dispatch and the `Reply<T>` decoding contract are covered
in [Command API model](./commands_overview.md); this page lists the methods.

Redis lists are doubly linked lists, so head/tail pushes and pops (`LPUSH`, `RPUSH`, `LPOP`, `RPOP`) are O(1), while
index-based access (`LINDEX`) and range scans (`LRANGE`, `LPOS`) are O(N). This is a Redis storage property, not a
property of this client.

One time-related note for this group: blocking commands take a `timeout` in **seconds**, which is the native Redis
protocol unit. Each blocking command exposes both a raw `long long` (seconds) form and a `std::chrono::seconds` overload
that forwards `.count()` (`list_commands.h:340`, `:392`). These are protocol seconds, **not** `qb::duration`; do not
substitute qb time types here.

## Concepts

### Reply types you will see in this group

| Reply `T`                                                         | Meaning                                                 | Commands                                                                                    |
|-------------------------------------------------------------------|---------------------------------------------------------|---------------------------------------------------------------------------------------------|
| `long long`                                                       | a list length or a removed/insert count                 | `lpush`, `lpushx`, `rpush`, `rpushx`, `llen`, `linsert`, `lrem`                             |
| `qb::redis::status`                                               | a `+OK` status reply                                    | `lset`, `ltrim`                                                                             |
| `std::optional<std::string>`                                      | a single element, absent when the list is empty/missing | `lpop`/`rpop` (single-element form), `lindex`, `lmove`, `rpoplpush`, `blmove`, `brpoplpush` |
| `std::vector<std::string>`                                        | the popped or ranged elements                           | `lpop`/`rpop` (count form), `lrange`                                                        |
| `std::vector<long long>`                                          | matched positions                                       | `lpos`                                                                                      |
| `std::optional<std::pair<std::string, std::string>>`              | `{key, element}` from a blocking pop                    | `blpop`, `brpop`                                                                            |
| `std::optional<std::pair<std::string, std::vector<std::string>>>` | `{key, elements}` from a multi-key pop                  | `lmpop`, `blmpop`                                                                           |

`qb::redis::Reply<T>` (`reply.h:1102`) exposes `ok()`, `result()` (alias `value()`), `value_or(default)`, `error()`, and
an explicit `operator bool()`. Optional replies are tested with `reply.result().has_value()`.
See [Command API model](./commands_overview.md) for the full reply surface.

> There is no `list_pop_result` or `list_move_result` type in this client. Multi-key pops decode to
`std::optional<std::pair<std::string, std::vector<std::string>>>`; blocking single pops (`blpop`/`brpop`) decode to
`std::optional<std::pair<std::string, std::string>>`. Treat the pair's `.first` as the key the element came from and
`.second` as the element(s).

### Enums

Two enums from `types.h` parameterize this group:

- `qb::redis::InsertPosition` (`types.h:51`) — `BEFORE`, `AFTER` — selects where `linsert` places the element relative
  to the pivot.
- `qb::redis::ListPosition` (`types.h:53`) — `LEFT`, `RIGHT` — selects the list end for `lmove`, `blmove`, `lmpop`, and
  `blmpop`.

Both are serialized to wire keywords through `to_string(...)` (`types.h:558`, `:560`); you pass the enum, not a string.

### Variadic pushes

`lpush`, `lpushx`, `rpush`, and `rpushx` are variadic in both forms — you pass one or more elements as separate
arguments (`list_commands.h:87`, `:118`, `:149`, `:180`). There is no separate "vector" overload and no
single-value-only restriction; `rpushx("k", "a", "b", "c")` is valid.

## Setup for the examples

```cpp
#include <qbm/redis/redis.h>                 // namespace qb::redis
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>

using namespace std::chrono_literals;    // for 5s timeouts, etc.

// Inside a coroutine:
qb::redis::tcp::client redis{qb::io::uri{"tcp://localhost:6379"}};
if (!co_await redis.connect())
    co_return;
```

<!-- src: qbm/redis/readme/connection.md -->

Each command below shows its coroutine signature, its callback overload, and a snippet. The callback form always returns
`Derived&` (the client) for chaining and is SFINAE-gated on the callback accepting `Reply<T>&&` (`list_commands.h:65`).

## Length

### `llen`

`LLEN key` — the number of elements in the list (0 if the key is missing).

```cpp
// Coroutine
auto llen(const std::string &key);                                  // -> Reply<long long>
// Callback
template <typename Func> Derived &llen(Func &&, const std::string &key);
```

<!-- src: qbm/redis/src/qbm/redis/commands/list_commands.h:53,66 -->

```cpp
auto r = co_await redis.llen("queue");
if (r.ok())
    qb::io::cout() << "length: " << r.result() << "\n";
```

## Pushes

### `lpush`, `rpush`

`LPUSH key element [element ...]` / `RPUSH key element [element ...]` — prepend (`lpush`) or append (`rpush`) one or
more elements, returning the list length after the push. Both are variadic.

```cpp
// Coroutine
template <typename... Args> auto lpush(const std::string &key, Args &&...args);   // -> Reply<long long>
template <typename... Args> auto rpush(const std::string &key, Args &&...args);   // -> Reply<long long>
// Callback
template <typename Func, typename... Args> Derived &lpush(Func &&, const std::string &key, Args &&...args);
template <typename Func, typename... Args> Derived &rpush(Func &&, const std::string &key, Args &&...args);
```

<!-- src: qbm/redis/src/qbm/redis/commands/list_commands.h:81,139,97,155 -->

```cpp
auto len = co_await redis.rpush("queue", "job1", "job2", "job3");
// len.result() == new length

// Callback form
redis.lpush([](qb::redis::Reply<long long> &&r) { /* r.result() */ }, "queue", "urgent");
```

### `lpushx`, `rpushx`

`LPUSHX key element [element ...]` / `RPUSHX key element [element ...]` — like `lpush`/`rpush`, but a no-op (returns 0)
when the key does not already hold a list. Both are variadic in both the coroutine and callback forms.

```cpp
// Coroutine
template <typename... Args> auto lpushx(const std::string &key, Args &&...args);  // -> Reply<long long>
template <typename... Args> auto rpushx(const std::string &key, Args &&...args);  // -> Reply<long long>
// Callback
template <typename Func, typename... Args> Derived &lpushx(Func &&, const std::string &key, Args &&...args);
template <typename Func, typename... Args> Derived &rpushx(Func &&, const std::string &key, Args &&...args);
```

<!-- src: qbm/redis/src/qbm/redis/commands/list_commands.h:110,168,126,184 -->

```cpp
auto r = co_await redis.rpushx("queue", "job4");
// r.result() == 0 if "queue" did not exist
```

## Pops

### `lpop`, `rpop`

`LPOP key [count]` / `RPOP key [count]` — remove and return element(s) from the head (`lpop`) or tail (`rpop`). Each
verb has two coroutine overloads and two matching callback overloads: a **single-element** form returning
`std::optional<std::string>`, and a **count** form returning `std::vector<std::string>`.

```cpp
// Coroutine — single element
auto lpop(const std::string &key);                                  // -> Reply<std::optional<std::string>>
auto rpop(const std::string &key);                                  // -> Reply<std::optional<std::string>>
// Coroutine — count
auto lpop(const std::string &key, long long count);                 // -> Reply<std::vector<std::string>>
auto rpop(const std::string &key, long long count);                 // -> Reply<std::vector<std::string>>
// Callback — single element
template <typename Func> Derived &lpop(Func &&, const std::string &key);
template <typename Func> Derived &rpop(Func &&, const std::string &key);
// Callback — count
template <typename Func> Derived &lpop(Func &&, const std::string &key, long long count);
template <typename Func> Derived &rpop(Func &&, const std::string &key, long long count);
```

<!-- src: qbm/redis/src/qbm/redis/commands/list_commands.h:228,284,200,256,242,298,217,273 -->

```cpp
auto one = co_await redis.lpop("queue");
if (one.result().has_value())
    qb::io::cout() << "popped: " << *one.result() << "\n";

auto many = co_await redis.rpop("queue", 3);
// many.result() is a std::vector<std::string>
```

## Indexed access and manipulation

### `lindex`

`LINDEX key index` — the element at `index` (0-based; negative counts from the tail, `-1` is the last element). Absent
when the index is out of range.

```cpp
// Coroutine
auto lindex(const std::string &key, long long index);               // -> Reply<std::optional<std::string>>
// Callback
template <typename Func> Derived &lindex(Func &&, const std::string &key, long long index);
```

<!-- src: qbm/redis/src/qbm/redis/commands/list_commands.h:419,434 -->

```cpp
auto r = co_await redis.lindex("queue", -1);
// r.result() == last element, or std::nullopt
```

### `linsert`

`LINSERT key BEFORE|AFTER pivot element` — insert `val` before or after the first occurrence of `pivot`. Returns the new
length, `0` if the key is missing, or `-1` if the pivot is not found.

```cpp
// Coroutine
auto linsert(const std::string &key, InsertPosition position,
             const std::string &pivot, const std::string &val);     // -> Reply<long long>
// Callback
template <typename Func> Derived &linsert(Func &&, const std::string &key, InsertPosition position,
                                          const std::string &pivot, const std::string &val);
```

<!-- src: qbm/redis/src/qbm/redis/commands/list_commands.h:448,465 -->

```cpp
auto r = co_await redis.linsert("queue", qb::redis::InsertPosition::BEFORE, "job2", "job1.5");
```

### `lset`

`LSET key index element` — overwrite the element at `index`. Returns a `status` (`+OK` on success). Errors if the index
is out of range.

```cpp
// Coroutine
auto lset(const std::string &key, long long index, const std::string &val);   // -> Reply<status>
// Callback
template <typename Func> Derived &lset(Func &&, const std::string &key, long long index, const std::string &val);
```

<!-- src: qbm/redis/src/qbm/redis/commands/list_commands.h:538,554 -->

```cpp
auto r = co_await redis.lset("queue", 0, "rewritten");
if (r.result())  // status converts to bool: true when "OK"
    qb::io::cout() << "set\n";
```

### `lrem`

`LREM key count element` — remove occurrences of `val`. `count > 0` removes from head to tail, `count < 0` from tail to
head, `count == 0` removes all. Returns the number removed.

```cpp
// Coroutine
auto lrem(const std::string &key, long long count, const std::string &val);   // -> Reply<long long>
// Callback
template <typename Func> Derived &lrem(Func &&, const std::string &key, long long count, const std::string &val);
```

<!-- src: qbm/redis/src/qbm/redis/commands/list_commands.h:508,525 -->

```cpp
auto r = co_await redis.lrem("queue", 0, "done");
// r.result() == number of "done" entries removed
```

## Range and trim

### `lrange`

`LRANGE key start stop` — the elements between `start` and `stop` inclusive (both 0-based, negatives count from the
tail). `0 -1` returns the whole list.

```cpp
// Coroutine
auto lrange(const std::string &key, long long start, long long stop);   // -> Reply<std::vector<std::string>>
// Callback
template <typename Func> Derived &lrange(Func &&, const std::string &key, long long start, long long stop);
```

<!-- src: qbm/redis/src/qbm/redis/commands/list_commands.h:478,494 -->

```cpp
auto all = co_await redis.lrange("queue", 0, -1);
for (const auto &e : all.result())
    qb::io::cout() << e << "\n";
```

### `ltrim`

`LTRIM key start stop` — keep only the elements in `[start, stop]`, discarding the rest. Returns a `status`.

```cpp
// Coroutine
auto ltrim(const std::string &key, long long start, long long stop);   // -> Reply<status>
// Callback
template <typename Func> Derived &ltrim(Func &&, const std::string &key, long long start, long long stop);
```

<!-- src: qbm/redis/src/qbm/redis/commands/list_commands.h:567,583 -->

```cpp
// Keep only the newest 100 entries
auto r = co_await redis.ltrim("queue", -100, -1);
```

## Moving elements between lists

### `lmove`

`LMOVE source destination LEFT|RIGHT LEFT|RIGHT` — atomically pop one element from a chosen end of `source` and push it
onto a chosen end of `destination`, returning the moved element (`std::nullopt` if `source` is empty).

```cpp
// Coroutine
auto lmove(const std::string &source, const std::string &destination,
           ListPosition wherefrom, ListPosition whereto);           // -> Reply<std::optional<std::string>>
// Callback
template <typename Func> Derived &lmove(Func &&, const std::string &source, const std::string &destination,
                                        ListPosition wherefrom, ListPosition whereto);
```

<!-- src: qbm/redis/src/qbm/redis/commands/list_commands.h:629,648 -->

```cpp
using qb::redis::ListPosition;
auto moved = co_await redis.lmove("pending", "processing", ListPosition::LEFT, ListPosition::RIGHT);
```

### `rpoplpush`

`RPOPLPUSH source destination` — atomically pop from the tail of `source` and push to the head of `destination`. Returns
the moved element, or `std::nullopt` when `source` is empty.

```cpp
// Coroutine
auto rpoplpush(const std::string &source, const std::string &destination);   // -> Reply<std::optional<std::string>>
// Callback
template <typename Func> Derived &rpoplpush(Func &&, const std::string &source, const std::string &destination);
```

<!-- src: qbm/redis/src/qbm/redis/commands/list_commands.h:598,614 -->

```cpp
auto moved = co_await redis.rpoplpush("pending", "processing");
```

### `lmpop`

`LMPOP numkeys key [key ...] LEFT|RIGHT [COUNT count]` — pop up to `count` elements from the chosen end of the first
non-empty list among `keys`. Returns `{key, elements}` or `std::nullopt` when all are empty.

```cpp
// Coroutine
auto lmpop(const std::vector<std::string> &keys, ListPosition position, long long count = 1);
// -> Reply<std::optional<std::pair<std::string, std::vector<std::string>>>>
// Callback
template <typename Func> Derived &lmpop(Func &&, const std::vector<std::string> &keys,
                                        ListPosition position, long long count = 1);
```

<!-- src: qbm/redis/src/qbm/redis/commands/list_commands.h:664,681 -->

```cpp
auto r = co_await redis.lmpop({"q1", "q2"}, qb::redis::ListPosition::LEFT, 2);
if (r.result().has_value()) {
    const auto &[key, elements] = *r.result();
    qb::io::cout() << "popped " << elements.size() << " from " << key << "\n";
}
```

> An empty `keys` sends no command, but the callback still fires — with `ok() == false` and a reason in
`error()` (`list_commands.h:699`). It used to return unfired; on the coroutine form that parked the awaiter
forever. Branch on `ok()`, not on whether your callback ran.

## Positional search

### `lpos`

`LPOS key element [RANK rank] [COUNT count] [MAXLEN maxlen]` — find the index/indices of `element`. There is a single
overload that always returns a vector of positions (`list_commands.h:870-871` always sends `COUNT`, defaulting to `0` = all
matches when `count` is `std::nullopt`); for a single-position lookup, read `result().front()` after checking the vector
is non-empty.

The optional arguments are, in order, `rank`, `count`, then `maxlen`, matching the wire order `[RANK] [COUNT] [MAXLEN]`
shown above. `COUNT` is always emitted (defaulting to `0` = all matches when `count` is `std::nullopt`); `RANK` and
`MAXLEN` are emitted only when supplied (`list_commands.h:872-874`).

```cpp
// Coroutine
auto lpos(const std::string &key, const std::string &element,
          std::optional<long long> rank   = std::nullopt,
          std::optional<long long> count  = std::nullopt,
          std::optional<long long> maxlen = std::nullopt);          // -> Reply<std::vector<long long>>
// Callback
template <typename Func> Derived &lpos(Func &&, const std::string &key, const std::string &element,
                                       std::optional<long long> rank   = std::nullopt,
                                       std::optional<long long> count  = std::nullopt,
                                       std::optional<long long> maxlen = std::nullopt);
```

<!-- src: qbm/redis/src/qbm/redis/commands/list_commands.h:816,836 -->

```cpp
auto r = co_await redis.lpos("queue", "job2");
if (r.ok() && !r.result().empty())
    qb::io::cout() << "first match at " << r.result().front() << "\n";

// All matches, scanning at most 1000 elements:
auto all = co_await redis.lpos("queue", "job2", std::nullopt, 0, 1000);
```

> Like `lmpop`, an empty `key` or `element` sends no command and resolves with `ok() == false`
> (`list_commands.h:860`) — the callback and the awaiter both run.

## Blocking operations

Blocking commands park the *connection* until an element is available or the timeout elapses. The `timeout` is in *
*seconds** as a `long long`, with a `0` value meaning block indefinitely; each blocking pop also offers a
`std::chrono::seconds` overload that forwards `.count()`. These are native Redis protocol seconds, not `qb::duration` —
see [Connection](./connection.md) for how the framework's own connect/command timeouts (which *are* `qb::duration`)
differ. Use the coroutine form so a blocked command suspends the actor's coroutine rather than stalling the event loop.

### `blpop`, `brpop`

`BLPOP key [key ...] timeout` / `BRPOP key [key ...] timeout` — block until an element can be popped from the head (
`blpop`) or tail (`brpop`) of one of `keys`. Returns `{key, element}` identifying which list served the element, or
`std::nullopt` on timeout.

```cpp
// Coroutine — seconds (0 = block forever)
auto blpop(const std::vector<std::string> &keys, long long timeout = 0);
auto brpop(const std::vector<std::string> &keys, long long timeout = 0);
// -> Reply<std::optional<std::pair<std::string, std::string>>>
// Coroutine — std::chrono::seconds overload
auto blpop(const std::vector<std::string> &keys, const std::chrono::seconds &timeout);
auto brpop(const std::vector<std::string> &keys, const std::chrono::seconds &timeout);
// Callback — both timeout forms
template <typename Func> Derived &blpop(Func &&, const std::vector<std::string> &keys, long long timeout);
template <typename Func> Derived &blpop(Func &&, const std::vector<std::string> &keys, const std::chrono::seconds &timeout);
```

<!-- src: qbm/redis/src/qbm/redis/commands/list_commands.h:312,364,339,391,327,379,352,405 -->

```cpp
auto r = co_await redis.blpop({"high", "low"}, 5s);   // 5-second block
if (r.result().has_value()) {
    const auto &[key, element] = *r.result();
    qb::io::cout() << "got " << element << " from " << key << "\n";
} else {
    qb::io::cout() << "timed out\n";
}
```

### `blmove`

`BLMOVE source destination LEFT|RIGHT LEFT|RIGHT timeout` — the blocking variant of `lmove`. Returns the moved element,
or `std::nullopt` on timeout. `timeout` is `long long` seconds.

```cpp
// Coroutine
auto blmove(const std::string &source, const std::string &destination,
            ListPosition wherefrom, ListPosition whereto, long long timeout);   // -> Reply<std::optional<std::string>>
// Callback
template <typename Func> Derived &blmove(Func &&, const std::string &source, const std::string &destination,
                                         ListPosition wherefrom, ListPosition whereto, long long timeout);
```

<!-- src: qbm/redis/src/qbm/redis/commands/list_commands.h:746,766 -->

```cpp
using qb::redis::ListPosition;
auto moved = co_await redis.blmove("pending", "processing", ListPosition::LEFT, ListPosition::RIGHT, 5);
```

### `blmpop`

`BLMPOP timeout numkeys key [key ...] LEFT|RIGHT [COUNT count]` — the blocking variant of `lmpop`. Returns
`{key, elements}` or `std::nullopt` on timeout. `timeout` is `long long` seconds.

```cpp
// Coroutine
auto blmpop(const std::vector<std::string> &keys, ListPosition position,
            long long timeout, long long count = 1);
// -> Reply<std::optional<std::pair<std::string, std::vector<std::string>>>>
// Callback
template <typename Func> Derived &blmpop(Func &&, const std::vector<std::string> &keys,
                                         ListPosition position, long long timeout, long long count = 1);
```

<!-- src: qbm/redis/src/qbm/redis/commands/list_commands.h:705,723 -->

```cpp
auto r = co_await redis.blmpop({"q1", "q2"}, qb::redis::ListPosition::LEFT, 5, 2);
```

> The `blmpop` callback overload behaves identically on an empty `keys` — no command, but a resolved
> failed reply; the coroutine form forwards to it (`list_commands.h:724`).

### `brpoplpush` (deprecated)

`BRPOPLPUSH source destination timeout` — the blocking variant of `rpoplpush`. **Deprecated in this client** in favor of
`blmove` (`list_commands.h:779`); it remains in the surface for backward compatibility. Prefer `blmove` with
`ListPosition::RIGHT, ListPosition::LEFT`.

```cpp
// Coroutine (deprecated)
auto brpoplpush(const std::string &source, const std::string &destination, long long timeout);
// -> Reply<std::optional<std::string>>
// Callback (deprecated)
template <typename Func> Derived &brpoplpush(Func &&, const std::string &source,
                                             const std::string &destination, long long timeout);
```

<!-- src: qbm/redis/src/qbm/redis/commands/list_commands.h:783,800 -->

## Pitfalls

- **Pop result type depends on the overload.** `lpop(key)` / `rpop(key)` yield `Reply<std::optional<std::string>>`;
  `lpop(key, count)` / `rpop(key, count)` yield `Reply<std::vector<std::string>>`. Pick the overload by call shape, not
  by a flag.
- **Blocking timeouts are protocol seconds, not `qb::duration`.** Pass `long long` seconds or `std::chrono::seconds`; a
  `0` timeout blocks forever. The framework's connect/command timeouts are a separate, `qb::duration`-typed concern
  documented in [Connection](./connection.md).
- **No callback silently no-ops.** `lmpop`, `blmpop` and `lpos` used to return unfired on an empty required
  argument — a hang on the coroutine form. They now send no command and resolve with `ok() == false`
  (`list_commands.h:699`, `:742`, `:860`), like every other guard in this module.
- **`lpos` has no scalar overload.** It always returns `std::vector<long long>` because it always sends `COUNT` on the
  wire (`list_commands.h:870-871`). For a single position, read `result().front()` after checking the vector is non-empty.
- **`brpoplpush` is deprecated.** New code should use `blmove`.
- **Blocking commands occupy the connection.** A pending `blpop`/`brpop`/`blmove`/`blmpop` ties up the client until it
  resolves; use a dedicated client for long-lived blocking reads rather than sharing one with latency-sensitive traffic.

## See also

- [Command API model](./commands_overview.md) — how the coroutine/callback dispatch and `Reply<T>` decoding work.
- [Connection](./connection.md) — client construction, `connect()`, and the `qb::duration` connect/command timeouts.
- [Key commands](./key_commands.md) — expiry, deletion, and the `EXPIRE`-seconds / `PEXPIRE`-milliseconds unit boundary.
- [Error handling](./error_handling.md) — reading `Reply<T>::error()` and protocol error types.
- [Pipelining and `await()`](./pipeline_and_await.md) — batching commands on one round-trip.
