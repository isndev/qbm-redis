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

#ifndef QBM_REDIS_PARSER_PARSER_H
#define QBM_REDIS_PARSER_PARSER_H

#include "types.h"
#include "buffer.h"
#include <stack>
#include <functional>
#include <expected>

namespace qb::redis::parser {

// ============================================================================
// Parser configuration
// ============================================================================
struct ParserConfig {
    ProtocolVersion protocol_version = ProtocolVersion::RESP3;
    size_t max_nesting_depth = 64;          // Max aggregate nesting
    size_t max_bulk_size = 512 * 1024 * 1024;  // 512MB
    size_t max_array_size = 1'000'000;      // Max array elements
    bool strict_mode = false;               // Reject trailing data after parse
};

// ============================================================================
// Parser state
// ============================================================================
enum class State {
    READY,              // Waiting for type byte
    PARSING_LENGTH,     // Parsing length line (for bulk/aggregate types)
    PARSING_SIMPLE,     // Parsing simple type data
    PARSING_BULK,       // Parsing bulk data
    PARSING_AGGREGATE,  // Parsing aggregate elements
    COMPLETE,           // Have complete value
    ERROR               // Error state
};

// ============================================================================
// Main RESP parser - streaming capable
// ============================================================================
class RespParser {
public:
    explicit RespParser(const ParserConfig& config = {})
        : _config(config), _state(State::READY), _current_depth(0) {}
    
    // Reset parser state
    void reset() {
        _state = State::READY;
        _current_depth = 0;
        _buffer.reset();
        _parse_stack = {};
        _pending_value.reset();
        _temp_data.clear();
    }
    
    // Get current state
    [[nodiscard]] State state() const noexcept { return _state; }
    [[nodiscard]] bool is_ready() const noexcept { return _state == State::READY; }
    [[nodiscard]] bool is_complete() const noexcept { return _state == State::COMPLETE; }
    [[nodiscard]] bool has_error() const noexcept { return _state == State::ERROR; }
    [[nodiscard]] bool is_parsing() const noexcept {
        return _state != State::READY && _state != State::COMPLETE && _state != State::ERROR;
    }
    
    // Feed data to parser (string_view version)
    bool feed(std::string_view data) {
        if (has_error()) return false;
        return _buffer.append(std::span<const char>(data.data(), data.size()));
    }
    
    // Feed span data to parser - explicit template to avoid ambiguity
    template<typename T>
    requires std::same_as<T, std::span<const char>>
    bool feed(T data) {
        if (has_error()) return false;
        return _buffer.append(data);
    }
    
    // Try to parse a complete value
    // Returns:
    //   - value if complete value parsed
    //   - ParseError with INCOMPLETE_DATA if need more data
    //   - ParseError with other code if parse error
    [[nodiscard]] ParseResult<Value> parse() {
        if (has_error()) {
            return make_parse_error(ParseErrorCode::PROTOCOL_ERROR, "Parser in error state");
        }
        
        // Fast path: try parse from view
        auto view_result = try_parse_from_view();
        if (view_result.has_value() || 
            (view_result.error().code() != ParseErrorCode::INCOMPLETE_DATA)) {
            return view_result;
        }
        
        // Need more data or must use buffer
        return try_parse_from_buffer();
    }
    
    // Parse all complete values from the internal buffer.
    //
    // Uses a non-destructive ViewBuffer over the compacted (contiguous) buffer.
    // On INCOMPLETE_DATA the ViewBuffer position is simply not committed, so no
    // bytes are lost and the next call re-parses from the same starting point
    // once more data has been fed in.  This correctly handles every RESP type
    // including arbitrarily nested arrays, maps, sets and push messages.
    [[nodiscard]] std::vector<Value> parse_all() {
        std::vector<Value> results;

        compact(); // Guarantee a single contiguous readable_span()

        while (true) {
            auto span = _buffer.readable_span();
            if (span.empty()) break;

            ViewBuffer view(span);
            auto result = parse_value(view, 0);

            if (!result.has_value()) {
                // Incomplete or protocol error – do NOT consume any bytes.
                break;
            }

            // Only commit the bytes that were actually consumed on success.
            _buffer.consume(view.position());
            results.push_back(std::move(*result));
        }

        return results;
    }

    // Returns true when the internal buffer appears to contain at least one
    // complete top-level value.  Used only as a fast-path pre-check; the
    // definitive answer comes from parse_all().
    [[nodiscard]] bool has_complete_value() const {
        if (_buffer.empty()) return false;
        auto span = _buffer.readable_span();
        if (span.empty()) return false;
        // A non-const temporary parser shares no mutable state with *this –
        // we only need to check whether a parse succeeds.
        ViewBuffer view(span);
        // peek at the type byte; if it does not exist we have no data
        auto type_opt = view.peek();
        if (!type_opt) return false;
        // For simple types a single CRLF is enough to confirm completeness
        char type = *type_opt;
        if (type == '+' || type == '-' || type == ':' ||
            type == '#' || type == ',' || type == '(' || type == '_') {
            return _buffer.find_crlf().has_value();
        }
        // For all other types (bulk, aggregate) we need to attempt a full parse.
        // The const_cast is safe: parse_value() only reads from the ViewBuffer.
        auto result = const_cast<RespParser *>(this)->parse_value(view, 0);
        return result.has_value();
    }
    
    // Get unparsed data in buffer
    [[nodiscard]] std::span<const char> unparsed_data() const {
        return _buffer.readable_span();
    }
    
    // Compact internal buffer
    void compact() {
        _buffer.compact();
    }

private:
    // Try to parse directly from buffer view (zero-copy path)
    [[nodiscard]] ParseResult<Value> try_parse_from_view() {
        // Get contiguous readable data
        auto span1 = _buffer.readable_span();
        if (span1.empty()) {
            return make_parse_error(ParseErrorCode::INCOMPLETE_DATA);
        }
        
        // Try to parse from first span
        ViewBuffer view(span1);
        auto result = parse_value(view, 0);
        
        if (result.has_value()) {
            // Success - consume bytes from main buffer
            size_t consumed = view.position();
            _buffer.consume(consumed);
            return result;
        }
        
        if (result.error().code() == ParseErrorCode::INCOMPLETE_DATA) {
            // Check if we need to look at second span (wrapped data)
            auto span2 = _buffer.readable_span_second();
            if (!span2.empty()) {
                // Need to handle wrapped data - fall back to buffered parse
                return make_parse_error(ParseErrorCode::INCOMPLETE_DATA);
            }
        }
        
        return result;
    }
    
    // Try to parse from internal buffer (copy path for wrapped data)
    [[nodiscard]] ParseResult<Value> try_parse_from_buffer() {
        // Extract a line to determine type
        auto line = _buffer.extract_line();
        if (!line) {
            return make_parse_error(ParseErrorCode::INCOMPLETE_DATA);
        }
        
        if (line->empty()) {
            return make_parse_error(ParseErrorCode::INVALID_TYPE, "Empty line");
        }
        
        char type = (*line)[0];
        std::string_view payload(line->data() + 1, line->size() - 1);
        
        return parse_by_type(type, payload);
    }
    
    [[nodiscard]] std::optional<std::string> extract_line_from_buffer() const {
        // Non-destructive extraction
        auto crlf = _buffer.find_crlf();
        if (!crlf) return std::nullopt;
        
        std::string result;
        result.reserve(*crlf);
        
        for (size_t i = 0; i < *crlf; ++i) {
            auto c = _buffer.peek(i);
            if (!c) break;
            result.push_back(*c);
        }
        
        return result;
    }
    
    // Parse value from view buffer
    [[nodiscard]] ParseResult<Value> parse_value(ViewBuffer& view, size_t depth) {
        if (depth > _config.max_nesting_depth) {
            return make_parse_error(ParseErrorCode::NESTING_TOO_DEEP);
        }
        
        auto type_opt = view.peek();
        if (!type_opt) {
            return make_parse_error(ParseErrorCode::INCOMPLETE_DATA);
        }
        
        char type = *type_opt;
        
        if (!is_valid_type_prefix(type)) {
            return make_parse_error(ParseErrorCode::INVALID_TYPE, 
                std::format("Invalid type prefix: {}", type));
        }
        
        // Check RESP3 types in RESP2 mode
        if (_config.protocol_version == ProtocolVersion::RESP2 && is_resp3_type(type)) {
            return make_parse_error(ParseErrorCode::INVALID_TYPE,
                std::format("RESP3 type not allowed in RESP2 mode: {}", type));
        }
        
        view.consume(1);  // Consume type byte
        
        // Handle simple types that don't have length prefix
        switch (type) {
            case type_id::NULL_:
                return parse_null(view);
            case type_id::BOOLEAN:
                return parse_boolean(view);
            case type_id::SIMPLE_STRING:
                return parse_simple_string(view);
            case type_id::SIMPLE_ERROR:
                return parse_simple_error(view);
            case type_id::INTEGER:
                return parse_integer(view);
            case type_id::DOUBLE:
                return parse_double(view);
            case type_id::BIG_NUMBER:
                return parse_big_number(view);
            default:
                // Aggregate or bulk types need length
                break;
        }
        
        // Parse length line
        auto line_opt = view.extract_line_view();
        if (!line_opt) {
            return make_parse_error(ParseErrorCode::INCOMPLETE_DATA);
        }
        
        return parse_bulk_or_aggregate(type, *line_opt, view, depth);
    }
    
    // Parse null: _\r\n
    [[nodiscard]] static ParseResult<Value> parse_null(ViewBuffer& view) {
        if (!view.skip_crlf()) {
            return make_parse_error(ParseErrorCode::INCOMPLETE_DATA);
        }
        return make_parse_result(Value(Null{}));
    }
    
    // Parse boolean: #t\r\n or #f\r\n
    [[nodiscard]] static ParseResult<Value> parse_boolean(ViewBuffer& view) {
        auto val_opt = view.peek();
        if (!val_opt) {
            return make_parse_error(ParseErrorCode::INCOMPLETE_DATA);
        }
        
        bool value;
        if (*val_opt == 't') {
            value = true;
        } else if (*val_opt == 'f') {
            value = false;
        } else {
            return make_parse_error(ParseErrorCode::INVALID_BOOLEAN,
                std::format("Invalid boolean value: {}", *val_opt));
        }
        
        view.consume(1);
        
        if (!view.skip_crlf()) {
            return make_parse_error(ParseErrorCode::INCOMPLETE_DATA);
        }
        
        return make_parse_result(Value(Boolean{value}));
    }
    
    // Parse simple string: +...\r\n
    [[nodiscard]] static ParseResult<Value> parse_simple_string(ViewBuffer& view) {
        auto line_opt = view.extract_line_view();
        if (!line_opt) {
            return make_parse_error(ParseErrorCode::INCOMPLETE_DATA);
        }
        
        return make_parse_result(Value(SimpleString{std::string(*line_opt)}));
    }
    
    // Parse simple error: -...\r\n
    [[nodiscard]] static ParseResult<Value> parse_simple_error(ViewBuffer& view) {
        auto line_opt = view.extract_line_view();
        if (!line_opt) {
            return make_parse_error(ParseErrorCode::INCOMPLETE_DATA);
        }
        
        // Parse prefix (first word)
        auto space_pos = line_opt->find(' ');
        std::string prefix;
        std::string message;
        
        if (space_pos == std::string_view::npos) {
            prefix = std::string(*line_opt);
        } else {
            prefix = std::string(line_opt->substr(0, space_pos));
            message = std::string(line_opt->substr(space_pos + 1));
        }
        
        return make_parse_result(Value(SimpleError{prefix, message}));
    }
    
    // Parse integer: :[+-]?\d+\r\n
    [[nodiscard]] static ParseResult<Value> parse_integer(ViewBuffer& view) {
        auto line_opt = view.extract_line_view();
        if (!line_opt) {
            return make_parse_error(ParseErrorCode::INCOMPLETE_DATA);
        }
        
        int64_t value = 0;
        if (!parse_integer(*line_opt, value)) {
            return make_parse_error(ParseErrorCode::INVALID_INTEGER,
                std::format("Invalid integer: {}", *line_opt));
        }
        
        return make_parse_result(Value(Integer{value}));
    }
    
    // Parse double: ,[+-]?\d*\.?\d*(e[+-]?\d+)?\r\n
    [[nodiscard]] static ParseResult<Value> parse_double(ViewBuffer& view) {
        auto line_opt = view.extract_line_view();
        if (!line_opt) {
            return make_parse_error(ParseErrorCode::INCOMPLETE_DATA);
        }
        
        double value = 0.0;
        if (!parse_double(*line_opt, value)) {
            return make_parse_error(ParseErrorCode::INVALID_DOUBLE,
                std::format("Invalid double: {}", *line_opt));
        }
        
        return make_parse_result(Value(Double{value}));
    }
    
    // Parse big number: ([+-]?\d+)\r\n
    [[nodiscard]] static ParseResult<Value> parse_big_number(ViewBuffer& view) {
        auto line_opt = view.extract_line_view();
        if (!line_opt) {
            return make_parse_error(ParseErrorCode::INCOMPLETE_DATA);
        }
        
        if (line_opt->empty()) {
            return make_parse_error(ParseErrorCode::INVALID_BIG_NUMBER, "Empty big number");
        }
        
        bool negative = false;
        size_t start = 0;
        
        if ((*line_opt)[0] == '-') {
            negative = true;
            start = 1;
        } else if ((*line_opt)[0] == '+') {
            start = 1;
        }
        
        // Validate digits
        for (size_t i = start; i < line_opt->size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>((*line_opt)[i]))) {
                return make_parse_error(ParseErrorCode::INVALID_BIG_NUMBER,
                    "Big number contains non-digit characters");
            }
        }
        
        return make_parse_result(Value(BigNumber{std::string(*line_opt), negative}));
    }
    
    // Parse bulk or aggregate types
    [[nodiscard]] ParseResult<Value> parse_bulk_or_aggregate(char type, 
                                                             std::string_view length_str,
                                                             ViewBuffer& view,
                                                             size_t depth) {
        int64_t len = 0;
        if (!parse_integer(length_str, len)) {
            return make_parse_error(ParseErrorCode::INVALID_LENGTH,
                std::format("Invalid length: {}", length_str));
        }
        
        // Handle null aggregate types (count = -1)
        if (len < 0) {
            return make_parse_result(Value(Null{}));
        }

        switch (type) {
            case type_id::BULK_STRING:
                return parse_bulk_string(len, view);
            case type_id::BULK_ERROR:
                return parse_bulk_error(len, view);
            case type_id::VERBATIM_STRING:
                return parse_verbatim_string(len, view);
            case type_id::ARRAY:
                return parse_array(static_cast<size_t>(len), view, depth);
            case type_id::SET:
                return parse_set(static_cast<size_t>(len), view, depth);
            case type_id::PUSH:
                return parse_push(static_cast<size_t>(len), view, depth);
            case type_id::MAP:
                return parse_map(static_cast<size_t>(len), view, depth);
            case type_id::ATTRIBUTE:
                return parse_attribute(static_cast<size_t>(len), view, depth);
            default:
                return make_parse_error(ParseErrorCode::INVALID_TYPE);
        }
    }
    
    // Parse bulk string: $N\r\n<data>\r\n
    [[nodiscard]] static ParseResult<Value> parse_bulk_string(int64_t len, ViewBuffer& view) {
        if (len < 0) {
            // Null bulk string in RESP2 mode
            return make_parse_result(Value(Null{}));
        }
        
        if (len > static_cast<int64_t>(512 * 1024 * 1024)) {
            return make_parse_error(ParseErrorCode::BUFFER_OVERFLOW, "Bulk string too large");
        }
        
        auto data_opt = view.extract_bytes_view(static_cast<size_t>(len));
        if (!data_opt) {
            return make_parse_error(ParseErrorCode::INCOMPLETE_DATA);
        }
        
        if (!view.skip_crlf()) {
            return make_parse_error(ParseErrorCode::INCOMPLETE_DATA);
        }
        
        return make_parse_result(Value(BulkString{std::string(*data_opt)}));
    }
    
    // Parse bulk error: !N\r\n<error>\r\n
    [[nodiscard]] static ParseResult<Value> parse_bulk_error(int64_t len, ViewBuffer& view) {
        if (len < 0) {
            return make_parse_error(ParseErrorCode::INVALID_LENGTH, "Negative bulk error length");
        }
        
        auto data_opt = view.extract_bytes(static_cast<size_t>(len));
        if (!data_opt) {
            return make_parse_error(ParseErrorCode::INCOMPLETE_DATA);
        }
        
        if (!view.skip_crlf()) {
            return make_parse_error(ParseErrorCode::INCOMPLETE_DATA);
        }
        
        // Parse prefix and message
        auto space_pos = data_opt->find(' ');
        std::string prefix;
        std::string message;
        
        if (space_pos == std::string::npos) {
            prefix = std::move(*data_opt);
        } else {
            prefix = data_opt->substr(0, space_pos);
            message = data_opt->substr(space_pos + 1);
        }
        
        return make_parse_result(Value(BulkError{prefix, message}));
    }
    
    // Parse verbatim string: =N\r\n<encoding>:<data>\r\n
    [[nodiscard]] static ParseResult<Value> parse_verbatim_string(int64_t len, ViewBuffer& view) {
        if (len < 4) {  // Minimum: "xxx:" (3 chars encoding + colon + at least 1 char)
            return make_parse_error(ParseErrorCode::INVALID_VERBATIM_FORMAT, "Verbatim string too short");
        }
        
        auto data_opt = view.extract_bytes(static_cast<size_t>(len));
        if (!data_opt) {
            return make_parse_error(ParseErrorCode::INCOMPLETE_DATA);
        }
        
        if (!view.skip_crlf()) {
            return make_parse_error(ParseErrorCode::INCOMPLETE_DATA);
        }
        
        // Find colon separator
        auto colon_pos = data_opt->find(':');
        if (colon_pos != 3) {
            return make_parse_error(ParseErrorCode::INVALID_VERBATIM_FORMAT,
                "Verbatim string encoding must be 3 characters");
        }
        
        VerbatimString result;
        std::memcpy(result.encoding, data_opt->data(), 3);
        result.value = data_opt->substr(4);
        
        return make_parse_result(Value(std::move(result)));
    }
    
    // Parse array: *N\r\n<elements...>
    [[nodiscard]] ParseResult<Value> parse_array(size_t count, ViewBuffer& view, size_t depth) {
        if (count > 1'000'000) {
            return make_parse_error(ParseErrorCode::BUFFER_OVERFLOW, "Array too large");
        }
        
        Array result;
        result.elements.reserve(count);
        
        for (size_t i = 0; i < count; ++i) {
            auto elem = parse_value(view, depth + 1);
            if (!elem.has_value()) {
                return elem;
            }
            result.elements.push_back(std::make_unique<Value>(std::move(*elem)));
        }
        
        return make_parse_result(Value(std::move(result)));
    }
    
    // Parse set: ~N\r\n<elements...>
    [[nodiscard]] ParseResult<Value> parse_set(size_t count, ViewBuffer& view, size_t depth) {
        if (count > 1'000'000) {
            return make_parse_error(ParseErrorCode::BUFFER_OVERFLOW, "Set too large");
        }
        
        Set result;
        result.elements.reserve(count);
        
        for (size_t i = 0; i < count; ++i) {
            auto elem = parse_value(view, depth + 1);
            if (!elem.has_value()) {
                return elem;
            }
            result.elements.push_back(std::make_unique<Value>(std::move(*elem)));
        }
        
        return make_parse_result(Value(std::move(result)));
    }
    
    // Parse push: >N\r\n<elements...>
    [[nodiscard]] ParseResult<Value> parse_push(size_t count, ViewBuffer& view, size_t depth) {
        if (count > 1'000'000) {
            return make_parse_error(ParseErrorCode::BUFFER_OVERFLOW, "Push too large");
        }
        
        Push result;
        result.elements.reserve(count);
        
        for (size_t i = 0; i < count; ++i) {
            auto elem = parse_value(view, depth + 1);
            if (!elem.has_value()) {
                return elem;
            }
            result.elements.push_back(std::make_unique<Value>(std::move(*elem)));
        }
        
        return make_parse_result(Value(std::move(result)));
    }
    
    // Parse map: %N\r\n<key1><value1><key2><value2>...
    [[nodiscard]] ParseResult<Value> parse_map(size_t count, ViewBuffer& view, size_t depth) {
        if (count > 500'000) {  // N pairs = 2N elements
            return make_parse_error(ParseErrorCode::BUFFER_OVERFLOW, "Map too large");
        }
        
        Map result;
        result.entries.reserve(count);
        
        for (size_t i = 0; i < count; ++i) {
            auto key = parse_value(view, depth + 1);
            if (!key.has_value()) {
                return key;
            }
            
            auto val = parse_value(view, depth + 1);
            if (!val.has_value()) {
                return val;
            }
            
            result.entries.emplace_back(std::make_unique<Value>(std::move(*key)), std::make_unique<Value>(std::move(*val)));
        }
        
        return make_parse_result(Value(std::move(result)));
    }
    
    // Parse attribute: |N\r\n<key1><value1>...<keyN><valueN><actual-reply>
    //
    // Per the RESP3 spec the attribute block PRECEDES the real reply.
    // We must parse both the N metadata pairs AND the following value so
    // that the whole thing is returned as a single logical Value.
    [[nodiscard]] ParseResult<Value> parse_attribute(size_t count, ViewBuffer& view, size_t depth) {
        if (count > 500'000) {
            return make_parse_error(ParseErrorCode::BUFFER_OVERFLOW, "Attribute too large");
        }

        Attribute result;
        result.data.entries.reserve(count);

        for (size_t i = 0; i < count; ++i) {
            auto key = parse_value(view, depth + 1);
            if (!key.has_value()) return key;

            auto val = parse_value(view, depth + 1);
            if (!val.has_value()) return val;

            result.data.entries.emplace_back(
                std::make_unique<Value>(std::move(*key)),
                std::make_unique<Value>(std::move(*val)));
        }

        // Parse the actual reply that the attribute decorates
        auto actual = parse_value(view, depth);
        if (!actual.has_value()) return actual;
        result.value = std::make_unique<Value>(std::move(*actual));

        return make_parse_result(Value(std::move(result)));
    }
    
    // Helper: parse integer from string
    [[nodiscard]] static bool parse_integer(std::string_view str, int64_t& out) {
        if (str.empty()) return false;
        
        size_t pos = 0;
        bool negative = false;
        
        if (str[0] == '-') {
            negative = true;
            pos = 1;
        } else if (str[0] == '+') {
            pos = 1;
        }
        
        if (pos >= str.size()) return false;
        
        // Special case: INT64_MIN (-9223372036854775808)
        // This is the only case where abs(value) > INT64_MAX
        if (negative && str.substr(pos) == "9223372036854775808") {
            out = std::numeric_limits<int64_t>::min();
            return true;
        }
        
        int64_t value = 0;
        for (; pos < str.size(); ++pos) {
            char c = str[pos];
            if (c < '0' || c > '9') return false;
            
            // Check overflow for positive values
            if (value > (std::numeric_limits<int64_t>::max() - (c - '0')) / 10) {
                return false;
            }
            
            value = value * 10 + (c - '0');
        }
        
        out = negative ? -value : value;
        return true;
    }
    
    // Helper: parse double from string (handles inf, -inf, nan).
    // Uses std::from_chars exclusively: locale-independent and faster than stod.
    [[nodiscard]] static bool parse_double(std::string_view str, double& out) {
        if (str == "inf" || str == "+inf") {
            out = std::numeric_limits<double>::infinity();
            return true;
        }
        if (str == "-inf") {
            out = -std::numeric_limits<double>::infinity();
            return true;
        }
        if (str == "nan") {
            out = std::numeric_limits<double>::quiet_NaN();
            return true;
        }

        double value = 0.0;
        auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value,
                                         std::chars_format::general);
        // Both the parse itself and full consumption of the string must succeed.
        if (ec != std::errc{} || ptr != str.data() + str.size()) return false;
        out = value;
        return true;
    }
    
    // Parse by type from buffered line
    [[nodiscard]] ParseResult<Value> parse_by_type(char type, std::string_view payload) {
        switch (type) {
            case type_id::SIMPLE_STRING:
                return make_parse_result(Value(SimpleString{std::string(payload)}));
            case type_id::SIMPLE_ERROR:
                return parse_simple_error_string(payload);
            case type_id::INTEGER: {
                int64_t val = 0;
                if (!parse_integer(payload, val)) {
                    return make_parse_error(ParseErrorCode::INVALID_INTEGER);
                }
                return make_parse_result(Value(Integer{val}));
            }
            case type_id::NULL_:
                return make_parse_result(Value(Null{}));
            case type_id::BOOLEAN: {
                if (payload == "t") return make_parse_result(Value(Boolean{true}));
                if (payload == "f") return make_parse_result(Value(Boolean{false}));
                return make_parse_error(ParseErrorCode::INVALID_BOOLEAN);
            }
            case type_id::DOUBLE: {
                double val = 0.0;
                if (!parse_double(payload, val)) {
                    return make_parse_error(ParseErrorCode::INVALID_DOUBLE);
                }
                return make_parse_result(Value(Double{val}));
            }
            case type_id::BIG_NUMBER:
                return parse_big_number_string(payload);
            case type_id::BULK_STRING:
            case type_id::BULK_ERROR:
            case type_id::VERBATIM_STRING:
            case type_id::ARRAY:
            case type_id::MAP:
            case type_id::ATTRIBUTE:
            case type_id::SET:
            case type_id::PUSH: {
                // These need more data from buffer
                int64_t len = 0;
                if (!parse_integer(payload, len)) {
                    return make_parse_error(ParseErrorCode::INVALID_LENGTH);
                }
                return parse_bulk_or_aggregate_from_buffer(type, len);
            }
            default:
                return make_parse_error(ParseErrorCode::INVALID_TYPE);
        }
    }
    
    [[nodiscard]] ParseResult<Value> parse_simple_error_string(std::string_view str) {
        auto space_pos = str.find(' ');
        std::string prefix;
        std::string message;
        
        if (space_pos == std::string_view::npos) {
            prefix = std::string(str);
        } else {
            prefix = std::string(str.substr(0, space_pos));
            message = std::string(str.substr(space_pos + 1));
        }
        
        return make_parse_result(Value(SimpleError{prefix, message}));
    }
    
    [[nodiscard]] ParseResult<Value> parse_big_number_string(std::string_view str) {
        if (str.empty()) {
            return make_parse_error(ParseErrorCode::INVALID_BIG_NUMBER);
        }
        
        bool negative = false;
        size_t start = 0;
        
        if (str[0] == '-') {
            negative = true;
            start = 1;
        } else if (str[0] == '+') {
            start = 1;
        }
        
        for (size_t i = start; i < str.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(str[i]))) {
                return make_parse_error(ParseErrorCode::INVALID_BIG_NUMBER);
            }
        }
        
        return make_parse_result(Value(BigNumber{std::string(str), negative}));
    }
    
    [[nodiscard]] ParseResult<Value> parse_bulk_or_aggregate_from_buffer(char type, int64_t len) {
        // Handle null aggregate types (count = -1)
        if (len < 0) {
            return make_parse_result(Value(Null{}));
        }

        switch (type) {
            case type_id::BULK_STRING:
                return parse_bulk_string_from_buffer(len);
            case type_id::BULK_ERROR:
                return parse_bulk_error_from_buffer(len);
            case type_id::VERBATIM_STRING:
                return parse_verbatim_string_from_buffer(len);
            case type_id::ARRAY:
                return parse_array_from_buffer(static_cast<size_t>(len), 0);
            case type_id::SET:
                return parse_set_from_buffer(static_cast<size_t>(len), 0);
            case type_id::PUSH:
                return parse_push_from_buffer(static_cast<size_t>(len), 0);
            case type_id::MAP:
                return parse_map_from_buffer(static_cast<size_t>(len), 0);
            case type_id::ATTRIBUTE:
                return parse_attribute_from_buffer(static_cast<size_t>(len), 0);
            default:
                return make_parse_error(ParseErrorCode::INVALID_TYPE);
        }
    }
    
    [[nodiscard]] ParseResult<Value> parse_bulk_string_from_buffer(int64_t len) {
        if (len < 0) {
            return make_parse_result(Value(Null{}));
        }
        
        auto data_opt = _buffer.extract_bytes(static_cast<size_t>(len));
        if (!data_opt) {
            return make_parse_error(ParseErrorCode::INCOMPLETE_DATA);
        }
        
        // Skip CRLF
        auto crlf_check = _buffer.peek(0);
        if (!crlf_check || *crlf_check != '\r') {
            return make_parse_error(ParseErrorCode::PROTOCOL_ERROR, "Expected CRLF after bulk data");
        }
        _buffer.consume(2);
        
        return make_parse_result(Value(BulkString{std::move(*data_opt)}));
    }
    
    [[nodiscard]] ParseResult<Value> parse_bulk_error_from_buffer(int64_t len) {
        if (len < 0) {
            return make_parse_error(ParseErrorCode::INVALID_LENGTH, "Negative bulk error length");
        }
        
        auto data_opt = _buffer.extract_bytes(static_cast<size_t>(len));
        if (!data_opt) {
            return make_parse_error(ParseErrorCode::INCOMPLETE_DATA);
        }
        
        _buffer.consume(2);  // Skip CRLF
        
        auto space_pos = data_opt->find(' ');
        std::string prefix;
        std::string message;
        
        if (space_pos == std::string::npos) {
            prefix = std::move(*data_opt);
        } else {
            prefix = data_opt->substr(0, space_pos);
            message = data_opt->substr(space_pos + 1);
        }
        
        return make_parse_result(Value(BulkError{prefix, message}));
    }
    
    [[nodiscard]] ParseResult<Value> parse_verbatim_string_from_buffer(int64_t len) {
        if (len < 4) {
            return make_parse_error(ParseErrorCode::INVALID_VERBATIM_FORMAT);
        }
        
        auto data_opt = _buffer.extract_bytes(static_cast<size_t>(len));
        if (!data_opt) {
            return make_parse_error(ParseErrorCode::INCOMPLETE_DATA);
        }
        
        _buffer.consume(2);  // Skip CRLF
        
        auto colon_pos = data_opt->find(':');
        if (colon_pos != 3) {
            return make_parse_error(ParseErrorCode::INVALID_VERBATIM_FORMAT);
        }
        
        VerbatimString result;
        std::memcpy(result.encoding, data_opt->data(), 3);
        result.value = data_opt->substr(4);
        
        return make_parse_result(Value(std::move(result)));
    }
    
    [[nodiscard]] ParseResult<Value> parse_array_from_buffer(size_t count, size_t depth);
    [[nodiscard]] ParseResult<Value> parse_set_from_buffer(size_t count, size_t depth);
    [[nodiscard]] ParseResult<Value> parse_push_from_buffer(size_t count, size_t depth);
    [[nodiscard]] ParseResult<Value> parse_map_from_buffer(size_t count, size_t depth);
    [[nodiscard]] ParseResult<Value> parse_attribute_from_buffer(size_t count, size_t depth);
    
    // Member variables
    ParserConfig _config;
    State _state;
    size_t _current_depth;
    InputBuffer _buffer;
    
    struct ParseFrame {
        char type;
        size_t remaining;
        Value partial;
    };
    std::stack<ParseFrame> _parse_stack;
    
    std::optional<Value> _pending_value;
    std::vector<char> _temp_data;
};

// Implementation of recursive parsing functions
inline ParseResult<Value> RespParser::parse_array_from_buffer(size_t count, size_t depth) {
    if (depth > _config.max_nesting_depth) {
        return make_parse_error(ParseErrorCode::NESTING_TOO_DEEP);
    }
    
    Array result;
    result.elements.reserve(count);
    
    for (size_t i = 0; i < count; ++i) {
        auto elem = try_parse_from_buffer();
        if (!elem.has_value()) {
            return elem;
        }
        result.elements.push_back(std::make_unique<Value>(std::move(*elem)));
    }
    
    return make_parse_result(Value(std::move(result)));
}

inline ParseResult<Value> RespParser::parse_set_from_buffer(size_t count, size_t depth) {
    if (depth > _config.max_nesting_depth) {
        return make_parse_error(ParseErrorCode::NESTING_TOO_DEEP);
    }
    
    Set result;
    result.elements.reserve(count);
    
    for (size_t i = 0; i < count; ++i) {
        auto elem = try_parse_from_buffer();
        if (!elem.has_value()) {
            return elem;
        }
        result.elements.push_back(std::make_unique<Value>(std::move(*elem)));
    }
    
    return make_parse_result(Value(std::move(result)));
}

inline ParseResult<Value> RespParser::parse_push_from_buffer(size_t count, size_t depth) {
    if (depth > _config.max_nesting_depth) {
        return make_parse_error(ParseErrorCode::NESTING_TOO_DEEP);
    }
    
    Push result;
    result.elements.reserve(count);
    
    for (size_t i = 0; i < count; ++i) {
        auto elem = try_parse_from_buffer();
        if (!elem.has_value()) {
            return elem;
        }
        result.elements.push_back(std::make_unique<Value>(std::move(*elem)));
    }
    
    return make_parse_result(Value(std::move(result)));
}

inline ParseResult<Value> RespParser::parse_map_from_buffer(size_t count, size_t depth) {
    if (depth > _config.max_nesting_depth) {
        return make_parse_error(ParseErrorCode::NESTING_TOO_DEEP);
    }
    
    Map result;
    result.entries.reserve(count);
    
    for (size_t i = 0; i < count; ++i) {
        auto key = try_parse_from_buffer();
        if (!key.has_value()) {
            return key;
        }
        
        auto val = try_parse_from_buffer();
        if (!val.has_value()) {
            return val;
        }
        
        result.entries.emplace_back(std::make_unique<Value>(std::move(*key)), std::make_unique<Value>(std::move(*val)));
    }
    
    return make_parse_result(Value(std::move(result)));
}

inline ParseResult<Value> RespParser::parse_attribute_from_buffer(size_t count, size_t depth) {
    if (depth > _config.max_nesting_depth) {
        return make_parse_error(ParseErrorCode::NESTING_TOO_DEEP);
    }

    Attribute result;
    result.data.entries.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        auto key = try_parse_from_buffer();
        if (!key.has_value()) return key;

        auto val = try_parse_from_buffer();
        if (!val.has_value()) return val;

        result.data.entries.emplace_back(
            std::make_unique<Value>(std::move(*key)),
            std::make_unique<Value>(std::move(*val)));
    }

    // Parse the actual reply that follows the attribute metadata
    auto actual = try_parse_from_buffer();
    if (!actual.has_value()) return actual;
    result.value = std::make_unique<Value>(std::move(*actual));

    return make_parse_result(Value(std::move(result)));
}

// ============================================================================
// Simple non-streaming parser for complete data
// ============================================================================
[[nodiscard]] inline ParseResult<Value> parse(std::string_view data,
                                               const ParserConfig& config = {}) {
    RespParser parser(config);
    if (!parser.feed(data)) {
        return make_parse_error(ParseErrorCode::BUFFER_OVERFLOW);
    }
    return parser.parse();
}

} // namespace qb::redis::parser

#endif // QBM_REDIS_PARSER_PARSER_H
