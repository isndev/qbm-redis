# Changelog

All notable changes to the qbm-redis module are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); the module tracks the qb framework's
[Semantic Versioning](https://semver.org/). Framework-wide policy is in the qb
[VERSIONING](https://github.com/isndev/qb/blob/main/VERSIONING.md) document.

## [Unreleased]

### Added

- **A coroutine parked on Redis says so in qb's `CoroutineScheduler::dump()` (Huly QB-71).** `redis_awaiter` (every
  command) and `connect_awaiter` begin their `await_suspend` with `qb::io::async::track_suspension(h, ...)`: with
  suspension tracking on, the dump shows the coroutine waiting on `"redis"` or `"redis connect"`, for how long. An
  awaiter without the call would leave the coroutine with the record of its previous wait, ageing. Off -- the default
  -- the call is one predictable branch. Pinned by `THE_DUMP_SAYS_A_COROUTINE_WAITS_ON_A_CONNECT_THEN_A_COMMAND` in
  `tests/integration/connection/connection-commands.cpp`; qb's `scripts/check-awaiter-tracking.py`, run over this
  module from the superproject, refuses an awaiter without it.

### Fixed

- **A throwing consumer disconnect callback no longer suppresses auto-reconnect (Huly QB-663).** The connector contains
  standard and non-standard exceptions from the disconnect notification, logs them, and then checks the current retry
  policy. A callback may still disable that policy before the decision; pending replies fail once and no old subscription
  is replayed.
- **RESP argument counts now match the emitted bulks (Huly QB-634).** A `std::vector<char>` is one binary bulk string even
  when empty or longer than one byte. Containers of optional values and nested pairs/tuples count each present value,
  so the announced array length remains correct without changing the omitted-optional wire contract.
- **A throwing success callback is invoked once (Huly QB-635).** Only decoding exceptions become failed `Reply` values;
  callback exceptions reach the existing dispatch boundary after the reply has left the FIFO. A later pipelined reply
  still reaches its own callback.
- **Floating command arguments retain their significant digits (Huly QB-636).** Locale-independent `std::to_chars`
  emits a round-trip representation for `double`, including small `INCRBYFLOAT` and `HINCRBYFLOAT` increments that
  previously rounded to `0.000000` before reaching Redis. Numeric `json_value` and `qb::json` arguments to raw commands
  use the same floating path; integer argument formatting is unchanged.
- **Server-side `extract_*` helpers preserve their result across C++20/C++23 library and consumer builds (Huly QB-286).**
  A C++20 Redis archive returned qb's variant-backed `expected`, while a C++23 consumer interpreted the same
  symbol and bytes as `std::expected` (and vice versa). The compiled functions now exchange values and errors
  through stable output references; caller-side templates construct the mode's `qb::expected`. The public alias
  still selects `std::expected` when available. Ordinary calls keep their source spelling, but code taking a
  helper's address must name its template specialization (for example, `&extract_integer<>`); rebuild consumers.
- **The inline RESP parser keeps C++20 and C++23 result bodies separate (Huly QB-286).** Its public
  `parse()` methods, `parse_all()`, `has_complete_value()` and internal result helpers are now templates keyed
  by the caller's `ParseResult<Value>` type. Mixed-language translation units no longer fold a C++20 body into a
  C++23 call (or the reverse). Direct calls retain their spelling; code taking a function or member address needs
  an explicit specialization such as `&RespParser::parse<>`.
- **`RESET` on a Pub/Sub consumer resets the predicted subscriptions at its reply (Huly QB-641).** A later
  unsubscribe-all now expects the server's one empty-state confirmation rather than the old channel/pattern count;
  it cannot consume the next subscription's reply. While RESET is pending, Pub/Sub commands fail locally instead
  of predicting from an uncertain state. RESP2 and RESP3 integration cases cover both kinds of subscription.
- **`CLIENT REPLY OFF/SKIP` no longer orphan a typed reply handler (Huly QB-650).** Redis acknowledges neither mode,
  so the callback/coroutine API now fails locally before sending either command. `ON` still awaits the server's reply;
  a later command keeps its own FIFO slot. A one-way command API, if added, must distinguish local enqueue from a
  server acknowledgement.
- **An exact-capacity RESP reply is consumed once (Huly QB-623).** The input ring now reserves its sentinel
  on an append equal to its storage capacity and wraps the write cursor when an append ends at the physical boundary.
  Previously a 16 KiB reply could be decoded repeatedly without progress; the same stale-data state could follow a
  partial read near the end of a smaller ring. Unit controls cover exact/neighbor sizes, fragmented input and a
  complete parser drain.
- **A reconnection no longer carries anything of the connection that dropped (Huly QB-202).** The connector failed
  every pending handler on a disconnect but left the bytes of the commands not yet flushed in its output buffer: they
  went out first on the next connection, and their replies were handed to whichever commands were then first in line
  -- the reply FIFO off by one for the life of the connection. The buffers are now emptied where the commands are
  failed, before the handlers run, so a command a failing callback re-issues still leaves, in order, with the next
  connection.
- **`disconnect()` completes the teardown before it returns (Huly QB-202).** It used to defer it to the io watcher's
  next dispatch; a `connect()` completing first (libev runs pending watchers last-in, first-out) restarted the watcher,
  which cancelled the teardown: the commands in flight were never failed and received the new connection's replies.
  It now runs on the spot, through qb-io's new `disconnect_now()` -- without a loop pass, so a `disconnect()` called
  from a coroutine resumes nothing under it.
- **Destroying a client right after a command no longer reads freed memory when a command timeout is set (Huly
  QB-202).** The deadline watcher a command arms is a lazy coroutine: it first runs at the scheduler's next drain, and
  it read the client there before checking that the client was still alive. It now checks first -- and so does the
  auto-reconnect task, which `disconnect()` now spawns from inside the call. Both found under AddressSanitizer.
- **`co_consumer::receive()` ends when the consumer disconnects, and serves the next connection (Huly QB-252).**
  The consumer's `event::disconnected` handler was private and befriended only the base that routes the event, not
  the detector the route goes through, so it never ran: `while (auto msg = co_await consumer.receive())` stayed
  parked after `disconnect()` and after a peer drop, ending only when the consumer was destroyed -- the README's
  shutdown pattern rests on it. And a queue, once closed, stayed closed. Now a disconnect closes the queue:
  `receive()` yields what was already received, then `std::nullopt` while the consumer stays disconnected; after a
  reconnect it serves the new connection, starting with what was received and not yet read (subscriptions are not
  replayed).
- **A command whose arguments cannot be serialized leaves nothing behind (Huly QB-255).** `command()` queued the reply
  handler, then serialized the command, and an argument over 512 MiB (`REDIS_MAX_STRING_SIZE`) throws `SecurityError`
  halfway through the frame: the throw left an orphan handler at the tail of the reply FIFO and a partial RESP frame in
  the outbound pipe, so the next command went out behind the truncated one and the replies were handed to the wrong
  handlers -- and the exception reached the caller, the module's only one. A command is now serialized first, the pipe
  rolled back if that fails, and only then is its handler queued; the callback receives a failed `Reply`
  (`"String too large"`) before `command()` returns. The consumer's command and (un)subscribe paths do the same, and a
  subscription prediction is advanced only for a command that will reach the server.
- **`await()` from a coroutine body no longer aborts a debug build (Huly QB-253).** Documented as safe -- on a second
  client, say -- its nested loop pass re-entered the coroutine scheduler. Fixed in qb-io, where a nested pass now
  leaves the coroutines to the enclosing drain; a test pins it here. `readme/pipeline_and_await.md` now says what a
  coroutine body gives up: the `set_command_timeout()` watcher is a coroutine too, so there `await()` returns on the
  replies or a disconnect, never on the deadline.

### Documentation

- **`ROADMAP.md` removed (Huly QB-137).** It described a hiredis-based parser (`redisReaderGetReply`) and a
  `*_async` API this module never shipped, listed the shipped pipelining as still to do, and `llms.txt`
  linked it, so agents were served it as current. The CHANGELOG and the published milestones are where
  what is planned and what shipped live.
- **`ReplyErrorType` is documented as what it is (Huly QB-142).** `readme/error_handling.md` said the parser
  classifies error frames into `ERR` / `MOVED` / `ASK`; nothing does yet -- every error, a Cluster redirect
  included, is `ok() == false` with the server's text in `reply.error()`, and the page now says to read
  its prefix.
- **Keyspace notifications (Huly QB-140)** -- a section of `readme/subscription_commands.md`: switching them
  on with `config_set`, subscribing with a pattern, and the three things to know (fire-and-forget,
  `CONFIG SET` refused on managed offerings, the subscription lost on a reconnect).
- **Six `readme/` citations re-derived (Huly QB-254).** Four ranges started one line early (`connection.md`,
  `server_commands.md`, `sorted_set_commands.md`, `transaction_commands.md`), and two cited `qb/json.h:283` -- the
  include spelling, which names no file in the tree -- now `qb/src/qb/json.h:283`; found by the strengthened
  `scripts/cite-check.py`, which also resolves another project's file against the sibling checkout in the
  superproject.

## [3.2.1] - 2026-09-24

Lockstep release with the qb 3.2.1 train; no change in this repository (the version says compatible, this section says unchanged).

## [3.2.0] - 2026-09-21

### Fixed

- **`Reply<T>::value_or` accepts a literal default.** The function was `auto`-returning with
  `return` statements that deduced different types for a literal (`const char *` against
  `std::string`, `int` against `long long`), so the idiom every doc taught -- `r.value_or("")`,
  `(co_await redis.zrevrank(k, m)).value_or(-1)` -- failed to instantiate and only a typed
  default (`value_or(std::string{})`, `value_or(-1LL)`) compiled; the unit test had only ever
  passed typed ones. The return type is the value's type on every path now (`T::value_type` for
  an optional `T`, `T` otherwise) and `qbm-redis-test-unit-reply-wrapper` pins the literal forms.

- **A RESP3 PUSH frame of a kind the consumer does not know no longer pops the reply FIFO (Huly
  QB-141).** `invalidate` (client-side caching on the tracking connection), `smessage` /
  `ssubscribe` (sharded pub/sub) and any future push kind fell through `RedisConsumer::on`'s
  dispatch to the command-reply path and resolved the handler at the head of the FIFO -- a
  permanent reply/command desynchronisation the moment such a frame arrived. They are dropped
  with a warning now, like every push the plain client already discards; a RESP2 ARRAY reply
  whose first element is not a pub/sub kind (`PING`'s `["pong",""]` in subscriber mode) keeps its
  path, since that IS the reply to the FIFO head. `qbm-redis-test-system-consumer-unknown-push`:
  no daemon -- the test is the server, a loopback listener it `accept()`s and writes raw RESP3
  into, so the frames cross the real socket, parser and dispatcher.

## [3.1.0] - 2026-08-30

Lockstep release with the qb 3.1.0 train. In this repository only one auction-house citation
re-keyed (its header tag was retired upstream).

## [3.0.1] - 2026-08-29

Lockstep patch with the qb 3.0.1 train (cut for qb-examples, Huly QB-4). In this repository only
tooling: a nightly CI run against qb's `develop` of the day, and `scripts/gen-llms-txt.py` strips
HTML comments by scanning rather than by regex; the one hyphenless `Verified against` marker in
`readme/module_commands.md` joins the corpus form.

## [3.0.0] - 2026-08-20

Tracks changes not yet part of a tagged release. Since 2026-08-11 that is **both** branches:
`main` was fast-forwarded to `develop` for the release, so the module version is **3.0.0** on either,
in lockstep with the qb framework; see the qb CHANGELOG for what makes that release major.

### Fixed

- **The reconnect suite's server stall was an iteration count, so its duration scaled with the
  host — and overrunning it was silent.** `reconnect-resilience.cpp` occupied the single-threaded
  server with a fixed 2e8-iteration Lua `EVAL` on a comment claiming *"measured ~0.6s for 2e8 on
  this box… it can never wedge the server indefinitely"*. The 0.6s was right and the conclusion was
  not: an iteration count is a WORK budget. Measured on one macOS box (Redis 8.10.0), same count:
  **601 ms** idle, **52 820 ms** under 20 CPU spinners — 10.5x past the server's 5000 ms
  `busy-reply-threshold`, past which the server answers `-BUSY` to every other client, the next
  case's connect times out and `GTEST_SKIP`s, **and ctest still reports the binary `Passed`**.
  The count is now calibrated against the server under test (`calibrated_stall_iterations`): a small
  probe `EVAL` is timed once per process and the real stall sized from the measured rate, making it
  a DURATION on any host — under that same load, a 1977 ms probe chose 4.0e6 iterations and stalled
  1188 ms. Bounding it from inside the script is not an option and the comment now records why:
  Redis freezes a script's view of the clock, so `redis.call('TIME')` returns the value cached at
  script start and only advances once the busy-script watchdog fires — a loop written to stop after
  300 ms of TIME measured **5033 ms**, i.e. exactly the overrun it was meant to prevent.
- **`CMakeLists.txt` had no `cmake_minimum_required()`, and a standalone configure died on an
  unhelpful error.** CMake reported `No cmake_minimum_required command is present` alongside
  `Unknown CMake command "qb_status_message"` — which reads like a missing include rather than
  "wrong entry point". This module is built from the qb-dev superproject, which loads qb's CMake
  helpers first; an *installed* qb ships none of them (`lib/cmake/qb/` carries only `qbConfig`,
  `qbConfigVersion`, `qbTargets` and the `Find` modules), so pointing `CMAKE_PREFIX_PATH` at one
  does not help — the exact mistake the old error invited. There is now a
  `cmake_minimum_required(VERSION 3.24)` and a guard that names the constraint and points at the
  superproject root and the `package` preset.

- **`qbm/redis/redis.h:1631` broke any `-Werror` consumer**: `RedisCoroConsumer::on(disconnected &&e)`
  never used `e`, so a consumer building with `-Wall -Wextra -Werror` failed on
  `unused parameter 'e' [-Wunused-parameter]`. Nothing here saw it because CMake puts `-isystem` on an
  IMPORTED target, which silences every `-W*` inside the module's headers — for the one configuration
  CMake generates automatically. The parameter is now unnamed.
- **6 installed headers were not self-contained** — each compiled only because something else was
  included first: the four SCAN command headers (`hash`, `key`, `set`, `sorted_set`) call
  `QB_LOG_WARN` in their scanner-callback guard without `<qb/io.h>` (the macro then survives as an
  unexpanded call and the diagnostic is a baffling *invalid operands to binary expression*),
  `server_commands.h` takes a `qb::duration` and calls `qb::detail::to_ev_seconds` without
  `<qb/system/time.h>`, and `types.h` uses `qb::unordered_map` without
  `<qb/system/container/unordered_map.h>`.
  Both classes are now gated: the superproject's `package-consume.yml` runs
  `qb/scripts/check-installed-headers.sh` over the `qbm` tree, once plainly and once with `--hostile`
  (`-I` instead of `-isystem`, `-Wall -Wextra -Werror`).

### Changed

- **BREAKING — `client_kill()` now replies `Reply<long long>`, was `Reply<status>`.** `CLIENT KILL`
  answers with an *integer count of connections killed* in every form this wrapper can send: called
  with no arguments it sends `SKIPME yes`, so the filter form — which is the integer-replying one —
  is the only form reachable. Typing it as `status` mis-decoded that reply. The coroutine overload
  changed at `src/qbm/redis/commands/server_commands.h:162`
  (`make_coro_command<status>` → `make_coro_command<long long>`) and the callback overload's SFINAE
  guard moved with it, so **an existing caller does not get a converted value — it gets no matching
  function, a hard compile error.** Migrate by changing the awaited/handled type:
  `auto n = co_await redis.client_kill(addr);` now yields a count, not a status. Found by
  `tests/integration/resilience/`, the first test to call it.

- **Logging call sites use qb's prefixed `QB_LOG_*` macros** (28 sites). qb 3.0.0 renamed
  `LOG_DEBUG` / `LOG_VERB` / `LOG_INFO` / `LOG_WARN` / `LOG_CRIT` to `QB_LOG_*` because the
  unprefixed spellings — three of which are also POSIX `<syslog.h>` names — reached every consumer
  of this module's umbrella header and silently replaced a consumer's own. qb still defines the
  unprefixed names as `#ifndef`-guarded aliases, and that guard is exactly why these call sites had
  to move: a consumer who defines `LOG_INFO` first now keeps their definition, and this module's
  headers would otherwise have started logging through *it*.

- **BREAKING — the public include prefix is now `<qbm/redis/...>`** (was `<redis/...>`). Every consumer
  edits its `#include` lines: `#include <redis/redis.h>` becomes `#include <qbm/redis/redis.h>`. The CMake
  target is unchanged (`qbm::redis`), and so is the installed location `<prefix>/include/qbm/redis/`.
  The old spelling existed only because `qb_register_module` made this module's include root its
  PARENT directory — the superproject's `qbm/`, which does not exist in this repository at all — and
  mirrored it with `<prefix>/include/qbm` on the consumer's include path. That put the maximally
  generic top-level name `redis` in every consumer's include namespace. Now the module's own `src/`
  IS the include root and is copied verbatim to `<prefix>/include`, so `<qbm/redis/...>` is the same
  string in this tree and in an installed prefix, and the two cannot drift.
- **The source tree moved to `src/qbm/redis/`** — one pure `git mv`, 100 % rename detection, zero
  content change, so `git blame` and every line-numbered citation survive intact. `commands/` and `parser/` moved with the root headers; nothing was renamed inside them, so
  `<qbm/redis/commands/string_commands.h>` is the old `<redis/commands/string_commands.h>` with one
  prefix changed.
  `tests/`, `readme/` and `scripts/` live BESIDE `src/`, never inside it, which is what
  makes a stray `#include <tests/fixture.h>` impossible rather than merely unlikely.
  The test suite now includes the shipped spelling instead of resolving `"../redis.h"` by string
  concatenation onto a `-I <mod>/tests` flag.
- **`project(qbm-redis VERSION ...)` is now `3.0.0`**, tracking `QB_FRAMEWORK_VERSION`. It had been
  left at `2.6.0` while the framework moved on. The module is not standalone-configurable (it calls
  `qb_register_module` / `qb_add_test`, which an installed qb does not ship), so its version can only
  ever mean "the qb this was built against" — and the structural breaks queued for 3.0.0 land hardest
  in the modules, where a package still claiming `2.6.0` would be actively misleading.
- **`scripts/doc-lint.sh` now validates the *value* of the `Verified-against:` markers**, not just
  their presence. It previously checked only that the marker existed, which is how every page in this
  module sat at `qb 2.6.0` across two version bumps unnoticed. The expected version is read from
  `project(qbm-redis VERSION ...)` — the one authoritative version available when this repo is checked
  out alone, as it is in its own CI — and cross-checked against `QB_FRAMEWORK_VERSION` whenever a qb
  tree is reachable. A version it cannot determine is a hard stop, never a skip.

## [2.6.0] - 2026-08-02

Aligned with the qb 2.6.0 framework release.

### Changed

- Test suite restructured into tiered `unit/` / `system/` / `integration/` / `benchmark/` directories.

### Fixed

- RESP3 map decoding, several reply-type conversions, and `MULTI` / `EXEC` transaction handling corrected.

### Documentation

- Narrative and reference docs overhauled; every page carries code-verified `src:` citations enforced by
  `scripts/doc-lint.sh`.

## [2.0.0]

Aligns qbm-redis with the qb 2.0 framework (C++20 baseline) and hardens the RESP protocol paths.

### Changed

- Time handling migrated to the canonical chrono model: connect and command timeouts, the `RetryPolicy`
  delays (initial / max / on-retry), and `debug_sleep` are `qb::duration`. Redis command arguments keep
  native units by design — `EXPIRE` takes seconds and `PEXPIRE` takes milliseconds, exposed through
  `std::chrono`-unit overloads — and reply TTL values stay integers. The retired `qb::Timestamp` /
  `qb::Duration` types are gone.
- Adapted to the qb C++20 baseline (`QB_CXX_STANDARD=20`, optional 23).

### Fixed

- Use-after-free of the connector in the auto-reconnect connect path.
- `parse<double>` now rejects trailing garbage (requires full consumption).
- Robust reply decoding: `uint64` `SCAN` cursor and non-throwing transforms.
- An over-counting server can no longer cross-resolve a command via a stray confirmation.

### Security

- Contain non-`std` handler exceptions at the `noexcept` `onMessage` boundary.
- Contain a throwing pub/sub message callback so the reply FIFO cannot desynchronize.
- Overflow-safe `ViewBuffer` length bounds (defense in depth).
- Fault on a corrupt RESP terminator and drop the dead destructive parse path.

[Unreleased]: https://github.com/isndev/qbm-redis/compare/v3.2.1...HEAD
[3.2.1]: https://github.com/isndev/qbm-redis/compare/v3.2.0...v3.2.1
[3.2.0]: https://github.com/isndev/qbm-redis/compare/v3.1.0...v3.2.0
[3.1.0]: https://github.com/isndev/qbm-redis/compare/v3.0.1...v3.1.0
[3.0.1]: https://github.com/isndev/qbm-redis/compare/v3.0.0...v3.0.1
[3.0.0]: https://github.com/isndev/qbm-redis/compare/v2.6.0...v3.0.0
[2.6.0]: https://github.com/isndev/qbm-redis/releases/tag/v2.6.0
