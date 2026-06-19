/**
 * @file parser/buffer.h
 * @brief InputBuffer and ViewBuffer for streaming RESP parsing
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

#ifndef QBM_REDIS_PARSER_BUFFER_H
#define QBM_REDIS_PARSER_BUFFER_H

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <span>
#include <vector>

namespace qb::redis::parser {

// ============================================================================
// Circular buffer optimized for streaming RESP data
// ============================================================================

/**
 * @class InputBuffer
 * @brief Circular buffer for streaming RESP data ingestion
 *
 * Supports append, consume, peek, extract_line, extract_bytes.
 * Handles wrapped data across buffer boundaries.
 */
class InputBuffer {
public:
    static constexpr size_t DEFAULT_CAPACITY = 16 * 1024;         // 16KB
    static constexpr size_t MAX_CAPACITY     = 512 * 1024 * 1024; // 512MB

    explicit InputBuffer(size_t initial_capacity = DEFAULT_CAPACITY)
        : _buffer(initial_capacity)
        , _read_pos(0)
        , _write_pos(0) {}

    // Non-copyable but movable
    InputBuffer(const InputBuffer &)            = delete;
    InputBuffer &operator=(const InputBuffer &) = delete;
    InputBuffer(InputBuffer &&)                 = default;
    InputBuffer &operator=(InputBuffer &&)      = default;

    // Reset buffer state
    void
    reset() noexcept {
        _read_pos  = 0;
        _write_pos = 0;
    }

    // Capacity management
    [[nodiscard]] size_t
    capacity() const noexcept {
        return _buffer.size();
    }

    [[nodiscard]] size_t
    size() const noexcept {
        if (_write_pos >= _read_pos) {
            return _write_pos - _read_pos;
        } else {
            return _buffer.size() - _read_pos + _write_pos;
        }
    }

    [[nodiscard]] bool
    empty() const noexcept {
        return _read_pos == _write_pos;
    }

    [[nodiscard]] size_t
    available() const noexcept {
        return capacity() - size();
    }

    // Append data to buffer
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

    // Get contiguous readable data (may need to call twice for wrapped data)
    [[nodiscard]] std::span<const char>
    readable_span() const noexcept {
        if (_read_pos <= _write_pos) {
            return std::span<const char>(_buffer.data() + _read_pos, _write_pos - _read_pos);
        } else {
            return std::span<const char>(_buffer.data() + _read_pos, _buffer.size() - _read_pos);
        }
    }

    // Get second span if data wraps around
    [[nodiscard]] std::span<const char>
    readable_span_second() const noexcept {
        if (_read_pos > _write_pos && _write_pos > 0) {
            return std::span<const char>(_buffer.data(), _write_pos);
        }
        return {};
    }

    // Advance read position
    void
    consume(size_t bytes) noexcept {
        if (bytes == 0)
            return;

        size_t current_size = size();
        bytes               = std::min(bytes, current_size);

        _read_pos = (_read_pos + bytes) % _buffer.size();
    }

    // Peek at byte without consuming
    [[nodiscard]] std::optional<char>
    peek(size_t offset = 0) const noexcept {
        if (offset >= size())
            return std::nullopt;

        return _buffer[(_read_pos + offset) % _buffer.size()];
    }

    // Find CRLF in buffer
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

    // Extract a line (up to and including CRLF)
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

    // Extract N bytes
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

    // Compact buffer - move data to the front and optionally shrink.
    //
    // Invariant maintained: _write_pos < _buffer.size() after this call.
    // (A circular buffer where _write_pos == _buffer.size() would make
    // consume() wrap _read_pos back to 0 without reaching _write_pos,
    // causing empty() to return false after consuming all data.)
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

        _buffer.resize(new_capacity);
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
 * @brief Non-owning view over contiguous data for zero-copy parsing
 *
 * Tracks read position, supports extract_line_view, extract_bytes_view,
 * skip_crlf. Does not copy data.
 */
class ViewBuffer {
public:
    explicit ViewBuffer(std::span<const char> data) noexcept
        : _data(data)
        , _position(0) {}

    // Reset view to new data
    void
    reset(std::span<const char> data) noexcept {
        _data     = data;
        _position = 0;
    }

    // Access
    [[nodiscard]] std::span<const char>
    data() const noexcept {
        return _data;
    }

    [[nodiscard]] size_t
    size() const noexcept {
        return _data.size();
    }

    [[nodiscard]] size_t
    remaining() const noexcept {
        return _data.size() - _position;
    }

    [[nodiscard]] bool
    empty() const noexcept {
        return _position >= _data.size();
    }

    [[nodiscard]] size_t
    position() const noexcept {
        return _position;
    }

    // Advance position
    void
    consume(size_t bytes) noexcept {
        _position = std::min(_position + bytes, _data.size());
    }

    // Peek at current byte.
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

    // Get current span
    [[nodiscard]] std::span<const char>
    current() const noexcept {
        return _data.subspan(_position);
    }

    // Get span of N bytes (overflow-safe bound: see peek()).
    [[nodiscard]] std::span<const char>
    get(size_t n) const noexcept {
        if (n > _data.size() - _position) {
            return {};
        }
        return _data.subspan(_position, n);
    }

    // Find CRLF from current position
    [[nodiscard]] std::optional<size_t>
    find_crlf() const noexcept {
        for (size_t i = _position; i + 1 < _data.size(); ++i) {
            if (_data[i] == '\r' && _data[i + 1] == '\n') {
                return i - _position; // Offset from current position
            }
        }
        return std::nullopt;
    }

    // Extract string up to CRLF (excluding CRLF)
    [[nodiscard]] std::optional<std::string_view>
    extract_line_view() {
        auto crlf_pos = find_crlf();
        if (!crlf_pos)
            return std::nullopt;

        auto result = _data.subspan(_position, *crlf_pos);
        consume(*crlf_pos + 2); // Skip CRLF

        return std::string_view(result.data(), result.size());
    }

    // Extract string (creates copy)
    [[nodiscard]] std::optional<std::string>
    extract_line() {
        auto view = extract_line_view();
        if (!view)
            return std::nullopt;
        return std::string(*view);
    }

    // Extract N bytes as view (overflow-safe bound: see peek()).
    [[nodiscard]] std::optional<std::string_view>
    extract_bytes_view(size_t n) {
        if (n > _data.size() - _position)
            return std::nullopt;

        auto result = _data.subspan(_position, n);
        consume(n);
        return std::string_view(result.data(), result.size());
    }

    // Extract N bytes as string
    [[nodiscard]] std::optional<std::string>
    extract_bytes(size_t n) {
        auto view = extract_bytes_view(n);
        if (!view)
            return std::nullopt;
        return std::string(*view);
    }

    // Skip expected CRLF
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
