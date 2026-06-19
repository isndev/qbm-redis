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

class BitmapProtocolModesTest : public ProtocolModesTestBase {};

INSTANTIATE_PROTOCOL_MODES(BitmapProtocolModesTest);

/*
 * COROUTINE TESTS
 */

// Test BITCOUNT with coroutines
TEST_P(BitmapProtocolModesTest, CORO_BITMAP_COMMANDS_BITCOUNT) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("coro_bitcount");

        // Set a string with bits
        (void) co_await redis.set(key, std::string("\xFF\x00\xFF", 3));

        // Count all bits
        auto reply = co_await redis.bitcount(key);
        EXPECT_TRUE(reply.ok());
        EXPECT_EQ(reply.result(), 16);

        // Test with a specific range
        auto reply1 = co_await redis.bitcount(key, 0, 0);
        EXPECT_TRUE(reply1.ok());
        EXPECT_EQ(reply1.result(), 8);

        auto reply2 = co_await redis.bitcount(key, 1, 1);
        EXPECT_TRUE(reply2.ok());
        EXPECT_EQ(reply2.result(), 0);

        auto reply3 = co_await redis.bitcount(key, 2, 2);
        EXPECT_TRUE(reply3.ok());
        EXPECT_EQ(reply3.result(), 8);

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test BITFIELD with coroutines
TEST_P(BitmapProtocolModesTest, CORO_BITMAP_COMMANDS_BITFIELD) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("coro_bitfield");

        // Test SET and GET
        std::vector<std::string> operations = {"SET", "u4", "0", "100", "GET", "u4", "0"};

        auto reply = co_await redis.bitfield(key, operations);
        EXPECT_TRUE(reply.ok());
        auto results = reply.result();
        EXPECT_EQ(results.size(), 2);
        EXPECT_TRUE(results[0].has_value());
        EXPECT_TRUE(results[1].has_value());
        EXPECT_EQ(results[0].value(), 0);
        EXPECT_EQ(results[1].value(), 4);

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test BITOP with coroutines
TEST_P(BitmapProtocolModesTest, CORO_BITMAP_COMMANDS_BITOP) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key1    = protocol_key("coro_bitop1");
        std::string key2    = protocol_key("coro_bitop2");
        std::string destkey = protocol_key("coro_bitop_dest");

        // Prepare test strings
        (void) co_await redis.set(key1, std::string("\xFF\x00\xFF", 3));
        (void) co_await redis.set(key2, "\x0F\xF0");

        // Test AND
        auto reply_and = co_await redis.bitop("AND", destkey, std::vector<std::string>{key1, key2});
        EXPECT_TRUE(reply_and.ok());
        long long len = reply_and.result();
        EXPECT_TRUE(len == 2 || len == 3);

        auto result_and = co_await redis.get(destkey);
        EXPECT_TRUE(result_and.ok());
        EXPECT_TRUE(result_and.result().has_value());
        EXPECT_EQ(result_and.result()->substr(0, 2), std::string("\x0F\x00", 2));

        // Test OR
        auto reply_or = co_await redis.bitop("OR", destkey, std::vector<std::string>{key1, key2});
        EXPECT_TRUE(reply_or.ok());
        len = reply_or.result();
        EXPECT_TRUE(len == 2 || len == 3);

        auto result_or = co_await redis.get(destkey);
        EXPECT_TRUE(result_or.ok());
        EXPECT_TRUE(result_or.result().has_value());
        EXPECT_EQ(result_or.result()->substr(0, 2), std::string("\xFF\xF0", 2));

        // Test XOR
        auto reply_xor = co_await redis.bitop("XOR", destkey, std::vector<std::string>{key1, key2});
        EXPECT_TRUE(reply_xor.ok());
        len = reply_xor.result();
        EXPECT_TRUE(len == 2 || len == 3);

        auto result_xor = co_await redis.get(destkey);
        EXPECT_TRUE(result_xor.ok());
        EXPECT_TRUE(result_xor.result().has_value());
        EXPECT_EQ(result_xor.result()->substr(0, 2), std::string("\xF0\xF0", 2));

        // Test NOT
        auto reply_not = co_await redis.bitop("NOT", destkey, std::vector<std::string>{key1});
        EXPECT_TRUE(reply_not.ok());
        len = reply_not.result();
        EXPECT_TRUE(len == 2 || len == 3);

        auto result_not = co_await redis.get(destkey);
        EXPECT_TRUE(result_not.ok());
        EXPECT_TRUE(result_not.result().has_value());
        EXPECT_EQ(result_not.result()->substr(0, 2), std::string("\x00\xFF", 2));

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test BITPOS with coroutines
TEST_P(BitmapProtocolModesTest, CORO_BITMAP_COMMANDS_BITPOS) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("coro_bitpos");

        // Set a string with bits
        (void) co_await redis.set(key, std::string("\xFF\x00\xFF", 3));

        // Find the first bit set to 1
        auto reply1 = co_await redis.bitpos(key, true);
        EXPECT_TRUE(reply1.ok());
        EXPECT_EQ(reply1.result(), 0);

        // Find the first bit set to 0
        auto reply2 = co_await redis.bitpos(key, false);
        EXPECT_TRUE(reply2.ok());
        EXPECT_EQ(reply2.result(), 8);

        // Test with specific ranges
        auto reply3 = co_await redis.bitpos(key, true, 0, 0);
        EXPECT_TRUE(reply3.ok());
        EXPECT_TRUE(reply3.result() == -1 || reply3.result() == 0);

        auto reply4 = co_await redis.bitpos(key, true, 1, 1);
        EXPECT_TRUE(reply4.ok());
        EXPECT_TRUE(reply4.result() == 8 || reply4.result() == -1);

        auto reply5 = co_await redis.bitpos(key, true, 2, 2);
        EXPECT_TRUE(reply5.ok());
        EXPECT_TRUE(reply5.result() == -1 || reply5.result() == 16);

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test BITFIELD_RO (read-only BITFIELD)
TEST_P(BitmapProtocolModesTest, CORO_BITMAP_COMMANDS_BITFIELD_RO) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("coro_bitfield_ro");

        // Set value with BITFIELD first
        (void) co_await redis.bitfield(key, {"SET", "u8", "0", "42", "GET", "u8", "0"});

        // BITFIELD_RO: read-only GET
        auto reply = co_await redis.bitfieldRo(key, {"GET", "u8", "0"});
        EXPECT_TRUE(reply.ok());
        EXPECT_EQ(reply.result().size(), 1u);
        EXPECT_TRUE(reply.result()[0].has_value());
        EXPECT_EQ(reply.result()[0].value(), 42);

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);
}

// Test GETBIT and SETBIT with coroutines
TEST_P(BitmapProtocolModesTest, CORO_BITMAP_COMMANDS_GETBIT_SETBIT) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("coro_getbit_setbit");

        // Test SETBIT
        auto reply1 = co_await redis.setbit(key, 7, true);
        EXPECT_TRUE(reply1.ok());
        EXPECT_EQ(reply1.result(), 0); // Old value was 0

        auto reply2 = co_await redis.setbit(key, 7, false);
        EXPECT_TRUE(reply2.ok());
        EXPECT_EQ(reply2.result(), 1); // Old value was 1

        auto reply3 = co_await redis.setbit(key, 7, true);
        EXPECT_TRUE(reply3.ok());
        EXPECT_EQ(reply3.result(), 0); // Old value was 0

        // Test GETBIT
        auto reply4 = co_await redis.getbit(key, 0);
        EXPECT_TRUE(reply4.ok());
        EXPECT_EQ(reply4.result(), 0);

        auto reply5 = co_await redis.getbit(key, 7);
        EXPECT_TRUE(reply5.ok());
        EXPECT_EQ(reply5.result(), 1);

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

TEST_P(BitmapProtocolModesTest, SETBIT_GETBIT) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k = protocol_key("bitmap");
        (void) co_await redis.setbit(k, 7, true);
        auto get_r = co_await redis.getbit(k, 7);
        EXPECT_TRUE(get_r.ok()) << get_r.error();
        if (get_r.ok())
            EXPECT_EQ(get_r.result(), 1);
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(BitmapProtocolModesTest, BITFIELD_INTEGER) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k = protocol_key("bitfield");
        auto r = co_await redis.bitfield(k, {"SET", "i32", "#0", "100"});
        EXPECT_TRUE(r.ok()) << r.error();
        if (r.ok())
            EXPECT_FALSE(r.result().empty());
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(BitmapProtocolModesTest, BITFIELD_RO) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k = protocol_key("bitfield_ro");
        (void) co_await redis.bitfield(k, {"SET", "u8", "0", "100"});
        auto r = co_await redis.bitfieldRo(k, {"GET", "u8", "0"});
        EXPECT_TRUE(r.ok()) << r.error();
        if (r.ok() && !r.result().empty() && r.result()[0]) {
            EXPECT_EQ(r.result()[0].value(), 100);
        }
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(BitmapProtocolModesTest, BITCOUNT_INTEGER) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k = protocol_key("bitcount");
        (void) co_await redis.setbit(k, 0, true);
        (void) co_await redis.setbit(k, 1, true);
        (void) co_await redis.setbit(k, 2, true);
        auto r = co_await redis.bitcount(k);
        EXPECT_TRUE(r.ok()) << r.error();
        if (r.ok())
            EXPECT_EQ(r.result(), 3);
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

// Test async BITCOUNT
// Test async BITFIELD
// Test async GETBIT and SETBIT