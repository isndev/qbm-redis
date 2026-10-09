/*
 * qb - C++ Actor Framework
 * Copyright (C) 2011-2026 isndev (cpp.actor). All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 *         limitations under the License.
 */

/** @file reconnect-disconnected-callback.cpp
 * @brief A failing consumer disconnect notification must not prevent auto-reconnect (QB-663).
 * The in-process loopback peer closes the real TCP connection; the second accept observes
 * a fresh connection rather than inferring reconnect from a flag.
 */

#include <gtest/gtest.h>
#include <chrono>
#include <stdexcept>
#include <string>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <qb/io/tcp/listener.h>
#include <qb/io/tcp/socket.h>
#include <qbm/redis/redis.h>

using namespace std::chrono_literals;

namespace {

template <typename Pred>
bool
run_until(Pred &&pred, std::chrono::milliseconds timeout = 2000ms) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!pred() && std::chrono::steady_clock::now() < deadline)
        qb::io::async::run(EVRUN_NOWAIT);
    return pred();
}

void
exercise_disconnect(int throw_kind, bool disable_policy) {
    qb::io::async::init();
    qb::io::tcp::listener listener;
    ASSERT_EQ(listener.listen_v4(0, "127.0.0.1"), qb::io::SocketStatus::Done);
    const auto                  uri = qb::io::uri{"tcp://127.0.0.1:" + std::to_string(listener.local_endpoint().port())};
    qb::redis::tcp::cb_consumer consumer{uri};
    ASSERT_TRUE(qb::io::async::run_sync(consumer.connect()));
    qb::io::tcp::socket first;
    ASSERT_EQ(listener.accept(first), qb::io::SocketStatus::Done);

    int disconnect_calls = 0;
    consumer.on_disconnected([&](qb::io::async::event::disconnected &&) {
        ++disconnect_calls;
        if (disable_policy)
            consumer.disable_auto_reconnect();
        if (throw_kind == 1)
            throw std::runtime_error("disconnect callback failed");
        if (throw_kind == 2)
            throw 42;
    });
    consumer.enable_auto_reconnect(
        qb::redis::RetryPolicy{}.with_max_attempts(2).with_initial_delay(1ms).with_connect_timeout(500ms).with_jitter(false));
    int pending_calls = 0;
    consumer.subscribe(
        [&](qb::redis::Reply<qb::redis::subscription> &&r) {
            ++pending_calls;
            EXPECT_FALSE(r.ok());
            EXPECT_EQ(r.error(), "disconnected");
        },
        "news");
    ASSERT_EQ(consumer.pending_reply_count(), 1u);
    first.disconnect();

    ASSERT_TRUE(run_until([&] { return disconnect_calls == 1; })) << "peer closure did not notify the consumer";
    EXPECT_EQ(pending_calls, 1) << "the in-flight subscription has one terminal callback";
    EXPECT_EQ(consumer.pending_reply_count(), 0u);
    if (disable_policy) {
        for (int i = 0; i < 100; ++i)
            qb::io::async::run(EVRUN_NOWAIT);
        EXPECT_FALSE(consumer.is_reconnecting()) << "callback disabled policy before reconnect decision";
        EXPECT_FALSE(consumer.is_connected());
        return;
    }

    ASSERT_TRUE(run_until([&] { return consumer.is_connected(); })) << "callback throw suppressed auto-reconnect";
    qb::io::tcp::socket second;
    ASSERT_EQ(listener.accept(second), qb::io::SocketStatus::Done) << "fresh connection was not accepted";
    EXPECT_EQ(disconnect_calls, 1);
    EXPECT_EQ(pending_calls, 1) << "reconnect must not replay the old subscription";
    consumer.disable_auto_reconnect();
    consumer.disconnect();
}

} // namespace

TEST(RedisReconnectDisconnectedCallback, StandardExceptionStillReconnects) {
    exercise_disconnect(1, false);
}

TEST(RedisReconnectDisconnectedCallback, NonStandardExceptionStillReconnects) {
    exercise_disconnect(2, false);
}

TEST(RedisReconnectDisconnectedCallback, NormalCallbackStillReconnects) {
    exercise_disconnect(0, false);
}

TEST(RedisReconnectDisconnectedCallback, CallbackCanDisablePolicyBeforeDecision) {
    exercise_disconnect(0, true);
}

TEST(RedisReconnectDisconnectedCallback, ThrowingCallbackCanDisablePolicyBeforeDecision) {
    exercise_disconnect(1, true);
}
