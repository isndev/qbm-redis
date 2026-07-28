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

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include "../../redis.h"

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace qb::redis::test {

/// Exact phrase CTest's SKIP_REGULAR_EXPRESSION matches (set by `REQUIRES live`) to mark a
/// daemon-down binary as Skipped. Keep in sync with qb/cmake/qbFunctions.cmake.
inline constexpr const char *kDaemonUnreachableSentinel = "QBM_INTEGRATION_SKIP_DAEMON_UNREACHABLE";

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
 *
 * @note The budget is spent AT MOST ONCE per process. If a full budget elapses without a single
 *       successful connect the daemon is taken to be absent and every later call returns false
 *       immediately — otherwise the cost is `test cases x budget`, which overruns CTest's
 *       integration-tier timeout instead of skipping. See the implementation comment.
 */
[[nodiscard]] inline bool
redis_try_connect(qb::redis::tcp::client &redis, std::chrono::seconds budget = std::chrono::seconds(20)) {
    // A daemon that is simply ABSENT is absent for the whole process, and this helper runs once
    // per TEST CASE (gtest builds a fresh fixture for each). Re-spending the full budget every
    // time makes the no-daemon path cost cases x budget, which overruns ctest's 300 s
    // integration-tier timeout well before the binary can report its skips:
    //   connection-commands  16 cases x 20 s = 320 s  -> killed at 300.01 s
    //   pipeline             15 cases x 20 s = 300 s  -> killed at 300.01 s
    // Both were observed as ***Timeout rather than ***Skipped. So latch the verdict: once an
    // attempt has burned the whole budget WITHOUT EVER CONNECTING, every later fixture in this
    // process answers false immediately and the suite skips in seconds.
    //
    // The retry loop still does the job it was written for. It defends against a server that IS
    // there but is transiently frozen by a prior test's EVAL busy-loop — in that case connect()
    // succeeds and only the flushall fails, so `ever_connected` is true and nothing latches.
    static std::atomic<bool> daemon_absent{false};
    if (daemon_absent.load(std::memory_order_relaxed))
        return false;

    qb::io::async::init();
    const auto give_up        = std::chrono::steady_clock::now() + budget;
    bool       ever_connected = false;
    for (;;) {
        redis.set_command_timeout(std::chrono::seconds(1));
        const bool connected = qb::io::async::run_sync(redis.connect());
        ever_connected       = ever_connected || connected;
        const bool flushed   = connected && qb::io::async::run_sync(redis.flushall()).ok();
        redis.set_command_timeout(qb::duration::zero());
        if (flushed)
            return true;
        if (std::chrono::steady_clock::now() >= give_up) {
            if (!ever_connected)
                daemon_absent.store(true, std::memory_order_relaxed);
            return false;
        }
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
            GTEST_SKIP() << kDaemonUnreachableSentinel << " (redis at " << redis_test_uri() << " not reachable)";
    }

    void
    TearDown() override {
        // Best-effort cleanup; ignore the result so teardown never reds an otherwise-green test.
        //
        // `IsSkipped()` guard: the old comment claimed a disconnected client "just fails the
        // flushall fast", but redis_try_connect() restores an UNBOUNDED command timeout before
        // returning, so a command queued on a client that never connected waits on a reply that
        // cannot arrive. Skipped tests must not pay for a cleanup they have nothing to clean.
        if (IsSkipped())
            return;
        (void) qb::io::async::run_sync(redis.flushall());
    }
};

namespace detail {
[[nodiscard]] inline int
process_id_for_keys() noexcept {
#if defined(_WIN32)
    return static_cast<int>(_getpid());
#else
    return static_cast<int>(getpid());
#endif
}
} // namespace detail

/// RESP wire protocol the parameterized suites exercise.
enum class ProtocolMode { RESP2, RESP3 };

/**
 * @brief Base fixture for RESP2/RESP3 parameterized integration suites.
 *
 * Same API as the legacy `protocol_test_common.h` `ProtocolModesTestBase`, but built on the
 * shared skip-not-throw + env-overridable contract: SetUp connects-or-`GTEST_SKIP`s (it never
 * throws, so a daemon-down run reports Skipped not Failed). Derive, add
 * `INSTANTIATE_PROTOCOL_MODES(SuiteName)`, and use `protocol_key()` for per-mode/per-pid keys.
 */
class ProtocolModesTestBase : public ::testing::TestWithParam<ProtocolMode> {
protected:
    qb::redis::tcp::client redis{redis_test_uri()};

    void
    SetUp() override {
        if (!redis_try_connect(redis))
            GTEST_SKIP() << kDaemonUnreachableSentinel << " (redis at " << redis_test_uri() << " not reachable)";
    }

    void
    TearDown() override {
        // Same IsSkipped() guard as RedisIntegrationTest::TearDown — a skipped test has a client
        // that never connected, and flushall() on it waits on a reply that cannot arrive.
        if (IsSkipped())
            return;
        (void) qb::io::async::run_sync(redis.flushall());
    }

    /// Per-mode, per-pid key (e.g. "base:resp3:pid12345"). NOTE: this does NOT by itself make
    /// concurrent runs safe — SetUp/TearDown issue a *global* FLUSHALL that would wipe a sibling
    /// process's keys regardless of how unique they are. The real isolation is the
    /// `RESOURCE_LOCK qb_redis_integration` set on every integration target in tests/CMakeLists.txt,
    /// which serializes these binaries under CTest so only one touches the shared daemon at a time.
    /// The per-pid suffix is belt-and-suspenders (keeps keys distinct in logs / if a stray run
    /// overlaps); it is NOT a substitute for the lock. Running two integration binaries as bare
    /// parallel processes (outside CTest's resource scheduler) IS unsafe — their FLUSHALLs collide.
    std::string
    protocol_key(const char *base) const {
        return std::string(base) + (GetParam() == ProtocolMode::RESP3 ? ":resp3" : ":resp2") + ":pid"
               + std::to_string(detail::process_id_for_keys());
    }
};

} // namespace qb::redis::test

// ── Legacy public-API surface ───────────────────────────────────────────────
// The pre-restructure protocol_test_common.h exposed these names at global scope.
// Re-export them so integration files migrated from that header compile unchanged
// (e.g. `class Foo : public ProtocolModesTestBase`, `GetParam() == ProtocolMode::RESP3`).
using ::qb::redis::test::ProtocolMode;
using ::qb::redis::test::ProtocolModesTestBase;
using ::qb::redis::test::RedisIntegrationTest;

/// Back-compat for the legacy `protocol_test_common.h` brace-init form
/// `qb::redis::tcp::client{REDIS_URI_PROTOCOL}`. Env-aware (honors `REDIS_URI`).
#define REDIS_URI_PROTOCOL {::qb::redis::test::redis_test_uri()}

/** Suppress nodiscard for cleanup/setup calls where the result is intentionally ignored. */
#define CO_IGNORE(expr) (void) (expr)

/**
 * @brief Pump the event loop until `completed` or a watchdog deadline expires.
 *
 * Replaces the unsafe `while (!completed) run(EVRUN_NOWAIT)` busy-spin (no diagnostic on hang).
 * The watchdog is a `scoped_callback`: destroying it at function exit stops the timer. Do NOT
 * use `async::callback` — it self-deletes only after fire, so a lambda capturing `&timed_out`
 * could run after `timed_out` left scope on an early finish → UB near the deadline.
 */
inline void
run_coro_test_until(const bool &completed, qb::duration timeout = std::chrono::seconds(30)) {
    bool timed_out = false;
    auto watchdog  = qb::io::async::scoped_callback([&timed_out]() noexcept { timed_out = true; }, timeout);
    (void) watchdog;
    while (!completed && !timed_out) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
    if (!completed) {
        ADD_FAILURE() << "Redis coroutine test exceeded " << qb::detail::to_ev_seconds(timeout) << "s watchdog — likely a hung await.";
    }
}

#define PROTOCOL_ENSURE_RESP3()                                 \
    if (GetParam() == ::qb::redis::test::ProtocolMode::RESP3) { \
        auto _h = co_await redis.hello(3);                      \
        EXPECT_TRUE(_h.ok()) << _h.error();                     \
        if (!_h.ok()) {                                         \
            done = true;                                        \
            co_return;                                          \
        }                                                       \
    }

#define PROTOCOL_ENSURE_RESP3_VAR(done_var)                     \
    if (GetParam() == ::qb::redis::test::ProtocolMode::RESP3) { \
        auto _h = co_await redis.hello(3);                      \
        EXPECT_TRUE(_h.ok()) << _h.error();                     \
        if (!_h.ok()) {                                         \
            done_var = true;                                    \
            co_return;                                          \
        }                                                       \
    }

#define PROTOCOL_ENSURE_RESP3_CONSUMER(consumer_var, done_var)  \
    if (GetParam() == ::qb::redis::test::ProtocolMode::RESP3) { \
        auto _h = co_await consumer_var.hello(3);               \
        EXPECT_TRUE(_h.ok()) << _h.error();                     \
        if (!_h.ok()) {                                         \
            done_var = true;                                    \
            co_return;                                          \
        }                                                       \
    }

#define PROTOCOL_ENSURE_RESP3_CLIENT(client_var, done_var)      \
    if (GetParam() == ::qb::redis::test::ProtocolMode::RESP3) { \
        auto _h = co_await client_var.hello(3);                 \
        EXPECT_TRUE(_h.ok()) << _h.error();                     \
        if (!_h.ok()) {                                         \
            done_var = true;                                    \
            co_return;                                          \
        }                                                       \
    }

#define INSTANTIATE_PROTOCOL_MODES(SuiteName)                                                                                   \
    INSTANTIATE_TEST_SUITE_P(Resp2AndResp3, SuiteName,                                                                          \
                             ::testing::Values(::qb::redis::test::ProtocolMode::RESP2, ::qb::redis::test::ProtocolMode::RESP3), \
                             [](const ::testing::TestParamInfo<::qb::redis::test::ProtocolMode> &info) {                        \
                                 return info.param == ::qb::redis::test::ProtocolMode::RESP3 ? "RESP3" : "RESP2";               \
                             })

#endif // QBM_REDIS_TESTS_SHARED_REDIS_INTEGRATION_FIXTURE_H
