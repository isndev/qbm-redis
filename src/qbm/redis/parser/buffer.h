/**
 * @file qbm/redis/parser/buffer.h
 * @brief Streaming byte buffers for RESP parsing.
 *
 * Provides two complementary buffer types used by the RESP parser:
 *   - @ref qb::redis::parser::InputBuffer : an owning circular (ring) buffer
 *     that accumulates inbound bytes across socket reads and handles data that
 *     wraps around the physical end of its storage.
 *   - @ref qb::redis::parser::ViewBuffer : a non-owning cursor over contiguous
 *     external memory, enabling zero-copy line/byte extraction.
 *
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2026 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)
 * @ingroup Redis
 */
#ifndef QBM_REDIS_PARSER_BUFFER_H
#define QBM_REDIS_PARSER_BUFFER_H

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace qb::redis::parser {

// ============================================================================
// Circular buffer optimized for streaming RESP data
// ============================================================================

/**
 * @class InputBuffer
 * @brief Owning circular (ring) buffer for streaming RESP data ingestion.
 *
 * Accumulates inbound bytes across multiple socket reads and exposes them for
 * parsing through @ref readable_span / @ref readable_span_second, @ref peek,
 * @ref find_crlf, @ref extract_line and @ref extract_bytes. Storage grows on
 * demand up to @ref MAX_CAPACITY and may be reclaimed via @ref compact.
 *
 * Because the storage is circular, logically contiguous data may physically
 * wrap around the end of the buffer; readers must therefore consult both
 * @ref readable_span and @ref readable_span_second. The implementation
 * deliberately keeps one sentinel slot free so a completely full buffer is
 * never mistaken for an empty one (see @ref ensure_space and @ref compact).
 *
 * The type is move-only (non-copyable, movable).
 */
class InputBuffer {
public:
    /// Default initial storage capacity, in bytes (16 KiB).
    static constexpr size_t DEFAULT_CAPACITY = 16 * 1024;
    /// Hard upper bound on storage capacity, in bytes (512 MiB).
    static constexpr size_t MAX_CAPACITY = 512 * 1024 * 1024;

    /**
     * @brief Construct a buffer with the given initial storage capacity.
     * @param initial_capacity Number of bytes to pre-allocate
     *                         (defaults to @ref DEFAULT_CAPACITY).
     */
    explicit InputBuffer(size_t initial_capacity = DEFAULT_CAPACITY)
        : _buffer(initial_capacity)
        , _read_pos(0)
        , _write_pos(0) {}

    InputBuffer(const InputBuffer &)            = delete;
    InputBuffer &operator=(const InputBuffer &) = delete;
    InputBuffer(InputBuffer &&)                 = default;
    InputBuffer &operator=(InputBuffer &&)      = default;

    /**
     * @brief Discard all buffered data without releasing storage.
     */
    void
    reset() noexcept {
        _read_pos  = 0;
        _write_pos = 0;
    }

    /**
     * @brief Total physical storage capacity, in bytes.
     * @return Size of the underlying storage.
     */
    [[nodiscard]] size_t
    capacity() const noexcept {
        return _buffer.size();
    }

    /**
     * @brief Number of bytes currently readable.
     * @return Count of unconsumed bytes, accounting for ring wrap-around.
     */
    [[nodiscard]] size_t
    size() const noexcept {
        if (_write_pos >= _read_pos) {
            return _write_pos - _read_pos;
        } else {
            return _buffer.size() - _read_pos + _write_pos;
        }
    }

    /**
     * @brief Whether the buffer holds no readable data.
     * @return @c true if there is nothing to read.
     */
    [[nodiscard]] bool
    empty() const noexcept {
        return _read_pos == _write_pos;
    }

    /**
     * @brief Free space available for appending without growing storage.
     * @return Number of bytes that can be appended before a resize is needed.
     */
    [[nodiscard]] size_t
    available() const noexcept {
        return capacity() - size();
    }

    /**
     * @brief Append a block of bytes, growing or compacting storage as needed.
     * @param data Bytes to copy into the buffer (may be empty).
     * @return @c true on success; @c false if the required capacity would
     *         exceed @ref MAX_CAPACITY.
     */
    bool
    append(std::span<const char> data) {
        if (data.empty())
            return true;

        if (data.size() > available()) {
            // Try to make room or grow
            if (!ensure_space(data.size())) {
                return false;
            }
        }

        // Write data using memcpy for efficiency
        const size_t cap   = _buffer.size();
        const size_t space = cap - _write_pos; // bytes until physical end

        if (data.size() <= space) {
            // Fits without wrapping
            std::memcpy(_buffer.data() + _write_pos, data.data(), data.size());
            _write_pos += data.size();
            // IMPORTANT: do NOT wrap `_write_pos` to 0 when it reaches `cap`.
            // A ring buffer where `_write_pos == _read_pos` is indistinguishable
            // from an empty one, so wrapping here makes a buffer that is
            // exactly full look empty to `size()` / `empty()` / `readable_span()`.
            // `ensure_space()` now reserves an extra sentinel slot so
            // `_write_pos < cap` after every successful append; we only wrap
            // below (split branch) when the data legitimately crosses the end.
        } else {
            // Split across the end of the physical buffer
            std::memcpy(_buffer.data() + _write_pos, data.data(), space);
            const size_t remaining = data.size() - space;
            std::memcpy(_buffer.data(), data.data() + space, remaining);
            _write_pos = remaining;
        }

        return true;
    }

    /**
     * @brief First contiguous run of readable bytes.
     *
     * When buffered data wraps around the physical end of storage, this returns
     * only the portion up to that end; call @ref readable_span_second for the
     * remainder.
     *
     * @return A view over the leading contiguous readable bytes (possibly empty).
     */
    [[nodiscard]] std::span<const char>
    readable_span() const noexcept {
        if (_read_pos <= _write_pos) {
            return std::span<const char>(_buffer.data() + _read_pos, _write_pos - _read_pos);
        } else {
            return std::span<const char>(_buffer.data() + _read_pos, _buffer.size() - _read_pos);
        }
    }

    /**
     * @brief Second contiguous run of readable bytes when data wraps.
     * @return The wrapped-around portion of readable data, or an empty span if
     *         the data is contiguous.
     */
    [[nodiscard]] std::span<const char>
    readable_span_second() const noexcept {
        if (_read_pos > _write_pos && _write_pos > 0) {
            return std::span<const char>(_buffer.data(), _write_pos);
        }
        return {};
    }

    /**
     * @brief Advance the read position, discarding consumed bytes.
     * @param bytes Number of bytes to consume; clamped to @ref size().
     */
    void
    consume(size_t bytes) noexcept {
        if (bytes == 0)
            return;

        size_t current_size = size();
        bytes               = std::min(bytes, current_size);

        _read_pos = (_read_pos + bytes) % _buffer.size();
    }

    /**
     * @brief Read a byte at the given offset without consuming it.
     * @param offset Distance from the current read position (default 0).
     * @return The byte, or @c std::nullopt if @p offset is out of range.
     */
    [[nodiscard]] std::optional<char>
    peek(size_t offset = 0) const noexcept {
        if (offset >= size())
            return std::nullopt;

        return _buffer[(_read_pos + offset) % _buffer.size()];
    }

    /**
     * @brief Locate the first CRLF (\\r\\n) sequence in the readable data.
     * @return Offset of the CR relative to the read position, or
     *         @c std::nullopt if no CRLF is present.
     */
    [[nodiscard]] std::optional<size_t>
    find_crlf() const noexcept {
        size_t sz = size();
        for (size_t i = 0; i < sz; ++i) {
            char c1 = _buffer[(_read_pos + i) % _buffer.size()];
            if (c1 == '\r' && i + 1 < sz) {
                char c2 = _buffer[(_read_pos + i + 1) % _buffer.size()];
                if (c2 == '\n') {
                    return i; // Position of CR
                }
            }
        }
        return std::nullopt;
    }

    /**
     * @brief Extract one CRLF-terminated line, consuming it and its CRLF.
     * @return The line contents without the trailing CRLF, or
     *         @c std::nullopt if no complete line is buffered yet.
     */
    [[nodiscard]] std::optional<std::string>
    extract_line() {
        auto crlf_pos = find_crlf();
        if (!crlf_pos)
            return std::nullopt;

        // Extract data excluding CRLF
        std::string result;
        result.reserve(*crlf_pos);

        for (size_t i = 0; i < *crlf_pos; ++i) {
            result.push_back(_buffer[(_read_pos + i) % _buffer.size()]);
        }

        // Consume line + CRLF
        consume(*crlf_pos + 2);

        return result;
    }

    /**
     * @brief Extract exactly @p n bytes, consuming them.
     * @param n Number of bytes to extract.
     * @return The extracted bytes, or @c std::nullopt if fewer than @p n bytes
     *         are buffered.
     */
    [[nodiscard]] std::optional<std::string>
    extract_bytes(size_t n) {
        if (n > size())
            return std::nullopt;

        std::string result;
        result.reserve(n);

        for (size_t i = 0; i < n; ++i) {
            result.push_back(_buffer[(_read_pos + i) % _buffer.size()]);
        }

        consume(n);
        return result;
    }

    /**
     * @brief Move buffered data to the front of storage and optionally shrink it.
     *
     * Relocates wrapped or offset data so it becomes contiguous from index 0,
     * then reclaims storage when the buffer is substantially over-allocated.
     *
     * Invariant maintained: @c _write_pos < @c _buffer.size() after this call.
     * (A circular buffer where @c _write_pos == @c _buffer.size() would make
     * @ref consume() wrap @c _read_pos back to 0 without reaching @c _write_pos,
     * causing @ref empty() to return false after consuming all data.)
     */
    void
    compact() {
        if (empty()) {
            reset();
            return;
        }

        if (_read_pos > _write_pos) {
            // Data is wrapped: [_read_pos..end] + [0.._write_pos)
            const size_t data_size = size();
            // Allocate at least data_size + 1 so _write_pos < _buffer.size()
            const size_t      new_cap = std::max(DEFAULT_CAPACITY, data_size + 1);
            std::vector<char> temp(new_cap);

            const size_t first_part = _buffer.size() - _read_pos;
            std::memcpy(temp.data(), _buffer.data() + _read_pos, first_part);
            std::memcpy(temp.data() + first_part, _buffer.data(), _write_pos);

            _buffer    = std::move(temp);
            _read_pos  = 0;
            _write_pos = data_size; // data_size < new_cap ✓
        } else if (_read_pos > 0) {
            // Contiguous data at [_read_pos.._write_pos)
            const size_t data_size = _write_pos - _read_pos;
            std::memmove(_buffer.data(), _buffer.data() + _read_pos, data_size);
            _write_pos = data_size;
            _read_pos  = 0;
        }

        // Optionally shrink an over-allocated buffer.
        // Use size() + 1 as the minimum to preserve the _write_pos < _buffer.size() invariant.
        if (_buffer.size() > DEFAULT_CAPACITY && _buffer.size() / 2 > size()) {
            const size_t target = std::max(DEFAULT_CAPACITY, size() + 1);
            _buffer.resize(target);
        }
    }

private:
    /**
     * @brief Ensure at least @p needed bytes of free space, compacting or
     *        growing storage as required.
     * @param needed Number of free bytes the caller needs.
     * @return @c true if the space is now available; @c false if satisfying the
     *         request would exceed @ref MAX_CAPACITY.
     */
    bool
    ensure_space(size_t needed) {
        size_t current_available = available();

        if (current_available >= needed) {
            return true;
        }

        // Compact first
        compact();

        if (available() >= needed) {
            return true;
        }

        // Need to grow. We reserve one extra sentinel byte so the ring invariant
        // `_write_pos != _read_pos when non-empty` can be preserved by append()
        // without wrapping `_write_pos` to zero — which would otherwise make a
        // completely full buffer indistinguishable from an empty one and
        // silently drop every subsequent byte.
        size_t new_capacity = _buffer.size();
        size_t required     = size() + needed + 1;

        while (new_capacity < required && new_capacity < MAX_CAPACITY) {
            new_capacity *= 2;
        }

        if (new_capacity < required) {
            return false; // Exceeds max capacity
        }

        // Grown by allocating a fresh vector and swapping it in, rather than by
        // _buffer.resize(new_capacity), and that is deliberate — do not "simplify" it
        // back. The two are equivalent (resize() keeps the existing bytes and
        // value-initialises the tail; so does a value-initialised vector plus a copy of
        // the old bytes), but GCC 14 at -O3 mis-attributes resize()'s appended range to
        // the OLD allocation. Inlining parse() -> RespParser ctor -> InputBuffer(16384)
        // -> feed -> append -> ensure_space makes the old size a compile-time 16384, and
        // GCC then reports the first appended element as
        //     error: array subscript 16384 is outside array bounds of 'char [16384]'
        //            [-Werror=array-bounds=]
        // against std::_Construct, naming the 16384-byte object the constructor
        // allocated. It is a false positive — resize() reallocates before it appends —
        // but QB_TESTS_WERROR defaults to QB_CI, so it is fatal on every runner and
        // invisible on the maintainer's clang. Guarding the resize() with
        // `if (new_capacity > _buffer.size())` was tried first and does NOT silence it:
        // GCC's complaint is about which allocation is written, not about a zero-length
        // append. Naming the destination separately removes the ambiguity.
        std::vector<char> grown(new_capacity);
        std::memcpy(grown.data(), _buffer.data(), _buffer.size());
        _buffer.swap(grown);
        return true;
    }

    std::vector<char> _buffer;
    size_t            _read_pos;
    size_t            _write_pos;
};

// ============================================================================
// View buffer for parsing from external memory (zero-copy where possible)
// ============================================================================

/**
 * @class ViewBuffer
 * @brief Non-owning cursor over contiguous data for zero-copy RESP parsing.
 *
 * Wraps an externally owned, contiguous byte range and tracks a read position
 * within it. Line and byte extraction return @c std::string_view aliases into
 * the underlying memory (no copy); convenience overloads producing owned
 * @c std::string copies are also provided.
 *
 * The caller must keep the referenced memory alive for the lifetime of any view
 * returned by this buffer.
 */
class ViewBuffer {
public:
    /**
     * @brief Construct a view over the given contiguous byte range.
     * @param data Externally owned bytes to parse; must outlive this buffer.
     */
    explicit ViewBuffer(std::span<const char> data) noexcept
        : _data(data)
        , _position(0) {}

    /**
     * @brief Rebind the view to a new byte range and rewind to its start.
     * @param data New externally owned bytes to parse.
     */
    void
    reset(std::span<const char> data) noexcept {
        _data     = data;
        _position = 0;
    }

    /**
     * @brief The full underlying byte range.
     * @return The complete span this view was constructed over.
     */
    [[nodiscard]] std::span<const char>
    data() const noexcept {
        return _data;
    }

    /**
     * @brief Total size of the underlying data, in bytes.
     * @return The size of the full span.
     */
    [[nodiscard]] size_t
    size() const noexcept {
        return _data.size();
    }

    /**
     * @brief Number of bytes left to read from the current position.
     * @return Count of unconsumed bytes.
     */
    [[nodiscard]] size_t
    remaining() const noexcept {
        return _data.size() - _position;
    }

    /**
     * @brief Whether the cursor has reached the end of the data.
     * @return @c true if no bytes remain.
     */
    [[nodiscard]] bool
    empty() const noexcept {
        return _position >= _data.size();
    }

    /**
     * @brief Current read position (offset from the start of the data).
     * @return The cursor offset.
     */
    [[nodiscard]] size_t
    position() const noexcept {
        return _position;
    }

    /**
     * @brief Advance the cursor by @p bytes.
     * @param bytes Number of bytes to skip; clamped to the end of the data.
     */
    void
    consume(size_t bytes) noexcept {
        _position = std::min(_position + bytes, _data.size());
    }

    /**
     * @brief Read a byte at the given offset from the cursor without consuming.
     * @param offset Distance from the current position (default 0).
     * @return The byte, or @c std::nullopt if @p offset is out of range.
     */
    // Bounds checks are written as `offset >= remaining` rather than
    // `_position + offset >= size` so a huge offset cannot overflow size_t and wrap
    // past the guard into an out-of-bounds index. `_position <= _data.size()` always
    // holds (consume() clamps, reset() zeroes), so `size - _position` never underflows.
    [[nodiscard]] std::optional<char>
    peek(size_t offset = 0) const noexcept {
        if (offset >= _data.size() - _position)
            return std::nullopt;
        return _data[_position + offset];
    }

    /**
     * @brief View of all remaining bytes from the current position.
     * @return A span over the unconsumed bytes.
     */
    [[nodiscard]] std::span<const char>
    current() const noexcept {
        return _data.subspan(_position);
    }

    /**
     * @brief View of the next @p n bytes without consuming them.
     * @param n Number of bytes to view.
     * @return A span over the next @p n bytes, or an empty span if fewer than
     *         @p n bytes remain.
     */
    // Overflow-safe bound: see peek().
    [[nodiscard]] std::span<const char>
    get(size_t n) const noexcept {
        if (n > _data.size() - _position) {
            return {};
        }
        return _data.subspan(_position, n);
    }

    /**
     * @brief Locate the first CRLF (\\r\\n) at or after the current position.
     * @return Offset of the CR relative to the current position, or
     *         @c std::nullopt if no CRLF is found.
     */
    [[nodiscard]] std::optional<size_t>
    find_crlf() const noexcept {
        for (size_t i = _position; i + 1 < _data.size(); ++i) {
            if (_data[i] == '\r' && _data[i + 1] == '\n') {
                return i - _position; // Offset from current position
            }
        }
        return std::nullopt;
    }

    /**
     * @brief Extract one CRLF-terminated line as a zero-copy view.
     *
     * On success the cursor is advanced past the line and its CRLF.
     *
     * @return A view of the line contents without the trailing CRLF, or
     *         @c std::nullopt if no complete line is available. The view aliases
     *         the underlying data and is valid only while that data lives.
     */
    [[nodiscard]] std::optional<std::string_view>
    extract_line_view() {
        auto crlf_pos = find_crlf();
        if (!crlf_pos)
            return std::nullopt;

        auto result = _data.subspan(_position, *crlf_pos);
        consume(*crlf_pos + 2); // Skip CRLF

        return std::string_view(result.data(), result.size());
    }

    /**
     * @brief Extract one CRLF-terminated line as an owned copy.
     * @return The line contents without the trailing CRLF, or
     *         @c std::nullopt if no complete line is available.
     */
    [[nodiscard]] std::optional<std::string>
    extract_line() {
        auto view = extract_line_view();
        if (!view)
            return std::nullopt;
        return std::string(*view);
    }

    /**
     * @brief Extract exactly @p n bytes as a zero-copy view, advancing the cursor.
     * @param n Number of bytes to extract.
     * @return A view of the @p n bytes, or @c std::nullopt if fewer than @p n
     *         bytes remain. The view aliases the underlying data and is valid
     *         only while that data lives.
     */
    // Overflow-safe bound: see peek().
    [[nodiscard]] std::optional<std::string_view>
    extract_bytes_view(size_t n) {
        if (n > _data.size() - _position)
            return std::nullopt;

        auto result = _data.subspan(_position, n);
        consume(n);
        return std::string_view(result.data(), result.size());
    }

    /**
     * @brief Extract exactly @p n bytes as an owned copy, advancing the cursor.
     * @param n Number of bytes to extract.
     * @return The @p n bytes, or @c std::nullopt if fewer than @p n bytes remain.
     */
    [[nodiscard]] std::optional<std::string>
    extract_bytes(size_t n) {
        auto view = extract_bytes_view(n);
        if (!view)
            return std::nullopt;
        return std::string(*view);
    }

    /**
     * @brief Consume a CRLF (\\r\\n) at the current position if present.
     * @return @c true if a CRLF was found and consumed; @c false otherwise
     *         (the cursor is left unchanged on failure).
     */
    [[nodiscard]] bool
    skip_crlf() {
        if (_position + 2 > _data.size())
            return false;
        if (_data[_position] != '\r' || _data[_position + 1] != '\n')
            return false;
        consume(2);
        return true;
    }

private:
    std::span<const char> _data;
    size_t                _position;
};

} // namespace qb::redis::parser

#endif // QBM_REDIS_PARSER_BUFFER_H
