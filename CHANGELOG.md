# Changelog

All notable changes to the qbm-redis module are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); the module tracks the qb framework's
[Semantic Versioning](https://semver.org/). Framework-wide policy is in the qb
[VERSIONING](https://github.com/isndev/qb/blob/main/VERSIONING.md) document.

## [Unreleased]

Tracks changes on the development branch not yet part of a tagged release. The module version is
**3.0.0**, in lockstep with the qb framework; see the qb CHANGELOG for what makes that release major.

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
  `tests/`, `readme/`, `scripts/` and `cmake/` live BESIDE `src/`, never inside it, which is what
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

## [2.6.0] - 2026-06-29

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

[Unreleased]: https://github.com/isndev/qbm-redis/compare/v2.6.0...HEAD
[2.6.0]: https://github.com/isndev/qbm-redis/releases/tag/v2.6.0
