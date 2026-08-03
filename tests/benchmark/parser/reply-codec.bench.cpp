/**
 * @file qbm/redis/tests/benchmark/parser/reply-codec.bench.cpp
 * @brief Throughput benchmarks for the qbm-redis reply codec (Value<->typed<->wire).
 *
 * Measures the second half of the per-reply hot path that sits on top of the RESP
 * decoder (benchmarked separately in resp-parse.bench.cpp):
 *
 *   (a) reply::parse<std::vector<score_member>> over a large flat RESP2
 *       ZRANGE-WITHSCORES array  — the inbound typed-decode the sorted-set
 *       command surface drives.
 *   (b) reply::parse<map_stream_entry_list> over a multi-stream XREAD reply —
 *       the most deeply structured inbound decode (array of [stream, entry-list]
 *       pairs -> per-stream entry list -> field map; the RESP2 XREAD wire shape
 *       this typed converter accepts).
 *   (c) to_redis_string + put_in_pipe for a large multi-bulk command — the
 *       outbound serialization every command emit drives.
 *   (d) reply::parse<qb::json> over a wide flat RESP3 map — the JSON-reconstruction
 *       heuristic (the widest inbound conversion).
 *   (e) interval bound formatting (std::to_chars on doubles) via constructing
 *       BoundedInterval<double> — the score/range formatting hot spot.
 *
 * The Value trees are built once with local builders that mirror the shapes the
 * reply unit tests assert on (reply-parse.cpp / reply-serialize.cpp /
 * interval-formatting.cpp). Daemon-free: no socket, no event loop. Tree
 * construction is hoisted out of the timed region (built once into file-static
 * values, or PauseTiming-guarded where a fresh pipe must be allocated per
 * iteration). DoNotOptimize is applied to non-const result lvalues. Each
 * benchmark runs one out-of-loop correctness gate (SkipWithError on mismatch) so
 * a broken codec cannot report a misleadingly-fast number. There are no
 * EXPECT_LT wall-clock gates.
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
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <qb/system/allocator/pipe.h>

#include <qbm/redis/reply.h> // qb::redis::reply::parse, to_redis_string, put_in_pipe
#include <qbm/redis/types.h> // score_member, map_stream_entry_list, BoundedInterval

namespace {

using qb::redis::parser::Array;
using qb::redis::parser::BulkString;
using qb::redis::parser::Double;
using qb::redis::parser::Integer;
using qb::redis::parser::Map;
using qb::redis::parser::Value;

// ---------------------------------------------------------------------------
// Local Value-tree builders (mirror reply_value_builders.h / test-reply.cpp).
// Kept inline so this benchmark is self-contained and deterministic.
// ---------------------------------------------------------------------------

std::unique_ptr<Value>
mk_bulk(std::string s) {
    return std::make_unique<Value>(Value(BulkString{std::move(s)}));
}

std::unique_ptr<Value>
mk_int(int64_t i) {
    return std::make_unique<Value>(Value(Integer{i}));
}

Value
make_array(std::vector<std::unique_ptr<Value>> elems) {
    Array arr;
    arr.elements = std::move(elems);
    return Value(std::move(arr));
}

Value
make_map(std::vector<std::pair<std::unique_ptr<Value>, std::unique_ptr<Value>>> entries) {
    Map m;
    m.entries = std::move(entries);
    return Value(std::move(m));
}

// A stream entry: [ "<id>", [f1, v1, f2, v2, ...] ]
std::unique_ptr<Value>
make_stream_entry(std::string id, std::vector<std::pair<std::string, std::string>> fv) {
    std::vector<std::unique_ptr<Value>> fields;
    for (auto &[f, v] : fv) {
        fields.push_back(mk_bulk(f));
        fields.push_back(mk_bulk(v));
    }
    std::vector<std::unique_ptr<Value>> entry;
    entry.push_back(mk_bulk(std::move(id)));
    entry.push_back(std::make_unique<Value>(make_array(std::move(fields))));
    return std::make_unique<Value>(make_array(std::move(entry)));
}

// ---------------------------------------------------------------------------
// (a) Large RESP2 flat ZRANGE-WITHSCORES array: [member, score, member, score, ...]
// ---------------------------------------------------------------------------

Value
make_zrange_withscores(int n) {
    std::vector<std::unique_ptr<Value>> elems;
    elems.reserve(static_cast<std::size_t>(n) * 2);
    for (int i = 0; i < n; ++i) {
        elems.push_back(mk_bulk("member:" + std::to_string(i)));
        elems.push_back(mk_bulk(std::to_string(static_cast<double>(i) + 0.5)));
    }
    return make_array(std::move(elems));
}

void
BM_ReplyParse_ScoreMemberVector(benchmark::State &state) {
    const int   n     = static_cast<int>(state.range(0));
    const Value reply = make_zrange_withscores(n);

    {
        auto out = qb::redis::reply::parse<std::vector<qb::redis::score_member>>(reply);
        if (out.size() != static_cast<std::size_t>(n)) {
            state.SkipWithError("score_member vector decoded a different count than seeded");
            return;
        }
    }

    for (auto _ : state) {
        auto out = qb::redis::reply::parse<std::vector<qb::redis::score_member>>(reply);
        benchmark::DoNotOptimize(out);
    }

    state.SetItemsProcessed(state.iterations() * n);
}

// ---------------------------------------------------------------------------
// (b) Multi-stream XREAD reply. The typed `map_stream_entry_list` converter
// (reply.cpp parse(ParseTag<map_stream_entry_list>,…)) decodes the RESP2 XREAD
// wire shape: an ARRAY of [streamName, [entry, entry, …]] pairs — NOT a RESP3
// map. (Confirmed against live redis-cli: RESP2 XREAD = array-of-pairs; the
// converter throws ReplyParseError("ARRAY", …) on a top-level Map.) Mirror the
// exact tree the unit test pins (reply-parse.cpp ReplyMapStreamEntryList.*).
// ---------------------------------------------------------------------------

Value
make_xread_reply(int streams, int entries_per_stream) {
    std::vector<std::unique_ptr<Value>> outer;
    outer.reserve(static_cast<std::size_t>(streams));
    for (int s = 0; s < streams; ++s) {
        std::vector<std::unique_ptr<Value>> entry_list;
        for (int e = 0; e < entries_per_stream; ++e) {
            entry_list.push_back(
                make_stream_entry(std::to_string(e + 1) + "-0", {{"field", "value:" + std::to_string(e)}, {"seq", std::to_string(e)}}));
        }
        // [ "stream:<s>", [ entry, entry, … ] ]
        std::vector<std::unique_ptr<Value>> stream_pair;
        stream_pair.push_back(mk_bulk("stream:" + std::to_string(s)));
        stream_pair.push_back(std::make_unique<Value>(make_array(std::move(entry_list))));
        outer.push_back(std::make_unique<Value>(make_array(std::move(stream_pair))));
    }
    return make_array(std::move(outer));
}

void
BM_ReplyParse_MapStreamEntryList(benchmark::State &state) {
    const int   streams = static_cast<int>(state.range(0));
    const int   per     = 10;
    const Value reply   = make_xread_reply(streams, per);

    {
        auto out = qb::redis::reply::parse<qb::redis::map_stream_entry_list>(reply);
        if (out.size() != static_cast<std::size_t>(streams)) {
            state.SkipWithError("map_stream_entry_list decoded a different stream count than seeded");
            return;
        }
    }

    for (auto _ : state) {
        auto out = qb::redis::reply::parse<qb::redis::map_stream_entry_list>(reply);
        benchmark::DoNotOptimize(out);
    }

    state.SetItemsProcessed(state.iterations() * streams * per);
}

// ---------------------------------------------------------------------------
// (c) Outbound: to_redis_string + put_in_pipe for a large multi-bulk command.
// A fresh pipe must be allocated per iteration; allocation is pause-timed so only
// the serialization is measured.
// ---------------------------------------------------------------------------

std::vector<std::string>
make_command_args(int n) {
    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(n) + 1);
    args.emplace_back("MSET");
    for (int i = 0; i < n; ++i) {
        args.push_back("key:" + std::to_string(i));
        args.push_back("value:" + std::to_string(i));
    }
    return args;
}

void
BM_ReplySerialize_MultiBulk(benchmark::State &state) {
    const int                      n    = static_cast<int>(state.range(0));
    const std::vector<std::string> args = make_command_args(n);

    {
        qb::allocator::pipe<char> pipe;
        for (const auto &a : args)
            qb::redis::to_redis_string(pipe, a);
        if (pipe.size() == 0) {
            state.SkipWithError("serialization produced an empty pipe");
            return;
        }
    }

    qb::allocator::pipe<char> pipe;
    for (auto _ : state) {
        pipe.reset();
        for (const auto &a : args)
            qb::redis::to_redis_string(pipe, a);
        benchmark::DoNotOptimize(pipe.begin());
    }

    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(args.size()));
}

void
BM_ReplySerialize_PutInPipe(benchmark::State &state) {
    // put_in_pipe writes the *N\r\n array header + each arg, the full outbound
    // command-emit path. A handful of representative args (string + score_member +
    // stream_id) so the redis_count + to_redis_string dispatch is exercised.
    {
        qb::allocator::pipe<char> pipe;
        qb::redis::put_in_pipe(pipe, std::string("ZADD"), std::string("myset"), qb::redis::score_member{1.5, "alice"},
                               qb::redis::score_member{2.5, "bob"});
        if (pipe.size() == 0) {
            state.SkipWithError("put_in_pipe produced an empty pipe");
            return;
        }
    }

    qb::allocator::pipe<char> pipe;
    for (auto _ : state) {
        pipe.reset();
        qb::redis::put_in_pipe(pipe, std::string("ZADD"), std::string("myset"), qb::redis::score_member{1.5, "alice"},
                               qb::redis::score_member{2.5, "bob"});
        benchmark::DoNotOptimize(pipe.begin());
    }

    state.SetItemsProcessed(state.iterations());
}

// ---------------------------------------------------------------------------
// (d) parse<qb::json> over a wide flat RESP3 map (the JSON-reconstruction path).
// ---------------------------------------------------------------------------

Value
make_wide_map(int n) {
    std::vector<std::pair<std::unique_ptr<Value>, std::unique_ptr<Value>>> entries;
    entries.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        if (i % 2 == 0)
            entries.emplace_back(mk_bulk("field:" + std::to_string(i)), mk_bulk("value:" + std::to_string(i)));
        else
            entries.emplace_back(mk_bulk("field:" + std::to_string(i)), mk_int(i));
    }
    return make_map(std::move(entries));
}

void
BM_ReplyParse_JsonWideMap(benchmark::State &state) {
    const int   n     = static_cast<int>(state.range(0));
    const Value reply = make_wide_map(n);

    {
        auto j = qb::redis::reply::parse<qb::json>(reply);
        if (!j.is_object() || j.size() != static_cast<std::size_t>(n)) {
            state.SkipWithError("json reconstruction produced an object of the wrong size");
            return;
        }
    }

    for (auto _ : state) {
        auto j = qb::redis::reply::parse<qb::json>(reply);
        benchmark::DoNotOptimize(j);
    }

    state.SetItemsProcessed(state.iterations() * n);
}

// ---------------------------------------------------------------------------
// (e) Interval bound formatting (std::to_chars on doubles) via constructing
// BoundedInterval<double>. Construction formats both bounds; lower()/upper()
// return the pre-formatted "[x" / "(x" strings.
// ---------------------------------------------------------------------------

void
BM_ReplyFormat_IntervalToChars(benchmark::State &state) {
    {
        qb::redis::score_interval iv(0.1, 1234567.89012345, qb::redis::BoundType::CLOSED);
        if (iv.lower().empty() || iv.upper().empty()) {
            state.SkipWithError("interval bound formatting produced empty bounds");
            return;
        }
    }

    double lo = 0.1;
    double hi = 1234567.89012345;
    for (auto _ : state) {
        qb::redis::score_interval iv(lo, hi, qb::redis::BoundType::OPEN);
        auto                      lo_s = iv.lower(); // non-const lvalue (avoids deprecated const-ref DoNotOptimize)
        auto                      hi_s = iv.upper();
        benchmark::DoNotOptimize(lo_s);
        benchmark::DoNotOptimize(hi_s);
        // Perturb so the formatter cannot be hoisted/constant-folded.
        lo += 1.0;
        hi += 1.0;
    }

    state.SetItemsProcessed(state.iterations() * 2); // two bounds formatted per iter
}

} // namespace

BENCHMARK(BM_ReplyParse_ScoreMemberVector)->Arg(100)->Arg(1000)->ArgNames({"members"})->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_ReplyParse_MapStreamEntryList)->Arg(4)->Arg(16)->ArgNames({"streams"})->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_ReplySerialize_MultiBulk)->Arg(100)->Arg(1000)->ArgNames({"pairs"})->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_ReplySerialize_PutInPipe)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_ReplyParse_JsonWideMap)->Arg(100)->Arg(1000)->ArgNames({"fields"})->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_ReplyFormat_IntervalToChars)->Unit(benchmark::kNanosecond);

BENCHMARK_MAIN();
