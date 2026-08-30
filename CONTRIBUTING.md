<!-- Verified-against: qbm-redis @ qb 3.1.0 (C++20 default, C++23 supported) -->

# Contributing to qbm-redis

qbm-redis is a module of the qb actor framework. General contribution guidelines — branch and pull-request
flow, code style (`.clang-format` / `.clang-tidy`), the Developer Certificate of Origin sign-off, and the
Code of Conduct — follow the framework's [CONTRIBUTING guide](https://github.com/isndev/qb/blob/main/CONTRIBUTING.md). Security
vulnerabilities must not be filed as public issues; see [SECURITY.md](./SECURITY.md).

This document covers what is specific to building and testing the module.

## Build and test

qbm-redis is built through the qb module loader, not on its own. From a project that embeds qb:

```cmake
add_subdirectory(qb)
qb_load_modules("${CMAKE_CURRENT_SOURCE_DIR}/qbm")
target_link_libraries(your_target PRIVATE qbm::redis)
```

To build and run the module's test suite, configure the framework with tests enabled and build from the
repository root:

```bash
cmake -DCMAKE_BUILD_TYPE=Release -DQB_BUILD_TESTS=ON -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure -R qbm-redis
```

Integration tests need a reachable Redis server; they skip when one is not configured. Add a test for any
new command or behavior, and exercise both the success and the malformed-reply paths when your change
touches RESP decoding.

## Conventions

Use the `qb::redis` namespace and include the module's umbrella header. Express durations with
`qb::duration` (connect and command timeouts, `RetryPolicy` delays, `debug_sleep`). Redis command arguments
keep their native units: `EXPIRE` takes seconds and `PEXPIRE` takes milliseconds, offered through
`std::chrono`-unit overloads — do not force these onto `qb::duration`. Reply TTL values are integers. Never
use the removed `qb::Timestamp` / `qb::Duration` types.
