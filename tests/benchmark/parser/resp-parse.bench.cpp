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
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <qbm/redis/parser.h> // qb::redis::parser::{parse, RespParser, Value, ...}

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

// Check every decoded leaf and its RESP type against independent literals.
// No expected Value is obtained by calling the parser under measurement.
bool
is_bulk(const Value &value, std::string_view expected) {
    return value.is_bulk_string() && value.as_bulk_string().view() == expected;
}

bool
bulk_sequence_matches(const std::vector<std::unique_ptr<Value>> &values, std::initializer_list<std::string_view> expected) {
    if (values.size() != expected.size())
        return false;
    std::size_t i = 0;
    for (auto text : expected) {
        if (!values[i] || !is_bulk(*values[i], text))
            return false;
        ++i;
    }
    return true;
}

bool
one_shot_matches(const Value &value, std::int64_t corpus) {
    switch (corpus) {
        case kSimpleString:
            return value.is_simple_string() && value.as_string_view() == "OK";
        case kInteger:
            return value.is_integer() && value.as_integer().value == 1234567890;
        case kBulkSmall:
        case kBulk1MB:
        case kBulk10MB: {
            const std::size_t size = corpus == kBulkSmall ? 11u : corpus == kBulk1MB ? 1024u * 1024 : 10u * 1024 * 1024;
            if (!value.is_bulk_string())
                return false;
            const auto text = value.as_bulk_string().view();
            return text.size() == size && text.find_first_not_of('x') == std::string_view::npos;
        }
        case kArray:
            return value.is_array() && bulk_sequence_matches(value.as_array().elements, {"foo", "bar", "baz"});
        case kMap: {
            if (!value.is_map() || value.as_map().size() != 2)
                return false;
            const auto &entries = value.as_map().entries;
            return entries[0].first && entries[0].second && entries[1].first && entries[1].second && is_bulk(*entries[0].first, "key")
                   && is_bulk(*entries[0].second, "value") && is_bulk(*entries[1].first, "id") && entries[1].second->is_integer()
                   && entries[1].second->as_integer().value == 42;
        }
        case kSet:
            return value.is_set() && bulk_sequence_matches(value.as_set().elements, {"a", "b", "c"});
        case kPush:
            return value.is_push() && bulk_sequence_matches(value.as_push().elements, {"message", "channel", "hello"});
        case kDeeplyNested: {
            const Value *leaf = &value;
            for (int depth = 0; depth < 50; ++depth) {
                if (!leaf->is_array() || leaf->as_array().size() != 1 || !leaf->as_array()[0])
                    return false;
                leaf = leaf->as_array()[0];
            }
            return leaf->is_integer() && leaf->as_integer().value == 1;
        }
        default:
            return false;
    }
}

void
BM_RespParse_OneShot(benchmark::State &state) {
    const std::string &frame = select_one_shot_corpus(state.range(0));

    // Out-of-loop gate: exact RESP type, contents and aggregate order.
    {
        auto check = parse(frame, resp3_config());
        if (!check.has_value() || !one_shot_matches(*check, state.range(0))) {
            state.SkipWithError("one-shot RESP type, value or order mismatch");
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
        auto values = parser.parse_all();
        if (parser.has_error() || !parser.unparsed_data().empty() || values.size() != 1 || !values[0].is_array()
            || values[0].as_array().size() != 1000u) {
            state.SkipWithError("wide-array parse_all count, type or consumption mismatch");
            return;
        }
        const auto &array = values[0].as_array();
        for (std::size_t i = 0; i < array.size(); ++i) {
            if (!array[i] || !array[i]->is_integer() || array[i]->as_integer().value != static_cast<std::int64_t>(i)) {
                state.SkipWithError("wide-array element type, value or order mismatch");
                return;
            }
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
    const int                count       = static_cast<int>(state.range(0));
    static const std::string frame_1000  = make_small_batch(1000);
    static const std::string frame_10000 = make_small_batch(10000);
    const std::string       &frame       = (count >= 10000) ? frame_10000 : frame_1000;

    {
        RespParser parser(resp3_config());
        parser.feed(frame);
        auto values = parser.parse_all();
        if (parser.has_error() || !parser.unparsed_data().empty() || values.size() != static_cast<std::size_t>(count)) {
            state.SkipWithError("small-batch decoded a different frame count than fed");
            return;
        }
        for (int i = 0; i < count; ++i) {
            const auto &value = values[static_cast<std::size_t>(i)];
            if (!value.is_integer() || value.as_integer().value != i) {
                state.SkipWithError("small-batch element type, value or order mismatch");
                return;
            }
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
        for (int reuse = 0; reuse < 2; ++reuse) {
            parser.feed(frame);
            auto v = parser.parse();
            if (!v.has_value() || !v->is_integer() || v->as_integer().value != 1234567890 || parser.has_error()
                || !parser.unparsed_data().empty()) {
                state.SkipWithError("parser-reuse integer value or consumption mismatch");
                return;
            }
            parser.compact();
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
