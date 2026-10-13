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
 * @file integration/stream/stream-commands.cpp
 * @brief Live RESP2/RESP3 integration tests for the qbm-redis stream (X*) command mixin.
 *
 * Restructured from `test-stream-commands.cpp`:
 *   - the 4 terse-tail dups (the 2nd XADD_XLEN, XREAD_JSON, XRANGE_XREVRANGE, XDEL_INTEGER)
 *     are deleted — strict subsets of the CORO_* bodies;
 *   - XREAD / XREADGROUP no longer assert only `.ok()`: they assert the read picked up the
 *     expected number of entries and that the seeded field/value pairs appear in the decoded
 *     JSON (deterministic across RESP2/RESP3 via the serialized form);
 *   - XINFO STREAM asserts concrete fields (`length`, `last-generated-id`);
 *   - new cases: explicit-ID add + duplicate-ID error, XACK on an unknown ID → 0,
 *     and XPENDING detail (range + consumer-filtered) over a known pending entry;
 *   - busy-spins → shared `run_coro_test_until` watchdog.
 *
 * NOTE: the wrapper exposes neither a standalone XSETID command nor XTRIM MINID (only
 * XTRIM MAXLEN); those two ADD items from the spec are covered as far as the typed surface
 * permits — XADD explicit-ID gives deterministic IDs, and trimming is asserted via XTRIM
 * MAXLEN. See the manifest deviation note.
 */

#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include "../../shared/redis_integration_fixture.h"
#include <qbm/redis/redis.h>

using namespace qb::io;
using namespace qb::redis::test;

namespace {
struct LogicalStreamEntry {
    std::string                                      id;
    std::vector<std::pair<std::string, std::string>> fields;

    bool operator==(const LogicalStreamEntry &) const = default;
};

[[nodiscard]] bool
json_contains(const qb::json &wire, const std::string &needle) {
    return wire.dump().find(needle) != std::string::npos;
}

[[nodiscard]] std::optional<std::vector<std::pair<std::string, std::string>>>
decode_fields(const qb::json &wire) {
    std::vector<std::pair<std::string, std::string>> fields;
    if (wire.is_object()) {
        for (auto it = wire.begin(); it != wire.end(); ++it) {
            if (!it.value().is_string())
                return std::nullopt;
            fields.emplace_back(it.key(), it.value().get<std::string>());
        }
        return fields;
    }
    if (!wire.is_array() || wire.size() % 2 != 0)
        return std::nullopt;
    for (std::size_t i = 0; i < wire.size(); i += 2) {
        if (!wire[i].is_string() || !wire[i + 1].is_string())
            return std::nullopt;
        fields.emplace_back(wire[i].get<std::string>(), wire[i + 1].get<std::string>());
    }
    return fields;
}

[[nodiscard]] std::optional<std::vector<LogicalStreamEntry>>
decode_entry_array(const qb::json &wire) {
    if (!wire.is_array())
        return std::nullopt;
    std::vector<LogicalStreamEntry> entries;
    for (const auto &entry : wire) {
        std::string     id;
        const qb::json *field_wire = nullptr;
        if (entry.is_array() && entry.size() == 2 && entry[0].is_string()) {
            id         = entry[0].get<std::string>();
            field_wire = &entry[1];
        } else if (entry.is_object() && entry.size() == 1) {
            const auto it = entry.begin();
            id            = it.key();
            field_wire    = &it.value();
        } else {
            return std::nullopt;
        }
        auto fields = decode_fields(*field_wire);
        if (!fields)
            return std::nullopt;
        entries.push_back({std::move(id), std::move(*fields)});
    }
    return entries;
}

/// Normalize RESP2's nested arrays and RESP3's map into one logical stream view.
[[nodiscard]] std::optional<std::vector<LogicalStreamEntry>>
decode_stream(const qb::json &wire, const std::string &key) {
    if (wire.is_null() || (wire.is_array() && wire.empty()))
        return std::vector<LogicalStreamEntry>{};
    if (wire.is_object()) {
        const auto it = wire.find(key);
        if (it == wire.end())
            return std::nullopt;
        return decode_entry_array(*it);
    }
    if (!wire.is_array())
        return std::nullopt;
    for (const auto &stream : wire) {
        if (stream.is_array() && stream.size() == 2 && stream[0].is_string() && stream[0].get<std::string>() == key)
            return decode_entry_array(stream[1]);
        if (stream.is_object()) {
            const auto it = stream.find(key);
            if (it != stream.end())
                return decode_entry_array(*it);
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::vector<std::pair<std::string, std::string>>>
decode_pending_owners(const qb::json &wire) {
    if (!wire.is_array())
        return std::nullopt;
    std::vector<std::pair<std::string, std::string>> pending;
    for (const auto &entry : wire) {
        if (!entry.is_array() || entry.size() < 2 || !entry[0].is_string() || !entry[1].is_string())
            return std::nullopt;
        pending.emplace_back(entry[0].get<std::string>(), entry[1].get<std::string>());
    }
    return pending;
}
} // namespace

class StreamProtocolModesTest : public ProtocolModesTestBase {};

INSTANTIATE_PROTOCOL_MODES(StreamProtocolModesTest);

// XADD (auto-id) + XLEN.
TEST_P(StreamProtocolModesTest, XADD_XLEN) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("basic");

        std::vector<std::pair<std::string, std::string>> entries   = {{"field1", "value1"}, {"field2", "value2"}};
        auto                                             add_reply = co_await redis.xadd(key, entries);
        EXPECT_TRUE(add_reply.ok()) << add_reply.error();
        EXPECT_GT(add_reply.result().timestamp, 0);

        EXPECT_EQ((co_await redis.xlen(key)).result(), 1);

        CO_IGNORE(co_await redis.xadd(key, {{"field3", "value3"}}));
        CO_IGNORE(co_await redis.xadd(key, {{"field4", "value4"}}));
        EXPECT_EQ((co_await redis.xlen(key)).result(), 3);

        CO_IGNORE(co_await redis.del(key));
        completed = true;
    });
    run_coro_test_until(completed);
}

// XADD with an explicit ID, and the duplicate / out-of-order ID error path.
TEST_P(StreamProtocolModesTest, XADD_EXPLICIT_AND_DUPLICATE_ID) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("explicit-id");

        std::vector<std::pair<std::string, std::string>> e1 = {{"f", "v1"}};
        auto                                             a1 = co_await redis.xadd(key, e1, std::string("5-5"));
        EXPECT_TRUE(a1.ok()) << a1.error();
        EXPECT_EQ(a1.result().timestamp, 5);
        EXPECT_EQ(a1.result().sequence, 5);

        // A strictly larger ID is accepted.
        std::vector<std::pair<std::string, std::string>> e2 = {{"f", "v2"}};
        auto                                             a2 = co_await redis.xadd(key, e2, std::string("5-6"));
        EXPECT_TRUE(a2.ok()) << a2.error();
        EXPECT_EQ(a2.result().sequence, 6);

        // Re-using a smaller-or-equal ID is rejected by the server.
        std::vector<std::pair<std::string, std::string>> e3  = {{"f", "dup"}};
        auto                                             dup = co_await redis.xadd(key, e3, std::string("5-5"));
        EXPECT_FALSE(dup.ok());
        EXPECT_FALSE(dup.error().empty());

        EXPECT_EQ((co_await redis.xlen(key)).result(), 2); // dup was not added

        CO_IGNORE(co_await redis.del(key));
        completed = true;
    });
    run_coro_test_until(completed);
}

// Redis accepts each 64-bit component past INT64_MAX; typed XADD/XRANGE must preserve it.
TEST_P(StreamProtocolModesTest, XADD_XRANGE_UNSIGNED_ID_BOUNDARY) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        for (const auto *id_text : {"1-9223372036854775808", "1-18446744073709551615", "9223372036854775808-0", "18446744073709551615-0"}) {
            const std::string key   = protocol_key("unsigned") + "-" + id_text;
            auto              added = co_await redis.xadd(key, {{"f", "v"}}, std::string(id_text));
            EXPECT_TRUE(added.ok()) << id_text << ": " << added.error();
            if (added.ok()) {
                EXPECT_EQ(added.result().to_string(), id_text);
                EXPECT_GT(added.result().timestamp_u64(), 0u);
            }

            auto length = co_await redis.xlen(key);
            EXPECT_TRUE(length.ok()) << length.error();
            if (length.ok()) {
                EXPECT_EQ(length.result(), 1) << id_text; // Redis committed even if local decode failed.
            }

            auto range = co_await redis.xrange(key, "-", "+");
            EXPECT_TRUE(range.ok()) << id_text << ": " << range.error();
            if (range.ok()) {
                EXPECT_EQ(range.result().size(), 1u);
                if (!range.result().empty()) {
                    EXPECT_EQ(range.result()[0].id.to_string(), id_text);
                    EXPECT_EQ(range.result()[0].fields.at("f"), "v");
                }
            }
            CO_IGNORE(co_await redis.del(key));
        }
        completed = true;
    });
    run_coro_test_until(completed);
}

// XDEL.
TEST_P(StreamProtocolModesTest, XDEL) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("delete");

        std::vector<std::pair<std::string, std::string>> e1 = {{"field", "value1"}};
        auto                                             a1 = co_await redis.xadd(key, e1);
        EXPECT_TRUE(a1.ok()) << a1.error();
        std::string id1 = a1.result().to_string();

        std::vector<std::pair<std::string, std::string>> e2 = {{"field", "value2"}};
        EXPECT_TRUE((co_await redis.xadd(key, e2)).ok());

        EXPECT_EQ((co_await redis.xlen(key)).result(), 2);

        auto del_reply = co_await redis.xdel(key, id1);
        EXPECT_TRUE(del_reply.ok()) << del_reply.error();
        EXPECT_EQ(del_reply.result(), 1);

        EXPECT_EQ((co_await redis.xlen(key)).result(), 1);

        // Deleting an already-removed ID returns 0.
        auto del_again = co_await redis.xdel(key, id1);
        EXPECT_TRUE(del_again.ok()) << del_again.error();
        EXPECT_EQ(del_again.result(), 0);

        CO_IGNORE(co_await redis.del(key));
        completed = true;
    });
    run_coro_test_until(completed);
}

// XGROUP CREATE / DESTROY.
TEST_P(StreamProtocolModesTest, XGROUP) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key   = protocol_key("group");
        std::string group = "mygroup";

        CO_IGNORE(co_await redis.xadd(key, {{"field", "value"}}));

        auto create_reply = co_await redis.xgroup_create(key, group, "0", true);
        EXPECT_TRUE(create_reply.ok()) << create_reply.error();
        EXPECT_TRUE(create_reply.result().ok());

        auto destroy_reply = co_await redis.xgroup_destroy(key, group);
        EXPECT_TRUE(destroy_reply.ok()) << destroy_reply.error();
        EXPECT_EQ(destroy_reply.result(), 1);

        CO_IGNORE(co_await redis.del(key));
        completed = true;
    });
    run_coro_test_until(completed);
}

// XTRIM MAXLEN: exact trim to a target length.
TEST_P(StreamProtocolModesTest, XTRIM) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("trim");

        for (int i = 0; i < 10; ++i)
            CO_IGNORE(co_await redis.xadd(key, {{"field", std::to_string(i)}}));

        EXPECT_EQ((co_await redis.xlen(key)).result(), 10);

        // Exact (non-approximate) trim to 5 → removes exactly 5.
        auto trim_reply = co_await redis.xtrim(key, 5);
        EXPECT_TRUE(trim_reply.ok()) << trim_reply.error();
        EXPECT_EQ(trim_reply.result(), 5);
        EXPECT_EQ((co_await redis.xlen(key)).result(), 5);

        CO_IGNORE(co_await redis.del(key));
        completed = true;
    });
    run_coro_test_until(completed);
}

// XREAD: reads back all seeded entries and their field/value payloads.
TEST_P(StreamProtocolModesTest, XREAD) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("read");

        auto added1 = co_await redis.xadd(key, {{"field1", "value1"}});
        auto added2 = co_await redis.xadd(key, {{"field2", "value2"}});
        auto added3 = co_await redis.xadd(key, {{"field3", "value3"}});
        if (!(added1.ok() && added2.ok() && added3.ok())) {
            ADD_FAILURE() << "failed to seed all XREAD entries";
            completed = true;
            co_return;
        }

        auto read_reply = co_await redis.xread(key, "0", 10);
        if (!read_reply.ok()) {
            ADD_FAILURE() << read_reply.error();
            completed = true;
            co_return;
        }
        auto entries = decode_stream(read_reply.result(), key);
        if (!entries) {
            ADD_FAILURE() << "XREAD reply has no decodable stream entry shape: " << read_reply.result().dump();
            completed = true;
            co_return;
        }
        EXPECT_EQ(*entries, (std::vector<LogicalStreamEntry>{
                                {added1.result().to_string(), {{"field1", "value1"}}},
                                {added2.result().to_string(), {{"field2", "value2"}}},
                                {added3.result().to_string(), {{"field3", "value3"}}},
                            }));

        // Reading from the max id ("$" newest) with no new entries yields an empty reply.
        auto empty_reply = co_await redis.xread(key, "$", 10);
        if (!empty_reply.ok()) {
            ADD_FAILURE() << empty_reply.error();
            completed = true;
            co_return;
        }
        auto empty = decode_stream(empty_reply.result(), key);
        if (!empty) {
            ADD_FAILURE() << "empty XREAD reply has no decodable stream shape: " << empty_reply.result().dump();
            completed = true;
            co_return;
        }
        EXPECT_TRUE(empty->empty());

        CO_IGNORE(co_await redis.del(key));
        completed = true;
    });
    run_coro_test_until(completed);
}

// XREADGROUP: the group's first read delivers the seeded entries.
TEST_P(StreamProtocolModesTest, XREADGROUP) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key      = protocol_key("readgroup");
        std::string group    = "testgroup";
        std::string consumer = "testconsumer";

        auto added1 = co_await redis.xadd(key, {{"field", "value1"}});
        auto added2 = co_await redis.xadd(key, {{"field", "value2"}});
        if (!(added1.ok() && added2.ok())) {
            ADD_FAILURE() << "failed to seed all XREADGROUP entries";
            completed = true;
            co_return;
        }
        auto group_created = co_await redis.xgroup_create(key, group, "0", true);
        if (!group_created.ok()) {
            ADD_FAILURE() << "failed to create XREADGROUP group: " << group_created.error();
            completed = true;
            co_return;
        }

        auto read_reply = co_await redis.xreadgroup(key, group, consumer, ">", 10);
        if (!read_reply.ok()) {
            ADD_FAILURE() << read_reply.error();
            completed = true;
            co_return;
        }
        auto entries = decode_stream(read_reply.result(), key);
        if (!entries) {
            ADD_FAILURE() << "XREADGROUP reply has no decodable stream entry shape: " << read_reply.result().dump();
            completed = true;
            co_return;
        }
        EXPECT_EQ(*entries, (std::vector<LogicalStreamEntry>{
                                {added1.result().to_string(), {{"field", "value1"}}},
                                {added2.result().to_string(), {{"field", "value2"}}},
                            }));

        // A second ">" read returns nothing new (both already delivered to this consumer).
        auto again = co_await redis.xreadgroup(key, group, consumer, ">", 10);
        if (!again.ok()) {
            ADD_FAILURE() << again.error();
            completed = true;
            co_return;
        }
        auto no_new_entries = decode_stream(again.result(), key);
        if (!no_new_entries) {
            ADD_FAILURE() << "empty XREADGROUP reply has no decodable stream shape: " << again.result().dump();
            completed = true;
            co_return;
        }
        EXPECT_TRUE(no_new_entries->empty());

        CO_IGNORE(co_await redis.xgroup_destroy(key, group));
        CO_IGNORE(co_await redis.del(key));
        completed = true;
    });
    run_coro_test_until(completed);
}

// XACK: acks a delivered entry once (1), and an unknown id acks nothing (0).
TEST_P(StreamProtocolModesTest, XACK) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key      = protocol_key("ack");
        std::string group    = "ackgroup";
        std::string consumer = "ackconsumer";

        auto add_reply = co_await redis.xadd(key, {{"field", "value"}});
        EXPECT_TRUE(add_reply.ok()) << add_reply.error();
        EXPECT_TRUE((co_await redis.xgroup_create(key, group, "0", true)).ok());
        CO_IGNORE(co_await redis.xreadgroup(key, group, consumer, ">", 1));

        std::string msg_id = add_reply.result().to_string();

        auto ack_reply = co_await redis.xack(key, group, msg_id);
        EXPECT_TRUE(ack_reply.ok()) << ack_reply.error();
        EXPECT_EQ(ack_reply.result(), 1);

        // Acking the same id again — or an id never pending — returns 0.
        auto ack_again = co_await redis.xack(key, group, msg_id);
        EXPECT_TRUE(ack_again.ok()) << ack_again.error();
        EXPECT_EQ(ack_again.result(), 0);

        auto ack_unknown = co_await redis.xack(key, group, std::string("9999999-0"));
        EXPECT_TRUE(ack_unknown.ok()) << ack_unknown.error();
        EXPECT_EQ(ack_unknown.result(), 0);

        CO_IGNORE(co_await redis.xgroup_destroy(key, group));
        CO_IGNORE(co_await redis.del(key));
        completed = true;
    });
    run_coro_test_until(completed);
}

// XRANGE / XREVRANGE — entry counts, COUNT cap, and field payloads.
TEST_P(StreamProtocolModesTest, XRANGE_XREVRANGE) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("range");

        CO_IGNORE(co_await redis.xadd(key, {{"f1", "v1"}}));
        CO_IGNORE(co_await redis.xadd(key, {{"f2", "v2"}}));
        CO_IGNORE(co_await redis.xadd(key, {{"f3", "v3"}}));

        auto range_r = co_await redis.xrange(key, "-", "+");
        EXPECT_TRUE(range_r.ok()) << range_r.error();
        if (!(range_r.result().size() == 3u)) {
            ADD_FAILURE() << "precondition failed: range_r.result().size() == 3u";
            co_return;
        }
        // First entry carries the first field/value pair.
        EXPECT_EQ(range_r.result()[0].fields.at("f1"), "v1");
        EXPECT_EQ(range_r.result()[2].fields.at("f3"), "v3");

        auto range_count = co_await redis.xrange(key, "-", "+", 2);
        EXPECT_TRUE(range_count.ok()) << range_count.error();
        EXPECT_EQ(range_count.result().size(), 2u);

        auto rev_r = co_await redis.xrevrange(key, "+", "-");
        EXPECT_TRUE(rev_r.ok()) << rev_r.error();
        if (!(rev_r.result().size() == 3u)) {
            ADD_FAILURE() << "precondition failed: rev_r.result().size() == 3u";
            co_return;
        }
        // Reverse order → newest first.
        EXPECT_GE(rev_r.result()[0].id.timestamp, rev_r.result()[1].id.timestamp);
        EXPECT_EQ(rev_r.result()[0].fields.at("f3"), "v3");

        CO_IGNORE(co_await redis.del(key));
        completed = true;
    });
    run_coro_test_until(completed);
}

// XGROUP SETID + CREATECONSUMER.
TEST_P(StreamProtocolModesTest, XGROUP_SETID_CREATECONSUMER) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key   = protocol_key("xgroup");
        std::string group = "g1";

        auto add_r = co_await redis.xadd(key, {{"f", "v1"}});
        EXPECT_TRUE(add_r.ok()) << add_r.error();
        EXPECT_TRUE((co_await redis.xgroup_create(key, group, "0", true)).ok());

        auto setid_r = co_await redis.xgroupSetid(key, group, add_r.result().to_string());
        EXPECT_TRUE(setid_r.ok()) << setid_r.error();

        auto create_r = co_await redis.xgroupCreateconsumer(key, group, "c1");
        EXPECT_TRUE(create_r.ok()) << create_r.error();
        EXPECT_TRUE(create_r.result());

        // Re-creating the same consumer reports it was not newly created → false.
        auto create_again = co_await redis.xgroupCreateconsumer(key, group, "c1");
        EXPECT_TRUE(create_again.ok()) << create_again.error();
        EXPECT_FALSE(create_again.result());

        CO_IGNORE(co_await redis.xgroup_destroy(key, group));
        CO_IGNORE(co_await redis.del(key));
        completed = true;
    });
    run_coro_test_until(completed);
}

// XCLAIM / XAUTOCLAIM — message ownership transfer between consumers.
TEST_P(StreamProtocolModesTest, XCLAIM_XAUTOCLAIM) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key       = protocol_key("claim");
        std::string group     = "claimgroup";
        std::string consumer1 = "c1";
        std::string consumer2 = "c2";

        auto add1 = co_await redis.xadd(key, {{"field", "one"}});
        auto add2 = co_await redis.xadd(key, {{"field", "two"}});
        if (!(add1.ok() && add2.ok())) {
            ADD_FAILURE() << "failed to seed both claim entries";
            completed = true;
            co_return;
        }
        const std::string id1           = add1.result().to_string();
        const std::string id2           = add2.result().to_string();
        auto              group_created = co_await redis.xgroup_create(key, group, "0", true);
        if (!group_created.ok()) {
            ADD_FAILURE() << "failed to create claim group: " << group_created.error();
            completed = true;
            co_return;
        }

        auto delivered = co_await redis.xreadgroup(key, group, consumer1, ">", 2);
        if (!delivered.ok()) {
            ADD_FAILURE() << "failed to place both entries in consumer1's pending list: " << delivered.error();
            completed = true;
            co_return;
        }

        auto claim_r = co_await redis.xclaim(key, group, consumer2, 0, {id1});
        if (!claim_r.ok()) {
            ADD_FAILURE() << claim_r.error();
            completed = true;
            co_return;
        }
        if (!(claim_r.result().size() == 1u)) {
            ADD_FAILURE() << "precondition failed: claim_r.result().size() == 1u";
            completed = true;
            co_return;
        }
        EXPECT_EQ(claim_r.result()[0].id.to_string(), id1);
        const auto claimed_field = claim_r.result()[0].fields.find("field");
        if (claimed_field == claim_r.result()[0].fields.end()) {
            ADD_FAILURE() << "XCLAIM omitted the seeded field";
            completed = true;
            co_return;
        }
        EXPECT_EQ(claimed_field->second, "one");

        auto pending1 = co_await redis.xpending(key, group, "-", "+", 10, consumer1);
        auto pending2 = co_await redis.xpending(key, group, "-", "+", 10, consumer2);
        if (!(pending1.ok() && pending2.ok())) {
            ADD_FAILURE() << "failed to inspect pending ownership after XCLAIM";
            completed = true;
            co_return;
        }
        auto owners1 = decode_pending_owners(pending1.result());
        auto owners2 = decode_pending_owners(pending2.result());
        if (!(owners1 && owners2)) {
            ADD_FAILURE() << "XPENDING ownership reply has an unexpected shape";
            completed = true;
            co_return;
        }
        EXPECT_EQ(*owners1, (std::vector<std::pair<std::string, std::string>>{{id2, consumer1}}));
        EXPECT_EQ(*owners2, (std::vector<std::pair<std::string, std::string>>{{id1, consumer2}}));

        auto acked = co_await redis.xack(key, group, id1);
        if (!acked.ok()) {
            ADD_FAILURE() << acked.error();
            completed = true;
            co_return;
        }
        EXPECT_EQ(acked.result(), 1);

        auto autoclaim_r = co_await redis.xautoclaim(key, group, consumer2, 0, "0", 10);
        if (!autoclaim_r.ok()) {
            ADD_FAILURE() << autoclaim_r.error();
            completed = true;
            co_return;
        }
        if (!autoclaim_r.result().is_array() || autoclaim_r.result().size() < 2 || !autoclaim_r.result()[0].is_string()) {
            ADD_FAILURE() << "XAUTOCLAIM reply has an unexpected shape: " << autoclaim_r.result().dump();
            completed = true;
            co_return;
        }
        EXPECT_EQ(autoclaim_r.result()[0].get<std::string>(), "0-0");
        auto auto_entries = decode_entry_array(autoclaim_r.result()[1]);
        if (!auto_entries) {
            ADD_FAILURE() << "XAUTOCLAIM entries have an unexpected shape: " << autoclaim_r.result().dump();
            completed = true;
            co_return;
        }
        EXPECT_EQ(*auto_entries, (std::vector<LogicalStreamEntry>{{id2, {{"field", "two"}}}}));

        auto no_longer_c1 = co_await redis.xpending(key, group, "-", "+", 10, consumer1);
        auto now_c2       = co_await redis.xpending(key, group, "-", "+", 10, consumer2);
        if (!(no_longer_c1.ok() && now_c2.ok())) {
            ADD_FAILURE() << "failed to inspect pending ownership after XAUTOCLAIM";
            completed = true;
            co_return;
        }
        auto old_owner = decode_pending_owners(no_longer_c1.result());
        auto new_owner = decode_pending_owners(now_c2.result());
        if (!(old_owner && new_owner)) {
            ADD_FAILURE() << "XPENDING ownership reply has an unexpected shape after XAUTOCLAIM";
            completed = true;
            co_return;
        }
        EXPECT_TRUE(old_owner->empty());
        EXPECT_EQ(*new_owner, (std::vector<std::pair<std::string, std::string>>{{id2, consumer2}}));

        CO_IGNORE(co_await redis.xgroup_destroy(key, group));
        CO_IGNORE(co_await redis.del(key));
        completed = true;
    });
    run_coro_test_until(completed);
}

// JUSTID uses its own ID-only reply type and must not be sent through the entry decoder.
TEST_P(StreamProtocolModesTest, XCLAIM_JUSTID) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key   = protocol_key("claim-justid");
        std::string group = "claim-justid-group";

        auto added = co_await redis.xadd(key, {{"field", "value"}});
        EXPECT_TRUE(added.ok()) << added.error();
        if (!added) {
            completed = true;
            co_return;
        }
        std::string id = added.result().to_string();
        EXPECT_TRUE((co_await redis.xgroup_create(key, group, "0", true)).ok());
        EXPECT_TRUE((co_await redis.xreadgroup(key, group, "before", ">", 1)).ok());

        auto rejected = co_await redis.xclaim(key, group, "after", 0, {id}, {"justid"});
        EXPECT_FALSE(rejected.ok());
        EXPECT_EQ(rejected.error(), "XCLAIM JUSTID requires xclaim_justid");
        auto still_before = co_await redis.xpending(key, group, "-", "+", 10, std::string("before"));
        EXPECT_TRUE(still_before.ok()) << still_before.error();
        if (still_before) {
            EXPECT_TRUE(json_contains(still_before.result(), id));
        }
        auto not_after = co_await redis.xpending(key, group, "-", "+", 10, std::string("after"));
        EXPECT_TRUE(not_after.ok()) << not_after.error();
        if (not_after) {
            EXPECT_TRUE(not_after.result().empty());
        }

        int rejected_callbacks = 0;
        redis.xclaim(
            [&](qb::redis::Reply<qb::redis::stream_entry_list> &&reply) {
                ++rejected_callbacks;
                EXPECT_FALSE(reply.ok());
                EXPECT_EQ(reply.error(), "XCLAIM JUSTID requires xclaim_justid");
            },
            key, group, "after", 0, {id}, {"IDLE", "0", "JUSTID"});
        EXPECT_EQ(rejected_callbacks, 1);

        auto duplicate = co_await redis.xclaim_justid(key, group, "after", 0, {id}, {"JUSTID"});
        EXPECT_FALSE(duplicate.ok());
        EXPECT_EQ(duplicate.error(), "xclaim_justid adds JUSTID automatically");
        auto missing = co_await redis.xclaim(key, group, "after", 0, {"0-0"}, {"JUSTID"});
        EXPECT_FALSE(missing.ok());
        EXPECT_EQ(missing.error(), "XCLAIM JUSTID requires xclaim_justid");
        EXPECT_EQ(rejected_callbacks, 1);
        auto still_pending = co_await redis.xpending(key, group, "-", "+", 10, std::string("before"));
        EXPECT_TRUE(still_pending.ok()) << still_pending.error();
        if (still_pending) {
            EXPECT_TRUE(json_contains(still_pending.result(), id));
        }

        auto claimed = co_await redis.xclaim_justid(key, group, "after", 0, {id});
        EXPECT_TRUE(claimed.ok()) << claimed.error();
        if (claimed) {
            EXPECT_EQ(claimed.result(), std::vector<std::string>{id});
        }
        auto after = co_await redis.xpending(key, group, "-", "+", 10, std::string("after"));
        EXPECT_TRUE(after.ok()) << after.error();
        if (after) {
            EXPECT_TRUE(json_contains(after.result(), id));
        }

        auto empty = co_await redis.xclaim_justid(key, group, "after", 0, {"0-0"});
        EXPECT_TRUE(empty.ok()) << empty.error();
        if (empty) {
            EXPECT_TRUE(empty.result().empty());
        }

        auto fields = co_await redis.xclaim(key, group, "with-fields", 0, {id}, {"IDLE", "0", "RETRYCOUNT", "2"});
        EXPECT_TRUE(fields.ok()) << fields.error();
        if (fields && fields.result().size() == 1u) {
            EXPECT_EQ(fields.result()[0].id.to_string(), id);
            EXPECT_EQ(fields.result()[0].fields.at("field"), "value");
        } else {
            ADD_FAILURE() << "XCLAIM with field-bearing options returned no entry";
        }

        auto second = co_await redis.xadd(key, {{"field", "callback"}});
        EXPECT_TRUE(second.ok()) << second.error();
        if (!second) {
            completed = true;
            co_return;
        }
        std::string second_id = second.result().to_string();
        EXPECT_TRUE((co_await redis.xreadgroup(key, group, "before", ">", 1)).ok());
        int callback_count = 0;
        redis.xclaim_justid(
            [&](qb::redis::Reply<std::vector<std::string>> &&reply) {
                ++callback_count;
                EXPECT_TRUE(reply.ok()) << reply.error();
                if (reply) {
                    EXPECT_EQ(reply.result(), std::vector<std::string>{second_id});
                }
            },
            key, group, "callback-owner", 0, {second_id}, {"IDLE", "0"});
        EXPECT_TRUE((co_await redis.ping()).ok());
        EXPECT_EQ(callback_count, 1);
        auto callback_owner = co_await redis.xpending(key, group, "-", "+", 10, std::string("callback-owner"));
        EXPECT_TRUE(callback_owner.ok()) << callback_owner.error();
        if (callback_owner) {
            EXPECT_TRUE(json_contains(callback_owner.result(), second_id));
        }
        EXPECT_EQ(callback_count, 1);

        completed = true;
    });
    run_coro_test_until(completed);
}

// XINFO STREAM / GROUPS / CONSUMERS / HELP — concrete fields.
TEST_P(StreamProtocolModesTest, XINFO) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key   = protocol_key("info");
        std::string group = "infogroup";

        CO_IGNORE(co_await redis.xadd(key, {{"field", "value1"}}));
        CO_IGNORE(co_await redis.xadd(key, {{"field", "value2"}}));
        CO_IGNORE(co_await redis.xadd(key, {{"field", "value3"}}));
        EXPECT_TRUE((co_await redis.xgroup_create(key, group, "0", true)).ok());

        auto info_reply = co_await redis.xinfo_stream(key);
        EXPECT_TRUE(info_reply.ok()) << info_reply.error();
        // XINFO STREAM is a flat field/value map → reconstructed as a JSON object.
        const auto &info = info_reply.result();
        EXPECT_TRUE(info.is_object());
        EXPECT_TRUE(info.contains("length"));
        EXPECT_EQ(info["length"], 3);
        EXPECT_TRUE(info.contains("last-generated-id"));

        auto groups_reply = co_await redis.xinfo_groups(key);
        EXPECT_TRUE(groups_reply.ok()) << groups_reply.error();
        EXPECT_TRUE(groups_reply.result().is_array());
        if (!(groups_reply.result().size() == 1u)) {
            ADD_FAILURE() << "precondition failed: groups_reply.result().size() == 1u";
            co_return;
        }
        EXPECT_TRUE(json_contains(groups_reply.result(), group));

        auto consumers_reply = co_await redis.xinfo_consumers(key, group);
        EXPECT_TRUE(consumers_reply.ok()) << consumers_reply.error();
        EXPECT_TRUE(consumers_reply.result().is_array()); // no consumers yet → empty array

        auto help_reply = co_await redis.xinfo_help();
        EXPECT_TRUE(help_reply.ok()) << help_reply.error();
        EXPECT_TRUE(help_reply.result().is_array());
        EXPECT_FALSE(help_reply.result().empty());

        CO_IGNORE(co_await redis.xgroup_destroy(key, group));
        CO_IGNORE(co_await redis.del(key));
        completed = true;
    });
    run_coro_test_until(completed);
}

// XPENDING detail: range form lists the pending entry; consumer filter narrows it.
TEST_P(StreamProtocolModesTest, XPENDING) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key      = protocol_key("pending");
        std::string group    = "pendinggroup";
        std::string consumer = "c1";

        auto add_r = co_await redis.xadd(key, {{"f", "v1"}});
        EXPECT_TRUE(add_r.ok()) << add_r.error();
        std::string id = add_r.result().to_string();
        EXPECT_TRUE((co_await redis.xgroup_create(key, group, "0", true)).ok());
        CO_IGNORE(co_await redis.xreadgroup(key, group, consumer, ">", 1));

        // Detail form (start, end, count) → an array with one pending entry referencing
        // the delivered id and its owning consumer.
        auto detail = co_await redis.xpending(key, group, "-", "+", 10);
        EXPECT_TRUE(detail.ok()) << detail.error();
        EXPECT_TRUE(detail.result().is_array());
        if (!(detail.result().size() == 1u)) {
            ADD_FAILURE() << "precondition failed: detail.result().size() == 1u";
            co_return;
        }
        EXPECT_TRUE(json_contains(detail.result(), id));
        EXPECT_TRUE(json_contains(detail.result(), consumer));

        // Filtering by a different consumer yields no pending entries for it.
        auto other = co_await redis.xpending(key, group, "-", "+", 10, std::string("other-consumer"));
        EXPECT_TRUE(other.ok()) << other.error();
        EXPECT_TRUE(other.result().is_array());
        EXPECT_TRUE(other.result().empty());

        CO_IGNORE(co_await redis.xgroup_destroy(key, group));
        CO_IGNORE(co_await redis.del(key));
        completed = true;
    });
    run_coro_test_until(completed);
}
