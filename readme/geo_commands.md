# Geospatial commands

> **Audience:** Adopter · **Status:** stable · **Verified-against:** qbm-redis @ qb 3.1.0 (C++20 default, C++23
> supported)

Reference for the geospatial command group — `GEOADD`, `GEODIST`, `GEOHASH`, `GEOPOS`, `GEORADIUS`, `GEORADIUSBYMEMBER`,
and `GEOSEARCH` — which store longitude/latitude points in a sorted set and query them by distance.

**Prerequisites:** [../README.md](../README.md) (install, `qb_load_modules`,
`qbm::redis`), [connection.md](./connection.md), [commands_overview.md](./commands_overview.md) (the `Reply<T>` model,
coroutine vs. callback forms) — **See also:** [sorted_set_commands.md](./sorted_set_commands.md) (a geo index *is* a
sorted set), [error_handling.md](./error_handling.md)

---

## Summary

Redis stores geospatial data inside an ordinary sorted set: each member's score is the 52-bit geohash of its
`(longitude, latitude)` point. The commands in this group write points (`GEOADD`), measure the distance between two of
them (`GEODIST`), read back the geohash string (`GEOHASH`) or the decoded coordinates (`GEOPOS`), and find every member
inside a radius from a coordinate (`GEORADIUS`), from an existing member (`GEORADIUSBYMEMBER`), or via the newer
`GEOSEARCH`. Because the index is a sorted set, anything in [sorted_set_commands.md](./sorted_set_commands.md) (`ZCARD`,
`ZREM`, `ZRANGE`, …) applies to the same key.

The `geo_commands<Derived>` mixin is one of the command groups inherited by `qb::redis::tcp::client`. Every command is
exposed in two forms, both fully asynchronous:

- a **coroutine** form (`auto`-returning) that yields a `Reply<T>` you `co_await`;
- a **callback** form that takes your handler first and returns `Derived&` for chaining.

There is no blocking variant — the older "Sync" signatures never existed in this module. None of these commands carry a
`qb::duration`: distances are plain `double` values and the unit is chosen with the `qb::redis::GeoUnit` enum, so the
`qb::duration` / native-unit boundary documented for `EXPIRE` in [commands_overview.md](./commands_overview.md) does *
*not** apply here.

```cpp
#include <qbm/redis/redis.h>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>

qb::io::async::task<void> geo_demo(qb::redis::tcp::client &redis) {
    auto key = std::string{"sicily"};

    co_await redis.geoadd(key, 13.361389, 38.115556, "Palermo",
                                15.087269, 37.502669, "Catania");   // Reply<long long>: 2

    auto km = co_await redis.geodist(key, "Palermo", "Catania",
                                     qb::redis::GeoUnit::KM);        // Reply<std::optional<double>>
    if (km && km.result())                                          // Reply<T> is contextually bool (== ok())
        qb::io::cout() << "distance: " << *km.result() << " km" << std::endl;

    auto near = co_await redis.georadius(key, 15.0, 37.5, 200,
                                         qb::redis::GeoUnit::KM);    // Reply<std::vector<std::string>>
    if (near)
        for (auto &name : near.result())
            qb::io::cout() << "in range: " << name << std::endl;
}
```

<!-- src: qbm/redis/tests/integration/geo/geo-commands.cpp -->

---

## Concepts

### Distance units — `GeoUnit`, not `qb::duration`

A geo distance is a length, not a time, so it never touches the framework time model. The unit is the
`qb::redis::GeoUnit` enum, serialized for the wire by `to_string(GeoUnit)`:

| `GeoUnit`     | wire token | meaning          |
|---------------|------------|------------------|
| `GeoUnit::M`  | `m`        | meters (default) |
| `GeoUnit::KM` | `km`       | kilometers       |
| `GeoUnit::MI` | `mi`       | miles            |
| `GeoUnit::FT` | `ft`       | feet             |

<!-- src: qbm/redis/src/qbm/redis/types.h:80, qbm/redis/src/qbm/redis/redis.cpp:354 -->

`GEODIST`, `GEORADIUS`, `GEORADIUSBYMEMBER`, and `GEOSEARCH` all default to `GeoUnit::M`. The radius argument itself is
a `double` in the chosen unit. Do not reach for `qb::duration` / `std::chrono` here — these are distances, and the only
`std::chrono`-shaped values in qbm-redis live on the connection and retry path, not on command arguments (
see [connection.md](./connection.md)).

### Reply types in this group

| Command             | `Reply<T>` payload `T`                    | Meaning                                                                   |
|---------------------|-------------------------------------------|---------------------------------------------------------------------------|
| `geoadd`            | `long long`                               | number of **new** members added (score updates excluded)                  |
| `geodist`           | `std::optional<double>`                   | distance, or `std::nullopt` if either member is missing                   |
| `geohash`           | `std::vector<std::optional<std::string>>` | one geohash string per queried member; `std::nullopt` if missing          |
| `geopos`            | `std::vector<std::optional<geo_pos>>`     | one `{longitude, latitude}` per queried member; `std::nullopt` if missing |
| `georadius`         | `std::vector<std::string>`                | member names inside the radius                                            |
| `georadiusbymember` | `std::vector<std::string>`                | member names inside the radius                                            |
| `geosearch`         | `std::vector<std::string>`                | member names inside the radius                                            |

`qb::redis::geo_pos` is a two-`double` aggregate:

```cpp
struct geo_pos {
    double longitude{};
    double latitude{};
    bool operator==(const geo_pos &) const = default;
};
```

<!-- src: qbm/redis/src/qbm/redis/types.h:324-329 -->

Two corrections against older notes. First, the position-returning commands (`geohash`, `geopos`) yield a **vector
of `std::optional`**: a `std::nullopt` element means the member at that index is absent from the index, so check each
element before dereferencing. Second, the radius/search commands in this module return **member names only** (
`std::vector<std::string>`) — there is no `std::vector<search_result>` overload and no `geo_radius_options` /
`geo_search_options` struct. To request `WITHCOORD` / `WITHDIST` / `WITHHASH` and parse the richer reply yourself, see
the raw-options note below.

### The `options` vector is appended verbatim

`georadius`, `georadiusbymember`, and `geosearch` each accept a trailing `const std::vector<std::string> &options = {}`.
In every case the vector is forwarded to the wire command as its final argument, and the serializer expands a container
into one token per element (`to_redis_string` over the range), so each element is sent as one additional command token,
in order, with no validation or escaping. This is how you reach Redis sub-options that have no typed parameter:

```cpp
// GEORADIUS sicily 13.36 38.11 200 km WITHDIST
co_await redis.georadius(key, 13.361389, 38.115556, 200,
                         qb::redis::GeoUnit::KM,
                         std::vector<std::string>{"WITHDIST"});

// GEORADIUS ... COUNT 1
co_await redis.georadius(key, 13.361389, 38.115556, 200,
                         qb::redis::GeoUnit::KM,
                         std::vector<std::string>{"COUNT", "1"});

// GEORADIUS ... ASC
co_await redis.georadius(key, 13.361389, 38.115556, 200,
                         qb::redis::GeoUnit::KM,
                         std::vector<std::string>{"ASC"});
```

<!-- src: qbm/redis/src/qbm/redis/commands/geo_commands.h:237-238, qbm/redis/src/qbm/redis/reply.h:875-890 -->

You own the spelling and ordering of these tokens. The same vector reaches `geosearch` too — its callback overload
appends `options` after the `BYRADIUS <radius> <unit>` tokens, so `COUNT`/`ASC`/`WITH*` are forwarded there as well.
Note also that the declared return type stays `std::vector<std::string>` even when you pass `WITHCOORD`/`WITHDIST`/
`WITHHASH`; in that case Redis replies with nested arrays and `parse<std::string>` extracts the first sub-element of
each, so you get only the member name. If you need the structured per-member distance/coordinate/hash, issue the command
at a lower level or use a sorted-set read instead.

---

## Command reference

All signatures below are the public methods of `geo_commands<Derived>`. Every callback overload **except `geodist`** is
SFINAE-gated on `std::is_invocable_v<Func, Reply<T>&&>` for that command's `T`; a handler with the wrong `Reply<T>`
signature drops out of overload resolution, so the call fails to compile (no viable overload) rather than mismatching at
runtime. `geodist`'s callback overload (geo_commands.h:109-111) is the lone exception: it returns a plain `Derived&`
with no `std::enable_if` guard, so a wrong-typed handler still binds the overload and the type error surfaces deeper (
inside `command<std::optional<double>>`) rather than as a clean "no viable overload".

### `geoadd` — add points

```cpp
// coroutine: yields Reply<long long>
template <typename... Members>
auto geoadd(const std::string &key, Members &&...members);

// callback: returns Derived&
template <typename Func, typename... Members>  // Func invocable with Reply<long long>&&
Derived &geoadd(Func &&func, const std::string &key, Members &&...members);
```

<!-- src: qbm/redis/src/qbm/redis/commands/geo_commands.h:53,73 -->

Adds one or more points to the sorted set at `key`. Members are passed as flat `(longitude, latitude, name)` triplets —
the parameter pack is forwarded verbatim, so you supply the triplets directly rather than building any struct. The reply
is the number of **new** members added; updates to an existing member's position do not count. The library does not
validate the triplet count: a malformed argument list surfaces as a Redis-side error in the `Reply`.

```cpp
auto r1 = co_await redis.geoadd(key, 13.361389, 38.115556, "Palermo");
// r1.result() == 1
auto r2 = co_await redis.geoadd(key, 15.087269, 37.502669, "Catania",
                                     13.583333, 37.316667, "Agrigento");
// r2.result() == 2
```

<!-- src: qbm/redis/tests/integration/geo/geo-commands.cpp:62-85 -->

### `geodist` — distance between two members

```cpp
// coroutine: yields Reply<std::optional<double>>
auto geodist(const std::string &key, const std::string &member1,
             const std::string &member2, GeoUnit unit = GeoUnit::M);

// callback: returns Derived&
template <typename Func>  // Func invocable with Reply<std::optional<double>>&&
Derived &geodist(Func &&func, const std::string &key, const std::string &member1,
                 const std::string &member2, GeoUnit unit = GeoUnit::M);
```

<!-- src: qbm/redis/src/qbm/redis/commands/geo_commands.h:92,111 -->

Returns the distance between `member1` and `member2` in `unit` (default meters). The result is `std::nullopt` when
either member is missing from the index, so test `.has_value()` before dereferencing.

```cpp
auto d  = co_await redis.geodist(key, "Palermo", "Catania");                    // meters
auto km = co_await redis.geodist(key, "Palermo", "Catania", qb::redis::GeoUnit::KM);
if (km.ok() && km.result().has_value())
    qb::io::cout() << *km.result() << " km" << std::endl;

auto missing = co_await redis.geodist(key, "NonExistent1", "NonExistent2");
// missing.ok() == true, missing.result().has_value() == false
```

<!-- src: qbm/redis/tests/integration/geo/geo-commands.cpp:88-123 -->

### `geohash` — geohash strings

```cpp
// coroutine: yields Reply<std::vector<std::optional<std::string>>>
template <typename... Members>
auto geohash(const std::string &key, Members &&...members);

// callback: returns Derived&
template <typename Func, typename... Members>  // Func invocable with Reply<std::vector<std::optional<std::string>>>&&
Derived &geohash(Func &&func, const std::string &key, Members &&...members);
```

<!-- src: qbm/redis/src/qbm/redis/commands/geo_commands.h:126,146 -->

Returns the standard geohash string for each requested member, in request order. An element is `std::nullopt` when that
member is absent.

```cpp
auto reply = co_await redis.geohash(key, "Palermo");
if (reply.ok()) {
    auto &hashes = reply.result();        // std::vector<std::optional<std::string>>
    if (!hashes.empty() && hashes[0])
        qb::io::cout() << "geohash: " << *hashes[0] << std::endl;
}
```

<!-- src: qbm/redis/tests/integration/geo/geo-commands.cpp:126-154 -->

### `geopos` — decoded coordinates

```cpp
// coroutine: yields Reply<std::vector<std::optional<geo_pos>>>
template <typename... Members>
auto geopos(const std::string &key, Members &&...members);

// callback: returns Derived&
template <typename Func, typename... Members>  // Func invocable with Reply<std::vector<std::optional<geo_pos>>>&&
Derived &geopos(Func &&func, const std::string &key, Members &&...members);
```

<!-- src: qbm/redis/src/qbm/redis/commands/geo_commands.h:166,186 -->

Returns the `{longitude, latitude}` of each requested member, in request order. An element is `std::nullopt` when that
member is absent. Note Redis re-encodes the stored geohash, so the returned coordinates differ slightly from the values
you added.

```cpp
auto reply = co_await redis.geopos(key, "Palermo");
if (reply.ok()) {
    auto &positions = reply.result();     // std::vector<std::optional<geo_pos>>
    if (!positions.empty() && positions[0])
        qb::io::cout() << positions[0]->longitude << ", "
                       << positions[0]->latitude << std::endl;
}
```

<!-- src: qbm/redis/tests/integration/geo/geo-commands.cpp:157-185 -->

### `georadius` — search by coordinate

```cpp
// coroutine: yields Reply<std::vector<std::string>>
auto georadius(const std::string &key, double longitude, double latitude, double radius,
               GeoUnit unit = GeoUnit::M, const std::vector<std::string> &options = {});

// callback: returns Derived&
template <typename Func>  // Func invocable with Reply<std::vector<std::string>>&&
Derived &georadius(Func &&func, const std::string &key, double longitude, double latitude,
                   double radius, GeoUnit unit = GeoUnit::M,
                   const std::vector<std::string> &options = {});
```

<!-- src: qbm/redis/src/qbm/redis/commands/geo_commands.h:210,235 -->

Returns the names of all members within `radius` (in `unit`) of the `(longitude, latitude)` center. The reply is member
names only; pass raw `options` tokens for `WITHDIST` / `COUNT` / `ASC` / `DESC` as shown
in [the options concept](#the-options-vector-is-appended-verbatim). Querying a key that does not exist yields an empty
vector, not an error.

```cpp
auto reply = co_await redis.georadius(key, 13.361389, 38.115556, 200,
                                      qb::redis::GeoUnit::KM);
if (reply.ok())
    for (auto &name : reply.result())     // std::vector<std::string>
        qb::io::cout() << name << std::endl;
```

<!-- src: qbm/redis/tests/integration/geo/geo-commands.cpp:188-228 -->

### `georadiusbymember` — search by existing member

```cpp
// coroutine: yields Reply<std::vector<std::string>>
auto georadiusbymember(const std::string &key, const std::string &member, double radius,
                       GeoUnit unit = GeoUnit::M, const std::vector<std::string> &options = {});

// callback: returns Derived&
template <typename Func>  // Func invocable with Reply<std::vector<std::string>>&&
Derived &georadiusbymember(Func &&func, const std::string &key, const std::string &member,
                           double radius, GeoUnit unit = GeoUnit::M,
                           const std::vector<std::string> &options = {});
```

<!-- src: qbm/redis/src/qbm/redis/commands/geo_commands.h:255,278 -->

Same as `georadius`, but the center is the position of an existing `member` rather than an explicit coordinate. The
member itself is included in the result.

```cpp
auto reply = co_await redis.georadiusbymember(key, "Palermo", 200,
                                              qb::redis::GeoUnit::KM);
if (reply.ok())
    for (auto &name : reply.result())     // std::vector<std::string>
        qb::io::cout() << name << std::endl;
```

<!-- src: qbm/redis/tests/integration/geo/geo-commands.cpp:231-260 -->

### `geosearch` — radius search from a member

```cpp
// coroutine: yields Reply<std::vector<std::string>>
auto geosearch(const std::string &key, const std::string &member, double radius,
               GeoUnit unit = GeoUnit::M, const std::vector<std::string> &options = {});

// callback: returns Derived&
template <typename Func>  // Func invocable with Reply<std::vector<std::string>>&&
Derived &geosearch(Func &&func, const std::string &key, const std::string &member,
                   double radius, GeoUnit unit = GeoUnit::M,
                   const std::vector<std::string> &options = {});
```

<!-- src: qbm/redis/src/qbm/redis/commands/geo_commands.h:297,320 -->

Emits `GEOSEARCH key FROMMEMBER <member> BYRADIUS <radius> <unit>` followed by any `options` tokens, and returns the
member names within that circle. This is a deliberately narrow wrapper over the full `GEOSEARCH`: the center is always
`FROMMEMBER` (there is no `FROMLONLAT`) and the area is always `BYRADIUS` (there is no `BYBOX`). Within those two fixed
choices, `options` works exactly as it does for `georadius` — `COUNT`, `ASC`/`DESC`, and `WITH*` are appended verbatim (
see [the options concept](#the-options-vector-is-appended-verbatim)).

```cpp
auto reply = co_await redis.geosearch(key, "Palermo", 200,
                                      qb::redis::GeoUnit::KM);
if (reply.ok())
    for (auto &name : reply.result())     // std::vector<std::string>
        qb::io::cout() << name << std::endl;
```

<!-- src: qbm/redis/tests/integration/geo/geo-commands.cpp:263-293 -->

---

## Callback form

Every command above has a callback overload that takes the handler first and returns `Derived&`. Use it from
non-coroutine code (for example, inside an actor's `on(...)` handler):

```cpp
redis.georadius(
    [](qb::redis::Reply<std::vector<std::string>> &&r) {
        if (r)
            for (auto &name : r.result())
                qb::io::cout() << name << std::endl;
    },
    key, 13.361389, 38.115556, 200, qb::redis::GeoUnit::KM);
```

The handler parameter must be exactly `Reply<T>&&` for that command's `T` (here `std::vector<std::string>`); for every
gated overload a mismatched signature removes the callback overload from consideration and the call will not compile.
The one exception is `geodist`, whose callback overload is not SFINAE-gated (see the Command reference note above): a
wrong-typed handler still binds it and the error appears deeper rather than as "no viable overload".

---

## Pitfalls

- **No blocking API.** These methods are coroutine- or callback-based only. A call without `co_await` (or a callback)
  builds and queues the command, and the result reaches you asynchronously. The `Reply<...> geoadd(...)` "Sync"
  signatures and the `*_async` method names in older docs do not exist.
- **`geosearch` covers only `FROMMEMBER` + `BYRADIUS`.** The single overload hard-codes the center to
  `FROMMEMBER <member>` and the area to `BYRADIUS <radius> <unit>`; there is no `FROMLONLAT` and no `BYBOX`. The
  trailing `options` vector *is* forwarded (so `COUNT`/`ASC`/`WITH*` reach the server), but if you need a coordinate
  center or a box query, `geosearch` cannot express it — use `georadius` (coordinate center) instead.
- **Position and hash results are per-member optionals.** `geohash` and `geopos` return
  `std::vector<std::optional<...>>`; a missing member is a `std::nullopt` element at that index, not a short vector and
  not an error. Always test the element before dereferencing.
- **`geodist` of a missing member is success, not error.** The reply is `ok()` with an empty `std::optional`.
  Distinguish "no such pair" from a real failure by checking `.result().has_value()`, not `.ok()`.
- **A miss is an empty list, not an error.** `georadius` / `georadiusbymember` / `geosearch` against a non-existent
  key (or with no members in range) return an empty `std::vector<std::string>` and `ok() == true`.
- **Radius commands return names only.** There is no typed options struct and no `search_result` overload in this
  module. `WITHCOORD`/`WITHDIST`/`WITHHASH` passed through the raw `options` vector still yield a
  `std::vector<std::string>` of member names, because `parse<std::string>` collapses each nested reply array to its
  first element.
- **No `qb::duration` here.** Distances are `double` plus a `GeoUnit`; do not substitute the framework time types. The
  only `std::chrono`-shaped values in qbm-redis live on the connection and retry path (connect/command timeouts,
  `RetryPolicy` delays — see [connection.md](./connection.md)), never on a geo command argument.

---

## See also

- [sorted_set_commands.md](./sorted_set_commands.md) — the underlying type; use `ZREM`/`ZCARD`/`ZSCAN` to delete points
  or enumerate a geo index.
- [commands_overview.md](./commands_overview.md) — the `Reply<T>` model, coroutine vs. callback dispatch, and the
  time-unit boundary that does *not* apply to geo distances.
- [error_handling.md](./error_handling.md) — interpreting `Reply<T>::ok()`, `error()`, and error categories.
- [connection.md](./connection.md) — where the real `qb::duration` values live (connect/command timeouts,
  `RetryPolicy`).
- [Redis geospatial commands](https://redis.io/commands/?group=geo) — upstream command semantics.
