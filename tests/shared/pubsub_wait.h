/*
 * qb - C++ Actor Framework
 * Copyright (C) 2011-2026 isndev (cpp.actor). All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use it except in compliance with the License.
 * See the License for the specific terms.
 */

/**
 * @file pubsub_wait.h
 * @brief Delivery-driven wait helpers for qbm-redis pub/sub integration tests.
 *
 * Pub/Sub message delivery is asynchronous: a PUBLISH returns the receiver count
 * synchronously, but the MESSAGE frame to each subscriber is delivered on a later
 * loop turn. The legacy subscription/publish tests gated on a fixed
 * `co_await sleep(50-100ms)` then asserted exact counts — the single dominant
 * flaky-timing defect of this module (spec §7.C): under load the window closes
 * before delivery and the test spuriously reds, while on a fast box it just wastes
 * 100 ms per case.
 *
 * These helpers replace every fixed sleep with a **predicate-driven pump**: run the
 * event loop in non-blocking bursts until a caller-supplied condition holds (e.g.
 * `message_count == N`, `received.size() == N`) or a bounded watchdog fires. On
 * timeout they `ADD_FAILURE()` with a diagnostic instead of hanging — mirroring the
 * `run_coro_test_until` contract the fixture already ships.
 *
 * The watchdog is a `scoped_callback` (stopped at scope exit), NOT `async::callback`:
 * the latter self-deletes only after it fires, so a lambda capturing a stack flag
 * would run after the flag went out of scope when the predicate is met early.
 */

#ifndef QBM_REDIS_TESTS_SHARED_PUBSUB_WAIT_H
#define QBM_REDIS_TESTS_SHARED_PUBSUB_WAIT_H

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <gtest/gtest.h>
#include <qb/io/async.h>

namespace qb::redis::test {

/**
 * @brief Pump the event loop until @p predicate returns true or @p timeout elapses.
 *
 * @param predicate Condition checked between non-blocking loop bursts. Returns true
 *                  once the awaited delivery / state change has happened.
 * @param timeout   Watchdog deadline (default 5 s — generous for delivery, short
 *                  enough that a wedged loop fails the test instead of CI-timing-out).
 * @return true if the predicate was satisfied before the watchdog fired.
 *
 * On timeout the caller's test is failed via `ADD_FAILURE()` (non-fatal) so the body
 * can still tear subscriptions down; check the return value if you need to branch.
 */
[[nodiscard]] inline bool
pubsub_wait_until(const std::function<bool()> &predicate, qb::duration timeout = std::chrono::seconds(5)) {
    if (predicate())
        return true;

    bool timed_out = false;
    auto watchdog  = qb::io::async::scoped_callback([&timed_out]() noexcept { timed_out = true; }, timeout);
    (void) watchdog;

    while (!timed_out) {
        qb::io::async::run(EVRUN_NOWAIT);
        if (predicate())
            return true;
    }

    ADD_FAILURE() << "pubsub delivery watchdog fired after " << qb::detail::to_ev_seconds(timeout)
                  << "s — expected message(s) never arrived (delivery stalled or lost).";
    return false;
}

/**
 * @brief Pump the loop until an atomic message counter reaches (at least) @p expected.
 *
 * Convenience over @ref pubsub_wait_until for the common `std::atomic<size_t>
 * message_count` callback-consumer pattern. Waits for `counter >= expected` so a
 * burst that over-delivers is still observable (assert the exact count afterwards).
 *
 * @param counter   Atomic incremented by the consumer's message callback.
 * @param expected  Target delivery count.
 * @param timeout   Watchdog deadline.
 * @return true if `counter >= expected` was reached before the watchdog fired.
 */
[[nodiscard]] inline bool
pubsub_wait_count(const std::atomic<size_t> &counter, size_t expected, qb::duration timeout = std::chrono::seconds(5)) {
    return pubsub_wait_until([&counter, expected]() { return counter.load() >= expected; }, timeout);
}

/**
 * @brief Pump the loop for the full @p settle window, ignoring any predicate.
 *
 * For NEGATIVE assertions only: "after unsubscribe, a later publish must NOT fire the
 * callback". There is no event to wait *for*, so we deterministically drain the loop
 * for a bounded settle window and then assert the counter did not advance. Unlike a
 * fixed `sleep`, this keeps the loop live (so a stray late delivery WOULD be observed
 * and caught) rather than parking the thread blind.
 *
 * @param settle Duration to keep pumping (default 200 ms — long enough that an
 *               in-flight delivery on a local daemon would have landed).
 */
inline void
pubsub_drain_for(qb::duration settle = std::chrono::milliseconds(200)) {
    bool elapsed   = false;
    auto stopwatch = qb::io::async::scoped_callback([&elapsed]() noexcept { elapsed = true; }, settle);
    (void) stopwatch;
    while (!elapsed)
        qb::io::async::run(EVRUN_NOWAIT);
}

} // namespace qb::redis::test

#endif // QBM_REDIS_TESTS_SHARED_PUBSUB_WAIT_H
