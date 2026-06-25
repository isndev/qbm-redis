/*
 * qb - C++ Actor Framework
 * Copyright (C) 2011-2026 isndev (cpp.actor). All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use it except in compliance with the License.
 * See the License for the specific terms.
 */

/**
 * @file redis_integration_fixture.h
 * @brief Shared skip-not-fail base for qbm-redis integration (`REQUIRES live`) tests.
 *
 * Integration tests need a live `redis:6379`. This header centralizes the ONE contract
 * the test-suite conventions mandate (docs/tests-audit/_CONVENTIONS.md §4.5):
 *
 *   - the endpoint is overridable via the `REDIS_URI` environment variable;
 *   - if the daemon is unreachable the test is `GTEST_SKIP`-ped (never thrown / hard-failed),
 *     printing the exact sentinel `QBM_INTEGRATION_SKIP_DAEMON_UNREACHABLE` that the CMake
 *     helper wires into CTest's `SKIP_REGULAR_EXPRESSION` (via `REQUIRES live`), so a
 *     daemon-down run reports **Skipped, not Failed** and `ctest -LE live` stays green.
 *
 * Replaces the legacy `ProtocolModesTestBase` behaviour (hardcoded `tcp://localhost:6379`,
 * `throw std::runtime_error` after a 20 s retry).
 */

#ifndef QBM_REDIS_TESTS_SHARED_REDIS_INTEGRATION_FIXTURE_H
#define QBM_REDIS_TESTS_SHARED_REDIS_INTEGRATION_FIXTURE_H

#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>
#include <gtest/gtest.h>
#include <qb/io/async.h>
#include "../../redis.h"

namespace qbm::redis::test {

/// Exact phrase CTest's SKIP_REGULAR_EXPRESSION matches (set by `REQUIRES live`) to mark a
/// daemon-down binary as Skipped. Keep in sync with qb/cmake/qbFunctions.cmake.
inline constexpr const char *kDaemonUnreachableSentinel =
    "QBM_INTEGRATION_SKIP_DAEMON_UNREACHABLE";

/// Redis endpoint, overridable via `REDIS_URI` (local daemon / container / CI), default
/// `tcp://localhost:6379`.
[[nodiscard]] inline std::string
redis_test_uri() {
    if (const char *env = std::getenv("REDIS_URI"); env != nullptr && *env != '\0')
        return std::string(env);
    return "tcp://localhost:6379";
}

/**
 * @brief Attempt to connect (and flushall) within a bounded window. Returns true on success.
 *
 * Tolerates a transiently-busy single-threaded server (a prior test's EVAL busy-loop can keep
 * the server frozen): each attempt uses a 1 s command deadline so it fails fast instead of
 * hanging, and retries until @p budget elapses. Never throws — the caller decides what a
 * false return means (integration fixtures GTEST_SKIP; see @ref RedisIntegrationTest).
 */
[[nodiscard]] inline bool
redis_try_connect(qb::redis::tcp::client &redis,
                  std::chrono::seconds budget = std::chrono::seconds(20)) {
    qb::io::async::init();
    const auto give_up = std::chrono::steady_clock::now() + budget;
    for (;;) {
        redis.set_command_timeout(std::chrono::seconds(1));
        const bool connected = qb::io::async::run_sync(redis.connect());
        const bool flushed   = connected && qb::io::async::run_sync(redis.flushall()).ok();
        redis.set_command_timeout(qb::duration::zero());
        if (flushed)
            return true;
        if (std::chrono::steady_clock::now() >= give_up)
            return false;
        redis.disconnect();
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
}

/**
 * @brief Base fixture for non-parameterized redis integration tests.
 *
 * Derive and use `redis` in the test body. `SetUp()` connects-or-skips. Parameterized
 * protocol-mode suites keep their own `TestWithParam` base but MUST call `redis_try_connect`
 * + `GTEST_SKIP() << kDaemonUnreachableSentinel` in their own `SetUp` (the skip macro must
 * execute in `SetUp`'s scope, not a helper's).
 */
class RedisIntegrationTest : public ::testing::Test {
protected:
    qb::redis::tcp::client redis{redis_test_uri()};

    void
    SetUp() override {
        if (!redis_try_connect(redis))
            GTEST_SKIP() << kDaemonUnreachableSentinel << " (redis at " << redis_test_uri()
                         << " not reachable)";
    }

    void
    TearDown() override {
        // Best-effort cleanup; ignore the result so teardown never reds an otherwise-green
        // test (a skipped/disconnected client just fails the flushall fast and we drop it).
        (void) qb::io::async::run_sync(redis.flushall());
    }
};

} // namespace qbm::redis::test

#endif // QBM_REDIS_TESTS_SHARED_REDIS_INTEGRATION_FIXTURE_H
