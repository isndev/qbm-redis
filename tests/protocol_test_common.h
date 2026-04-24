/*
 * qb - C++ Actor Framework
 * Copyright (C) 2011-2025 isndev (cpp.actor). All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use it except in compliance with the License.
 * See the License for the specific terms.
 */

/**
 * @file protocol_test_common.h
 * @brief Shared fixture and helpers for RESP2/RESP3 protocol mode tests.
 *
 * Include this header in test files to add parameterized tests that run
 * commands in both RESP2 and RESP3 modes.
 */

#ifndef QBM_REDIS_TESTS_PROTOCOL_TEST_COMMON_H
#define QBM_REDIS_TESTS_PROTOCOL_TEST_COMMON_H

#include <gtest/gtest.h>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <string>
#include "../redis.h"

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#define REDIS_URI_PROTOCOL {"tcp://localhost:6379"}

namespace qbm_redis_test_detail {
[[nodiscard]] inline int process_id_for_keys() noexcept {
#if defined(_WIN32)
    return static_cast<int>(_getpid());
#else
    return static_cast<int>(getpid());
#endif
}
} // namespace qbm_redis_test_detail

enum class ProtocolMode { RESP2, RESP3 };

/**
 * Base fixture for protocol mode tests. Derive and add INSTANTIATE_TEST_SUITE_P
 * with a unique suite name per test file.
 */
class ProtocolModesTestBase : public ::testing::TestWithParam<ProtocolMode> {
protected:
    qb::redis::tcp::client redis{REDIS_URI_PROTOCOL};

    void SetUp() override {
        qb::io::async::init();
        if (!qb::io::async::run_sync(redis.connect()) ||
            !qb::io::async::run_sync(redis.flushall()).ok()) {
            throw std::runtime_error("Unable to connect to Redis or flushall failed");
        }
    }

    void TearDown() override {
        qb::io::async::run_sync(redis.flushall());
    }

    std::string protocol_key(const char* base) const {
        // Include process id so two concurrent ctest workers (or manual parallel runs)
        // against the same Redis do not reuse identical key names.
        return std::string(base) + (GetParam() == ProtocolMode::RESP3 ? ":resp3" : ":resp2") +
               ":pid" + std::to_string(qbm_redis_test_detail::process_id_for_keys());
    }
};

/** Suppress nodiscard for cleanup/setup calls where result is intentionally ignored */
#define CO_IGNORE(expr) (void)(expr)

/**
 * @brief Pump the event loop until `completed` or a watchdog deadline expires.
 * Replaces the unsafe `while (!completed) run(EVRUN_NOWAIT)` pattern (busy spin, no
 * diagnostic on hang).
 *
 * Uses `EVRUN_NOWAIT` for throughput. The watchdog is `scoped_callback`: destroying
 * it at function exit stops the timer. Do **not** use `async::callback` here — it
 * self-deletes only after fire, so a lambda capturing `&timed_out` would run after
 * `timed_out` went out of scope when the test finishes early → segfault near timeout.
 *
 * @param completed   Flag set to true by the coroutine body on success.
 * @param timeout_sec Deadline applied to the overall test body.
 */
inline void run_coro_test_until(const bool& completed, double timeout_sec = 30.0) {
    bool timed_out = false;
    auto watchdog = qb::io::async::scoped_callback([&timed_out]() noexcept { timed_out = true; }, timeout_sec);
    (void)watchdog;
    while (!completed && !timed_out) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
    if (!completed) {
        ADD_FAILURE() << "Redis coroutine test exceeded "
                      << timeout_sec << "s watchdog — likely a hung await.";
    }
}

#define PROTOCOL_ENSURE_RESP3() \
    if (GetParam() == ProtocolMode::RESP3) { \
        auto _h = co_await redis.hello(3); \
        EXPECT_TRUE(_h.ok()) << _h.error(); \
        if (!_h.ok()) { done = true; co_return; } \
    }

#define PROTOCOL_ENSURE_RESP3_VAR(done_var) \
    if (GetParam() == ProtocolMode::RESP3) { \
        auto _h = co_await redis.hello(3); \
        EXPECT_TRUE(_h.ok()) << _h.error(); \
        if (!_h.ok()) { done_var = true; co_return; } \
    }

#define PROTOCOL_ENSURE_RESP3_CONSUMER(consumer_var, done_var) \
    if (GetParam() == ProtocolMode::RESP3) { \
        auto _h = co_await consumer_var.hello(3); \
        EXPECT_TRUE(_h.ok()) << _h.error(); \
        if (!_h.ok()) { done_var = true; co_return; } \
    }

#define PROTOCOL_ENSURE_RESP3_CLIENT(client_var, done_var) \
    if (GetParam() == ProtocolMode::RESP3) { \
        auto _h = co_await client_var.hello(3); \
        EXPECT_TRUE(_h.ok()) << _h.error(); \
        if (!_h.ok()) { done_var = true; co_return; } \
    }

#define INSTANTIATE_PROTOCOL_MODES(SuiteName) \
    INSTANTIATE_TEST_SUITE_P(Resp2AndResp3, SuiteName, \
        ::testing::Values(ProtocolMode::RESP2, ProtocolMode::RESP3), \
        [](const ::testing::TestParamInfo<ProtocolMode>& info) { \
            return info.param == ProtocolMode::RESP3 ? "RESP3" : "RESP2"; \
        })

#endif // QBM_REDIS_TESTS_PROTOCOL_TEST_COMMON_H
