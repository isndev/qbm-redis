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

//
// Pure-logic unit tests for the RESP parser internals. No live server, no
// network: every test exercises buffer.h / serializer.h / types.h / parser.h
// directly. Goal is to raise coverage of the low-level RESP machinery that the
// integration tests in test-parser.cpp drive only indirectly.
//

#include <cmath>
#include <gtest/gtest.h>
#include <optional>
#include <parser/parser.h>
#include <parser/serializer.h>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace qb::redis::parser;

// ============================================================================
// Helper functions (mirrors test-parser.cpp style)
// ============================================================================

[[maybe_unused]] static std::string
make_bulk_string(std::string_view content) {
    return std::string("$") + std::to_string(content.size()) + "\r\n" + std::string(content) + "\r\n";
}

[[maybe_unused]] static std::string
make_array(std::initializer_list<std::string> elements) {
    std::string result = "*" + std::to_string(elements.size()) + "\r\n";
    for (const auto &elem : elements) {
        result += elem;
    }
    return result;
}

// Convert a std::string into a span<const char> for InputBuffer::append.
static std::span<const char>
as_span(const std::string &s) {
    return std::span<const char>(s.data(), s.size());
}

// ============================================================================
// InputBuffer tests (the ring buffer, previously 0% directly tested)
// ============================================================================

class InputBufferTest : public ::testing::Test {};

// 1. Basic append/size/readable_span/consume/empty.
TEST_F(InputBufferTest, BasicAppendSizeReadConsume) {
    InputBuffer buf;
    EXPECT_TRUE(buf.empty());
    EXPECT_EQ(buf.size(), 0u);

    std::string data = "hello";
    EXPECT_TRUE(buf.append(as_span(data)));
    EXPECT_FALSE(buf.empty());
    EXPECT_EQ(buf.size(), 5u);

    auto span = buf.readable_span();
    ASSERT_EQ(span.size(), 5u);
    EXPECT_EQ(std::string(span.data(), span.size()), "hello");

    buf.consume(3);
    EXPECT_EQ(buf.size(), 2u);
    auto span2 = buf.readable_span();
    EXPECT_EQ(std::string(span2.data(), span2.size()), "lo");

    buf.consume(2);
    EXPECT_TRUE(buf.empty());
    EXPECT_EQ(buf.size(), 0u);
}

// 2. Appending an empty span is a no-op that returns true.
TEST_F(InputBufferTest, AppendEmptyIsNoOp) {
    InputBuffer buf;
    std::string empty;
    EXPECT_TRUE(buf.append(as_span(empty)));
    EXPECT_TRUE(buf.empty());
    EXPECT_EQ(buf.size(), 0u);
}

// 3. find_crlf + extract_line + extract_bytes over "foo\r\nbar".
TEST_F(InputBufferTest, FindCrlfExtractLineAndBytes) {
    InputBuffer buf;
    std::string data = "foo\r\nbar";
    EXPECT_TRUE(buf.append(as_span(data)));

    auto crlf = buf.find_crlf();
    ASSERT_TRUE(crlf.has_value());
    EXPECT_EQ(*crlf, 3u); // CR is at offset 3

    auto line = buf.extract_line();
    ASSERT_TRUE(line.has_value());
    EXPECT_EQ(*line, "foo");
    // line + CRLF consumed, "bar" remains
    EXPECT_EQ(buf.size(), 3u);

    auto bytes = buf.extract_bytes(3);
    ASSERT_TRUE(bytes.has_value());
    EXPECT_EQ(*bytes, "bar");
    EXPECT_TRUE(buf.empty());

    // extract_bytes asking for more than available -> nullopt
    EXPECT_FALSE(buf.extract_bytes(1).has_value());
}

// 4. peek bounds: offset 0 ok, offset == size() -> nullopt.
TEST_F(InputBufferTest, PeekBounds) {
    InputBuffer buf;
    std::string data = "AB";
    EXPECT_TRUE(buf.append(as_span(data)));

    auto p0 = buf.peek(0);
    ASSERT_TRUE(p0.has_value());
    EXPECT_EQ(*p0, 'A');

    auto p1 = buf.peek(1);
    ASSERT_TRUE(p1.has_value());
    EXPECT_EQ(*p1, 'B');

    EXPECT_FALSE(buf.peek(2).has_value());          // offset == size()
    EXPECT_FALSE(buf.peek(buf.size()).has_value()); // explicit size()
}

// 5. Wrap-around: a small buffer with append/consume/append crossing the end so
//    readable_span_second() becomes non-empty; reconstruct the logical data.
TEST_F(InputBufferTest, WrapAroundSecondSpan) {
    // capacity 8; sentinel keeps one slot free so usable run is < capacity.
    InputBuffer buf(8);
    ASSERT_EQ(buf.capacity(), 8u);

    std::string first = "ABCDE"; // write_pos -> 5
    EXPECT_TRUE(buf.append(as_span(first)));
    buf.consume(4); // read_pos -> 4, "E" remains
    EXPECT_EQ(buf.size(), 1u);

    // Append 4 more: writes "FGH" up to physical end (idx 5,6,7) then wraps "I" to idx 0.
    std::string second = "FGHI";
    EXPECT_TRUE(buf.append(as_span(second)));
    EXPECT_EQ(buf.size(), 5u); // "EFGHI"

    auto s1 = buf.readable_span();
    auto s2 = buf.readable_span_second();
    EXPECT_FALSE(s2.empty()); // data wrapped, so second span is non-empty

    std::string reconstructed(s1.data(), s1.size());
    reconstructed.append(s2.data(), s2.size());
    EXPECT_EQ(reconstructed, "EFGHI");
}

// 6. compact() on wrapped data makes it contiguous (second span empties).
TEST_F(InputBufferTest, CompactWrappedDataBecomesContiguous) {
    InputBuffer buf(8);
    std::string first = "ABCDE";
    EXPECT_TRUE(buf.append(as_span(first)));
    buf.consume(4);
    std::string second = "FGHI";
    EXPECT_TRUE(buf.append(as_span(second)));
    ASSERT_FALSE(buf.readable_span_second().empty()); // wrapped first

    buf.compact();

    EXPECT_TRUE(buf.readable_span_second().empty()); // now contiguous
    auto span = buf.readable_span();
    ASSERT_EQ(span.size(), 5u);
    EXPECT_EQ(std::string(span.data(), span.size()), "EFGHI");
}

// 7. compact() on contiguous-with-offset data shifts it back to index 0.
TEST_F(InputBufferTest, CompactContiguousWithOffset) {
    InputBuffer buf(64);
    std::string data = "hello world";
    EXPECT_TRUE(buf.append(as_span(data)));
    buf.consume(6); // read_pos -> 6, "world" remains, contiguous but offset

    EXPECT_TRUE(buf.readable_span_second().empty());
    buf.compact();

    auto span = buf.readable_span();
    ASSERT_EQ(span.size(), 5u);
    EXPECT_EQ(std::string(span.data(), span.size()), "world");
}

// 8. Grow on a large append: capacity grows and data survives intact.
TEST_F(InputBufferTest, GrowOnLargeAppendDataIntact) {
    InputBuffer  buf(16);
    const size_t before = buf.capacity();
    EXPECT_EQ(before, 16u);

    std::string big(100, 'z');
    EXPECT_TRUE(buf.append(as_span(big)));
    EXPECT_GE(buf.capacity(), 100u); // grew
    EXPECT_EQ(buf.size(), 100u);

    auto span = buf.readable_span();
    // After a single contiguous append the whole payload is in the first span.
    auto        s2 = buf.readable_span_second();
    std::string got(span.data(), span.size());
    got.append(s2.data(), s2.size());
    EXPECT_EQ(got, big);
}

// 9. reset() keeps capacity and marks the buffer empty.
TEST_F(InputBufferTest, ResetKeepsCapacity) {
    InputBuffer buf(128);
    std::string data = "payload";
    EXPECT_TRUE(buf.append(as_span(data)));
    const size_t cap = buf.capacity();

    buf.reset();
    EXPECT_TRUE(buf.empty());
    EXPECT_EQ(buf.size(), 0u);
    EXPECT_EQ(buf.capacity(), cap); // storage retained
}

// ============================================================================
// ViewBuffer tests
// ============================================================================

class ViewBufferTest : public ::testing::Test {};

// 10. extract_line_view / extract_bytes_view / current / remaining / position /
//     empty over "ab\r\ncd".
TEST_F(ViewBufferTest, LineViewBytesViewCursorAccessors) {
    std::string data = "ab\r\ncd";
    ViewBuffer  vb(as_span(data));

    EXPECT_EQ(vb.size(), 6u);
    EXPECT_EQ(vb.remaining(), 6u);
    EXPECT_EQ(vb.position(), 0u);
    EXPECT_FALSE(vb.empty());

    auto line = vb.extract_line_view();
    ASSERT_TRUE(line.has_value());
    EXPECT_EQ(*line, "ab");
    EXPECT_EQ(vb.position(), 4u); // "ab" + CRLF consumed
    EXPECT_EQ(vb.remaining(), 2u);

    auto cur = vb.current();
    EXPECT_EQ(std::string(cur.data(), cur.size()), "cd");

    auto bytes = vb.extract_bytes_view(2);
    ASSERT_TRUE(bytes.has_value());
    EXPECT_EQ(*bytes, "cd");
    EXPECT_TRUE(vb.empty());
    EXPECT_EQ(vb.remaining(), 0u);

    // extract_bytes_view past the end -> nullopt
    EXPECT_FALSE(vb.extract_bytes_view(1).has_value());
}

// 11. skip_crlf three branches: "\r\nX" true, "XY" false, single-byte false.
TEST_F(ViewBufferTest, SkipCrlfBranches) {
    {
        std::string data = "\r\nX";
        ViewBuffer  vb(as_span(data));
        EXPECT_TRUE(vb.skip_crlf());
        EXPECT_EQ(vb.position(), 2u);
    }
    {
        std::string data = "XY"; // two bytes but not CRLF
        ViewBuffer  vb(as_span(data));
        EXPECT_FALSE(vb.skip_crlf());
        EXPECT_EQ(vb.position(), 0u); // unchanged on failure
    }
    {
        std::string data = "\r"; // only one byte
        ViewBuffer  vb(as_span(data));
        EXPECT_FALSE(vb.skip_crlf());
        EXPECT_EQ(vb.position(), 0u);
    }
}

// 12. find_crlf returns nullopt when no CRLF is present.
TEST_F(ViewBufferTest, FindCrlfNoneFound) {
    std::string data = "abc";
    ViewBuffer  vb(as_span(data));
    EXPECT_FALSE(vb.find_crlf().has_value());
}

// 13. reset() rebinds the view to new data and rewinds.
TEST_F(ViewBufferTest, ResetRebinds) {
    std::string first = "first\r\n";
    ViewBuffer  vb(as_span(first));
    auto        l1 = vb.extract_line_view();
    ASSERT_TRUE(l1.has_value());
    EXPECT_EQ(*l1, "first");
    EXPECT_TRUE(vb.empty());

    std::string second = "second\r\n";
    vb.reset(as_span(second));
    EXPECT_EQ(vb.position(), 0u);
    EXPECT_EQ(vb.size(), second.size());
    auto l2 = vb.extract_line_view();
    ASSERT_TRUE(l2.has_value());
    EXPECT_EQ(*l2, "second");
}

// ============================================================================
// Serializer tests
// ============================================================================

class SerializerUnitTest : public ::testing::Test {};

// 14. Double finite / inf / -inf / nan with the exact spelling from serializer.h.
TEST_F(SerializerUnitTest, SerializeDoubleVariants) {
    EXPECT_EQ(Serializer::serialize(Value(Double{1.5})), ",1.5\r\n");
    EXPECT_EQ(Serializer::serialize(Value(Double{std::numeric_limits<double>::infinity()})), ",inf\r\n");
    EXPECT_EQ(Serializer::serialize(Value(Double{-std::numeric_limits<double>::infinity()})), ",-inf\r\n");
    EXPECT_EQ(Serializer::serialize(Value(Double{std::numeric_limits<double>::quiet_NaN()})), ",nan\r\n");
}

// 15. BigNumber.
TEST_F(SerializerUnitTest, SerializeBigNumber) {
    EXPECT_EQ(Serializer::serialize(Value(BigNumber{"12345678901234567890", false})), "(12345678901234567890\r\n");
}

// 16. BulkError all branches: prefix+message, prefix-only, message-only.
TEST_F(SerializerUnitTest, SerializeBulkErrorBranches) {
    // prefix + message: "SYNTAX bad" -> 10 bytes
    EXPECT_EQ(Serializer::serialize(Value(BulkError{"SYNTAX", "bad"})), "!10\r\nSYNTAX bad\r\n");
    // prefix only (empty message): "ERR" -> 3 bytes, no trailing space
    EXPECT_EQ(Serializer::serialize(Value(BulkError{"ERR", ""})), "!3\r\nERR\r\n");
    // message only (empty prefix): "boom" -> 4 bytes
    EXPECT_EQ(Serializer::serialize(Value(BulkError{"", "boom"})), "!4\r\nboom\r\n");
}

// 17. VerbatimString.
TEST_F(SerializerUnitTest, SerializeVerbatimString) {
    VerbatimString vs;
    vs.encoding[0] = 't';
    vs.encoding[1] = 'x';
    vs.encoding[2] = 't';
    vs.value       = "Some string";
    // total_len = 4 + value.size() = 4 + 11 = 15
    EXPECT_EQ(Serializer::serialize(Value(std::move(vs))), "=15\r\ntxt:Some string\r\n");
}

// 18. Push.
TEST_F(SerializerUnitTest, SerializePush) {
    Push p;
    p.elements.push_back(std::make_unique<Value>(Value(SimpleString{"message"})));
    p.elements.push_back(std::make_unique<Value>(Value(Integer{1})));
    EXPECT_EQ(Serializer::serialize(Value(std::move(p))), ">2\r\n+message\r\n:1\r\n");
}

// 19. Attribute with value and without value (Null fallback path).
TEST_F(SerializerUnitTest, SerializeAttributeWithAndWithoutValue) {
    // With a real value attached.
    {
        Attribute a;
        a.data.entries.emplace_back(std::make_unique<Value>(Value(SimpleString{"k"})), std::make_unique<Value>(Value(Integer{7})));
        a.value = std::make_unique<Value>(Value(Integer{42}));
        EXPECT_EQ(Serializer::serialize(Value(std::move(a))), "|1\r\n+k\r\n:7\r\n:42\r\n");
    }
    // No value -> serializer emits a trailing Null ("_\r\n").
    {
        Attribute a;
        a.data.entries.emplace_back(std::make_unique<Value>(Value(SimpleString{"k"})), std::make_unique<Value>(Value(Integer{7})));
        // a.value left null
        EXPECT_EQ(Serializer::serialize(Value(std::move(a))), "|1\r\n+k\r\n:7\r\n_\r\n");
    }
}

// 20. serialize_command variadic ("GET", "key").
TEST_F(SerializerUnitTest, SerializeCommandVariadic) {
    auto cmd = Serializer::serialize_command("GET", "key");
    EXPECT_EQ(cmd, "*2\r\n$3\r\nGET\r\n$3\r\nkey\r\n");
}

// 21. serialize_hello password-only (uses the "default" username branch).
TEST_F(SerializerUnitTest, SerializeHelloPasswordOnly) {
    auto hello = Serializer::serialize_hello(ProtocolVersion::RESP3, std::nullopt, std::string("secret"));
    EXPECT_EQ(hello, "*5\r\n$5\r\nHELLO\r\n$1\r\n3\r\n$4\r\nAUTH\r\n$7\r\ndefault\r\n$6\r\nsecret\r\n");
}

// 22. Free serialization helpers.
TEST_F(SerializerUnitTest, FreeHelperFunctions) {
    EXPECT_EQ(serialize_simple_string("OK"), "+OK\r\n");
    EXPECT_EQ(serialize_error("ERR boom"), "-ERR boom\r\n");
    EXPECT_EQ(serialize_integer(-42), ":-42\r\n");
    EXPECT_EQ(serialize_bulk_string("hi"), "$2\r\nhi\r\n");
    EXPECT_EQ(serialize_null(), "_\r\n");
    EXPECT_EQ(serialize_array_header(3), "*3\r\n");
}

// 23. CommandBuilder arg overloads + arg_count + build.
TEST_F(SerializerUnitTest, CommandBuilderArgOverloads) {
    CommandBuilder cmd("SET");
    cmd.arg("strkey");                       // std::string_view overload
    cmd.arg(static_cast<const char *>("c")); // const char* overload
    cmd.arg(static_cast<int64_t>(123));      // int64_t overload
    cmd.arg(1.5);                            // double overload

    EXPECT_EQ(cmd.arg_count(), 5u); // SET + 4 args

    auto built = cmd.build();
    EXPECT_EQ(built, "*5\r\n$3\r\nSET\r\n$6\r\nstrkey\r\n$1\r\nc\r\n$3\r\n123\r\n$3\r\n1.5\r\n");
}

// 24. CommandBuilder build_with_raw + clear + default ctor.
TEST_F(SerializerUnitTest, CommandBuilderRawClearDefaultCtor) {
    CommandBuilder cmd; // default ctor, no command
    EXPECT_EQ(cmd.arg_count(), 0u);

    cmd.arg("PING");
    EXPECT_EQ(cmd.arg_count(), 1u);

    std::string raw      = "EXTRA";
    auto        with_raw = cmd.build_with_raw(std::span<const char>(raw.data(), raw.size()));
    EXPECT_EQ(with_raw, "*1\r\n$4\r\nPING\r\nEXTRA");

    cmd.clear();
    EXPECT_EQ(cmd.arg_count(), 0u);
    EXPECT_EQ(cmd.build(), "*0\r\n");
}

// ============================================================================
// Types tests
// ============================================================================

class TypesUnitTest : public ::testing::Test {};

// 25. Value conversion helpers across kinds incl. nullopt / default branches.
TEST_F(TypesUnitTest, ValueConversionHelpers) {
    // to_integer: Integer, Boolean(true/false), and nullopt for non-numeric-int.
    EXPECT_EQ(Value(Integer{99}).to_integer(), std::optional<int64_t>(99));
    EXPECT_EQ(Value(Boolean{true}).to_integer(), std::optional<int64_t>(1));
    EXPECT_EQ(Value(Boolean{false}).to_integer(), std::optional<int64_t>(0));
    EXPECT_FALSE(Value(SimpleString{"x"}).to_integer().has_value());

    // to_double: Double, Integer-promoted, nullopt otherwise.
    EXPECT_EQ(Value(Double{2.5}).to_double(), std::optional<double>(2.5));
    EXPECT_EQ(Value(Integer{4}).to_double(), std::optional<double>(4.0));
    EXPECT_FALSE(Value(SimpleString{"x"}).to_double().has_value());

    // to_string: SimpleString, BulkString, VerbatimString, nullopt otherwise.
    EXPECT_EQ(Value(SimpleString{"ss"}).to_string(), std::optional<std::string>("ss"));
    EXPECT_EQ(Value(BulkString{"bs"}).to_string(), std::optional<std::string>("bs"));
    {
        VerbatimString vs;
        vs.encoding[0] = 't';
        vs.encoding[1] = 'x';
        vs.encoding[2] = 't';
        vs.value       = "vv";
        EXPECT_EQ(Value(std::move(vs)).to_string(), std::optional<std::string>("vv"));
    }
    EXPECT_FALSE(Value(Integer{1}).to_string().has_value());

    // as_string_view: across the three string kinds, empty default otherwise.
    EXPECT_EQ(Value(SimpleString{"abc"}).as_string_view(), "abc");
    EXPECT_EQ(Value(BulkString{"def"}).as_string_view(), "def");
    EXPECT_TRUE(Value(Integer{1}).as_string_view().empty());

    // size(): aggregates, bulk string, simple string, and 0 default.
    {
        Array arr;
        arr.elements.push_back(std::make_unique<Value>(Value(Integer{1})));
        arr.elements.push_back(std::make_unique<Value>(Value(Integer{2})));
        Value v(std::move(arr));
        EXPECT_EQ(v.size(), 2u);
        EXPECT_FALSE(v.empty());
    }
    EXPECT_EQ(Value(BulkString{"hello"}).size(), 5u);
    EXPECT_EQ(Value(SimpleString{"hi"}).size(), 2u);
    EXPECT_EQ(Value(Integer{1}).size(), 0u);
    EXPECT_TRUE(Value(Integer{1}).empty());

    // get_error_message: SimpleError, BulkError, and empty for non-errors.
    EXPECT_EQ(Value(SimpleError{"ERR", "boom"}).get_error_message(), "ERR boom");
    EXPECT_EQ(Value(BulkError{"WRONGTYPE", "no"}).get_error_message(), "WRONGTYPE no");
    EXPECT_TRUE(Value(Integer{1}).get_error_message().empty());
}

// 26. Aggregate operator== mismatch branches + full_message() 3 branches +
//     ParseError::what() for several codes.
TEST_F(TypesUnitTest, EqualityFullMessageAndWhat) {
    // Array operator== size-mismatch branch returns false.
    {
        Array a1;
        a1.elements.push_back(std::make_unique<Value>(Value(Integer{1})));
        Array a2; // empty
        EXPECT_FALSE(a1 == a2);
    }
    // Array operator== element-mismatch branch returns false.
    {
        Array a1;
        a1.elements.push_back(std::make_unique<Value>(Value(Integer{1})));
        Array a2;
        a2.elements.push_back(std::make_unique<Value>(Value(Integer{2})));
        EXPECT_FALSE(a1 == a2);
    }
    // Array operator== equal branch returns true.
    {
        Array a1;
        a1.elements.push_back(std::make_unique<Value>(Value(Integer{1})));
        Array a2;
        a2.elements.push_back(std::make_unique<Value>(Value(Integer{1})));
        EXPECT_TRUE(a1 == a2);
    }

    // SimpleError::full_message() three branches.
    EXPECT_EQ((SimpleError{"", "only-msg"}).full_message(), "only-msg"); // empty prefix
    EXPECT_EQ((SimpleError{"ONLY", ""}).full_message(), "ONLY");         // empty message
    EXPECT_EQ((SimpleError{"ERR", "boom"}).full_message(), "ERR boom");  // both

    // BulkError::full_message() three branches.
    EXPECT_EQ((BulkError{"", "only-msg"}).full_message(), "only-msg");
    EXPECT_EQ((BulkError{"ONLY", ""}).full_message(), "ONLY");
    EXPECT_EQ((BulkError{"ERR", "boom"}).full_message(), "ERR boom");

    // ParseError::what() for several codes.
    EXPECT_STREQ(ParseError(ParseErrorCode::OK).what(), "OK");
    EXPECT_STREQ(ParseError(ParseErrorCode::INCOMPLETE_DATA).what(), "Incomplete data");
    EXPECT_STREQ(ParseError(ParseErrorCode::INVALID_TYPE).what(), "Invalid type");
    EXPECT_STREQ(ParseError(ParseErrorCode::INVALID_VERBATIM_FORMAT).what(), "Invalid verbatim format");
    EXPECT_STREQ(ParseError(ParseErrorCode::NESTING_TOO_DEEP).what(), "Nesting too deep");
    EXPECT_STREQ(ParseError(ParseErrorCode::BUFFER_OVERFLOW).what(), "Buffer overflow");
    EXPECT_STREQ(ParseError(ParseErrorCode::PROTOCOL_ERROR).what(), "Protocol error");

    // ParseError code()/message() accessors.
    ParseError pe(ParseErrorCode::INVALID_LENGTH, "bad len");
    EXPECT_EQ(pe.code(), ParseErrorCode::INVALID_LENGTH);
    EXPECT_EQ(pe.message(), "bad len");
}

// ============================================================================
// Parser tests
// ============================================================================

class ParserUnitTest : public ::testing::Test {
protected:
    ParserConfig config; // defaults to RESP3
};

// 27. Verbatim string too short -> INVALID_VERBATIM_FORMAT.
TEST_F(ParserUnitTest, VerbatimTooShort) {
    // len = 3 ("txt") is below the minimum of 4.
    auto result = parse("=3\r\ntxt\r\n", config);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ParseErrorCode::INVALID_VERBATIM_FORMAT);
}

// 28. Verbatim string with the colon not at position 3 -> INVALID_VERBATIM_FORMAT.
TEST_F(ParserUnitTest, VerbatimBadColon) {
    // len = 4, payload "txtx" has no colon at index 3 (colon_pos != 3).
    auto result = parse("=4\r\ntxtx\r\n", config);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ParseErrorCode::INVALID_VERBATIM_FORMAT);
}

// 29. Big number: empty body and non-digit body -> INVALID_BIG_NUMBER.
TEST_F(ParserUnitTest, BigNumberEmptyAndNonDigit) {
    {
        auto result = parse("(\r\n", config); // empty big number
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code(), ParseErrorCode::INVALID_BIG_NUMBER);
    }
    {
        auto result = parse("(12a34\r\n", config); // non-digit
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code(), ParseErrorCode::INVALID_BIG_NUMBER);
    }
}

// 30. Negative length (other than -1) on map / set / push -> INVALID_LENGTH.
TEST_F(ParserUnitTest, NegativeLengthAggregates) {
    {
        auto result = parse("%-7\r\n", config);
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code(), ParseErrorCode::INVALID_LENGTH);
    }
    {
        auto result = parse("~-2\r\n", config);
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code(), ParseErrorCode::INVALID_LENGTH);
    }
    {
        auto result = parse(">-3\r\n", config);
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code(), ParseErrorCode::INVALID_LENGTH);
    }
}

// 31. State transitions: is_parsing / is_complete / unparsed_data.
TEST_F(ParserUnitTest, StateTransitions) {
    RespParser parser;
    EXPECT_TRUE(parser.is_ready());
    EXPECT_FALSE(parser.is_parsing());
    EXPECT_FALSE(parser.is_complete());
    EXPECT_FALSE(parser.has_error());

    // Feed partial bulk; parse returns INCOMPLETE_DATA but the parser is not faulted.
    EXPECT_TRUE(parser.feed("$5\r\nhel"));
    auto incomplete = parser.parse();
    EXPECT_FALSE(incomplete.has_value());
    EXPECT_EQ(incomplete.error().code(), ParseErrorCode::INCOMPLETE_DATA);
    EXPECT_FALSE(parser.has_error());

    // unparsed_data exposes the still-buffered bytes.
    auto pending = parser.unparsed_data();
    EXPECT_EQ(std::string(pending.data(), pending.size()), "$5\r\nhel");

    // Complete the value and parse it.
    EXPECT_TRUE(parser.feed("lo\r\n"));
    auto done = parser.parse();
    ASSERT_TRUE(done.has_value());
    EXPECT_EQ(done->as_bulk_string().value, "hello");

    // Drive the parser into the FAULT state with corrupt input, then confirm
    // is_parsing()/has_error() reflect it and feed() is refused.
    RespParser bad;
    EXPECT_TRUE(bad.feed("$3\r\nabcXX")); // payload not terminated by CRLF
    auto values = bad.parse_all();        // PROTOCOL_ERROR faults the parser
    EXPECT_TRUE(values.empty());
    EXPECT_TRUE(bad.has_error());
    EXPECT_FALSE(bad.is_parsing()); // FAULT is excluded from is_parsing()
    EXPECT_FALSE(bad.feed("more")); // feed refused while faulted
}

// 32. feed() std::span<const char> overload.
TEST_F(ParserUnitTest, FeedSpanOverload) {
    RespParser  parser;
    std::string data = "+OK\r\n";
    EXPECT_TRUE(parser.feed(std::span<const char>(data.data(), data.size())));

    auto result = parser.parse();
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->is_simple_string());
    EXPECT_EQ(result->as_string_view(), "OK");
}

// 33. has_complete_value scalar fast path (CRLF-only) and bulk full-parse path.
TEST_F(ParserUnitTest, HasCompleteValueFastPath) {
    // Empty buffer -> false.
    {
        RespParser parser;
        EXPECT_FALSE(parser.has_complete_value());
    }
    // Scalar fast path: a single CRLF confirms a simple string.
    {
        RespParser parser;
        EXPECT_TRUE(parser.feed("+OK\r\n"));
        EXPECT_TRUE(parser.has_complete_value());
    }
    // Scalar fast path: no CRLF yet -> false.
    {
        RespParser parser;
        EXPECT_TRUE(parser.feed(":123")); // integer type, no terminator
        EXPECT_FALSE(parser.has_complete_value());
    }
    // Bulk full-parse path: incomplete bulk -> false, completed bulk -> true.
    {
        RespParser parser;
        EXPECT_TRUE(parser.feed("$5\r\nhel"));
        EXPECT_FALSE(parser.has_complete_value());
        EXPECT_TRUE(parser.feed("lo\r\n"));
        EXPECT_TRUE(parser.has_complete_value());
    }
}

// 34. Map::operator== size-mismatch, element-mismatch, and equal branches.
TEST_F(TypesUnitTest, MapEquality) {
    auto make_map = [](int64_t k, int64_t v) {
        Map m;
        m.entries.emplace_back(std::make_unique<Value>(Value(Integer{k})), std::make_unique<Value>(Value(Integer{v})));
        return m;
    };

    // size-mismatch branch -> false
    {
        Map a = make_map(1, 2);
        Map b; // empty
        EXPECT_FALSE(a == b);
    }
    // element-mismatch branch (value differs) -> false
    {
        Map a = make_map(1, 2);
        Map b = make_map(1, 3);
        EXPECT_FALSE(a == b);
    }
    // element-mismatch branch (key differs) -> false
    {
        Map a = make_map(1, 2);
        Map b = make_map(9, 2);
        EXPECT_FALSE(a == b);
    }
    // equal branch -> true
    {
        Map a = make_map(1, 2);
        Map b = make_map(1, 2);
        EXPECT_TRUE(a == b);
    }
}

// 35. Set::operator== size-mismatch, element-mismatch, and equal branches.
TEST_F(TypesUnitTest, SetEquality) {
    // size-mismatch branch -> false
    {
        Set a;
        a.elements.push_back(std::make_unique<Value>(Value(Integer{1})));
        Set b; // empty
        EXPECT_FALSE(a == b);
    }
    // element-mismatch branch -> false
    {
        Set a;
        a.elements.push_back(std::make_unique<Value>(Value(Integer{1})));
        Set b;
        b.elements.push_back(std::make_unique<Value>(Value(Integer{2})));
        EXPECT_FALSE(a == b);
    }
    // equal branch -> true
    {
        Set a;
        a.elements.push_back(std::make_unique<Value>(Value(Integer{7})));
        Set b;
        b.elements.push_back(std::make_unique<Value>(Value(Integer{7})));
        EXPECT_TRUE(a == b);
    }
}

// 36. Push::operator== size-mismatch, element-mismatch, and equal branches.
TEST_F(TypesUnitTest, PushEquality) {
    // size-mismatch branch -> false
    {
        Push a;
        a.elements.push_back(std::make_unique<Value>(Value(Integer{1})));
        Push b; // empty
        EXPECT_FALSE(a == b);
    }
    // element-mismatch branch -> false
    {
        Push a;
        a.elements.push_back(std::make_unique<Value>(Value(Integer{1})));
        Push b;
        b.elements.push_back(std::make_unique<Value>(Value(Integer{2})));
        EXPECT_FALSE(a == b);
    }
    // equal branch -> true
    {
        Push a;
        a.elements.push_back(std::make_unique<Value>(Value(Integer{5})));
        Push b;
        b.elements.push_back(std::make_unique<Value>(Value(Integer{5})));
        EXPECT_TRUE(a == b);
    }
}

// 37. Attribute::operator== all four branches.
TEST_F(TypesUnitTest, AttributeEquality) {
    auto make_attr = [](int64_t k, int64_t mapv, std::optional<int64_t> val) {
        Attribute a;
        a.data.entries.emplace_back(std::make_unique<Value>(Value(Integer{k})), std::make_unique<Value>(Value(Integer{mapv})));
        if (val)
            a.value = std::make_unique<Value>(Value(Integer{*val}));
        return a;
    };

    // Branch 1: data differs -> false.
    {
        Attribute a = make_attr(1, 2, 42);
        Attribute b = make_attr(1, 9, 42);
        EXPECT_FALSE(a == b);
    }
    // Branch 2: equal data, both values null -> true.
    {
        Attribute a = make_attr(1, 2, std::nullopt);
        Attribute b = make_attr(1, 2, std::nullopt);
        EXPECT_TRUE(a == b);
    }
    // Branch 3: equal data, exactly one value null -> false.
    {
        Attribute a = make_attr(1, 2, 42);
        Attribute b = make_attr(1, 2, std::nullopt);
        EXPECT_FALSE(a == b);
        // and the symmetric case
        Attribute c = make_attr(1, 2, std::nullopt);
        Attribute d = make_attr(1, 2, 42);
        EXPECT_FALSE(c == d);
    }
    // Branch 4: equal data, both values present -> compare values.
    {
        Attribute a = make_attr(1, 2, 42);
        Attribute b = make_attr(1, 2, 42);
        EXPECT_TRUE(a == b);
        Attribute c = make_attr(1, 2, 42);
        Attribute d = make_attr(1, 2, 99);
        EXPECT_FALSE(c == d);
    }
}

// 38. Free constexpr helpers: is_valid_type_prefix / is_resp3_type / is_aggregate_type.
TEST_F(TypesUnitTest, TypePrefixHelpers) {
    // is_valid_type_prefix: a representative true per family + default false.
    EXPECT_TRUE(is_valid_type_prefix(type_id::SIMPLE_STRING));
    EXPECT_TRUE(is_valid_type_prefix(type_id::SIMPLE_ERROR));
    EXPECT_TRUE(is_valid_type_prefix(type_id::INTEGER));
    EXPECT_TRUE(is_valid_type_prefix(type_id::BULK_STRING));
    EXPECT_TRUE(is_valid_type_prefix(type_id::ARRAY));
    EXPECT_TRUE(is_valid_type_prefix(type_id::NULL_));
    EXPECT_TRUE(is_valid_type_prefix(type_id::BOOLEAN));
    EXPECT_TRUE(is_valid_type_prefix(type_id::DOUBLE));
    EXPECT_TRUE(is_valid_type_prefix(type_id::BIG_NUMBER));
    EXPECT_TRUE(is_valid_type_prefix(type_id::BULK_ERROR));
    EXPECT_TRUE(is_valid_type_prefix(type_id::VERBATIM_STRING));
    EXPECT_TRUE(is_valid_type_prefix(type_id::MAP));
    EXPECT_TRUE(is_valid_type_prefix(type_id::ATTRIBUTE));
    EXPECT_TRUE(is_valid_type_prefix(type_id::SET));
    EXPECT_TRUE(is_valid_type_prefix(type_id::PUSH));
    EXPECT_FALSE(is_valid_type_prefix('X')); // default branch
    EXPECT_FALSE(is_valid_type_prefix('\0'));

    // is_resp3_type: RESP3-only types true, RESP2 types and junk false.
    EXPECT_TRUE(is_resp3_type(type_id::NULL_));
    EXPECT_TRUE(is_resp3_type(type_id::BOOLEAN));
    EXPECT_TRUE(is_resp3_type(type_id::DOUBLE));
    EXPECT_TRUE(is_resp3_type(type_id::BIG_NUMBER));
    EXPECT_TRUE(is_resp3_type(type_id::BULK_ERROR));
    EXPECT_TRUE(is_resp3_type(type_id::VERBATIM_STRING));
    EXPECT_TRUE(is_resp3_type(type_id::MAP));
    EXPECT_TRUE(is_resp3_type(type_id::ATTRIBUTE));
    EXPECT_TRUE(is_resp3_type(type_id::SET));
    EXPECT_TRUE(is_resp3_type(type_id::PUSH));
    EXPECT_FALSE(is_resp3_type(type_id::SIMPLE_STRING)); // RESP2 -> false (default)
    EXPECT_FALSE(is_resp3_type(type_id::INTEGER));
    EXPECT_FALSE(is_resp3_type('X'));

    // is_aggregate_type: the five aggregates true, scalars and junk false.
    EXPECT_TRUE(is_aggregate_type(type_id::ARRAY));
    EXPECT_TRUE(is_aggregate_type(type_id::MAP));
    EXPECT_TRUE(is_aggregate_type(type_id::ATTRIBUTE));
    EXPECT_TRUE(is_aggregate_type(type_id::SET));
    EXPECT_TRUE(is_aggregate_type(type_id::PUSH));
    EXPECT_FALSE(is_aggregate_type(type_id::SIMPLE_STRING)); // default branch
    EXPECT_FALSE(is_aggregate_type(type_id::INTEGER));
    EXPECT_FALSE(is_aggregate_type('X'));

    // Exercise constexpr evaluation context too.
    static_assert(is_valid_type_prefix(type_id::ARRAY));
    static_assert(!is_valid_type_prefix('X'));
    static_assert(is_resp3_type(type_id::MAP));
    static_assert(!is_resp3_type(type_id::INTEGER));
    static_assert(is_aggregate_type(type_id::SET));
    static_assert(!is_aggregate_type(type_id::INTEGER));
}

// 39. ParseError::what() for the remaining (previously unasserted) codes.
TEST_F(TypesUnitTest, ParseErrorWhatRemainingCodes) {
    EXPECT_STREQ(ParseError(ParseErrorCode::INVALID_LENGTH).what(), "Invalid length");
    EXPECT_STREQ(ParseError(ParseErrorCode::INVALID_INTEGER).what(), "Invalid integer");
    EXPECT_STREQ(ParseError(ParseErrorCode::INVALID_DOUBLE).what(), "Invalid double");
    EXPECT_STREQ(ParseError(ParseErrorCode::INVALID_BOOLEAN).what(), "Invalid boolean");
    EXPECT_STREQ(ParseError(ParseErrorCode::INVALID_BIG_NUMBER).what(), "Invalid big number");
}

// 40. Implicit conversion operators: Integer->int64_t, Boolean->bool, Double->double.
TEST_F(TypesUnitTest, ScalarImplicitConversions) {
    // Integer::operator int64_t
    {
        Integer i{1234};
        int64_t n = i; // implicit
        EXPECT_EQ(n, 1234);
        EXPECT_EQ(static_cast<int64_t>(Integer{-7}), -7);
    }
    // Boolean::operator bool
    {
        Boolean t{true};
        Boolean f{false};
        bool    bt = t; // implicit
        bool    bf = f;
        EXPECT_TRUE(bt);
        EXPECT_FALSE(bf);
    }
    // Double::operator double
    {
        Double d{3.5};
        double x = d; // implicit
        EXPECT_DOUBLE_EQ(x, 3.5);
        EXPECT_DOUBLE_EQ(static_cast<double>(Double{-1.25}), -1.25);
    }
}

// 41. BulkString view()/empty()/size() and VerbatimString encoding_view().
TEST_F(TypesUnitTest, BulkAndVerbatimAccessors) {
    // BulkString accessors on a non-empty value.
    {
        BulkString b{"hello"};
        EXPECT_EQ(b.view(), "hello");
        EXPECT_FALSE(b.empty());
        EXPECT_EQ(b.size(), 5u);
    }
    // BulkString accessors on an empty value.
    {
        BulkString b{""};
        EXPECT_TRUE(b.view().empty());
        EXPECT_TRUE(b.empty());
        EXPECT_EQ(b.size(), 0u);
    }
    // VerbatimString::encoding_view returns the 3-char encoding tag.
    {
        VerbatimString vs;
        vs.encoding[0] = 'm';
        vs.encoding[1] = 'k';
        vs.encoding[2] = 'd';
        vs.value       = "body";
        auto ev        = vs.encoding_view();
        EXPECT_EQ(ev.size(), VerbatimString::ENCODING_LEN);
        EXPECT_EQ(ev, "mkd");
    }
}

// ============================================================================
// Main
// ============================================================================

int
main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
