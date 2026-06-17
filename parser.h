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

#ifndef QBM_REDIS_PARSER_H
#define QBM_REDIS_PARSER_H

/**
 * @file parser.h
 * @brief Main include file for the QB Redis Protocol Parser
 * 
 * This is a world-class C++20/23 implementation of the Redis protocol (RESP2/RESP3).
 * It provides zero-copy parsing where possible, full RESP3 support, and seamless
 * integration with the QB actor framework.
 * 
 * @author QB Team
 * @version 1.0.0
 * 
 * Features:
 * - Complete RESP2 support (Simple String, Error, Integer, Bulk String, Array)
 * - Complete RESP3 support (Null, Boolean, Double, Big Number, Bulk Error,
 *   Verbatim String, Map, Attribute, Set, Push)
 * - Streaming parser for async I/O
 * - Zero-copy parsing from contiguous buffers
 * - Modern C++ features (qb::expected, std::variant, std::span)
 * - Full hiredis API compatibility for drop-in replacement
 * 
 * Usage:
 * @code
 * // Simple one-shot parsing
 * auto result = qb::redis::parser::parse("+OK\r\n");
 * if (result) {
 *     auto& value = *result;
 *     // Use value...
 * }
 *
 * // Streaming parser for async I/O
 * qb::redis::parser::RespParser parser;
 * parser.feed(data);
 * auto replies = parser.parse_all();  // returns std::vector<Value>
 *
 * // Command building (fluent)
 * qb::redis::parser::CommandBuilder cmd("SET");
 * cmd.arg("mykey").arg("myvalue").arg_if(true, "EX", "60");
 * auto serialized = cmd.build();  // returns RESP wire bytes
 * @endcode
 */

// Core types and protocol definitions
#include "parser/types.h"

// Buffer management
#include "parser/buffer.h"

// Main parser
#include "parser/parser.h"

// Serializer
#include "parser/serializer.h"

// Version information
#define QBM_REDIS_PARSER_VERSION_MAJOR 1
#define QBM_REDIS_PARSER_VERSION_MINOR 0
#define QBM_REDIS_PARSER_VERSION_PATCH 0

namespace qb::redis::parser {

/**
 * @brief Get parser version string
 */
[[nodiscard]] inline constexpr const char* version() noexcept {
    return "1.0.0";
}

/**
 * @brief Get parser version number (encoded as 0x010000 for 1.0.0)
 */
[[nodiscard]] inline constexpr uint32_t version_number() noexcept {
    return (QBM_REDIS_PARSER_VERSION_MAJOR << 16) |
           (QBM_REDIS_PARSER_VERSION_MINOR << 8) |
           QBM_REDIS_PARSER_VERSION_PATCH;
}

/**
 * @brief Check if RESP3 is supported
 */
[[nodiscard]] inline constexpr bool supports_resp3() noexcept {
    return true;
}

/**
 * @brief Check if zero-copy parsing is available
 */
[[nodiscard]] inline constexpr bool supports_zero_copy() noexcept {
    return true;
}

} // namespace qb::redis::parser

#endif // QBM_REDIS_PARSER_H
