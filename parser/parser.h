/**
 * @file qbm/redis/parser/parser.h
 * @brief Streaming RESP2/RESP3 wire-protocol parser.
 *
 * Defines @ref qb::redis::parser::RespParser, an incremental parser that turns a
 * byte stream of RESP2/RESP3 data into typed @ref qb::redis::parser::Value
 * objects. Data is appended with @ref qb::redis::parser::RespParser::feed and
 * decoded with @ref qb::redis::parser::RespParser::parse (one value) or @ref
 * qb::redis::parser::RespParser::parse_all (every buffered value). Parsing is
 * non-destructive: incomplete input is retained and re-parsed once more bytes
 * arrive, so the parser is safe to drive from an async I/O loop that delivers
 * partial frames. Also provides @ref qb::redis::parser::ParserConfig (limits and
 * protocol selection) and the one-shot convenience entry point @ref
 * qb::redis::parser::parse.
 *
 *            SPDX-License-Identifier: Apache-2.0
 *
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2026 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)
 * @ingroup Redis
 */
#ifndef QBM_REDIS_PARSER_PARSER_H
#define QBM_REDIS_PARSER_PARSER_H

#include "buffer.h"
#include "types.h"

namespace qb::redis::parser {

// ============================================================================
// Parser configuration
// ============================================================================

/**
 * @brief Parser limits and protocol options.
 *
 * The size limits guard against hostile or corrupt input that would otherwise
 * cause unbounded allocation while decoding a single frame.
 */
struct ParserConfig {
    ProtocolVersion protocol_version  = ProtocolVersion::RESP3; ///< Wire protocol to accept.
    size_t          max_nesting_depth = 64;                     ///< Max aggregate nesting depth.
    size_t          max_bulk_size     = 512 * 1024 * 1024;      ///< Max bulk payload bytes (512MB).
    size_t          max_array_size    = 1'000'000;              ///< Max aggregate element count.
    bool            strict_mode       = false;                  ///< Reject trailing data after a parse.
};

// ============================================================================
// Parser state
// ============================================================================

/** @brief Internal parser state-machine states. */
enum class State {
    READY,             ///< Waiting for a type byte.
    PARSING_LENGTH,    ///< Parsing the length line (bulk/aggregate types).
    PARSING_SIMPLE,    ///< Parsing simple-type data.
    PARSING_BULK,      ///< Parsing bulk data.
    PARSING_AGGREGATE, ///< Parsing aggregate elements.
    COMPLETE,          ///< A complete value is available.
    /** Parser hit a fatal error (not `ERROR` — Windows headers define that macro). */
    FAULT
};

// ============================================================================
// Main RESP parser - streaming capable
// ============================================================================

/**
 * @class RespParser
 * @brief Streaming RESP2/RESP3 parser for async I/O.
 *
 * Append bytes with @ref feed(), then decode buffered values with @ref parse()
 * (a single value) or @ref parse_all() (every complete value). Parsing is
 * non-destructive: incomplete frames are retained and re-parsed when more bytes
 * arrive, and a fatal protocol error faults the parser (see @ref has_error) so
 * the connection can be torn down rather than spinning on corrupt input. Decoded
 * scalars and strings reference the contiguous internal buffer where possible
 * (zero-copy), with owning copies materialized into the resulting @ref Value.
 */
class RespParser {
public:
    /**
     * @brief Construct a parser with the given configuration.
     * @param config Limits and protocol options (defaults to RESP3 with the
     *               default size limits).
     */
    explicit RespParser(const ParserConfig &config = {})
        : _config(config)
        , _state(State::READY)
        , _current_depth(0) {}

    /** @brief Reset the parser to its initial state and discard buffered bytes. */
    void
    reset() {
        _state         = State::READY;
        _current_depth = 0;
        _buffer.reset();
    }

    /** @brief Current state-machine state. */
    [[nodiscard]] State
    state() const noexcept {
        return _state;
    }
    /** @brief @return `true` if the parser is idle, awaiting a new value. */
    [[nodiscard]] bool
    is_ready() const noexcept {
        return _state == State::READY;
    }
    /** @brief @return `true` if a complete value is available. */
    [[nodiscard]] bool
    is_complete() const noexcept {
        return _state == State::COMPLETE;
    }
    /** @brief @return `true` if the parser has faulted on a protocol error. */
    [[nodiscard]] bool
    has_error() const noexcept {
        return _state == State::FAULT;
    }
    /** @brief @return `true` while a value is being decoded (mid-frame). */
    [[nodiscard]] bool
    is_parsing() const noexcept {
        return _state != State::READY && _state != State::COMPLETE && _state != State::FAULT;
    }

    /**
     * @brief Append raw wire bytes to the internal buffer.
     * @param data Bytes to buffer.
     * @return `true` on success, `false` if the parser has faulted or the
     *         buffer could not grow.
     */
    bool
    feed(std::string_view data) {
        if (has_error())
            return false;
        return _buffer.append(std::span<const char>(data.data(), data.size()));
    }

    /**
     * @brief Append raw wire bytes to the internal buffer (span overload).
     *
     * Explicitly constrained to `std::span<const char>` to avoid ambiguity with
     * the @ref feed(std::string_view) overload.
     *
     * @tparam T Must be `std::span<const char>`.
     * @param data Bytes to buffer.
     * @return `true` on success, `false` if the parser has faulted or the
     *         buffer could not grow.
     */
    template <typename T>
    requires std::same_as<T, std::span<const char>>
    bool
    feed(T data) {
        if (has_error())
            return false;
        return _buffer.append(data);
    }

    /**
     * @brief Try to parse a single complete value from buffered bytes.
     *
     * @ref compact() first guarantees the buffered bytes are contiguous, so the
     * non-destructive view pass covers every case: on @c INCOMPLETE_DATA nothing
     * is consumed and the same bytes are retried once more data is fed.
     *
     * @return The parsed @ref Value on success; a @ref ParseError with
     *         @c INCOMPLETE_DATA if more bytes are required; or a @ref ParseError
     *         with another code on a protocol error.
     */
    [[nodiscard]] ParseResult<Value>
    parse() {
        if (has_error()) {
            return make_parse_error(ParseErrorCode::PROTOCOL_ERROR, "Parser in error state");
        }
        compact();
        return try_parse_from_view();
    }

    /**
     * @brief Parse every complete value currently buffered.
     *
     * Uses a non-destructive ViewBuffer over the compacted (contiguous) buffer.
     * On @c INCOMPLETE_DATA the ViewBuffer position is simply not committed, so
     * no bytes are lost and the next call re-parses from the same starting point
     * once more data has been fed in. Any other error code is a fatal protocol
     * error: the parser is faulted (see @ref has_error) so the driver tears the
     * connection down instead of spinning on the same corrupt byte forever. This
     * correctly handles every RESP type, including arbitrarily nested arrays,
     * maps, sets and push messages.
     *
     * @return The values decoded in this call (possibly empty); trailing
     *         incomplete bytes remain buffered for a later call.
     */
    [[nodiscard]] std::vector<Value>
    parse_all() {
        std::vector<Value> results;

        compact(); // Guarantee a single contiguous readable_span()

        while (true) {
            auto span = _buffer.readable_span();
            if (span.empty())
                break;

            ViewBuffer view(span);
            auto       result = parse_value(view, 0);

            if (!result.has_value()) {
                // INCOMPLETE_DATA: keep the bytes and retry when more arrive.
                // Any other code is a fatal protocol error — fault the parser so
                // the driver tears the connection down instead of re-parsing the
                // same corrupt byte forever (a silent permanent stall).
                if (result.error().code() != ParseErrorCode::INCOMPLETE_DATA) {
                    _state = State::FAULT;
                }
                break;
            }

            // Only commit the bytes that were actually consumed on success.
            _buffer.consume(view.position());
            results.push_back(std::move(*result));
        }

        return results;
    }

    /**
     * @brief Fast pre-check for at least one complete top-level value.
     *
     * Used only as an optimization hint; the definitive answer comes from
     * @ref parse_all(). For simple scalar types a single CRLF is sufficient to
     * confirm completeness; for bulk and aggregate types a full trial parse is
     * attempted.
     *
     * @return `true` if a complete top-level value appears to be buffered.
     */
    [[nodiscard]] bool
    has_complete_value() const {
        if (_buffer.empty())
            return false;
        auto span = _buffer.readable_span();
        if (span.empty())
            return false;
        // A non-const temporary parser shares no mutable state with *this –
        // we only need to check whether a parse succeeds.
        ViewBuffer view(span);
        // peek at the type byte; if it does not exist we have no data
        auto type_opt = view.peek();
        if (!type_opt)
            return false;
        // For simple types a single CRLF is enough to confirm completeness
        char type = *type_opt;
        if (type == '+' || type == '-' || type == ':' || type == '#' || type == ',' || type == '(' || type == '_') {
            return _buffer.find_crlf().has_value();
        }
        // For all other types (bulk, aggregate) we need to attempt a full parse.
        // The const_cast is safe: parse_value() only reads from the ViewBuffer.
        auto result = const_cast<RespParser *>(this)->parse_value(view, 0);
        return result.has_value();
    }

    /** @brief @return A view of the bytes buffered but not yet consumed. */
    [[nodiscard]] std::span<const char>
    unparsed_data() const {
        return _buffer.readable_span();
    }

    /** @brief Coalesce buffered bytes into a single contiguous region. */
    void
    compact() {
        _buffer.compact();
    }

private:
    // Parse one value from the (contiguous, compacted) buffer without
    // destroying any bytes on failure. The previous "buffered" fallback path
    // (extract_line/extract_bytes directly off the InputBuffer) consumed bytes
    // before knowing whether the value was complete, silently corrupting the
    // stream on INCOMPLETE_DATA; compact() + this view pass replaces it.
    [[nodiscard]] ParseResult<Value>
    try_parse_from_view() {
        auto span1 = _buffer.readable_span();
        if (span1.empty()) {
            return make_parse_error(ParseErrorCode::INCOMPLETE_DATA);
        }

        ViewBuffer view(span1);
        auto       result = parse_value(view, 0);

        if (result.has_value()) {
            // Success - consume exactly the bytes the view walked over.
            _buffer.consume(view.position());
        }
        return result;
    }

    /**
     * @brief Strictly consume the CRLF terminator that must follow fixed-length
     *        payloads (`$`, `!`, `=`) and single-byte types (`_`, `#`).
     *
     * Distinguishes the two failure modes that a bare `skip_crlf()` conflates:
     * fewer than two bytes available is INCOMPLETE_DATA (retry later), while
     * two available bytes that are not "\r\n" is a fatal PROTOCOL_ERROR —
     * treating it as incomplete would stall the connection forever waiting for
     * bytes that can never make the terminator valid.
     */
    [[nodiscard]] static std::optional<ParseError>
    expect_crlf(ViewBuffer &view) {
        const auto c0 = view.peek(0);
        const auto c1 = view.peek(1);
        if (!c0 || !c1) {
            return ParseError(ParseErrorCode::INCOMPLETE_DATA);
        }
        if (*c0 != '\r' || *c1 != '\n') {
            return ParseError(ParseErrorCode::PROTOCOL_ERROR, "Expected CRLF terminator");
        }
        view.consume(2);
        return std::nullopt;
    }

    // Parse value from view buffer
    [[nodiscard]] ParseResult<Value>
    parse_value(ViewBuffer &view, size_t depth) {
        if (depth > _config.max_nesting_depth) {
            return make_parse_error(ParseErrorCode::NESTING_TOO_DEEP);
        }

        auto type_opt = view.peek();
        if (!type_opt) {
            return make_parse_error(ParseErrorCode::INCOMPLETE_DATA);
        }

        char type = *type_opt;

        if (!is_valid_type_prefix(type)) {
            return make_parse_error(ParseErrorCode::INVALID_TYPE, qb::format_message("Invalid type prefix: ", type));
        }

        // Check RESP3 types in RESP2 mode
        if (_config.protocol_version == ProtocolVersion::RESP2 && is_resp3_type(type)) {
            return make_parse_error(ParseErrorCode::INVALID_TYPE, qb::format_message("RESP3 type not allowed in RESP2 mode: ", type));
        }

        view.consume(1); // Consume type byte

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
    [[nodiscard]] static ParseResult<Value>
    parse_null(ViewBuffer &view) {
        if (auto err = expect_crlf(view)) {
            return unexpected(std::move(*err));
        }
        return make_parse_result(Value(Null{}));
    }

    // Parse boolean: #t\r\n or #f\r\n
    [[nodiscard]] static ParseResult<Value>
    parse_boolean(ViewBuffer &view) {
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
            return make_parse_error(ParseErrorCode::INVALID_BOOLEAN, qb::format_message("Invalid boolean value: ", *val_opt));
        }

        view.consume(1);

        if (auto err = expect_crlf(view)) {
            return unexpected(std::move(*err));
        }

        return make_parse_result(Value(Boolean{value}));
    }

    // Parse simple string: +...\r\n
    [[nodiscard]] static ParseResult<Value>
    parse_simple_string(ViewBuffer &view) {
        auto line_opt = view.extract_line_view();
        if (!line_opt) {
            return make_parse_error(ParseErrorCode::INCOMPLETE_DATA);
        }

        return make_parse_result(Value(SimpleString{std::string(*line_opt)}));
    }

    // Parse simple error: -...\r\n
    [[nodiscard]] static ParseResult<Value>
    parse_simple_error(ViewBuffer &view) {
        auto line_opt = view.extract_line_view();
        if (!line_opt) {
            return make_parse_error(ParseErrorCode::INCOMPLETE_DATA);
        }

        // Parse prefix (first word)
        auto        space_pos = line_opt->find(' ');
        std::string prefix;
        std::string message;

        if (space_pos == std::string_view::npos) {
            prefix = std::string(*line_opt);
        } else {
            prefix  = std::string(line_opt->substr(0, space_pos));
            message = std::string(line_opt->substr(space_pos + 1));
        }

        return make_parse_result(Value(SimpleError{prefix, message}));
    }

    // Parse integer: :[+-]?\d+\r\n
    [[nodiscard]] static ParseResult<Value>
    parse_integer(ViewBuffer &view) {
        auto line_opt = view.extract_line_view();
        if (!line_opt) {
            return make_parse_error(ParseErrorCode::INCOMPLETE_DATA);
        }

        int64_t value = 0;
        if (!parse_integer(*line_opt, value)) {
            return make_parse_error(ParseErrorCode::INVALID_INTEGER, qb::format_message("Invalid integer: ", *line_opt));
        }

        return make_parse_result(Value(Integer{value}));
    }

    // Parse double: ,[+-]?\d*\.?\d*(e[+-]?\d+)?\r\n
    [[nodiscard]] static ParseResult<Value>
    parse_double(ViewBuffer &view) {
        auto line_opt = view.extract_line_view();
        if (!line_opt) {
            return make_parse_error(ParseErrorCode::INCOMPLETE_DATA);
        }

        double value = 0.0;
        if (!parse_double(*line_opt, value)) {
            return make_parse_error(ParseErrorCode::INVALID_DOUBLE, qb::format_message("Invalid double: ", *line_opt));
        }

        return make_parse_result(Value(Double{value}));
    }

    // Parse big number: ([+-]?\d+)\r\n
    [[nodiscard]] static ParseResult<Value>
    parse_big_number(ViewBuffer &view) {
        auto line_opt = view.extract_line_view();
        if (!line_opt) {
            return make_parse_error(ParseErrorCode::INCOMPLETE_DATA);
        }

        if (line_opt->empty()) {
            return make_parse_error(ParseErrorCode::INVALID_BIG_NUMBER, "Empty big number");
        }

        bool   negative = false;
        size_t start    = 0;

        if ((*line_opt)[0] == '-') {
            negative = true;
            start    = 1;
        } else if ((*line_opt)[0] == '+') {
            start = 1;
        }

        // Validate digits
        for (size_t i = start; i < line_opt->size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>((*line_opt)[i]))) {
                return make_parse_error(ParseErrorCode::INVALID_BIG_NUMBER, "Big number contains non-digit characters");
            }
        }

        return make_parse_result(Value(BigNumber{std::string(*line_opt), negative}));
    }

    // Parse bulk or aggregate types
    [[nodiscard]] ParseResult<Value>
    parse_bulk_or_aggregate(char type, std::string_view length_str, ViewBuffer &view, size_t depth) {
        int64_t len = 0;
        if (!parse_integer(length_str, len)) {
            return make_parse_error(ParseErrorCode::INVALID_LENGTH, qb::format_message("Invalid length: ", length_str));
        }

        // Only a length of exactly -1 denotes a null value (RESP2 $-1 / *-1 and
        // the RESP3 null aggregate forms). Any other negative length is a
        // protocol error, not a silent null — otherwise corrupt input like
        // "%-7\r\n" would be swallowed as a valid reply.
        if (len == -1) {
            return make_parse_result(Value(Null{}));
        }
        if (len < 0) {
            return make_parse_error(ParseErrorCode::INVALID_LENGTH, qb::format_message("Negative length is not a valid null marker: ", len));
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

    [[nodiscard]] bool
    bulk_payload_exceeds_limit(int64_t len) const noexcept {
        if (len < 0) {
            return false;
        }
        return static_cast<size_t>(len) > _config.max_bulk_size;
    }

    // Parse bulk string: $N\r\n<data>\r\n
    [[nodiscard]] ParseResult<Value>
    parse_bulk_string(int64_t len, ViewBuffer &view) {
        if (len < 0) {
            // Null bulk string in RESP2 mode
            return make_parse_result(Value(Null{}));
        }

        if (bulk_payload_exceeds_limit(len)) {
            return make_parse_error(ParseErrorCode::BUFFER_OVERFLOW, "Bulk string too large");
        }

        auto data_opt = view.extract_bytes_view(static_cast<size_t>(len));
        if (!data_opt) {
            return make_parse_error(ParseErrorCode::INCOMPLETE_DATA);
        }

        if (auto err = expect_crlf(view)) {
            return unexpected(std::move(*err));
        }

        return make_parse_result(Value(BulkString{std::string(*data_opt)}));
    }

    // Parse bulk error: !N\r\n<error>\r\n
    [[nodiscard]] ParseResult<Value>
    parse_bulk_error(int64_t len, ViewBuffer &view) {
        if (len < 0) {
            return make_parse_error(ParseErrorCode::INVALID_LENGTH, "Negative bulk error length");
        }

        if (bulk_payload_exceeds_limit(len)) {
            return make_parse_error(ParseErrorCode::BUFFER_OVERFLOW, "Bulk error payload too large");
        }

        auto data_opt = view.extract_bytes(static_cast<size_t>(len));
        if (!data_opt) {
            return make_parse_error(ParseErrorCode::INCOMPLETE_DATA);
        }

        if (auto err = expect_crlf(view)) {
            return unexpected(std::move(*err));
        }

        // Parse prefix and message
        auto        space_pos = data_opt->find(' ');
        std::string prefix;
        std::string message;

        if (space_pos == std::string::npos) {
            prefix = std::move(*data_opt);
        } else {
            prefix  = data_opt->substr(0, space_pos);
            message = data_opt->substr(space_pos + 1);
        }

        return make_parse_result(Value(BulkError{prefix, message}));
    }

    // Parse verbatim string: =N\r\n<encoding>:<data>\r\n
    [[nodiscard]] ParseResult<Value>
    parse_verbatim_string(int64_t len, ViewBuffer &view) {
        if (len < 4) { // Minimum: "xxx:" (3 chars encoding + colon + at least 1 char)
            return make_parse_error(ParseErrorCode::INVALID_VERBATIM_FORMAT, "Verbatim string too short");
        }

        if (bulk_payload_exceeds_limit(len)) {
            return make_parse_error(ParseErrorCode::BUFFER_OVERFLOW, "Verbatim string too large");
        }

        auto data_opt = view.extract_bytes(static_cast<size_t>(len));
        if (!data_opt) {
            return make_parse_error(ParseErrorCode::INCOMPLETE_DATA);
        }

        if (auto err = expect_crlf(view)) {
            return unexpected(std::move(*err));
        }

        // Find colon separator
        auto colon_pos = data_opt->find(':');
        if (colon_pos != 3) {
            return make_parse_error(ParseErrorCode::INVALID_VERBATIM_FORMAT, "Verbatim string encoding must be 3 characters");
        }

        VerbatimString result;
        std::memcpy(result.encoding, data_opt->data(), 3);
        result.value = data_opt->substr(4);

        return make_parse_result(Value(std::move(result)));
    }

    // Parse array: *N\r\n<elements...>
    [[nodiscard]] ParseResult<Value>
    parse_array(size_t count, ViewBuffer &view, size_t depth) {
        if (count > _config.max_array_size) {
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
    [[nodiscard]] ParseResult<Value>
    parse_set(size_t count, ViewBuffer &view, size_t depth) {
        if (count > _config.max_array_size) {
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
    [[nodiscard]] ParseResult<Value>
    parse_push(size_t count, ViewBuffer &view, size_t depth) {
        if (count > _config.max_array_size) {
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
    [[nodiscard]] ParseResult<Value>
    parse_map(size_t count, ViewBuffer &view, size_t depth) {
        if (count > _config.max_array_size / 2) { // N pairs = 2N elements; bound by the element cap
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
    [[nodiscard]] ParseResult<Value>
    parse_attribute(size_t count, ViewBuffer &view, size_t depth) {
        if (count > _config.max_array_size / 2) { // N pairs = 2N elements; bound by the element cap
            return make_parse_error(ParseErrorCode::BUFFER_OVERFLOW, "Attribute too large");
        }

        Attribute result;
        result.data.entries.reserve(count);

        for (size_t i = 0; i < count; ++i) {
            auto key = parse_value(view, depth + 1);
            if (!key.has_value())
                return key;

            auto val = parse_value(view, depth + 1);
            if (!val.has_value())
                return val;

            result.data.entries.emplace_back(std::make_unique<Value>(std::move(*key)), std::make_unique<Value>(std::move(*val)));
        }

        // Parse the actual reply that the attribute decorates. Recurse at depth+1
        // (like the key/value entries above): a RESP3 attribute legally decorates
        // another value, so a chain of attributes is nesting and MUST be bounded by
        // max_nesting_depth. Recursing at the same depth here let an attribute chain
        // bypass the depth guard entirely and exhaust the stack (DoS from a hostile
        // or MITM'd server).
        auto actual = parse_value(view, depth + 1);
        if (!actual.has_value())
            return actual;
        result.value = std::make_unique<Value>(std::move(*actual));

        return make_parse_result(Value(std::move(result)));
    }

    // Helper: parse integer from string
    [[nodiscard]] static bool
    parse_integer(std::string_view str, int64_t &out) {
        if (str.empty())
            return false;

        size_t pos      = 0;
        bool   negative = false;

        if (str[0] == '-') {
            negative = true;
            pos      = 1;
        } else if (str[0] == '+') {
            pos = 1;
        }

        if (pos >= str.size())
            return false;

        // Special case: INT64_MIN (-9223372036854775808)
        // This is the only case where abs(value) > INT64_MAX
        if (negative && str.substr(pos) == "9223372036854775808") {
            out = std::numeric_limits<int64_t>::min();
            return true;
        }

        int64_t value = 0;
        for (; pos < str.size(); ++pos) {
            char c = str[pos];
            if (c < '0' || c > '9')
                return false;

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
    [[nodiscard]] static bool
    parse_double(std::string_view str, double &out) {
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

        double value   = 0.0;
        auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value, std::chars_format::general);
        // Both the parse itself and full consumption of the string must succeed.
        if (ec != std::errc{} || ptr != str.data() + str.size())
            return false;
        out = value;
        return true;
    }

    // Member variables
    ParserConfig _config;
    State        _state;
    size_t       _current_depth;
    InputBuffer  _buffer;
};

// ============================================================================
// Simple non-streaming parser for complete data
// ============================================================================

/**
 * @brief One-shot parse of complete RESP data
 * @param data Complete RESP wire data
 * @param config Parser configuration (optional)
 * @return Parsed Value or ParseError
 */
[[nodiscard]] inline ParseResult<Value>
parse(std::string_view data, const ParserConfig &config = {}) {
    RespParser parser(config);
    if (!parser.feed(data)) {
        return make_parse_error(ParseErrorCode::BUFFER_OVERFLOW);
    }
    return parser.parse();
}

} // namespace qb::redis::parser

#endif // QBM_REDIS_PARSER_PARSER_H
