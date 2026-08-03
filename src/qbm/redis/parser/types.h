/**
 * @file qbm/redis/parser/types.h
 * @brief RESP protocol types: Value, Array, Map, Set, Push, etc.
 *
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2026 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)
 * @ingroup Redis
 */
#ifndef QBM_REDIS_PARSER_TYPES_H
#define QBM_REDIS_PARSER_TYPES_H

#include <charconv>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#include <qb/utility/compat.h>

namespace qb::redis {

using qb::expected;
using qb::unexpected;

} // namespace qb::redis

namespace qb::redis::parser {

// ============================================================================
// Protocol version
// ============================================================================

/** @brief Redis protocol version (RESP2 or RESP3) */
enum class ProtocolVersion { RESP2 = 2, RESP3 = 3 };

// ============================================================================
// Error types
// ============================================================================

/** @brief Parser error codes */
enum class ParseErrorCode {
    OK = 0,
    INCOMPLETE_DATA,         // Need more data
    INVALID_TYPE,            // Unknown type prefix
    INVALID_LENGTH,          // Invalid length format
    INVALID_INTEGER,         // Invalid integer format
    INVALID_DOUBLE,          // Invalid double format
    INVALID_BOOLEAN,         // Invalid boolean format
    INVALID_BIG_NUMBER,      // Invalid big number format
    INVALID_VERBATIM_FORMAT, // Invalid verbatim string format
    NESTING_TOO_DEEP,        // Recursion limit exceeded
    BUFFER_OVERFLOW,         // Buffer size limit exceeded
    PROTOCOL_ERROR,          // General protocol error
};

/** @brief Parser error with code and optional message */
class ParseError {
public:
    ParseError(ParseErrorCode code, std::string_view message = {})
        : _code(code)
        , _message(message) {}

    [[nodiscard]] ParseErrorCode
    code() const noexcept {
        return _code;
    }
    [[nodiscard]] std::string_view
    message() const noexcept {
        return _message;
    }

    [[nodiscard]] const char *
    what() const noexcept {
        switch (_code) {
            case ParseErrorCode::OK:
                return "OK";
            case ParseErrorCode::INCOMPLETE_DATA:
                return "Incomplete data";
            case ParseErrorCode::INVALID_TYPE:
                return "Invalid type";
            case ParseErrorCode::INVALID_LENGTH:
                return "Invalid length";
            case ParseErrorCode::INVALID_INTEGER:
                return "Invalid integer";
            case ParseErrorCode::INVALID_DOUBLE:
                return "Invalid double";
            case ParseErrorCode::INVALID_BOOLEAN:
                return "Invalid boolean";
            case ParseErrorCode::INVALID_BIG_NUMBER:
                return "Invalid big number";
            case ParseErrorCode::INVALID_VERBATIM_FORMAT:
                return "Invalid verbatim format";
            case ParseErrorCode::NESTING_TOO_DEEP:
                return "Nesting too deep";
            case ParseErrorCode::BUFFER_OVERFLOW:
                return "Buffer overflow";
            case ParseErrorCode::PROTOCOL_ERROR:
                return "Protocol error";
        }
        return "Unknown error";
    }

private:
    ParseErrorCode _code;
    std::string    _message;
};

/** @brief Result type for parse operations (value or ParseError) */
template <typename T>
using ParseResult = expected<T, ParseError>;

// Helper to create parse errors
[[nodiscard]] inline unexpected<ParseError>
make_parse_error(ParseErrorCode code, std::string_view message = {}) {
    return unexpected<ParseError>(ParseError(code, message));
}

// Forward declare Value for make_parse_result
struct Value;

// Helper to create parse results - defined after Value is complete
[[nodiscard]] inline ParseResult<Value> make_parse_result(Value &&value);

// ============================================================================
// RESP Type identifiers (first byte)
// ============================================================================
namespace type_id {
constexpr char SIMPLE_STRING = '+';
constexpr char SIMPLE_ERROR  = '-';
constexpr char INTEGER       = ':';
constexpr char BULK_STRING   = '$';
constexpr char ARRAY         = '*';

// RESP3 types
constexpr char NULL_           = '_';
constexpr char BOOLEAN         = '#';
constexpr char DOUBLE          = ',';
constexpr char BIG_NUMBER      = '(';
constexpr char BULK_ERROR      = '!';
constexpr char VERBATIM_STRING = '=';
constexpr char MAP             = '%';
constexpr char ATTRIBUTE       = '|';
constexpr char SET             = '~';
constexpr char PUSH            = '>';
} // namespace type_id

// Forward declarations for recursive types
struct Value;
struct Array;
struct Map;
struct Set;
struct Push;
struct Attribute;

// ============================================================================
// Simple RESP Value types (non-recursive)
// ============================================================================

/** @brief RESP3 null type (_\\r\\n) */
struct Null {
    bool operator==(const Null &) const = default;
};

/** @brief RESP simple string (+OK\\r\\n) */
struct SimpleString {
    std::string value;
    bool        operator==(const SimpleString &other) const = default;
};

// Simple error: -ERR message\r\n
struct SimpleError {
    std::string prefix; // ERR, WRONGTYPE, etc.
    std::string message{};

    [[nodiscard]] std::string
    full_message() const {
        if (prefix.empty())
            return message;
        if (message.empty())
            return prefix;
        return prefix + " " + message;
    }

    bool operator==(const SimpleError &other) const = default;
};

/** @brief RESP integer (:123\\r\\n) */
struct Integer {
    int64_t value;
    bool    operator==(const Integer &other) const = default;
            operator int64_t() const {
        return value;
    }
};

/** @brief RESP bulk string ($5\\r\\nhello\\r\\n) */
struct BulkString {
    std::string value;

    [[nodiscard]] std::string_view
    view() const noexcept {
        return std::string_view(value);
    }

    [[nodiscard]] bool
    empty() const noexcept {
        return value.empty();
    }
    [[nodiscard]] size_t
    size() const noexcept {
        return value.size();
    }

    bool operator==(const BulkString &other) const = default;
};

// Boolean: #t\r\n or #f\r\n
struct Boolean {
    bool value;
         operator bool() const {
        return value;
    }
    bool operator==(const Boolean &other) const = default;
};

// Double: ,1.23\r\n
struct Double {
    double value;
           operator double() const {
        return value;
    }
    bool operator==(const Double &other) const = default;
};

// Big number: (3492890328409238509324850943850943825024385\r\n
struct BigNumber {
    std::string value; // String representation of arbitrary precision number
    bool        negative;

    bool operator==(const BigNumber &other) const = default;
};

// Bulk error: !21\r\nSYNTAX invalid syntax\r\n
struct BulkError {
    std::string prefix;
    std::string message;

    [[nodiscard]] std::string
    full_message() const {
        if (prefix.empty())
            return message;
        if (message.empty())
            return prefix;
        return prefix + " " + message;
    }

    bool operator==(const BulkError &other) const = default;
};

// Verbatim string: =15\r\ntxt:Some string\r\n
struct VerbatimString {
    static constexpr size_t ENCODING_LEN = 3;

    char        encoding[ENCODING_LEN]{}; // e.g., "txt", "mkd" - zero-initialized
    std::string value;

    [[nodiscard]] std::string_view
    encoding_view() const noexcept {
        return std::string_view(encoding, ENCODING_LEN);
    }

    bool operator==(const VerbatimString &other) const = default;
};

// ============================================================================
// Recursive aggregate types (using unique_ptr to break circular dependency)
// ============================================================================

/** @brief RESP array (*N\\r\\n<elements...>) */
struct Array {
    std::vector<std::unique_ptr<Value>> elements;

    [[nodiscard]] bool
    empty() const noexcept {
        return elements.empty();
    }
    [[nodiscard]] size_t
    size() const noexcept {
        return elements.size();
    }

    Value *
    operator[](size_t index) {
        return elements[index].get();
    }
    const Value *
    operator[](size_t index) const {
        return elements[index].get();
    }

    // Iterators for range-based for loops
    auto
    begin() {
        return elements.begin();
    }
    auto
    end() {
        return elements.end();
    }
    auto
    begin() const {
        return elements.begin();
    }
    auto
    end() const {
        return elements.end();
    }

    bool operator==(const Array &other) const;
};

/** @brief RESP3 map (%N\\r\\n<key1><value1>...) */
struct Map {
    std::vector<std::pair<std::unique_ptr<Value>, std::unique_ptr<Value>>> entries;

    [[nodiscard]] bool
    empty() const noexcept {
        return entries.empty();
    }
    [[nodiscard]] size_t
    size() const noexcept {
        return entries.size();
    }

    // Iterators for range-based for loops
    auto
    begin() {
        return entries.begin();
    }
    auto
    end() {
        return entries.end();
    }
    auto
    begin() const {
        return entries.begin();
    }
    auto
    end() const {
        return entries.end();
    }

    bool operator==(const Map &other) const;
};

// Attribute: |N\r\n<key1><val1>...<keyN><valN><actual-reply>
//
// RESP3 attributes are out-of-band metadata that PRECEDE the real reply.
// The parser must consume both the N metadata pairs AND the following actual
// value so that a single call to parse() returns the complete logical message.
struct Attribute {
    Map                    data;  ///< out-of-band metadata key-value pairs
    std::unique_ptr<Value> value; ///< the actual reply that follows the metadata

    // Defined out-of-line after Value is fully declared (same pattern as Array/Map/Set)
    bool operator==(const Attribute &other) const;
};

// Set: ~N\r\n<elements...>
struct Set {
    std::vector<std::unique_ptr<Value>> elements;

    [[nodiscard]] bool
    empty() const noexcept {
        return elements.empty();
    }
    [[nodiscard]] size_t
    size() const noexcept {
        return elements.size();
    }

    // Iterators for range-based for loops
    auto
    begin() {
        return elements.begin();
    }
    auto
    end() {
        return elements.end();
    }
    auto
    begin() const {
        return elements.begin();
    }
    auto
    end() const {
        return elements.end();
    }

    bool operator==(const Set &other) const;
};

// Push: >N\r\n<elements...> (out-of-band data)
struct Push {
    std::vector<std::unique_ptr<Value>> elements;

    [[nodiscard]] bool
    empty() const noexcept {
        return elements.empty();
    }
    [[nodiscard]] size_t
    size() const noexcept {
        return elements.size();
    }

    bool operator==(const Push &other) const;
};

// ============================================================================
// ============================================================================
// Variant Value type - the main type representing any RESP value
// ============================================================================

/** @brief Base variant type for all RESP values */
using ValueBase = std::variant<Null,         // _\r\n
                               SimpleString, // +\r\n
                               SimpleError,  // -\r\n
                               Integer,      // :\r\n
                               BulkString,   // $\r\n
                               Array,        // *\r\n
                               // RESP3 types
                               Boolean,        // #\r\n
                               Double,         // ,\r\n
                               BigNumber,      // (\r\n
                               BulkError,      // !\r\n
                               VerbatimString, // =\r\n
                               Map,            // %\r\n
                               Attribute,      // |\r\n
                               Set,            // ~\r\n
                               Push            // >\r\n
                               >;

/** @brief Main RESP value type - variant of all RESP2/RESP3 types */
struct Value : ValueBase {
    using ValueBase::ValueBase;

    // Convenience constructors
    Value()
        : ValueBase(Null{}) {}
    Value(std::nullptr_t)
        : ValueBase(Null{}) {}
    Value(const char *str)
        : ValueBase(SimpleString{std::string(str)}) {}
    Value(std::string str)
        : ValueBase(SimpleString{std::move(str)}) {}
    Value(int64_t i)
        : ValueBase(Integer{i}) {}
    Value(int i)
        : ValueBase(Integer{static_cast<int64_t>(i)}) {}
    Value(bool b)
        : ValueBase(Boolean{b}) {}
    Value(double d)
        : ValueBase(Double{d}) {}

    // Type checking
    [[nodiscard]] bool
    is_null() const noexcept {
        return std::holds_alternative<Null>(*this);
    }

    [[nodiscard]] bool
    is_simple_string() const noexcept {
        return std::holds_alternative<SimpleString>(*this);
    }

    [[nodiscard]] bool
    is_simple_error() const noexcept {
        return std::holds_alternative<SimpleError>(*this);
    }

    [[nodiscard]] bool
    is_error() const noexcept {
        return is_simple_error() || std::holds_alternative<BulkError>(*this);
    }

    [[nodiscard]] bool
    is_integer() const noexcept {
        return std::holds_alternative<Integer>(*this);
    }

    [[nodiscard]] bool
    is_bulk_string() const noexcept {
        return std::holds_alternative<BulkString>(*this);
    }

    [[nodiscard]] bool
    is_string() const noexcept {
        return is_simple_string() || is_bulk_string() || std::holds_alternative<VerbatimString>(*this);
    }

    [[nodiscard]] bool
    is_array() const noexcept {
        return std::holds_alternative<Array>(*this);
    }

    [[nodiscard]] bool
    is_boolean() const noexcept {
        return std::holds_alternative<Boolean>(*this);
    }

    [[nodiscard]] bool
    is_double() const noexcept {
        return std::holds_alternative<Double>(*this);
    }

    [[nodiscard]] bool
    is_big_number() const noexcept {
        return std::holds_alternative<BigNumber>(*this);
    }

    [[nodiscard]] bool
    is_number() const noexcept {
        return is_integer() || is_double() || is_big_number();
    }

    [[nodiscard]] bool
    is_map() const noexcept {
        return std::holds_alternative<Map>(*this);
    }

    [[nodiscard]] bool
    is_attribute() const noexcept {
        return std::holds_alternative<Attribute>(*this);
    }

    [[nodiscard]] bool
    is_set() const noexcept {
        return std::holds_alternative<Set>(*this);
    }

    [[nodiscard]] bool
    is_push() const noexcept {
        return std::holds_alternative<Push>(*this);
    }

    [[nodiscard]] bool
    is_aggregate() const noexcept {
        return is_array() || is_map() || is_attribute() || is_set() || is_push();
    }

    // Accessors
    [[nodiscard]] SimpleString &
    as_simple_string() {
        return std::get<SimpleString>(*this);
    }
    [[nodiscard]] const SimpleString &
    as_simple_string() const {
        return std::get<SimpleString>(*this);
    }

    [[nodiscard]] SimpleError &
    as_simple_error() {
        return std::get<SimpleError>(*this);
    }
    [[nodiscard]] const SimpleError &
    as_simple_error() const {
        return std::get<SimpleError>(*this);
    }

    [[nodiscard]] BulkError &
    as_bulk_error() {
        return std::get<BulkError>(*this);
    }
    [[nodiscard]] const BulkError &
    as_bulk_error() const {
        return std::get<BulkError>(*this);
    }

    [[nodiscard]] std::string
    get_error_message() const {
        if (auto *err = std::get_if<SimpleError>(this)) {
            return err->full_message();
        }
        if (auto *err = std::get_if<BulkError>(this)) {
            return err->full_message();
        }
        return {};
    }

    [[nodiscard]] Integer &
    as_integer() {
        return std::get<Integer>(*this);
    }
    [[nodiscard]] const Integer &
    as_integer() const {
        return std::get<Integer>(*this);
    }

    [[nodiscard]] BulkString &
    as_bulk_string() {
        return std::get<BulkString>(*this);
    }
    [[nodiscard]] const BulkString &
    as_bulk_string() const {
        return std::get<BulkString>(*this);
    }

    [[nodiscard]] VerbatimString &
    as_verbatim_string() {
        return std::get<VerbatimString>(*this);
    }
    [[nodiscard]] const VerbatimString &
    as_verbatim_string() const {
        return std::get<VerbatimString>(*this);
    }

    [[nodiscard]] std::string_view
    as_string_view() const {
        if (auto *s = std::get_if<SimpleString>(this)) {
            return s->value;
        }
        if (auto *s = std::get_if<BulkString>(this)) {
            return s->view();
        }
        if (auto *s = std::get_if<VerbatimString>(this)) {
            return s->value;
        }
        return {};
    }

    [[nodiscard]] Array &
    as_array() {
        return std::get<Array>(*this);
    }
    [[nodiscard]] const Array &
    as_array() const {
        return std::get<Array>(*this);
    }

    [[nodiscard]] Boolean &
    as_boolean() {
        return std::get<Boolean>(*this);
    }
    [[nodiscard]] const Boolean &
    as_boolean() const {
        return std::get<Boolean>(*this);
    }

    [[nodiscard]] Double &
    as_double() {
        return std::get<Double>(*this);
    }
    [[nodiscard]] const Double &
    as_double() const {
        return std::get<Double>(*this);
    }

    [[nodiscard]] BigNumber &
    as_big_number() {
        return std::get<BigNumber>(*this);
    }
    [[nodiscard]] const BigNumber &
    as_big_number() const {
        return std::get<BigNumber>(*this);
    }

    [[nodiscard]] Map &
    as_map() {
        return std::get<Map>(*this);
    }
    [[nodiscard]] const Map &
    as_map() const {
        return std::get<Map>(*this);
    }

    [[nodiscard]] Attribute &
    as_attribute() {
        return std::get<Attribute>(*this);
    }
    [[nodiscard]] const Attribute &
    as_attribute() const {
        return std::get<Attribute>(*this);
    }

    [[nodiscard]] Set &
    as_set() {
        return std::get<Set>(*this);
    }
    [[nodiscard]] const Set &
    as_set() const {
        return std::get<Set>(*this);
    }

    [[nodiscard]] Push &
    as_push() {
        return std::get<Push>(*this);
    }
    [[nodiscard]] const Push &
    as_push() const {
        return std::get<Push>(*this);
    }

    [[nodiscard]] size_t
    size() const noexcept {
        if (auto *a = std::get_if<Array>(this))
            return a->size();
        if (auto *m = std::get_if<Map>(this))
            return m->size();
        if (auto *s = std::get_if<Set>(this))
            return s->size();
        if (auto *p = std::get_if<Push>(this))
            return p->size();
        if (auto *b = std::get_if<BulkString>(this))
            return b->size();
        if (auto *s = std::get_if<SimpleString>(this))
            return s->value.size();
        return 0;
    }

    [[nodiscard]] bool
    empty() const noexcept {
        return size() == 0;
    }

    [[nodiscard]] std::optional<int64_t>
    to_integer() const {
        if (auto *i = std::get_if<Integer>(this)) {
            return i->value;
        }
        if (auto *b = std::get_if<Boolean>(this)) {
            return b->value ? 1 : 0;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<double>
    to_double() const {
        if (auto *d = std::get_if<Double>(this)) {
            return d->value;
        }
        if (auto *i = std::get_if<Integer>(this)) {
            return static_cast<double>(i->value);
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::string>
    to_string() const {
        if (auto *s = std::get_if<SimpleString>(this)) {
            return s->value;
        }
        if (auto *s = std::get_if<BulkString>(this)) {
            return s->value;
        }
        if (auto *s = std::get_if<VerbatimString>(this)) {
            return s->value;
        }
        return std::nullopt;
    }
};

// ============================================================================
// Implementation of make_parse_result (after Value is complete)
// ============================================================================

[[nodiscard]] inline ParseResult<Value>
make_parse_result(Value &&value) {
    return ParseResult<Value>(std::move(value));
}

// ============================================================================
// Comparison implementations (after Value is complete)
// ============================================================================

// Array comparison
inline bool
Array::operator==(const Array &other) const {
    if (elements.size() != other.elements.size())
        return false;
    for (size_t i = 0; i < elements.size(); ++i) {
        if (*elements[i] != *other.elements[i])
            return false;
    }
    return true;
}

// Map comparison
inline bool
Map::operator==(const Map &other) const {
    if (entries.size() != other.entries.size())
        return false;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (*entries[i].first != *other.entries[i].first || *entries[i].second != *other.entries[i].second)
            return false;
    }
    return true;
}

// Set comparison
inline bool
Set::operator==(const Set &other) const {
    if (elements.size() != other.elements.size())
        return false;
    for (size_t i = 0; i < elements.size(); ++i) {
        if (*elements[i] != *other.elements[i])
            return false;
    }
    return true;
}

// Push comparison
inline bool
Push::operator==(const Push &other) const {
    if (elements.size() != other.elements.size())
        return false;
    for (size_t i = 0; i < elements.size(); ++i) {
        if (*elements[i] != *other.elements[i])
            return false;
    }
    return true;
}

// Attribute comparison (Value must be complete)
inline bool
Attribute::operator==(const Attribute &other) const {
    if (data != other.data)
        return false;
    if (!value && !other.value)
        return true;
    if (!value || !other.value)
        return false;
    return *value == *other.value;
}

// ============================================================================
// Helper functions
// ============================================================================

[[nodiscard]] constexpr bool
is_valid_type_prefix(char c) noexcept {
    switch (c) {
        case type_id::SIMPLE_STRING:
        case type_id::SIMPLE_ERROR:
        case type_id::INTEGER:
        case type_id::BULK_STRING:
        case type_id::ARRAY:
        case type_id::NULL_:
        case type_id::BOOLEAN:
        case type_id::DOUBLE:
        case type_id::BIG_NUMBER:
        case type_id::BULK_ERROR:
        case type_id::VERBATIM_STRING:
        case type_id::MAP:
        case type_id::ATTRIBUTE:
        case type_id::SET:
        case type_id::PUSH:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] constexpr bool
is_resp3_type(char c) noexcept {
    switch (c) {
        case type_id::NULL_:
        case type_id::BOOLEAN:
        case type_id::DOUBLE:
        case type_id::BIG_NUMBER:
        case type_id::BULK_ERROR:
        case type_id::VERBATIM_STRING:
        case type_id::MAP:
        case type_id::ATTRIBUTE:
        case type_id::SET:
        case type_id::PUSH:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] constexpr bool
is_aggregate_type(char c) noexcept {
    switch (c) {
        case type_id::ARRAY:
        case type_id::MAP:
        case type_id::ATTRIBUTE:
        case type_id::SET:
        case type_id::PUSH:
            return true;
        default:
            return false;
    }
}

} // namespace qb::redis::parser

#endif // QBM_REDIS_PARSER_TYPES_H
