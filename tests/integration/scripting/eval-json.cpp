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
 * @file integration/scripting/eval-json.cpp
 * @brief End-to-end eval<qb::json> + cjson round-trips through a live server.
 *
 * Integration tier (`REQUIRES live`, needs a Lua/cjson-capable redis). Runs in both
 * RESP2 and RESP3 via the shared @ref qb::redis::test::ProtocolModesTestBase fixture.
 *
 * Migrated from test-json-parse.cpp. The pure client-side `{...}`-auto-detect /
 * RESP->qb::json reconstruction half is ported separately to the daemon-free
 * unit/reply/reply-json-decode.cpp; this file keeps ONLY the genuinely server-dependent
 * eval<qb::json> + cjson.encode round-trips that prove the end-to-end scripting decode.
 * The two trailing EVAL_JSON_* smoke duplicates were deleted (subsets of the bodies
 * below). The per-test busy-spins were replaced by run_coro_test_until (watchdog);
 * the local try/catch RUN_CORO_TEST macro is dropped — the watchdog already prevents a
 * never-completing await from hanging the loop, and a thrown qb::json exception is now a
 * real failure (ADD_FAILURE via the awaiting body), not silently swallowed-as-green.
 */

#include <gtest/gtest.h>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <qb/json.h>
#include "../../shared/redis_integration_fixture.h"
#include "../redis.h"

using namespace qb::io;
using namespace std::chrono;

namespace {

using qb::redis::test::ProtocolModesTestBase;

class EvalJsonTest : public ProtocolModesTestBase {};

INSTANTIATE_PROTOCOL_MODES(EvalJsonTest);

// ---------------------------------------------------------------------------
// GET of a JSON-encoded string → the {...}-auto-detector reconstructs an object.
// ---------------------------------------------------------------------------
TEST_P(EvalJsonTest, JSON_STRING_TO_OBJECT) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key      = protocol_key("json_string");
        const std::string json_str = R"({"name":"John","age":30,"city":"New York"})";

        EXPECT_TRUE((co_await redis.set(key, json_str)).ok());

        auto reply = co_await redis.eval<qb::json>("return redis.call('GET', KEYS[1])", {key});
        EXPECT_TRUE(reply.ok()) << reply.error();
        const auto &result = reply.result();
        EXPECT_TRUE(result.is_object());
        EXPECT_EQ(result["name"], "John");
        EXPECT_EQ(result["age"], 30);
        EXPECT_EQ(result["city"], "New York");
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// ---------------------------------------------------------------------------
// tonumber → REDIS_REPLY_INTEGER → qb::json number.
// ---------------------------------------------------------------------------
TEST_P(EvalJsonTest, JSON_INTEGER) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("json_integer");
        EXPECT_TRUE((co_await redis.set(key, "42")).ok());

        auto reply = co_await redis.eval<qb::json>("return tonumber(redis.call('GET', KEYS[1]))", {key});
        EXPECT_TRUE(reply.ok()) << reply.error();
        const auto &result = reply.result();
        EXPECT_TRUE(result.is_number());
        EXPECT_EQ(result, 42);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// ---------------------------------------------------------------------------
// LRANGE → Redis array → qb::json array (order preserved; LPUSH prepends).
// ---------------------------------------------------------------------------
TEST_P(EvalJsonTest, JSON_ARRAY) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("json_array");
        EXPECT_TRUE((co_await redis.lpush(key, "item1", "item2", "item3")).ok());

        auto reply = co_await redis.eval<qb::json>("return redis.call('LRANGE', KEYS[1], 0, -1)", {key});
        EXPECT_TRUE(reply.ok()) << reply.error();
        const auto &result = reply.result();
        EXPECT_TRUE(result.is_array());
        if (!(result.size() == 3u)) {
            ADD_FAILURE() << "precondition failed: result.size() == 3u";
            co_return;
        }
        EXPECT_EQ(result[0], "item3"); // LPUSH prepends
        EXPECT_EQ(result[1], "item2");
        EXPECT_EQ(result[2], "item1");
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// ---------------------------------------------------------------------------
// HGETALL + Lua loop + cjson.encode → qb::json object (values stay strings).
// ---------------------------------------------------------------------------
TEST_P(EvalJsonTest, JSON_HASH_VIA_CJSON) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string key = protocol_key("json_hash");
        EXPECT_TRUE((co_await redis.hset(key, "name", "Alice")).ok());
        EXPECT_TRUE((co_await redis.hset(key, "age", "25")).ok());
        EXPECT_TRUE((co_await redis.hset(key, "city", "London")).ok());

        const std::string script = R"(
            local h = redis.call('HGETALL', KEYS[1])
            local obj = {}
            for i = 1, #h, 2 do obj[h[i]] = h[i+1] end
            return cjson.encode(obj)
        )";

        auto reply = co_await redis.eval<qb::json>(script, {key});
        EXPECT_TRUE(reply.ok()) << reply.error();
        const auto &result = reply.result();
        EXPECT_TRUE(result.is_object());
        EXPECT_EQ(result["name"], "Alice");
        EXPECT_EQ(result["age"], "25"); // stored/returned as string
        EXPECT_EQ(result["city"], "London");
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// ---------------------------------------------------------------------------
// Complex nested structure built in Lua and cjson.encode-d.
// ---------------------------------------------------------------------------
TEST_P(EvalJsonTest, JSON_COMPLEX_NESTED) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        const std::string script = R"(
            local result = {}
            result.string  = "Hello World"
            result.number  = 42
            result.boolean = true
            result.array   = {"one", "two", "three"}
            result.object  = {key1 = "value1", key2 = "value2"}
            result.nested  = {array = {1, 2, 3}, sub = {nested = "value"}}
            return cjson.encode(result)
        )";

        auto reply = co_await redis.eval<qb::json>(script);
        EXPECT_TRUE(reply.ok()) << reply.error();
        const auto &result = reply.result();
        EXPECT_TRUE(result.is_object());
        EXPECT_EQ(result["string"], "Hello World");
        EXPECT_EQ(result["number"], 42);
        EXPECT_EQ(result["boolean"], true);

        EXPECT_TRUE(result["array"].is_array());
        if (!(result["array"].size() == 3u)) {
            ADD_FAILURE() << "precondition failed: result[\"array\"].size() == 3u";
            co_return;
        }
        EXPECT_EQ(result["array"][0], "one");
        EXPECT_EQ(result["array"][2], "three");

        EXPECT_TRUE(result["object"].is_object());
        EXPECT_EQ(result["object"]["key1"], "value1");
        EXPECT_EQ(result["object"]["key2"], "value2");

        EXPECT_TRUE(result["nested"].is_object());
        EXPECT_TRUE(result["nested"]["array"].is_array());
        EXPECT_EQ(result["nested"]["array"][1], 2);
        EXPECT_EQ(result["nested"]["sub"]["nested"], "value");
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

// ---------------------------------------------------------------------------
// cjson.encode of a flat object directly (no GET round-trip) → qb::json object.
// ---------------------------------------------------------------------------
TEST_P(EvalJsonTest, JSON_CJSON_MIXED_SCALARS) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto reply = co_await redis.eval<qb::json>(R"(return cjson.encode({a = 1, b = "two", c = true}))");
        EXPECT_TRUE(reply.ok()) << reply.error();
        const auto &result = reply.result();
        EXPECT_TRUE(result.is_object());
        EXPECT_EQ(result["a"], 1);
        EXPECT_EQ(result["b"], "two");
        EXPECT_EQ(result["c"], true);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    run_coro_test_until(completed);
}

} // namespace
