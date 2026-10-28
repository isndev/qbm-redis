/**
 * @file metrics.cpp
 * @brief Explicit, snapshot-only redis metrics adaptation.
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2026 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)
 */
#include "metrics.h"

#include <stdexcept>
#include <utility>

namespace qb::metrics {

void
describe(snapshot &snap, qb::redis::stats const &stats, label_set labels) {
    snap.counter("qb_redis_commands_total", "Redis commands completed, including local refusals.").add(labels, stats.commands);
    snap.counter("qb_redis_errors_total", "Completed commands without success, a subset of commands.").add(labels, stats.errors);
    snap.counter("qb_redis_failed_total", "Commands without a server answer, a subset of errors.").add(labels, stats.failed);
    snap.counter("qb_redis_held_total", "Commands that waited for the connection to become ready.").add(labels, stats.held);
    snap.counter("qb_redis_retries_total", "Commands reissued by a retrying view after a retryable failure.").add(labels, stats.retries);
    snap.counter("qb_redis_redirects_total", "Cluster redirections (MOVED, ASK) a redirect-following view followed.")
        .add(labels, stats.redirects);
    snap.counter("qb_redis_reconnects_total", "Ready connections reached after a lost connection.").add(labels, stats.reconnects);
    snap.counter("qb_redis_gaps_total", "Consumer stream gap markers emitted.").add(labels, stats.gaps);
    snap.counter("qb_redis_messages_received_total", "Redis pub/sub messages received.").add(labels, stats.messages_received);
    snap.counter("qb_redis_messages_dropped_total", "Messages dropped by a full bounded consumer queue.").add(labels, stats.messages_dropped);
    auto &families = snap.counter("qb_redis_commands_by_family_total", "Completed commands by bounded command family.");
    for (std::size_t i = 0; i < qb::redis::command_families; ++i) {
        auto on_family = labels;
        on_family.push_back({"family", std::string(qb::redis::command_family_name(static_cast<qb::redis::command_family>(i)))});
        families.add(std::move(on_family), stats.by_family[i]);
    }
}

void
describe(snapshot &snap, qb::redis::latency_histogram const &latency, qb::redis::command_family const family, label_set labels) {
    if (static_cast<std::size_t>(family) >= qb::redis::command_families)
        throw std::invalid_argument("Redis metrics: invalid command family");
    labels.push_back({"family", std::string(qb::redis::command_family_name(family))});
    std::vector<double> bounds;
    bounds.reserve(latency.buckets.size() - 1);
    for (std::size_t i = 0; i + 1 < latency.buckets.size(); ++i) {
        // record() truncates to microseconds; Prometheus bounds are inclusive. Bucket 31 is +Inf.
        const auto inclusive_ns = (std::uint64_t{1} << (i + 1)) * 1000 - 1;
        bounds.push_back(std::chrono::duration<double>(std::chrono::nanoseconds(inclusive_ns)).count());
    }
    snap.histogram("qb_redis_command_duration_seconds", "Send-to-completion duration of explicitly timed commands, by family.")
        .add(std::move(labels), std::move(bounds), {latency.buckets.begin(), latency.buckets.end()}, std::nullopt);
}

} // namespace qb::metrics
