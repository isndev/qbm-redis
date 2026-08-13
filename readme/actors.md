# A Redis actor

> **Audience:** Adopter · **Status:** stable · **Verified-against:** qbm-redis @ qb 3.0.0 (C++20 default, C++23
> supported)

How a `qb::redis::tcp::client` and a `tcp::co_consumer` live inside a `qb::Actor`: who turns the loop, how a handler
awaits a command without stopping the core, why the pub/sub `receive()` awaiter behaves differently from every other
one in this module, and what `kill()` does — and does not do — to a coroutine parked on a reply.

**Prerequisites:** [connection.md](./connection.md) (open a connection
first), [commands_overview.md](./commands_overview.md) — **See also:
** [pipeline_and_await.md](./pipeline_and_await.md), [subscription_commands.md](./subscription_commands.md),
[error_handling.md](./error_handling.md), and, in the framework,
[Writing actors](https://github.com/isndev/qb/blob/main/readme/4_qb_core/actor.md)
· [Asynchronous work inside an actor](https://github.com/isndev/qb/blob/main/readme/5_core_io_integration/async_in_actors.md)
· [C++20 coroutines](https://github.com/isndev/qb/blob/main/readme/3_qb_io/coroutines.md)

---

## Summary

A Redis client is an ordinary qb-io object. It binds to the event loop of the thread that constructs it, and inside a
`qb::Main` that thread is a `VirtualCore`. An actor that holds a client therefore needs **no pump and no drain**: the
core runs its loop once per pass, before it dispatches your handlers, and that pass is what carries RESP bytes both
ways.

What the actor owes back is one rule: **never stop returning to the loop.** `co_await` returns. `run_sync`, `run_once`
and `await()` do not.

```cpp
#include <qb/actor.h>
#include <qb/main.h>
#include <qbm/redis/redis.h>
#include <memory>
#include <string>

struct SessionLookup : qb::Event {
    std::string key;
    explicit SessionLookup(std::string k) : key(std::move(k)) {}
};
struct SessionValue : qb::Event {
    std::string value;    // empty when the key is missing or the command failed
    explicit SessionValue(std::string v) : value(std::move(v)) {}
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
        std::string       key    = ev.key;
        const qb::ActorId sender = ev.getSource();

        spawn([redis, key, sender](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            auto reply = co_await redis->get(key);           // Reply<std::optional<std::string>>
            ctx.push_to<SessionValue>(sender,
                                      reply.ok() && reply.result() ? *reply.result() : std::string{});
        });
    }

    void on(qb::KillEvent const &) {
        if (_redis)
            _redis->disconnect();   // fails every pending reply, so nothing is left parked
        kill();
    }
};
```

Four decisions in that class carry the page: the client is reached through a `shared_ptr` the coroutine copies, the
handshake happens in `onInit()`, the command runs under `spawn` rather than in the handler, and the kill handler
disconnects **before** it kills.

---

## Concepts

### Who turns the loop

| Where the client lives | What turns the loop | What you must call |
|:---|:---|:---|
| A member of an actor, or created in `onInit()` | the owning `VirtualCore`'s loop pass | nothing |
| A local in `main()`, before `qb::Main::start()` | nothing yet — no loop is running | `qb::io::async::run_sync(...)` |
| A local in a test fixture or a CLI | nothing yet | `run_sync(...)`, or callbacks + `await()` |

The bottom two rows are why `run_sync` and `await()` appear throughout these pages, and they are correct there: the
thread they stop is the caller's. The top row is what this page is about, and on it both calls are defects.

Construct the client on the actor's own thread. An actor's constructor already runs there — the engine builds it on the
worker core, not where you called `addActor` — so a member, a `unique_ptr` filled in the constructor and one created in
`onInit()` are equally correct. What is not correct is one client shared between actors: the reply queue and the
outbound pipe are unsynchronised, and sharing corrupts the FIFO ordering that RESP depends on. Give each actor its own
connection, or put Redis behind a single actor and send it events.

### `onInit()` is where you connect

`onInit()` is a coroutine, so the handshake reads as a straight line. While it is suspended the actor is **Activating**:
the engine holds its inbound business events and replays them once init succeeds, so nothing is served against a
half-open connection. `co_return false` fails the init and the actor is destroyed without handling a message.

Give `connect` a deadline — `connect(qb::duration)` — rather than relying on the default. The connect awaiter is one of
the few things in this module that cannot be resolved by anything else if the peer simply never answers.

### Awaiting a command from a handler

An event handler must return. Work that suspends goes to `Actor::spawn`, which launches an isolated coroutine on this
core's scheduler and returns immediately. The framework
owns [the rules that come with it](https://github.com/isndev/qb/blob/main/readme/5_core_io_integration/async_in_actors.md#coroutines-from-a-handler-spawn-and-spawn_detached);
the module-specific consequence is this:

> **A spawned coroutine may not touch the actor's client after a `co_await`.** The actor — and any client that is its
> member — may have been destroyed while the coroutine was parked. Capturing `this`, `&_redis`, or a member reference
> is undefined behaviour from the first suspension onward.

Holding the client behind a `std::shared_ptr` and copying the pointer into the lambda solves it outright: the frame
owns a reference, so the client outlives the actor for exactly as long as something is parked on it. Everything else the
coroutine needs — the key, the requester's `ActorId` — is copied the same way, and after the `co_await` it speaks only
through `ctx`.

---

## What cancellation does to a parked command, and what does not

`Actor::kill()` cancels the actor's coroutine scope. Whether that reaches a parked coroutine is a property of the
awaiter, and
[C++20 coroutines](https://github.com/isndev/qb/blob/main/readme/3_qb_io/coroutines.md#every-awaitable-and-what-cancellation-does-to-it)
owns the full inventory. This module contributes two entries, and they behave differently — which is the single most
useful thing on this page.

### Every command: `redis_awaiter` is not cancellation-aware

`co_await redis.get(...)`, `set`, `publish`, `eval`, `xadd` — every one of the 200-plus command methods returns a
`redis_awaiter`. It registers no `on_cancel` hook and consults no token, so `cancel()` — and therefore `kill()` —
neither wakes nor unwinds a coroutine parked on one.
<!-- src: qbm/redis/src/qbm/redis/redis.h:711-726 (await_ready false; await_suspend stores the handle and launches the operation — no token, no hook) -->

It is a callback bridge and nothing more: `await_ready()` returns `false`, the handle is stored, the command is issued
with a completion lambda, and resumption goes through `coro_scheduler().schedule_resume`. What it *does* carry is a
`shared_ptr<bool> valid_` cleared in its destructor, so a reply that lands after the awaiter is gone is a silent no-op
rather than a use-after-free.
<!-- src: qbm/redis/src/qbm/redis/redis.h:706-709 (destructor clears valid_), :720-722 (the completion checks it first) -->

So a parked command ends in exactly four ways:

| What happens | What the coroutine sees |
|:---|:---|
| The server replies | `Reply<T>` — the normal path |
| The link drops (`disconnect()`, a dead peer, or the command deadline tripping) | `Reply<T>` with `ok() == false` and `error() == "disconnected"`, or `"command timed out"` if a deadline tripped first — `on(disconnected)` fails **every** queued handler in order |
| Someone destroys the coroutine frame | nothing — the frame is gone and the late reply is dropped |
| Nothing else | it stays parked |

The second row is the one to build on. It is not incidental: `on(disconnected)` swaps the reply queue out *before*
draining it, precisely so a failing handler that re-issues a command does not get failed by the same drain loop.
<!-- src: qbm/redis/src/qbm/redis/redis.h:954-974 (swap before drain, then fail each pending handler) -->

### Pub/sub: `receive()` ends on close

The coroutine consumer is the exception worth knowing. `receive()` does **not** return a `redis_awaiter`; it awaits a
`qb::io::async::channel<message>` that the consumer fills from its RESP push frames.
<!-- src: qbm/redis/src/qbm/redis/redis.h:1673-1676 (receive() → co_await _msg_channel.recv()) -->

A channel `recv()` is still not cancellation-aware — `cancel()` does nothing to it — but it *is* woken by
`close()`, and the consumer closes the channel in two places: when the connection drops, and in its own destructor.
<!-- src: qbm/redis/src/qbm/redis/redis.h:1633-1637 (on disconnected → _msg_channel.close()), :1678-1680 (destructor closes it) -->

That gives the subscribe loop a clean termination condition with no cancellation machinery at all, and it is exactly
what the shipped examples use: the loop ends when `receive()` yields `std::nullopt`, and the actor makes that happen by
disconnecting the consumer.

```cpp
// A pub/sub actor. The consume loop is scoped to the actor, and ended by disconnect().
struct Broadcast : qb::Event {
    std::string payload;
    explicit Broadcast(std::string p) : payload(std::move(p)) {}
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
        qb::io::cout() << "event: " << ev.payload << '\n';
    }

    void on(qb::KillEvent const &) {
        if (_sub)
            _sub->disconnect();   // closes the channel → receive() yields nullopt → the loop ends
        kill();
    }
};
```

<!-- src: examples/all/auction_house/src/actors/websocket_handler.cpp:55-67 (the same consume_loop shape) -->

The `disconnect()` is load-bearing. Without it, `kill()` leaves the loop parked on `receive()` forever — the awaiter is
listening to nothing — and the actor's destructor runs anyway, because nothing waits for a coroutine.

### Making a command interruptible

Because the awaiter cannot be woken, an interruptible command is one whose *frame* someone else can destroy. That is
what `ScopedCoroContext::cancellable` does — but it takes a `qb::io::async::task<T>`, not an awaiter, so the command
has to be inside a coroutine first. Use a named free function; not an immediately-invoked lambda, whose closure is
already destroyed by the time the body runs.

```cpp
qb::io::async::task<qb::redis::Reply<std::optional<std::string>>>
run_get(std::shared_ptr<qb::redis::tcp::client> redis, std::string key) {
    co_return co_await redis->get(std::move(key));
}

// ... inside the handler:
spawn([redis, key, sender](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
    try {
        auto reply = co_await ctx.cancellable(run_get(redis, key));
        ctx.push_to<SessionValue>(sender,
                                  reply.ok() && reply.result() ? *reply.result() : std::string{});
    } catch (qb::io::async::cancelled_error const &) {
        // The actor was killed while the command was in flight.
    }
});
```

On `kill()` the hook fires, destroys the inner frame — which destroys the `redis_awaiter` and retracts its `valid_`
flag — and resumes with `cancelled_error`. `spawn`'s wrapper swallows a `cancelled_error` that escapes a scoped
coroutine, so the `try`/`catch` is needed only when you have cleanup of your own.

**But prefer the deadline.** Redis is a FIFO pipelined protocol: abandoning one reply does not let the next command
overtake it, so cancelling buys you nothing on the wire. `set_command_timeout(qb::duration)` is the module's own answer,
and it is the better one for a stuck peer — when no reply arrives inside the window for a non-blocking in-flight
command, the client **drops the connection** and fails every pending reply with `"command timed out"`, which resumes
every parked coroutine at once. Dropping the whole connection is not a blunt instrument here but the only correct one:
a FIFO stream cannot fail one mid-queue command without desynchronising every later reply.
<!-- src: qbm/redis/src/qbm/redis/redis.h:898-904 (on_command_deadline → disconnect, deliberately not resolving awaiters here) -->

Blocking commands (`BLPOP`, `BRPOP`, `WAIT`, `XREAD`, …) suspend the deadline while in flight, so their own server-side
timeout governs instead. Give those a real timeout argument; `command_timeout` will not bound them.

---

## Callbacks inside an actor

The callback overloads are the simpler half inside an actor: they enqueue and return, the core's next loop pass carries
the bytes, and the callback runs from that pass. Nothing needs draining.

```cpp
void on(TouchSession const &ev) {
    _redis->expire([](qb::redis::Reply<bool> &&r) {
                       if (!r.ok()) { /* log */ }
                   },
                   ev.key, std::chrono::seconds(1800));
}
```

**Do not call `await()` from a handler.** `await()` is `while (!_replies.empty()) listener::current.run(EVRUN_NOWAIT);`
— a spin, not a kernel block, but a spin *on the `VirtualCore` thread* that does not return until every pending reply
has landed. For the duration this core dispatches no actor events, ticks no `ICallback` and reaps nothing, and there is
no diagnostic: the guard those calls carry only fires inside the coroutine scheduler's ready-drain, which an actor
handler is not running under.
<!-- src: qbm/redis/src/qbm/redis/redis.h:1049-1054 (await: spin the loop until the reply queue empties) -->

Inside an actor, that call has no job to do anyway — the loop pass already drains the queue.
See [pipeline_and_await.md](./pipeline_and_await.md) for what `await()` is for, which is a thread you own.

---

## Auto-reconnect is a coroutine your actor does not own

`enable_auto_reconnect(policy)` makes `on(disconnected)` spawn a retry coroutine directly on the current thread's
scheduler. Two consequences an actor should know:

- **It is not in the actor's cancellation scope.** It was not started by `Actor::spawn`, so `kill()` does not signal it
  and `has_active_coroutines()` does not count it.
- **It stops when the client object is destroyed, not when the actor is killed.** The task captures the connector's
  `shared_ptr<bool> alive`, which `~connector` clears, and checks it after every suspension.

<!-- src: qbm/redis/src/qbm/redis/redis.h:363-366 (on disconnected spawns the retry task), :369-385 (_reconnect_task checks alive after the await) -->

The retry runs on the same loop as everything else — there is no extra thread — and it sleeps between attempts rather
than spinning. Combined with the fail-queued behaviour above, the shape you get is: a drop fails every parked
`co_await` immediately with `ok() == false`, and the connection comes back underneath you. Your handlers see failures
for the gap and then start succeeding again; nothing hangs.

---

## Bridging to synchronous code

`qb::io::async::run_sync(awaitable)` drives one awaitable to completion by pumping the current thread's loop. It is
correct wherever **the thread it blocks is yours**: a `main()` before `qb::Main::start()`, a test fixture, a warm-up
script. Every `run_sync` in the rest of these pages is one of those.

The framework's own best statement of the case is a comment in a shipped example:

> *Pre-engine setup: there is no actor loop yet, so we drive a coroutine to completion synchronously with
> `qb::io::async::run_sync`.*
> — `examples/all/auction_house/src/main.cpp:34-35`

```cpp
// main(), before the engine starts: warm a cache once.
int main() {
    qb::io::async::init();
    qb::redis::tcp::client redis{qb::io::uri{"tcp://127.0.0.1:6379"}};
    if (!qb::io::async::run_sync(redis.connect()))
        return 1;
    qb::io::async::run_sync(redis.set("build", "3.0.0"));
    redis.disconnect();

    qb::Main engine;
    engine.addActor<SessionCache>(0);
    engine.start();
    engine.join();
    return 0;
}
```

Inside an actor the same call is a defect, and a quiet one. The thread it blocks is the `VirtualCore`: until the
awaitable resolves, this core dispatches no events, so every other actor on it stops. What makes it hard to notice is
that the pump *does* keep turning the loop — sockets stay serviced, timers fire, other coroutines resume — so Redis
still answers and only actor latency moves.
[Asynchronous work inside an actor](https://github.com/isndev/qb/blob/main/readme/5_core_io_integration/async_in_actors.md#run_sync--the-stack-stays-and-step-6-never-finishes)
owns that mechanism and the two annotated call chains that make it concrete.

---

## Shutting down

Order matters, because the coroutines outlive the handler that started them:

1. **`disconnect()` first**, on every client and consumer the actor owns. It fails every pending reply and closes the
   pub/sub channel, so each parked `co_await` resumes — with a failed `Reply<T>`, or with `std::nullopt` from
   `receive()` — on the next pass.
2. **`kill()` second.** It cancels the actor's scope, which reaches anything parked on a framework awaiter or inside a
   `ctx.cancellable(...)` wrapper.
3. **The destructor runs later**, at the core's reap. `has_active_coroutines()` reports what is still outstanding if
   you want to look first.

Skipping step 1 is the common mistake, and for a subscribe loop it is unbounded: `receive()` is woken by a message or a
close and by nothing else, so a killed actor whose consumer is still connected leaves that coroutine parked until the
next publish — or forever.

---

## Pitfalls

- **Calling `run_sync`, `run_once` or `await()` from a handler.** All three stop the `VirtualCore` and nothing reports
  it. `co_await` inside `spawn` is the form that returns.
- **Capturing `this` or `&_redis` into a `spawn` body.** The actor may be destroyed while the coroutine is parked. Copy
  a `shared_ptr` and the plain values you need, before the first `co_await`.
- **Expecting `kill()` to interrupt a command.** It does not: the awaiter registers no hook. Wrap it in
  `ctx.cancellable(...)`, set a `command_timeout`, or `disconnect()`.
- **Killing a subscriber without disconnecting it.** `receive()` waits on a channel nothing will close, so the loop
  parks indefinitely.
- **Passing an awaiter to `ctx.cancellable` / `with_deadline`.** Both take a `qb::io::async::task<T>`. Wrap the
  `co_await` in a named coroutine first — never an immediately-invoked lambda, whose closure dies before the body runs.
- **Sharing one client between actors.** The reply queue and outbound pipe are unsynchronised; sharing corrupts the
  FIFO order. One client per actor, or one Redis actor for everybody.
- **Relying on `command_timeout` to bound `BLPOP` and friends.** Blocking commands suspend the deadline. Pass them a
  server-side timeout.

---

## See also

- [connection.md](./connection.md) — URIs, the connect awaiter, TLS, `RetryPolicy`, fail-queued, the command deadline
- [commands_overview.md](./commands_overview.md) — how a method maps to RESP, `Reply<T>`, coroutine versus callback
- [pipeline_and_await.md](./pipeline_and_await.md) — the reply queue, pipelining, and what `await()` is for
- [subscription_commands.md](./subscription_commands.md) — the callback and coroutine consumers, and driving them
- [error_handling.md](./error_handling.md) — `Reply<T>`, RESP errors, and the containment boundaries
- [Writing actors](https://github.com/isndev/qb/blob/main/readme/4_qb_core/actor.md) — lifecycle, `onInit`, `kill()`,
  the reap
- [Asynchronous work inside an actor](https://github.com/isndev/qb/blob/main/readme/5_core_io_integration/async_in_actors.md)
  — `spawn`, `defer`, `callback`, and the two call chains
- [C++20 coroutines](https://github.com/isndev/qb/blob/main/readme/3_qb_io/coroutines.md) — every awaitable and what
  cancellation does to it
