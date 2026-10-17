<!-- Verified-against: qbm-redis @ qb 3.2.1. Source of truth: the headers under qbm/redis/src/qbm/redis/. -->
# qbm-redis — Concepts for Writing Correct Code

Asynchronous Redis client for the **qb C++ actor framework**. Speaks RESP2/RESP3
directly over a single non-blocking TCP (or TLS) session on the **qb-io event
loop** — no `hiredis`, no external Redis dependency. Public namespace
`qb::redis`; implementation in `qb::redis::detail`. Umbrella header
`<qbm/redis/redis.h>`. This file teaches the mental model and the load-bearing
invariants; it is not an exhaustive command reference (200+ commands live in
`llm/qbm-redis.llm.api.md` and `qbm/redis/readme/`).

Default standard is **C++20** (C++23 when `QB_CXX_STANDARD=23`); the coroutine
API needs at least C++20. Reference qb-core / qb-io types from the qb Factbook —
do not restate them here.

---

<!-- llms-txt:lead -->
> qbm-redis is the Redis client of the qb Actor Framework (QBAF), C++20-first: a non-blocking
> client that speaks RESP2 and RESP3 directly over one qb-io TCP or TLS session, with **no
> hiredis** and no external Redis dependency — the full command surface, pub/sub, transactions,
> scripting, streams and cluster operations, with the same method names for `co_await` and
> callback styles. A **compiled** library — link `qbm::redis` — behind one umbrella header,
> `<qbm/redis/redis.h>`.

Six rules decide whether generated qbm-redis code is correct; everything else is detail.

1. **The callback is the FIRST argument, not the last.** `redis.get(cb, "k")`, never
   `redis.get("k", cb)`. The callback overloads are SFINAE-gated on
   `std::is_invocable_v<Func, Reply<T>&&>`, so a wrong callback signature silently fails to
   select the overload instead of erroring where you wrote it.
2. **The client is single-threaded.** One I/O thread, one in-flight accessor; the reply queue
   and outbound pipe are unsynchronized. `command()` serializes into the outbound pipe, then
   registers the reply handler before any byte leaves; replies match commands positionally.
3. **`set_command_timeout` is a connection watchdog, not a per-command deadline.** On expiry
   it drops the whole connection, because a FIFO cannot fail one command mid-queue; pending
   commands fail with "command timed out". Blocking commands (`BLPOP`, `BRPOP`, `BLMOVE`,
   `BLMPOP`, `BRPOPLPUSH`, `BZPOPMIN`, `BZPOPMAX`, `BZMPOP`, `WAIT`, `WAITAOF`, `XREAD`,
   `XREADGROUP`) suspend that deadline while in flight.
4. **A session is a handshake you configure, and only a peer's loss is recovered.**
   `with_resp3()`, `with_auth(user, pw)`, `with_select(db)` and `set_handshake(hook)` run on
   every connection, the first one included, before any command you submitted; `connect()`
   resolves and `is_connected()` is true once they did. Commands submitted while not ready are
   held; a refused handshake (WRONGPASS, NOPERM) is terminal. An armed
   `enable_auto_reconnect(policy)` recovers a peer's loss; an explicit `disconnect()` is
   terminal (`state()` `down`, `down_reason()` `user`). In-flight commands fail once, held ones
   when the client goes down or at the command timeout: nothing waits for ever. A consumer
   replays its CONFIRMED subscriptions and marks the hole: one `message` with `is_gap()` (an
   empty channel) before the new connection's first message. On a consumer a successful `reset()`
   clears the predicted subscriptions, and Pub/Sub commands fail locally while it is pending.
5. **An empty required argument list is a failed `Reply`, not a bad frame.** `del`, `exists`,
   `sadd`, `hmget`, `geoadd`, `pfcount` and friends with nothing to act on send no frame and
   resolve with `ok() == false` and a reason in `error()`. **There is no silent no-op left** —
   every guard resolves the callback/awaiter, including the callback cursor forms of
   `hscan` / `zscan` and `lpos`, which used to `return` unfired. Do not pre-guard for it, and
   do not read "callback did not fire" as the empty-argument signal: check `ok()`.
6. **Units are Redis's, not `std::chrono`'s, at the command boundary.** `EXPIRE`/`SETEX`/`TTL`
   are seconds and `PEXPIRE`/`PSETEX`/`PTTL` are milliseconds, natively; the chrono overloads
   are ergonomic wrappers that forward `.count()`, and a raw `long long` bypasses the
   type-level unit check. Only `connect()` timeouts, `RetryPolicy` delays and
   `set_command_timeout` are `qb::duration`. Reply containers are qb-core types, not `std::`:
   `smembers` yields `qb::unordered_set<std::string>`, `hgetall` yields `qb::unordered_map<…>`.
<!-- /llms-txt:lead -->

## 1. Mental model

- **One client = one connection.** `qb::redis::tcp::client` owns a single
  qb-io socket and a FIFO reply queue. It is **not thread-safe**: drive it from a
  single I/O thread or actor strand, one in-flight accessor at a time.
- **MONITOR owns a dedicated connection.** `monitor(callback)` first reports its
  `+OK` admission, then every command line, then one failed reply when RESET,
  QUIT or disconnect ends the stream. Ordinary commands fail locally while it is
  active; after reconnect, issue MONITOR again yourself.
- **Two completion models, same method name — no `_async` suffix.** Every
  command has:
  - a **coroutine** overload (no callback arg) returning an awaiter you
    `co_await` to get `Reply<T>`;
  - a **callback** overload whose **first** argument is a `Func` invocable with
    `Reply<T>&&`, returning the client for fluent chaining.
  Pick one style per call stack.
- **RESP3 is opt-in — you must send `HELLO 3` yourself.** The parser supports
  RESP3, but `connect()` only switches the protocol object and starts I/O; it
  never issues HELLO, so the session stays RESP2 until you call
  `co_await client.hello(3)` (and `consumer.hello(3)`) as the first command
  after connect. Until then, RESP3 `PUSH` frames (pub/sub, cache invalidation)
  are inactive. Once enabled, those PUSH frames are out-of-band and never pop a
  command-reply handler.
- **Redis command errors are not exceptions.** A `WRONGTYPE`/`ERR` reply arrives
  as a normal `Reply<T>` with `ok() == false` and `error()` set. Exceptions
  (`qb::redis::*Error`, all `std::exception`-derived) are reserved for the
  connect/parse layer and the sync entry paths.
- **`Reply<T>` is the universal result.** `T` is the decoded type:
  `qb::redis::status` (OK strings), `long long`, `double`, `bool`,
  `std::optional<std::string>`, `std::vector<...>`, qb-core containers
  (`qb::unordered_map`, `qb::unordered_set`), `qb::json`, domain structs.
  RESP3 attributes are unwrapped for typed values and nested generic container elements;
  decorated errors keep the server message. `raw()` owns the complete original value,
  and `attributes()` borrows its outermost `const parser::Map *` (null when absent).
  Metadata blocks are not merged; moving or releasing `raw()` transfers their lifetime
  (`src/qbm/redis/reply.h:1292-1306`).
- **Command groups are CRTP mixins** (`key_commands<Derived>`,
  `string_commands<Derived>`, …) composed into the client. You never
  instantiate them directly. Method names are lowercase Redis verbs, with
  deliberate renames to dodge std/keyword clashes: `COPY`→`copyKey`,
  `SORT`→`sortKey`/`sortKeyStore`/`sortKeyRo` (and `sortKeyGet`/`sortKeyRoGet`
  for `GET` patterns, whose elements may be nil), `MOVE`→`move`.
- **Pipelining is implicit.** Issue several callback-form commands without
  awaiting; each enqueues one handler, bytes go out in order, replies return
  positionally (FIFO). Drain with `await()`.
- **Raw command arguments follow RESP bulk count.** `std::vector<char>` is one
  binary bulk (including an empty vector); a disengaged `std::optional` emits
  none. Containers, pairs and tuples expand their present elements. Floating
  arguments, including numeric `json_value` and `qb::json` raw-command values,
  use a locale-independent decimal spelling that round-trips to the original
  value, so small increments remain nonzero on the wire. Numeric `qb::json`
  integers retain their signed or unsigned 64-bit decimal value, including
  unsigned values above `INT64_MAX`.
- **Administrative JSON preserves the command contract.** `role()` returns
  native positional arrays for master, replica and Sentinel; names remain strings
  and offsets remain integers. `command_stats()` reads `INFO commandstats` into
  an object with the exact `cmdstat_*` keys and complete raw text values. Errors
  keep their original `error()` and `raw()`; generic `info()` still returns a
  JSON string. `client_kill(..., skipme)` always emits `SKIPME yes` or `no`.
- **RetryPolicy** (qb-io's `qb::io::reconnect_policy`, shared with qbm-http) drives
  connect-with-retry and auto-reconnect (exponential backoff). A reconnect replays the
  configured handshake and a consumer's confirmed subscriptions, never an in-flight command.
- **HSCAN preserves `Out` across pages.** The cursor coroutine and automatic callback
  forms use the selected container (default `qb::unordered_map<std::string,std::string>`).
  Automatic scans require owning output: nested standard containers/wrappers of
  `std::string_view` are rejected. For borrowed views, use cursor pages and retain each
  `Reply` that owns their raw data. Custom parse-result types carry their own ownership
  responsibility. Terminal page errors survive aggregation; ordinary page setup or merge
  exceptions fail the automatic scan once. Completion callback exceptions are logged and
  cannot trigger a second completion (`hash_commands.h:49-62`, `:88-101`, `:131-182`, `:675-684`).
- **`AsyncResult<std::string>` stores string success values.** Construct an error explicitly
  with `AsyncResult<T>::from_error(std::string)` (also on `void`); an empty message still
  means failure. Other supported payload types retain their existing constructors
  (`server_reply.h:357-389`, `:435-450`).

### Time / units (critical boundary)

- **Framework-side time is `qb::duration`**: connect timeout, `RetryPolicy`
  delays/`connect_timeout`, `set_command_timeout`, `debug_sleep`. Defaults:
  `RetryPolicy.initial_delay` 100 ms, `max_delay` 30 s, `connect_timeout` zero
  (= the 3 s of `connect()`);
  `connect()` timeout 3 s; `command_timeout` `qb::duration::zero()` (disabled).
- **Redis command time args keep native wire units** via `std::chrono`-unit
  overloads — a documented boundary, **NOT** `qb::duration`:
  - **Seconds:** `EXPIRE`, `EXPIREAT`, `SETEX` → `std::chrono::seconds` (`GETEX`'s
    `EX` path is a raw `long long`, not a chrono overload).
  - **Milliseconds:** `PEXPIRE`, `PEXPIREAT`, `PSETEX`, `GETEX` with `PX`, `WAIT` →
    `std::chrono::milliseconds`. Each forwards `.count()` to a raw `long long`
    overload.
  - **`RESTORE` ttl and `MIGRATE` timeout** take a raw `long long` in
    milliseconds — **no `std::chrono` overload exists**.
- **Reply TTLs are plain integers** (`ttl`/`expiretime` = seconds,
  `pttl`/`pexpiretime` = ms). The unit lives in the method name.
- **NEVER write** `qb::Timestamp`, `qb::Duration`, `qb::TimePoint`,
  `to_timestamp(`, `to_time_point(` — they are not part of this API.

---

## 2. Core concepts with snippets

All snippets assume `#include <qbm/redis/redis.h>`. From synchronous code (`main`,
tests, scripts), call `qb::io::async::init()` once, then pump any awaitable to
completion with `qb::io::async::run_sync(...)`. Inside a coroutine on the loop,
`co_await` directly.

### Connect

```cpp
qb::redis::tcp::client redis{qb::io::uri{"tcp://127.0.0.1:6379"}};
// tcp:// or redis://: plaintext. The client alias fixes the transport type.
// URI is endpoint-only: credentials and DB are NOT parsed from it;
// call co_await redis.auth(...) / redis.select(n) after connect.

// coroutine: the connect awaiter yields bool
if (!co_await redis.connect())               // or redis.connect(qb::io::uri{...})
    co_return;                               // optional qb::duration timeout arg

// callback form: connect(func, uri = current, timeout = 3s); func takes bool
redis.connect([](bool ok) { /* ... */ });

// from sync code:
// if (!qb::io::async::run_sync(redis.connect())) return 1;
```

A coroutine parked on a command shows as `"redis"` in qb's
`CoroutineScheduler::dump()` (on `connect()`, `"redis connect"`) when its thread
tracks suspensions (3.3).

Enable RESP3 yourself: issue `co_await redis.hello(3)` as the first command
after connect (the session is RESP2 until you do). TLS requires the separate
`qb::redis::tcp::ssl::client` alias, compiled only with `QB_HAS_SSL`; changing
the URI of `tcp::client` to `rediss://` cannot change its plaintext transport:

```cpp
#ifdef QB_HAS_SSL
qb::redis::tcp::ssl::client secure_redis{qb::io::uri{"rediss://redis.example:6380"}};
secure_redis.set_ssl_root_cert("ca.pem"); // private CA; keep default peer verification enabled
// For mutual TLS, also set_ssl_client_certificate("cl.pem", "cl.key") before connect.
if (!co_await secure_redis.connect())
    co_return;
#endif
```

The system trust store and peer verification are the defaults. Use
`set_verify_peer(false)` only when deliberately accepting an unverified
trusted/self-signed endpoint; a private CA alone calls for `set_ssl_root_cert`,
not disabled verification. Set TLS options before `connect()`.

### Basic commands and Reply<T>

```cpp
co_await redis.set("greeting", "hello");                 // Reply<qb::redis::status>
qb::redis::Reply<std::optional<std::string>> g = co_await redis.get("greeting");

if (g.ok() && g.value().has_value())                     // ok() = sent + non-error
    use(*g.value());                                     // value() aliases result()
std::string s = g.value_or("");                          // empty if !ok() or nil

co_await redis.del("greeting");                          // Reply<long long>
auto n = co_await redis.incr("counter");                 // Reply<long long>

// callback form — func is the FIRST argument, takes Reply<T>&&:
redis.get([](qb::redis::Reply<std::optional<std::string>>&& r) {
    if (r.ok() && r.result()) { /* ... */ }
}, "greeting");
```

`Reply<T>`: `ok()`, `result()`/`value()`, `value_or(default)`, `error()`
(a `std::string`, empty when ok), `raw()` (owning `parser::Value`), `attributes()`
(borrowed outermost RESP3 metadata map, null when absent).

### Key TTLs — the seconds/milliseconds boundary

```cpp
using namespace std::chrono_literals;
co_await redis.expire("k", 60s);     // EXPIRE  — seconds   (std::chrono::seconds)
co_await redis.pexpire("k", 1500ms); // PEXPIRE — milliseconds
co_await redis.set("k", "v", 30s);   // SET ... PX (chrono overload is ms-based)
co_await redis.setex("k", 60s, "v"); // SETEX  — seconds
co_await redis.psetex("k", 1500ms, "v");                 // PSETEX — milliseconds

auto t  = co_await redis.ttl("k");   // Reply<long long>, value is SECONDS
auto pt = co_await redis.pttl("k");  // Reply<long long>, value is MILLISECONDS
```

`getex` is asymmetric: the `long long` overload emits `EX` (seconds), the
`std::chrono::milliseconds` overload emits `PX` (milliseconds).

### Pub/Sub (dedicated consumers, never the plain client for SUBSCRIBE)

```cpp
// Callback consumer: set handlers, then subscribe.
qb::redis::tcp::cb_consumer consumer{qb::io::uri{"tcp://127.0.0.1:6379"}};
consumer.on_message([](qb::redis::message&& m) {
    handle(m.channel, m.payload);
});
co_await consumer.connect();
co_await consumer.hello(3);                 // RESP3 push frames
co_await consumer.subscribe("alerts");      // also subscribe(vector), psubscribe(...)

// Coroutine consumer: pull sequentially; receive() yields std::nullopt on close.
qb::redis::tcp::co_consumer rx{qb::io::uri{"tcp://127.0.0.1:6379"}};
co_await rx.connect();
co_await rx.hello(3);
co_await rx.subscribe("alerts");
while (auto msg = co_await rx.receive()) { handle(msg->channel, msg->payload); }

// Publish from a SEPARATE full client (publish() lives only on the client):
qb::redis::tcp::client pub{qb::io::uri{"tcp://127.0.0.1:6379"}};
co_await pub.connect();
co_await pub.publish("alerts", "server restarted");      // Reply<long long> (#receivers)
```

`co_consumer` buffers messages in a bounded channel (default capacity 8192). Its stream ends only
when the consumer is DOWN: `receive()` yields what was queued, then `std::nullopt`, and
`down_reason()` says why. A loss an armed run recovers from does not end it: the handshake replays
the subscriptions the server confirmed (RESET forgets them), and ONE gap marker -- `is_gap()`, an
empty channel, `gap.reason` reconnect -- is queued before the new connection's first message. A full
queue is a gap too: `on_message_dropped(cb)` fires for each loss (or a warning logs) and one marker
carries the count (`gap.reason` overflow, `gap.lost`). `cb_consumer` delivers the same marker through
`on_message`; `on_gap(cb)` is told on both.

### Transactions (MULTI/EXEC)

```cpp
co_await redis.watch("balance");            // optimistic lock (abort on change)
co_await redis.multi();                     // queued commands now return QUEUED
co_await redis.set("balance", "100");
co_await redis.incr("balance");
auto exec = co_await redis.exec<long long>();            // Reply<std::vector<long long>>
// exec.ok()==false means EITHER an abort OR a decode error -- they are the same shape.
// Discriminate: if (exec.raw() && exec.raw()->is_null()) { /* watched key changed, retry */ }
// raw() is nullptr on the disconnect/failure paths, so the && is load-bearing.
// For mixed reply types, exec<qb::redis::pipeline_result>() and read exec.raw().
// Abort manually: co_await redis.discard();  clear watches: co_await redis.unwatch();
```
NEVER read an intermediate reply's VALUE between `multi()` and `exec()`. `+QUEUED` is a RESP
SimpleString and nothing special-cases it: a `std::string` / `std::optional<std::string>` reply comes
back `ok() == true` carrying the literal `"QUEUED"`, a `status` reply comes back `Reply::ok() == true`
with only the inner `status::ok()` false, and only integer/bool/double/container replies fail loudly.
`is_in_multi()` tells you whether a reply is an acknowledgement rather than a result.

### Scripting (EVAL / EVALSHA)

```cpp
// Decode type Ret is an explicit template arg. Do NOT pass numkeys — the library
// derives it from keys.size().
auto sha = co_await redis.script_load("return redis.call('GET', KEYS[1])");  // Reply<std::string>
auto v = co_await redis.evalsha<std::optional<std::string>>(
             sha.value(), /*keys*/ {"greeting"}, /*args*/ {});
auto out = co_await redis.eval<long long>(
               "return tonumber(ARGV[1]) + 1", {}, {"41"});
// evalRo / evalshaRo map to EVAL_RO / EVALSHA_RO (server-enforced read-only).
```

### Pipelining and await

```cpp
// Callback form, no awaits between → batched, replies in FIFO order:
redis.set([](auto&&){}, "a", "1")
     .set([](auto&&){}, "b", "2")
     .incr([](auto&&){}, "a");
redis.await();                              // drains pending callbacks (non-blocking poll)
// pending_reply_count() reports queue depth.
```

`await()`/`flush()` spin `qb::io::async::listener::current.run(EVRUN_NOWAIT)` —
call only from the thread that drives this client's I/O. A coroutine body may call
them (since 3.3, Huly QB-253), but no coroutine resumes until it yields — the
`set_command_timeout()` watcher included, so there they return on the replies or a
disconnect only -- and never on a command HELD while the client is not ready: the
connection, its handshake and the reconnection run are coroutines too. In a coroutine,
`co_await` the command. (`RedisPipeline::flush()` is unrelated to Redis `FLUSHDB`/`FLUSHALL`.)

### Generic escape hatch

```cpp
// Any command without a typed wrapper:
auto r = co_await redis.command<qb::json>("COMMAND", "GETKEYS", "SET", "k", "v");
```

### Error handling

```cpp
auto r = co_await redis.get("k");
if (!r.ok()) {
    log(r.error());                         // std::string: Redis/protocol message
    co_return;
}
// Connect failure → connect awaiter yields false (no throw).
// Sync helpers / parse layer may throw qb::redis::ConnectionError, CommandError,
// TimeoutError, ProtoError, ReplyParseError, AuthError — all derive from
// qb::redis::Error : std::exception (e.what()). SecurityError (an argument over
// 512 MiB) never reaches you: the command fails with "String too large", leaving
// no handler and no partial frame (Huly QB-255).
```

---

## 3. Invariants

- The client and consumers are **single-threaded**: one I/O thread/strand, one
  in-flight accessor. The reply queue and outbound pipe are unsynchronized.
- `command()` registers the reply handler **before** sending bytes → pipelining
  is FIFO-safe; replies match commands positionally.
- `set_command_timeout` is a **connection-health watchdog, not a per-command
  deadline**: on deadline it **drops the whole connection** (FIFO cannot fail one
  mid-queue command). Pending commands fail with "command timed out";
  auto-reconnect resumes if enabled -- the watchdog's drop is recoverable. It is also
  how long a HELD command waits for the client to be ready.
- **Blocking commands** (`BLPOP`/`BRPOP`/`BLMOVE`/`BLMPOP`/`BRPOPLPUSH`/
  `BZPOPMIN`/`BZPOPMAX`/`BZMPOP`/`WAIT`/`WAITAOF`/`XREAD`/`XREADGROUP`) **suspend
  the command deadline** while in flight; their server-side timeout governs.
- **The reconnect contract** (`<qb/io/reconnect.h>`): `state()` walks `idle`,
  `connecting`, `handshaking`, `ready`, `reconnecting`, `down`. A loss fails every
  in-flight reply once, with `"disconnected"`; the unsent bytes go with them. Work
  submitted while `connecting` / `handshaking` / `reconnecting` is HELD and leaves
  after the handshake; at `down` it fails with the reason (`"disconnected"`,
  `"reconnection attempts exhausted"`, `"handshake refused: …"`), on an idle or down
  client on the next loop pass. `disconnect()` has failed what was in flight by the
  time it returns, and it is terminal -- from a failure callback the loss runs, too (a
  `connect()` there owns the client). `on_connected` / `on_disconnected(reason)`
  strictly alternate; `on_disconnected` runs after the in-flight work failed and
  before the reconnection decision, so disarming the policy there prevents the run.
- **Sharded pub/sub** (Redis 7+, Huly QB-127): `consumer.ssubscribe(ch)` /
  `sunsubscribe(ch)`, `client.spublish(ch, msg)`; an `smessage` is delivered as a
  `message`. On a cluster the client follows no redirect: talk to the node that owns
  the channel's slot, and keep the channels of one call in one slot.
- **Observability** (`<qbm/redis/stats.h>`, Huly QB-134): `stats()` is a snapshot of
  monotonic counts -- a client's `commands` / `errors` / `failed` / `held` / `by_family`,
  `reconnects`, a consumer's `gaps` / `messages_received` / `messages_dropped`.
  `set_latency_stats(true)` turns on per-family histograms (`latency(family)`,
  p50/p95/p99 = `percentile(0.50/0.95/0.99)`); `on_command_complete(fn)` sees every
  completed command (`command_info`: verb, family, outcome, error, duration) before its
  callback. Off, both cost one branch. To feed a metrics actor, push a COPY from the hook:
  its `std::string_view`s die with the call; never drive the client from it.
- RESP3 `PUSH` frames must not pop a command handler; the plain client discards
  them, the consumer routes them. Mishandling desyncs the FIFO permanently.
- A throwing user callback at a noexcept dispatch boundary (`onMessage`,
  `on(disconnected)`) is caught and logged, never terminating the process.
- `TReply` constructs the successful `Reply<T>` directly from its decoded value before
  invoking the callback. Ordinary replies use `decode<false>`; attributed replies go through
  the private out-of-line `dispatch_attributed` member, which invokes `func(decode<true>(raw))`.
  The attributed decoder unwraps before classifying the inner server error. Both decoder
  instantiations retain the original raw owner on failure and use public `parse<T>`, preserving
  customizations. The callback runs after decoding returns, outside its catches, whether invoked
  by the operator or the cold member; a callback exception cannot cause a second completion.
  Attribute traversal is iterative, with no new depth limit beyond the configured wire parser.
  _(src/qbm/redis/reply.h:1366-1388,1391-1396,1400-1429; src/qbm/redis/reply.cpp:31-40)_

---

## 4. Gotchas

- **Callback is the first arg, not the last.** `redis.get(cb, "k")`, not
  `redis.get("k", cb)`. Callback overloads are SFINAE-gated on
  `std::is_invocable_v<Func, Reply<T>&&>` — a wrong signature silently fails to
  match the overload rather than erroring at the call site.
- **Auto-iterating scanners and multi-key `hvals` are callback-only.** The
  no-cursor `sscan`/`zscan`/`hscan` and fan-out `hvals` buffer the whole result
  and fire once; **no coroutine form**, and a throwing callback is caught/logged,
  not propagated. Use the explicit-cursor `scan(cursor,...)` overloads to bound
  memory on large keyspaces.
  Automatic SCAN/SSCAN stop on a failed page and retain its error plus accumulated items;
  multi-key HVALS keeps the first explicit error and all successful sibling values. Those
  aggregates have no single raw reply. CONFIG GET and coroutine TIME transform one reply
  and preserve both its `error()` and `raw()`.
- **Empty required args → a failed `Reply`, not a bad frame.** A required-variadic
  or required-collection command with nothing to act on (`del`/`exists`/`touch`/
  `unlink` with no keys, `lpush`/`rpush`, `sadd`/`srem`, `sdiffstore`/`*store`,
  `hdel`/`hmget`/`hmset`, `zmpop`/`bzmpop`, `geoadd`, `pfcount`/`pfmerge`,
  `script exists`, …) sends no frame and resolves the callback/awaiter with
  `ok()==false` (reason in `error()`) via `fail_client` — uniform, never a silent
  no-op or a malformed command.
- **No callback silently no-ops any more.** The callback **cursor** forms
  `hscan`/`zscan` on an empty key, and `lpos` on an empty key/element, used to
  `return` without firing the callback — which parked the coroutine form forever.
  They now resolve like every other guard, with `ok()==false`.
- **Multi-stream `xread`/`xreadgroup` reject mismatched vectors locally** with
  `std::invalid_argument` when `keys` is empty or `keys.size() != ids.size()`.
  The callback overload checks during the call. The coroutine overload only captures
  its arguments at construction; its callback operation, including validation, starts
  in `redis_awaiter::await_suspend`. Catch around `co_await`, not just construction:

  ```cpp
  try {
      auto pending = redis.xread(keys, ids); // no Redis request or size validation yet
      auto reply = co_await pending;         // may throw invalid_argument before enqueue
      if (!reply.ok()) { /* inspect Redis/transport error in reply.error() */ }
  } catch (const std::invalid_argument &) { /* local key/ID mismatch */ }
  ```

  This local exception is not delivered as a failed `Reply`. An inline
  `co_await redis.xread(keys, ids)` inside the same `try` has the same handling.
- **`pexpire`/`expire` unit mistakes:** `expire(k, 60)` and `expire(k, 60s)` both
  mean 60 **seconds**; `pexpire(k, 60ms)` is 60 ms. Passing a raw `long long`
  bypasses the type-level unit check.
- **`exec` returns a vector**; an aborted transaction (watched key changed)
  surfaces as `!ok()`/nil, not an exception. For heterogeneous queued replies use
  `exec<pipeline_result>()` and read `.raw()` (entries are move-only).
- **A missing key's nil is a value, not a failure.** Counted `lpop`/`rpop` yield an
  empty vector and `memory_usage` yields `0` (`raw()` keeps the server's nil);
  `sortKey` elements are strings, so a `GET` of a missing key fails the reply —
  `sortKeyGet`/`sortKeyRoGet` keep it as `std::nullopt`.
- **Reply containers are qb-core types**, not `std::`: `smembers` →
  `qb::unordered_set<std::string>`; `hgetall`/`zscan` → `qb::unordered_map<...>`.
- **`reset()` after disconnect** does not re-establish subscriptions; consumers
  must re-`subscribe`.

---

## 5. Build / integration (one-liner)

```cmake
add_subdirectory(qb)                                  # framework first (CMake guards on QB_FOUND)
qb_load_modules("${CMAKE_CURRENT_SOURCE_DIR}/qbm")    # discovers qbm modules
target_link_libraries(your_app PRIVATE qbm::redis)    # PUBLIC include + cxx_std_${QB_CXX_STANDARD}
```

Then `#include <qbm/redis/redis.h>`. Do not hand-add the include dir — it arrives
`PUBLIC` from `qbm::redis`. TLS (`qb::redis::tcp::ssl::*`) exists only when the
framework was built with `QB_HAS_SSL` (OpenSSL); otherwise plaintext TCP only.
