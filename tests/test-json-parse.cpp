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
#include <qb/json.h>
#include "../redis.h"
#include "protocol_test_common.h"

using namespace qb::io;
using namespace std::chrono;

// ============================================================================
// Fixture: all tests run in both RESP2 and RESP3
// ============================================================================

class JsonParseProtocolModesTest : public ProtocolModesTestBase {};

INSTANTIATE_PROTOCOL_MODES(JsonParseProtocolModesTest);

// ============================================================================
// Helper macro: runs a coroutine test body.
// Wraps the body in try-catch so that any uncaught exception in the coroutine
// (e.g. qb::json::exception from accessing a wrong-type result) does NOT
// cause _completed to stay false and hang the event loop forever.
// ============================================================================
#define RUN_CORO_TEST(body)                                                    \
    do {                                                                       \
        bool _completed = false;                                               \
        auto _task      = [this, &_completed]() -> qb::io::async::task<void> { \
            try {                                                              \
                body                                                           \
            } catch (const std::exception &_ex) {                              \
                ADD_FAILURE() << "Unexpected exception: " << _ex.what();       \
            } catch (...) {                                                    \
                ADD_FAILURE() << "Unexpected unknown exception";               \
            }                                                                  \
            _completed = true;                                                 \
        };                                                                     \
        qb::io::async::coro_scheduler().spawn(_task());                        \
        while (!_completed)                                                    \
            qb::io::async::run(EVRUN_NOWAIT);                                  \
    } while (false)

// ============================================================================
// Tests
// ============================================================================

// Test: GET returns a JSON-encoded string → parsed as object
TEST_P(JsonParseProtocolModesTest, CORO_JSON_PARSE_STRING) {
    RUN_CORO_TEST({
        PROTOCOL_ENSURE_RESP3_VAR(_completed);
        const std::string key      = protocol_key("string");
        const std::string json_str = R"({"name":"John","age":30,"city":"New York"})";

        auto set_reply = co_await redis.set(key, json_str);
        EXPECT_TRUE(set_reply.ok());

        // eval returns the raw JSON string; our parser auto-detects {…} and parses it
        auto eval_reply = co_await redis.eval<qb::json>("return redis.call('GET', KEYS[1])", {key});
        EXPECT_TRUE(eval_reply.ok());

        const auto &result = eval_reply.result();
        EXPECT_TRUE(result.is_object());
        EXPECT_EQ(result["name"], "John");
        EXPECT_EQ(result["age"], 30);
        EXPECT_EQ(result["city"], "New York");
    });
}

// Test: eval returns an integer via tonumber → parsed as JSON number
TEST_P(JsonParseProtocolModesTest, CORO_JSON_PARSE_INTEGER) {
    RUN_CORO_TEST({
        PROTOCOL_ENSURE_RESP3_VAR(_completed);
        const std::string key = protocol_key("integer");

        auto set_reply = co_await redis.set(key, "42");
        EXPECT_TRUE(set_reply.ok());

        // tonumber returns a Lua number, which Redis encodes as REDIS_REPLY_INTEGER
        auto eval_reply = co_await redis.eval<qb::json>("return tonumber(redis.call('GET', KEYS[1]))", {key});
        EXPECT_TRUE(eval_reply.ok());

        const auto &result = eval_reply.result();
        EXPECT_TRUE(result.is_number());
        EXPECT_EQ(result, 42);
    });
}

// Test: LRANGE returns a Redis array → parsed as JSON array
TEST_P(JsonParseProtocolModesTest, CORO_JSON_PARSE_ARRAY) {
    RUN_CORO_TEST({
        PROTOCOL_ENSURE_RESP3_VAR(_completed);
        const std::string key = protocol_key("array");

        (void) co_await redis.lpush(key, "item1", "item2", "item3");

        auto eval_reply = co_await redis.eval<qb::json>("return redis.call('LRANGE', KEYS[1], 0, -1)", {key});
        EXPECT_TRUE(eval_reply.ok());

        const auto &result = eval_reply.result();
        EXPECT_TRUE(result.is_array());
        EXPECT_EQ(result.size(), 3u);
        EXPECT_EQ(result[0], "item3"); // LPUSH prepends
        EXPECT_EQ(result[1], "item2");
        EXPECT_EQ(result[2], "item1");
    });
}

// Test: HGETALL encoded via cjson → parsed as JSON object
TEST_P(JsonParseProtocolModesTest, CORO_JSON_PARSE_HASH) {
    RUN_CORO_TEST({
        PROTOCOL_ENSURE_RESP3_VAR(_completed);
        const std::string key = protocol_key("hash");

        (void) co_await redis.hset(key, "name", "Alice");
        (void) co_await redis.hset(key, "age", "25");
        (void) co_await redis.hset(key, "city", "London");

        // HGETALL returns flat [k, v, k, v, …] — use cjson to get a proper object
        const std::string script = R"(
            local h = redis.call('HGETALL', KEYS[1])
            local obj = {}
            for i = 1, #h, 2 do obj[h[i]] = h[i+1] end
            return cjson.encode(obj)
        )";

        auto eval_reply = co_await redis.eval<qb::json>(script, {key});
        EXPECT_TRUE(eval_reply.ok());

        const auto &result = eval_reply.result();
        EXPECT_TRUE(result.is_object());
        EXPECT_EQ(result["name"], "Alice");
        EXPECT_EQ(result["age"], "25"); // stored as string
        EXPECT_EQ(result["city"], "London");
    });
}

// Test: complex nested structure via cjson.encode
TEST_P(JsonParseProtocolModesTest, CORO_JSON_PARSE_COMPLEX) {
    RUN_CORO_TEST({
        PROTOCOL_ENSURE_RESP3_VAR(_completed);
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

        auto eval_reply = co_await redis.eval<qb::json>(script);
        EXPECT_TRUE(eval_reply.ok());

        const auto &result = eval_reply.result();
        EXPECT_TRUE(result.is_object());
        EXPECT_EQ(result["string"], "Hello World");
        EXPECT_EQ(result["number"], 42);
        EXPECT_EQ(result["boolean"], true);

        EXPECT_TRUE(result["array"].is_array());
        EXPECT_EQ(result["array"].size(), 3u);
        EXPECT_EQ(result["array"][0], "one");

        EXPECT_TRUE(result["object"].is_object());
        EXPECT_EQ(result["object"]["key1"], "value1");

        EXPECT_TRUE(result["nested"].is_object());
        EXPECT_TRUE(result["nested"]["array"].is_array());
        EXPECT_EQ(result["nested"]["sub"]["nested"], "value");
    });
}

TEST_P(JsonParseProtocolModesTest, EVAL_JSON_OBJECT) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        const std::string script = R"(
            return cjson.encode({a = 1, b = "two", c = true})
        )";
        auto              r      = co_await redis.eval<qb::json>(script);
        EXPECT_TRUE(r.ok()) << r.error();
        if (r.ok()) {
            const auto &j = r.result();
            EXPECT_TRUE(j.is_object());
            EXPECT_EQ(j["a"], 1);
            EXPECT_EQ(j["b"], "two");
            EXPECT_EQ(j["c"], true);
        }
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(JsonParseProtocolModesTest, EVAL_JSON_STRING) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k = protocol_key("json_str");
        (void) co_await redis.set(k, R"({"x":42})");
        auto r = co_await redis.eval<qb::json>("return redis.call('GET', KEYS[1])", {k});
        EXPECT_TRUE(r.ok()) << r.error();
        if (r.ok()) {
            const auto &j = r.result();
            EXPECT_TRUE(j.is_object());
            EXPECT_EQ(j["x"], 42);
        }
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

int
main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
