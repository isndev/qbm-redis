# Publish commands

> **Audience:** Adopter · **Status:** stable · **Verified-against:** qbm-redis @ qb 2.6.0 (C++20 default, C++23
> supported)

Reference for the `qb::redis::publish_commands` group: a single command, `PUBLISH`, which fans a message out to every
client currently subscribed to a channel and returns how many received it, in both coroutine and callback form.

**Prerequisites:** [connection.md](./connection.md) (open a client
first), [commands_overview.md](./commands_overview.md) (Reply, callbacks vs coroutines) — **See also:
** [subscription_commands.md](./subscription_commands.md) (the receiving side: `cb_consumer`, `subscribe`/
`psubscribe`), [error_handling.md](./error_handling.md)

---

## What this group is

`publish_commands<Derived>` is a CRTP mixin (`<redis/publish_commands.h>`) that injects the publishing side of Redis
Pub/Sub into the client. It contributes exactly **one** command, `PUBLISH`. The mixin is never instantiated on its own:
the concrete client `qb::redis::detail::Redis<QB_IO_>` derives from it (alongside the other command-group mixins), so
the methods below are called on a live client through its public alias `qb::redis::tcp::client` (or
`qb::redis::tcp::ssl::client`). The mixin carries no state — it forwards to the derived client's `command<T>(...)` and
`make_coro_command<T>(...)`, which own argument serialization, I/O, and connection lifetime (
see [connection.md](./connection.md)).

<!-- src: qbm/redis/commands/publish_commands.h:37-43 (CRTP derived()), redis.h:1618 (tcp::client alias) -->

```cpp
#include <redis/redis.h>            // umbrella header; pulls in publish_commands.h

qb::redis::tcp::client redis{qb::io::uri{"tcp://127.0.0.1:6379"}};
// co_await redis.connect();        // see connection.md
```

This page covers only the publish side. The subscriber side — receiving those messages through
`qb::redis::tcp::cb_consumer`, and the `subscribe`/`psubscribe`/`unsubscribe`/`punsubscribe` commands — lives
in [subscription_commands.md](./subscription_commands.md). There is no `SPUBLISH` (sharded Pub/Sub) in this slice.

---

## Semantics

`PUBLISH` is fire-and-forget fan-out with **no delivery guarantee and no persistence**. The reply is the number of
clients that received the message at the moment you published:

- A positive count is the number of subscribers (channel subscribers plus matching pattern subscribers) that the message
  was delivered to.
- A count of `0` means nobody was subscribed; the message is dropped. Redis Pub/Sub does not buffer messages for absent
  subscribers, so this is not an error — `reply.ok()` is still `true`.

<!-- src: qbm/redis/commands/publish_commands.h:57-83; FACTBOOK invariant publish_commands.h:57-83 -->

A subscriber receives a message only if it was subscribed *before* you published. If you need at-least-once semantics,
durability, or consumer groups, use Redis Streams instead (see your stream command reference), not Pub/Sub.

---

## Reply type

`PUBLISH` always resolves to `qb::redis::Reply<long long>`. Read it the same way as every other command:

- `reply.ok()` — `true` when the server returned a non-error reply.
- `reply.result()` (alias `reply.value()`) — the `long long` subscriber count.
- `reply.error()` — the error message string when `!reply.ok()`.

<!-- src: qbm/redis/reply.h:1162-1229 (Reply ok/result/value/error) -->

---

## `publish`

Post `message` to `channel`, delivering it to all current subscribers of that channel and to any pattern subscriber
whose pattern matches it.

Both forms share the name `publish` — there is **no** `publish_async` suffix. The two overloads are distinguished by
their first argument:

```cpp
// Coroutine overload — returns an awaiter that yields Reply<long long>.
auto publish(const std::string &channel, const std::string &message);

// Callback overload — first argument is an invocable taking Reply<long long>&&;
// returns the derived client by reference so calls can chain.
template <typename Func>
std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
publish(Func &&func, const std::string &channel, const std::string &message);
```

<!-- src: qbm/redis/commands/publish_commands.h:57-83 -->

| Argument  | Type                              | Meaning                                |
|:----------|:----------------------------------|:---------------------------------------|
| `channel` | `const std::string&`              | Channel name to publish to.            |
| `message` | `const std::string&`              | Message payload (may be empty).        |
| `func`    | `Func&&` (callback overload only) | Invocable taking `Reply<long long>&&`. |

- **Reply:** `Reply<long long>` — the number of clients that received the message.
- **Callback overload return:** `Derived&` (the live client), for chaining further commands.
- **No time arguments.** `PUBLISH` carries no TTL or deadline, so there is no native-unit time boundary in this group. (
  Connect timeouts, the `RetryPolicy` backoff, and the optional command watchdog are `qb::duration`, but they belong to
  the client, not to this command — see [connection.md](./connection.md).)

### Coroutine form

```cpp
#include <redis/redis.h>
#include <qb/io/async.h>

using namespace qb::redis;

qb::io::async::task<void> announce(tcp::client &redis) {
    auto reply = co_await redis.publish("news.world", "market opened");
    if (!reply.ok()) {
        // reply.error() holds the server error message
        co_return;
    }
    // reply.result() == number of subscribers that received the message;
    // 0 means nobody was listening — not an error.
    long long received = reply.result();
    (void) received;
}
```

<!-- src: qbm/redis/tests/integration/pubsub/pubsub-fanout.cpp:93-138 (co_await publisher.publish(ch, msg)) -->

### Callback form

```cpp
#include <redis/redis.h>

using namespace qb::redis;

void announce(tcp::client &redis) {
    redis.publish(
        [](Reply<long long> &&reply) {
            if (reply.ok()) {
                long long received = reply.result();
                (void) received;
            }
        },
        "news.world", "market opened");
}
```

The callback overload is SFINAE-gated on `std::is_invocable_v<Func, Reply<long long>&&>`: a lambda whose parameter is
not `Reply<long long>&&` does not match this overload, so you cannot accidentally bind the wrong reply type.

<!-- src: qbm/redis/commands/publish_commands.h:78-83 -->

---

## End-to-end: publish to a live subscriber

This is the shape the module's own tests use — a `cb_consumer` subscribes, then a separate `tcp::client` publishes and
observes a non-zero receipt count. The consumer's message-handling API is documented
in [subscription_commands.md](./subscription_commands.md); it appears here only to make the receipt count meaningful.

```cpp
#include <redis/redis.h>
#include <qb/io/async.h>
#include <atomic>

using namespace qb::redis;

std::atomic<int> received{0};

// Consumer: the constructor takes the per-message callback.
tcp::cb_consumer consumer{qb::io::uri{"tcp://127.0.0.1:6379"},
                          [&](auto &&msg) { ++received; (void) msg; }};

tcp::client publisher{qb::io::uri{"tcp://127.0.0.1:6379"}};

qb::io::async::task<void> demo() {
    co_await consumer.connect();
    co_await publisher.connect();

    co_await consumer.subscribe("news.world");          // see subscription_commands.md

    auto reply = co_await publisher.publish("news.world", "market opened");
    // reply.result() == 1 here: exactly one subscriber received the message.
    (void) reply;
}
```

<!-- src: qbm/redis/tests/integration/pubsub/pubsub-fanout.cpp:93-138 -->

---

## Pitfalls

- **`publish_async` does not exist.** Older drafts named the callback overload `publish_async`; the real API names both
  overloads `publish` and disambiguates on the first argument. Use `redis.publish(cb, channel, msg)` for the callback
  form.
- **A `0` reply is not an error.** No subscribers means the message was dropped, but `reply.ok()` is still `true`. Test
  for delivery with `reply.result()`, not with `ok()`.
- **No replay for late subscribers.** A subscriber that connects or subscribes after you publish never sees the message.
  Pub/Sub has no backlog; if you need durability, reach for Redis Streams.
- **`publish` is on the regular client, not the consumer.** The publishing side lives on `tcp::client`. A connection
  that has entered subscribe mode (via `subscribe`/`psubscribe`) cannot issue arbitrary commands; keep publishing and
  subscribing on separate client objects, as the example does.
- **Drive the loop.** Like every command in this module, `publish` resolves only while the qb-io event loop runs. Inside
  a `qb::io::async::task<>` that is automatic; from synchronous code, pump it with
  `qb::io::async::run_sync(redis.publish(...))`.

---

## See also

- [subscription_commands.md](./subscription_commands.md) — the receiving side: `cb_consumer`, `subscribe`, `psubscribe`,
  `unsubscribe`, `punsubscribe`, and message callbacks.
- [connection.md](./connection.md) — opening a client, the `qb::duration` connect/retry timeouts.
- [commands_overview.md](./commands_overview.md) — `Reply<T>`, the coroutine-vs-callback split shared by every command
  group.
- [error_handling.md](./error_handling.md) — interpreting `reply.ok()` / `reply.error()`.
