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
 * @file qbm/redis/tests/unit/parser/resp-buffer.cpp
 * @brief Unit tests for the RESP buffer primitives: the owning ring buffer
 *        @ref qb::redis::parser::InputBuffer and the non-owning cursor
 *        @ref qb::redis::parser::ViewBuffer.
 *
 * Split out of the legacy `test-parser-units.cpp` (InputBuffer/ViewBuffer
 * fixtures) and joined by the overflow-safety regression that lived in
 * `test-parser.cpp` (ViewBufferBounds.HugeLengthDoesNotOverflow).
 *
 * Pure logic, no daemon, no event loop, no RESOURCE_LOCK.
 *
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2026 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)
 * @ingroup Redis
 */

#include <gtest/gtest.h>
#include <span>
#include <string>
#include <string_view>

#include "../parser/buffer.h"

using namespace qb::redis::parser;

// Convert a std::string into a span<const char> for InputBuffer::append.
static std::span<const char>
as_span(const std::string &s) {
    return std::span<const char>(s.data(), s.size());
}

// ============================================================================
// InputBuffer — the owning ring buffer (previously 0% directly tested)
// ============================================================================

class InputBufferTest : public ::testing::Test {};

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

TEST_F(InputBufferTest, AppendEmptyIsNoOp) {
    InputBuffer buf;
    std::string empty;
    EXPECT_TRUE(buf.append(as_span(empty)));
    EXPECT_TRUE(buf.empty());
    EXPECT_EQ(buf.size(), 0u);
}

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

TEST_F(InputBufferTest, FindCrlfNoneWhenLoneCr) {
    // A bare CR with no following LF is not a CRLF; find_crlf must not match.
    InputBuffer buf;
    std::string data = "abc\rdef";
    EXPECT_TRUE(buf.append(as_span(data)));
    EXPECT_FALSE(buf.find_crlf().has_value());
    EXPECT_FALSE(buf.extract_line().has_value());
}

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

// Wrap-around: append/consume/append crossing the physical end so the data
// is split across readable_span() + readable_span_second(); reconstruct it.
TEST_F(InputBufferTest, WrapAroundSecondSpan) {
    // capacity 8; sentinel keeps one slot free so usable run is < capacity.
    InputBuffer buf(8);
    ASSERT_EQ(buf.capacity(), 8u);

    std::string first = "ABCDE"; // write_pos -> 5
    EXPECT_TRUE(buf.append(as_span(first)));
    buf.consume(4); // read_pos -> 4, "E" remains
    EXPECT_EQ(buf.size(), 1u);

    // Append 4 more: "FGH" up to physical end (idx 5,6,7) then "I" wraps to idx 0.
    std::string second = "FGHI";
    EXPECT_TRUE(buf.append(as_span(second)));
    EXPECT_EQ(buf.size(), 5u); // "EFGHI"

    auto s1 = buf.readable_span();
    auto s2 = buf.readable_span_second();
    EXPECT_FALSE(s2.empty()); // data wrapped, so second span is non-empty

    std::string reconstructed(s1.data(), s1.size());
    reconstructed.append(s2.data(), s2.size());
    EXPECT_EQ(reconstructed, "EFGHI");

    // peek must traverse the wrap transparently.
    ASSERT_TRUE(buf.peek(0).has_value());
    EXPECT_EQ(*buf.peek(0), 'E');
    ASSERT_TRUE(buf.peek(4).has_value());
    EXPECT_EQ(*buf.peek(4), 'I'); // the wrapped byte
}

// extract_line across a wrapped buffer must still recover the logical line.
TEST_F(InputBufferTest, ExtractLineAcrossWrap) {
    InputBuffer buf(8);
    std::string first = "ABCDE";
    EXPECT_TRUE(buf.append(as_span(first)));
    buf.consume(4); // "E" remains, read_pos near the end

    // Append so that the CRLF and tail wrap around the physical end.
    std::string second = "F\r\nGI";
    EXPECT_TRUE(buf.append(as_span(second)));
    ASSERT_FALSE(buf.readable_span_second().empty());

    auto line = buf.extract_line();
    ASSERT_TRUE(line.has_value());
    EXPECT_EQ(*line, "EF"); // logical bytes before the wrapped CRLF
    auto rest = buf.extract_bytes(2);
    ASSERT_TRUE(rest.has_value());
    EXPECT_EQ(*rest, "GI");
}

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

TEST_F(InputBufferTest, GrowOnLargeAppendDataIntact) {
    InputBuffer  buf(16);
    const size_t before = buf.capacity();
    EXPECT_EQ(before, 16u);

    std::string big(100, 'z');
    EXPECT_TRUE(buf.append(as_span(big)));
    EXPECT_GE(buf.capacity(), 100u); // grew
    EXPECT_EQ(buf.size(), 100u);

    auto        span = buf.readable_span();
    auto        s2   = buf.readable_span_second();
    std::string got(span.data(), span.size());
    got.append(s2.data(), s2.size());
    EXPECT_EQ(got, big);
}

// A buffer that fills exactly to a sub-capacity boundary must NOT look empty:
// this is the regression for the ring sentinel slot (write_pos==read_pos trap).
TEST_F(InputBufferTest, FullThenDrainNotMistakenEmpty) {
    InputBuffer buf(8);
    // Fill up to one byte below capacity (sentinel slot reserved).
    std::string data(7, 'q');
    EXPECT_TRUE(buf.append(as_span(data)));
    EXPECT_FALSE(buf.empty());
    EXPECT_EQ(buf.size(), 7u);

    // Drain fully and confirm it now reads empty (not stuck non-empty).
    auto all = buf.extract_bytes(7);
    ASSERT_TRUE(all.has_value());
    EXPECT_EQ(*all, data);
    EXPECT_TRUE(buf.empty());
    EXPECT_EQ(buf.size(), 0u);
}

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
// ViewBuffer — the non-owning cursor
// ============================================================================

class ViewBufferTest : public ::testing::Test {};

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

// extract_line_view returns a view that ALIASES the underlying memory (zero-copy),
// not a copy. Mutating the source must be visible through the returned view.
TEST_F(ViewBufferTest, LineViewIsZeroCopyAlias) {
    std::string data = "abc\r\n";
    ViewBuffer  vb(as_span(data));
    auto        line = vb.extract_line_view();
    ASSERT_TRUE(line.has_value());
    ASSERT_EQ(line->size(), 3u);
    EXPECT_EQ(line->data(), data.data()); // aliases the source buffer
}

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

TEST_F(ViewBufferTest, FindCrlfNoneFound) {
    std::string data = "abc";
    ViewBuffer  vb(as_span(data));
    EXPECT_FALSE(vb.find_crlf().has_value());
}

TEST_F(ViewBufferTest, GetAndCurrentViews) {
    std::string data = "abcdef";
    ViewBuffer  vb(as_span(data));

    auto g = vb.get(3);
    ASSERT_EQ(g.size(), 3u);
    EXPECT_EQ(std::string(g.data(), g.size()), "abc");
    EXPECT_EQ(vb.position(), 0u); // get() does not consume

    vb.consume(2);
    auto cur = vb.current();
    EXPECT_EQ(std::string(cur.data(), cur.size()), "cdef");

    // get() past the end yields an empty span.
    EXPECT_TRUE(vb.get(100).empty());
}

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

// Length bounds must be overflow-safe: a server-sized length near SIZE_MAX must
// be rejected, not wrap past the `_position + n > size` guard into an
// out-of-bounds span/view. The wrap only triggers when _position > 0, so we
// consume first. (Promoted from test-parser.cpp ViewBufferBounds.)
TEST_F(ViewBufferTest, HugeLengthDoesNotOverflow) {
    const char data[] = "hello";
    ViewBuffer vb(std::span<const char>(data, 5));
    vb.consume(3);                               // _position = 3, "lo" (2 bytes) remain
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
