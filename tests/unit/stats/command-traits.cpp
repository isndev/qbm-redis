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

/**
 * @file command-traits.cpp
 * @brief The command table of `qbm/redis/stats.h` (Huly QB-134) against the command groups' own sources.
 *
 * The table is hand-maintained; this test is what keeps it honest. It reads every `*_commands.h` of the
 * module, finds each verb a command group issues -- the string literal after the first argument of a
 * `command<...>(` or `pubsub_command(` call -- and requires the table to hold exactly those verbs (plus the
 * ones the client issues outside the groups: MONITOR), each with the family of the group that issues it. A
 * verb added to a group without its row, a row whose verb no group issues any more, a row in the wrong family
 * or out of order: each fails here. The scan refuses to pass vacuously.
 *
 * It also pins what the flags say for the verbs whose classification matters (the command watchdog's blocking
 * set, read-only and idempotent writes for QB-133 -- `idempotent` is the verb's plain form, the retrying view
 * refuses the options that break it), the letter-case fallback, and the latency histogram.
 */

#include <gtest/gtest.h>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <qbm/redis/stats.h>

namespace command_traits_test {

/// The verbs the command groups issue, read from their sources: verb -> the group's file stem (`string`).
std::map<std::string, std::string>
scan_verbs(std::filesystem::path const &commands_dir) {
    std::map<std::string, std::string> verbs;
    for (auto const &entry : std::filesystem::directory_iterator(commands_dir)) {
        if (entry.path().extension() != ".h")
            continue;
        std::ifstream      in(entry.path(), std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        const std::string s    = text.str();
        std::string       stem = entry.path().stem().string(); // e.g. string_commands
        const auto        cut  = stem.rfind("_commands");
        if (cut != std::string::npos)
            stem.erase(cut);
        for (std::string const needle : {"command<", "pubsub_command("}) {
            for (std::size_t at = s.find(needle); at != std::string::npos; at = s.find(needle, at + 1)) {
                if (needle == "command<" && at >= 7 && s.compare(at - 7, 7, "pubsub_") == 0)
                    continue; // not a template call
                std::size_t i = at + needle.size();
                if (needle == "command<") {
                    int angle = 1; // skip the template argument list
                    while (i < s.size() && angle > 0) {
                        if (s[i] == '<')
                            ++angle;
                        else if (s[i] == '>')
                            --angle;
                        ++i;
                    }
                    if (i >= s.size() || s[i] != '(')
                        continue;
                    ++i;
                }
                int  depth     = 0; // skip the first argument, to its top-level comma
                bool in_string = false;
                for (; i < s.size(); ++i) {
                    const char c = s[i];
                    if (in_string) {
                        if (c == '\\')
                            ++i;
                        else if (c == '"')
                            in_string = false;
                        continue;
                    }
                    if (c == '"')
                        in_string = true;
                    else if (c == '(' || c == '[' || c == '{')
                        ++depth;
                    else if (c == ')' || c == ']' || c == '}') {
                        if (depth == 0)
                            break;
                        --depth;
                    } else if (c == ',' && depth == 0)
                        break;
                }
                if (i >= s.size() || s[i] != ',')
                    continue; // one argument: the coroutine form forwarding to the callback one
                ++i;
                while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
                    ++i;
                if (i >= s.size() || s[i] != '"')
                    continue; // the verb is a variable: the generic command<T>(name, ...)
                const auto end  = s.find('"', i + 1);
                const auto verb = s.substr(i + 1, end - i - 1);
                if (!verb.empty() && std::isupper(static_cast<unsigned char>(verb[0])))
                    verbs.emplace(verb, stem);
            }
        }
    }
    return verbs;
}

std::filesystem::path
commands_dir() {
    // tests/unit/stats/command-traits.cpp -> src/qbm/redis/commands
    return std::filesystem::path(__FILE__).parent_path() / ".." / ".." / ".." / "src" / "qbm" / "redis" / "commands";
}

/// The verbs the client issues outside the command groups, and their family.
const std::map<std::string, std::string> kOutsideTheGroups{{"MONITOR", "server"}};

} // namespace command_traits_test

using namespace command_traits_test;
using qb::redis::command_family;
using qb::redis::command_traits_of;

TEST(RedisCommandTraits, TheTableHoldsExactlyTheVerbsTheCommandGroupsIssue) {
    const auto dir = commands_dir();
    ASSERT_TRUE(std::filesystem::is_directory(dir)) << dir.string();
    auto scanned = scan_verbs(dir);
    ASSERT_GE(scanned.size(), 200u) << "the scan found too few verbs to mean anything: the scanner broke";
    for (auto const &[verb, family] : kOutsideTheGroups)
        scanned.emplace(verb, family);

    std::set<std::string> in_table;
    for (auto const &row : qb::redis::detail::command_table) {
        const std::string verb{row.verb};
        EXPECT_TRUE(in_table.insert(verb).second) << verb << " appears twice in the table";
        const auto it = scanned.find(verb);
        if (it == scanned.end()) {
            ADD_FAILURE() << verb << " is in the table but no command group issues it: remove its row";
            continue;
        }
        EXPECT_EQ(qb::redis::command_family_name(row.traits.family), it->second)
            << verb << " is issued by " << it->second << "_commands.h: its row says another family";
    }
    for (auto const &[verb, family] : scanned)
        EXPECT_TRUE(in_table.count(verb)) << verb << " (" << family << "_commands.h) has no row in qbm/redis/stats.h";
    for (std::size_t i = 1; i < qb::redis::detail::command_table.size(); ++i)
        EXPECT_LT(qb::redis::detail::command_table[i - 1].verb, qb::redis::detail::command_table[i].verb) << "the table is kept sorted";
}

TEST(RedisCommandTraits, TheBlockingSetIsTheWatchdogsOne) {
    // The verbs the command watchdog suspends for (they park server-side with their own timeout), exactly.
    const std::set<std::string> blocking{"BLPOP",    "BRPOP",  "BLMOVE", "BLMPOP",  "BRPOPLPUSH", "BZPOPMIN",
                                         "BZPOPMAX", "BZMPOP", "WAIT",   "WAITAOF", "XREAD",      "XREADGROUP"};
    for (auto const &row : qb::redis::detail::command_table)
        EXPECT_EQ(row.traits.blocking, blocking.count(std::string{row.verb}) == 1) << row.verb;
}

TEST(RedisCommandTraits, ReadOnlyHoldsInEveryFormIdempotentInThePlainOne) {
    EXPECT_TRUE(command_traits_of("GET").read_only);
    EXPECT_TRUE(command_traits_of("SORT_RO").read_only);
    EXPECT_FALSE(command_traits_of("SORT").read_only) << "SORT ... STORE writes";
    EXPECT_FALSE(command_traits_of("GEOSEARCHSTORE").read_only);
    EXPECT_TRUE(command_traits_of("DEL").idempotent);
    EXPECT_TRUE(command_traits_of("SET").idempotent);
    EXPECT_TRUE(command_traits_of("ZADD").idempotent) << "the plain form: the retrying view refuses ZADD ... INCR";
    EXPECT_FALSE(command_traits_of("INCR").idempotent);
    EXPECT_FALSE(command_traits_of("LPUSH").idempotent);
    for (auto const &row : qb::redis::detail::command_table)
        EXPECT_FALSE(row.traits.read_only && row.traits.idempotent) << row.verb << ": read-only and a write at once";
}

TEST(RedisCommandTraits, ALookupIgnoresTheLetterCaseAndKnowsNothingElse) {
    EXPECT_EQ(command_traits_of("get").family, command_family::string);
    EXPECT_TRUE(command_traits_of("blpop").blocking);
    const auto unknown = command_traits_of("MY.MODULE.CMD");
    EXPECT_EQ(unknown.family, command_family::other);
    EXPECT_FALSE(unknown.blocking || unknown.read_only || unknown.idempotent);
    EXPECT_EQ(command_traits_of(std::string(64, 'x')).family, command_family::other) << "a verb past the fold buffer";
}

TEST(RedisCommandTraits, EveryFamilyHasAName) {
    std::set<std::string_view> names;
    for (std::size_t i = 0; i < qb::redis::command_families; ++i) {
        const auto name = qb::redis::command_family_name(static_cast<command_family>(i));
        EXPECT_NE(name, "?") << i;
        names.insert(name);
    }
    EXPECT_EQ(names.size(), qb::redis::command_families);
    EXPECT_EQ(qb::redis::command_family_name(command_family::other), "other");
}

TEST(RedisLatencyHistogram, PercentilesAreTheUpperBoundOfTheirBucket) {
    using namespace std::chrono_literals;
    qb::redis::latency_histogram h;
    EXPECT_EQ(h.percentile(0.5), qb::duration::zero()) << "nothing recorded";
    for (int i = 0; i < 98; ++i)
        h.record(3us); // bucket 1: [2, 4) us
    h.record(100us);   // bucket 6: [64, 128) us
    h.record(5000us);  // bucket 12: [4096, 8192) us
    EXPECT_EQ(h.count(), 100u);
    EXPECT_EQ(h.percentile(0.50), qb::duration{std::chrono::microseconds{4}});
    EXPECT_EQ(h.percentile(0.98), qb::duration{std::chrono::microseconds{4}});
    EXPECT_EQ(h.percentile(0.99), qb::duration{std::chrono::microseconds{128}});
    EXPECT_EQ(h.percentile(1.00), qb::duration{std::chrono::microseconds{8192}});
    h.record(0us);
    EXPECT_EQ(h.buckets[0], 1u) << "under a microsecond lands in bucket 0";
    h.record(std::chrono::hours{2});
    EXPECT_EQ(h.buckets.back(), 1u) << "past 2^31 us lands in the last bucket";
}
