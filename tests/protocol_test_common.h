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
#include "../redis.h"

#define REDIS_URI_PROTOCOL {"tcp://localhost:6379"}

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
        return std::string(base) + (GetParam() == ProtocolMode::RESP3 ? ":resp3" : ":resp2");
    }
};

/** Suppress nodiscard for cleanup/setup calls where result is intentionally ignored */
#define CO_IGNORE(expr) (void)(expr)

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
