# Changelog

All notable changes to the qbm-redis module are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); the module tracks the qb framework's
[Semantic Versioning](https://semver.org/). Framework-wide policy is in the qb
[VERSIONING](https://github.com/isndev/qb/blob/main/VERSIONING.md) document.

## [Unreleased]

Tracks changes on the development branch not yet part of a tagged release. The module version is
**3.0.0**, in lockstep with the qb framework; see the qb CHANGELOG for what makes that release major.

### Changed

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
