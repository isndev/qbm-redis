# Hash commands

> **Audience:** Adopter · **Status:** stable · **Verified-against:** qbm-redis @ qb 3.0.0 (C++20 default, C++23
> supported)

Reference for the Redis hash command group exposed by `qb::redis::hash_commands<Derived>` — field/value maps stored
under one key, each command listed with its exact signature, arguments, reply type, and a minimal coroutine and callback
snippet.

**Prerequisites:** [../README.md](../README.md) (install, `qb_load_modules`,
`qbm::redis`), [connection.md](./connection.md), [commands_overview.md](./commands_overview.md) — **See also:
** [string_commands.md](./string_commands.md), [key_commands.md](./key_commands.md), [error_handling.md](./error_handling.md), [pipeline_and_await.md](./pipeline_and_await.md)

---

## Summary

A Redis hash maps string fields to string values under a single key — the natural shape for storing an object's fields
without one key per field. The commands here are defined in `qbm/redis/src/qbm/redis/commands/hash_commands.h` as a CRTP mixin (
`hash_commands<Derived>`) that the client inherits, so you call them directly on a connected `qb::redis::tcp::client`:
`redis.hset(...)`, `redis.hgetall(...)`, and so on. There is no `_async` suffix and no separate sync/async class — the
overload you pick is what selects the calling style.

Each command exposes two overloads:

- a **coroutine** form (no callback argument) that returns an awaiter yielding `Reply<T>` — drive it with `co_await`
  inside a `qb::io::async::task<...>`, or with `qb::io::async::run_sync(...)` from synchronous code;
- a **callback** form whose **first** argument is the handler and which returns `Derived&` for chaining. Your callback
  must accept exactly `Reply<T>&&` for the command's `T`. **Seven** callback overloads across six command names
  (`hdel`, `hkeys`, `hlen`, `hmget`, `hmset`, and *both* `hscan` forms) add an explicit
  `std::enable_if_t<std::is_invocable_v<Func, Reply<T>&&>, …>` gate so the compiler can tell the callback overload apart
  from the coroutine overload (`hash_commands.h:241`, `:427`, `:454`, `:489`, `:530`, `:573`, `:601`); the remaining
  callback overloads disambiguate by argument position and return a plain `Derived &` with no such gate
  (`hash_commands.h:277`, `:307`, `:399`). Either way the requirement on your callback is the same.

<!-- src: qbm/redis/src/qbm/redis/commands/hash_commands.h:221-223,240-242 -->

Two operations are **callback-only** and have no coroutine form: the pattern-only auto-iterating
`hscan(func, key, pattern)`, and the multi-key `hvals(func, std::vector<std::string> keys)` fan-out. Both are covered
below.

**No time-unit boundary applies to this group.** Hash commands carry no TTL or expiry arguments, so none of the
`std::chrono` / `qb::duration` unit concerns from the key and string groups apply here. (Connect and command deadlines
remain `qb::duration` at the client level — see [connection.md](./connection.md).) Integer field increments use native
`long long`; float increments use native `double`.

---

## Reply types at a glance

`Reply<T>` is the uniform envelope (`qbm/redis/src/qbm/redis/reply.h:1102-1177`): `reply.ok()` reports success, `reply.result()` (alias
`reply.value()`) holds the parsed payload, `reply.error()` holds the server error string, and `Reply<T>` is contextually
convertible to `bool` (explicit). Container payloads are **mixed**: `hgetall` (and the default `Out` of `hscan`) uses
a qb-core container, `qb::unordered_map<std::string, std::string>`; `hkeys`, `hvals`, `hmget` and `hget` are all `std::`
types. Read the table below rather than assuming either family.

| Command(s)                                   | Reply payload `T`                                                                       |
|----------------------------------------------|-----------------------------------------------------------------------------------------|
| `hdel`, `hincrby`, `hlen`, `hset`, `hstrlen` | `long long`                                                                             |
| `hincrbyfloat`                               | `double`                                                                                |
| `hexists`, `hsetnx`                          | `bool`                                                                                  |
| `hget`                                       | `std::optional<std::string>`                                                            |
| `hgetall`                                    | `qb::unordered_map<std::string, std::string>`                                           |
| `hkeys`, `hvals`                             | `std::vector<std::string>`                                                              |
| `hmget`                                      | `std::vector<std::optional<std::string>>`                                               |
| `hmset`                                      | `qb::redis::status`                                                                     |
| `hscan` (cursor form)                        | `qb::redis::scan<Out>`, `Out` defaults to `qb::unordered_map<std::string, std::string>` |

<!-- src: qbm/redis/src/qbm/redis/commands/hash_commands.h:223, 350, 440, 621, 742, 382, 261, 682, 291, 320, 412, 771, 470, 511, 552-554; qbm/redis/src/qbm/redis/types.h:476, 534-538 -->

---

## Commands

The snippets assume a connected client inside a coroutine; see [connection.md](./connection.md) for setup.

```cpp
#include <qbm/redis/redis.h>            // namespace qb::redis
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
```

### `HSET key field value` — `hset`

Sets `field` to `value` in the hash at `key`. Returns the number of **new** fields created (0 when overwriting an
existing field).

```cpp
// Coroutine — qbm/redis/src/qbm/redis/commands/hash_commands.h:620-621
auto hset(const std::string &key, const std::string &field, const std::string &val);

// Callback — qbm/redis/src/qbm/redis/commands/hash_commands.h:637-639
template <typename Func>
Derived &hset(Func &&func, const std::string &key, const std::string &field,
              const std::string &val);
```

A `std::pair<std::string, std::string>` overload exists for both forms — `hset(key, {field, value})` — and forwards to
the field/value version (`hash_commands.h:651-652`, `:666-668`).

```cpp
// Coroutine
auto r = co_await redis.hset("user:1", "field1", "value1");
if (r.ok())
    qb::io::cout() << "created " << r.result() << " field(s)\n"; // 1 first time, 0 on overwrite

// Callback
redis.hset([](qb::redis::Reply<long long> &&r) {
    if (r.ok()) { /* r.result() == created count */ }
}, "user:1", "field2", "value2");
```

<!-- src: qbm/redis/tests/integration/hash/hash-commands.cpp:55-60 -->

### `HSETNX key field value` — `hsetnx`

Sets `field` only if it does not already exist. Returns `true` when the field was set, `false` when it already existed.

```cpp
// Coroutine — qbm/redis/src/qbm/redis/commands/hash_commands.h:681-682
auto hsetnx(const std::string &key, const std::string &field, const std::string &val);

// Callback — qbm/redis/src/qbm/redis/commands/hash_commands.h:698-700
template <typename Func>
Derived &hsetnx(Func &&func, const std::string &key, const std::string &field,
                const std::string &val);
```

A `std::pair` overload exists for both forms (`hash_commands.h:712-713`, `:727-729`).

```cpp
// Coroutine
auto first  = co_await redis.hsetnx("user:1", "field1", "value1"); // result() == true
auto second = co_await redis.hsetnx("user:1", "field1", "other");  // result() == false
```

<!-- src: qbm/redis/tests/integration/hash/hash-commands.cpp:238-258 -->

### `HMSET key field value [field value ...]` — `hmset`

Sets multiple field/value pairs in one call. Returns `qb::redis::status` (check `reply.result().ok()`, which is `true`
for `"OK"`).

> `hmset` emits the literal `HMSET` wire command. `HMSET` is deprecated in Redis in favor of variadic `HSET`; the method
> is kept for compatibility. Note the return type differs from `hset`: `hmset` returns `status`, while `hset` returns
`long long`.

```cpp
// Coroutine — qbm/redis/src/qbm/redis/commands/hash_commands.h:509-511
template <typename... FieldValues>
auto hmset(const std::string &key, FieldValues &&...field_values);

// Callback — qbm/redis/src/qbm/redis/commands/hash_commands.h:529-531
template <typename Func, typename... FieldValues>
std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
hmset(Func &&func, const std::string &key, FieldValues &&...field_values);
```

```cpp
// Coroutine — fields and values are passed as a flat argument list
auto r = co_await redis.hmset("user:1", "field1", "value1", "field2", "value2");
if (r.ok() && r.result().ok()) { /* all fields written */ }
```

<!-- src: qbm/redis/tests/integration/hash/hash-commands.cpp:113-115 -->

### `HGET key field` — `hget`

Returns the value of `field`, or an empty `std::optional` (`nil`) when the field or key is absent. Always check
`result().has_value()` before dereferencing.

```cpp
// Coroutine — qbm/redis/src/qbm/redis/commands/hash_commands.h:290-291
auto hget(const std::string &key, const std::string &field);

// Callback — qbm/redis/src/qbm/redis/commands/hash_commands.h:306-308
template <typename Func>
Derived &hget(Func &&func, const std::string &key, const std::string &field);
```

```cpp
// Coroutine
auto r = co_await redis.hget("user:1", "field1");
if (r.ok() && r.result().has_value())
    qb::io::cout() << *r.result() << "\n"; // "value1"

// Callback
redis.hget([](qb::redis::Reply<std::optional<std::string>> &&r) {
    if (r.ok() && r.result()) { /* use *r.result() */ }
}, "user:1", "field1");
```

<!-- src: qbm/redis/tests/integration/hash/hash-commands.cpp:62-76 -->

### `HMGET key field [field ...]` — `hmget`

Returns one optional per requested field, in request order. Missing fields are empty optionals — the result vector
length always matches the field count.

```cpp
// Coroutine — qbm/redis/src/qbm/redis/commands/hash_commands.h:468-470
template <typename... Fields>
auto hmget(const std::string &key, Fields &&...fields);

// Callback — qbm/redis/src/qbm/redis/commands/hash_commands.h:488-490
template <typename Func, typename... Fields>
std::enable_if_t<
    std::is_invocable_v<Func, Reply<std::vector<std::optional<std::string>>> &&>, Derived &>
hmget(Func &&func, const std::string &key, Fields &&...fields);
```

```cpp
// Coroutine — "field4" is absent -> empty optional at index 3
auto r = co_await redis.hmget("user:1", "field1", "field2", "field3", "field4");
if (r.ok()) {
    for (auto const &v : r.result())
        qb::io::cout() << (v ? *v : std::string{"<nil>"}) << "\n";
}
```

<!-- src: qbm/redis/tests/integration/hash/hash-commands.cpp:117-138 -->

### `HGETALL key` — `hgetall`

Returns every field/value pair as a `qb::unordered_map<std::string, std::string>`. An absent key yields an empty map (
still `ok()`).

```cpp
// Coroutine — qbm/redis/src/qbm/redis/commands/hash_commands.h:319-320
auto hgetall(const std::string &key);

// Callback — qbm/redis/src/qbm/redis/commands/hash_commands.h:334-336
template <typename Func>
Derived &hgetall(Func &&func, const std::string &key);
```

```cpp
// Coroutine
auto r = co_await redis.hgetall("user:1");
if (r.ok())
    for (auto const &[field, value] : r.result())
        qb::io::cout() << field << " = " << value << "\n";
```

<!-- src: qbm/redis/tests/integration/hash/hash-commands.cpp:140-148 -->

### `HKEYS key` — `hkeys`

Returns all field names as `std::vector<std::string>`.

```cpp
// Coroutine — qbm/redis/src/qbm/redis/commands/hash_commands.h:411-412
auto hkeys(const std::string &key);

// Callback — qbm/redis/src/qbm/redis/commands/hash_commands.h:426-428
template <typename Func>
std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>, Derived &>
hkeys(Func &&func, const std::string &key);
```

```cpp
auto r = co_await redis.hkeys("user:1");
if (r.ok()) { /* r.result() == {"field1", "field2", ...} */ }
```

### `HVALS key` — `hvals`

Returns all values as `std::vector<std::string>`.

```cpp
// Coroutine — qbm/redis/src/qbm/redis/commands/hash_commands.h:770-771
auto hvals(const std::string &key);

// Callback — qbm/redis/src/qbm/redis/commands/hash_commands.h:785-787
template <typename Func>
Derived &hvals(Func &&func, const std::string &key);
```

```cpp
auto r = co_await redis.hvals("user:1");
if (r.ok()) { /* r.result() == {"value1", "value2", ...} */ }
```

<!-- src: qbm/redis/tests/integration/hash/hash-commands.cpp:280-283 -->

#### `hvals` multi-key fan-out (callback-only)

A second `hvals` callback overload takes a `std::vector<std::string>` of keys and issues one `HVALS` per key *
*concurrently**, then fires your callback **once** with all values concatenated into a single flat
`std::vector<std::string>` — there are no key boundaries in the result. There is **no coroutine form** of this overload.

```cpp
// Callback only — qbm/redis/src/qbm/redis/commands/hash_commands.h:800-802
template <typename Func>
Derived &hvals(Func &&func, std::vector<std::string> keys);
```

```cpp
redis.hvals([](qb::redis::Reply<std::vector<std::string>> &&r) {
    if (r.ok()) { /* values from all hashes, flattened */ }
}, std::vector<std::string>{"user:1", "user:2", "user:3"});
```

Semantics to know (`hash_commands.h:137-209`):

- Success is folded with logical-AND across the per-key replies (`_reply.ok() &= reply.ok()`); one failing key makes the
  whole reply not-ok.
- An **empty** keys vector still completes once, with an ok and empty result.
- The fan-out is built on an internal `shared_ptr`-managed helper that keeps itself alive across the round-trips. An
  exception thrown from **your** callback is caught and logged (`LOG_WARN`), **not** propagated — do not rely on
  callback exceptions surfacing to the caller.

### `HDEL key field [field ...]` — `hdel`

Removes one or more fields. Returns the number of fields actually removed (absent fields are not counted).

```cpp
// Coroutine — qbm/redis/src/qbm/redis/commands/hash_commands.h:221-223
template <typename... Fields>
auto hdel(const std::string &key, Fields &&...fields);

// Callback — qbm/redis/src/qbm/redis/commands/hash_commands.h:240-242
template <typename Func, typename... Fields>
std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
hdel(Func &&func, const std::string &key, Fields &&...fields);
```

```cpp
auto r = co_await redis.hdel("user:1", "field1", "field2");
if (r.ok())
    qb::io::cout() << "removed " << r.result() << " field(s)\n";
```

<!-- src: qbm/redis/tests/integration/hash/hash-commands.cpp:89-91 -->

### `HEXISTS key field` — `hexists`

Returns `true` if `field` exists in the hash, `false` otherwise.

```cpp
// Coroutine — qbm/redis/src/qbm/redis/commands/hash_commands.h:260-261
auto hexists(const std::string &key, const std::string &field);

// Callback — qbm/redis/src/qbm/redis/commands/hash_commands.h:276-278
template <typename Func>
Derived &hexists(Func &&func, const std::string &key, const std::string &field);
```

```cpp
auto r = co_await redis.hexists("user:1", "field1");
if (r.ok() && r.result()) { /* present */ }
```

### `HLEN key` — `hlen`

Returns the number of fields in the hash as `long long` (0 for an absent key).

```cpp
// Coroutine — qbm/redis/src/qbm/redis/commands/hash_commands.h:439-440
auto hlen(const std::string &key);

// Callback — qbm/redis/src/qbm/redis/commands/hash_commands.h:453-455
template <typename Func>
std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
hlen(Func &&func, const std::string &key);
```

```cpp
auto r = co_await redis.hlen("user:1");
if (r.ok()) { /* r.result() == field count */ }
```

### `HSTRLEN key field` — `hstrlen`

Returns the string length of the value stored at `field` as `long long` (0 if the field or key is absent).

```cpp
// Coroutine — qbm/redis/src/qbm/redis/commands/hash_commands.h:741-742
auto hstrlen(const std::string &key, const std::string &field);

// Callback — qbm/redis/src/qbm/redis/commands/hash_commands.h:757-759
template <typename Func>
Derived &hstrlen(Func &&func, const std::string &key, const std::string &field);
```

```cpp
auto r = co_await redis.hstrlen("user:1", "field1");
if (r.ok()) { /* r.result() == length in bytes */ }
```

<!-- src: qbm/redis/tests/integration/hash/hash-commands.cpp:300-308 -->

### `HINCRBY key field increment` — `hincrby`

Increments the integer value of `field` by a signed `long long` and returns the value after the operation. Use a
negative increment to decrement.

```cpp
// Coroutine — qbm/redis/src/qbm/redis/commands/hash_commands.h:349-350
auto hincrby(const std::string &key, const std::string &field, long long increment);

// Callback — qbm/redis/src/qbm/redis/commands/hash_commands.h:366-368
template <typename Func>
Derived &hincrby(Func &&func, const std::string &key, const std::string &field,
                 long long increment);
```

```cpp
(void) co_await redis.hset("user:1", "counter", "0");
auto up   = co_await redis.hincrby("user:1", "counter", 10); // result() == 10
auto down = co_await redis.hincrby("user:1", "counter", -5); // result() == 5
```

<!-- src: qbm/redis/tests/integration/hash/hash-commands.cpp:162-170 -->

### `HINCRBYFLOAT key field increment` — `hincrbyfloat`

Increments the float value of `field` by a `double` and returns the value after the operation. Negative increments
decrement.

```cpp
// Coroutine — qbm/redis/src/qbm/redis/commands/hash_commands.h:381-382
auto hincrbyfloat(const std::string &key, const std::string &field, double increment);

// Callback — qbm/redis/src/qbm/redis/commands/hash_commands.h:398-400
template <typename Func>
Derived &hincrbyfloat(Func &&func, const std::string &key, const std::string &field,
                      double increment);
```

```cpp
auto r = co_await redis.hincrbyfloat("user:1", "balance", 10.5); // result() == 10.5
```

<!-- src: qbm/redis/tests/integration/hash/hash-commands.cpp:172-180 -->

### `HSCAN key cursor [MATCH pattern] [COUNT count]` — `hscan`

Incrementally iterates a hash without blocking the server on a single `HGETALL` of a large key. There are **two**
distinct overload families.

#### Cursor form (coroutine or callback)

You drive the cursor yourself: start at cursor `0`, and repeat until the returned cursor is `0` again. The reply payload
is `qb::redis::scan<Out>` (`types.h:534-538`), a struct with `cursor` (`std::size_t`) and `items` (the container
`Out`). Note that `Out` defaults differently per call site: the `scan` template's own default is
`std::vector<std::string>` (`types.h:534`), but **both** `hscan` overloads re-default it to
`qb::unordered_map<std::string, std::string>` (`hash_commands.h:552`, `:572`), which is what you get here.
`pattern` defaults to `"*"` and `count` defaults to `10` (a server hint, not a hard limit).

```cpp
// Coroutine — qbm/redis/src/qbm/redis/commands/hash_commands.h:552-554
template <typename Out = qb::unordered_map<std::string, std::string>>
auto hscan(const std::string &key, long long cursor,
           const std::string &pattern = "*", long long count = 10);

// Callback — qbm/redis/src/qbm/redis/commands/hash_commands.h:572-574
template <typename Func, typename Out = qb::unordered_map<std::string, std::string>>
std::enable_if_t<std::is_invocable_v<Func, Reply<qb::redis::scan<Out>> &&>, Derived &>
hscan(Func &&func, const std::string &key, long long cursor,
      const std::string &pattern = "*", long long count = 10);
```

```cpp
// Coroutine — manual cursor loop
long long cursor = 0;
do {
    auto r = co_await redis.hscan("user:1", cursor, "*", 10);
    if (!r.ok()) break;
    for (auto const &[field, value] : r.result().items) { /* ... */ }
    cursor = static_cast<long long>(r.result().cursor);
} while (cursor != 0);
```

<!-- src: qbm/redis/tests/integration/hash/hash-commands.cpp:325-335 -->

> The callback cursor form sends nothing when `key` is empty — but it does not go silent: it routes through
> `fail_client` with `"HSCAN requires a non-empty key"` and then returns `derived()`, so the handler runs with
> `ok() == false` and an awaiting coroutine resumes instead of parking (`hash_commands.h:578-581`).

#### Auto-iterating form (callback-only)

The pattern-only overload (no `cursor`) runs the full cursor loop for you and invokes your callback **once** with every
matching field/value pair collected. There is **no coroutine form** of this overload.

```cpp
// Callback only — qbm/redis/src/qbm/redis/commands/hash_commands.h:600-602
template <typename Func, typename Out = qb::unordered_map<std::string, std::string>>
std::enable_if_t<std::is_invocable_v<Func, Reply<qb::redis::scan<Out>> &&>, Derived &>
hscan(Func &&func, const std::string &key, const std::string &pattern = "*");
```

```cpp
redis.hscan([](qb::redis::Reply<qb::redis::scan<>> &&r) {
    if (r.ok())
        for (auto const &item : r.result().items) { /* fully collected */ }
}, "user:1", "*");
```

Behavior to know (`hash_commands.h:56-125`):

- The internal scanner hardcodes a per-call `COUNT` of **100**, independent of any value you might want — the auto form
  gives you no way to tune the page size. (The cursor form's `count` defaults to 10 and is tunable.)
- It is built on a `shared_ptr`-managed helper that keeps itself alive across the async cursor round-trips, so the call
  is safe even though it returns before iteration finishes.
- An exception thrown from **your** callback is caught and logged (`LOG_WARN`), **not** propagated. Do not rely on
  callback exceptions surfacing.

---

## Pitfalls

- **`HMSET` is deprecated; mind its return type.** Prefer variadic `hset` for new writes. If you do use `hmset`, it
  returns `qb::redis::status` (check `result().ok()`), whereas `hset` returns `long long`. The wire semantics differ
  too: `HMSET` sends a distinct command.
- **`HSET` return value is a *new-field* count, not a success flag.** Overwriting an existing field returns `0`, which
  is **not** a failure — check `reply.ok()` for success and read `result()` only to learn how many fields were newly
  created.
- **Always check the optional before dereferencing.** `hget` and each element of `hmget` are `std::optional` precisely
  because a field can be absent; `*r.result()` on a `nil` is undefined behavior. Use `has_value()` or `value_or(...)`.
- **Callback exceptions are swallowed in the fan-out and auto-scan helpers.** The multi-key `hvals` and the pattern-only
  `hscan` log and discard exceptions thrown from your callback. Handle errors inside the callback; do not expect them to
  propagate.
- **The auto-iterating `hscan` page size is fixed at 100.** If you need a different `COUNT`, use the cursor form and run
  the loop yourself.
- **Only the map-shaped replies are qb-core.** `hgetall` yields `qb::unordered_map<std::string, std::string>`
  (`hash_commands.h:320`) and the default `hscan` `Out` is the same (`hash_commands.h:552`) — do not assume
  `std::unordered_map` there. Every other multi-value reply in this group *is* a `std::` type (`hkeys`/`hvals` →
  `std::vector<std::string>`, `hmget` → `std::vector<std::optional<std::string>>`), so the rule does not generalize.
- **The client is not thread-safe.** Drive one client from a single I/O thread / strand; the reply queue and outbound
  pipe are unsynchronized (see [connection.md](./connection.md)).
- **No expiry on fields in this group.** Per-field TTLs (`HEXPIRE` and friends) are not part of this mixin. Set a TTL on
  the whole key with the key commands instead — and note those expiry arguments keep **native** Redis units (`EXPIRE`
  seconds, `PEXPIRE` milliseconds), a boundary documented in [commands_overview.md](./commands_overview.md), not here.

---

## See also

- [string_commands.md](./string_commands.md) — string values and counters
- [key_commands.md](./key_commands.md) — key-level expiry, existence, and renaming
- [commands_overview.md](./commands_overview.md) — reply envelope, the chrono/native time-unit boundary, container types
- [connection.md](./connection.md) — client construction, `connect()`, deadlines as `qb::duration`
- [error_handling.md](./error_handling.md) — `Reply<T>::ok()`, `error()`, and error categories
- [pipeline_and_await.md](./pipeline_and_await.md) — batching callback commands and `await()`
- [Redis hash commands](https://redis.io/commands/?group=hash) — upstream command reference
