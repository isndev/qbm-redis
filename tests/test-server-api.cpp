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
#include "../redis.h"
#include "../server_reply.h"

// Test that the modern server API compiles and types are correct
TEST(ServerAPITest, TypesCompile) {
    // Test ServerReply with integer
    qb::redis::ServerReply<int64_t> int_reply;
    int_reply.ok = true;
    int_reply.value = 42;
    EXPECT_TRUE(int_reply.is_ok());
    EXPECT_EQ(int_reply.result(), 42);
    
    // Test ServerReply with string
    qb::redis::ServerReply<std::string> str_reply;
    str_reply.ok = true;
    str_reply.value = "test";
    EXPECT_TRUE(str_reply.is_ok());
    EXPECT_EQ(str_reply.result(), "test");
    
    // Test ServerReply error case
    qb::redis::ServerReply<std::string> error_reply;
    error_reply.ok = false;
    error_reply.error = "ERR something wrong";
    EXPECT_FALSE(error_reply.is_ok());
    EXPECT_EQ(error_reply.error_message(), "ERR something wrong");
    
    // Test void specialization
    qb::redis::ServerReply<void> void_reply;
    void_reply.ok = true;
    EXPECT_TRUE(void_reply.is_ok());
    
    qb::redis::ServerReply<void> void_error;
    void_error.ok = false;
    void_error.error = "error";
    EXPECT_FALSE(void_error.is_ok());
}

// Test ValueExtractor with various types
TEST(ServerAPITest, ValueExtractor) {
    // Create a simple string value
    qb::redis::parser::Value val(qb::redis::parser::SimpleString{"OK"});
    
    qb::redis::ValueExtractor extractor(val);
    
    EXPECT_TRUE(extractor.as_string_view().has_value());
    EXPECT_EQ(*extractor.as_string_view(), "OK");
    EXPECT_FALSE(extractor.as_integer().has_value());
    EXPECT_FALSE(extractor.is_null());
    
    // Test with integer
    qb::redis::parser::Value int_val(qb::redis::parser::Integer{42});
    qb::redis::ValueExtractor int_extractor(int_val);
    
    EXPECT_TRUE(int_extractor.as_integer().has_value());
    EXPECT_EQ(*int_extractor.as_integer(), 42);
    EXPECT_FALSE(int_extractor.as_string_view().has_value());
}

// Test ValueExtractor with unique_ptr
TEST(ServerAPITest, ValueExtractorUniquePtr) {
    auto val = std::make_unique<qb::redis::parser::Value>(
        qb::redis::parser::BulkString{"hello world"}
    );
    
    qb::redis::ValueExtractor extractor(val);
    
    EXPECT_TRUE(extractor.as_string_view().has_value());
    EXPECT_EQ(*extractor.as_string_view(), "hello world");
}

// Test extract_string helper
TEST(ServerAPITest, ExtractStringHelper) {
    qb::redis::parser::Value val(qb::redis::parser::SimpleString{"test"});
    
    auto result = qb::redis::extract_string(val);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "test");
    
    // Test with null
    qb::redis::parser::Value null_val(qb::redis::parser::Null{});
    auto null_result = qb::redis::extract_string(null_val);
    EXPECT_FALSE(null_result.has_value());
    EXPECT_EQ(null_result.error(), "null value");
    
    // Test with wrong type
    qb::redis::parser::Value int_val(qb::redis::parser::Integer{42});
    auto int_result = qb::redis::extract_string(int_val);
    EXPECT_FALSE(int_result.has_value());
}

// Test extract_integer helper
TEST(ServerAPITest, ExtractIntegerHelper) {
    qb::redis::parser::Value val(qb::redis::parser::Integer{123});
    
    auto result = qb::redis::extract_integer(val);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 123);
}

// Test extract_string_array helper
TEST(ServerAPITest, ExtractStringArrayHelper) {
    // Create array of strings
    qb::redis::parser::Array arr;
    arr.elements.push_back(std::make_unique<qb::redis::parser::Value>(
        qb::redis::parser::BulkString{"first"}
    ));
    arr.elements.push_back(std::make_unique<qb::redis::parser::Value>(
        qb::redis::parser::BulkString{"second"}
    ));
    
    qb::redis::parser::Value val(std::move(arr));
    
    auto result = qb::redis::extract_string_array(val);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 2);
    EXPECT_EQ(result.value()[0], "first");
    EXPECT_EQ(result.value()[1], "second");
}

// Test AsyncResult
TEST(ServerAPITest, AsyncResult) {
    // Test success case
    qb::redis::AsyncResult<int> success(42);
    EXPECT_TRUE(success.is_ok());
    EXPECT_FALSE(success.has_error());
    EXPECT_EQ(success.value(), 42);
    EXPECT_TRUE(success);  // operator bool
    
    // Test error case
    qb::redis::AsyncResult<int> error(std::string("failure"));
    EXPECT_FALSE(error.is_ok());
    EXPECT_TRUE(error.has_error());
    EXPECT_EQ(error.error(), "failure");
    EXPECT_FALSE(error);
    
    // Test void specialization success
    qb::redis::AsyncResult<void> void_success;
    EXPECT_TRUE(void_success.is_ok());
    
    // Test void specialization error
    qb::redis::AsyncResult<void> void_error(std::string("void error"));
    EXPECT_FALSE(void_error.is_ok());
    EXPECT_EQ(void_error.error(), "void error");
}

// Test extract_stream_id
TEST(ServerAPITest, ExtractStreamIdHelper) {
    qb::redis::parser::Value val(qb::redis::parser::BulkString{"1234567890-0"});
    
    auto result = qb::redis::extract_stream_id(val);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value().timestamp, 1234567890);
    EXPECT_EQ(result.value().sequence, 0);
    
    // Test invalid format
    qb::redis::parser::Value invalid_val(qb::redis::parser::BulkString{"invalid"});
    auto invalid_result = qb::redis::extract_stream_id(invalid_val);
    EXPECT_FALSE(invalid_result.has_value());
}

// Test error handling with ValueExtractor
TEST(ServerAPITest, ValueExtractorError) {
    qb::redis::parser::Value err_val(qb::redis::parser::SimpleError{"ERR test error"});
    qb::redis::ValueExtractor extractor(err_val);
    
    EXPECT_TRUE(extractor.is_error());
    EXPECT_EQ(extractor.get_error_message(), "ERR test error");
}

// Test null handling
TEST(ServerAPITest, ValueExtractorNull) {
    qb::redis::parser::Value null_val(qb::redis::parser::Null{});
    qb::redis::ValueExtractor extractor(null_val);
    
    EXPECT_TRUE(extractor.is_null());
    EXPECT_FALSE(extractor.as_string_view().has_value());
    EXPECT_FALSE(extractor.as_integer().has_value());
}

// Test array iteration through ValueExtractor
TEST(ServerAPITest, ValueExtractorArray) {
    qb::redis::parser::Array arr;
    arr.elements.push_back(std::make_unique<qb::redis::parser::Value>(
        qb::redis::parser::Integer{1}
    ));
    arr.elements.push_back(std::make_unique<qb::redis::parser::Value>(
        qb::redis::parser::Integer{2}
    ));
    arr.elements.push_back(std::make_unique<qb::redis::parser::Value>(
        qb::redis::parser::Integer{3}
    ));
    
    qb::redis::parser::Value val(std::move(arr));
    qb::redis::ValueExtractor extractor(val);
    
    auto arr_opt = extractor.as_array();
    EXPECT_TRUE(arr_opt.has_value());
    EXPECT_EQ(arr_opt.value().get().size(), 3);
}

// Test map iteration through ValueExtractor
TEST(ServerAPITest, ValueExtractorMap) {
    qb::redis::parser::Map map;
    map.entries.push_back(std::make_pair(
        std::make_unique<qb::redis::parser::Value>(qb::redis::parser::BulkString{"key1"}),
        std::make_unique<qb::redis::parser::Value>(qb::redis::parser::BulkString{"value1"})
    ));
    
    qb::redis::parser::Value val(std::move(map));
    qb::redis::ValueExtractor extractor(val);
    
    auto map_opt = extractor.as_map();
    EXPECT_TRUE(map_opt.has_value());
    EXPECT_EQ(map_opt.value().get().size(), 1);
}
