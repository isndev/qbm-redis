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
 * @file qbm/redis/tests/shared/resp_corpus.h
 * @brief Single source of truth for canned RESP2/RESP3 wire frames + the
 *        generators for large/stress payloads, shared by the parser unit tests
 *        AND the parser benchmarks.
 *
 * The hand-written frames (`frames::*`) are tiny, exact, and human-auditable —
 * each carries the bytes plus a one-line note of the Value tree it decodes to,
 * so a unit test and a benchmark parse the *same* canonical input. The
 * generators (`gen::*`) are the data builders promoted verbatim out of
 * `test-parser.cpp`'s `PerformanceTest.*` / `StressTest.*` and `BoundaryTest`,
 * so the benchmark and the unit tier seed from one place instead of inventing
 * divergent corpora.
 *
 * Header-only, daemon-free, zero dependencies beyond the parser headers.
 *
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2026 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)
 * @ingroup Redis
 */
#ifndef QBM_REDIS_TESTS_SHARED_RESP_CORPUS_H
#define QBM_REDIS_TESTS_SHARED_RESP_CORPUS_H

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace qb::redis::test::corpus {

// ============================================================================
// Hand-written canonical frames (exact bytes + decoded-shape note)
// ============================================================================
//
// Each constant is the complete RESP wire encoding of one top-level value.
// The trailing comment states the Value tree it decodes to so that a reader
// can verify the unit asserts and the benchmark are parsing the same thing.
namespace frames {

// --- RESP2 scalars ---------------------------------------------------------
inline constexpr std::string_view simple_string = "+OK\r\n";                   // SimpleString("OK")
inline constexpr std::string_view simple_error  = "-ERR unknown command\r\n";  // SimpleError("ERR","unknown command")
inline constexpr std::string_view integer       = ":1234\r\n";                 // Integer(1234)
inline constexpr std::string_view integer_min   = ":-9223372036854775808\r\n"; // Integer(INT64_MIN)
inline constexpr std::string_view bulk          = "$5\r\nhello\r\n";           // BulkString("hello")
inline constexpr std::string_view bulk_empty    = "$0\r\n\r\n";                // BulkString("")
inline constexpr std::string_view bulk_null     = "$-1\r\n";                   // Null
inline constexpr std::string_view array_null    = "*-1\r\n";                   // Null

// --- RESP2 aggregates ------------------------------------------------------
// *2 [ "hello", "world" ]
inline constexpr std::string_view array_two_bulk = "*2\r\n$5\r\nhello\r\n$5\r\nworld\r\n";
// *3 [ 1, "hello", +OK ]
inline constexpr std::string_view array_mixed = "*3\r\n:1\r\n$5\r\nhello\r\n+OK\r\n";
// *2 [ *3[1,2,3], *2[+Hello,-World] ]
inline constexpr std::string_view array_nested = "*2\r\n*3\r\n:1\r\n:2\r\n:3\r\n*2\r\n+Hello\r\n-World\r\n";

// --- RESP3 scalars ---------------------------------------------------------
inline constexpr std::string_view null_      = "_\r\n";                               // Null
inline constexpr std::string_view boolean_t  = "#t\r\n";                              // Boolean(true)
inline constexpr std::string_view boolean_f  = "#f\r\n";                              // Boolean(false)
inline constexpr std::string_view dbl        = ",1.23\r\n";                           // Double(1.23)
inline constexpr std::string_view dbl_inf    = ",inf\r\n";                            // Double(+inf)
inline constexpr std::string_view big_number = "(123456789012345678901234567890\r\n"; // BigNumber(...)
inline constexpr std::string_view bulk_error = "!21\r\nSYNTAX invalid syntax\r\n";    // BulkError("SYNTAX","invalid syntax")
inline constexpr std::string_view verbatim   = "=15\r\ntxt:Some string\r\n";          // VerbatimString("txt","Some string")

// --- RESP3 aggregates ------------------------------------------------------
// %2 { "first": 1, "second": 2 }
inline constexpr std::string_view map_two = "%2\r\n+first\r\n:1\r\n+second\r\n:2\r\n";
// ~3 { one, two, three }
inline constexpr std::string_view set_three = "~3\r\n+one\r\n+two\r\n+three\r\n";
// >3 [ "message", "channel", "payload" ]  (pub/sub push)
inline constexpr std::string_view push_message = ">3\r\n+message\r\n+channel\r\n$7\r\npayload\r\n";
// |1 { "ttl": 3600 } -> 42  (attribute decorating an integer)
inline constexpr std::string_view attribute_int = "|1\r\n+ttl\r\n:3600\r\n:42\r\n";

} // namespace frames

// ============================================================================
// Generators (large / stress payloads, promoted from test-parser.cpp)
// ============================================================================
namespace gen {

/// N integers `:0\r\n:1\r\n...` — was PerformanceTest.ParseManySmallMessages (N=10000).
[[nodiscard]] inline std::string
many_integers(int count) {
    std::string data;
    data.reserve(static_cast<size_t>(count) * 6);
    for (int i = 0; i < count; ++i) {
        data += ":" + std::to_string(i) + "\r\n";
    }
    return data;
}

/// One bulk string of `payload_bytes` 'x' chars — was ParseLargeBulkString (10MB).
[[nodiscard]] inline std::string
large_bulk(size_t payload_bytes) {
    std::string content(payload_bytes, 'x');
    return "$" + std::to_string(payload_bytes) + "\r\n" + content + "\r\n";
}

/// `depth` nested single-element arrays terminating in `:1` — was ParseDeeplyNestedArray.
[[nodiscard]] inline std::string
deeply_nested_array(int depth) {
    std::string data;
    data.reserve(static_cast<size_t>(depth) * 4 + 4);
    for (int i = 0; i < depth; ++i) {
        data += "*1\r\n";
    }
    data += ":1\r\n";
    return data;
}

/// `count` copies of `*2[1,2]` back-to-back — was StressTest.ManySmallArrays (count=1000).
[[nodiscard]] inline std::string
many_small_arrays(int count) {
    std::string data;
    data.reserve(static_cast<size_t>(count) * 12);
    for (int i = 0; i < count; ++i) {
        data += "*2\r\n:1\r\n:2\r\n";
    }
    return data;
}

/// One flat array of `count` integers `*count\r\n:0..` — large single aggregate.
[[nodiscard]] inline std::string
flat_integer_array(int count) {
    std::string data = "*" + std::to_string(count) + "\r\n";
    for (int i = 0; i < count; ++i) {
        data += ":" + std::to_string(i) + "\r\n";
    }
    return data;
}

/// One flat RESP3 map of `pairs` `+kN -> :N` entries — wide-map heuristic seed.
[[nodiscard]] inline std::string
flat_map(int pairs) {
    std::string data = "%" + std::to_string(pairs) + "\r\n";
    for (int i = 0; i < pairs; ++i) {
        data += "+k" + std::to_string(i) + "\r\n:" + std::to_string(i) + "\r\n";
    }
    return data;
}

} // namespace gen

} // namespace qb::redis::test::corpus

#endif // QBM_REDIS_TESTS_SHARED_RESP_CORPUS_H
