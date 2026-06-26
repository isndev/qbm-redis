/**
 * @file qbm/redis/tests/benchmark/parser/resp-parse.bench.cpp
 * @brief Throughput benchmarks for the inbound RESP2/RESP3 wire decoder.
 *
 * Measures the qbm-redis per-reply hot path: `qb::redis::parser::parse` (one-shot)
 * and the streaming `RespParser::feed` + `parse`/`parse_all` pump every redis
 * connection drives for each inbound reply. The corpus mirrors the wire frames
 * the codec unit tests pin (resp-codec.cpp) and the generators promoted out of the
 * old `test-parser.cpp` `PerformanceTest.*` / `StressTest.*` cases — so the
 * benchmark decodes the exact same shapes the tests assert correctness on:
 *
 *   - SimpleString   "+OK\r\n"                          (scalar fast path)
 *   - Integer        ":1234567890\r\n"                  (scalar fast path)
 *   - BulkString     small (11B), 1MB, 10MB             (payload copy cost)
 *   - Array          multi-bulk *3 of bulk strings      (aggregate + element loop)
 *   - RESP3 Map      %N flat field/value                (paired aggregate)
 *   - RESP3 Set      ~N of bulk strings                 (aggregate)
 *   - RESP3 Push     >N pub/sub message frame           (out-of-band aggregate)
 *   - DeeplyNested   50 levels of *1 wrapping an int    (recursion depth)
 *   - 1000-array     *1000 of small ints                (wide aggregate stress)
 *   - 10k-small      10000 pipelined ":i\r\n" frames    (batch parse_all throughput)
 *   - parser-reuse   feed/parse/compact one frame ×N    (reset-free reuse path)
 *
 * Daemon-free: every input is built in-process from byte literals/generators; no
 * socket, no event loop. All corpus construction is hoisted out of the timed
 * region (built once into file-static strings or pause-timed when per-iteration
 * parser state must be fresh). DoNotOptimize is applied to non-const result
 * lvalues. Each benchmark performs one out-of-loop correctness gate that calls
 * state.SkipWithError if the decode does not match the expected shape, so a
 * silently-broken codec never reports a misleading "fast" number. There are no
 * EXPECT_LT wall-clock gates — measurement is reported, never asserted.
 *
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2026 qb - isndev (cpp.actor)
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
 * limitations under the License.
 * @ingroup Redis
 */

#include <benchmark/benchmark.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "../parser.h" // qb::redis::parser::{parse, RespParser, Value, ...}

namespace {

using qb::redis::parser::parse;
using qb::redis::parser::ParseErrorCode;
using qb::redis::parser::ParserConfig;
using qb::redis::parser::RespParser;
using qb::redis::parser::Value;

// ---------------------------------------------------------------------------
// Deterministic corpus generators (seeded from resp-codec.cpp / the old
// test-parser.cpp PerformanceTest+StressTest shapes). Built once, cached in
// file-static strings so construction never lands in a timed region.
// ---------------------------------------------------------------------------

std::string
make_simple_string() {
    return "+OK\r\n";
}

std::string
make_integer() {
    return ":1234567890\r\n";
}

// $<len>\r\n<payload of `size` 'x'>\r\n
std::string
make_bulk(std::size_t size) {
    std::string payload(size, 'x');
    std::string frame;
    frame.reserve(size + 32);
    frame += '$';
    frame += std::to_string(size);
    frame += "\r\n";
    frame += payload;
    frame += "\r\n";
    return frame;
}

// *3\r\n$3\r\nfoo\r\n$3\r\nbar\r\n$3\r\nbaz\r\n
std::string
make_array() {
    return "*3\r\n$3\r\nfoo\r\n$3\r\nbar\r\n$3\r\nbaz\r\n";
}

// RESP3 map %2\r\n<k1><v1><k2><v2>
std::string
make_map() {
    return "%2\r\n$3\r\nkey\r\n$5\r\nvalue\r\n$2\r\nid\r\n:42\r\n";
}

// RESP3 set ~3 of bulk strings
std::string
make_set() {
    return "~3\r\n$1\r\na\r\n$1\r\nb\r\n$1\r\nc\r\n";
}

// RESP3 push >3 pub/sub "message" frame
std::string
make_push() {
    return ">3\r\n$7\r\nmessage\r\n$7\r\nchannel\r\n$5\r\nhello\r\n";
}

// 50 nested *1 arrays wrapping a single integer (recursion-depth path).
std::string
make_deeply_nested(int depth) {
    std::string frame;
    frame.reserve(static_cast<std::size_t>(depth) * 4 + 8);
    for (int i = 0; i < depth; ++i)
        frame += "*1\r\n";
    frame += ":1\r\n";
    return frame;
}

// *N\r\n of N small integers (wide aggregate stress).
std::string
make_wide_array(int n) {
    std::string frame = "*" + std::to_string(n) + "\r\n";
    for (int i = 0; i < n; ++i) {
        frame += ':';
        frame += std::to_string(i);
        frame += "\r\n";
    }
    return frame;
}

// N back-to-back ":i\r\n" frames (pipelined batch for parse_all).
std::string
make_small_batch(int n) {
    std::string frame;
    frame.reserve(static_cast<std::size_t>(n) * 6);
    for (int i = 0; i < n; ++i) {
        frame += ':';
        frame += std::to_string(i);
        frame += "\r\n";
    }
    return frame;
}

// Default config decodes RESP3 (a superset of RESP2), matching the live client.
const ParserConfig &
resp3_config() {
    static const ParserConfig cfg{}; // protocol_version defaults to RESP3
    return cfg;
}

// ---------------------------------------------------------------------------
// One-shot parse of a single complete frame: qb::redis::parser::parse.
// state.range(0) selects the corpus.
// ---------------------------------------------------------------------------

enum Corpus {
    kSimpleString = 0,
    kInteger,
    kBulkSmall,
    kBulk1MB,
    kBulk10MB,
    kArray,
    kMap,
    kSet,
    kPush,
    kDeeplyNested,
};

const std::string &
select_one_shot_corpus(std::int64_t which) {
    static const std::string simple   = make_simple_string();
    static const std::string integer  = make_integer();
    static const std::string bulk_sm  = make_bulk(11);
    static const std::string bulk_1m  = make_bulk(1u * 1024 * 1024);
    static const std::string bulk_10m = make_bulk(10u * 1024 * 1024);
    static const std::string array    = make_array();
    static const std::string map      = make_map();
    static const std::string set      = make_set();
    static const std::string push     = make_push();
    static const std::string nested   = make_deeply_nested(50);
    switch (which) {
        case kSimpleString:
            return simple;
        case kInteger:
            return integer;
        case kBulkSmall:
            return bulk_sm;
        case kBulk1MB:
            return bulk_1m;
        case kBulk10MB:
            return bulk_10m;
        case kArray:
            return array;
        case kMap:
            return map;
        case kSet:
            return set;
        case kPush:
            return push;
        default:
            return nested;
    }
}

void
BM_RespParse_OneShot(benchmark::State &state) {
    const std::string &frame = select_one_shot_corpus(state.range(0));

    // Out-of-loop correctness gate: the frame must decode to a complete value.
    {
        auto check = parse(frame, resp3_config());
        if (!check.has_value()) {
            state.SkipWithError("one-shot parse failed for selected corpus");
            return;
        }
    }

    for (auto _ : state) {
        auto result = parse(frame, resp3_config());
        benchmark::DoNotOptimize(result);
    }

    state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(frame.size()));
    state.SetItemsProcessed(state.iterations());
}

// ---------------------------------------------------------------------------
// Streaming batch: feed N frames, drain with parse_all(). The wide-array,
// 1000-array, and 10k-small-message stress paths.
// ---------------------------------------------------------------------------

void
BM_RespParse_WideArray(benchmark::State &state) {
    static const std::string frame = make_wide_array(1000);

    {
        RespParser parser(resp3_config());
        parser.feed(frame);
        auto v = parser.parse();
        if (!v.has_value() || !v->is_array() || v->as_array().size() != 1000u) {
            state.SkipWithError("wide-array did not decode to a 1000-element array");
            return;
        }
    }

    for (auto _ : state) {
        RespParser parser(resp3_config());
        parser.feed(frame);
        auto values = parser.parse_all();
        benchmark::DoNotOptimize(values);
    }

    state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(frame.size()));
    state.SetItemsProcessed(state.iterations());
}

void
BM_RespParse_SmallBatch(benchmark::State &state) {
    const int                count = static_cast<int>(state.range(0));
    static const std::string frame_1000  = make_small_batch(1000);
    static const std::string frame_10000 = make_small_batch(10000);
    const std::string       &frame       = (count >= 10000) ? frame_10000 : frame_1000;

    {
        RespParser parser(resp3_config());
        parser.feed(frame);
        auto values = parser.parse_all();
        if (values.size() != static_cast<std::size_t>(count)) {
            state.SkipWithError("small-batch decoded a different frame count than fed");
            return;
        }
    }

    for (auto _ : state) {
        RespParser parser(resp3_config());
        parser.feed(frame);
        auto values = parser.parse_all();
        benchmark::DoNotOptimize(values);
    }

    state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(frame.size()));
    state.SetItemsProcessed(state.iterations() * count);
}

// ---------------------------------------------------------------------------
// Parser reuse: one parser instance, feed + parse + compact a fresh small frame
// each call (the reset-free reuse path StressTest.ParserReuse pins).
// ---------------------------------------------------------------------------

void
BM_RespParse_ParserReuse(benchmark::State &state) {
    static const std::string frame = make_integer();

    {
        RespParser parser(resp3_config());
        parser.feed(frame);
        auto v = parser.parse();
        if (!v.has_value() || !v->is_integer()) {
            state.SkipWithError("parser-reuse frame did not decode to an integer");
            return;
        }
    }

    RespParser parser(resp3_config());
    for (auto _ : state) {
        parser.feed(frame);
        auto v = parser.parse();
        benchmark::DoNotOptimize(v);
        parser.compact();
    }

    state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(frame.size()));
    state.SetItemsProcessed(state.iterations());
}

} // namespace

// corpus: 0=+OK 1=:int 2=bulk-small 3=bulk-1MB 4=bulk-10MB 5=array 6=map 7=set
//         8=push 9=deeply-nested(50)
BENCHMARK(BM_RespParse_OneShot)
    ->Arg(kSimpleString)
    ->Arg(kInteger)
    ->Arg(kBulkSmall)
    ->Arg(kBulk1MB)
    ->Arg(kBulk10MB)
    ->Arg(kArray)
    ->Arg(kMap)
    ->Arg(kSet)
    ->Arg(kPush)
    ->Arg(kDeeplyNested)
    ->ArgNames({"corpus"})
    ->Unit(benchmark::kNanosecond);

BENCHMARK(BM_RespParse_WideArray)->Unit(benchmark::kMicrosecond);

BENCHMARK(BM_RespParse_SmallBatch)->Arg(1000)->Arg(10000)->ArgNames({"frames"})->Unit(benchmark::kMicrosecond);

BENCHMARK(BM_RespParse_ParserReuse)->Unit(benchmark::kNanosecond);

BENCHMARK_MAIN();
