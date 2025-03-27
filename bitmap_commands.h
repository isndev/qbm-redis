/*
 * qb - C++ Actor Framework
 * Copyright (C) 2011-2021 isndev (www.qbaf.io). All rights reserved.
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

#ifndef QBM_REDIS_BITMAP_COMMANDS_H
#define QBM_REDIS_BITMAP_COMMANDS_H

#include "string_commands.h"

namespace qb::redis {

/**
 * @class bitmap_commands
 * @brief Provides Redis bitmap command implementations for bit manipulation operations.
 *
 * This class implements Redis bitmap commands for working with bit operations on strings.
 * Bitmaps in Redis are not a dedicated data type but a set of bit-oriented operations
 * defined on the String type. Since strings are binary safe and their maximum length is
 * 512 MB, they are suitable for setting up to 2^32 different bits.
 * 
 * Bitmaps are particularly useful for:
 * - Efficiently storing boolean information (presence/absence) for large sets
 * - Performing fast bit-level operations like AND, OR, XOR between multiple bitmaps
 * - Tracking user activities, feature flags, or other binary state data
 *
 * Each command has both synchronous and asynchronous versions.
 *
 * @tparam Derived The derived class type (CRTP pattern)
 */
template<typename Derived>
class bitmap_commands {
public:
    /**
     * @brief Count the number of set bits (population counting) in a string.
     * 
     * Gets the number of bits set to 1 in the specified key within an optional range.
     * This is useful for calculating the cardinality of a set represented by a bitmap.
     *
     * @param key The key storing the string (bitmap) value
     * @param start Start index (inclusive) of the range, 0-based. Default is 0 (beginning of string)
     * @param end End index (inclusive) of the range. Default is -1 (end of string)
     * @return Number of bits that have been set to 1 in the specified range
     * @note Indices can be negative to count from the end of the string (-1 is the last byte)
     * @note Time complexity: O(N) where N is the size of the string
     * @see https://redis.io/commands/bitcount
     */
    long long
    bitcount(const std::string &key, long long start = 0, long long end = -1) {
        return static_cast<Derived &>(*this).template command<long long>("BITCOUNT", key, start, end).result;
    }

    /**
     * @brief Asynchronous version of the BITCOUNT command.
     * 
     * @param func Callback function to be invoked when the operation completes
     * @param key The key storing the string value
     * @param start Start index (inclusive) of the range, 0-based
     * @param end End index (inclusive) of the range
     * @return Reference to the derived Redis client for method chaining
     * @see https://redis.io/commands/bitcount
     */
    template<typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    bitcount(Func &&func, const std::string &key, long long start = 0, long long end = -1) {
        return static_cast<Derived &>(*this)
                .template command<long long>(std::forward<Func>(func), "BITCOUNT", key, start, end);
    }

    /**
     * @brief Perform bitwise operations between strings and store the result.
     * 
     * Performs a bitwise operation between multiple keys (containing string values) 
     * and stores the result in the destination key. The supported operations are:
     * AND, OR, XOR, and NOT. The NOT operation only accepts one input key.
     *
     * @param op Bit operation type (AND, OR, XOR, NOT)
     * @param destination The destination key where the result will be stored
     * @param keys Source keys containing the strings to be operated on (variadic)
     * @return The length of the string stored at the destination key
     * @note If a source key does not exist or contains a non-string value, it is 
     *       treated as a zero string (all bits set to 0)
     * @note Time complexity: O(N) where N is the size of the longest string
     * @see https://redis.io/commands/bitop
     * @see `BitOp` enum for available operations
     */
    template<typename... Keys>
    long long
    bitop(BitOp op, const std::string &destination, Keys &&...keys) {
        return static_cast<Derived &>(*this)
                .template command<long long>("BITOP", std::to_string(op), destination, std::forward<Keys>(keys)...)
                .result;
    }

    /**
     * @brief Asynchronous version of the BITOP command.
     *
     * @param func Callback function to be invoked when the operation completes
     * @param op Bit operation type (AND, OR, XOR, NOT)
     * @param destination The destination key where the result will be stored
     * @param keys Source keys containing the strings to be operated on (variadic)
     * @return Reference to the derived Redis client for method chaining
     * @see https://redis.io/commands/bitop
     */
    template<typename Func, typename... Keys>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    bitop(Func &&func, BitOp op, const std::string &destination, Keys &&...keys) {
        return static_cast<Derived &>(*this).template command<long long>(
                std::forward<Func>(func),
                "BITOP",
                std::to_string(op),
                destination,
                std::forward<Keys>(keys)...);
    }

    /**
     * @brief Find the position of the first bit set to a specific value.
     * 
     * Returns the position of the first bit set to the specified value (0 or 1)
     * in the bitmap stored at the given key. The search can be limited to a specific
     * range by providing the start and end byte positions.
     *
     * @param key The key storing the string (bitmap) value
     * @param bit The bit value to search for (0 or 1)
     * @param start Start index (inclusive) of the range in bytes. Default is 0 (beginning of string)
     * @param end End index (inclusive) of the range in bytes. Default is -1 (end of string)
     * @return The position of the first bit set to the given value
     * @note If the bit value is not found, returns -1 when searching for 1, or the size
     *       of the string (in bits) when searching for 0
     * @note Time complexity: O(N) where N is the size of the string
     * @see https://redis.io/commands/bitpos
     */
    long long
    bitpos(const std::string &key, long long bit, long long start = 0, long long end = -1) {
        return static_cast<Derived &>(*this).template command<long long>("BITPOS", key, bit, start, end).result;
    }

    /**
     * @brief Asynchronous version of the BITPOS command.
     *
     * @param func Callback function to be invoked when the operation completes
     * @param key The key storing the string value
     * @param bit The bit value to search for (0 or 1)
     * @param start Start index (inclusive) of the range in bytes
     * @param end End index (inclusive) of the range in bytes
     * @return Reference to the derived Redis client for method chaining
     * @see https://redis.io/commands/bitpos
     */
    template<typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    bitpos(Func &&func, const std::string &key, long long bit, long long start = 0, long long end = -1) {
        return static_cast<Derived &>(*this)
                .template command<long long>(std::forward<Func>(func), "BITPOS", key, bit, start, end);
    }

    /**
     * @brief Get the bit value at a specific offset in the string.
     * 
     * Returns the bit value at the specified offset in the string stored at the given key.
     * If the key does not exist or the offset is beyond the string length, 0 is returned.
     *
     * @param key The key storing the string (bitmap) value
     * @param offset The bit offset (0-based)
     * @return The bit value at the specified offset (0 or 1)
     * @note Offsets are always from the start of the string, starting at 0
     * @note Time complexity: O(1)
     * @see https://redis.io/commands/getbit
     */
    long long
    getbit(const std::string &key, long long offset) {
        return static_cast<Derived &>(*this).template command<long long>("GETBIT", key, offset).result;
    }

    /**
     * @brief Asynchronous version of the GETBIT command.
     *
     * @param func Callback function to be invoked when the operation completes
     * @param key The key storing the string value
     * @param offset The bit offset (0-based)
     * @return Reference to the derived Redis client for method chaining
     * @see https://redis.io/commands/getbit
     */
    template<typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    getbit(Func &&func, const std::string &key, long long offset) {
        return static_cast<Derived &>(*this)
                .template command<long long>(std::forward<Func>(func), "GETBIT", key, offset);
    }

    /**
     * @brief Set or clear the bit at a specific offset in the string.
     * 
     * Sets or clears the bit at the specified offset in the string stored at the given key.
     * If the key does not exist, a new string value is created. The string is grown to make 
     * sure it can accommodate the specified offset.
     *
     * @param key The key storing the string (bitmap) value
     * @param offset The bit offset (0-based)
     * @param value The bit value to set (0 or 1)
     * @return The original bit value stored at the offset before the operation
     * @note Time complexity: O(1)
     * @see https://redis.io/commands/setbit
     */
    long long
    setbit(const std::string &key, long long offset, long long value) {
        return static_cast<Derived &>(*this).template command<long long>("SETBIT", key, offset, value).result;
    }

    /**
     * @brief Asynchronous version of the SETBIT command.
     *
     * @param func Callback function to be invoked when the operation completes
     * @param key The key storing the string value
     * @param offset The bit offset (0-based)
     * @param value The bit value to set (0 or 1)
     * @return Reference to the derived Redis client for method chaining
     * @see https://redis.io/commands/setbit
     */
    template<typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    setbit(Func &&func, const std::string &key, long long offset, long long value) {
        return static_cast<Derived &>(*this)
                .template command<long long>(std::forward<Func>(func), "SETBIT", key, offset, value);
    }

    /**
     * @brief Set multiple bits from a bitset at once, starting from a specific offset.
     * 
     * Sets multiple bits in the string stored at the given key using a std::bitset.
     * The bits are set starting from the specified offset.
     *
     * @tparam NB_BITS Size of the bitset
     * @param key The key storing the string (bitmap) value
     * @param offset The starting bit offset (0-based)
     * @param bits The bitset containing the bits to set
     * @return The number of bits that were changed (from 0 to 1)
     * @note This method performs multiple SETBIT operations
     * @note Time complexity: O(NB_BITS)
     */
    template<std::size_t NB_BITS>
    long long
    setbit(const std::string &key, long long offset, std::bitset<NB_BITS> const &bits) {
        auto ret = 0ll;
        for (auto i = 0; i < NB_BITS; ++i) {
            if (setbit(key, offset++, bits[i]))
                ret++;
        }
        return ret;
    }

    /**
     * @brief Asynchronous version of the multiple-bit SETBIT command.
     *
     * @tparam NB_BITS Size of the bitset
     * @param func Callback function to be invoked when all operations complete
     * @param key The key storing the string value
     * @param offset The starting bit offset (0-based)
     * @param bits The bitset containing the bits to set
     * @return Reference to the derived Redis client for method chaining
     */
    template<typename Func, std::size_t NB_BITS>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    setbit(Func &&func, const std::string &key, long long offset, std::bitset<NB_BITS> const &bits) {
        auto ret = new long long{0};
        for (auto i = 0; i < NB_BITS; ++i) {
            setbit(
                    [ret](auto &&r) {
                        if (r.ok)
                            ++(*ret);
                    },
                    key,
                    offset++,
                    bits[i]);
        }
        return static_cast<Derived &>(*this).ping([ret, func = std::forward<Func>(func)](auto &&) {
            func(redis::Reply<long long>{*ret == NB_BITS, *ret, nullptr});
            delete ret;
        });
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_BITMAP_COMMANDS_H 