/**
 * @file metrics.h
 * @brief Explicit, snapshot-only redis metrics adaptation.
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2026 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)
 */
#pragma once

#include <qb/core/metrics.h>
#include "stats.h"

namespace qb::metrics {

/**
 * @brief Adds the counters and gauges of one redis statistics snapshot.
 * @details No hooks, clocks or client access. Use stable connection/core labels to distinguish sources;
 *          canonicalize() sums samples with identical labels, including last-duration gauges.
 *          Do not use SQL, keys, credentials or user-supplied paths as labels.
 */
void describe(snapshot &snap, qb::redis::stats const &stats, label_set labels);

/**
 * @brief Adds one explicitly enabled Redis latency histogram, with a bounded `family` label.
 * @details Call only while the client's latency_stats() is enabled. The 31 finite inclusive bounds
 *          represent the integer-nanosecond intervals of record(); bucket 31 is unbounded. No sum is
 *          exposed because the client stores none. Disabling latency statistics discards the distribution.
 *          The caller's labels must not contain `family` or the reserved histogram label `le`.
 * @throws std::invalid_argument for an invalid command family or invalid/duplicate labels.
 */
void describe(snapshot &snap, qb::redis::latency_histogram const &latency, qb::redis::command_family family, label_set labels);

} // namespace qb::metrics
