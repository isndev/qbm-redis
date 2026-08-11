# Bitmap commands

> **Audience:** Adopter · **Status:** stable · **Verified-against:** qbm-redis @ qb 3.0.0 (C++20 default, C++23
> supported)

Reference for the bitmap command group — `BITCOUNT`, `BITFIELD`, `BITFIELD_RO`, `BITOP`, `BITPOS`, `GETBIT`, and
`SETBIT` — which address an ordinary Redis string as an array of bits.

**Prerequisites:** [../README.md](../README.md) (install, `qb_load_modules`,
`qbm::redis`), [connection.md](./connection.md), [commands_overview.md](./commands_overview.md) (the `Reply<T>` model,
coroutine vs. callback forms) — **See also:** [string_commands.md](./string_commands.md) (bitmaps are
strings), [hyperloglog_commands.md](./hyperloglog_commands.md), [error_handling.md](./error_handling.md)

---

## Summary

A Redis bitmap is just a string viewed one bit at a time. There is no dedicated bitmap type on the wire: `SETBIT`/
`GETBIT` flip and read individual bits, `BITCOUNT` and `BITPOS` scan ranges, `BITOP` combines whole strings with bitwise
`AND`/`OR`/`XOR`/`NOT`, and `BITFIELD` reads, writes, and increments packed integer fields of arbitrary width. Because
the underlying value is a string, anything in [string_commands.md](./string_commands.md) (`GET`, `STRLEN`, `EXPIRE`, …)
applies to the same key.

The `bitmap_commands<Derived>` mixin is one of the command groups inherited by `qb::redis::tcp::client`. Every command
is exposed in two forms, both fully asynchronous:

- a **coroutine** form (`auto`-returning) that yields a `Reply<T>` you `co_await`;
- a **callback** form that takes your handler first and returns `Derived&` for chaining.

There is no blocking variant — the older "Sync" signatures are gone. None of these commands carry a time argument, so
the `qb::duration` / native-unit boundary documented for `EXPIRE` in [commands_overview.md](./commands_overview.md) does
**not** apply to this group.

```cpp
#include <qbm/redis/redis.h>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>

qb::io::async::task<void> bitmap_demo(qb::redis::tcp::client &redis) {
    auto key = std::string{"user:42:flags"};

    co_await redis.setbit(key, 7, true);          // Reply<long long>: old bit (0)
    auto bit = co_await redis.getbit(key, 7);     // Reply<long long>: 1
    auto set = co_await redis.bitcount(key);      // Reply<long long>: count of 1-bits

    if (bit && set)                               // Reply<T> is contextually bool (== ok())
        qb::io::cout() << "bits set: " << set.result() << std::endl;
}
```

<!-- src: qbm/redis/tests/integration/bitmap/bitmap-commands.cpp -->

---

## Concepts

### Reply types in this group

| Command                   | `Reply<T>` payload `T`                  | Meaning                                       |
|---------------------------|-----------------------------------------|-----------------------------------------------|
| `bitcount`                | `long long`                             | number of bits set to 1 in the range          |
| `bitpos`                  | `long long`                             | bit position of the first match, or `-1`      |
| `getbit`                  | `long long`                             | the bit value, `0` or `1`                     |
| `setbit`                  | `long long`                             | the **previous** bit value, `0` or `1`        |
| `bitop`                   | `long long`                             | byte length of the string stored at `destkey` |
| `bitfield` / `bitfieldRo` | `std::vector<std::optional<long long>>` | one entry per sub-operation                   |

Two points worth correcting against older notes: `getbit`/`setbit` resolve to `Reply<long long>` carrying `0`/`1`,
**not** `Reply<bool>`; and `bitop` returns the destination string's *length in bytes*, not a status. Read the value
through `reply.result()` once `reply.ok()` (or the contextual `bool`) is true.
See [commands_overview.md](./commands_overview.md) and [error_handling.md](./error_handling.md) for the full `Reply<T>`
contract.

For `bitfield`/`bitfieldRo`, the result vector is positional: element *i* corresponds to the *i*-th sub-command. An
entry is `std::nullopt` when that sub-operation produced no value — most notably when `OVERFLOW FAIL` aborts a write.

### Bit offsets and ranges

`getbit`/`setbit` take a bit `offset` (a `long long`); bit 0 is the most-significant bit of the first byte. `bitcount`
takes inclusive `start`/`end` offsets that default to the whole string (`long long start = 0, long long end = -1` —
`bitmap_commands.h:62,81`); negative values count back from the end. **`bitpos` does not share those defaults**: its
`start`/`end` are `std::optional<long long>` defaulting to `std::nullopt` (`bitmap_commands.h:171,191`), and an unset
optional is dropped from the emitted command altogether. In this binding those offsets are **byte** offsets — the Redis
7+ `BYTE|BIT` modifier is not exposed, so the range is interpreted byte-wise.

`bitpos` returns `-1` when no matching bit exists in the searched range. Omitting `end` leaves the search *open-ended*,
so searching for a `0` bit on an all-ones value returns the first bit of the implicit zero-padding past the stored
bytes; passing an explicit `end` (`-1` included) closes the range and yields `-1` instead. Searching for a `1` bit
returns `-1` when none is found.

### BITFIELD operation tokens

`bitfield` and `bitfieldRo` take the sub-commands as a flat `std::vector<std::string>` whose elements are passed
verbatim to the server, one token per vector entry — for example `{"SET", "u8", "0", "42", "GET", "u8", "0"}`. Field
types are `u`/`i` followed by a width (`u4`, `i32`, …); offsets are absolute (`0`) or type-multiplied with a `#`
prefix (`#0`). These tokens are **not** validated client-side: a malformed type, width, or sub-command surfaces only as
a Redis error in the `Reply` (`reply.ok() == false`, message in `reply.error()`), never as a compile-time check.
`bitfieldRo` accepts only `GET` sub-operations; the server rejects writes.

### BITOP operation strings

`bitop` takes its operation as a raw `std::string` — `"AND"`, `"OR"`, `"XOR"`, or `"NOT"`. A `BitOp` enum and
`qb::redis::to_string(BitOp)` exist (`src/qbm/redis/types.h:59,551`), but this method does **not** use them, so the
spelling is unvalidated until the server rejects it. `"NOT"` is unary: pass exactly one source key. The other
operations accept one or more. The destination length equals the length of the longest input string; shorter inputs are
zero-extended.

---

## Command reference

All signatures below are the public methods of `bitmap_commands<Derived>`. The callback overloads are SFINAE-gated on
`std::is_invocable_v<Func, Reply<T>&&>` for that command's `T`; a handler with the wrong `Reply<T>` signature drops out
of overload resolution, so the call fails to compile (no viable overload) rather than mismatching at runtime.

### `bitcount` — count set bits

```cpp
// coroutine: yields Reply<long long>
auto bitcount(const std::string &key, long long start = 0, long long end = -1);

// callback: returns Derived&
template <typename Func>  // Func invocable with Reply<long long>&&
Derived &bitcount(Func &&func, const std::string &key,
                  long long start = 0, long long end = -1);
```

<!-- src: qbm/redis/src/qbm/redis/commands/bitmap_commands.h:62,81 -->

Counts the bits set to 1 in `key`, optionally restricted to the inclusive byte range `[start, end]`. The reply is the
count.

```cpp
co_await redis.set(key, std::string("\xFF\x00\xFF", 3));  // 11111111 00000000 11111111
auto all  = co_await redis.bitcount(key);        // result() == 16
auto byte0 = co_await redis.bitcount(key, 0, 0); // result() == 8
auto byte1 = co_await redis.bitcount(key, 1, 1); // result() == 0
```

<!-- src: qbm/redis/tests/integration/bitmap/bitmap-commands.cpp:49-77 -->

### `bitpos` — find first 0/1 bit

```cpp
// coroutine: yields Reply<long long>
auto bitpos(const std::string &key, bool bit,
            std::optional<long long> start = std::nullopt,
            std::optional<long long> end   = std::nullopt);

// callback: returns Derived&
template <typename Func>  // Func invocable with Reply<long long>&&
Derived &bitpos(Func &&func, const std::string &key, bool bit,
                std::optional<long long> start = std::nullopt,
                std::optional<long long> end   = std::nullopt);
```

<!-- src: qbm/redis/src/qbm/redis/commands/bitmap_commands.h:170-171,189-192 -->

Returns the position of the first bit equal to `bit` (`true` for 1, `false` for 0), or `-1` if no such bit exists. The
`bool bit` is serialized as `1`/`0` for you.

`start`/`end` are `std::optional<long long>`, not `long long` with `0`/`-1` defaults, and that is load-bearing: an
unset optional is dropped from both the argument count and the payload, so `bitpos(key, false)` emits a bare
`BITPOS key 0` and keeps Redis's open-ended search. The callback overload also refuses to emit `end` without `start`
(Redis reads a lone trailing value as `start`), building the emitted `end` through a guarded assignment rather than a
ternary. <!-- src: qbm/redis/src/qbm/redis/commands/bitmap_commands.h:193-216 -->

```cpp
co_await redis.set(key, std::string("\xFF\x00\xFF", 3));
auto first_one  = co_await redis.bitpos(key, true);   // result() == 0
auto first_zero = co_await redis.bitpos(key, false);  // result() == 8
```

<!-- src: qbm/redis/tests/integration/bitmap/bitmap-commands.cpp:203-245 -->

The open-ended case has its own regression test: on `"\xFF\xFF"`, `bitpos(key, false)` must return `16` (the clear bit
past the string), while `bitpos(key, false, 0, -1)` returns `-1` because the explicit `end` closes the
range. <!-- src: qbm/redis/tests/integration/bitmap/bitmap-commands.cpp:251-274 -->

### `getbit` — read one bit

```cpp
// coroutine: yields Reply<long long>
auto getbit(const std::string &key, long long offset);

// callback: returns Derived&
template <typename Func>  // Func invocable with Reply<long long>&&
Derived &getbit(Func &&func, const std::string &key, long long offset);
```

<!-- src: qbm/redis/src/qbm/redis/commands/bitmap_commands.h:230-231,245-247 -->

Returns the bit at `offset` (`0` or `1`). Offsets past the end of the string read as `0`.

```cpp
auto bit = co_await redis.getbit(key, 7);
if (bit) qb::io::cout() << "bit 7 = " << bit.result() << std::endl;  // 0 or 1
```

<!-- src: qbm/redis/tests/integration/bitmap/bitmap-commands.cpp:277-306 -->

### `setbit` — write one bit

```cpp
// coroutine: yields Reply<long long>
auto setbit(const std::string &key, long long offset, bool value);

// callback: returns Derived&
template <typename Func>  // Func invocable with Reply<long long>&&
Derived &setbit(Func &&func, const std::string &key, long long offset, bool value);
```

<!-- src: qbm/redis/src/qbm/redis/commands/bitmap_commands.h:263-264,279-281 -->

Sets the bit at `offset` to `value` (`true`/`false`) and returns the bit's **previous** value. The string grows to fit
`offset` if necessary, zero-padding the gap.

```cpp
auto prev1 = co_await redis.setbit(key, 7, true);   // result() == 0 (was unset)
auto prev2 = co_await redis.setbit(key, 7, false);  // result() == 1 (was set)
```

<!-- src: qbm/redis/tests/integration/bitmap/bitmap-commands.cpp:277-306 -->

### `bitop` — bitwise operation between strings

```cpp
// coroutine: yields Reply<long long>
auto bitop(const std::string &operation, const std::string &destkey,
           const std::vector<std::string> &keys);

// callback: returns Derived&
template <typename Func>  // Func invocable with Reply<long long>&&
Derived &bitop(Func &&func, const std::string &operation,
               const std::string &destkey, const std::vector<std::string> &keys);
```

<!-- src: qbm/redis/src/qbm/redis/commands/bitmap_commands.h:133,150 -->

Computes `operation` (`"AND"`/`"OR"`/`"XOR"`/`"NOT"`) across the source `keys` and stores the result in `destkey`,
returning that string's length in bytes. `"NOT"` requires exactly one source key.

```cpp
co_await redis.set(key1, std::string("\xFF\x00\xFF", 3));
co_await redis.set(key2, "\x0F\xF0");

auto len = co_await redis.bitop("AND", destkey,
                                std::vector<std::string>{key1, key2});
auto out = co_await redis.get(destkey);   // destkey holds key1 AND key2
```

<!-- src: qbm/redis/tests/integration/bitmap/bitmap-commands.cpp:139-200 -->

### `bitfield` — packed integer fields

```cpp
// coroutine: yields Reply<std::vector<std::optional<long long>>>
auto bitfield(const std::string &key, const std::vector<std::string> &operations);

// callback: returns Derived&
template <typename Func>  // Func invocable with Reply<std::vector<std::optional<long long>>>&&
Derived &bitfield(Func &&func, const std::string &key,
                  const std::vector<std::string> &operations);
```

<!-- src: qbm/redis/src/qbm/redis/commands/bitmap_commands.h:99,115 -->

Runs a sequence of `GET`/`SET`/`INCRBY` sub-operations (with an optional `OVERFLOW WRAP|SAT|FAIL` directive) over packed
integer fields. The reply vector has one positional entry per value-producing sub-operation; an entry is `std::nullopt`
when none was produced (for example under `OVERFLOW FAIL`). Tokens are passed verbatim and not validated client-side.

```cpp
auto reply = co_await redis.bitfield(key, {"SET", "u4", "0", "100",
                                           "GET", "u4", "0"});
if (reply) {
    auto &results = reply.result();          // std::vector<std::optional<long long>>
    if (results[1].has_value())
        qb::io::cout() << "field = " << *results[1] << std::endl;  // 4 (100 mod 16)
}
```

<!-- src: qbm/redis/tests/integration/bitmap/bitmap-commands.cpp:80-107 -->

### `bitfieldRo` — read-only `BITFIELD_RO`

```cpp
// coroutine: yields Reply<std::vector<std::optional<long long>>>
auto bitfieldRo(const std::string &key, const std::vector<std::string> &operations);

// callback: returns Derived&
template <typename Func>  // Func invocable with Reply<std::vector<std::optional<long long>>>&&
Derived &bitfieldRo(Func &&func, const std::string &key,
                    const std::vector<std::string> &operations);
```

<!-- src: qbm/redis/src/qbm/redis/commands/bitmap_commands.h:296-297,312-314 -->

The read-only variant of `bitfield`: only `GET` sub-operations are valid, which makes it safe to route to read replicas.
The reply shape matches `bitfield`.

```cpp
co_await redis.bitfield(key, {"SET", "u8", "0", "42"});
auto reply = co_await redis.bitfieldRo(key, {"GET", "u8", "0"});
// reply.result()[0].value() == 42
```

<!-- src: qbm/redis/tests/integration/bitmap/bitmap-commands.cpp:110-134 -->

### Callback form

Every command above has a callback overload that takes the handler first and returns `Derived&`. Use it from
non-coroutine code:

```cpp
redis.bitcount([](qb::redis::Reply<long long> &&r) {
    if (r) qb::io::cout() << "set bits: " << r.result() << std::endl;
    else   qb::io::cerr() << "BITCOUNT failed: " << r.error() << std::endl;
}, key);
```

<!-- src: qbm/redis/src/qbm/redis/commands/bitmap_commands.h:81 -->

---

## Pitfalls

- **No blocking API.** These methods are coroutine- or callback-based only. A call without `co_await` (or a callback)
  just builds and queues the command; the result reaches you asynchronously. The `long long bitcount(...)` /
  `bool getbit(...)` "Sync" signatures in older docs do not exist.
- **`getbit`/`setbit` yield `Reply<long long>`, not `Reply<bool>`.** Compare `result()` against `0`/`1`. `setbit`
  returns the *previous* bit, which is the idiom for atomic test-and-set on a flag.
- **`bitop` returns a length, not a status.** A non-error reply carries the destination string's byte length; check
  `reply.ok()` for success, then read `reply.result()` if you need the size.
- **Operation strings and `BITFIELD` tokens are unvalidated.** `bitop("ADN", …)`, a bad field type, or a write under
  `bitfieldRo` all fail only at the server. Always test `reply.ok()` and inspect `reply.error()`; do not assume a
  malformed argument is caught at compile time.
- **Byte-wise ranges only.** `start`/`end` are byte offsets; the `BYTE|BIT` modifier from Redis 7 is not surfaced. To
  address sub-byte ranges, compute bit math yourself or use `BITFIELD`.
- **Do not pass `0, -1` to `bitpos` "for the default".** Its `start`/`end` are `std::optional<long long>` defaulting to
  `std::nullopt` (`bitmap_commands.h:171,191-192`), and an explicit `end` — `-1` included — closes the range. On an
  all-ones value, `bitpos(key, false)` returns the clear bit just past the string while `bitpos(key, false, 0, -1)`
  returns `-1`. Only `bitcount` has the `0`/`-1` defaults (`bitmap_commands.h:62,81`).
- **No time units here.** Nothing in this group takes a duration. TTLs on a bitmap key are set with the string/key
  commands (`EXPIRE` in seconds, `PEXPIRE` in milliseconds) — see [commands_overview.md](./commands_overview.md) for
  that native-unit boundary.

---

## See also

- [string_commands.md](./string_commands.md) — bitmaps are strings; `GET`, `STRLEN`, `SETRANGE`, and TTL commands share
  the key.
- [hyperloglog_commands.md](./hyperloglog_commands.md) — probabilistic cardinality over a similar string-backed
  representation.
- [commands_overview.md](./commands_overview.md) — the `Reply<T>` model, coroutine vs. callback dispatch, and the
  time-unit boundary.
- [error_handling.md](./error_handling.md) — interpreting `reply.ok()`, `reply.error()`, and server-side command errors.
