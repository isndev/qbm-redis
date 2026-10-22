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
 *   (f) reply::parse<qb::redis::message> over a pub/sub `message` frame — what a
 *       consumer does for every message it receives; the struct carries a gap
 *       marker's `kind` and `gap` since QB-126b (104 -> 128 bytes).
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
#include <array>
#include <span>
#include <type_traits>
#include <variant>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <qb/system/allocator/pipe.h>

#include <qbm/redis/redis.h>
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
        for (int i = 0; i < n; ++i) {
            const auto &item = out[static_cast<std::size_t>(i)];
            if (item.score != static_cast<double>(i) + 0.5 || item.member != "member:" + std::to_string(i)) {
                state.SkipWithError("score_member value or order mismatch");
                return;
            }
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
        // Stream/field maps are unordered; entry lists retain the wire order.
        for (int s = 0; s < streams; ++s) {
            const auto stream = out.find("stream:" + std::to_string(s));
            if (stream == out.end() || stream->second.size() != static_cast<std::size_t>(per)) {
                state.SkipWithError("stream key or entry count mismatch");
                return;
            }
            for (int e = 0; e < per; ++e) {
                const auto &entry = stream->second[static_cast<std::size_t>(e)];
                const auto  field = entry.fields.find("field");
                const auto  seq   = entry.fields.find("seq");
                if (entry.id.timestamp != e + 1 || entry.id.sequence != 0 || entry.fields.size() != 2 || field == entry.fields.end()
                    || seq == entry.fields.end() || field->second != "value:" + std::to_string(e) || seq->second != std::to_string(e)) {
                    state.SkipWithError("stream entry ID, order or field value mismatch");
                    return;
                }
            }
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
        // Build the expected wire from the fixture specification, not args or
        // the production serializer. This benchmark emits bulk strings without
        // an array header; put_in_pipe below covers the complete command.
        std::string expected = "$4\r\nMSET\r\n";
        for (int i = 0; i < n; ++i) {
            const auto suffix = std::to_string(i);
            expected += "$" + std::to_string(4 + suffix.size()) + "\r\nkey:" + suffix + "\r\n";
            expected += "$" + std::to_string(6 + suffix.size()) + "\r\nvalue:" + suffix + "\r\n";
        }
        if (std::string_view{pipe.begin(), pipe.size()} != expected) {
            state.SkipWithError("multi-bulk argument order or exact wire bytes mismatch");
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
        constexpr std::string_view expected = "*6\r\n$4\r\nZADD\r\n$5\r\nmyset\r\n"
                                              "$3\r\n1.5\r\n$5\r\nalice\r\n$3\r\n2.5\r\n$3\r\nbob\r\n";
        if (std::string_view{pipe.begin(), pipe.size()} != expected) {
            state.SkipWithError("put_in_pipe count, order or exact wire bytes mismatch");
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

// All fixtures fit the signed wire domain accepted by both comparison roles.
// UINT64_MAX belongs to correctness tests: the old encoder emits a wrong value,
// so its throughput cannot be compared as equivalent work.
void
BM_ReplySerialize_JsonInteger(benchmark::State &state) {
    const auto  fixture = state.range(0);
    qb::json    input;
    std::string expected;
    switch (fixture) {
        case 0:
            input    = qb::json::number_integer_t{0};
            expected = "0";
            break;
        case 1:
            input    = std::numeric_limits<qb::json::number_integer_t>::min();
            expected = "-9223372036854775808";
            break;
        case 2:
            input    = qb::json::number_unsigned_t{9223372036854775807ULL};
            expected = "9223372036854775807";
            break;
        default:
            state.SkipWithError("unknown JSON integer fixture");
            return;
    }
    const auto                wire = "$" + std::to_string(expected.size()) + "\r\n" + expected + "\r\n";
    qb::allocator::pipe<char> pipe;
    qb::redis::to_redis_string(pipe, input);
    if (std::string_view{pipe.begin(), pipe.size()} != wire) {
        state.SkipWithError("JSON integer serialization changed exact RESP bytes");
        return;
    }
    for (auto _ : state) {
        pipe.reset();
        qb::redis::to_redis_string(pipe, input);
        benchmark::DoNotOptimize(pipe.begin());
        benchmark::DoNotOptimize(pipe.size());
        benchmark::ClobberMemory();
    }
    if (std::string_view{pipe.begin(), pipe.size()} != wire)
        state.SkipWithError("JSON integer serialization changed after timed loop");
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
        for (int i = 0; i < n; ++i) {
            const auto field = j.find("field:" + std::to_string(i));
            if (field == j.end()
                || (i % 2 == 0 ? !field->is_string() || *field != "value:" + std::to_string(i)
                               : field->type() != qb::json::value_t::number_integer || *field != i)) {
                state.SkipWithError("json reconstruction key, value or type mismatch");
                return;
            }
        }
    }

    for (auto _ : state) {
        auto j = qb::redis::reply::parse<qb::json>(reply);
        benchmark::DoNotOptimize(j);
    }

    state.SetItemsProcessed(state.iterations() * n);
}

// ---------------------------------------------------------------------------
// (f) parse<qb::redis::message> over a pub/sub `message` frame (Huly QB-126b).
// ---------------------------------------------------------------------------

Value
make_pubsub_message(std::size_t payload_bytes) {
    std::vector<std::unique_ptr<Value>> elems;
    elems.push_back(mk_bulk("message"));
    elems.push_back(mk_bulk("orders:{7}"));
    elems.push_back(mk_bulk(std::string(payload_bytes, 'p')));
    return make_array(std::move(elems));
}

void
BM_ReplyParse_PubSubMessage(benchmark::State &state) {
    const auto  bytes = static_cast<std::size_t>(state.range(0));
    const Value reply = make_pubsub_message(bytes);

    {
        const auto m = qb::redis::reply::parse<qb::redis::message>(reply);
        if (m.channel != "orders:{7}" || m.payload.size() != bytes || !m.pattern.empty()) {
            state.SkipWithError("message decode produced the wrong channel, payload or pattern");
            return;
        }
    }

    for (auto _ : state) {
        auto m = qb::redis::reply::parse<qb::redis::message>(reply);
        benchmark::DoNotOptimize(m);
    }

    state.SetItemsProcessed(state.iterations());
}

// ---------------------------------------------------------------------------
// (e) Interval bound formatting (std::to_chars on doubles) via constructing
// BoundedInterval<double>. Construction formats both bounds; lower()/upper()
// return the pre-formatted "[x" / "(x" strings.
// ---------------------------------------------------------------------------

void
BM_ReplyFormat_IntervalToChars(benchmark::State &state) {
    {
        qb::redis::score_interval iv(0.1, 1234567.89012345, qb::redis::BoundType::OPEN);
        // Same OPEN mode as the measured constructor, including its 17-digit
        // decimal rendering and exclusive-bound marker.
        if (iv.lower() != "(0.10000000000000001" || iv.upper() != "(1234567.89012345") {
            state.SkipWithError("interval exact bound bytes mismatch");
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

// A normal TReply dispatch: parse -> one non-throwing callback -> ownership transfer.
// Raw Value construction, reply disposal and all correctness checks are pause-timed.
// The callback's one count/store is common work on control and candidate.
static void
BM_TReply_SuccessDispatch(benchmark::State &state) {
    using Reply                                               = qb::redis::Reply<long long>;
    constexpr long long                              values[] = {123, -7, 0, 42};
    Reply                                            got;
    std::int64_t                                     calls    = 0;
    auto                                             callback = std::function<void(Reply)>([&](Reply r) {
        ++calls;
        got = std::move(r);
    });
    qb::redis::TReply<decltype(callback), long long> handler(std::move(callback));

    {
        auto        raw      = mk_int(values[0]);
        const auto *identity = raw.get();
        handler(std::move(raw));
        if (calls != 1 || !got.ok() || got.value() != values[0] || !got.error().empty() || got.raw().get() != identity) {
            state.SkipWithError("TReply success preflight value, count or raw ownership mismatch");
            return;
        }
    }
    got                        = Reply{};
    calls                      = 0;
    std::int64_t  completed    = 0;
    std::uint64_t observed_sum = 0;
    std::uint64_t expected_sum = 0;
    for (auto _ : state) {
        state.PauseTiming();
        if (completed == std::numeric_limits<std::int64_t>::max()) {
            state.SkipWithError("TReply cumulative completion count overflow");
            return;
        }
        got                    = Reply{};
        const auto  expected   = values[static_cast<std::size_t>(completed) % std::size(values)];
        auto        raw        = mk_int(expected);
        const auto *identity   = raw.get();
        auto       *opaque_raw = raw.get();
        benchmark::DoNotOptimize(opaque_raw);
        state.ResumeTiming();
        handler(std::move(raw));
        state.PauseTiming();
        if (calls != completed + 1 || !got.ok() || got.value() != expected || !got.error().empty() || got.raw().get() != identity) {
            state.SkipWithError("measured TReply success value, count or raw ownership mismatch");
            return;
        }
        ++completed;
        observed_sum += static_cast<std::uint64_t>(got.value());
        expected_sum += static_cast<std::uint64_t>(expected);
        state.ResumeTiming();
    }
    if (completed != state.iterations() || calls != completed || observed_sum != expected_sum) {
        state.SkipWithError("incomplete cumulative TReply success workload");
        return;
    }
    state.SetItemsProcessed(completed);
}

// Both witnesses use the same preparation, callback and ownership checks. The
// empty witness dispatches while paused; its timed interval contains only a
// compiler barrier. Compare per-batch times before normalizing by reply count:
// timer-boundary cost is not a measured dispatch and must not be subtracted as
// though it were an independently additive codec cost.
template <bool TimedDispatch>
void
treply_success_batch(benchmark::State &state) {
    using Reply                  = qb::redis::Reply<long long>;
    constexpr long long values[] = {123, -7, 0, 42};
    const auto          batch    = static_cast<std::size_t>(state.range(0));
    if (batch != 1 && batch != 64 && batch != 128) {
        state.SkipWithError("unsupported TReply witness batch size");
        return;
    }

    std::vector<std::unique_ptr<Value>>              raw(batch);
    std::vector<const Value *>                       identities(batch);
    std::vector<long long>                           expected(batch);
    std::vector<Reply>                               got(batch);
    std::size_t                                      calls    = 0;
    auto                                             callback = std::function<void(Reply)>([&](Reply r) {
        // A duplicate callback must fail the count oracle without overflowing
        // the result buffer. Keep every normal Reply alive until batch validation.
        if (calls < batch)
            got[calls] = std::move(r);
        ++calls;
    });
    qb::redis::TReply<decltype(callback), long long> handler(std::move(callback));

    const auto prepare = [&](std::int64_t completed) {
        calls = 0;
        for (std::size_t i = 0; i < batch; ++i) {
            expected[i]      = values[(static_cast<std::size_t>(completed) + i) % std::size(values)];
            raw[i]           = mk_int(expected[i]);
            identities[i]    = raw[i].get();
            auto *opaque_raw = raw[i].get();
            benchmark::DoNotOptimize(opaque_raw);
        }
        // Each allocated Value escapes before dispatch; the optimizer cannot
        // substitute the four known integers for all callbacks in a batch.
        benchmark::ClobberMemory();
    };
    const auto dispatch = [&] {
        for (auto &value : raw)
            handler(std::move(value));
    };
    const auto valid = [&] {
        if (calls != batch)
            return false;
        for (std::size_t i = 0; i < batch; ++i) {
            if (raw[i] || !got[i].ok() || got[i].value() != expected[i] || !got[i].error().empty() || got[i].raw().get() != identities[i]
                || !got[i].raw()->is_integer() || got[i].raw()->as_integer().value != expected[i])
                return false;
        }
        return true;
    };
    const auto dispose = [&] {
        for (auto &reply : got)
            reply = Reply{};
    };

    prepare(0);
    dispatch();
    if (!valid()) {
        state.SkipWithError("TReply batch preflight value, count or raw ownership mismatch");
        return;
    }
    dispose();

    std::int64_t  completed    = 0;
    std::uint64_t observed_sum = 0;
    std::uint64_t expected_sum = 0;
    for (auto _ : state) {
        state.PauseTiming();
        if (completed > std::numeric_limits<std::int64_t>::max() - static_cast<std::int64_t>(batch)) {
            state.SkipWithError("TReply batch cumulative completion count overflow");
            return;
        }
        prepare(completed);
        state.ResumeTiming();
        if constexpr (TimedDispatch)
            dispatch();
        benchmark::ClobberMemory();
        state.PauseTiming();
        if constexpr (!TimedDispatch)
            dispatch();
        if (!valid()) {
            state.SkipWithError("TReply batch value, count or raw ownership mismatch");
            return;
        }
        for (std::size_t i = 0; i < batch; ++i) {
            observed_sum += static_cast<std::uint64_t>(got[i].value());
            expected_sum += static_cast<std::uint64_t>(expected[i]);
        }
        completed += static_cast<std::int64_t>(batch);
        dispose();
        state.ResumeTiming();
    }
    if (completed / static_cast<std::int64_t>(batch) != state.iterations() || observed_sum != expected_sum) {
        state.SkipWithError("incomplete cumulative TReply batch workload");
        return;
    }
    if constexpr (TimedDispatch)
        state.SetItemsProcessed(completed);
    else
        state.SetItemsProcessed(state.iterations()); // empty timed intervals, not dispatches
}

void
BM_TReply_TimerBoundaryEmpty(benchmark::State &state) {
    treply_success_batch<false>(state);
}

void
BM_TReply_SuccessDispatchBatch(benchmark::State &state) {
    treply_success_batch<true>(state);
}

// Public mixins over command-reply-types.cpp's synchronous RESP2 seam; frame buffers are reused.
class AggregationClient
    : public qb::redis::key_commands<AggregationClient>
    , public qb::redis::set_commands<AggregationClient>
    , public qb::redis::hash_commands<AggregationClient>
    , public qb::redis::server_commands<AggregationClient> {
public:
    std::span<const std::string>             replies;
    std::array<qb::allocator::pipe<char>, 3> frames;
    std::size_t                              next{};
    std::uint64_t                            commands{}, coro_callbacks{};
    bool                                     failed{};
    template <typename Ret, typename Func, typename... Args>
    AggregationClient &
    command(Func &&func, qb::redis::detail::known_verb const verb, Args &&...args) {
        const auto name  = verb.name();
        const auto index = next++;
        ++commands;
        if (index >= replies.size()) {
            failed = true;
            return *this;
        }
        auto &frame = frames[index];
        frame.reset();
        qb::redis::put_in_pipe(frame, name, std::forward<Args>(args)...);
        auto parsed = qb::redis::parser::parse(replies[index]);
        if (!parsed.has_value()) {
            failed = true;
            return *this;
        }
        qb::redis::TReply<Func, Ret> handler{std::forward<Func>(func)};
        handler(std::make_unique<Value>(std::move(*parsed)));
        return *this;
    }
    // Execute the TIME wrapper's operation synchronously: no coroutine frame/scheduler.
    template <typename T, typename Operation>
    qb::redis::Reply<T>
    make_coro_command(Operation &&operation) {
        qb::redis::Reply<T> result;
        std::forward<Operation>(operation)([&](qb::redis::Reply<T> &&reply) {
            result = std::move(reply);
            ++coro_callbacks;
        });
        return result;
    }
};

void
BM_CommandSuccessfulAggregation(benchmark::State &state) {
    using Strings      = std::vector<std::string>;
    using Pairs        = std::vector<std::pair<std::string, std::string>>;
    using Time         = std::pair<long long, long long>;
    using Scan         = qb::redis::scan<>;
    const auto command = state.range(0);
    if (command < 0 || command > 4) {
        state.SkipWithError("unknown aggregation fixture");
        return;
    }
    std::vector<std::string> scripts[] = {
        {"*2\r\n$1\r\n7\r\n*2\r\n$1\r\na\r\n$1\r\nb\r\n", "*2\r\n$1\r\n0\r\n*1\r\n$1\r\nc\r\n"},
        {"*1\r\n$1\r\na\r\n", "*1\r\n$1\r\nb\r\n", "*1\r\n$1\r\nc\r\n"},
        {"*4\r\n$9\r\nmaxmemory\r\n$1\r\n0\r\n$7\r\ntimeout\r\n$1\r\n5\r\n"},
        {"*2\r\n$2\r\n42\r\n$1\r\n7\r\n"}
    };
    const std::vector<std::string_view> expected_frames[] = {
        {"*4\r\n$4\r\nSCAN\r\n$1\r\n0\r\n$5\r\nMATCH\r\n$1\r\n*\r\n", "*4\r\n$4\r\nSCAN\r\n$1\r\n7\r\n$5\r\nMATCH\r\n$1\r\n*\r\n"},
        {"*7\r\n$5\r\nSSCAN\r\n$3\r\nset\r\n$1\r\n0\r\n$5\r\nMATCH\r\n$1\r\n*\r\n$5\r\nCOUNT\r\n$3\r\n100\r\n",
         "*7\r\n$5\r\nSSCAN\r\n$3\r\nset\r\n$1\r\n7\r\n$5\r\nMATCH\r\n$1\r\n*\r\n$5\r\nCOUNT\r\n$3\r\n100\r\n"},
        {"*2\r\n$5\r\nHVALS\r\n$5\r\nfirst\r\n", "*2\r\n$5\r\nHVALS\r\n$6\r\nmiddle\r\n", "*2\r\n$5\r\nHVALS\r\n$4\r\nlast\r\n"},
        {"*3\r\n$6\r\nCONFIG\r\n$3\r\nGET\r\n$1\r\n*\r\n"},
        {"*1\r\n$4\r\nTIME\r\n"}
    };
    AggregationClient client;
    auto             &script = scripts[command <= 1 ? 0 : command - 1];
    auto             *opaque = script.data();
    benchmark::DoNotOptimize(opaque);
    benchmark::ClobberMemory();
    client.replies = script;
    const Strings     keys{"first", "middle", "last"}, expected_values{"a", "b", "c"};
    const Pairs       expected_pairs{{"maxmemory", "0"}, {"timeout", "5"}};
    const std::string pattern{"*"}, key{"set"};
    std::variant<qb::redis::Reply<Scan>, qb::redis::Reply<Strings>, qb::redis::Reply<Pairs>, qb::redis::Reply<Time>> result;
    std::uint64_t callbacks = 0, completed = 0;
    auto          capture = [&](auto &&reply) {
        result = std::move(reply);
        ++callbacks;
    };
    const auto run = [&] {
        client.next = 0; // allocation-free script reset; cumulative counts are retained
        if (command == 0)
            client.scan(capture, pattern);
        else if (command == 1)
            client.sscan(capture, key, pattern);
        else if (command == 2)
            client.hvals(capture, keys);
        else if (command == 3)
            client.config_get(capture, pattern);
        else
            capture(client.time());
    };
    const auto valid = [&](std::uint64_t operations) {
        if (operations > std::numeric_limits<std::uint64_t>::max() / script.size() || client.failed || client.next != script.size()
            || client.commands != operations * script.size() || callbacks != operations
            || client.coro_callbacks != (command == 4 ? operations : 0))
            return false;
        for (std::size_t i = 0; i < script.size(); ++i)
            if (std::string_view{client.frames[i].begin(), client.frames[i].size()} != expected_frames[command][i])
                return false;
        return std::visit(
            [&](const auto &reply) {
                if (!reply.ok() || !reply.error().empty())
                    return false;
                using T = std::decay_t<decltype(reply.result())>;
                if constexpr (std::is_same_v<T, Scan>)
                    return command < 2 && reply.result().cursor == 0 && reply.result().items == expected_values;
                else if constexpr (std::is_same_v<T, Strings>)
                    return command == 2 && reply.result() == expected_values;
                else if constexpr (std::is_same_v<T, Pairs>)
                    return command == 3 && reply.result() == expected_pairs;
                else
                    return command == 4 && reply.result() == Time{42, 7};
            },
            result);
    };
    run();
    if (!valid(1)) {
        state.SkipWithError("command aggregation preflight value, frame or count mismatch");
        return;
    }
    client.commands = client.coro_callbacks = callbacks = 0;
    state.SetLabel("CPU command serialization / RESP parsing / aggregation; no network or coroutine");
    for (auto _ : state) {
        run();
        benchmark::DoNotOptimize(result);
        ++completed;
    }
    // Exact final content + cumulative counts. No claim about transient content in earlier iterations.
    if (completed != static_cast<std::uint64_t>(state.iterations()) || !valid(completed)) {
        state.SkipWithError("command aggregation post-loop value, frame or cumulative count mismatch");
        return;
    }
    state.SetItemsProcessed(state.iterations());
}

} // namespace

BENCHMARK(BM_ReplyParse_ScoreMemberVector)->Arg(100)->Arg(1000)->ArgNames({"members"})->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_ReplyParse_MapStreamEntryList)->Arg(4)->Arg(16)->ArgNames({"streams"})->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_ReplySerialize_MultiBulk)->Arg(100)->Arg(1000)->ArgNames({"pairs"})->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_ReplySerialize_PutInPipe)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_ReplySerialize_JsonInteger)->Arg(0)->Arg(1)->Arg(2)->ArgNames({"fixture"})->Unit(benchmark::kNanosecond);
BENCHMARK(BM_ReplyParse_JsonWideMap)->Arg(100)->Arg(1000)->ArgNames({"fields"})->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_ReplyFormat_IntervalToChars)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_ReplyParse_PubSubMessage)->Arg(16)->Arg(256)->ArgNames({"payload_bytes"})->Unit(benchmark::kNanosecond);

BENCHMARK(BM_TReply_SuccessDispatch)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_TReply_TimerBoundaryEmpty)->Arg(1)->Arg(64)->Arg(128)->ArgNames({"replies"})->Unit(benchmark::kNanosecond);
BENCHMARK(BM_TReply_SuccessDispatchBatch)->Arg(64)->Arg(128)->ArgNames({"replies"})->Unit(benchmark::kNanosecond)->UseRealTime();

BENCHMARK(BM_CommandSuccessfulAggregation)->Arg(0)->Arg(1)->Arg(2)->Arg(3)->Arg(4)->ArgNames({"command"});

BENCHMARK_MAIN();
