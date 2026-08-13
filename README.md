# qbm-redis — asynchronous Redis client for the qb Actor Framework (QBAF)

> **Audience:** Adopter · **Status:** stable · **Verified-against:** qbm-redis @ qb 3.0.0 (C++20 default, C++23
> supported)

A non-blocking Redis client for the qb Actor Framework (QBAF), built on qb-io: connect, run the full command surface,
drive pub/sub, transactions, scripting, streams, and cluster operations — with the same method names for `co_await`
and callback styles, over a native RESP2/RESP3 parser with no `hiredis` dependency.

**Prerequisites:** a working qb framework checkout (see [qb/README.md](https://github.com/isndev/qb/blob/main/README.md)) and a reachable Redis
server — **See also:** [readme/README.md](./readme/README.md) (technical
index), [readme/actors.md](./readme/actors.md), [readme/connection.md](./readme/connection.md), [readme/commands_overview.md](./readme/commands_overview.md), [readme/pipeline_and_await.md](./readme/pipeline_and_await.md).

---

## A Redis actor

A client is an ordinary qb-io object: it binds to the event loop of the thread that constructs it, and inside a
`qb::Main` that thread is a `VirtualCore`. So an actor that holds one needs **no pump and no drain** — the core runs its
loop once per pass, before it dispatches your handlers, and that pass is what carries RESP bytes both ways. What the
actor owes back is one rule: **never stop returning to the loop.**

```cpp
#include <qb/actor.h>
#include <qb/main.h>
#include <qbm/redis/redis.h>
#include <memory>
#include <string>

// An event is relocated by memcpy, so its payload must be trivially RELOCATABLE:
// qb::string<N> when the value is bounded, a heap-owned handle when it is not.
struct SessionLookup : qb::Event {
    qb::string<128> key;
    explicit SessionLookup(std::string const &k) : key(k) {}
};
struct SessionValue : qb::Event {
    std::shared_ptr<std::string> value;   // null when the key is missing or the command failed
    explicit SessionValue(std::shared_ptr<std::string> v) : value(std::move(v)) {}
};

class SessionCache : public qb::Actor {
    // shared_ptr, not a plain member: a coroutine parked on a reply must be able to keep the
    // client alive even if the actor is destroyed underneath it.
    std::shared_ptr<qb::redis::tcp::client> _redis;

public:
    qb::io::async::task<bool> onInit() override {
        registerEvent<SessionLookup>(*this);
        registerEvent<qb::KillEvent>(*this);

        _redis = std::make_shared<qb::redis::tcp::client>(qb::io::uri{"tcp://127.0.0.1:6379"});
        _redis->set_command_timeout(std::chrono::seconds(5));   // bound every reply
        co_return co_await _redis->connect(std::chrono::seconds(2));
    }

    void on(SessionLookup const &ev) {
        auto              redis  = _redis;        // copy ALL of it BEFORE spawning
        std::string       key    = ev.key.c_str();
        const qb::ActorId sender = ev.getSource();

        spawn([redis, key, sender](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            auto reply = co_await redis->get(key);           // Reply<std::optional<std::string>>
            ctx.push_to<SessionValue>(
                sender, reply.ok() && reply.result()
                            ? std::make_shared<std::string>(*reply.result())
                            : nullptr);
        });
    }

    void on(qb::KillEvent const &) {
        if (_redis)
            _redis->disconnect();   // fails every pending reply, so nothing is left parked
        kill();
    }
};
```

Four decisions carry the whole model, and [A Redis actor](./readme/actors.md) is the page for all four:

- **`onInit()` is where you connect.** It is a coroutine, so the handshake reads as a straight line. While it is
  suspended the actor is *Activating* — the engine holds its inbound business events and replays them once init
  succeeds — so nothing is served against a half-open connection. Give `connect` a deadline rather than relying on the
  default: the connect awaiter is one of the few things here that nothing else can resolve if the peer never answers.
- **An event handler must return, so suspending work goes to `Actor::spawn`.**
- **A spawned coroutine may not touch the actor's client after a `co_await`** — the actor, and any member of it, may
  have been destroyed while the coroutine was parked. Hence the `std::shared_ptr` the handler copies before spawning.
  What is *not* correct is one client shared between actors: the reply queue and the outbound pipe are unsynchronised,
  and sharing corrupts the FIFO ordering RESP depends on. Give each actor its own connection, or put Redis behind a
  single actor and send it events.
- **The kill handler disconnects *before* it kills**, so no coroutine is left parked on a reply that will never come.

One framework rule shows through in the event definitions: **an event payload must be trivially *relocatable*, not
merely copyable**, because the engine moves events with `memcpy` and never runs the source destructor. A by-value
`std::string` is not — on libstdc++ a short one addresses its own inline buffer — so a bounded value goes into a
`qb::string<N>` and an unbounded one behind a `std::shared_ptr` or a `std::vector`. There is no compile-time check and
there cannot be one. See [Inter-actor messaging](https://github.com/isndev/qb/blob/main/readme/4_qb_core/messaging.md).

### Pub/sub: a consume loop that ends by itself

`co_consumer::receive()` is the one awaiter in this module that behaves differently, and usefully so. It does not return
a `redis_awaiter`; it awaits a `qb::io::async::channel<message>` the consumer fills from its RESP push frames. A channel
`recv()` is still not cancellation-aware, but it *is* woken by `close()` — and the consumer closes the channel when the
connection drops and again in its own destructor. That gives a subscribe loop a clean termination condition with no
cancellation machinery at all:

```cpp
struct Broadcast : qb::Event {
    std::shared_ptr<std::string> payload;   // heap-owned: nothing points into the event
    explicit Broadcast(std::string p) : payload(std::make_shared<std::string>(std::move(p))) {}
};

class EventFanout : public qb::Actor {
    std::shared_ptr<qb::redis::tcp::co_consumer> _sub;

public:
    qb::io::async::task<bool> onInit() override {
        registerEvent<Broadcast>(*this);
        registerEvent<qb::KillEvent>(*this);

        _sub = std::make_shared<qb::redis::tcp::co_consumer>(qb::io::uri{"tcp://127.0.0.1:6379"});
        if (!co_await _sub->connect())
            co_return false;
        if (!(co_await _sub->subscribe(std::string{"events"})).ok())
            co_return false;

        auto              sub  = _sub;          // copied into the frame
        const qb::ActorId self = id();
        spawn([sub, self](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            // Ends when the channel closes — i.e. on disconnect or destruction.
            while (auto msg = co_await sub->receive())
                ctx.push_to<Broadcast>(self, msg->payload);
        });
        co_return true;
    }

    void on(Broadcast const &ev) {
        // Back on the actor's own thread, in the ordinary dispatch path.
        qb::io::cout() << "event: " << *ev.payload << '\n';
    }

    void on(qb::KillEvent const &) {
        if (_sub)
            _sub->disconnect();   // closes the channel → receive() yields nullopt → the loop ends
        kill();
    }
};
```

The `disconnect()` is load-bearing. Without it, `kill()` leaves the loop parked on `receive()` forever and the actor's
destructor runs anyway, because nothing waits for a coroutine.

### What cancellation does, and does not

Every one of the 200-plus command methods returns a `redis_awaiter`, and **it is not cancellation-aware**: it registers
no `on_cancel` hook and consults no token, so `cancel()` — and therefore `kill()` — neither wakes nor unwinds a
coroutine parked on one. It is a callback bridge carrying a `shared_ptr<bool>` that its destructor clears, so a reply
landing after the awaiter is gone is a silent no-op rather than a use-after-free.
<!-- src: qbm/redis/src/qbm/redis/redis.h:711-726 (await_ready false; await_suspend stores the handle and launches the operation — no token, no hook) -->

A parked command therefore ends in exactly four ways: the server replies; the link drops (`disconnect()`, a dead peer,
or the command deadline tripping) and `on(disconnected)` fails **every** queued handler in order, so the `co_await`
resumes with `ok() == false`; someone destroys the coroutine frame and the late reply is dropped; or it stays parked.
The second row is the one to build on — and it is why the kill handler above disconnects first. To bound a command
explicitly inside an actor, wrap it in `ctx.cancellable(...)`, or set `set_command_timeout(...)` as the actor above
does.

---

## Two styles, one method name

Each command group is a CRTP mixin that injects two overloads per command:

- **Coroutine (awaitable):** the no-callback overload returns an awaiter that yields `Reply<T>`.
  `co_await redis.get("k")` suspends the coroutine until the reply arrives, without blocking the event loop.
- **Callback:** the overload whose first parameter is the callback. It is SFINAE-gated on
  `std::is_invocable_v<Func, Reply<T>&&>`, so the callback must accept exactly `Reply<T>&&`. It returns `Derived&` for
  chaining and is the basis of pipelining.

```cpp
// Coroutine
auto r = co_await redis.get("user:1");           // Reply<std::optional<std::string>>

// Callback (same name, callback first)
redis.get([](qb::redis::Reply<std::optional<std::string>>&& r) {
    if (r) qb::io::cout() << r.value_or(std::string{"(nil)"}) << '\n';
}, "user:1");
```

<!-- src: qbm/redis/src/qbm/redis/commands/string_commands.h:165-184, FACTBOOK.md redis-collections invariants -->

`value_or` deduces its return type from both branches, so hand it a `std::string` rather than a string literal —
`value_or(std::string{"(nil)"})`, not `value_or("(nil)")`, which does not compile.

### Reply\<T\>

`Reply<T>` is the typed result wrapper. Inspect it directly:

| Member                            | Meaning                                                                                                        |
|:----------------------------------|:---------------------------------------------------------------------------------------------------------------|
| `ok()` / `explicit operator bool` | `true` when the command succeeded (no Redis error, not disconnected).                                          |
| `result()` / `value()`            | The decoded value of type `T`.                                                                                 |
| `value_or(default)`               | Returns the value, or `default` when `!ok()` — and for `std::optional<T>` results, when the optional is empty. |
| `error()`                         | The error string when `!ok()` (Redis error text, `"command timed out"`, or `"disconnected"`).                  |
| `raw()`                           | The owning `parser::Value`, needed for move-only payloads such as `pipeline_result`.                           |

Redis-side command errors do **not** throw; they surface as `ok() == false` with `error()` set. Exceptions are reserved
for protocol/connection faults.
<!-- src: qbm/redis/src/qbm/redis/reply.h:1051-1092 -->

### Driving the loop

You never block the qb-io thread. Reach completion one of three ways:

- **Inside a coroutine** that runs on the loop: `co_await` the command. This is the only correct form inside an actor.
- **From synchronous code** (`main`, tests, a CLI): `qb::io::async::run_sync(awaitable)` pumps one awaitable to
  completion. Call `qb::io::async::init()` once first.
- **Callback + drain:** issue callback commands, then call `redis.await()` (a non-blocking
  `listener::current.run(EVRUN_NOWAIT)` spin until the reply queue empties) or let your normal event loop tick.

`run_sync`, `run_once` and `await()` all stop the thread they run on until the work finishes. In a `main()` that thread
is yours; inside an actor it is the `VirtualCore`, and every other actor on it stops with you — silently, because
nothing in the framework diagnoses it.

<!-- src: qbm/redis/src/qbm/redis/redis.h:858-862 (await), qb/src/qb/io/async/coroutine/utils.h:278 (run_sync) -->

### Pipelining

Pipelining is a property of the callback API: issue multiple `command(callback, …)` calls without awaiting between them.
Each enqueues one handler and writes its bytes in order; the handler is registered **before** the bytes are sent, so a
fast reply cannot outrun its handler. Replies are matched positionally in FIFO order.

```cpp
redis.set([](qb::redis::Reply<qb::redis::status>&& r) { /* ... */ }, "a", "1");
redis.set([](qb::redis::Reply<qb::redis::status>&& r) { /* ... */ }, "b", "2");
redis.incr([](qb::redis::Reply<long long>&& r) { /* ... */ }, "counter");
redis.await();   // drains all three on the current loop
```

See [readme/pipeline_and_await.md](./readme/pipeline_and_await.md). `RedisPipeline::flush()` is unrelated to the
`FLUSHDB`/`FLUSHALL` commands.
<!-- src: qbm/redis/src/qbm/redis/redis.h:1039-1091, tests/integration/connection/pipeline.cpp:322 -->

---

## Outside an actor

A self-contained executable that drives the loop itself. `qb::io::async::run_sync(...)` pumps a single awaitable to
completion on the current thread — convenient for `main()`, tests, and scripts, and a defect inside an actor.

```cpp
#include <qb/io/async.h>
#include <qbm/redis/redis.h>

int main() {
    qb::io::async::init();

    qb::redis::tcp::client redis{qb::io::uri{"tcp://localhost:6379"}};
    if (!qb::io::async::run_sync(redis.connect()))   // connect_awaiter -> bool
        return 1;

    // SET returns Reply<status>; operator bool is true on success.
    auto set_r = qb::io::async::run_sync(redis.set("greeting", "Hello Redis!"));
    if (!set_r)
        return 1;

    // GET returns Reply<std::optional<std::string>>; value_or unwraps the optional.
    auto value = qb::io::async::run_sync(redis.get("greeting")).value_or(std::string{});
    qb::io::cout() << "Retrieved: " << value << '\n';

    qb::io::async::run_sync(redis.del("greeting"));  // Reply<long long>
    return 0;
}
```

<!-- src: qbm/redis/src/qbm/redis/redis.h:404 (connect), string_commands.h:165/568, key_commands.h:137, qb/src/qb/io/async/coroutine/utils.h:278 (run_sync) -->

Inside a coroutine you `co_await` directly. Pub/sub uses a dedicated consumer plus a separate client to publish, because
the publishing command lives only on the full client:

```cpp
#include <qb/io/async.h>
#include <qbm/redis/redis.h>

qb::io::async::task<void> chat() {
    // Subscriber: a callback consumer routes messages out-of-band.
    qb::redis::tcp::cb_consumer consumer{
        qb::io::uri{"tcp://localhost:6379"},
        [](qb::redis::message&& msg) {
            qb::io::cout() << '[' << msg.channel << "] " << msg.payload << '\n';
        }};

    co_await consumer.connect();
    co_await consumer.hello(3);                 // RESP3 for native push frames
    co_await consumer.subscribe("alerts");
    co_await consumer.psubscribe("user:*:updates");

    // Publisher: a separate client (publish() is not on the consumer).
    qb::redis::tcp::client publisher{qb::io::uri{"tcp://localhost:6379"}};
    co_await publisher.connect();
    co_await publisher.publish("alerts", "Server restarted");
    co_await publisher.publish("user:42:updates", R"({"event":"login"})");
}
```

<!-- src: qbm/redis/src/qbm/redis/redis.h:955-958 (consumer base), 1264-1301 (cb_consumer), subscription_commands.h:63/105, publish_commands.h:57 -->

Prefer to receive sequentially? Use `co_consumer` and `co_await receive()`, which yields `std::nullopt` when the channel
closes on disconnect:

```cpp
qb::io::async::task<void> notifications() {
    qb::redis::tcp::co_consumer consumer{qb::io::uri{"tcp://localhost:6379"}};
    co_await consumer.connect();
    co_await consumer.hello(3);
    co_await consumer.subscribe("notifications");

    while (auto msg = co_await consumer.receive()) {
        qb::io::cout() << "Notification: " << msg->payload << '\n';
    }
}
```

<!-- src: qbm/redis/src/qbm/redis/redis.h:1343-1417 (RedisCoroConsumer::receive) -->

---

## What this module is

`qbm-redis` speaks the Redis Serialization Protocol (RESP2 and RESP3) directly over a qb-io socket. There is no external
Redis library: the connection lifecycle, the streaming RESP parser, command serialization, reply decoding, and pub/sub
are all implemented on top of `qb::io::async`. All wire I/O is non-blocking and runs on the qb-io event loop; you reach
completion either by `co_await` or through callback overloads that the loop drains.

The public surface lives in `qb::redis` (internals in `qb::redis::detail`, the parser in `qb::redis::parser`). A single
header pulls in everything an application needs:

```cpp
#include <qbm/redis/redis.h>   // brings in <qb/io/async.h> transitively
```

`qbm-redis` is a **compiled library** (sources `redis.cpp`, `reply.cpp`, and `server_reply.cpp`), aliased `qbm::redis`.
It is **not** header-only — link the target; including the header alone will not resolve the reply-parsing and protocol
symbols.

### How it relates to qb-core

The module depends on `qb::core` at the build level (`qb_register_module(... DEPENDS qb-core)`), which transitively
brings in `qb::io`. At the API level you use qb-io types: a client is driven by whatever thread runs `qb::io::async`.
You can use it from a plain executable that calls `qb::io::async::init()` and drives the loop yourself, or hold a client
inside a `qb::Actor` and let the actor's `VirtualCore` tick drive the same loop. The client does not require actors —
but inside `qb::Main` it is what most deployments do, which is why this page opens with it.
<!-- src: qbm/redis/CMakeLists.txt:33-45, qbm/redis/src/qbm/redis/redis.h:593-614 -->

---

## Feature overview

| Area                     | What you get                                                                                                                                                                                                                                                                                                                                                                                                                        |
|:-------------------------|:------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| **Connection**           | Async connect from a URI (`tcp://host:port` or `redis://host:port` plaintext, `rediss://…` for TLS, `unix:///path`); `connect()`, `connect(uri)`, optional `qb::duration` connect timeout (default 3s); `hello(version)` to negotiate RESP2/RESP3; `select`, `swapdb`, `reset`, `quit`. The URI carries only the endpoint — credentials and DB are not parsed from it; issue `auth()`/`select()` after connect (see connection.md). |
| **TLS**                  | Available when the framework is built with `QB_HAS_SSL` (OpenSSL). Use the `qb::redis::tcp::ssl::*` aliases; `set_verify_peer(false)` disables chain + hostname verification for trusted/self-signed endpoints (set before `connect()`). Without SSL, the TCP aliases still build.                                                                                                                                                  |
| **Full command surface** | String, key, list, hash, set, sorted-set, bitmap, HyperLogLog, geo, stream, scripting, function, pub/sub, transaction, server, cluster, ACL, and module command groups — each a CRTP mixin on the client.                                                                                                                                                                                                                           |
| **Two async styles**     | Every command has a coroutine overload (`co_await redis.get(key)` yields `Reply<T>`) and a callback overload (`redis.get(cb, key)` where `cb` takes `Reply<T>&&`). Same method name, no `_async` suffix.                                                                                                                                                                                                                            |
| **Pipelining**           | Issue several callback commands without awaiting between them; replies match positionally in FIFO order. Drain with `await()` or your event loop. `tcp::pipeline` is a named wrapper.                                                                                                                                                                                                                                               |
| **Auto-reconnect**       | `enable_auto_reconnect(RetryPolicy{…})` reconnects with exponential backoff after a disconnect; delays and the connect timeout are `qb::duration`.                                                                                                                                                                                                                                                                                  |
| **Command deadline**     | `set_command_timeout(qb::duration)` arms a connection-health watchdog: if no reply arrives in the window for a non-blocking in-flight command, the connection is dropped (blocking commands suspend it).                                                                                                                                                                                                                            |
| **Pub/sub**              | `cb_consumer` delivers messages through a callback; `co_consumer` exposes `co_await receive()`. Publish from a separate `tcp::client`.                                                                                                                                                                                                                                                                                              |
| **Typed replies**        | `qb::redis::Reply<T>` carries `ok()` / `operator bool`, `result()` (alias `value()`), `value_or(default)`, and `error()`. Redis errors come back as `ok() == false`, not exceptions.                                                                                                                                                                                                                                                |

---

## Build and integration

`qbm-redis` is registered through the framework's module helper. Two supported modes, both giving the same target
(`qbm::redis`) and the same header spelling (`<qbm/redis/redis.h>`).

**Embedded** — add the framework, load the modules directory, then link the alias:

```cmake
add_subdirectory(qb)                                # qb-core + qb-io
qb_load_modules("${CMAKE_CURRENT_SOURCE_DIR}/qbm")  # registers qbm::redis (+ siblings)

add_executable(app main.cpp)
target_link_libraries(app PRIVATE qbm::redis)
```

**Installed** — consume a `cmake --install`ed tree. No `find_package(qb)` line is needed; the module's package config
resolves qb itself:

```cmake
find_package(qbm-redis CONFIG REQUIRED)             # find_dependency(qb) happens inside
target_link_libraries(app PRIVATE qbm::redis)
```

Headers land under `<prefix>/include/qbm/redis/...` and the CMake files under `<prefix>/lib/cmake/qbm-redis/`;
`<prefix>/include/qbm` is the installed spelling of the source tree's `qbm/` root, so the include line is identical in
both modes. `qbm-redisConfig.cmake` fails at configure time if the installed qb is a different version than the one
this module was compiled against, or disagrees with it about `QB_HAS_SSL`.

<!-- src: qbm/redis/CMakeLists.txt:33-45, qb/cmake/qbFunctions.cmake (qb_register_module / qb_load_modules) -->

Linking `qbm::redis` is the supported way to get the `<qbm/redis/...>` headers — the include directory is attached `PUBLIC`
by `qb_register_module`, so you do not add it by hand. Because the target propagates `cxx_std_${QB_CXX_STANDARD}` as a
`PUBLIC` usage requirement, linking it forces your target to the framework C++ standard — **C++20 by default, C++23
if `QB_CXX_STANDARD=23`**. The coroutine API (`co_await`) needs at least C++20.
<!-- src: qb/cmake/qbConfig.cmake:140 -->

Two build-time conditions are worth knowing:

- The module **silently skips itself** if the qb framework is not configured (`CMakeLists.txt` early-returns when
  `NOT QB_FOUND`). Build it inside or alongside a configured qb tree.
- The TLS aliases (`qb::redis::tcp::ssl::*`) require `QB_HAS_SSL` (OpenSSL found). Without it the build proceeds
  TCP-only — the `#ifdef QB_HAS_SSL` block in `redis.h` is excluded, and CMake prints an informational message rather
  than failing.

<!-- src: qbm/redis/CMakeLists.txt:50-53 (the NOT QB_FOUND early return), :56-58 (the TCP-only status message), qbm/redis/src/qbm/redis/redis.h:1705-1716 (the #ifdef QB_HAS_SSL ssl:: alias block) -->

A module **cannot be configured standalone**: it calls `qb_register_module()` and `qb_add_test()`, development-time
helpers an installed qb does not ship. The repository's own CI configures `.github/ci/superbuild/CMakeLists.txt`, a
minimal root that adds a qb *source* tree first and this module second.

---

## RetryPolicy and auto-reconnect

`RetryPolicy` configures exponential-backoff reconnection. Its delays and the connect timeout are **`qb::duration`** (
the framework's `std::chrono::nanoseconds` span); chrono literals convert implicitly.

| Field             | Default | Notes                                              |
|:------------------|:--------|:---------------------------------------------------|
| `max_attempts`    | `-1`    | `-1` = unlimited.                                  |
| `initial_delay`   | `100ms` | First backoff delay (`qb::duration`).              |
| `max_delay`       | `30s`   | Backoff cap (`qb::duration`).                      |
| `multiplier`      | `2.0`   | Exponential growth factor.                         |
| `jitter`          | `true`  | Randomizes each delay ±25%.                        |
| `connect_timeout` | `3s`    | Per-attempt connect timeout (`qb::duration`).      |
| `on_retry`        | none    | `void(int attempt, qb::duration next_delay)` hook. |

```cpp
using namespace std::chrono_literals;

redis.enable_auto_reconnect(qb::redis::RetryPolicy{}
    .with_initial_delay(50ms)
    .with_max_delay(2s)
    .with_connect_timeout(2s)
    .with_on_retry([](int attempt, qb::duration) {
        qb::io::cout() << "reconnect attempt " << attempt << '\n';
    }));

redis.disconnect();   // a later disconnect spawns the background reconnect task
// After reconnecting you must re-negotiate RESP3 if you rely on it:
// co_await redis.hello(3);
```

<!-- src: qbm/redis/src/qbm/redis/redis.h:207-252 (RetryPolicy), 511-513 (enable_auto_reconnect) -->

`set_command_timeout(qb::duration)` is a separate mechanism: a per-connection health watchdog. If no reply arrives
within the window for an in-flight **non-blocking** command, the whole connection is dropped (a FIFO pipelined protocol
cannot fail one mid-queue command without desynchronizing later replies) and pending commands fail with
`"command timed out"`; auto-reconnect resumes if enabled. Blocking commands (`BLPOP`, `WAIT`, `XREAD`, …) suspend the
deadline so their own server-side timeout governs. The default is `qb::duration::zero()` (disabled).
<!-- src: qbm/redis/src/qbm/redis/redis.h:670-676, 729-735, 882-893 -->

> **Time-unit boundary.** Connect/command timeouts and `RetryPolicy` delays are `qb::duration`. **Redis command
arguments keep native units by design** and are exposed through `std::chrono`-unit overloads, not `qb::duration`:
`EXPIRE`/`SETEX`/`EXPIREAT` take **seconds**, `PEXPIRE`/`PSETEX`/`PEXPIREAT` take **milliseconds**; `WAIT`/`RESTORE`/
`MIGRATE` timeouts are **milliseconds**. Reply TTLs (`ttl`, `pttl`, `expiretime`, …) come back as plain integers — the
> unit lives in the method name. Do not push these onto `qb::duration`. (The retired tokens `qb::Timestamp`,
`qb::Duration`, `qb::TimePoint`, `to_timestamp(`, and `to_time_point(` were removed from the framework and must not
> appear in your code.)
<!-- src: key_commands.h:241/416/288/464, string_commands.h:537/730 -->

---

## Client and consumer types

| Type                                                                       | Role                                                                                                  |
|:---------------------------------------------------------------------------|:------------------------------------------------------------------------------------------------------|
| `qb::redis::tcp::client`                                                   | The full command client (all command groups). Alias of `qb::redis::database<qb::io::transport::tcp>`. |
| `qb::redis::tcp::pipeline`                                                 | Named callback-pipelining wrapper over a client.                                                      |
| `qb::redis::tcp::cb_consumer`                                              | Pub/sub consumer that delivers each `message` through a callback.                                     |
| `qb::redis::tcp::co_consumer`                                              | Pub/sub consumer with `co_await receive()` returning `std::optional<message>`.                        |
| `qb::redis::tcp::ssl::client` / `pipeline` / `cb_consumer` / `co_consumer` | TLS variants over `transport::stcp` (compiled only when `QB_HAS_SSL`).                                |

The consumers carry the connection and subscription commands (`connect`, `hello`, `subscribe`, `psubscribe`,
`unsubscribe`), but not the data or `publish` commands — publish from a `tcp::client`. The full `tcp::client` does not
subscribe; that surface belongs to the consumers.
<!-- src: qbm/redis/src/qbm/redis/redis.h:1438-1461 (aliases), 955-958 (consumer mixins), 593-614 (client mixins) -->

---

## Concurrency and safety

A client (or consumer) is **not thread-safe**: drive it from a single I/O thread / strand, one concurrent accessor at a
time. The reply queue and outbound pipe are unsynchronized. To use Redis from several cores, give each core its own
client on its own loop.

Lifetime is guarded internally: the connector holds a liveness token so detached work (auto-reconnect, the deadline
watcher, in-flight awaiters) can detect that the client was destroyed and no-op instead of touching freed memory. RESP3
PUSH frames are treated as out-of-band — the plain client discards them so they never desynchronize the reply FIFO;
consumers route them to pub/sub. A throwing reply/user callback is caught and logged rather than crossing the libev
`noexcept` boundary.
<!-- src: qbm/redis/src/qbm/redis/redis.h:583-584 (not thread-safe), 338-351 (liveness token), 743-772 (PUSH + catch) -->

---

## Generic command escape hatch

For commands without a dedicated wrapper, call `command<T>(name, args...)` with the decode type `T`:

```cpp
auto name = co_await redis.command<qb::json>("JSON.GET", "user:1", "$.name");
auto keys = co_await redis.command<std::vector<std::string>>(
    "COMMAND", "GETKEYS", "SET", "mykey", "value");
```

<!-- src: qbm/redis/src/qbm/redis/redis.h:826-838 -->

---

## Documentation map

The technical index is **[readme/README.md](./readme/README.md)**. Five core guides form the learning path; everything
after them is a reference catalogue you look things up in.

- [readme/actors.md](./readme/actors.md) — **start here.** Owning a client inside a `qb::Actor`: `onInit()`,
  `Actor::spawn`, cancellation, shutdown.
- [readme/connection.md](./readme/connection.md) — connect, URIs, `hello(3)` for RESP3, `reset`, auto-reconnect.
- [readme/commands_overview.md](./readme/commands_overview.md) — coroutine vs callback, `Reply<T>` accessors, error
  handling.
- [readme/error_handling.md](./readme/error_handling.md) — `ok()`, `error()`, and when exceptions are used.
- [readme/pipeline_and_await.md](./readme/pipeline_and_await.md) — callback batching, `pending_reply_count()`, `await()`
  drain semantics, `tcp::pipeline`.

Per-command-group
references: [string](./readme/string_commands.md) · [key](./readme/key_commands.md) · [list](./readme/list_commands.md) · [hash](./readme/hash_commands.md) · [set](./readme/set_commands.md) · [sorted set](./readme/sorted_set_commands.md) · [bitmap](./readme/bitmap_commands.md) · [hyperloglog](./readme/hyperloglog_commands.md) · [geo](./readme/geo_commands.md) · [stream](./readme/stream_commands.md) · [publish](./readme/publish_commands.md) · [subscription](./readme/subscription_commands.md) · [transaction](./readme/transaction_commands.md) · [scripting](./readme/scripting_commands.md) · [function](./readme/function_commands.md) · [server](./readme/server_commands.md) · [acl](./readme/acl_commands.md) · [cluster](./readme/cluster_commands.md) · [module](./readme/module_commands.md).

Runnable usage lives in `qbm/redis/tests/` — every test runs in both RESP2 and RESP3.

---

## For AI assistants

This repository publishes machine-readable documentation following the
[llms.txt](https://llmstxt.org/) convention, so a coding agent can read qbm-redis without
guessing:

- **[`llms.txt`](./llms.txt)** — the index: a one-paragraph summary, the six rules that decide whether generated qbm-redis code is correct, and a link
  list of every document in this repository.
- **[`llms-full.txt`](./llms-full.txt)** — ~22k tokens: `llm/qbm-redis.llm.md` (the mental model, invariants and gotchas) and `llm/qbm-redis.llm.api.md` (a deterministic reference for the full command surface, every signature verified against the headers under `src/qbm/redis/`), concatenated into one fetch.

Both files are generated by `scripts/gen-llms-txt.py` from `llm/` and checked in CI
(`scripts/doc-lint.sh` section 1d), so they cannot drift from the documentation they index.

**Use it over MCP, with nothing to host and nothing to install.**
[GitMCP](https://gitmcp.io) exposes any public GitHub repository as an MCP endpoint and reads
`llms.txt` first (its documented order is `llms.txt`, then an AI-optimised documentation
build, then `README.md`):

```json
{ "mcpServers": { "qbm-redis": { "url": "https://gitmcp.io/isndev/qbm-redis" } } }
```

Claude Desktop and other clients without native remote-MCP support wrap the same URL:
`"command": "npx", "args": ["mcp-remote", "https://gitmcp.io/isndev/qbm-redis"]`.

**Cursor `@Docs`** — add
`https://raw.githubusercontent.com/isndev/qbm-redis/main/llms-full.txt`.

## License

Apache License 2.0. Part of the [qb Actor Framework](https://github.com/isndev/qb).
