/** @file unit/stats/metrics.cpp
 * @brief Pure Redis statistics exposition, including exact histogram interval boundaries.
 */
#include <gtest/gtest.h>
#include <qbm/redis/metrics.h>

namespace redis_metrics_test {
TEST(RedisMetrics, TotalsAndFamiliesAreSeparateExactCounters) {
    qb::redis::stats        stats;
    constexpr std::uint64_t first = 9007199254740993ULL;
    stats.commands                = first;
    stats.errors                  = first + 1;
    stats.failed                  = first + 2;
    stats.held                    = first + 3;
    stats.retries                 = first + 4;
    stats.redirects               = first + 5;
    stats.oneway                  = first + 6;
    stats.reconnects              = first + 7;
    stats.gaps                    = first + 8;
    stats.messages_received       = first + 9;
    stats.messages_dropped        = first + 10;
    for (std::size_t i = 0; i < stats.by_family.size(); ++i)
        stats.by_family[i] = first + i;
    qb::metrics::snapshot snapshot;
    qb::metrics::describe(snapshot, stats, {{"connection", "cache"}, {"core", "1"}});
    const char *names[] = {"commands",          "errors",          "failed", "held", "retries", "redirects", "oneway", "reconnects", "gaps",
                           "messages_received", "messages_dropped"};
    for (std::size_t i = 0; i < std::size(names); ++i) {
        const auto  name   = "qb_redis_" + std::string(names[i]) + "_total";
        const auto *family = snapshot.find(name);
        ASSERT_NE(family, nullptr) << name;
        EXPECT_EQ(family->type, qb::metrics::metric_type::counter);
        ASSERT_EQ(family->samples.size(), 1u);
        EXPECT_TRUE(family->samples[0].number.is_integer());
        EXPECT_EQ(family->samples[0].number.integer(), first + i);
        EXPECT_EQ(family->samples[0].labels.size(), 2u);
    }
    const auto *families = snapshot.find("qb_redis_commands_by_family_total");
    ASSERT_NE(families, nullptr);
    ASSERT_EQ(families->samples.size(), qb::redis::command_families);
    for (std::size_t i = 0; i < families->samples.size(); ++i) {
        EXPECT_EQ(families->samples[i].number.integer(), first + i);
        EXPECT_EQ(families->samples[i].labels.back().name, "family");
        EXPECT_EQ(families->samples[i].labels.back().value, qb::redis::command_family_name(static_cast<qb::redis::command_family>(i)));
    }
    EXPECT_EQ(snapshot.families().size(), 12u) << "eleven totals and the by-family breakdown";
    EXPECT_EQ(snapshot.find("qb_redis_command_duration_seconds"), nullptr);
    const auto text = qb::metrics::write_text(snapshot);
    EXPECT_NE(text.find("qb_redis_commands_total{connection=\"cache\",core=\"1\"} 9007199254740993\n"), std::string::npos);
}

TEST(RedisMetrics, HistogramUsesInclusiveNanosecondBoundsAndUnboundedOverflow) {
    qb::redis::latency_histogram latency;
    for (const auto span :
         {qb::duration(0), qb::duration(1999), qb::duration(2000), qb::duration(3999), qb::duration(4000), qb::duration(2147483647999LL),
          qb::duration(2147483648000LL), qb::duration(std::chrono::hours(24))})
        latency.record(span);
    qb::metrics::snapshot snapshot;
    qb::metrics::describe(snapshot, latency, qb::redis::command_family::connection, {{"connection", "cache"}});
    const auto *family = snapshot.find("qb_redis_command_duration_seconds");
    ASSERT_NE(family, nullptr);
    ASSERT_EQ(family->histograms.size(), 1u);
    const auto &histogram = family->histograms[0];
    ASSERT_EQ(histogram.bounds.size(), 31u);
    ASSERT_EQ(histogram.buckets.size(), 32u);
    EXPECT_DOUBLE_EQ(histogram.bounds[0], 0.000001999);
    EXPECT_DOUBLE_EQ(histogram.bounds[1], 0.000003999);
    EXPECT_DOUBLE_EQ(histogram.bounds[30], 2147.483647999);
    EXPECT_EQ(histogram.buckets[0], 2u);
    EXPECT_EQ(histogram.buckets[1], 2u);
    EXPECT_EQ(histogram.buckets[2], 1u);
    EXPECT_EQ(histogram.buckets[30], 1u);
    EXPECT_EQ(histogram.buckets[31], 2u);
    EXPECT_EQ(histogram.count(), 8u);
    EXPECT_FALSE(histogram.sum.has_value());
    const auto text = qb::metrics::write_text(snapshot);
    EXPECT_NE(text.find("le=\"+Inf\"} 8\n"), std::string::npos);
    EXPECT_NE(text.find("qb_redis_command_duration_seconds_count{connection=\"cache\",family=\"connection\"} 8\n"), std::string::npos);
    EXPECT_EQ(text.find("qb_redis_command_duration_seconds_sum"), std::string::npos);
}
TEST(RedisMetrics, InvalidFamilyAndConflictingLabelsAreRejected) {
    qb::metrics::snapshot        snapshot;
    qb::redis::latency_histogram latency;
    EXPECT_THROW(qb::metrics::describe(snapshot, latency, static_cast<qb::redis::command_family>(qb::redis::command_families), {}),
                 std::invalid_argument);
    EXPECT_TRUE(snapshot.empty());
    EXPECT_THROW(qb::metrics::describe(snapshot, latency, qb::redis::command_family::connection, {{"family", "spoofed"}}),
                 std::invalid_argument);
    EXPECT_THROW(qb::metrics::describe(snapshot, latency, qb::redis::command_family::connection, {{"le", "spoofed"}}), std::invalid_argument);
}
} // namespace redis_metrics_test
