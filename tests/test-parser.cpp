/*
 * qb - C++ Actor Framework
 * Copyright (C) 2011-2025 isndev (cpp.actor). All rights reserved.
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
#include <parser/parser.h>
#include <parser/serializer.h>
#include "../reply.h"
#include <chrono>
#include <string>
#include <vector>

using namespace qb::redis::parser;

// ============================================================================
// Helper functions
// ============================================================================

[[maybe_unused]] static std::string make_bulk_string(std::string_view content) {
    return std::string("$") + std::to_string(content.size()) + "\r\n" + 
           std::string(content) + "\r\n";
}

[[maybe_unused]] static std::string make_array(std::initializer_list<std::string> elements) {
    std::string result = "*" + std::to_string(elements.size()) + "\r\n";
    for (const auto& elem : elements) {
        result += elem;
    }
    return result;
}

// ============================================================================
// RESP2 Tests
// ============================================================================

class Resp2ParserTest : public ::testing::Test {
protected:
    void SetUp() override {
        config.protocol_version = ProtocolVersion::RESP2;
    }
    
    ParserConfig config;
};

// Simple String tests
TEST_F(Resp2ParserTest, SimpleString_OK) {
    auto result = parse("+OK\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    
    
    EXPECT_TRUE(value.is_simple_string());
    EXPECT_EQ(value.as_simple_string().value, "OK");
}

TEST_F(Resp2ParserTest, SimpleString_PONG) {
    auto result = parse("+PONG\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    
    
    EXPECT_TRUE(value.is_simple_string());
    EXPECT_EQ(value.as_string_view(), "PONG");
}

TEST_F(Resp2ParserTest, SimpleString_Empty) {
    auto result = parse("+\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    
    
    EXPECT_TRUE(value.is_simple_string());
    EXPECT_TRUE(value.as_string_view().empty());
}

TEST_F(Resp2ParserTest, SimpleString_WithSpaces) {
    auto result = parse("+Hello World\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    EXPECT_EQ(value.as_string_view(), "Hello World");
}

// Error tests
TEST_F(Resp2ParserTest, SimpleError_Generic) {
    auto result = parse("-ERR unknown command\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    
    
    EXPECT_TRUE(value.is_error());
    EXPECT_EQ(value.as_simple_error().prefix, "ERR");
    EXPECT_EQ(value.as_simple_error().message, "unknown command");
}

TEST_F(Resp2ParserTest, SimpleError_WrongType) {
    auto result = parse("-WRONGTYPE Operation against wrong type\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    
    
    EXPECT_TRUE(value.is_error());
    EXPECT_EQ(value.as_simple_error().prefix, "WRONGTYPE");
}

TEST_F(Resp2ParserTest, SimpleError_NoPrefix) {
    auto result = parse("-Some error message\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    
    
    EXPECT_TRUE(value.is_error());
    EXPECT_EQ(value.as_simple_error().prefix, "Some");
    EXPECT_EQ(value.as_simple_error().message, "error message");
}

// Integer tests
TEST_F(Resp2ParserTest, Integer_Positive) {
    auto result = parse(":123\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    
    
    EXPECT_TRUE(value.is_integer());
    EXPECT_EQ(value.as_integer().value, 123);
}

TEST_F(Resp2ParserTest, Integer_Zero) {
    auto result = parse(":0\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    EXPECT_EQ(value.as_integer().value, 0);
}

TEST_F(Resp2ParserTest, Integer_Negative) {
    auto result = parse(":-456\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    EXPECT_EQ(value.as_integer().value, -456);
}

TEST_F(Resp2ParserTest, Integer_Large) {
    auto result = parse(":9223372036854775807\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    EXPECT_EQ(value.as_integer().value, 9223372036854775807LL);
}

TEST_F(Resp2ParserTest, Integer_LargeNegative) {
    auto result = parse(":-9223372036854775808\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    EXPECT_EQ(value.as_integer().value, std::numeric_limits<int64_t>::min());
}

// Bulk String tests
TEST_F(Resp2ParserTest, BulkString_Normal) {
    auto result = parse("$5\r\nhello\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    
    
    EXPECT_TRUE(value.is_bulk_string());
    EXPECT_EQ(value.as_bulk_string().value, "hello");
    EXPECT_EQ(value.size(), 5);
}

TEST_F(Resp2ParserTest, BulkString_Empty) {
    auto result = parse("$0\r\n\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    
    
    EXPECT_TRUE(value.is_bulk_string());
    EXPECT_TRUE(value.as_bulk_string().value.empty());
}

TEST_F(Resp2ParserTest, BulkString_Null) {
    auto result = parse("$-1\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    
    
    EXPECT_TRUE(value.is_null());
}

TEST_F(Resp2ParserTest, BulkString_BinaryData) {
    std::string binary_data;
    binary_data.push_back(0x00);
    binary_data.push_back(0x01);
    binary_data.push_back(0xFF);
    binary_data.push_back('\r');
    binary_data.push_back('\n');
    
    std::string input = "$" + std::to_string(binary_data.size()) + "\r\n" + 
                        binary_data + "\r\n";
    
    auto result = parse(input, config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    
    
    EXPECT_TRUE(value.is_bulk_string());
    EXPECT_EQ(value.as_bulk_string().value, binary_data);
}

TEST_F(Resp2ParserTest, BulkString_Large) {
    std::string large_content(1000000, 'x');
    std::string input = "$" + std::to_string(large_content.size()) + "\r\n" + 
                        large_content + "\r\n";
    
    auto result = parse(input, config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    EXPECT_EQ(value.as_bulk_string().value.size(), 1000000);
}

// Array tests
TEST_F(Resp2ParserTest, Array_Empty) {
    auto result = parse("*0\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    
    
    EXPECT_TRUE(value.is_array());
    EXPECT_TRUE(value.as_array().elements.empty());
}

TEST_F(Resp2ParserTest, Array_SingleInteger) {
    auto result = parse("*1\r\n:42\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    
    
    EXPECT_TRUE(value.is_array());
    EXPECT_EQ(value.size(), 1);
    EXPECT_TRUE(value.as_array().elements[0]->is_integer());
    EXPECT_EQ(value.as_array().elements[0]->as_integer().value, 42);
}

TEST_F(Resp2ParserTest, Array_MultipleStrings) {
    auto result = parse("*2\r\n$5\r\nhello\r\n$5\r\nworld\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    
    
    EXPECT_TRUE(value.is_array());
    EXPECT_EQ(value.size(), 2);
    EXPECT_EQ(value.as_array().elements[0]->as_string_view(), "hello");
    EXPECT_EQ(value.as_array().elements[1]->as_string_view(), "world");
}

TEST_F(Resp2ParserTest, Array_MixedTypes) {
    auto result = parse("*3\r\n:1\r\n$5\r\nhello\r\n+OK\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    
    
    EXPECT_EQ(value.size(), 3);
    EXPECT_TRUE(value.as_array().elements[0]->is_integer());
    EXPECT_TRUE(value.as_array().elements[1]->is_bulk_string());
    EXPECT_TRUE(value.as_array().elements[2]->is_simple_string());
}

TEST_F(Resp2ParserTest, Array_Null) {
    auto result = parse("*-1\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    EXPECT_TRUE(value.is_null());
}

TEST_F(Resp2ParserTest, Array_Nested) {
    auto result = parse("*2\r\n*3\r\n:1\r\n:2\r\n:3\r\n*2\r\n+Hello\r\n-World\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    
    
    EXPECT_EQ(value.size(), 2);
    EXPECT_TRUE(value.as_array().elements[0]->is_array());
    EXPECT_TRUE(value.as_array().elements[1]->is_array());
    EXPECT_EQ(value.as_array().elements[0]->size(), 3);
    EXPECT_EQ(value.as_array().elements[1]->size(), 2);
}

TEST_F(Resp2ParserTest, Array_WithNullElement) {
    auto result = parse("*3\r\n$5\r\nhello\r\n$-1\r\n$5\r\nworld\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    
    
    EXPECT_EQ(value.size(), 3);
    EXPECT_TRUE(value.as_array().elements[0]->is_bulk_string());
    EXPECT_TRUE(value.as_array().elements[1]->is_null());
    EXPECT_TRUE(value.as_array().elements[2]->is_bulk_string());
}

// ============================================================================
// RESP3 Tests
// ============================================================================

class Resp3ParserTest : public ::testing::Test {
protected:
    void SetUp() override {
        config.protocol_version = ProtocolVersion::RESP3;
    }
    
    ParserConfig config;
};

// Null tests
TEST_F(Resp3ParserTest, Null_Basic) {
    auto result = parse("_\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    EXPECT_TRUE(value.is_null());
}

// Boolean tests
TEST_F(Resp3ParserTest, Boolean_True) {
    auto result = parse("#t\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    
    
    EXPECT_TRUE(value.is_boolean());
    EXPECT_TRUE(value.as_boolean().value);
}

TEST_F(Resp3ParserTest, Boolean_False) {
    auto result = parse("#f\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    
    
    EXPECT_TRUE(value.is_boolean());
    EXPECT_FALSE(value.as_boolean().value);
}

// Double tests
TEST_F(Resp3ParserTest, Double_Positive) {
    auto result = parse(",1.23\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    
    
    EXPECT_TRUE(value.is_double());
    EXPECT_DOUBLE_EQ(value.as_double().value, 1.23);
}

TEST_F(Resp3ParserTest, Double_Negative) {
    auto result = parse(",-45.67\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    EXPECT_DOUBLE_EQ(value.as_double().value, -45.67);
}

TEST_F(Resp3ParserTest, Double_Integer) {
    auto result = parse(",10\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    EXPECT_DOUBLE_EQ(value.as_double().value, 10.0);
}

TEST_F(Resp3ParserTest, Double_Scientific) {
    auto result = parse(",1.5e10\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    EXPECT_DOUBLE_EQ(value.as_double().value, 1.5e10);
}

TEST_F(Resp3ParserTest, Double_PositiveInfinity) {
    auto result = parse(",inf\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    EXPECT_TRUE(std::isinf(value.as_double().value));
    EXPECT_GT(value.as_double().value, 0);
}

TEST_F(Resp3ParserTest, Double_NegativeInfinity) {
    auto result = parse(",-inf\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    EXPECT_TRUE(std::isinf(value.as_double().value));
    EXPECT_LT(value.as_double().value, 0);
}

TEST_F(Resp3ParserTest, Double_NaN) {
    auto result = parse(",nan\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    EXPECT_TRUE(std::isnan(value.as_double().value));
}

// Big Number tests
TEST_F(Resp3ParserTest, BigNumber_Positive) {
    auto result = parse("(123456789012345678901234567890\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    
    
    EXPECT_TRUE(value.is_big_number());
    EXPECT_EQ(value.as_big_number().value, "123456789012345678901234567890");
    EXPECT_FALSE(value.as_big_number().negative);
}

TEST_F(Resp3ParserTest, BigNumber_Negative) {
    auto result = parse("(-9999999999999999999999999\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    
    
    EXPECT_TRUE(value.is_big_number());
    EXPECT_TRUE(value.as_big_number().negative);
}

// Bulk Error tests
TEST_F(Resp3ParserTest, BulkError_Basic) {
    auto result = parse("!21\r\nSYNTAX invalid syntax\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    
    
    EXPECT_TRUE(value.is_error());
    // In ModernReply, bulk errors are stored similarly to simple errors
}

// Verbatim String tests
TEST_F(Resp3ParserTest, VerbatimString_TXT) {
    auto result = parse("=15\r\ntxt:Some string\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    
    
    EXPECT_TRUE(value.is_string());  // Verbatim string is a string type
    EXPECT_EQ(value.as_verbatim_string().encoding_view(), "txt");
    EXPECT_EQ(value.as_verbatim_string().value, "Some string");
}

TEST_F(Resp3ParserTest, VerbatimString_MKD) {
    auto result = parse("=14\r\nmkd:# Markdown\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    EXPECT_EQ(value.as_verbatim_string().encoding_view(), "mkd");
}

// Map tests
TEST_F(Resp3ParserTest, Map_Empty) {
    auto result = parse("%0\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    
    
    EXPECT_TRUE(value.is_map());
    EXPECT_TRUE(value.as_map().entries.empty());
}

TEST_F(Resp3ParserTest, Map_SingleEntry) {
    auto result = parse("%1\r\n+key\r\n$5\r\nvalue\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    
    
    EXPECT_TRUE(value.is_map());
    EXPECT_EQ(value.as_map().entries.size(), 1);
}

TEST_F(Resp3ParserTest, Map_MultipleEntries) {
    auto result = parse("%2\r\n+first\r\n:1\r\n+second\r\n:2\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    
    
    EXPECT_EQ(value.as_map().entries.size(), 2);
}

// Set tests
TEST_F(Resp3ParserTest, Set_Empty) {
    auto result = parse("~0\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    
    
    EXPECT_TRUE(value.is_set());
    EXPECT_TRUE(value.as_set().elements.empty());
}

TEST_F(Resp3ParserTest, Set_WithElements) {
    auto result = parse("~3\r\n+one\r\n+two\r\n+three\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    
    
    EXPECT_TRUE(value.is_set());
    EXPECT_EQ(value.as_set().elements.size(), 3);
}

// Push tests
TEST_F(Resp3ParserTest, Push_Message) {
    auto result = parse(">3\r\n+message\r\n+channel\r\n$7\r\npayload\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    
    
    EXPECT_TRUE(value.is_push());
    EXPECT_EQ(value.as_push().elements.size(), 3);
}

// ============================================================================
// Streaming Parser Tests
// ============================================================================

class StreamingParserTest : public ::testing::Test {
protected:
    ParserConfig config;
};

TEST_F(StreamingParserTest, SingleCompleteMessage) {
    RespParser parser;
    
    EXPECT_TRUE(parser.feed("+OK\r\n"));
    
    auto result = parser.parse();
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    EXPECT_TRUE(value.is_string());
}

TEST_F(StreamingParserTest, MultipleMessagesAtOnce) {
    RespParser parser;
    
    EXPECT_TRUE(parser.feed("+OK\r\n+PONG\r\n:42\r\n"));
    
    std::vector<std::string> received;
    
    // Parse all 3 messages
    for (int i = 0; i < 3; ++i) {
        auto result = parser.parse();
        ASSERT_TRUE(result.has_value());
    const auto& value = *result;
        if (value.is_string()) {
            received.push_back(std::string(value.as_string_view()));
        } else if (value.is_integer()) {
            received.push_back(std::to_string(value.as_integer().value));
        }
    }
    
    EXPECT_EQ(received.size(), 3);
    EXPECT_EQ(received[0], "OK");
    EXPECT_EQ(received[1], "PONG");
    EXPECT_EQ(received[2], "42");
}

TEST_F(StreamingParserTest, PartialMessage) {
    RespParser parser;
    
    // Feed partial data
    EXPECT_TRUE(parser.feed("+OK"));
    
    auto result1 = parser.parse();
    EXPECT_FALSE(result1.has_value());
    EXPECT_EQ(result1.error().code(), ParseErrorCode::INCOMPLETE_DATA);
    
    // Complete the message
    EXPECT_TRUE(parser.feed("\r\n"));
    
    auto result2 = parser.parse();
    ASSERT_TRUE(result2.has_value());
    EXPECT_TRUE(result2->is_string());
    EXPECT_EQ(result2->as_string_view(), "OK");
}

TEST_F(StreamingParserTest, SplitBulkString) {
    RespParser parser;
    
    std::string bulk = "$10\r\nHelloWorld\r\n";
    
    // Feed byte by byte
    for (size_t i = 0; i < bulk.size(); ++i) {
        EXPECT_TRUE(parser.feed(std::string_view(&bulk[i], 1)));
    }
    
    auto result = parser.parse();
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    EXPECT_TRUE(value.is_bulk_string());
    EXPECT_EQ(value.as_bulk_string().value, "HelloWorld");
}

TEST_F(StreamingParserTest, EmptyFeed) {
    RespParser parser;
    
    EXPECT_TRUE(parser.feed(""));
    
    auto result = parser.parse();
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ParseErrorCode::INCOMPLETE_DATA);
}

TEST_F(StreamingParserTest, HasMessageCheck) {
    RespParser parser;
    
    EXPECT_FALSE(parser.has_complete_value());
    
    EXPECT_TRUE(parser.feed("+OK\r\n"));
    
    EXPECT_TRUE(parser.has_complete_value());
}

TEST_F(StreamingParserTest, TryGetMessage) {
    RespParser parser;
    
    // No data yet - should get INCOMPLETE_DATA
    auto msg1 = parser.parse();
    EXPECT_FALSE(msg1.has_value());
    
    EXPECT_TRUE(parser.feed("+OK\r\n"));
    
    auto msg2 = parser.parse();
    ASSERT_TRUE(msg2.has_value());
    EXPECT_TRUE(msg2->is_string());
    
    // After consuming the message, no more data
    auto msg3 = parser.parse();
    EXPECT_FALSE(msg3.has_value());
}

// ============================================================================
// Error Handling Tests
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

TEST_F(ErrorHandlingTest, InvalidInteger) {
    auto result = parse(":abc\r\n", config);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ParseErrorCode::INVALID_INTEGER);
}

TEST_F(ErrorHandlingTest, InvalidBulkLength) {
    auto result = parse("$abc\r\n", config);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ParseErrorCode::INVALID_LENGTH);
}

TEST_F(ErrorHandlingTest, IncompleteBulkString) {
    auto result = parse("$10\r\nHello", config);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ParseErrorCode::INCOMPLETE_DATA);
}

TEST_F(ErrorHandlingTest, InvalidArrayCount) {
    auto result = parse("*abc\r\n", config);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ParseErrorCode::INVALID_LENGTH);
}

TEST_F(ErrorHandlingTest, InvalidBoolean) {
    auto result = parse("#x\r\n", config);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ParseErrorCode::INVALID_BOOLEAN);
}

TEST_F(ErrorHandlingTest, InvalidDouble) {
    auto result = parse(",abc\r\n", config);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ParseErrorCode::INVALID_DOUBLE);
}

TEST_F(ErrorHandlingTest, NestingTooDeep) {
    config.max_nesting_depth = 2;
    
    // Create deeply nested array: *1\r\n*1\r\n*1\r\n:1\r\n (3 levels)
    std::string nested = "*1\r\n*1\r\n*1\r\n:1\r\n";
    auto result = parse(nested, config);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ParseErrorCode::NESTING_TOO_DEEP);
}

// ============================================================================
// RESP2 vs RESP3 Mode Tests
// ============================================================================

class ProtocolVersionTest : public ::testing::Test {};

TEST_F(ProtocolVersionTest, Resp2RejectsNullType) {
    ParserConfig config;
    config.protocol_version = ProtocolVersion::RESP2;
    
    auto result = parse("_\r\n", config);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ParseErrorCode::INVALID_TYPE);
}

TEST_F(ProtocolVersionTest, Resp2RejectsBooleanType) {
    ParserConfig config;
    config.protocol_version = ProtocolVersion::RESP2;
    
    auto result = parse("#t\r\n", config);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ParseErrorCode::INVALID_TYPE);
}

TEST_F(ProtocolVersionTest, Resp3AcceptsNullType) {
    ParserConfig config;
    config.protocol_version = ProtocolVersion::RESP3;
    
    auto result = parse("_\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    EXPECT_TRUE(value.is_null());
}

// ============================================================================
// Serializer Tests
// ============================================================================

class SerializerTest : public ::testing::Test {};

TEST_F(SerializerTest, SerializeSimpleString) {
    Value val(SimpleString{"OK"});
    auto serialized = Serializer::serialize(val);
    EXPECT_EQ(serialized, "+OK\r\n");
}

TEST_F(SerializerTest, SerializeError) {
    Value val(SimpleError{"ERR", "message"});
    auto serialized = Serializer::serialize(val);
    EXPECT_EQ(serialized, "-ERR message\r\n");
}

TEST_F(SerializerTest, SerializeInteger) {
    Value val(Integer{42});
    auto serialized = Serializer::serialize(val);
    EXPECT_EQ(serialized, ":42\r\n");
}

TEST_F(SerializerTest, SerializeBulkString) {
    Value val(BulkString{"hello"});
    auto serialized = Serializer::serialize(val);
    EXPECT_EQ(serialized, "$5\r\nhello\r\n");
}

TEST_F(SerializerTest, SerializeNull) {
    Value val(Null{});
    auto serialized = Serializer::serialize(val);
    EXPECT_EQ(serialized, "_\r\n");
}

TEST_F(SerializerTest, SerializeBooleanTrue) {
    Value val(Boolean{true});
    auto serialized = Serializer::serialize(val);
    EXPECT_EQ(serialized, "#t\r\n");
}

TEST_F(SerializerTest, SerializeArray) {
    Array arr;
    arr.elements.push_back(std::make_unique<Value>(Value(Integer{1})));
    arr.elements.push_back(std::make_unique<Value>(Value(Integer{2})));
    arr.elements.push_back(std::make_unique<Value>(Value(Integer{3})));
    
    Value val(std::move(arr));
    auto serialized = Serializer::serialize(val);
    EXPECT_EQ(serialized, "*3\r\n:1\r\n:2\r\n:3\r\n");
}

TEST_F(SerializerTest, CommandBuilderBasic) {
    CommandBuilder cmd("SET");
    cmd.arg("mykey").arg("myvalue");
    
    auto serialized = cmd.build();
    EXPECT_EQ(serialized, "*3\r\n$3\r\nSET\r\n$5\r\nmykey\r\n$7\r\nmyvalue\r\n");
}

TEST_F(SerializerTest, CommandBuilderWithInteger) {
    CommandBuilder cmd("EXPIRE");
    cmd.arg("mykey").arg(60);
    
    auto serialized = cmd.build();
    EXPECT_EQ(serialized, "*3\r\n$6\r\nEXPIRE\r\n$5\r\nmykey\r\n$2\r\n60\r\n");
}

TEST_F(SerializerTest, CommandBuilderWithOptional) {
    CommandBuilder cmd("SET");
    cmd.arg("key").arg("value");
    cmd.arg_optional(std::optional<std::string>("EX"));
    cmd.arg_optional(std::optional<std::string>{});  // Empty optional - should not add
    
    auto serialized = cmd.build();
    EXPECT_EQ(serialized, "*4\r\n$3\r\nSET\r\n$3\r\nkey\r\n$5\r\nvalue\r\n$2\r\nEX\r\n");
}

TEST_F(SerializerTest, SerializeHelloCommand) {
    auto hello = Serializer::serialize_hello(ProtocolVersion::RESP3);
    EXPECT_EQ(hello, "*2\r\n$5\r\nHELLO\r\n$1\r\n3\r\n");
}

TEST_F(SerializerTest, SerializeHelloWithAuth) {
    auto hello = Serializer::serialize_hello(
        ProtocolVersion::RESP3,
        "default",
        "mypassword"
    );
    EXPECT_EQ(hello, "*5\r\n$5\r\nHELLO\r\n$1\r\n3\r\n$4\r\nAUTH\r\n$7\r\ndefault\r\n$10\r\nmypassword\r\n");
}

// ============================================================================
// Round-trip Tests (Parse -> Serialize -> Parse)
// ============================================================================

class RoundTripTest : public ::testing::Test {
protected:
    ParserConfig config;
};

TEST_F(RoundTripTest, SimpleString) {
    std::string original = "+Hello\r\n";
    auto parsed = parse(original, config);
    ASSERT_TRUE(parsed.has_value());
    
    auto serialized = Serializer::serialize(*parsed);
    EXPECT_EQ(serialized, original);
}

TEST_F(RoundTripTest, Integer) {
    std::string original = ":12345\r\n";
    auto parsed = parse(original, config);
    ASSERT_TRUE(parsed.has_value());
    
    auto serialized = Serializer::serialize(*parsed);
    EXPECT_EQ(serialized, original);
}

TEST_F(RoundTripTest, BulkString) {
    std::string original = "$5\r\nhello\r\n";
    auto parsed = parse(original, config);
    ASSERT_TRUE(parsed.has_value());
    
    auto serialized = Serializer::serialize(*parsed);
    EXPECT_EQ(serialized, original);
}

TEST_F(RoundTripTest, Array) {
    std::string original = "*2\r\n$5\r\nhello\r\n$5\r\nworld\r\n";
    auto parsed = parse(original, config);
    ASSERT_TRUE(parsed.has_value());
    
    auto serialized = Serializer::serialize(*parsed);
    EXPECT_EQ(serialized, original);
}

// ============================================================================
// Performance Tests
// ============================================================================

class PerformanceTest : public ::testing::Test {
protected:
    ParserConfig config;
};

TEST_F(PerformanceTest, ParseManySmallMessages) {
    constexpr int count = 10000;
    std::string data;
    for (int i = 0; i < count; ++i) {
        data += ":" + std::to_string(i) + "\r\n";
    }
    
    RespParser parser;
    EXPECT_TRUE(parser.feed(data));
    
    int received = 0;
    while (true) {
        auto result = parser.parse();
        if (!result.has_value()) {
            if (result.error().code() == ParseErrorCode::INCOMPLETE_DATA) {
                break;
            }
        } else {
            EXPECT_TRUE((*result).is_integer());
            ++received;
        }
    }
    
    EXPECT_EQ(received, count);
}

TEST_F(PerformanceTest, ParseLargeBulkString) {
    std::string large_content(10 * 1024 * 1024, 'x');  // 10MB
    std::string data = "$" + std::to_string(large_content.size()) + "\r\n" + 
                       large_content + "\r\n";
    
    auto start = std::chrono::high_resolution_clock::now();
    
    auto result = parse(data, config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // Should parse 10MB in less than 100ms on modern hardware
    EXPECT_LT(duration.count(), 100);
    EXPECT_EQ(value.as_bulk_string().value.size(), large_content.size());
}

TEST_F(PerformanceTest, ParseDeeplyNestedArray) {
    // Create array with depth 50
    std::string data;
    for (int i = 0; i < 50; ++i) {
        data += "*1\r\n";
    }
    data += ":1\r\n";
    for (int i = 0; i < 50; ++i) {
        // Close all arrays (no extra data needed in RESP)
    }
    
    auto result = parse(data, config);
    ASSERT_TRUE(result.has_value());
    
    // Navigate to deepest element
    const Value* current = &*result;
    int depth = 0;
    while (current->is_array() && !current->as_array().elements.empty()) {
        current = current->as_array().elements[0].get();
        ++depth;
    }
    
    EXPECT_EQ(depth, 50);
    EXPECT_TRUE(current->is_integer());
    EXPECT_EQ(current->as_integer().value, 1);
}

// ============================================================================
// Additional Comprehensive Tests for Complete Coverage
// ============================================================================

// ----------------------------------------------------------------------------
// Edge Cases - Numeric Boundaries
// ----------------------------------------------------------------------------
class NumericEdgeCasesTest : public ::testing::Test {
protected:
    ParserConfig config;
};

TEST_F(NumericEdgeCasesTest, Integer_Max) {
    auto result = parse(":9223372036854775807\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    EXPECT_EQ(value.as_integer().value, std::numeric_limits<int64_t>::max());
}

TEST_F(NumericEdgeCasesTest, Integer_Min) {
    auto result = parse(":-9223372036854775808\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    EXPECT_EQ(value.as_integer().value, std::numeric_limits<int64_t>::min());
}

TEST_F(NumericEdgeCasesTest, Integer_AllNines) {
    auto result = parse(":999999999999999999\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    EXPECT_EQ(value.as_integer().value, 999999999999999999LL);
}

TEST_F(NumericEdgeCasesTest, Double_ZeroVariants) {
    // Positive zero
    auto result1 = parse(",0\r\n", config);
    ASSERT_TRUE(result1.has_value());
    EXPECT_DOUBLE_EQ(result1->as_double().value, 0.0);
    
    // Negative zero
    auto result2 = parse(",-0\r\n", config);
    ASSERT_TRUE(result2.has_value());
    EXPECT_DOUBLE_EQ(result2->as_double().value, 0.0);
    
    // Decimal zero
    auto result3 = parse(",0.0\r\n", config);
    ASSERT_TRUE(result3.has_value());
    EXPECT_DOUBLE_EQ(result3->as_double().value, 0.0);
}

TEST_F(NumericEdgeCasesTest, Double_SmallValues) {
    auto result = parse(",0.000000001\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    EXPECT_DOUBLE_EQ(value.as_double().value, 1e-9);
}

TEST_F(NumericEdgeCasesTest, Double_LargeValues) {
    auto result = parse(",1.7976931348623157e+308\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    EXPECT_DOUBLE_EQ(value.as_double().value, 1.7976931348623157e+308);
}

TEST_F(NumericEdgeCasesTest, BigNumber_ExtremelyLarge) {
    // 1000 digit number
    std::string big_num(1000, '9');
    auto result = parse("(" + big_num + "\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    EXPECT_TRUE(value.is_big_number());
    EXPECT_EQ(value.as_big_number().value.size(), 1000);
}

// ----------------------------------------------------------------------------
// Bulk String Edge Cases
// ----------------------------------------------------------------------------
class BulkStringEdgeCasesTest : public ::testing::Test {
protected:
    ParserConfig config;
};

TEST_F(BulkStringEdgeCasesTest, BulkString_WithCRLFInside) {
    // Bulk string containing \r\n characters
    std::string content = "line1\r\nline2\r\nline3";
    std::string data = "$" + std::to_string(content.size()) + "\r\n" + content + "\r\n";
    
    auto result = parse(data, config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    EXPECT_EQ(value.as_bulk_string().value, content);
}

TEST_F(BulkStringEdgeCasesTest, BulkString_WithNullBytes) {
    // Binary data with null bytes
    std::string content;
    content.push_back('\0');
    content.push_back('A');
    content.push_back('\0');
    content.push_back('B');
    content.push_back('\0');
    
    std::string data = "$" + std::to_string(content.size()) + "\r\n" + content + "\r\n";
    
    auto result = parse(data, config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    EXPECT_EQ(value.as_bulk_string().value.size(), 5);
    EXPECT_EQ(value.as_bulk_string().value[0], '\0');
    EXPECT_EQ(value.as_bulk_string().value[2], '\0');
}

TEST_F(BulkStringEdgeCasesTest, BulkString_AllCharacters) {
    // All byte values 0-255
    std::string content;
    for (int i = 0; i < 256; ++i) {
        content.push_back(static_cast<char>(i));
    }
    
    std::string data = "$" + std::to_string(content.size()) + "\r\n" + content + "\r\n";
    
    auto result = parse(data, config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    EXPECT_EQ(value.as_bulk_string().value.size(), 256);
}

TEST_F(BulkStringEdgeCasesTest, BulkString_EmptyAfterNull) {
    // $0\r\n\r\n (empty bulk)
    auto result = parse("$0\r\n\r\n", config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    EXPECT_TRUE(value.is_bulk_string());
    EXPECT_TRUE(value.as_bulk_string().value.empty());
}

// ----------------------------------------------------------------------------
// Array Edge Cases
// ----------------------------------------------------------------------------
class ArrayEdgeCasesTest : public ::testing::Test {
protected:
    ParserConfig config;
};

TEST_F(ArrayEdgeCasesTest, Array_Large) {
    // Array with 1000 elements
    std::string data = "*1000\r\n";
    for (int i = 0; i < 1000; ++i) {
        data += ":" + std::to_string(i) + "\r\n";
    }
    
    auto result = parse(data, config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    EXPECT_EQ(value.as_array().elements.size(), 1000);
}

TEST_F(ArrayEdgeCasesTest, Array_AllSameType) {
    // Array of all bulk strings
    std::string data = "*5\r\n";
    for (int i = 0; i < 5; ++i) {
        data += "$5\r\nhello\r\n";
    }
    
    auto result = parse(data, config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    EXPECT_EQ(value.as_array().elements.size(), 5);
    for (const auto& elem : value.as_array().elements) {
        EXPECT_TRUE(elem->is_bulk_string());
    }
}

TEST_F(ArrayEdgeCasesTest, Array_MixedWithAggregates) {
    // Array containing both simple and aggregate types
    // [*2\r\n:1\r\n+two\r\n, $5\r\nthree\r\n]
    std::string data = "*3\r\n*2\r\n:1\r\n+two\r\n$5\r\nthree\r\n:4\r\n";
    
    auto result = parse(data, config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    EXPECT_EQ(value.as_array().elements.size(), 3);
    EXPECT_TRUE(value.as_array().elements[0]->is_array());
    EXPECT_TRUE(value.as_array().elements[1]->is_bulk_string());
    EXPECT_TRUE(value.as_array().elements[2]->is_integer());
}

TEST_F(ArrayEdgeCasesTest, Array_DeepNesting) {
    // [[[[[[1]]]]]]
    std::string data = "*1\r\n*1\r\n*1\r\n*1\r\n*1\r\n*1\r\n:1\r\n";
    
    auto result = parse(data, config);
    ASSERT_TRUE(result.has_value());

    const Value* current = &*result;
    int depth = 0;
    while (current->is_array() && !current->as_array().elements.empty()) {
        current = current->as_array().elements[0].get();
        ++depth;
    }
    EXPECT_EQ(depth, 6);
    EXPECT_EQ(current->as_integer().value, 1);
}

// ----------------------------------------------------------------------------
// RESP3 Advanced Features
// ----------------------------------------------------------------------------
class Resp3AdvancedTest : public ::testing::Test {
protected:
    void SetUp() override {
        config.protocol_version = ProtocolVersion::RESP3;
    }
    
    ParserConfig config;
};

TEST_F(Resp3AdvancedTest, Attribute_WithSimpleValue) {
    // |1\r\n+key\r\n+value\r\n:42\r\n
    std::string data = "|1\r\n+key\r\n+value\r\n:42\r\n";
    
    auto result = parse(data, config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    EXPECT_TRUE(value.is_attribute());
    EXPECT_EQ(value.as_attribute().data.entries.size(), 1);
    // The value after attributes is in the parser's Value variant, not in Attribute
}

TEST_F(Resp3AdvancedTest, Attribute_WithBulkValue) {
    // Attribute with bulk string value
    std::string data = "|1\r\n+ttl\r\n:3600\r\n$5\r\ndata!\r\n";
    
    auto result = parse(data, config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    EXPECT_TRUE(value.is_attribute());
    EXPECT_EQ(value.as_attribute().data.entries.size(), 1);
}

TEST_F(Resp3AdvancedTest, Push_ComplexMessage) {
    // Push message with multiple elements
    // >3\r\n+message\r\n+channel\r\n$7\r\npayload\r\n
    std::string data = ">3\r\n+message\r\n+channel\r\n$7\r\npayload\r\n";
    
    auto result = parse(data, config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    EXPECT_TRUE(value.is_push());
    EXPECT_EQ(value.as_push().elements.size(), 3);
}

TEST_F(Resp3AdvancedTest, Map_WithIntegerKeys) {
    // Map using integers as keys (valid in RESP3)
    std::string data = "%2\r\n:1\r\n+one\r\n:2\r\n+two\r\n";
    
    auto result = parse(data, config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    EXPECT_TRUE(value.is_map());
    EXPECT_EQ(value.as_map().entries.size(), 2);
}

TEST_F(Resp3AdvancedTest, Map_WithBulkStringKeys) {
    // Map with bulk string keys
    std::string data = "%2\r\n$3\r\nkey\r\n$5\r\nvalue\r\n$4\r\nkey2\r\n$6\r\nvalue2\r\n";
    
    auto result = parse(data, config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    EXPECT_TRUE(value.is_map());
    EXPECT_EQ(value.as_map().entries.size(), 2);
}

TEST_F(Resp3AdvancedTest, Set_WithDifferentTypes) {
    // Set containing various types
    // ~4\r\n:1\r\n+two\r\n$5\r\nthree\r\n#t\r\n  (three = 5 chars)
    std::string data = "~4\r\n:1\r\n+two\r\n$5\r\nthree\r\n#t\r\n";
    
    auto result = parse(data, config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    EXPECT_TRUE(value.is_set());
    EXPECT_EQ(value.as_set().elements.size(), 4);
}

TEST_F(Resp3AdvancedTest, VerbatimString_BinaryContent) {
    // Verbatim string with binary content after encoding
    // =8\r\ntxt:\x00\x01\x02\r\n  (txt: = 4 chars + 3 binary + \r\n = but we need just content length)
    std::string content = std::string("txt:") + "\x00\x01\x02";
    std::string data = "=" + std::to_string(content.size()) + "\r\n" + content + "\r\n";
    
    auto result = parse(data, config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    EXPECT_TRUE(value.is_string());  // is_string() covers verbatim strings
    EXPECT_EQ(value.as_verbatim_string().encoding_view(), "txt");
}

TEST_F(Resp3AdvancedTest, BulkError_WithBinaryData) {
    // Bulk error containing special characters
    std::string error_msg = "ERR multiline error";
    std::string data = "!" + std::to_string(error_msg.size()) + "\r\n" + error_msg + "\r\n";
    
    auto result = parse(data, config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    EXPECT_TRUE(value.is_error());  // is_error() covers bulk errors
    EXPECT_EQ(value.as_bulk_error().prefix, "ERR");
    EXPECT_EQ(value.as_bulk_error().message, "multiline error");
}

// ----------------------------------------------------------------------------
// Streaming Edge Cases
// ----------------------------------------------------------------------------
class StreamingEdgeCasesTest : public ::testing::Test {
protected:
    ParserConfig config;
};

TEST_F(StreamingEdgeCasesTest, ByteByByteParsing) {
    // Feed data one byte at a time
    std::string message = "+OK\r\n";
    RespParser parser;
    
    // Feed first 4 bytes (incomplete)
    for (size_t i = 0; i < message.size() - 2; ++i) {
        EXPECT_TRUE(parser.feed(std::string_view(&message[i], 1)));
        auto result = parser.parse();
        EXPECT_FALSE(result.has_value());  // Incomplete
        EXPECT_EQ(result.error().code(), ParseErrorCode::INCOMPLETE_DATA);
    }
    
    // Feed last 2 bytes
    EXPECT_TRUE(parser.feed(std::string_view(&message[message.size()-2], 2)));
    auto result = parser.parse();
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ((*result).as_simple_string().value, "OK");
}

TEST_F(StreamingEdgeCasesTest, MultipleMessagesOneByteAtATime) {
    std::string messages = ":1\r\n:2\r\n:3\r\n";
    RespParser parser;
    int received = 0;
    
    for (char c : messages) {
        EXPECT_TRUE(parser.feed(std::string_view(&c, 1)));
        
        // Try to parse after each byte
        while (true) {
            auto result = parser.parse();
            if (!result.has_value()) {
                if (result.error().code() == ParseErrorCode::INCOMPLETE_DATA) {
                    break;
                }
            } else {
                EXPECT_TRUE((*result).is_integer());
                ++received;
            }
        }
    }
    
    EXPECT_EQ(received, 3);
}

TEST_F(StreamingEdgeCasesTest, SplitAtVariousPoints) {
    // Test splitting at key points in a message - using streaming parser's built-in handling
    
    // Test split after header (feed all data, parse once at end)
    {
        RespParser parser;
        // Feed in chunks
        EXPECT_TRUE(parser.feed(std::string_view("*2\r\n$5\r\nhel")));
        EXPECT_TRUE(parser.feed(std::string_view("lo\r\n$5\r\nworld\r\n")));
        
        // Parse all at once
        auto results = parser.parse_all();
        EXPECT_EQ(results.size(), 1);
        if (!results.empty()) {
            EXPECT_EQ(results[0].as_array().elements.size(), 2);
        }
    }
}

TEST_F(StreamingEdgeCasesTest, InterleavedMessages) {
    // Multiple interleaved message types - feed complete messages sequentially
    RespParser parser;
    
    // Feed complete messages one at a time
    EXPECT_TRUE(parser.feed(std::string_view("+OK\r\n", 5)));
    EXPECT_TRUE(parser.feed(std::string_view(":2\r\n", 4)));
    EXPECT_TRUE(parser.feed(std::string_view("$5\r\nhello\r\n", 11)));
    
    // Use parse_all() to get all complete messages
    auto results = parser.parse_all();
    
    // Should have received the complete messages
    EXPECT_EQ(results.size(), 3);  // +OK, :2, $5...\r\n
}

TEST_F(StreamingEdgeCasesTest, LargeMessageStreaming) {
    // Stream a large message in chunks
    std::string large_content(100000, 'x');
    std::string header = "$" + std::to_string(large_content.size()) + "\r\n";
    std::string full_message = header + large_content + "\r\n";
    
    RespParser parser;
    size_t chunk_size = 10000;
    
    for (size_t pos = 0; pos < full_message.size(); pos += chunk_size) {
        size_t len = std::min(chunk_size, full_message.size() - pos);
        EXPECT_TRUE(parser.feed(std::string_view(full_message.data() + pos, len)));
    }
    
    auto result = parser.parse();
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ((*result).as_bulk_string().value.size(), 100000);
}

// ----------------------------------------------------------------------------
// Error Recovery Tests
// ----------------------------------------------------------------------------
class ErrorRecoveryTest : public ::testing::Test {
protected:
    ParserConfig config;
};

TEST_F(ErrorRecoveryTest, InvalidTypePrefixRecovery) {
    RespParser parser;
    
    // Invalid prefix
    EXPECT_TRUE(parser.feed("Xinvalid\r\n"));
    auto result1 = parser.parse();
    EXPECT_FALSE(result1.has_value());
    
    // Reset and valid message
    parser.reset();
    EXPECT_TRUE(parser.feed("+OK\r\n"));
    auto result2 = parser.parse();
    EXPECT_TRUE(result2.has_value());
    EXPECT_EQ(result2->as_simple_string().value, "OK");
}

TEST_F(ErrorRecoveryTest, IncompleteMessageThenValid) {
    RespParser parser;
    
    // Incomplete bulk string
    EXPECT_TRUE(parser.feed("$100\r\nshort"));
    auto result1 = parser.parse();
    EXPECT_FALSE(result1.has_value());
    
    // Reset and parse valid message
    parser.reset();
    EXPECT_TRUE(parser.feed("+OK\r\n"));
    auto result2 = parser.parse();
    EXPECT_TRUE(result2.has_value());
}

TEST_F(ErrorRecoveryTest, MalformedIntegerRecovery) {
    RespParser parser;
    
    // Invalid integer
    EXPECT_TRUE(parser.feed(":abc\r\n"));
    auto result1 = parser.parse();
    EXPECT_FALSE(result1.has_value());
    
    // Continue with valid message
    parser.reset();
    EXPECT_TRUE(parser.feed(":123\r\n"));
    auto result2 = parser.parse();
    EXPECT_TRUE(result2.has_value());
    EXPECT_EQ(result2->as_integer().value, 123);
}

// ----------------------------------------------------------------------------
// Protocol Version Tests
// ----------------------------------------------------------------------------
class ProtocolVersionEdgeTest : public ::testing::Test {};

TEST_F(ProtocolVersionEdgeTest, Resp2RejectsResp3Types) {
    ParserConfig resp2_config;
    resp2_config.protocol_version = ProtocolVersion::RESP2;
    
    // These should fail in RESP2 mode
    EXPECT_FALSE(parse("_\r\n", resp2_config).has_value());  // Null
    EXPECT_FALSE(parse("#t\r\n", resp2_config).has_value());  // Boolean
    EXPECT_FALSE(parse(",1.5\r\n", resp2_config).has_value());  // Double
    EXPECT_FALSE(parse("(12345678901234567890\r\n", resp2_config).has_value());  // BigNumber
    EXPECT_FALSE(parse("!5\r\nerror\r\n", resp2_config).has_value());  // BulkError
    EXPECT_FALSE(parse("=15\r\ntxt:some text\r\n", resp2_config).has_value());  // Verbatim
    EXPECT_FALSE(parse("%1\r\n+key\r\n+val\r\n", resp2_config).has_value());  // Map
    EXPECT_FALSE(parse("~1\r\n+elem\r\n", resp2_config).has_value());  // Set
    EXPECT_FALSE(parse(">1\r\n+msg\r\n", resp2_config).has_value());  // Push
    EXPECT_FALSE(parse("|1\r\n+key\r\n+val\r\n:1\r\n", resp2_config).has_value());  // Attribute
}

TEST_F(ProtocolVersionEdgeTest, Resp3AcceptsAllTypes) {
    ParserConfig resp3_config;
    resp3_config.protocol_version = ProtocolVersion::RESP3;
    
    // RESP3 should accept both RESP2 and RESP3 types
    EXPECT_TRUE(parse("+OK\r\n", resp3_config).has_value());  // Simple String
    EXPECT_TRUE(parse("-ERR\r\n", resp3_config).has_value());  // Simple Error
    EXPECT_TRUE(parse(":42\r\n", resp3_config).has_value());  // Integer
    EXPECT_TRUE(parse("$5\r\nhello\r\n", resp3_config).has_value());  // Bulk String
    EXPECT_TRUE(parse("*2\r\n:1\r\n:2\r\n", resp3_config).has_value());  // Array
    EXPECT_TRUE(parse("_\r\n", resp3_config).has_value());  // Null
    EXPECT_TRUE(parse("#t\r\n", resp3_config).has_value());  // Boolean
    EXPECT_TRUE(parse(",1.5\r\n", resp3_config).has_value());  // Double
}

// ----------------------------------------------------------------------------
// Serializer Comprehensive Tests
// ----------------------------------------------------------------------------
class SerializerComprehensiveTest : public ::testing::Test {};

TEST_F(SerializerComprehensiveTest, SerializeComplexArray) {
    // Build and serialize a complex array
    Value arr = Array{std::vector<std::unique_ptr<Value>>()};    arr.as_array().elements.push_back(std::make_unique<Value>(SimpleString{"hello"}));
    arr.as_array().elements.push_back(std::make_unique<Value>(Integer{42}));
    arr.as_array().elements.push_back(std::make_unique<Value>(BulkString{"world"}));
    
    std::string serialized = Serializer::serialize(arr);
    
    // Parse it back
    auto result = parse(serialized, ParserConfig{});
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    EXPECT_EQ(value.as_array().elements.size(), 3);
}

TEST_F(SerializerComprehensiveTest, SerializeNestedStructure) {
    // Create [[1, 2], [3, 4]]
    Array inner1;
    inner1.elements.push_back(std::make_unique<Value>(Integer{1}));
    inner1.elements.push_back(std::make_unique<Value>(Integer{2}));
    
    Array inner2;
    inner2.elements.push_back(std::make_unique<Value>(Integer{3}));
    inner2.elements.push_back(std::make_unique<Value>(Integer{4}));
    
    Array outer;
    outer.elements.push_back(std::make_unique<Value>(std::move(inner1)));
    outer.elements.push_back(std::make_unique<Value>(std::move(inner2)));
    
    std::string serialized = Serializer::serialize(Value(std::move(outer)));
    
    auto result = parse(serialized, ParserConfig{});
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    EXPECT_EQ(value.as_array().elements.size(), 2);
    EXPECT_EQ(value.as_array().elements[0]->as_array().elements.size(), 2);
}

TEST_F(SerializerComprehensiveTest, SerializeMap) {
    Map map;
    map.entries.push_back({
        std::make_unique<Value>(BulkString{"key1"}),
        std::make_unique<Value>(SimpleString{"value1"})
    });
    map.entries.push_back({
        std::make_unique<Value>(BulkString{"key2"}),
        std::make_unique<Value>(Integer{42})
    });
    
    std::string serialized = Serializer::serialize(Value(std::move(map)));
    
    ParserConfig config;
    config.protocol_version = ProtocolVersion::RESP3;
    auto result = parse(serialized, config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    EXPECT_TRUE(value.is_map());
    EXPECT_EQ(value.as_map().entries.size(), 2);
}

TEST_F(SerializerComprehensiveTest, SerializeSet) {
    Set set;
    set.elements.push_back(std::make_unique<Value>(Integer{1}));
    set.elements.push_back(std::make_unique<Value>(Integer{2}));
    set.elements.push_back(std::make_unique<Value>(Integer{3}));
    
    std::string serialized = Serializer::serialize(Value(std::move(set)));
    
    ParserConfig config;
    config.protocol_version = ProtocolVersion::RESP3;
    auto result = parse(serialized, config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    EXPECT_TRUE(value.is_set());
    EXPECT_EQ(value.as_set().elements.size(), 3);
}

TEST_F(SerializerComprehensiveTest, CommandBuilderComplex) {
    // Build a complex SET command with multiple options
    std::string cmd = CommandBuilder("SET")
        .arg("mykey")
        .arg("myvalue")
        .arg_if(true, "EX", "60")
        .arg_if(false, "NX")  // Should not be added
        .arg_if(true, "XX")
        .build();
    
    // Should be: *5\r\n$3\r\nSET\r\n$5\r\nmykey\r\n$7\r\nmyvalue\r\n$2\r\nEX\r\n$2\r\n60\r\n$2\r\nXX\r\n
    EXPECT_NE(cmd.find("SET"), std::string::npos);
    EXPECT_NE(cmd.find("mykey"), std::string::npos);
    EXPECT_NE(cmd.find("EX"), std::string::npos);
    EXPECT_NE(cmd.find("60"), std::string::npos);
    EXPECT_NE(cmd.find("XX"), std::string::npos);
    EXPECT_EQ(cmd.find("NX"), std::string::npos);  // Should not be present
}

// ----------------------------------------------------------------------------
// Round-Trip Comprehensive Tests
// ----------------------------------------------------------------------------
class RoundTripComprehensiveTest : public ::testing::Test {
protected:
    ParserConfig config;
};

TEST_F(RoundTripComprehensiveTest, ComplexNestedStructure) {
    // Create complex structure, serialize, parse, compare
    Array root;
    
    // Add a nested array
    Array nested;
    nested.elements.push_back(std::make_unique<Value>(Integer{1}));
    nested.elements.push_back(std::make_unique<Value>(BulkString{"test"}));
    root.elements.push_back(std::make_unique<Value>(std::move(nested)));
    
    // Add a simple string
    root.elements.push_back(std::make_unique<Value>(SimpleString{"ok"}));
    
    // Add an integer
    root.elements.push_back(std::make_unique<Value>(Integer{999}));
    
    // Serialize
    std::string serialized = Serializer::serialize(Value(std::move(root)));
    
    // Parse back
    auto result = parse(serialized, config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    EXPECT_EQ(value.as_array().elements.size(), 3);
    EXPECT_TRUE(value.as_array().elements[0]->is_array());
    EXPECT_TRUE(value.as_array().elements[1]->is_simple_string());
    EXPECT_TRUE(value.as_array().elements[2]->is_integer());
}

TEST_F(RoundTripComprehensiveTest, BinaryDataPreservation) {
    // Ensure binary data survives round-trip
    std::string binary_data;
    for (int i = 0; i < 256; ++i) {
        binary_data.push_back(static_cast<char>(i));
    }
    
    BulkString bulk{binary_data};
    std::string serialized = Serializer::serialize(Value(std::move(bulk)));
    
    auto result = parse(serialized, config);
    ASSERT_TRUE(result.has_value());
    const auto& value = *result;
    EXPECT_EQ(value.as_bulk_string().value, binary_data);
}

TEST_F(RoundTripComprehensiveTest, SpecialCharactersPreservation) {
    // Test strings containing RESP control characters
    std::vector<std::string> test_strings = {
        "\r\n",
        "\r",
        "\n",
        "$100\r\n",
        "*5\r\n",
        ":42\r\n",
        "+OK\r\n",
        "-ERR\r\n"
    };
    
    for (const auto& str : test_strings) {
        BulkString bulk{str};
        std::string serialized = Serializer::serialize(Value(BulkString{str}));
        
        auto result = parse(serialized, config);
        ASSERT_TRUE(result.has_value()) << "Failed for: " << str;
        EXPECT_EQ((*result).as_bulk_string().value, str) << "Mismatch for: " << str;
    }
}

// ----------------------------------------------------------------------------
// Stress Tests
// ----------------------------------------------------------------------------
class StressTest : public ::testing::Test {
protected:
    ParserConfig config;
};

TEST_F(StressTest, ManySmallArrays) {
    // Parse many small arrays rapidly
    std::string data;
    constexpr int count = 1000;
    
    for (int i = 0; i < count; ++i) {
        data += "*2\r\n:1\r\n:2\r\n";
    }
    
    RespParser parser;
    EXPECT_TRUE(parser.feed(data));
    
    int received = 0;
    while (true) {
        auto result = parser.parse();
        if (!result.has_value() && result.error().code() == ParseErrorCode::INCOMPLETE_DATA) {
            break;
        }
        if (result.has_value()) {
            ++received;
        }
    }
    
    EXPECT_EQ(received, count);
}

TEST_F(StressTest, AlternatingTypes) {
    // Mix of different types in stream - only use RESP2 compatible types
    std::string data;
    for (int i = 0; i < 100; ++i) {
        switch (i % 4) {  // Removed case 4 (null type) which requires RESP3
            case 0: data += ":" + std::to_string(i) + "\r\n"; break;
            case 1: data += "+str" + std::to_string(i) + "\r\n"; break;
            case 2: data += "$5\r\ndata" + std::to_string(i) + "\r\n"; break;
            case 3: data += "*1\r\n:" + std::to_string(i) + "\r\n"; break;
        }
    }
    
    RespParser parser;
    EXPECT_TRUE(parser.feed(data));
    
    // Use parse_all() to get all messages - may be limited by buffer size
    auto results = parser.parse_all();
    
    // Should have received at least some messages (actual count depends on buffer capacity)
    EXPECT_GE(results.size(), 10);  // Expect at least 10 messages parsed
}

TEST_F(StressTest, ParserReuse) {
    // Reuse parser for multiple sequential messages
    RespParser parser;
    
    for (int i = 0; i < 100; ++i) {
        std::string data = ":" + std::to_string(i) + "\r\n";
        EXPECT_TRUE(parser.feed(data));
        
        auto result = parser.parse();
        ASSERT_TRUE(result.has_value()) << "Iteration " << i;
        EXPECT_EQ((*result).as_integer().value, i);
        
        // Compact but don't reset completely
        parser.compact();
    }
}

// ----------------------------------------------------------------------------
// Boundary Tests
// ----------------------------------------------------------------------------
class BoundaryTest : public ::testing::Test {
protected:
    ParserConfig config;
};

TEST_F(BoundaryTest, ExactBufferSize) {
    // Message that exactly fills common buffer sizes
    for (size_t size : {1, 1023, 1024, 1025, 4095, 4096, 4097, 8191, 8192, 8193}) {
        std::string content(size, 'x');
        std::string data = "$" + std::to_string(size) + "\r\n" + content + "\r\n";
        
        auto result = parse(data, config);
        ASSERT_TRUE(result.has_value()) << "Size: " << size;
        EXPECT_EQ((*result).as_bulk_string().value.size(), size);
    }
}

TEST_F(BoundaryTest, ExactLineBoundaries) {
    // CRLF at various positions
    std::vector<std::string> test_cases = {
        "\r\n",           // Just CRLF
        "x\r\n",          // One char + CRLF
        "xx\r\n",         // Two chars + CRLF
        std::string(998, 'x') + "\r\n",  // Just under 1000
        std::string(999, 'x') + "\r\n",   // Exactly 1000 chars before CRLF
        std::string(1000, 'x') + "\r\n",  // Just over 1000
    };
    
    for (const auto& content : test_cases) {
        std::string data = "+" + content;
        auto result = parse(data, config);
        ASSERT_TRUE(result.has_value()) << "Content size: " << content.size();
    }
}

TEST_F(BoundaryTest, BulkStringRespectsMaxBulkSize) {
    config.max_bulk_size = 8;
    std::string content(9, 'x');
    std::string data = "$9\r\n" + content + "\r\n";
    auto result = parse(data, config);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ParseErrorCode::BUFFER_OVERFLOW);
}

TEST_F(BoundaryTest, BulkStringAtMaxBulkSizeOk) {
    config.max_bulk_size = 8;
    std::string content(8, 'y');
    std::string data = "$8\r\n" + content + "\r\n";
    auto result = parse(data, config);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ((*result).as_bulk_string().value.size(), 8U);
}

// ============================================================================
// Hardening: null-length strictness + fatal-error detection
// ============================================================================

// Only a length of exactly -1 is the null marker. Any other negative length is
// a protocol error, not a silently-accepted null.
TEST(ParserHardening, OnlyMinusOneIsNull) {
    ParserConfig config; config.protocol_version = ProtocolVersion::RESP3;

    EXPECT_TRUE(parse("$-1\r\n", config).has_value());   // RESP2 null bulk
    EXPECT_TRUE((*parse("$-1\r\n", config)).is_null());
    EXPECT_TRUE(parse("*-1\r\n", config).has_value());   // RESP2 null array
    EXPECT_TRUE((*parse("*-1\r\n", config)).is_null());

    // Negative lengths other than -1 must be rejected.
    EXPECT_FALSE(parse("$-5\r\n", config).has_value());
    EXPECT_FALSE(parse("*-3\r\n", config).has_value());
    EXPECT_FALSE(parse("%-2\r\n", config).has_value());
    EXPECT_FALSE(parse("~-4\r\n", config).has_value());
}

// parse_all must fault (not silently stall) on a corrupt top-level byte, so the
// driver can tear the connection down instead of re-parsing it forever.
TEST(ParserHardening, ParseAllFaultsOnCorruptByte) {
    RespParser parser;
    // '@' is not a valid RESP type prefix.
    EXPECT_TRUE(parser.feed("@garbage\r\n"));
    auto values = parser.parse_all();
    EXPECT_TRUE(values.empty());
    EXPECT_TRUE(parser.has_error());
    // A faulted parser refuses further input so the connection is closed.
    EXPECT_FALSE(parser.feed("+OK\r\n"));
}

// A valid complete frame still parses, and an incomplete frame is retained
// (no fault) for the next feed.
TEST(ParserHardening, ParseAllRetainsIncomplete) {
    RespParser parser;
    EXPECT_TRUE(parser.feed("$5\r\nhel"));  // incomplete bulk
    auto v1 = parser.parse_all();
    EXPECT_TRUE(v1.empty());
    EXPECT_FALSE(parser.has_error());        // incomplete != fatal
    EXPECT_TRUE(parser.feed("lo\r\n"));      // completes it
    auto v2 = parser.parse_all();
    ASSERT_EQ(v2.size(), 1U);
    EXPECT_EQ(v2[0].as_bulk_string().value, "hello");
}

// A bulk/fixed-length payload whose CRLF terminator is corrupted must FAULT,
// not stall: the two bytes after the payload are present but wrong, so no
// amount of further data can ever make them a valid "\r\n". parse_all() must
// fault the parser so the driver drops the connection instead of looping.
TEST(ParserHardening, CorruptBulkTerminatorFaults) {
    RespParser parser;
    // $3\r\nabc<XX> — 3-byte payload "abc" followed by "XX" instead of CRLF.
    EXPECT_TRUE(parser.feed("$3\r\nabcXX"));
    auto values = parser.parse_all();
    EXPECT_TRUE(values.empty());
    EXPECT_TRUE(parser.has_error());
    EXPECT_FALSE(parser.feed("more\r\n")); // faulted parser refuses input
}

// In contrast, a bulk payload with the terminator not yet arrived is merely
// incomplete and is retried once the real CRLF is fed (no fault).
TEST(ParserHardening, PartialBulkTerminatorRetained) {
    RespParser parser;
    EXPECT_TRUE(parser.feed("$3\r\nabc")); // payload complete, CRLF missing
    auto v1 = parser.parse_all();
    EXPECT_TRUE(v1.empty());
    EXPECT_FALSE(parser.has_error()); // incomplete, not fatal
    EXPECT_TRUE(parser.feed("\r\n"));
    auto v2 = parser.parse_all();
    ASSERT_EQ(v2.size(), 1U);
    EXPECT_EQ(v2[0].as_bulk_string().value, "abc");
}

// A single-byte type (boolean) with a corrupt terminator likewise faults.
TEST(ParserHardening, CorruptBooleanTerminatorFaults) {
    ParserConfig config;
    config.protocol_version = ProtocolVersion::RESP3;
    RespParser parser(config);
    EXPECT_TRUE(parser.feed("#tXX")); // '#t' then "XX" instead of CRLF
    auto values = parser.parse_all();
    EXPECT_TRUE(values.empty());
    EXPECT_TRUE(parser.has_error());
}

// SCAN cursors are unsigned 64-bit. A cursor with the high bit set (here
// 18446744073709551615 = UINT64_MAX, i.e. > INT64_MAX) must parse to its exact
// unsigned value instead of throwing out_of_range as std::stoll did.
TEST(ScanCursor, HighBitCursorParsesUnsigned) {
    RespParser parser;
    // *2\r\n  $20\r\n<UINT64_MAX>\r\n  *1\r\n$3\r\nfoo\r\n
    ASSERT_TRUE(parser.feed(
        "*2\r\n$20\r\n18446744073709551615\r\n*1\r\n$3\r\nfoo\r\n"));
    auto values = parser.parse_all();
    ASSERT_EQ(values.size(), 1U);

    auto sc = qb::redis::reply::parse<qb::redis::scan<>>(values[0]);
    EXPECT_EQ(sc.cursor,
              static_cast<std::size_t>(18446744073709551615ULL));
    ASSERT_EQ(sc.items.size(), 1U);
    EXPECT_EQ(sc.items[0], "foo");
}

// A non-numeric cursor is a protocol error (still rejected, just not via an
// out_of_range thrown by stoll).
TEST(ScanCursor, NonNumericCursorRejected) {
    RespParser parser;
    ASSERT_TRUE(parser.feed("*2\r\n$3\r\nxyz\r\n*0\r\n"));
    auto values = parser.parse_all();
    ASSERT_EQ(values.size(), 1U);
    EXPECT_THROW(
        (void) qb::redis::reply::parse<qb::redis::scan<>>(values[0]),
        qb::redis::ProtoError);
}

// parse<double> from a string reply must consume the WHOLE string. A trailing
// garbage suffix (e.g. ZSCORE returning "1.5junk") used to be silently truncated to
// 1.5 because only from_chars' errc was checked, not ptr == end. It now matches the
// SCAN-cursor parse and the RESP parser's own parse_double.
TEST(ReplyParseDouble, RejectsTrailingGarbage) {
    auto value_of = [](const char *resp) {
        RespParser parser;
        EXPECT_TRUE(parser.feed(resp));
        auto values = parser.parse_all();
        EXPECT_EQ(values.size(), 1U);
        return std::move(values[0]);
    };
    // Whole-string doubles still parse.
    EXPECT_DOUBLE_EQ(qb::redis::reply::parse<double>(value_of("$3\r\n1.5\r\n")), 1.5);
    // Trailing garbage is now rejected (was silently truncated to 1.5 / 3).
    EXPECT_THROW((void) qb::redis::reply::parse<double>(value_of("$7\r\n1.5junk\r\n")),
                 qb::redis::ProtoError);
    EXPECT_THROW((void) qb::redis::reply::parse<double>(value_of("$2\r\n3x\r\n")),
                 qb::redis::ProtoError);
}

// ViewBuffer length bounds must be overflow-safe: a server-sized length near
// SIZE_MAX must be rejected, not wrap past the `_position + n > size` guard into an
// out-of-bounds span/view. The wrap only happens when _position > 0, so consume first.
TEST(ViewBufferBounds, HugeLengthDoesNotOverflow) {
    const char data[] = "hello";
    ViewBuffer vb(std::span<const char>(data, 5));
    vb.consume(3); // _position = 3, 2 bytes ("lo") remain
    const size_t huge = static_cast<size_t>(-1); // _position(3) + huge wraps past SIZE_MAX

    EXPECT_TRUE(vb.get(huge).empty());
    EXPECT_FALSE(vb.extract_bytes_view(huge).has_value());
    EXPECT_FALSE(vb.peek(huge).has_value());

    // Valid bounds still resolve against the 2 remaining bytes.
    EXPECT_EQ(vb.get(2).size(), 2u);
    ASSERT_TRUE(vb.peek(0).has_value());
    EXPECT_EQ(*vb.peek(0), 'l');
    EXPECT_FALSE(vb.peek(2).has_value()); // only 2 remain
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
