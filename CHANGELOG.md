# Changelog

All notable changes to the qbm-redis module are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); the module tracks the qb framework's
[Semantic Versioning](https://semver.org/). Framework-wide policy is in the qb
[VERSIONING](../../qb/VERSIONING.md) document.

## [Unreleased]

Tracks changes on the development branch not yet part of a tagged release.

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

[Unreleased]: https://github.com/isndev/qbm-redis/compare/main...HEAD
