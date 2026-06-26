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
 * @file qbm/redis/tests/unit/parser/resp-codec.cpp
 * @brief Unit tests for the RESP2/RESP3 wire codec: the streaming parser
 *        (@ref qb::redis::parser::RespParser / one-shot @ref qb::redis::parser::parse),
 *        the matching @ref qb::redis::parser::Serializer / @ref qb::redis::parser::CommandBuilder,
 *        and the thin reply-decode slice that round-trips through the codec
 *        (@c reply::parse<scan<>> / @c parse<double>).
 *
 * Consolidated from the legacy `test-parser.cpp` (minus PerformanceTest/StressTest,
 * which moved to the benchmark tier, and minus the buffer/types aspects which
 * split into resp-buffer.cpp / resp-types.cpp) MERGED with the serializer and
 * parser-state cases of `test-parser-units.cpp`. Strengthened per the migration
 * spec: aggregate asserts now check element values/order, the attribute value is
 * verified, and overflow / nesting-depth / fault-idempotency cases were added.
 *
 * Pure logic, no daemon, no event loop, no RESOURCE_LOCK.
 *
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2026 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)
 * @ingroup Redis
 */

#include <cmath>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <gtest/gtest.h>

#include "../parser/parser.h"
#include "../parser/serializer.h"
#include "../reply.h"
#include "../../shared/resp_corpus.h"

using namespace qb::redis::parser;
namespace corpus = qb::redis::test::corpus;

// ============================================================================
// RESP2 scalar decode
// ============================================================================

class Resp2ParserTest : public ::testing::Test {
protected:
    void
    SetUp() override {
        config.protocol_version = ProtocolVersion::RESP2;
    }
    ParserConfig config;
};

TEST_F(Resp2ParserTest, SimpleString_OK) {
    auto result = parse("+OK\r\n", config);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->is_simple_string());
    EXPECT_EQ(result->as_simple_string().value, "OK");
}

TEST_F(Resp2ParserTest, SimpleString_Empty) {
    auto result = parse("+\r\n", config);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->is_simple_string());
    EXPECT_TRUE(result->as_string_view().empty());
}

TEST_F(Resp2ParserTest, SimpleString_WithSpaces) {
    auto result = parse("+Hello World\r\n", config);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->as_string_view(), "Hello World");
}

TEST_F(Resp2ParserTest, SimpleError_PrefixAndMessage) {
    auto result = parse("-ERR unknown command\r\n", config);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->is_error());
    EXPECT_EQ(result->as_simple_error().prefix, "ERR");
    EXPECT_EQ(result->as_simple_error().message, "unknown command");
}

TEST_F(Resp2ParserTest, SimpleError_WrongType) {
    auto result = parse("-WRONGTYPE Operation against wrong type\r\n", config);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->is_error());
    EXPECT_EQ(result->as_simple_error().prefix, "WRONGTYPE");
    EXPECT_EQ(result->as_simple_error().message, "Operation against wrong type");
}

TEST_F(Resp2ParserTest, SimpleError_FirstWordIsPrefixWhenNoErrPrefix) {
    auto result = parse("-Some error message\r\n", config);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->is_error());
    EXPECT_EQ(result->as_simple_error().prefix, "Some");
    EXPECT_EQ(result->as_simple_error().message, "error message");
}

TEST_F(Resp2ParserTest, Integer_PositiveZeroNegative) {
    EXPECT_EQ(parse(":123\r\n", config)->as_integer().value, 123);
    EXPECT_EQ(parse(":0\r\n", config)->as_integer().value, 0);
    EXPECT_EQ(parse(":-456\r\n", config)->as_integer().value, -456);
}

// INT64 max/min — deduped from the doubled Integer_Large / NumericEdgeCases pair.
TEST_F(Resp2ParserTest, Integer_Int64Bounds) {
    EXPECT_EQ(parse(":9223372036854775807\r\n", config)->as_integer().value, std::numeric_limits<int64_t>::max());
    EXPECT_EQ(parse(":-9223372036854775808\r\n", config)->as_integer().value, std::numeric_limits<int64_t>::min());
    EXPECT_EQ(parse(":999999999999999999\r\n", config)->as_integer().value, 999999999999999999LL);
}

TEST_F(Resp2ParserTest, BulkString_Normal) {
    auto result = parse("$5\r\nhello\r\n", config);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->is_bulk_string());
    EXPECT_EQ(result->as_bulk_string().value, "hello");
    EXPECT_EQ(result->size(), 5u);
}

// Empty + null bulk — deduped from the doubled BulkString_Empty pair.
TEST_F(Resp2ParserTest, BulkString_EmptyAndNull) {
    auto empty = parse("$0\r\n\r\n", config);
    ASSERT_TRUE(empty.has_value());
    EXPECT_TRUE(empty->is_bulk_string());
    EXPECT_TRUE(empty->as_bulk_string().value.empty());

    auto null_ = parse("$-1\r\n", config);
    ASSERT_TRUE(null_.has_value());
    EXPECT_TRUE(null_->is_null());
}

TEST_F(Resp2ParserTest, BulkString_BinaryWithEmbeddedCRLF) {
    std::string binary_data;
    binary_data.push_back(0x00);
    binary_data.push_back(0x01);
    binary_data.push_back('\xFF');
    binary_data.push_back('\r');
    binary_data.push_back('\n');

    std::string input  = "$" + std::to_string(binary_data.size()) + "\r\n" + binary_data + "\r\n";
    auto        result = parse(input, config);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->is_bulk_string());
    EXPECT_EQ(result->as_bulk_string().value, binary_data);
}

TEST_F(Resp2ParserTest, BulkString_AllByteValues) {
    std::string content;
    for (int i = 0; i < 256; ++i)
        content.push_back(static_cast<char>(i));
    std::string input  = "$256\r\n" + content + "\r\n";
    auto        result = parse(input, config);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->as_bulk_string().value.size(), 256u);
    EXPECT_EQ(result->as_bulk_string().value, content);
}

// ============================================================================
// RESP2 array decode — STRENGTHENED to assert element identity, not just size
// ============================================================================

TEST_F(Resp2ParserTest, Array_Empty) {
    auto result = parse("*0\r\n", config);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->is_array());
    EXPECT_TRUE(result->as_array().elements.empty());
}

TEST_F(Resp2ParserTest, Array_MultipleStrings) {
    auto result = parse(std::string(corpus::frames::array_two_bulk), config);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 2u);
    EXPECT_EQ(result->as_array().elements[0]->as_string_view(), "hello");
    EXPECT_EQ(result->as_array().elements[1]->as_string_view(), "world");
}

TEST_F(Resp2ParserTest, Array_MixedTypesValuesChecked) {
    auto result = parse(std::string(corpus::frames::array_mixed), config);
    ASSERT_TRUE(result.has_value());
    const auto &els = result->as_array().elements;
    ASSERT_EQ(els.size(), 3u);
    ASSERT_TRUE(els[0]->is_integer());
    EXPECT_EQ(els[0]->as_integer().value, 1);
    ASSERT_TRUE(els[1]->is_bulk_string());
    EXPECT_EQ(els[1]->as_bulk_string().value, "hello");
    ASSERT_TRUE(els[2]->is_simple_string());
    EXPECT_EQ(els[2]->as_simple_string().value, "OK");
}

TEST_F(Resp2ParserTest, Array_Null) {
    auto result = parse("*-1\r\n", config);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->is_null());
}

TEST_F(Resp2ParserTest, Array_NestedValuesChecked) {
    auto result = parse(std::string(corpus::frames::array_nested), config);
    ASSERT_TRUE(result.has_value());
    const auto &outer = result->as_array().elements;
    ASSERT_EQ(outer.size(), 2u);

    ASSERT_TRUE(outer[0]->is_array());
    const auto &inner0 = outer[0]->as_array().elements;
    ASSERT_EQ(inner0.size(), 3u);
    EXPECT_EQ(inner0[0]->as_integer().value, 1);
    EXPECT_EQ(inner0[1]->as_integer().value, 2);
    EXPECT_EQ(inner0[2]->as_integer().value, 3);

    ASSERT_TRUE(outer[1]->is_array());
    const auto &inner1 = outer[1]->as_array().elements;
    ASSERT_EQ(inner1.size(), 2u);
    EXPECT_EQ(inner1[0]->as_simple_string().value, "Hello");
    EXPECT_EQ(inner1[1]->as_simple_error().prefix, "World");
}

TEST_F(Resp2ParserTest, Array_WithNullElement) {
    auto result = parse("*3\r\n$5\r\nhello\r\n$-1\r\n$5\r\nworld\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto &els = result->as_array().elements;
    ASSERT_EQ(els.size(), 3u);
    ASSERT_TRUE(els[0]->is_bulk_string());
    EXPECT_EQ(els[0]->as_bulk_string().value, "hello");
    EXPECT_TRUE(els[1]->is_null());
    ASSERT_TRUE(els[2]->is_bulk_string());
    EXPECT_EQ(els[2]->as_bulk_string().value, "world");
}

// Deep nesting — deduped from doubled depth-50 / depth-6 pair into one walk.
TEST_F(Resp2ParserTest, Array_DeepNestingWalk) {
    std::string data = corpus::gen::deeply_nested_array(50);
    auto        result = parse(data, config);
    ASSERT_TRUE(result.has_value());

    const Value *current = &*result;
    int          depth   = 0;
    while (current->is_array() && !current->as_array().elements.empty()) {
        current = current->as_array().elements[0].get();
        ++depth;
    }
    EXPECT_EQ(depth, 50);
    ASSERT_TRUE(current->is_integer());
    EXPECT_EQ(current->as_integer().value, 1);
}

// ============================================================================
// RESP3 scalar decode
// ============================================================================

class Resp3ParserTest : public ::testing::Test {
protected:
    void
    SetUp() override {
        config.protocol_version = ProtocolVersion::RESP3;
    }
    ParserConfig config;
};

TEST_F(Resp3ParserTest, Null_Basic) {
    auto result = parse("_\r\n", config);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->is_null());
}

TEST_F(Resp3ParserTest, Boolean_TrueFalse) {
    auto t = parse("#t\r\n", config);
    ASSERT_TRUE(t.has_value());
    ASSERT_TRUE(t->is_boolean());
    EXPECT_TRUE(t->as_boolean().value);

    auto f = parse("#f\r\n", config);
    ASSERT_TRUE(f.has_value());
    ASSERT_TRUE(f->is_boolean());
    EXPECT_FALSE(f->as_boolean().value);
}

TEST_F(Resp3ParserTest, Double_FiniteVariants) {
    EXPECT_DOUBLE_EQ(parse(",1.23\r\n", config)->as_double().value, 1.23);
    EXPECT_DOUBLE_EQ(parse(",-45.67\r\n", config)->as_double().value, -45.67);
    EXPECT_DOUBLE_EQ(parse(",10\r\n", config)->as_double().value, 10.0);
    EXPECT_DOUBLE_EQ(parse(",1.5e10\r\n", config)->as_double().value, 1.5e10);
    EXPECT_DOUBLE_EQ(parse(",0.000000001\r\n", config)->as_double().value, 1e-9);
    EXPECT_DOUBLE_EQ(parse(",1.7976931348623157e+308\r\n", config)->as_double().value, 1.7976931348623157e+308);
}

TEST_F(Resp3ParserTest, Double_ZeroVariants) {
    EXPECT_DOUBLE_EQ(parse(",0\r\n", config)->as_double().value, 0.0);
    EXPECT_DOUBLE_EQ(parse(",-0\r\n", config)->as_double().value, 0.0);
    EXPECT_DOUBLE_EQ(parse(",0.0\r\n", config)->as_double().value, 0.0);
}

TEST_F(Resp3ParserTest, Double_InfAndNan) {
    auto pinf = parse(",inf\r\n", config);
    ASSERT_TRUE(pinf.has_value());
    EXPECT_TRUE(std::isinf(pinf->as_double().value));
    EXPECT_GT(pinf->as_double().value, 0);
    // ADD: the explicit "+inf" spelling also decodes to +infinity.
    auto pinf2 = parse(",+inf\r\n", config);
    ASSERT_TRUE(pinf2.has_value());
    EXPECT_TRUE(std::isinf(pinf2->as_double().value));
    EXPECT_GT(pinf2->as_double().value, 0);

    auto ninf = parse(",-inf\r\n", config);
    ASSERT_TRUE(ninf.has_value());
    EXPECT_TRUE(std::isinf(ninf->as_double().value));
    EXPECT_LT(ninf->as_double().value, 0);

    auto nan = parse(",nan\r\n", config);
    ASSERT_TRUE(nan.has_value());
    EXPECT_TRUE(std::isnan(nan->as_double().value));
}

TEST_F(Resp3ParserTest, BigNumber_PositiveNegativeAndExtreme) {
    auto pos = parse("(123456789012345678901234567890\r\n", config);
    ASSERT_TRUE(pos.has_value());
    ASSERT_TRUE(pos->is_big_number());
    EXPECT_EQ(pos->as_big_number().value, "123456789012345678901234567890");
    EXPECT_FALSE(pos->as_big_number().negative);

    auto neg = parse("(-9999999999999999999999999\r\n", config);
    ASSERT_TRUE(neg.has_value());
    EXPECT_TRUE(neg->as_big_number().negative);

    // 1000-digit number survives intact.
    std::string big(1000, '9');
    auto        huge = parse("(" + big + "\r\n", config);
    ASSERT_TRUE(huge.has_value());
    EXPECT_EQ(huge->as_big_number().value.size(), 1000u);
}

TEST_F(Resp3ParserTest, BulkError_PrefixAndMessage) {
    auto result = parse("!21\r\nSYNTAX invalid syntax\r\n", config);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->is_error());
    EXPECT_EQ(result->as_bulk_error().prefix, "SYNTAX");
    EXPECT_EQ(result->as_bulk_error().message, "invalid syntax");
}

TEST_F(Resp3ParserTest, VerbatimString_EncodingAndPayload) {
    auto txt = parse("=15\r\ntxt:Some string\r\n", config);
    ASSERT_TRUE(txt.has_value());
    EXPECT_TRUE(txt->is_string()); // is_string() covers verbatim strings
    EXPECT_EQ(txt->as_verbatim_string().encoding_view(), "txt");
    EXPECT_EQ(txt->as_verbatim_string().value, "Some string");

    auto mkd = parse("=14\r\nmkd:# Markdown\r\n", config);
    ASSERT_TRUE(mkd.has_value());
    EXPECT_EQ(mkd->as_verbatim_string().encoding_view(), "mkd");
    EXPECT_EQ(mkd->as_verbatim_string().value, "# Markdown");
}

// ============================================================================
// RESP3 aggregate decode — STRENGTHENED to assert keys/values/order
// ============================================================================

TEST_F(Resp3ParserTest, Map_EmptyAndEntries) {
    auto empty = parse("%0\r\n", config);
    ASSERT_TRUE(empty.has_value());
    EXPECT_TRUE(empty->is_map());
    EXPECT_TRUE(empty->as_map().entries.empty());

    auto result = parse(std::string(corpus::frames::map_two), config);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->is_map());
    const auto &e = result->as_map().entries;
    ASSERT_EQ(e.size(), 2u);
    EXPECT_EQ(e[0].first->as_string_view(), "first");
    EXPECT_EQ(e[0].second->as_integer().value, 1);
    EXPECT_EQ(e[1].first->as_string_view(), "second");
    EXPECT_EQ(e[1].second->as_integer().value, 2);
}

TEST_F(Resp3ParserTest, Map_IntegerAndBulkKeysPreservePairing) {
    auto ikeys = parse("%2\r\n:1\r\n+one\r\n:2\r\n+two\r\n", config);
    ASSERT_TRUE(ikeys.has_value());
    const auto &ie = ikeys->as_map().entries;
    ASSERT_EQ(ie.size(), 2u);
    EXPECT_EQ(ie[0].first->as_integer().value, 1);
    EXPECT_EQ(ie[0].second->as_string_view(), "one");
    EXPECT_EQ(ie[1].first->as_integer().value, 2);
    EXPECT_EQ(ie[1].second->as_string_view(), "two");

    auto bkeys = parse("%2\r\n$3\r\nkey\r\n$5\r\nvalue\r\n$4\r\nkey2\r\n$6\r\nvalue2\r\n", config);
    ASSERT_TRUE(bkeys.has_value());
    const auto &be = bkeys->as_map().entries;
    ASSERT_EQ(be.size(), 2u);
    EXPECT_EQ(be[0].first->as_string_view(), "key");
    EXPECT_EQ(be[0].second->as_string_view(), "value");
    EXPECT_EQ(be[1].first->as_string_view(), "key2");
    EXPECT_EQ(be[1].second->as_string_view(), "value2");
}

TEST_F(Resp3ParserTest, Set_EmptyAndElements) {
    auto empty = parse("~0\r\n", config);
    ASSERT_TRUE(empty.has_value());
    EXPECT_TRUE(empty->is_set());
    EXPECT_TRUE(empty->as_set().elements.empty());

    auto result = parse(std::string(corpus::frames::set_three), config);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->is_set());
    const auto &el = result->as_set().elements;
    ASSERT_EQ(el.size(), 3u);
    EXPECT_EQ(el[0]->as_string_view(), "one");
    EXPECT_EQ(el[1]->as_string_view(), "two");
    EXPECT_EQ(el[2]->as_string_view(), "three");
}

TEST_F(Resp3ParserTest, Set_MixedTypesValuesChecked) {
    auto result = parse("~4\r\n:1\r\n+two\r\n$5\r\nthree\r\n#t\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto &el = result->as_set().elements;
    ASSERT_EQ(el.size(), 4u);
    EXPECT_EQ(el[0]->as_integer().value, 1);
    EXPECT_EQ(el[1]->as_string_view(), "two");
    EXPECT_EQ(el[2]->as_string_view(), "three");
    ASSERT_TRUE(el[3]->is_boolean());
    EXPECT_TRUE(el[3]->as_boolean().value);
}

TEST_F(Resp3ParserTest, Push_MessageValuesChecked) {
    auto result = parse(std::string(corpus::frames::push_message), config);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->is_push());
    const auto &el = result->as_push().elements;
    ASSERT_EQ(el.size(), 3u);
    EXPECT_EQ(el[0]->as_string_view(), "message");
    EXPECT_EQ(el[1]->as_string_view(), "channel");
    EXPECT_EQ(el[2]->as_string_view(), "payload");
}

// CLOSE THE ATTRIBUTE GAP: the value attached AFTER the metadata is now asserted.
TEST_F(Resp3ParserTest, Attribute_ValueIsDecoded) {
    auto result = parse(std::string(corpus::frames::attribute_int), config);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->is_attribute());
    const auto &attr = result->as_attribute();
    ASSERT_EQ(attr.data.entries.size(), 1u);
    EXPECT_EQ(attr.data.entries[0].first->as_string_view(), "ttl");
    EXPECT_EQ(attr.data.entries[0].second->as_integer().value, 3600);
    // The decorated value must be present and correct (was previously unchecked).
    ASSERT_TRUE(attr.value != nullptr);
    ASSERT_TRUE(attr.value->is_integer());
    EXPECT_EQ(attr.value->as_integer().value, 42);
}

TEST_F(Resp3ParserTest, Attribute_BulkValueIsDecoded) {
    auto result = parse("|1\r\n+ttl\r\n:3600\r\n$5\r\ndata!\r\n", config);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->is_attribute());
    const auto &attr = result->as_attribute();
    ASSERT_EQ(attr.data.entries.size(), 1u);
    ASSERT_TRUE(attr.value != nullptr);
    ASSERT_TRUE(attr.value->is_bulk_string());
    EXPECT_EQ(attr.value->as_bulk_string().value, "data!");
}

TEST_F(Resp3ParserTest, VerbatimString_BinaryContent) {
    std::string content = std::string("txt:") + std::string("\x00\x01\x02", 3);
    std::string data    = "=" + std::to_string(content.size()) + "\r\n" + content + "\r\n";
    auto        result  = parse(data, config);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->is_string());
    EXPECT_EQ(result->as_verbatim_string().encoding_view(), "txt");
    EXPECT_EQ(result->as_verbatim_string().value, std::string("\x00\x01\x02", 3));
}

// ============================================================================
// Streaming parser
// ============================================================================

class StreamingParserTest : public ::testing::Test {};

TEST_F(StreamingParserTest, MultipleMessagesViaParse) {
    RespParser parser;
    EXPECT_TRUE(parser.feed("+OK\r\n+PONG\r\n:42\r\n"));

    auto m1 = parser.parse();
    ASSERT_TRUE(m1.has_value());
    EXPECT_EQ(m1->as_string_view(), "OK");
    auto m2 = parser.parse();
    ASSERT_TRUE(m2.has_value());
    EXPECT_EQ(m2->as_string_view(), "PONG");
    auto m3 = parser.parse();
    ASSERT_TRUE(m3.has_value());
    EXPECT_EQ(m3->as_integer().value, 42);

    // Drained: next parse is INCOMPLETE_DATA.
    auto m4 = parser.parse();
    EXPECT_FALSE(m4.has_value());
    EXPECT_EQ(m4.error().code(), ParseErrorCode::INCOMPLETE_DATA);
}

TEST_F(StreamingParserTest, PartialThenCompleted) {
    RespParser parser;
    EXPECT_TRUE(parser.feed("+OK"));
    auto r1 = parser.parse();
    EXPECT_FALSE(r1.has_value());
    EXPECT_EQ(r1.error().code(), ParseErrorCode::INCOMPLETE_DATA);

    EXPECT_TRUE(parser.feed("\r\n"));
    auto r2 = parser.parse();
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(r2->as_string_view(), "OK");
}

// Byte-by-byte — deduped from the doubled SplitBulkString / ByteByByteParsing pair.
TEST_F(StreamingParserTest, ByteByByteBulkAssemblesCorrectly) {
    std::string bulk = "$10\r\nHelloWorld\r\n";
    RespParser  parser;
    for (size_t i = 0; i + 1 < bulk.size(); ++i) {
        EXPECT_TRUE(parser.feed(std::string_view(&bulk[i], 1)));
        auto partial = parser.parse();
        EXPECT_FALSE(partial.has_value());
        EXPECT_EQ(partial.error().code(), ParseErrorCode::INCOMPLETE_DATA);
    }
    EXPECT_TRUE(parser.feed(std::string_view(&bulk[bulk.size() - 1], 1)));
    auto result = parser.parse();
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->is_bulk_string());
    EXPECT_EQ(result->as_bulk_string().value, "HelloWorld");
}

TEST_F(StreamingParserTest, EmptyFeedIsIncomplete) {
    RespParser parser;
    EXPECT_TRUE(parser.feed(""));
    auto result = parser.parse();
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ParseErrorCode::INCOMPLETE_DATA);
}

TEST_F(StreamingParserTest, HasCompleteValueScalarAndBulkPaths) {
    {
        RespParser parser;
        EXPECT_FALSE(parser.has_complete_value());
    }
    { // scalar fast path: one CRLF confirms a simple string
        RespParser parser;
        EXPECT_TRUE(parser.feed("+OK\r\n"));
        EXPECT_TRUE(parser.has_complete_value());
    }
    { // scalar without terminator -> false
        RespParser parser;
        EXPECT_TRUE(parser.feed(":123"));
        EXPECT_FALSE(parser.has_complete_value());
    }
    { // bulk full-parse path: incomplete -> false, completed -> true
        RespParser parser;
        EXPECT_TRUE(parser.feed("$5\r\nhel"));
        EXPECT_FALSE(parser.has_complete_value());
        EXPECT_TRUE(parser.feed("lo\r\n"));
        EXPECT_TRUE(parser.has_complete_value());
    }
}

TEST_F(StreamingParserTest, ChunkedAggregateViaParseAll) {
    RespParser parser;
    EXPECT_TRUE(parser.feed("*2\r\n$5\r\nhel"));
    EXPECT_TRUE(parser.feed("lo\r\n$5\r\nworld\r\n"));
    auto results = parser.parse_all();
    ASSERT_EQ(results.size(), 1u);
    const auto &el = results[0].as_array().elements;
    ASSERT_EQ(el.size(), 2u);
    EXPECT_EQ(el[0]->as_string_view(), "hello");
    EXPECT_EQ(el[1]->as_string_view(), "world");
}

TEST_F(StreamingParserTest, ParseAllReturnsAllBufferedMessages) {
    RespParser parser;
    EXPECT_TRUE(parser.feed("+OK\r\n:2\r\n$5\r\nhello\r\n"));
    auto results = parser.parse_all();
    ASSERT_EQ(results.size(), 3u);
    EXPECT_EQ(results[0].as_string_view(), "OK");
    EXPECT_EQ(results[1].as_integer().value, 2);
    EXPECT_EQ(results[2].as_bulk_string().value, "hello");
}

TEST_F(StreamingParserTest, LargeMessageStreamedInChunks) {
    std::string large_content(100000, 'x');
    std::string full = "$" + std::to_string(large_content.size()) + "\r\n" + large_content + "\r\n";

    RespParser parser;
    size_t     chunk = 10000;
    for (size_t pos = 0; pos < full.size(); pos += chunk) {
        size_t len = std::min(chunk, full.size() - pos);
        EXPECT_TRUE(parser.feed(std::string_view(full.data() + pos, len)));
    }
    auto result = parser.parse();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->as_bulk_string().value.size(), 100000u);
}

// ============================================================================
// Error handling + recovery
// ============================================================================

class ErrorHandlingTest : public ::testing::Test {
protected:
    ParserConfig config;
};

TEST_F(ErrorHandlingTest, InvalidTypePrefix) {
    auto result = parse("?invalid\r\n", config);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ParseErrorCode::INVALID_TYPE);
}

TEST_F(ErrorHandlingTest, MissingCRLF) {
    auto result = parse("+OK", config);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ParseErrorCode::INCOMPLETE_DATA);
}

TEST_F(ErrorHandlingTest, InvalidIntegerLengthBooleanDouble) {
    EXPECT_EQ(parse(":abc\r\n", config).error().code(), ParseErrorCode::INVALID_INTEGER);
    EXPECT_EQ(parse("$abc\r\n", config).error().code(), ParseErrorCode::INVALID_LENGTH);
    EXPECT_EQ(parse("*abc\r\n", config).error().code(), ParseErrorCode::INVALID_LENGTH);
    EXPECT_EQ(parse("#x\r\n", config).error().code(), ParseErrorCode::INVALID_BOOLEAN);
    EXPECT_EQ(parse(",abc\r\n", config).error().code(), ParseErrorCode::INVALID_DOUBLE);
}

TEST_F(ErrorHandlingTest, IncompleteBulkString) {
    auto result = parse("$10\r\nHello", config);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ParseErrorCode::INCOMPLETE_DATA);
}

// ADD: integer overflow must be rejected as INVALID_INTEGER, not silently wrap.
TEST_F(ErrorHandlingTest, IntegerOverflowRejected) {
    auto over = parse(":99999999999999999999\r\n", config); // 20 nines > INT64_MAX
    ASSERT_FALSE(over.has_value());
    EXPECT_EQ(over.error().code(), ParseErrorCode::INVALID_INTEGER);
    // One past INT64_MAX likewise rejected.
    auto plus_one = parse(":9223372036854775808\r\n", config);
    ASSERT_FALSE(plus_one.has_value());
    EXPECT_EQ(plus_one.error().code(), ParseErrorCode::INVALID_INTEGER);
}

// ADD: a $<huge> bulk length near SIZE_MAX must be rejected (it parses as a
// negative int64 and is caught by the negative-length guard), never wrap.
TEST_F(ErrorHandlingTest, HugeBulkLengthRejected) {
    auto result = parse("$18446744073709551615\r\nx\r\n", config); // UINT64_MAX as length
    ASSERT_FALSE(result.has_value());
    // Parsed as out-of-range int64 -> INVALID_LENGTH (not BUFFER_OVERFLOW, not a wrap).
    EXPECT_EQ(result.error().code(), ParseErrorCode::INVALID_LENGTH);
}

// ADD: empty / malformed verbatim strings.
TEST_F(ErrorHandlingTest, VerbatimTooShortAndBadColon) {
    config.protocol_version = ProtocolVersion::RESP3;
    // len 3 ("txt") below the minimum of 4.
    EXPECT_EQ(parse("=3\r\ntxt\r\n", config).error().code(), ParseErrorCode::INVALID_VERBATIM_FORMAT);
    // colon not at index 3.
    EXPECT_EQ(parse("=4\r\ntxtx\r\n", config).error().code(), ParseErrorCode::INVALID_VERBATIM_FORMAT);
}

// ADD: big number empty body / non-digit body.
TEST_F(ErrorHandlingTest, BigNumberEmptyAndNonDigit) {
    config.protocol_version = ProtocolVersion::RESP3;
    EXPECT_EQ(parse("(\r\n", config).error().code(), ParseErrorCode::INVALID_BIG_NUMBER);
    EXPECT_EQ(parse("(12a34\r\n", config).error().code(), ParseErrorCode::INVALID_BIG_NUMBER);
}

class ErrorRecoveryTest : public ::testing::Test {};

TEST_F(ErrorRecoveryTest, ResetRecoversAfterInvalidPrefix) {
    RespParser parser;
    EXPECT_TRUE(parser.feed("Xinvalid\r\n"));
    EXPECT_FALSE(parser.parse().has_value());

    parser.reset();
    EXPECT_TRUE(parser.feed("+OK\r\n"));
    auto r = parser.parse();
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->as_simple_string().value, "OK");
}

TEST_F(ErrorRecoveryTest, ResetRecoversAfterMalformedInteger) {
    RespParser parser;
    EXPECT_TRUE(parser.feed(":abc\r\n"));
    EXPECT_FALSE(parser.parse().has_value());

    parser.reset();
    EXPECT_TRUE(parser.feed(":123\r\n"));
    auto r = parser.parse();
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->as_integer().value, 123);
}

// ============================================================================
// Nesting depth + buffer-overflow limits
// ============================================================================

class LimitTest : public ::testing::Test {
protected:
    ParserConfig config;
};

TEST_F(LimitTest, NestingTooDeepArray) {
    config.max_nesting_depth = 2;
    auto result = parse("*1\r\n*1\r\n*1\r\n:1\r\n", config); // 3 levels
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ParseErrorCode::NESTING_TOO_DEEP);
}

// ADD: nesting-depth limit applies to RESP3 aggregates (map/set/push), not just arrays.
TEST_F(LimitTest, NestingTooDeepResp3Aggregates) {
    config.protocol_version  = ProtocolVersion::RESP3;
    config.max_nesting_depth = 2;
    // map -> set -> push -> scalar = 3 aggregate levels.
    EXPECT_EQ(parse("%1\r\n+k\r\n~1\r\n>1\r\n:1\r\n", config).error().code(), ParseErrorCode::NESTING_TOO_DEEP);
}

// ADD: exact NESTING_TOO_DEEP / within-limit boundary.
TEST_F(LimitTest, NestingDepthBoundaryExact) {
    config.max_nesting_depth = 3;
    // Exactly 3 nested arrays then a scalar is allowed (depth 0..3).
    auto ok = parse("*1\r\n*1\r\n*1\r\n:7\r\n", config);
    ASSERT_TRUE(ok.has_value());
    // One deeper overflows.
    auto over = parse("*1\r\n*1\r\n*1\r\n*1\r\n:7\r\n", config);
    ASSERT_FALSE(over.has_value());
    EXPECT_EQ(over.error().code(), ParseErrorCode::NESTING_TOO_DEEP);
}

TEST_F(LimitTest, BulkStringRespectsMaxBulkSize) {
    config.max_bulk_size = 8;
    EXPECT_EQ(parse("$9\r\nxxxxxxxxx\r\n", config).error().code(), ParseErrorCode::BUFFER_OVERFLOW);
    // Exactly at the limit is accepted.
    auto at = parse("$8\r\nyyyyyyyy\r\n", config);
    ASSERT_TRUE(at.has_value());
    EXPECT_EQ(at->as_bulk_string().value.size(), 8u);
}

TEST_F(LimitTest, ExactBufferSizeBoundaries) {
    for (size_t size : {size_t{1}, size_t{1023}, size_t{1024}, size_t{1025}, size_t{4096}, size_t{8192}, size_t{8193}}) {
        std::string content(size, 'x');
        std::string data   = "$" + std::to_string(size) + "\r\n" + content + "\r\n";
        auto        result = parse(data, config);
        ASSERT_TRUE(result.has_value()) << "Size: " << size;
        EXPECT_EQ(result->as_bulk_string().value.size(), size) << "Size: " << size;
    }
}

// ============================================================================
// Protocol gating (RESP2 vs RESP3) — deduped per-type + en-masse into one matrix
// ============================================================================

TEST(ProtocolGating, Resp2RejectsEveryResp3Type) {
    ParserConfig resp2;
    resp2.protocol_version = ProtocolVersion::RESP2;
    const char *resp3_frames[] = {
        "_\r\n",                  // Null
        "#t\r\n",                 // Boolean
        ",1.5\r\n",               // Double
        "(12345678901234567890\r\n", // BigNumber
        "!5\r\nerror\r\n",        // BulkError
        "=15\r\ntxt:some text\r\n", // Verbatim
        "%1\r\n+key\r\n+val\r\n", // Map
        "~1\r\n+elem\r\n",        // Set
        ">1\r\n+msg\r\n",         // Push
        "|1\r\n+key\r\n+val\r\n:1\r\n", // Attribute
    };
    for (const char *f : resp3_frames) {
        auto r = parse(f, resp2);
        ASSERT_FALSE(r.has_value()) << "should reject: " << f;
        EXPECT_EQ(r.error().code(), ParseErrorCode::INVALID_TYPE) << "frame: " << f;
    }
}

TEST(ProtocolGating, Resp3AcceptsResp2AndResp3Types) {
    ParserConfig resp3;
    resp3.protocol_version = ProtocolVersion::RESP3;
    EXPECT_TRUE(parse("+OK\r\n", resp3).has_value());
    EXPECT_TRUE(parse("-ERR\r\n", resp3).has_value());
    EXPECT_TRUE(parse(":42\r\n", resp3).has_value());
    EXPECT_TRUE(parse("$5\r\nhello\r\n", resp3).has_value());
    EXPECT_TRUE(parse("*2\r\n:1\r\n:2\r\n", resp3).has_value());
    EXPECT_TRUE(parse("_\r\n", resp3).has_value());
    EXPECT_TRUE(parse("#t\r\n", resp3).has_value());
    EXPECT_TRUE(parse(",1.5\r\n", resp3).has_value());
}

// ============================================================================
// Serializer — exact wire bytes (merged from test-parser-units)
// ============================================================================

class SerializerTest : public ::testing::Test {};

TEST_F(SerializerTest, Scalars) {
    EXPECT_EQ(Serializer::serialize(Value(SimpleString{"OK"})), "+OK\r\n");
    EXPECT_EQ(Serializer::serialize(Value(SimpleError{"ERR", "message"})), "-ERR message\r\n");
    EXPECT_EQ(Serializer::serialize(Value(Integer{42})), ":42\r\n");
    EXPECT_EQ(Serializer::serialize(Value(BulkString{"hello"})), "$5\r\nhello\r\n");
    EXPECT_EQ(Serializer::serialize(Value(Null{})), "_\r\n");
    EXPECT_EQ(Serializer::serialize(Value(Boolean{true})), "#t\r\n");
    EXPECT_EQ(Serializer::serialize(Value(Boolean{false})), "#f\r\n");
}

TEST_F(SerializerTest, DoubleVariants) {
    EXPECT_EQ(Serializer::serialize(Value(Double{1.5})), ",1.5\r\n");
    EXPECT_EQ(Serializer::serialize(Value(Double{std::numeric_limits<double>::infinity()})), ",inf\r\n");
    EXPECT_EQ(Serializer::serialize(Value(Double{-std::numeric_limits<double>::infinity()})), ",-inf\r\n");
    EXPECT_EQ(Serializer::serialize(Value(Double{std::numeric_limits<double>::quiet_NaN()})), ",nan\r\n");
}

TEST_F(SerializerTest, BigNumber) {
    EXPECT_EQ(Serializer::serialize(Value(BigNumber{"12345678901234567890", false})), "(12345678901234567890\r\n");
}

TEST_F(SerializerTest, BulkErrorBranches) {
    EXPECT_EQ(Serializer::serialize(Value(BulkError{"SYNTAX", "bad"})), "!10\r\nSYNTAX bad\r\n");
    EXPECT_EQ(Serializer::serialize(Value(BulkError{"ERR", ""})), "!3\r\nERR\r\n");
    EXPECT_EQ(Serializer::serialize(Value(BulkError{"", "boom"})), "!4\r\nboom\r\n");
}

TEST_F(SerializerTest, VerbatimString) {
    VerbatimString vs;
    vs.encoding[0] = 't';
    vs.encoding[1] = 'x';
    vs.encoding[2] = 't';
    vs.value       = "Some string";
    EXPECT_EQ(Serializer::serialize(Value(std::move(vs))), "=15\r\ntxt:Some string\r\n");
}

TEST_F(SerializerTest, ArraySetPush) {
    {
        Array arr;
        arr.elements.push_back(std::make_unique<Value>(Value(Integer{1})));
        arr.elements.push_back(std::make_unique<Value>(Value(Integer{2})));
        arr.elements.push_back(std::make_unique<Value>(Value(Integer{3})));
        EXPECT_EQ(Serializer::serialize(Value(std::move(arr))), "*3\r\n:1\r\n:2\r\n:3\r\n");
    }
    {
        Push p;
        p.elements.push_back(std::make_unique<Value>(Value(SimpleString{"message"})));
        p.elements.push_back(std::make_unique<Value>(Value(Integer{1})));
        EXPECT_EQ(Serializer::serialize(Value(std::move(p))), ">2\r\n+message\r\n:1\r\n");
    }
}

TEST_F(SerializerTest, AttributeWithAndWithoutValue) {
    {
        Attribute a;
        a.data.entries.emplace_back(std::make_unique<Value>(Value(SimpleString{"k"})), std::make_unique<Value>(Value(Integer{7})));
        a.value = std::make_unique<Value>(Value(Integer{42}));
        EXPECT_EQ(Serializer::serialize(Value(std::move(a))), "|1\r\n+k\r\n:7\r\n:42\r\n");
    }
    { // no value -> trailing Null
        Attribute a;
        a.data.entries.emplace_back(std::make_unique<Value>(Value(SimpleString{"k"})), std::make_unique<Value>(Value(Integer{7})));
        EXPECT_EQ(Serializer::serialize(Value(std::move(a))), "|1\r\n+k\r\n:7\r\n_\r\n");
    }
}

TEST_F(SerializerTest, FreeHelpers) {
    EXPECT_EQ(serialize_simple_string("OK"), "+OK\r\n");
    EXPECT_EQ(serialize_error("ERR boom"), "-ERR boom\r\n");
    EXPECT_EQ(serialize_integer(-42), ":-42\r\n");
    EXPECT_EQ(serialize_bulk_string("hi"), "$2\r\nhi\r\n");
    EXPECT_EQ(serialize_null(), "_\r\n");
    EXPECT_EQ(serialize_array_header(3), "*3\r\n");
}

// ============================================================================
// CommandBuilder + serialize_command/_hello
// ============================================================================

class CommandBuilderTest : public ::testing::Test {};

TEST_F(CommandBuilderTest, BasicAndInteger) {
    EXPECT_EQ(CommandBuilder("SET").arg("mykey").arg("myvalue").build(),
              "*3\r\n$3\r\nSET\r\n$5\r\nmykey\r\n$7\r\nmyvalue\r\n");
    EXPECT_EQ(CommandBuilder("EXPIRE").arg("mykey").arg(60).build(),
              "*3\r\n$6\r\nEXPIRE\r\n$5\r\nmykey\r\n$2\r\n60\r\n");
}

TEST_F(CommandBuilderTest, ArgOverloadsAndCount) {
    CommandBuilder cmd("SET");
    cmd.arg("strkey");                       // string_view
    cmd.arg(static_cast<const char *>("c")); // const char*
    cmd.arg(static_cast<int64_t>(123));      // int64_t
    cmd.arg(1.5);                            // double
    EXPECT_EQ(cmd.arg_count(), 5u);
    EXPECT_EQ(cmd.build(), "*5\r\n$3\r\nSET\r\n$6\r\nstrkey\r\n$1\r\nc\r\n$3\r\n123\r\n$3\r\n1.5\r\n");
}

TEST_F(CommandBuilderTest, OptionalAndIfGating) {
    CommandBuilder cmd("SET");
    cmd.arg("key").arg("value");
    cmd.arg_optional(std::optional<std::string>("EX"));
    cmd.arg_optional(std::optional<std::string>{}); // skipped
    EXPECT_EQ(cmd.build(), "*4\r\n$3\r\nSET\r\n$3\r\nkey\r\n$5\r\nvalue\r\n$2\r\nEX\r\n");

    std::string with_options = CommandBuilder("SET")
                                   .arg("mykey")
                                   .arg("myvalue")
                                   .arg_if(true, "EX", "60")
                                   .arg_if(false, "NX") // skipped
                                   .arg_if(true, "XX")
                                   .build();
    EXPECT_EQ(with_options, "*6\r\n$3\r\nSET\r\n$5\r\nmykey\r\n$7\r\nmyvalue\r\n$2\r\nEX\r\n$2\r\n60\r\n$2\r\nXX\r\n");
}

TEST_F(CommandBuilderTest, RawClearDefaultCtor) {
    CommandBuilder cmd;
    EXPECT_EQ(cmd.arg_count(), 0u);
    cmd.arg("PING");
    std::string raw = "EXTRA";
    EXPECT_EQ(cmd.build_with_raw(std::span<const char>(raw.data(), raw.size())), "*1\r\n$4\r\nPING\r\nEXTRA");
    cmd.clear();
    EXPECT_EQ(cmd.arg_count(), 0u);
    EXPECT_EQ(cmd.build(), "*0\r\n");
}

TEST_F(CommandBuilderTest, SerializeCommandVariadicAndHello) {
    EXPECT_EQ(Serializer::serialize_command("GET", "key"), "*2\r\n$3\r\nGET\r\n$3\r\nkey\r\n");
    EXPECT_EQ(Serializer::serialize_hello(ProtocolVersion::RESP3), "*2\r\n$5\r\nHELLO\r\n$1\r\n3\r\n");
    EXPECT_EQ(Serializer::serialize_hello(ProtocolVersion::RESP3, "default", "mypassword"),
              "*5\r\n$5\r\nHELLO\r\n$1\r\n3\r\n$4\r\nAUTH\r\n$7\r\ndefault\r\n$10\r\nmypassword\r\n");
    // password-only uses the implicit "default" username branch.
    EXPECT_EQ(Serializer::serialize_hello(ProtocolVersion::RESP3, std::nullopt, std::string("secret")),
              "*5\r\n$5\r\nHELLO\r\n$1\r\n3\r\n$4\r\nAUTH\r\n$7\r\ndefault\r\n$6\r\nsecret\r\n");
}

// ============================================================================
// Round-trip (parse -> serialize -> parse) — value-equality, not size-only
// ============================================================================

class RoundTripTest : public ::testing::Test {
protected:
    ParserConfig config;
};

TEST_F(RoundTripTest, ScalarBytesAreStable) {
    for (std::string original : {std::string("+Hello\r\n"), std::string(":12345\r\n"), std::string("$5\r\nhello\r\n"),
                                 std::string("*2\r\n$5\r\nhello\r\n$5\r\nworld\r\n")}) {
        auto parsed = parse(original, config);
        ASSERT_TRUE(parsed.has_value()) << original;
        EXPECT_EQ(Serializer::serialize(*parsed), original);
    }
}

TEST_F(RoundTripTest, NestedStructurePreservesValues) {
    Array root;
    {
        Array nested;
        nested.elements.push_back(std::make_unique<Value>(Integer{1}));
        nested.elements.push_back(std::make_unique<Value>(BulkString{"test"}));
        root.elements.push_back(std::make_unique<Value>(std::move(nested)));
    }
    root.elements.push_back(std::make_unique<Value>(SimpleString{"ok"}));
    root.elements.push_back(std::make_unique<Value>(Integer{999}));

    std::string serialized = Serializer::serialize(Value(std::move(root)));
    auto        result     = parse(serialized, config);
    ASSERT_TRUE(result.has_value());
    const auto &els = result->as_array().elements;
    ASSERT_EQ(els.size(), 3u);
    ASSERT_TRUE(els[0]->is_array());
    EXPECT_EQ(els[0]->as_array().elements[0]->as_integer().value, 1);
    EXPECT_EQ(els[0]->as_array().elements[1]->as_bulk_string().value, "test");
    EXPECT_EQ(els[1]->as_simple_string().value, "ok");
    EXPECT_EQ(els[2]->as_integer().value, 999);
}

TEST_F(RoundTripTest, MapAndSetPreserveContents) {
    ParserConfig resp3;
    resp3.protocol_version = ProtocolVersion::RESP3;
    {
        Map map;
        map.entries.push_back({std::make_unique<Value>(BulkString{"key1"}), std::make_unique<Value>(SimpleString{"value1"})});
        map.entries.push_back({std::make_unique<Value>(BulkString{"key2"}), std::make_unique<Value>(Integer{42})});
        auto result = parse(Serializer::serialize(Value(std::move(map))), resp3);
        ASSERT_TRUE(result.has_value());
        ASSERT_TRUE(result->is_map());
        const auto &e = result->as_map().entries;
        ASSERT_EQ(e.size(), 2u);
        EXPECT_EQ(e[0].first->as_string_view(), "key1");
        EXPECT_EQ(e[0].second->as_string_view(), "value1");
        EXPECT_EQ(e[1].first->as_string_view(), "key2");
        EXPECT_EQ(e[1].second->as_integer().value, 42);
    }
    {
        Set set;
        set.elements.push_back(std::make_unique<Value>(Integer{1}));
        set.elements.push_back(std::make_unique<Value>(Integer{2}));
        set.elements.push_back(std::make_unique<Value>(Integer{3}));
        auto result = parse(Serializer::serialize(Value(std::move(set))), resp3);
        ASSERT_TRUE(result.has_value());
        ASSERT_TRUE(result->is_set());
        const auto &el = result->as_set().elements;
        ASSERT_EQ(el.size(), 3u);
        EXPECT_EQ(el[0]->as_integer().value, 1);
        EXPECT_EQ(el[1]->as_integer().value, 2);
        EXPECT_EQ(el[2]->as_integer().value, 3);
    }
}

TEST_F(RoundTripTest, BinaryAndControlCharsPreserved) {
    std::string binary;
    for (int i = 0; i < 256; ++i)
        binary.push_back(static_cast<char>(i));
    auto bin = parse(Serializer::serialize(Value(BulkString{binary})), config);
    ASSERT_TRUE(bin.has_value());
    EXPECT_EQ(bin->as_bulk_string().value, binary);

    for (const std::string &str : {std::string("\r\n"), std::string("\r"), std::string("\n"), std::string("$100\r\n"),
                                   std::string("*5\r\n"), std::string("+OK\r\n")}) {
        auto r = parse(Serializer::serialize(Value(BulkString{str})), config);
        ASSERT_TRUE(r.has_value()) << "Failed for: " << str;
        EXPECT_EQ(r->as_bulk_string().value, str) << "Mismatch for: " << str;
    }
}

// ADD: scalar round-trip property over a representative table (parse->serialize->parse equal).
TEST_F(RoundTripTest, ScalarValueEqualityProperty) {
    ParserConfig resp3;
    resp3.protocol_version = ProtocolVersion::RESP3;
    std::vector<Value> samples;
    samples.emplace_back(SimpleString{"hello"});
    samples.emplace_back(Integer{-12345});
    samples.emplace_back(BulkString{"with spaces"});
    samples.emplace_back(Boolean{true});
    samples.emplace_back(Double{3.14159});
    samples.emplace_back(BigNumber{"99999999999999999999", false});

    for (auto &v : samples) {
        std::string wire = Serializer::serialize(v);
        auto        a    = parse(wire, resp3);
        ASSERT_TRUE(a.has_value()) << wire;
        // Re-serialize must yield the identical bytes (idempotent codec).
        EXPECT_EQ(Serializer::serialize(*a), wire);
    }
}

// ============================================================================
// Parser state machine (merged from test-parser-units)
// ============================================================================

class ParserStateTest : public ::testing::Test {};

TEST_F(ParserStateTest, StateTransitionsAndUnparsedData) {
    RespParser parser;
    EXPECT_TRUE(parser.is_ready());
    EXPECT_FALSE(parser.is_parsing());
    EXPECT_FALSE(parser.is_complete());
    EXPECT_FALSE(parser.has_error());

    EXPECT_TRUE(parser.feed("$5\r\nhel"));
    auto incomplete = parser.parse();
    EXPECT_FALSE(incomplete.has_value());
    EXPECT_EQ(incomplete.error().code(), ParseErrorCode::INCOMPLETE_DATA);
    EXPECT_FALSE(parser.has_error());

    auto pending = parser.unparsed_data();
    EXPECT_EQ(std::string(pending.data(), pending.size()), "$5\r\nhel");

    EXPECT_TRUE(parser.feed("lo\r\n"));
    auto done = parser.parse();
    ASSERT_TRUE(done.has_value());
    EXPECT_EQ(done->as_bulk_string().value, "hello");
}

TEST_F(ParserStateTest, FeedSpanOverload) {
    RespParser  parser;
    std::string data = "+OK\r\n";
    EXPECT_TRUE(parser.feed(std::span<const char>(data.data(), data.size())));
    auto result = parser.parse();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->as_string_view(), "OK");
}

// ============================================================================
// Hardening: null strictness, fault detection, fault idempotency
// ============================================================================

TEST(ParserHardening, OnlyMinusOneIsNull) {
    ParserConfig config;
    config.protocol_version = ProtocolVersion::RESP3;

    EXPECT_TRUE(parse("$-1\r\n", config)->is_null());
    EXPECT_TRUE(parse("*-1\r\n", config)->is_null());

    // Negative lengths other than -1 are rejected (not silent nulls).
    EXPECT_EQ(parse("$-5\r\n", config).error().code(), ParseErrorCode::INVALID_LENGTH);
    EXPECT_EQ(parse("*-3\r\n", config).error().code(), ParseErrorCode::INVALID_LENGTH);
    EXPECT_EQ(parse("%-2\r\n", config).error().code(), ParseErrorCode::INVALID_LENGTH);
    EXPECT_EQ(parse("~-4\r\n", config).error().code(), ParseErrorCode::INVALID_LENGTH);
    EXPECT_EQ(parse(">-3\r\n", config).error().code(), ParseErrorCode::INVALID_LENGTH);
}

TEST(ParserHardening, ParseAllFaultsOnCorruptTopLevelByte) {
    RespParser parser;
    EXPECT_TRUE(parser.feed("@garbage\r\n")); // '@' is not a valid prefix
    auto values = parser.parse_all();
    EXPECT_TRUE(values.empty());
    EXPECT_TRUE(parser.has_error());
    EXPECT_FALSE(parser.is_parsing()); // FAULT excluded from is_parsing()
    EXPECT_FALSE(parser.feed("+OK\r\n")); // faulted parser refuses input
}

// ADD: parse_all on a faulted parser stays empty + errored (idempotent fault).
TEST(ParserHardening, ParseAllAfterFaultIsIdempotent) {
    RespParser parser;
    EXPECT_TRUE(parser.feed("@garbage\r\n"));
    EXPECT_TRUE(parser.parse_all().empty());
    ASSERT_TRUE(parser.has_error());
    // Repeated calls keep returning empty and stay faulted; parse() also errors.
    EXPECT_TRUE(parser.parse_all().empty());
    EXPECT_TRUE(parser.has_error());
    auto p = parser.parse();
    EXPECT_FALSE(p.has_value());
    EXPECT_EQ(p.error().code(), ParseErrorCode::PROTOCOL_ERROR);
    EXPECT_TRUE(parser.has_error());
}

TEST(ParserHardening, IncompleteFrameRetainedNotFaulted) {
    RespParser parser;
    EXPECT_TRUE(parser.feed("$5\r\nhel")); // incomplete bulk
    EXPECT_TRUE(parser.parse_all().empty());
    EXPECT_FALSE(parser.has_error());
    EXPECT_TRUE(parser.feed("lo\r\n"));
    auto v = parser.parse_all();
    ASSERT_EQ(v.size(), 1u);
    EXPECT_EQ(v[0].as_bulk_string().value, "hello");
}

// Corrupt fixed-length terminator faults; partial terminator merely retained.
TEST(ParserHardening, CorruptBulkTerminatorFaultsButPartialRetained) {
    {
        RespParser parser;
        EXPECT_TRUE(parser.feed("$3\r\nabcXX")); // "abc" then "XX" instead of CRLF
        EXPECT_TRUE(parser.parse_all().empty());
        EXPECT_TRUE(parser.has_error());
        EXPECT_FALSE(parser.feed("more\r\n"));
    }
    {
        RespParser parser;
        EXPECT_TRUE(parser.feed("$3\r\nabc")); // payload complete, CRLF not yet arrived
        EXPECT_TRUE(parser.parse_all().empty());
        EXPECT_FALSE(parser.has_error());
        EXPECT_TRUE(parser.feed("\r\n"));
        auto v = parser.parse_all();
        ASSERT_EQ(v.size(), 1u);
        EXPECT_EQ(v[0].as_bulk_string().value, "abc");
    }
}

TEST(ParserHardening, CorruptBooleanTerminatorFaults) {
    ParserConfig config;
    config.protocol_version = ProtocolVersion::RESP3;
    RespParser parser(config);
    EXPECT_TRUE(parser.feed("#tXX")); // '#t' then "XX" instead of CRLF
    EXPECT_TRUE(parser.parse_all().empty());
    EXPECT_TRUE(parser.has_error());
}

// ============================================================================
// Reply-decode through the codec (scan cursor + double strictness)
// ============================================================================

TEST(ReplyDecode, HighBitScanCursorParsesUnsigned) {
    RespParser parser;
    ASSERT_TRUE(parser.feed("*2\r\n$20\r\n18446744073709551615\r\n*1\r\n$3\r\nfoo\r\n"));
    auto values = parser.parse_all();
    ASSERT_EQ(values.size(), 1u);

    auto sc = qb::redis::reply::parse<qb::redis::scan<>>(values[0]);
    EXPECT_EQ(sc.cursor, static_cast<std::size_t>(18446744073709551615ULL));
    ASSERT_EQ(sc.items.size(), 1u);
    EXPECT_EQ(sc.items[0], "foo");
}

TEST(ReplyDecode, NonNumericScanCursorRejected) {
    RespParser parser;
    ASSERT_TRUE(parser.feed("*2\r\n$3\r\nxyz\r\n*0\r\n"));
    auto values = parser.parse_all();
    ASSERT_EQ(values.size(), 1u);
    EXPECT_THROW((void) qb::redis::reply::parse<qb::redis::scan<>>(values[0]), qb::redis::ProtoError);
}

TEST(ReplyDecode, ParseDoubleRejectsTrailingGarbage) {
    auto value_of = [](const char *resp) {
        RespParser parser;
        EXPECT_TRUE(parser.feed(resp));
        auto values = parser.parse_all();
        EXPECT_EQ(values.size(), 1u);
        return std::move(values[0]);
    };
    EXPECT_DOUBLE_EQ(qb::redis::reply::parse<double>(value_of("$3\r\n1.5\r\n")), 1.5);
    EXPECT_THROW((void) qb::redis::reply::parse<double>(value_of("$7\r\n1.5junk\r\n")), qb::redis::ProtoError);
    EXPECT_THROW((void) qb::redis::reply::parse<double>(value_of("$2\r\n3x\r\n")), qb::redis::ProtoError);
}
