/**
 * @file parser/serializer.h
 * @brief RESP serializer and CommandBuilder for wire format
 */
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

#ifndef QBM_REDIS_PARSER_SERIALIZER_H
#define QBM_REDIS_PARSER_SERIALIZER_H

#include <charconv>
#include <cmath>
#include <span>
#include "types.h"

namespace qb::redis::parser {

// ============================================================================
// RESP Serializer - converts values to RESP wire format
// ============================================================================

/**
 * @class Serializer
 * @brief Converts Value and primitives to RESP2/RESP3 wire format
 */
class Serializer {
public:
    // Serialize to string
    [[nodiscard]] static std::string
    serialize(const Value &value) {
        std::string result;
        serialize_to(value, result);
        return result;
    }

    // Serialize to existing string (append)
    static void
    serialize_to(const Value &value, std::string &out) {
        std::visit([&out](const auto &v) { serialize_impl(v, out); }, static_cast<const ValueBase &>(value));
    }

    // Serialize simple command array
    template <typename... Args>
    [[nodiscard]] static std::string
    serialize_command(Args &&...args) {
        std::string result;
        append_array_header(result, sizeof...(Args));
        (append_bulk_string(result, std::forward<Args>(args)), ...);
        return result;
    }

    // Serialize array of strings (command with arguments)
    [[nodiscard]] static std::string
    serialize_command_array(std::span<const std::string> args) {
        std::string result;
        append_array_header(result, args.size());
        for (const auto &arg : args) {
            append_bulk_string(result, arg);
        }
        return result;
    }

    // HELLO command for protocol handshake
    [[nodiscard]] static std::string
    serialize_hello(ProtocolVersion version, const std::optional<std::string> &username = std::nullopt,
                    const std::optional<std::string> &password = std::nullopt) {
        std::vector<std::string> args;
        args.emplace_back("HELLO");
        args.push_back(std::to_string(static_cast<int>(version)));

        if (username && password) {
            args.emplace_back("AUTH");
            args.push_back(*username);
            args.push_back(*password);
        } else if (password) {
            args.emplace_back("AUTH");
            args.emplace_back("default");
            args.push_back(*password);
        }

        return serialize_command_array(std::span(args));
    }

private:
    template <typename T>
    static void serialize_impl(const T &value, std::string &out);

    // Null: _\r\n
    static void
    serialize_impl(const Null &, std::string &out) {
        out.push_back(type_id::NULL_);
        out.append("\r\n");
    }

    // Simple String: +...\r\n
    static void
    serialize_impl(const SimpleString &s, std::string &out) {
        out.push_back(type_id::SIMPLE_STRING);
        out.append(s.value);
        out.append("\r\n");
    }

    // Simple Error: -...\r\n
    static void
    serialize_impl(const SimpleError &e, std::string &out) {
        out.push_back(type_id::SIMPLE_ERROR);
        if (!e.prefix.empty()) {
            out.append(e.prefix);
            if (!e.message.empty()) {
                out.push_back(' ');
            }
        }
        out.append(e.message);
        out.append("\r\n");
    }

    // Integer: :\d+\r\n
    static void
    serialize_impl(const Integer &i, std::string &out) {
        out.push_back(type_id::INTEGER);
        char buf[32];
        auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), i.value);
        out.append(buf, ptr - buf);
        out.append("\r\n");
    }

    // Bulk String: $N\r\n...\r\n
    static void
    serialize_impl(const BulkString &s, std::string &out) {
        append_bulk_string(out, s.value);
    }

    // Boolean: #t\r\n or #f\r\n
    static void
    serialize_impl(const Boolean &b, std::string &out) {
        out.push_back(type_id::BOOLEAN);
        out.push_back(b.value ? 't' : 'f');
        out.append("\r\n");
    }

    // Double: ,...\r\n
    static void
    serialize_impl(const Double &d, std::string &out) {
        out.push_back(type_id::DOUBLE);

        if (std::isinf(d.value)) {
            out.append(d.value > 0 ? "inf" : "-inf");
        } else if (std::isnan(d.value)) {
            out.append("nan");
        } else {
            // Use to_chars: locale-independent, round-trip exact at 17 significant digits.
            char buf[64];
            auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), d.value, std::chars_format::general, 17);
            if (ec == std::errc{}) {
                out.append(buf, ptr - buf);
            } else {
                out.append("0"); // unreachable in practice
            }
        }

        out.append("\r\n");
    }

    // Big Number: (...)\r\n
    static void
    serialize_impl(const BigNumber &n, std::string &out) {
        out.push_back(type_id::BIG_NUMBER);
        out.append(n.value);
        out.append("\r\n");
    }

    // Bulk Error: !N\r\n...\r\n
    static void
    serialize_impl(const BulkError &e, std::string &out) {
        std::string error_data;
        if (!e.prefix.empty()) {
            error_data.append(e.prefix);
            if (!e.message.empty()) {
                error_data.push_back(' ');
            }
        }
        error_data.append(e.message);

        out.push_back(type_id::BULK_ERROR);
        char buf[32];
        auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), static_cast<int64_t>(error_data.size()));
        out.append(buf, ptr - buf);
        out.append("\r\n");
        out.append(error_data);
        out.append("\r\n");
    }

    // Verbatim String: =N\r\n<encoding>:<data>\r\n
    static void
    serialize_impl(const VerbatimString &v, std::string &out) {
        size_t total_len = 4 + v.value.size(); // 3 for encoding + 1 for colon + value

        out.push_back(type_id::VERBATIM_STRING);
        char buf[32];
        auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), static_cast<int64_t>(total_len));
        out.append(buf, ptr - buf);
        out.append("\r\n");
        out.append(v.encoding, 3);
        out.push_back(':');
        out.append(v.value);
        out.append("\r\n");
    }

    // Array: *N\r\n<elements...>
    static void
    serialize_impl(const Array &a, std::string &out) {
        append_array_header(out, a.elements.size());
        for (const auto &elem : a.elements) {
            serialize_to(*elem, out);
        }
    }

    // Map: %N\r\n<key1><val1><key2><val2>...
    static void
    serialize_impl(const Map &m, std::string &out) {
        append_map_header(out, m.entries.size());
        for (const auto &[key, val] : m.entries) {
            serialize_to(*key, out);
            serialize_to(*val, out);
        }
    }

    // Attribute: |N\r\n<N key-value pairs><actual-reply>
    // Per RESP3 spec the attribute block PRECEDES the real reply, so we must
    // serialize both the N metadata pairs AND the following actual value.
    static void
    serialize_impl(const Attribute &a, std::string &out) {
        append_attribute_header(out, a.data.entries.size());
        for (const auto &[key, val] : a.data.entries) {
            serialize_to(*key, out);
            serialize_to(*val, out);
        }
        if (a.value) {
            serialize_to(*a.value, out);
        } else {
            serialize_impl(Null{}, out);
        }
    }

    // Set: ~N\r\n<elements...>
    static void
    serialize_impl(const Set &s, std::string &out) {
        append_set_header(out, s.elements.size());
        for (const auto &elem : s.elements) {
            serialize_to(*elem, out);
        }
    }

    // Push: >N\r\n<elements...>
    static void
    serialize_impl(const Push &p, std::string &out) {
        append_push_header(out, p.elements.size());
        for (const auto &elem : p.elements) {
            serialize_to(*elem, out);
        }
    }

    // Helper functions
    static void
    append_crlf(std::string &out) {
        out.append("\r\n");
    }

    static void
    append_bulk_string(std::string &out, std::string_view str) {
        out.push_back(type_id::BULK_STRING);
        char buf[32];
        auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), static_cast<int64_t>(str.size()));
        out.append(buf, ptr - buf);
        out.append("\r\n");
        out.append(str);
        out.append("\r\n");
    }

    static void
    append_array_header(std::string &out, size_t count) {
        out.push_back(type_id::ARRAY);
        char buf[32];
        auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), static_cast<int64_t>(count));
        out.append(buf, ptr - buf);
        out.append("\r\n");
    }

    static void
    append_map_header(std::string &out, size_t count) {
        out.push_back(type_id::MAP);
        char buf[32];
        auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), static_cast<int64_t>(count));
        out.append(buf, ptr - buf);
        out.append("\r\n");
    }

    static void
    append_set_header(std::string &out, size_t count) {
        out.push_back(type_id::SET);
        char buf[32];
        auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), static_cast<int64_t>(count));
        out.append(buf, ptr - buf);
        out.append("\r\n");
    }

    static void
    append_push_header(std::string &out, size_t count) {
        out.push_back(type_id::PUSH);
        char buf[32];
        auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), static_cast<int64_t>(count));
        out.append(buf, ptr - buf);
        out.append("\r\n");
    }

    static void
    append_attribute_header(std::string &out, size_t count) {
        out.push_back(type_id::ATTRIBUTE);
        char buf[32];
        auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), static_cast<int64_t>(count));
        out.append(buf, ptr - buf);
        out.append("\r\n");
    }
};

// ============================================================================
// Command builder - fluent interface for building Redis commands
// ============================================================================

/**
 * @class CommandBuilder
 * @brief Fluent builder for Redis commands (RESP array format)
 */
class CommandBuilder {
public:
    CommandBuilder() {
        _parts.reserve(4);
    }

    explicit CommandBuilder(std::string_view command) {
        _parts.reserve(4);
        _parts.emplace_back(command);
    }

    // Add argument
    CommandBuilder &
    arg(std::string_view value) {
        _parts.emplace_back(value);
        return *this;
    }

    CommandBuilder &
    arg(const char *value) {
        _parts.emplace_back(value);
        return *this;
    }

    CommandBuilder &
    arg(int64_t value) {
        _parts.push_back(std::to_string(value));
        return *this;
    }

    CommandBuilder &
    arg(int value) {
        _parts.push_back(std::to_string(value));
        return *this;
    }

    CommandBuilder &
    arg(double value) {
        char buf[64];
        auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), value, std::chars_format::general, 17);
        if (ec == std::errc{}) {
            _parts.emplace_back(buf, ptr - buf);
        } else {
            _parts.push_back(std::to_string(value)); // unreachable in practice
        }
        return *this;
    }

    // Add optional argument (only if has_value)
    CommandBuilder &
    arg_optional(const std::optional<std::string> &value) {
        if (value) {
            _parts.push_back(*value);
        }
        return *this;
    }

    // Add keyword + value if condition is true
    CommandBuilder &
    arg_if(bool condition, std::string_view keyword) {
        if (condition) {
            _parts.emplace_back(keyword);
        }
        return *this;
    }

    CommandBuilder &
    arg_if(bool condition, std::string_view keyword, std::string_view value) {
        if (condition) {
            _parts.emplace_back(keyword);
            _parts.emplace_back(value);
        }
        return *this;
    }

    // Build command
    [[nodiscard]] std::string
    build() const {
        return Serializer::serialize_command_array(std::span(_parts));
    }

    // Build with raw data appended (for binary data)
    [[nodiscard]] std::string
    build_with_raw(std::span<const char> extra) const {
        std::string result = build();
        result.append(extra.data(), extra.size());
        return result;
    }

    [[nodiscard]] size_t
    arg_count() const noexcept {
        return _parts.size();
    }

    void
    clear() {
        _parts.clear();
    }

private:
    std::vector<std::string> _parts;
};

// ============================================================================
// Utility functions
// ============================================================================

// Quick serialization helpers
[[nodiscard]] inline std::string
serialize_simple_string(std::string_view str) {
    std::string result;
    result.push_back(type_id::SIMPLE_STRING);
    result.append(str);
    result.append("\r\n");
    return result;
}

[[nodiscard]] inline std::string
serialize_error(std::string_view error) {
    std::string result;
    result.push_back(type_id::SIMPLE_ERROR);
    result.append(error);
    result.append("\r\n");
    return result;
}

[[nodiscard]] inline std::string
serialize_integer(int64_t value) {
    std::string result;
    result.push_back(type_id::INTEGER);
    char buf[32];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), value);
    result.append(buf, ptr - buf);
    result.append("\r\n");
    return result;
}

[[nodiscard]] inline std::string
serialize_bulk_string(std::string_view str) {
    std::string result;
    result.push_back(type_id::BULK_STRING);
    char buf[32];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), static_cast<int64_t>(str.size()));
    result.append(buf, ptr - buf);
    result.append("\r\n");
    result.append(str);
    result.append("\r\n");
    return result;
}

[[nodiscard]] inline std::string
serialize_null() {
    return "_\r\n";
}

[[nodiscard]] inline std::string
serialize_array_header(size_t count) {
    std::string result;
    result.push_back(type_id::ARRAY);
    char buf[32];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), static_cast<int64_t>(count));
    result.append(buf, ptr - buf);
    result.append("\r\n");
    return result;
}

} // namespace qb::redis::parser

#endif // QBM_REDIS_PARSER_SERIALIZER_H
