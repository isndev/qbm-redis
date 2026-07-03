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
 * @file integration/bitmap/bitmap-commands.cpp
 * @brief Live-redis integration tests for the SETBIT/GETBIT/BITCOUNT/BITPOS/BITOP/BITFIELD
 *        command family, exercised in both RESP2 and RESP3.
 *
 * Restructured from the legacy `test-bitmap-commands.cpp`:
 *  - dropped the 4 terse smoke dups (SETBIT_GETBIT / BITFIELD_INTEGER / BITFIELD_RO /
 *    BITCOUNT_INTEGER) — all strict subsets of the CORO_* bodies kept here;
 *  - BITOP length asserts tightened from the tolerant `len == 2 || len == 3` to the exact
 *    result length, after fixing the second operand `set(key2, "\x0F\xF0")` to the explicit
 *    `std::string("\x0F\xF0", 2)` form (string-literal ctor stops at the first NUL otherwise);
 *  - BITPOS range asserts tightened from disjunctions (`== -1 || == N`) to deterministic
 *    positions;
 *  - dead trailing `// Test async …` comment stubs removed.
 */

#include <gtest/gtest.h>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include "../../shared/redis_integration_fixture.h"
#include "../redis.h"

using namespace qb::redis::test;

namespace {

class BitmapProtocolModesTest : public ProtocolModesTestBase {};

INSTANTIATE_PROTOCOL_MODES(BitmapProtocolModesTest);

// BITCOUNT: whole-string and per-byte ranges over "\xFF\x00\xFF".
TEST_P(BitmapProtocolModesTest, BITCOUNT) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("bitcount");

        auto set_r = co_await redis.set(key, std::string("\xFF\x00\xFF", 3));
        EXPECT_TRUE(set_r.ok()) << set_r.error();

        auto all = co_await redis.bitcount(key);
        EXPECT_TRUE(all.ok()) << all.error();
        EXPECT_EQ(all.result(), 16); // two 0xFF bytes

        auto b0 = co_await redis.bitcount(key, 0, 0);
        EXPECT_TRUE(b0.ok()) << b0.error();
        EXPECT_EQ(b0.result(), 8);

        auto b1 = co_await redis.bitcount(key, 1, 1);
        EXPECT_TRUE(b1.ok()) << b1.error();
        EXPECT_EQ(b1.result(), 0);

        auto b2 = co_await redis.bitcount(key, 2, 2);
        EXPECT_TRUE(b2.ok()) << b2.error();
        EXPECT_EQ(b2.result(), 8);

        completed = true;
    });
    run_coro_test_until(completed);
}

// BITFIELD: SET then GET returns previous value (0) and the new value (4 = 0b0100, u4 of 100).
TEST_P(BitmapProtocolModesTest, BITFIELD) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("bitfield");

        auto r = co_await redis.bitfield(key, {"SET", "u4", "0", "100", "GET", "u4", "0"});
        EXPECT_TRUE(r.ok()) << r.error();
        const auto &res = r.result();
        if (!(res.size() == 2u)) {
            ADD_FAILURE() << "precondition failed: res.size() == 2u";
            co_return;
        }
        if (!(res[0].has_value())) {
            ADD_FAILURE() << "precondition failed: res[0].has_value()";
            co_return;
        }
        if (!(res[1].has_value())) {
            ADD_FAILURE() << "precondition failed: res[1].has_value()";
            co_return;
        }
        EXPECT_EQ(res[0].value(), 0); // previous value
        EXPECT_EQ(res[1].value(), 4); // 100 mod 16 (u4 wraps)

        completed = true;
    });
    run_coro_test_until(completed);
}

// BITFIELD_RO: read-only GET of a value previously written with BITFIELD.
TEST_P(BitmapProtocolModesTest, BITFIELD_RO) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("bitfield_ro");

        auto seed = co_await redis.bitfield(key, {"SET", "u8", "0", "42", "GET", "u8", "0"});
        EXPECT_TRUE(seed.ok()) << seed.error();

        auto r = co_await redis.bitfieldRo(key, {"GET", "u8", "0"});
        EXPECT_TRUE(r.ok()) << r.error();
        if (!(r.result().size() == 1u)) {
            ADD_FAILURE() << "precondition failed: r.result().size() == 1u";
            co_return;
        }
        if (!(r.result()[0].has_value())) {
            ADD_FAILURE() << "precondition failed: r.result()[0].has_value()";
            co_return;
        }
        EXPECT_EQ(r.result()[0].value(), 42);

        completed = true;
    });
    run_coro_test_until(completed);
}

// BITOP AND/OR/XOR/NOT — exact result length and the resulting bytes.
// key1 = "\xFF\x00\xFF" (3 bytes), key2 = "\x0F\xF0" (2 bytes).
// Redis right-pads the shorter operand with NUL bytes, so every binary BITOP result is 3 bytes.
TEST_P(BitmapProtocolModesTest, BITOP) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key1    = protocol_key("bitop1");
        const std::string key2    = protocol_key("bitop2");
        const std::string destkey = protocol_key("bitop_dest");

        auto s1 = co_await redis.set(key1, std::string("\xFF\x00\xFF", 3));
        EXPECT_TRUE(s1.ok()) << s1.error();
        // Explicit length-2 ctor: the bare "\x0F\xF0" literal would terminate at the first byte.
        auto s2 = co_await redis.set(key2, std::string("\x0F\xF0", 2));
        EXPECT_TRUE(s2.ok()) << s2.error();

        // AND: 0xFF&0x0F=0x0F, 0x00&0xF0=0x00, 0xFF&0x00(pad)=0x00 → "\x0F\x00\x00".
        auto r_and = co_await redis.bitop("AND", destkey, std::vector<std::string>{key1, key2});
        EXPECT_TRUE(r_and.ok()) << r_and.error();
        EXPECT_EQ(r_and.result(), 3);
        auto get_and = co_await redis.get(destkey);
        if (!(get_and.ok() && get_and.result().has_value())) {
            ADD_FAILURE() << "precondition failed: get_and.ok() && get_and.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*get_and.result(), std::string("\x0F\x00\x00", 3));

        // OR: 0xFF|0x0F=0xFF, 0x00|0xF0=0xF0, 0xFF|0x00=0xFF → "\xFF\xF0\xFF".
        auto r_or = co_await redis.bitop("OR", destkey, std::vector<std::string>{key1, key2});
        EXPECT_TRUE(r_or.ok()) << r_or.error();
        EXPECT_EQ(r_or.result(), 3);
        auto get_or = co_await redis.get(destkey);
        if (!(get_or.ok() && get_or.result().has_value())) {
            ADD_FAILURE() << "precondition failed: get_or.ok() && get_or.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*get_or.result(), std::string("\xFF\xF0\xFF", 3));

        // XOR: 0xFF^0x0F=0xF0, 0x00^0xF0=0xF0, 0xFF^0x00=0xFF → "\xF0\xF0\xFF".
        auto r_xor = co_await redis.bitop("XOR", destkey, std::vector<std::string>{key1, key2});
        EXPECT_TRUE(r_xor.ok()) << r_xor.error();
        EXPECT_EQ(r_xor.result(), 3);
        auto get_xor = co_await redis.get(destkey);
        if (!(get_xor.ok() && get_xor.result().has_value())) {
            ADD_FAILURE() << "precondition failed: get_xor.ok() && get_xor.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*get_xor.result(), std::string("\xF0\xF0\xFF", 3));

        // NOT (unary): ~"\xFF\x00\xFF" = "\x00\xFF\x00" (3 bytes, no padding involved).
        auto r_not = co_await redis.bitop("NOT", destkey, std::vector<std::string>{key1});
        EXPECT_TRUE(r_not.ok()) << r_not.error();
        EXPECT_EQ(r_not.result(), 3);
        auto get_not = co_await redis.get(destkey);
        if (!(get_not.ok() && get_not.result().has_value())) {
            ADD_FAILURE() << "precondition failed: get_not.ok() && get_not.result().has_value()";
            co_return;
        }
        EXPECT_EQ(*get_not.result(), std::string("\x00\xFF\x00", 3));

        completed = true;
    });
    run_coro_test_until(completed);
}

// BITPOS — deterministic first-set / first-clear positions over "\xFF\x00\xFF".
TEST_P(BitmapProtocolModesTest, BITPOS) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("bitpos");

        auto set_r = co_await redis.set(key, std::string("\xFF\x00\xFF", 3));
        EXPECT_TRUE(set_r.ok()) << set_r.error();

        // First 1-bit is bit 0 of byte 0.
        auto first_one = co_await redis.bitpos(key, true);
        EXPECT_TRUE(first_one.ok()) << first_one.error();
        EXPECT_EQ(first_one.result(), 0);

        // First 0-bit is bit 8 (start of the 0x00 byte).
        auto first_zero = co_await redis.bitpos(key, false);
        EXPECT_TRUE(first_zero.ok()) << first_zero.error();
        EXPECT_EQ(first_zero.result(), 8);

        // Byte 0 (0xFF): no 0-bit → -1.
        auto byte0_zero = co_await redis.bitpos(key, false, 0, 0);
        EXPECT_TRUE(byte0_zero.ok()) << byte0_zero.error();
        EXPECT_EQ(byte0_zero.result(), -1);

        // Byte 0 (0xFF): first 1-bit is position 0.
        auto byte0_one = co_await redis.bitpos(key, true, 0, 0);
        EXPECT_TRUE(byte0_one.ok()) << byte0_one.error();
        EXPECT_EQ(byte0_one.result(), 0);

        // Byte 1 (0x00): no 1-bit → -1.
        auto byte1_one = co_await redis.bitpos(key, true, 1, 1);
        EXPECT_TRUE(byte1_one.ok()) << byte1_one.error();
        EXPECT_EQ(byte1_one.result(), -1);

        // Byte 2 (0xFF): first 1-bit is position 16.
        auto byte2_one = co_await redis.bitpos(key, true, 2, 2);
        EXPECT_TRUE(byte2_one.ok()) << byte2_one.error();
        EXPECT_EQ(byte2_one.result(), 16);

        completed = true;
    });
    run_coro_test_until(completed);
}

// BITPOS regression: on an ALL-ONES value, searching for a 0-bit with NO explicit end must return
// the first position in the implicit zero-padding PAST the string (Redis's open-ended semantics).
// The builder previously forced end=-1, which makes the range closed and returns -1 instead of 16.
// This is the guard for the std::optional start/end fix in bitmap_commands.h.
TEST_P(BitmapProtocolModesTest, BITPOS_OpenEndedClearBitPastAllOnes) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("bitpos_allones");

        auto set_r = co_await redis.set(key, std::string("\xFF\xFF", 2));
        EXPECT_TRUE(set_r.ok()) << set_r.error();

        // No end given → open-ended search: first 0-bit is at position 16, just past the two 0xFF
        // bytes. Pre-fix this emitted `BITPOS key 0 0 -1` (closed) and returned -1.
        auto open_zero = co_await redis.bitpos(key, false);
        EXPECT_TRUE(open_zero.ok()) << open_zero.error();
        EXPECT_EQ(open_zero.result(), 16) << "open-ended BITPOS must find the clear bit past the string";

        // An explicit [0, -1] range is closed: no 0-bit within the stored bytes → -1 (unchanged).
        auto closed_zero = co_await redis.bitpos(key, false, 0, -1);
        EXPECT_TRUE(closed_zero.ok()) << closed_zero.error();
        EXPECT_EQ(closed_zero.result(), -1) << "explicit end makes the range closed";

        completed = true;
    });
    run_coro_test_until(completed);
}

// GETBIT / SETBIT — SETBIT returns the previous bit value; GETBIT reads it back.
TEST_P(BitmapProtocolModesTest, GETBIT_SETBIT) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("getbit_setbit");

        auto s1 = co_await redis.setbit(key, 7, true);
        EXPECT_TRUE(s1.ok()) << s1.error();
        EXPECT_EQ(s1.result(), 0); // was 0

        auto s2 = co_await redis.setbit(key, 7, false);
        EXPECT_TRUE(s2.ok()) << s2.error();
        EXPECT_EQ(s2.result(), 1); // was 1

        auto s3 = co_await redis.setbit(key, 7, true);
        EXPECT_TRUE(s3.ok()) << s3.error();
        EXPECT_EQ(s3.result(), 0); // was 0 again

        auto g0 = co_await redis.getbit(key, 0);
        EXPECT_TRUE(g0.ok()) << g0.error();
        EXPECT_EQ(g0.result(), 0);

        auto g7 = co_await redis.getbit(key, 7);
        EXPECT_TRUE(g7.ok()) << g7.error();
        EXPECT_EQ(g7.result(), 1);

        completed = true;
    });
    run_coro_test_until(completed);
}

} // namespace
