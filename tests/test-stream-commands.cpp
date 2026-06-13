/*
 * qb - C++ Actor Framework
 * Copyright (C) 2011-2025 isndev (cpp.actor). All rights reserved.
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

#include <gtest/gtest.h>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <thread>
#include "../redis.h"
#include "protocol_test_common.h"

using namespace qb::io;
using namespace std::chrono;

// ============================================================================
// Fixture: all tests run in both RESP2 and RESP3
// ============================================================================

class StreamProtocolModesTest : public ProtocolModesTestBase {};

INSTANTIATE_PROTOCOL_MODES(StreamProtocolModesTest);

// Test basic XADD and XLEN operations
TEST_P(StreamProtocolModesTest, CORO_STREAM_COMMANDS_XADD_XLEN) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("basic");

        // XADD test
        std::vector<std::pair<std::string, std::string>> entries = {
            {"field1", "value1"}, {"field2", "value2"}};
        auto add_reply = co_await redis.xadd(key, entries);
        EXPECT_TRUE(add_reply.ok());
        EXPECT_GT(add_reply.result().timestamp, 0);

        // XLEN test
        auto len_reply = co_await redis.xlen(key);
        EXPECT_TRUE(len_reply.ok());
        EXPECT_EQ(len_reply.result(), 1);

        // Add more entries
        (void)co_await redis.xadd(key, {{"field3", "value3"}});
        (void)co_await redis.xadd(key, {{"field4", "value4"}});

        auto len2_reply = co_await redis.xlen(key);
        EXPECT_EQ(len2_reply.result(), 3);

        // Cleanup
        (void)co_await redis.del(key);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test XDEL operation
TEST_P(StreamProtocolModesTest, CORO_STREAM_COMMANDS_XDEL) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("delete");

        // Setup stream
        std::vector<std::pair<std::string, std::string>> entries = {
            {"field", "value1"}};
        auto add_reply = co_await redis.xadd(key, entries);
        std::string id1 = std::to_string(add_reply.result().timestamp) + "-" + 
                          std::to_string(add_reply.result().sequence);

        entries = {{"field", "value2"}};
        auto add_reply2 = co_await redis.xadd(key, entries);
        std::string id2 = std::to_string(add_reply2.result().timestamp) + "-" + 
                          std::to_string(add_reply2.result().sequence);

        // Verify count
        auto len_before = co_await redis.xlen(key);
        EXPECT_EQ(len_before.result(), 2);

        // XDEL test
        auto del_reply = co_await redis.xdel(key, id1);
        EXPECT_TRUE(del_reply.ok());
        EXPECT_EQ(del_reply.result(), 1);

        auto len_after = co_await redis.xlen(key);
        EXPECT_EQ(len_after.result(), 1);

        // Cleanup
        (void)co_await redis.del(key);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test XGROUP_CREATE and XGROUP_DESTROY
TEST_P(StreamProtocolModesTest, CORO_STREAM_COMMANDS_XGROUP) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("group");
        std::string group = "mygroup";

        // Setup stream
        (void)co_await redis.xadd(key, {{"field", "value"}});

        // XGROUP_CREATE test
        auto create_reply = co_await redis.xgroup_create(key, group, "0", true);
        EXPECT_TRUE(create_reply.ok());
        EXPECT_TRUE(create_reply.result().ok());

        // XGROUP_DESTROY test
        auto destroy_reply = co_await redis.xgroup_destroy(key, group);
        EXPECT_TRUE(destroy_reply.ok());
        EXPECT_EQ(destroy_reply.result(), 1);

        // Cleanup
        (void)co_await redis.del(key);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test XTRIM operation
TEST_P(StreamProtocolModesTest, CORO_STREAM_COMMANDS_XTRIM) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("trim");

        // Add many entries
        for (int i = 0; i < 10; ++i) {
            (void)co_await redis.xadd(key, {{"field", std::to_string(i)}});
        }

        auto len_before = co_await redis.xlen(key);
        EXPECT_EQ(len_before.result(), 10);

        // XTRIM test - trim to 5 entries
        auto trim_reply = co_await redis.xtrim(key, 5);
        EXPECT_TRUE(trim_reply.ok());
        EXPECT_GE(trim_reply.result(), 5); // At least 5 entries removed

        auto len_after = co_await redis.xlen(key);
        EXPECT_LE(len_after.result(), 5); // At most 5 entries remaining

        // Cleanup
        (void)co_await redis.del(key);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test XREAD operation
TEST_P(StreamProtocolModesTest, CORO_STREAM_COMMANDS_XREAD) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("read");

        // Setup stream with entries
        (void)co_await redis.xadd(key, {{"field1", "value1"}});
        (void)co_await redis.xadd(key, {{"field2", "value2"}});
        (void)co_await redis.xadd(key, {{"field3", "value3"}});

        // XREAD test - read from beginning
        auto read_reply = co_await redis.xread(key, "0", 10);
        EXPECT_TRUE(read_reply.ok());
        // Result should contain the stream entries

        // Cleanup
        (void)co_await redis.del(key);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test XREADGROUP operation
TEST_P(StreamProtocolModesTest, CORO_STREAM_COMMANDS_XREADGROUP) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("readgroup");
        std::string group = "testgroup";
        std::string consumer = "testconsumer";

        // Setup stream and group
        (void)co_await redis.xadd(key, {{"field", "value1"}});
        (void)co_await redis.xadd(key, {{"field", "value2"}});
        (void)co_await redis.xgroup_create(key, group, "0", true);

        // XREADGROUP test
        auto read_reply = co_await redis.xreadgroup(key, group, consumer, ">", 10);
        EXPECT_TRUE(read_reply.ok());

        // Cleanup
        (void)co_await redis.xgroup_destroy(key, group);
        (void)co_await redis.del(key);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test XACK operation
TEST_P(StreamProtocolModesTest, CORO_STREAM_COMMANDS_XACK) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("ack");
        std::string group = "ackgroup";
        std::string consumer = "ackconsumer";

        // Setup
        auto add_reply = co_await redis.xadd(key, {{"field", "value"}});
        (void)co_await redis.xgroup_create(key, group, "0", true);

        // Read message
        auto read_reply = co_await redis.xreadgroup(key, group, consumer, ">", 1);
        
        // Get message ID for acknowledgement
        std::string msg_id = std::to_string(add_reply.result().timestamp) + "-" + 
                             std::to_string(add_reply.result().sequence);

        // XACK test
        auto ack_reply = co_await redis.xack(key, group, msg_id);
        EXPECT_TRUE(ack_reply.ok());
        EXPECT_EQ(ack_reply.result(), 1);

        // Cleanup
        (void)co_await redis.xgroup_destroy(key, group);
        (void)co_await redis.del(key);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test XRANGE and XREVRANGE
TEST_P(StreamProtocolModesTest, CORO_STREAM_COMMANDS_XRANGE_XREVRANGE) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("range");

        (void)co_await redis.xadd(key, {{"f1", "v1"}});
        (void)co_await redis.xadd(key, {{"f2", "v2"}});
        (void)co_await redis.xadd(key, {{"f3", "v3"}});

        // XRANGE: full range
        auto range_r = co_await redis.xrange(key, "-", "+");
        EXPECT_TRUE(range_r.ok());
        EXPECT_EQ(range_r.result().size(), 3u);

        // XRANGE with COUNT
        auto range_count = co_await redis.xrange(key, "-", "+", 2);
        EXPECT_TRUE(range_count.ok());
        EXPECT_EQ(range_count.result().size(), 2u);

        // XREVRANGE: reverse order
        auto rev_r = co_await redis.xrevrange(key, "+", "-");
        EXPECT_TRUE(rev_r.ok());
        EXPECT_EQ(rev_r.result().size(), 3u);
        if (rev_r.result().size() >= 2) {
            EXPECT_GE(rev_r.result()[0].id.timestamp, rev_r.result()[1].id.timestamp);
        }

        (void)co_await redis.del(key);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) qb::io::async::run(EVRUN_NOWAIT);
}

// Test XGROUP SETID and XGROUP CREATECONSUMER
TEST_P(StreamProtocolModesTest, CORO_STREAM_COMMANDS_XGROUP_SETID_CREATECONSUMER) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("xgroup");
        std::string group = "g1";

        auto add_r = co_await redis.xadd(key, {{"f", "v1"}});
        (void)co_await redis.xgroup_create(key, group, "0", true);

        // XGROUP SETID
        std::string new_id = std::to_string(add_r.result().timestamp) + "-" +
                             std::to_string(add_r.result().sequence);
        auto setid_r = co_await redis.xgroupSetid(key, group, new_id);
        EXPECT_TRUE(setid_r.ok());

        // XGROUP CREATECONSUMER
        auto create_r = co_await redis.xgroupCreateconsumer(key, group, "c1");
        EXPECT_TRUE(create_r.ok());
        EXPECT_TRUE(create_r.result());

        (void)co_await redis.xgroup_destroy(key, group);
        (void)co_await redis.del(key);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) qb::io::async::run(EVRUN_NOWAIT);
}

// Test XCLAIM and XAUTOCLAIM
TEST_P(StreamProtocolModesTest, CORO_STREAM_COMMANDS_XCLAIM_XAUTOCLAIM) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("claim");
        std::string group = "claimgroup";
        std::string consumer1 = "c1";
        std::string consumer2 = "c2";

        auto add_r = co_await redis.xadd(key, {{"field", "value"}});
        std::string msg_id = std::to_string(add_r.result().timestamp) + "-" +
                             std::to_string(add_r.result().sequence);
        (void)co_await redis.xgroup_create(key, group, "0", true);

        // Read with consumer1 (creates consumer, message goes to PEL)
        (void)co_await redis.xreadgroup(key, group, consumer1, ">", 1);

        // XCLAIM: consumer2 claims the message from consumer1 (min_idle 0)
        auto claim_r = co_await redis.xclaim(key, group, consumer2, 0, {msg_id});
        EXPECT_TRUE(claim_r.ok());
        EXPECT_EQ(claim_r.result().size(), 1u);

        // Setup for XAUTOCLAIM: add another message, read with c1, then autoclaim
        (void)co_await redis.xadd(key, {{"f2", "v2"}});
        (void)co_await redis.xreadgroup(key, group, consumer1, ">", 1);
        auto autoclaim_r = co_await redis.xautoclaim(key, group, consumer2, 0, "0", 10);
        EXPECT_TRUE(autoclaim_r.ok());

        (void)co_await redis.xgroup_destroy(key, group);
        (void)co_await redis.del(key);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) qb::io::async::run(EVRUN_NOWAIT);
}

// Test XINFO_STREAM, XINFO GROUPS, XINFO CONSUMERS, XINFO HELP
TEST_P(StreamProtocolModesTest, CORO_STREAM_COMMANDS_XINFO) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("info");
        std::string group = "infogroup";

        // Setup stream and group
        (void)co_await redis.xadd(key, {{"field", "value1"}});
        (void)co_await redis.xadd(key, {{"field", "value2"}});
        (void)co_await redis.xadd(key, {{"field", "value3"}});
        (void)co_await redis.xgroup_create(key, group, "0", true);

        // XINFO STREAM
        auto info_reply = co_await redis.xinfo_stream(key);
        EXPECT_TRUE(info_reply.ok());

        // XINFO GROUPS
        auto groups_reply = co_await redis.xinfo_groups(key);
        EXPECT_TRUE(groups_reply.ok());
        EXPECT_TRUE(groups_reply.result().is_array());

        // XINFO CONSUMERS (group may have no consumers yet)
        auto consumers_reply = co_await redis.xinfo_consumers(key, group);
        EXPECT_TRUE(consumers_reply.ok());
        EXPECT_TRUE(consumers_reply.result().is_array());

        // XINFO HELP
        auto help_reply = co_await redis.xinfo_help();
        EXPECT_TRUE(help_reply.ok());
        EXPECT_TRUE(help_reply.result().is_array());

        (void)co_await redis.xgroup_destroy(key, group);
        (void)co_await redis.del(key);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test XPENDING (range form with start, end, count)
TEST_P(StreamProtocolModesTest, CORO_STREAM_COMMANDS_XPENDING) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("pending");
        std::string group = "pendinggroup";
        std::string consumer = "c1";

        (void)co_await redis.xadd(key, {{"f", "v1"}});
        (void)co_await redis.xgroup_create(key, group, "0", true);
        (void)co_await redis.xreadgroup(key, group, consumer, ">", 1);

        // XPENDING with range
        auto r = co_await redis.xpending(key, group, "-", "+", 10);
        EXPECT_TRUE(r.ok());
        EXPECT_TRUE(r.result().is_array() || r.result().is_object());

        (void)co_await redis.xgroup_destroy(key, group);
        (void)co_await redis.del(key);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(StreamProtocolModesTest, XADD_XLEN) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k = protocol_key("stream");
        std::vector<std::pair<std::string, std::string>> entries{{"name", "alice"}, {"age", "30"}};
        auto add_r = co_await redis.xadd(k, entries);
        EXPECT_TRUE(add_r.ok()) << add_r.error();
        auto len_r = co_await redis.xlen(k);
        EXPECT_TRUE(len_r.ok()) << len_r.error();
        if (len_r.ok()) EXPECT_EQ(len_r.result(), 1);
        done = true;
    });
    while (!done) qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(StreamProtocolModesTest, XREAD_JSON) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k = protocol_key("xread");
        (void)co_await redis.xadd(k, {{"f", "v1"}});
        (void)co_await redis.xadd(k, {{"f", "v2"}});
        auto r = co_await redis.xread(k, "0", 10);
        EXPECT_TRUE(r.ok()) << r.error();
        if (r.ok()) EXPECT_TRUE(r.result().is_object() || r.result().is_array());
        done = true;
    });
    while (!done) qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(StreamProtocolModesTest, XRANGE_XREVRANGE) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k = protocol_key("xrange");
        (void)co_await redis.xadd(k, {{"f", "v1"}});
        (void)co_await redis.xadd(k, {{"f", "v2"}});
        auto range_r = co_await redis.xrange(k, "-", "+", 10);
        EXPECT_TRUE(range_r.ok()) << range_r.error();
        if (range_r.ok()) EXPECT_GE(range_r.result().size(), 1u);
        auto rev_r = co_await redis.xrevrange(k, "+", "-", 10);
        EXPECT_TRUE(rev_r.ok()) << rev_r.error();
        if (rev_r.ok()) EXPECT_GE(rev_r.result().size(), 1u);
        done = true;
    });
    while (!done) qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(StreamProtocolModesTest, XDEL_INTEGER) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k = protocol_key("xdel");
        auto add_r = co_await redis.xadd(k, {{"f", "v"}});
        EXPECT_TRUE(add_r.ok()) << add_r.error();
        std::string id = std::to_string(add_r.result().timestamp) + "-" +
                         std::to_string(add_r.result().sequence);
        auto r = co_await redis.xdel(k, id);
        EXPECT_TRUE(r.ok()) << r.error();
        if (r.ok()) EXPECT_EQ(r.result(), 1);
        done = true;
    });
    while (!done) qb::io::async::run(EVRUN_NOWAIT);
}

// Main function to run the tests
int
main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
