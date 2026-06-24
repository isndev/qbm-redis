# Sorted set commands

> **Audience:** Adopter · **Status:** stable · **Verified-against:** qbm-redis @ qb 2.0.0 (C++20 default, C++23
> supported)

Reference for the Redis sorted-set (ZSET) command group exposed by `qb::redis::sorted_set_commands<Derived>` — each
command listed with its exact signature, arguments, reply type, and a minimal coroutine and callback snippet.

**Prerequisites:** [../README.md](../README.md) (install, `qb_load_modules`,
`qbm::redis`), [connection.md](./connection.md), [commands_overview.md](./commands_overview.md) — **See also:
** [set_commands.md](./set_commands.md), [list_commands.md](./list_commands.md), [key_commands.md](./key_commands.md), [error_handling.md](./error_handling.md), [pipeline_and_await.md](./pipeline_and_await.md)

---

## Summary

A Redis sorted set is a collection of unique string members, each paired with a floating-point `double` score; members
are kept ordered by score (ties broken lexicographically). This is the natural shape for leaderboards, priority queues,
time-ordered indexes, and rate-limit windows.

The commands here are defined in `qbm/redis/sorted_set_commands.h` as a CRTP mixin (`sorted_set_commands<Derived>`) that
the client inherits, so you call them directly on a connected `qb::redis::tcp::client`: `redis.zadd(...)`,
`redis.zrange(...)`, and so on. There is no `_async` suffix and no separate sync/async class — the overload you pick
selects the calling style. Calling the mixin in isolation is not supported; it relies on the host client supplying
`command<T>(...)` and `make_coro_command<T>(...)`.

<!-- src: qbm/redis/sorted_set_commands.h:38-44 -->

Each command exposes two overloads:

- a **coroutine** form (no callback argument) that returns an awaiter yielding `Reply<T>` — drive it with `co_await`
  inside a `qb::io::async::task<...>`, or with `qb::io::async::run_sync(...)` from synchronous code;
- a **callback** form whose **first** argument is the handler and which returns `Derived&` for chaining. Your callback
  must accept exactly `Reply<T>&&` for the command's `T`; the callback overload is SFINAE-gated on
  `std::is_invocable_v<Func, Reply<T>&&>`.

<!-- src: qbm/redis/sorted_set_commands.h:272-285 -->

One operation is **callback-only** with no coroutine form: the pattern-only auto-iterating `zscan(func, key, pattern)`.
It is covered below alongside the cursor form.

---

## Concepts you need before the table

### `score_member` — the member/score pair

```cpp
// qbm/redis/types.h:237
struct score_member {
    double      score{};
    std::string member;
    bool operator==(const score_member &) const = default;
};
```

You build a sorted set from a `std::vector<score_member>` (`{{score, member}, ...}`), and every score-bearing read
returns `std::vector<score_member>`.

### Intervals — typed bounds for score and lexicographical ranges

Score and lex ranges are expressed with the interval class templates in `qbm/redis/types.h`, which carry the bound
semantics for you. Two aliases cover the common cases:

```cpp
// qbm/redis/types.h:170-171
using score_interval = qb::redis::BoundedInterval<double>;       // numeric score bounds
using lex_interval   = qb::redis::BoundedInterval<std::string>;  // lexicographical bounds

// Construct with a BoundType: CLOSED, OPEN, LEFT_OPEN, RIGHT_OPEN  (types.h:55)
qb::redis::score_interval scores(20.0, 40.0, qb::redis::BoundType::CLOSED);   // [20,40]
qb::redis::lex_interval   lex("b", "e", qb::redis::BoundType::LEFT_OPEN);     // (b,e]
```

`*count` / `*range` commands accept any type with `lower()` and `upper()` member functions — the score/lex query methods
are templated on `Interval`. Left/right-bounded and unbounded variants exist (`LeftBoundedInterval`,
`RightBoundedInterval`, `UnboundedInterval`) for half-open and full-range queries.

<!-- src: qbm/redis/types.h:54-171; qbm/redis/tests/test-sorted-set-commands.cpp:177, 364, 429 -->

### `LimitOptions` — pagination for range-by-score / range-by-lex

```cpp
// qbm/redis/types.h:178
struct LimitOptions {
    long long offset = 0;
    long long count  = -1;
};
```

`zrangebyscore` / `zrangebylex` emit the wire `LIMIT offset count` clause only when `opts.offset >= 0`. The default `{}`
has `offset == 0`, so the `LIMIT` is emitted by default; pass a negative offset to suppress it. `zrevrangebylex` and
`zrevrangebyscore` always emit `LIMIT` with the supplied offset/count.

<!-- src: qbm/redis/sorted_set_commands.h:594-596, 637-639, 843, 874 -->

### `UpdateType` — the NX/XX flag for `zadd`

```cpp
// qbm/redis/types.h:49
enum class UpdateType { EXIST, NOT_EXIST, ALWAYS };
```

`zadd` maps this enum to the wire flag: `EXIST` → `XX` (only update members that already exist), `NOT_EXIST` → `NX` (
only add new members), `ALWAYS` (the default) → no flag. There is **no GT/LT or INCR option** on this `zadd` overload —
use `zincrby` to add-or-increment a single member.

<!-- src: qbm/redis/redis.cpp:328-334; qbm/redis/sorted_set_commands.h:276-285 -->

### `Aggregation` — score combination for store/intersect commands

```cpp
// qbm/redis/types.h:57
enum class Aggregation { SUM, MIN, MAX };  // default SUM
```

Used by `zunionstore`, `zinterstore`, `zinter`, and `zinterWithScores` to combine scores across source sets. Weights,
when supplied, are applied before aggregation.

### Time-unit boundary — blocking timeouts are **seconds**, not `qb::duration`

The blocking commands (`bzpopmax`, `bzpopmin`, `bzmpop`) take their timeout in **seconds**. The primitive overloads take
a raw `long long timeout` (where `0` means *block forever*). `bzpopmax` and `bzpopmin` each add a `std::chrono::seconds`
convenience overload that simply forwards `timeout.count()`; `bzmpop` has **no** chrono overload — pass its timeout as a
raw `long long` count of seconds. This is the documented native-units boundary for this group — **do not** pass
`qb::duration` here.

```cpp
redis.bzpopmax(keys, 5);                          // 5 seconds
redis.bzpopmax(keys, std::chrono::seconds{5});    // same, typed
redis.bzpopmax(keys, 0);                          // block forever
```

Scores and increments stay native (`double`); ranks, counts, and cardinalities are integers (`long long`). Connect and
command deadlines remain `qb::duration` at the client level — see [connection.md](./connection.md).

<!-- src: qbm/redis/sorted_set_commands.h:138, 167-188 (bzpopmax chrono overload), 199, 228-249 (bzpopmin chrono overload), 1358-1384 (bzmpop, raw long long only) -->

---

## Reply types at a glance

`Reply<T>` is the uniform envelope (`qbm/redis/reply.h:1052`): `reply.ok()` reports success, `reply.result()` (alias
`reply.value()`) holds the parsed payload, `reply.error()` holds the server error string, and `Reply<T>` is contextually
convertible to `bool` (explicit). In this group the payloads are standard-library types (`std::vector`, `std::optional`,
`std::pair`, `std::tuple`); the one exception is `zscan`, whose `scan<...>.items` is a **qb-core** `qb::unordered_map`.

| Command(s)                                                                                                                                                                       | Reply payload `T`                                                  |
|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|--------------------------------------------------------------------|
| `zadd`, `zcard`, `zcount`, `zlexcount`, `zrem`, `zremrangebylex`, `zremrangebyrank`, `zremrangebyscore`, `zunionstore`, `zinterstore`, `zdiffstore`, `zintercard`, `zrangestore` | `long long`                                                        |
| `zincrby`                                                                                                                                                                        | `double`                                                           |
| `zscore`                                                                                                                                                                         | `std::optional<double>`                                            |
| `zmscore`                                                                                                                                                                        | `std::vector<std::optional<double>>`                               |
| `zrank`, `zrevrank`                                                                                                                                                              | `std::optional<long long>`                                         |
| `zrandmember` (single)                                                                                                                                                           | `std::optional<std::string>`                                       |
| `zrange`, `zrevrange`, `zrangebyscore`, `zrevrangebyscore`, `zpopmax`, `zpopmin`, `zdiffWithScores`, `zinterWithScores`, `zrandmemberWithScores`                                 | `std::vector<score_member>`                                        |
| `zrangebylex`, `zrevrangebylex`, `zdiff`, `zinter`, `zrandmemberCount`                                                                                                           | `std::vector<std::string>`                                         |
| `zscan` (cursor form)                                                                                                                                                            | `qb::redis::scan<qb::unordered_map<std::string, double>>`          |
| `bzpopmax`, `bzpopmin`                                                                                                                                                           | `std::optional<std::tuple<std::string, std::string, double>>`      |
| `zmpop`, `bzmpop`                                                                                                                                                                | `std::optional<std::pair<std::string, std::vector<score_member>>>` |

A score-bearing read returns `std::vector<score_member>` because the implementation appends `WITHSCORES` on the wire
unconditionally for those commands. The non-scored siblings (`zrangebylex`, `zinter`, `zdiff`, `zrandmemberCount`)
deliberately omit it and return `std::vector<std::string>`.

<!-- src: qbm/redis/sorted_set_commands.h:562-563, 635-639, 1044-1045, 1171-1172, 1327; qbm/redis/types.h:354-359 -->

---

## Commands

The snippets assume a connected client inside a coroutine; see [connection.md](./connection.md) for setup.

```cpp
#include <redis/redis.h>            // namespace qb::redis
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
```

### `ZADD key [NX|XX] score member [score member ...]` — `zadd`

Adds members with their scores. Returns the number of **new** members added; pass `changed = true` to return the number
of members **added or updated** instead (the `CH` flag).

```cpp
// Coroutine — qbm/redis/sorted_set_commands.h:252
auto zadd(const std::string &key, const std::vector<score_member> &members,
          UpdateType type = UpdateType::ALWAYS, bool changed = false);

// Callback — qbm/redis/sorted_set_commands.h:272
template <typename Func>
std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
zadd(Func &&func, const std::string &key, const std::vector<score_member> &members,
     UpdateType type = UpdateType::ALWAYS, bool changed = false);
```

```cpp
// Coroutine
std::vector<qb::redis::score_member> members = {
    {10.0, "member1"}, {20.0, "member2"}, {30.0, "member3"}};
auto r = co_await redis.zadd("board", members);
if (r.ok())
    qb::io::cout() << "added " << r.result() << " members\n"; // 3

// Callback — only insert members that don't already exist (NX)
redis.zadd([](qb::redis::Reply<long long> &&r) {
    if (r.ok()) { /* r.result() == count of new members */ }
}, "board", members, qb::redis::UpdateType::NOT_EXIST);
```

<!-- src: qbm/redis/tests/test-sorted-set-commands.cpp:44-48 -->

### `ZCARD key` — `zcard`

Returns the number of members in the sorted set (`0` for a missing key).

```cpp
// Coroutine — qbm/redis/sorted_set_commands.h:293
auto zcard(const std::string &key);

// Callback — qbm/redis/sorted_set_commands.h:309
template <typename Func>
std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
zcard(Func &&func, const std::string &key);
```

```cpp
auto r = co_await redis.zcard("board");
if (r.ok()) qb::io::cout() << r.result() << " members\n";
```

<!-- src: qbm/redis/tests/test-sorted-set-commands.cpp:51-53 -->

### `ZCOUNT key min max` — `zcount`

Counts members whose score falls inside a score interval.

```cpp
// Coroutine — qbm/redis/sorted_set_commands.h:325
template <typename Interval>
auto zcount(const std::string &key, const Interval &interval);

// Callback — qbm/redis/sorted_set_commands.h:344
template <typename Func, typename Interval>
std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
zcount(Func &&func, const std::string &key, const Interval &interval);
```

```cpp
qb::redis::score_interval i(20.0, 40.0, qb::redis::BoundType::CLOSED); // [20,40]
auto r = co_await redis.zcount("board", i);
```

### `ZLEXCOUNT key min max` — `zlexcount`

Counts members within a lexicographical interval. Use only when all members share the same score.

```cpp
// Coroutine — qbm/redis/sorted_set_commands.h:456
template <typename Interval>
auto zlexcount(const std::string &key, const Interval &interval);

// Callback — qbm/redis/sorted_set_commands.h:475
template <typename Func, typename Interval>
std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
zlexcount(Func &&func, const std::string &key, const Interval &interval);
```

```cpp
qb::redis::lex_interval i("b", "d", qb::redis::BoundType::CLOSED); // [b,d]
auto r = co_await redis.zlexcount("board", i);
```

<!-- src: qbm/redis/tests/test-sorted-set-commands.cpp:177-178 -->

### `ZSCORE key member` — `zscore`

Returns the score of `member`, or an empty optional when the member or key is absent.

```cpp
// Coroutine — qbm/redis/sorted_set_commands.h:960
auto zscore(const std::string &key, const std::string &member);

// Callback — qbm/redis/sorted_set_commands.h:977
template <typename Func>
std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<double>> &&>, Derived &>
zscore(Func &&func, const std::string &key, const std::string &member);
```

```cpp
auto r = co_await redis.zscore("board", "member2");
if (r.ok() && r.result().has_value())
    qb::io::cout() << *r.result() << "\n"; // 20.0
```

<!-- src: qbm/redis/tests/test-sorted-set-commands.cpp:56-59 -->

### `ZMSCORE key member [member ...]` — `zmscore`

Returns one optional score per requested member, in request order. Missing members are empty optionals; the vector
length always matches the member count.

```cpp
// Coroutine — qbm/redis/sorted_set_commands.h:1239
auto zmscore(const std::string &key, const std::vector<std::string> &members);

// Callback — qbm/redis/sorted_set_commands.h:1246
template <typename Func>
std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::optional<double>>> &&>, Derived &>
zmscore(Func &&func, const std::string &key, const std::vector<std::string> &members);
```

```cpp
auto r = co_await redis.zmscore("board", {"a", "b", "nonexistent"});
if (r.ok()) { /* r.result()[2] == std::nullopt */ }
```

<!-- src: qbm/redis/tests/test-sorted-set-commands.cpp:564 -->

### `ZINCRBY key increment member` — `zincrby`

Adds `increment` (may be negative) to the score of `member`, creating the member at score `increment` if absent. Returns
the new score.

```cpp
// Coroutine — qbm/redis/sorted_set_commands.h:351
auto zincrby(const std::string &key, double increment, const std::string &member);

// Callback — qbm/redis/sorted_set_commands.h:369
template <typename Func>
std::enable_if_t<std::is_invocable_v<Func, Reply<double> &&>, Derived &>
zincrby(Func &&func, const std::string &key, double increment, const std::string &member);
```

```cpp
auto r = co_await redis.zincrby("board", 5.0, "score"); // 10.0 -> 15.0
if (r.ok()) qb::io::cout() << r.result() << "\n";
```

<!-- src: qbm/redis/tests/test-sorted-set-commands.cpp:287-288 -->

### `ZRANK key member` / `ZREVRANK key member` — `zrank`, `zrevrank`

Returns the 0-based rank of `member`: `zrank` orders low score → high, `zrevrank` high → low. Returns an empty optional
when the member is absent.

```cpp
// Coroutine — qbm/redis/sorted_set_commands.h:642 / :877
auto zrank(const std::string &key, const std::string &member);
auto zrevrank(const std::string &key, const std::string &member);

// Callback — qbm/redis/sorted_set_commands.h:659 / :894
template <typename Func>
std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<long long>> &&>, Derived &>
zrank(Func &&func, const std::string &key, const std::string &member);
```

```cpp
auto r = co_await redis.zrank("board", "b");
if (r.ok() && r.result().has_value())
    qb::io::cout() << "rank " << *r.result() << "\n";
```

<!-- src: qbm/redis/tests/test-sorted-set-commands.cpp:90-100 -->

### `ZRANGE key start stop WITHSCORES` — `zrange`, `zrevrange`

Returns members by **index** range (`start`/`stop` are 0-based, negatives count from the end). Both methods emit
`WITHSCORES` unconditionally, so the reply is always `std::vector<score_member>`; `zrange` goes low → high, `zrevrange`
high → low.

```cpp
// Coroutine — qbm/redis/sorted_set_commands.h:540 / :779
auto zrange(const std::string &key, long long start, long long stop);
auto zrevrange(const std::string &key, long long start, long long stop);

// Callback — qbm/redis/sorted_set_commands.h:558 / :797
template <typename Func>
std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<score_member>> &&>, Derived &>
zrange(Func &&func, const std::string &key, long long start, long long stop);
```

```cpp
auto r = co_await redis.zrange("board", 0, -1); // every member, low score first
if (r.ok())
    for (auto const &sm : r.result())
        qb::io::cout() << sm.member << " = " << sm.score << "\n";
```

<!-- src: qbm/redis/tests/test-sorted-set-commands.cpp:138-146 -->

### `ZRANGEBYSCORE key min max [LIMIT offset count] WITHSCORES` — `zrangebyscore`, `zrevrangebyscore`

Returns members within a **score** interval, with scores. `zrevrangebyscore` walks high → low (note: the interval's
`upper()`/`lower()` are swapped on the wire for the reverse form, so pass the interval in natural low→high order).

```cpp
// Coroutine — qbm/redis/sorted_set_commands.h:609 / :846
template <typename Interval>
auto zrangebyscore(const std::string &key, Interval const &interval,
                   const LimitOptions &opts = {});
auto zrevrangebyscore(const std::string &key, Interval const &interval,
                      const LimitOptions &opt = {});

// Callback — qbm/redis/sorted_set_commands.h:630 / :867
template <typename Func, typename Interval>
std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<score_member>> &&>, Derived &>
zrangebyscore(Func &&func, const std::string &key, Interval const &interval,
              const LimitOptions &opts = {});
```

```cpp
qb::redis::score_interval i(20.0, 40.0, qb::redis::BoundType::CLOSED);
auto r = co_await redis.zrangebyscore("board", i);                // ascending, [20,40]
auto rev = co_await redis.zrevrangebyscore("board", i);           // descending
```

<!-- src: qbm/redis/tests/test-sorted-set-commands.cpp:364-370 -->

### `ZRANGEBYLEX key min max [LIMIT offset count]` — `zrangebylex`, `zrevrangebylex`

Returns members within a **lexicographical** interval, **without** scores (`std::vector<std::string>`). Use only when
all members share one score.

```cpp
// Coroutine — qbm/redis/sorted_set_commands.h:566 / :815
template <typename Interval>
auto zrangebylex(const std::string &key, Interval const &interval,
                 const LimitOptions &opts = {});
auto zrevrangebylex(const std::string &key, Interval const &interval,
                    const LimitOptions &opt = {});

// Callback — qbm/redis/sorted_set_commands.h:587 / :836
template <typename Func, typename Interval>
std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>, Derived &>
zrangebylex(Func &&func, const std::string &key, Interval const &interval,
            const LimitOptions &opts = {});
```

```cpp
qb::redis::lex_interval i("b", "e", qb::redis::BoundType::LEFT_OPEN); // (b,e]
auto r = co_await redis.zrangebylex("board", i);
```

<!-- src: qbm/redis/tests/test-sorted-set-commands.cpp:429-438 -->

### `ZRANGESTORE dst src min max [...]` — `zrangestore`

Stores the result of a range query from `src` into `dst`. Bounds and trailing options (`BYSCORE`, `BYLEX`, `REV`,
`LIMIT ...`) are passed as **raw strings** — there is no typed interval here, so format the bounds yourself. Returns the
number of members stored.

```cpp
// Coroutine — qbm/redis/sorted_set_commands.h:1334
auto zrangestore(const std::string &dst, const std::string &src,
                 const std::string &min, const std::string &max,
                 const std::vector<std::string> &options = {});

// Callback — qbm/redis/sorted_set_commands.h:1343
template <typename Func>
std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
zrangestore(Func &&func, const std::string &dst, const std::string &src,
            const std::string &min, const std::string &max,
            const std::vector<std::string> &options = {});
```

```cpp
auto r = co_await redis.zrangestore("dst", "src", "1", "3", {"BYSCORE"});
```

<!-- src: qbm/redis/tests/test-sorted-set-commands.cpp:592 -->

### `ZREM key member [member ...]` — `zrem`

Removes the listed members. Returns the number actually removed.

```cpp
// Coroutine — qbm/redis/sorted_set_commands.h:667
auto zrem(const std::string &key, const std::vector<std::string> &members);

// Callback — qbm/redis/sorted_set_commands.h:684
template <typename Func>
std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
zrem(Func &&func, const std::string &key, const std::vector<std::string> &members);
```

```cpp
auto r = co_await redis.zrem("board", {"b", "d"});
```

<!-- src: qbm/redis/tests/test-sorted-set-commands.cpp:250 -->

### `ZREMRANGEBYRANK` / `ZREMRANGEBYSCORE` / `ZREMRANGEBYLEX` — `zremrangebyrank`, `zremrangebyscore`, `zremrangebylex`

Bulk-remove by index range, score interval, or lex interval. Each returns the number of members removed.

```cpp
// Coroutine — qbm/redis/sorted_set_commands.h:726 / :752 / :691
auto zremrangebyrank(const std::string &key, long long start, long long stop);
template <typename Interval>
auto zremrangebyscore(const std::string &key, Interval const &interval);
template <typename Interval>
auto zremrangebylex(const std::string &key, Interval const &interval);

// Callback — qbm/redis/sorted_set_commands.h:744 / :771 / :710
template <typename Func>
std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
zremrangebyrank(Func &&func, const std::string &key, long long start, long long stop);
```

```cpp
auto a = co_await redis.zremrangebyrank("board", 0, 1);          // first two by rank
qb::redis::lex_interval i("c", "e", qb::redis::BoundType::CLOSED);
auto b = co_await redis.zremrangebylex("board", i);             // [c,e]
```

<!-- src: qbm/redis/tests/test-sorted-set-commands.cpp:258, 452-453 -->

### `ZPOPMIN` / `ZPOPMAX key [count]` — `zpopmin`, `zpopmax`

Removes and returns the `count` lowest- (`zpopmin`) or highest- (`zpopmax`) scored members, with scores (`count`
defaults to `1`).

```cpp
// Coroutine — qbm/redis/sorted_set_commands.h:515 / :483
auto zpopmin(const std::string &key, long long count = 1);
auto zpopmax(const std::string &key, long long count = 1);

// Callback — qbm/redis/sorted_set_commands.h:532 / :500
template <typename Func>
std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<score_member>> &&>, Derived &>
zpopmax(Func &&func, const std::string &key, long long count = 1);
```

```cpp
auto r = co_await redis.zpopmax("board", 2); // two highest, removed
if (r.ok())
    for (auto const &sm : r.result())
        qb::io::cout() << sm.member << " = " << sm.score << "\n";
```

<!-- src: qbm/redis/tests/test-sorted-set-commands.cpp:206-217 -->

### `ZMPOP numkeys key [key ...] MIN|MAX [COUNT count]` — `zmpop`

Pops from the **first non-empty** of the given sets. `min_or_max` is a **raw string** `"MIN"` or `"MAX"` (not an enum).
Returns an empty optional when every set is empty; otherwise `result()->first` is the source key and `result()->second`
is the popped members. `COUNT` is emitted only when `count > 1`. **Returns `derived()` without issuing a command — and
never invokes your callback — when `keys` is empty.**

```cpp
// Coroutine — qbm/redis/sorted_set_commands.h:1208
auto zmpop(const std::vector<std::string> &keys, const std::string &min_or_max,
           long long count = 1);

// Callback — qbm/redis/sorted_set_commands.h:1217
template <typename Func>
std::enable_if_t<std::is_invocable_v<Func,
    Reply<std::optional<std::pair<std::string, std::vector<score_member>>>> &&>, Derived &>
zmpop(Func &&func, const std::vector<std::string> &keys, const std::string &min_or_max,
      long long count = 1);
```

```cpp
auto r = co_await redis.zmpop({"board"}, "MIN", 1);
if (r.ok() && r.result().has_value()) {
    auto const &[src_key, popped] = *r.result();
    /* popped is std::vector<score_member> */
}
```

<!-- src: qbm/redis/tests/test-sorted-set-commands.cpp:555 -->

### Blocking pops — `bzpopmin`, `bzpopmax`, `bzmpop`

Block until a member can be popped from one of `keys`, or the **seconds** timeout elapses (`0` = block forever — see the
time-unit note above). `bzpopmin`/`bzpopmax` return `std::optional<std::tuple<key, member, score>>`; `bzmpop` mirrors
`zmpop`'s reply.

```cpp
// Coroutine — qbm/redis/sorted_set_commands.h:199 / :138 / :1358
auto bzpopmin(const std::vector<std::string> &keys, long long timeout);
auto bzpopmax(const std::vector<std::string> &keys, long long timeout);
auto bzmpop(const std::vector<std::string> &keys, long long timeout,
            const std::string &min_or_max, long long count = 1);

// std::chrono::seconds convenience overloads — sorted_set_commands.h:167, 228
auto bzpopmax(const std::vector<std::string> &keys,
              const std::chrono::seconds &timeout = std::chrono::seconds{0});

// Callback — qbm/redis/sorted_set_commands.h:155 / :216
template <typename Func>
std::enable_if_t<std::is_invocable_v<Func,
    Reply<std::optional<std::tuple<std::string, std::string, double>>> &&>, Derived &>
bzpopmax(Func &&func, const std::vector<std::string> &keys, long long timeout);
```

```cpp
auto r = co_await redis.bzpopmin({"q1", "q2"}, std::chrono::seconds{5});
if (r.ok() && r.result().has_value()) {
    auto const &[key, member, score] = *r.result();
    /* popped lowest-scored member from `key` */
}

auto m = co_await redis.bzmpop({"board"}, 1, "MIN", 1); // 1-second timeout
if (m.ok() && m.result().has_value())
    qb::io::cout() << m.result()->second.size() << " popped\n";
```

<!-- src: qbm/redis/tests/test-sorted-set-commands.cpp:601-604 -->

> `bzmpop` (like `zmpop`) returns `derived()` and never fires your callback when `keys` is empty (
`sorted_set_commands.h:1374`). Guard against empty key lists before calling.

### `ZRANDMEMBER key [count]` — `zrandmember`, `zrandmemberCount`, `zrandmemberWithScores`

Returns random members. Three distinct methods (note the **camelCase** names on the count/with-scores variants, which
diverge from the snake_case norm):

- `zrandmember(key)` → one member as `std::optional<std::string>` (empty when the key is missing);
- `zrandmemberCount(key, count)` → `std::vector<std::string>` (negative `count` allows duplicates);
- `zrandmemberWithScores(key, count)` → `std::vector<score_member>`.

```cpp
// Coroutine — qbm/redis/sorted_set_commands.h:1260 / :1283 / :1298
auto zrandmember(const std::string &key);
auto zrandmemberCount(const std::string &key, long long count);
auto zrandmemberWithScores(const std::string &key, long long count);
```

```cpp
auto one    = co_await redis.zrandmember("board");          // std::optional<std::string>
auto few    = co_await redis.zrandmemberCount("board", 2);  // std::vector<std::string>
auto scored = co_await redis.zrandmemberWithScores("board", 2);
```

<!-- src: qbm/redis/tests/test-sorted-set-commands.cpp:574-583 -->

### `ZUNIONSTORE` / `ZINTERSTORE` / `ZDIFFSTORE dest numkeys key [...]` — `zunionstore`, `zinterstore`, `zdiffstore`

Compute a union, intersection, or difference across source sets and store the result under `destination`. Returns the
cardinality of the stored set. `zunionstore`/`zinterstore` accept optional per-set `weights` and an `Aggregation`;
`zdiffstore` takes neither.

```cpp
// Coroutine — qbm/redis/sorted_set_commands.h:377 / :421 / :1056
auto zunionstore(const std::string &destination, const std::vector<std::string> &keys,
                 const std::vector<double> &weights = {},
                 Aggregation type = Aggregation::SUM);
auto zinterstore(const std::string &destination, const std::vector<std::string> &keys,
                 const std::vector<double> &weights = {},
                 Aggregation type = Aggregation::SUM);
auto zdiffstore(const std::string &destination, const std::vector<std::string> &keys);

// Callback — qbm/redis/sorted_set_commands.h:398 / :442 / :1074
template <typename Func>
std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
zunionstore(Func &&func, const std::string &destination,
            const std::vector<std::string> &keys,
            const std::vector<double> &weights = {},
            Aggregation type = Aggregation::SUM);
```

```cpp
auto u = co_await redis.zunionstore("union", {"set1", "set2"});
auto i = co_await redis.zinterstore("inter", {"set1", "set2"});
auto d = co_await redis.zdiffstore("diff",  {"set1", "set2"});
```

<!-- src: qbm/redis/tests/test-sorted-set-commands.cpp:325-333, 496 -->

### `ZINTER` / `ZDIFF numkeys key [...]` — `zinter`, `zinterWithScores`, `zdiff`, `zdiffWithScores`

Return the intersection or difference **without** storing it. The plain forms return `std::vector<std::string>`; the
`WithScores` forms (camelCase) return `std::vector<score_member>`. `zinter` accepts `weights` and an `Aggregation`;
`zdiff` takes only keys.

```cpp
// Coroutine — qbm/redis/sorted_set_commands.h:1091 / :1109 / :994 / :1008
auto zinter(const std::vector<std::string> &keys,
            const std::vector<double> &weights = {},
            Aggregation type = Aggregation::SUM);
auto zinterWithScores(const std::vector<std::string> &keys,
                      const std::vector<double> &weights = {},
                      Aggregation type = Aggregation::SUM);
auto zdiff(const std::vector<std::string> &keys);
auto zdiffWithScores(const std::vector<std::string> &keys);
```

```cpp
auto names  = co_await redis.zdiff({"set1", "set2"});          // std::vector<std::string>
auto scored = co_await redis.zdiffWithScores({"set1", "set2"}); // std::vector<score_member>
```

<!-- src: qbm/redis/tests/test-sorted-set-commands.cpp:483-489 -->

### `ZINTERCARD numkeys key [...] [LIMIT n]` — `zintercard`

Returns the cardinality of the intersection without materializing it. The optional `limit` caps how far the server
counts (`LIMIT` is emitted only when set).

```cpp
// Coroutine — qbm/redis/sorted_set_commands.h:1180
auto zintercard(const std::vector<std::string> &keys,
                std::optional<long long> limit = std::nullopt);

// Callback — qbm/redis/sorted_set_commands.h:1188
template <typename Func>
std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
zintercard(Func &&func, const std::vector<std::string> &keys,
           std::optional<long long> limit = std::nullopt);
```

```cpp
auto r = co_await redis.zintercard({"set1", "set2"});
```

<!-- src: qbm/redis/tests/test-sorted-set-commands.cpp:532 -->

### `ZSCAN key cursor [MATCH pattern] [COUNT count]` — `zscan`

Incrementally iterates members and their scores. The cursor form returns
`qb::redis::scan<qb::unordered_map<std::string, double>>` — read `result().cursor` (resume when non-zero) and
`result().items` (a `qb::unordered_map<member, score>`). **`zscan` no-ops when `key` is empty** (returns `derived()`
without issuing a command).

```cpp
// Cursor form, coroutine — qbm/redis/sorted_set_commands.h:902
auto zscan(const std::string &key, long long cursor, const std::string &pattern = "*",
           long long count = 10);

// Cursor form, callback — qbm/redis/sorted_set_commands.h:922
template <typename Func>
std::enable_if_t<std::is_invocable_v<Func,
    Reply<qb::redis::scan<qb::unordered_map<std::string, double>>> &&>, Derived &>
zscan(Func &&func, const std::string &key, long long cursor,
      const std::string &pattern = "*", long long count = 10);
```

```cpp
// Coroutine — drive the cursor yourself
long long cursor = 0;
do {
    auto r = co_await redis.zscan("board", cursor, "*", 100);
    if (!r.ok()) break;
    for (auto const &[member, score] : r.result().items)
        qb::io::cout() << member << " = " << score << "\n";
    cursor = static_cast<long long>(r.result().cursor);
} while (cursor != 0);
```

<!-- src: qbm/redis/tests/test-sorted-set-commands.cpp:401-403 -->

**Callback-only auto-iterating form.** `zscan(func, key, pattern)` (no cursor) drives the cursor internally — it
allocates a `shared_ptr`-managed scanner that keeps itself alive across the async round-trips (hardcoded page size
`COUNT 100`), collects every page, then fires your callback **once** with the complete `qb::unordered_map`. There is *
*no coroutine equivalent** of this overload.

```cpp
// Callback only — qbm/redis/sorted_set_commands.h:950
template <typename Func>
std::enable_if_t<std::is_invocable_v<Func,
    Reply<qb::redis::scan<qb::unordered_map<std::string, double>>> &&>, Derived &>
zscan(Func &&func, const std::string &key, const std::string &pattern = "*");
```

```cpp
redis.zscan([](qb::redis::Reply<qb::redis::scan<qb::unordered_map<std::string, double>>> &&r) {
    if (r.ok())
        for (auto const &[member, score] : r.result().items) { /* ... */ }
}, "board", "*");
```

<!-- src: qbm/redis/sorted_set_commands.h:54-127, 950-958 -->

---

## Pitfalls

- **`zadd` has no GT/LT/INCR.** This overload only exposes `UpdateType` (NX/XX) and the `CH` flag (`changed`). For
  add-or-increment of one member, use `zincrby`. Older docs that list `GT`/`LT`/`INCR` are wrong.
- **`zrange` always carries scores.** `zrange`/`zrevrange`/`zrangebyscore`/`zrevrangebyscore` emit `WITHSCORES`
  unconditionally and return `std::vector<score_member>`. There is no members-only index/score overload — use the
  `*bylex`, `zinter`, `zdiff`, or `zrandmemberCount` reads when you want `std::vector<std::string>`.
- **`zmpop` / `bzmpop` use raw `"MIN"`/`"MAX"` strings, not an enum.** A typo silently produces a server error rather
  than a compile error. Likewise `zrangestore` passes its bounds and options as raw strings — there is no typed
  interval.
- **Empty-input no-ops never fire your callback.** `zmpop`, `bzmpop`, and `zscan` return `derived()` without issuing a
  command (and without invoking your callback) when `keys`/`key` is empty. Do not assume your callback always runs.
- **Auto-iterating `zscan` swallows callback exceptions.** An exception thrown from your callback inside the no-cursor
  `zscan` scanner is caught and logged (`LOG_WARN`), never propagated. Don't rely on it surfacing.
- **camelCase outliers.** `zdiffWithScores`, `zinterWithScores`, `zrandmemberCount`, and `zrandmemberWithScores` break
  the snake_case convention — easy to miss when grepping. Their plain siblings (`zdiff`, `zinter`, `zrandmember`) are
  snake_case.
- **Blocking timeouts are seconds.** `bzpopmin`/`bzpopmax` take a `long long` or `std::chrono::seconds` timeout;
  `bzmpop` takes only a `long long` (seconds). `0` means *block forever* — never a `qb::duration`. Passing a small
  `qb::duration` count would be interpreted as a small number of seconds.
- **Retired time tokens.** `qb::Timestamp`, `qb::Duration`, `qb::TimePoint`, `to_timestamp(`, and `to_time_point(` are
  removed from the framework; they never applied to this group and must not appear in new code. Use `std::chrono`
  units (seconds for blocking pops) and `qb::duration` only for client-level connect/command deadlines.

---

## See also

- [set_commands.md](./set_commands.md) — unordered set operations (the unscored cousin of sorted sets)
- [list_commands.md](./list_commands.md) — list operations, including the other blocking pops (`blpop`, `brpop`)
- [key_commands.md](./key_commands.md) — TTL, existence, and key-level operations (`expire` unit boundary lives here)
- [commands_overview.md](./commands_overview.md) — the command-group map and shared `Reply<T>` conventions
- [pipeline_and_await.md](./pipeline_and_await.md) — batching and the coroutine/callback execution model
- [error_handling.md](./error_handling.md) — interpreting `Reply::ok()`, `Reply::error()`, and protocol errors
