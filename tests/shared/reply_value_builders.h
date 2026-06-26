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
 * @file reply_value_builders.h
 * @brief Header-only `parser::Value`-tree builders shared by the reply unit tests
 *        and the reply-codec benchmark.
 *
 * Hoisted from the top of the legacy `test-reply.cpp` so the deserialization
 * (`reply-parse`), serialization (`reply-serialize`), wrapper (`reply-wrapper`),
 * extractor (`reply-extractors`) and json-decode (`reply-json-decode`) unit TUs,
 * plus `benchmark/parser/reply-codec.bench.cpp`, all construct the exact same
 * RESP2/RESP3 `Value` shapes from a single source of truth.
 *
 * Pure logic: no daemon, no event loop. Builds `parser::Value` trees by hand and
 * exposes `do_parse<T>()` as a thin wrapper over `qb::redis::reply::parse<T>`.
 */

#ifndef QBM_REDIS_TESTS_SHARED_REPLY_VALUE_BUILDERS_H
#define QBM_REDIS_TESTS_SHARED_REPLY_VALUE_BUILDERS_H

#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "../../reply.h"

namespace qb::redis::test {

using qb::redis::parser::Array;
using qb::redis::parser::BulkString;
using qb::redis::parser::Double;
using qb::redis::parser::Integer;
using qb::redis::parser::Map;
using qb::redis::parser::Null;
using qb::redis::parser::Push;
using qb::redis::parser::Set;
using qb::redis::parser::SimpleString;
using qb::redis::parser::Value;

/// Wrap a BulkString in a heap Value owned by a unique_ptr (the element/entry
/// representation used everywhere by Array/Map/Set/Push).
inline std::unique_ptr<Value>
mk_bulk(std::string s) {
    return std::make_unique<Value>(Value(BulkString{std::move(s)}));
}

/// Wrap a SimpleString in a heap Value.
inline std::unique_ptr<Value>
mk_simple(std::string s) {
    return std::make_unique<Value>(Value(SimpleString{std::move(s)}));
}

/// Wrap an Integer in a heap Value.
inline std::unique_ptr<Value>
mk_int(int64_t i) {
    return std::make_unique<Value>(Value(Integer{i}));
}

/// Wrap a Double in a heap Value.
inline std::unique_ptr<Value>
mk_dbl(double d) {
    return std::make_unique<Value>(Value(Double{d}));
}

/// Build a Value holding an Array from a list of element makers.
inline Value
make_array(std::vector<std::unique_ptr<Value>> elems) {
    Array arr;
    arr.elements = std::move(elems);
    return Value(std::move(arr));
}

/// Build a Value holding a Set from a list of element makers.
inline Value
make_set(std::vector<std::unique_ptr<Value>> elems) {
    Set s;
    s.elements = std::move(elems);
    return Value(std::move(s));
}

/// Build a Value holding a Push from a list of element makers.
inline Value
make_push(std::vector<std::unique_ptr<Value>> elems) {
    Push p;
    p.elements = std::move(elems);
    return Value(std::move(p));
}

/// Build a Value holding a Map from (key, value) pairs.
inline Value
make_map(std::vector<std::pair<std::unique_ptr<Value>, std::unique_ptr<Value>>> entries) {
    Map m;
    m.entries = std::move(entries);
    return Value(std::move(m));
}

/// Build a single stream entry Value: [ id, [field, value, ...] ].
inline std::unique_ptr<Value>
make_stream_entry_value(std::string id, std::vector<std::pair<std::string, std::string>> fv) {
    std::vector<std::unique_ptr<Value>> fields;
    for (auto &p : fv) {
        fields.push_back(mk_bulk(p.first));
        fields.push_back(mk_bulk(p.second));
    }
    std::vector<std::unique_ptr<Value>> entry;
    entry.push_back(mk_bulk(std::move(id)));
    entry.push_back(std::make_unique<Value>(make_array(std::move(fields))));
    return std::make_unique<Value>(make_array(std::move(entry)));
}

/// Thin wrapper over qb::redis::reply::parse<T> for terse call sites.
template <typename T>
T
do_parse(const Value &v) {
    return qb::redis::reply::parse<T>(v);
}

} // namespace qb::redis::test

#endif // QBM_REDIS_TESTS_SHARED_REPLY_VALUE_BUILDERS_H
