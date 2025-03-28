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

#ifndef QBM_REDIS_BITMAP_COMMANDS_H
#define QBM_REDIS_BITMAP_COMMANDS_H

#include "reply.h"

namespace qb::redis {

/**
 * @class bitmap_commands
 * @brief Provides Redis bitmap command implementations.
 *
 * This class implements Redis bitmap operations, which provide a way to manipulate
 * string values as arrays of bits. Each command has both synchronous and asynchronous versions.
 *
 * Redis bitmaps are implemented as strings, where each byte represents 8 bits.
 * They are very space efficient and provide fast operations for counting bits,
 * finding bit positions, and performing bitwise operations.
 *
 * @tparam Derived The derived class type (CRTP pattern)
 */
template <typename Derived>
class bitmap_commands {
private:
    constexpr Derived &
    derived() {
        return static_cast<Derived &>(*this);
    }
public:
    /**
     * @brief Count the number of set bits (population counting) in a string.
     * 
     * By default all the bytes contained in the string are examined. It is possible
     * to specify the counting operation only in an interval passing the additional
     * arguments start and end.
     *
     * @param key The key storing the string value
     * @param start Start offset (inclusive), 0-based. Negative values count from the end of the string
     * @param end End offset (inclusive). Negative values count from the end of the string
     * @return The number of bits set to 1
     * @note If start and end are not specified, the entire string is examined
     * @note Time complexity: O(N) where N is the number of bytes examined
     * @see https://redis.io/commands/bitcount
     */
    long long
    bitcount(const std::string &key, long long start = 0, long long end = -1) {
        if (key.empty()) {
            return 0;
        }
        return derived()
            .template command<long long>("BITCOUNT", key, start, end)
            .result();
    }

    /**
     * @brief Asynchronous version of the BITCOUNT command.
     *
     * @param func Callback function to handle the result
     * @param key The key storing the string value
     * @param start Start offset (inclusive), 0-based. Negative values count from the end of the string
     * @param end End offset (inclusive). Negative values count from the end of the string
     * @return Reference to the derived class
     * @see https://redis.io/commands/bitcount
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    bitcount(Func &&func, const std::string &key, long long start = 0, long long end = -1) {
        return derived()
            .template command<long long>(std::forward<Func>(func), "BITCOUNT", key, start, end);
    }

    /**
     * @brief Perform arbitrary bitfield integer operations on strings.
     * 
     * The command treats a Redis string as an array of bits, and is capable of
     * addressing specific integer fields of varying bit widths and arbitrary
     * non (necessarily) aligned offset.
     *
     * @param key The key storing the string value
     * @param operations Vector of operations to perform
     * @return Vector of results for each operation
     * @note Time complexity: O(1) for each operation
     * @see https://redis.io/commands/bitfield
     */
    std::vector<std::optional<long long>>
    bitfield(const std::string &key, const std::vector<std::string> &operations) {
        if (key.empty() || operations.empty()) {
            return {};
        }
        return derived()
            .template command<std::vector<std::optional<long long>>>("BITFIELD", key, operations)
            .result();
    }

    /**
     * @brief Asynchronous version of the BITFIELD command.
     *
     * @param func Callback function to handle the result
     * @param key The key storing the string value
     * @param operations Vector of operations to perform
     * @return Reference to the derived class
     * @see https://redis.io/commands/bitfield
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::optional<long long>>> &&>, Derived &>
    bitfield(Func &&func, const std::string &key, const std::vector<std::string> &operations) {
        return derived()
            .template command<std::vector<std::optional<long long>>>(std::forward<Func>(func), "BITFIELD", key, operations);
    }

    /**
     * @brief Perform bitwise operations between strings.
     * 
     * Performs a bitwise operation between multiple keys (containing string values)
     * and stores the result in the destination key.
     *
     * @param operation The bitwise operation to perform (AND, OR, XOR, NOT)
     * @param destkey The key to store the result
     * @param keys Vector of keys to perform the operation on
     * @return The size of the string stored in the destination key
     * @note Time complexity: O(N) where N is the size of the longest string
     * @see https://redis.io/commands/bitop
     */
    long long
    bitop(const std::string &operation, const std::string &destkey, const std::vector<std::string> &keys) {
        if (destkey.empty() || keys.empty()) {
            return 0;
        }
        return derived()
            .template command<long long>("BITOP", operation, destkey, keys)
            .result();
    }

    /**
     * @brief Asynchronous version of the BITOP command.
     *
     * @param func Callback function to handle the result
     * @param operation The bitwise operation to perform (AND, OR, XOR, NOT)
     * @param destkey The key to store the result
     * @param keys Vector of keys to perform the operation on
     * @return Reference to the derived class
     * @see https://redis.io/commands/bitop
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    bitop(Func &&func, const std::string &operation, const std::string &destkey, const std::vector<std::string> &keys) {
        return derived()
            .template command<long long>(std::forward<Func>(func), "BITOP", operation, destkey, keys);
    }

    /**
     * @brief Find first bit set or cleared in a string.
     * 
     * Returns the position of the first bit set to 1 or 0 in a string.
     *
     * @param key The key storing the string value
     * @param bit The bit value to search for (0 or 1)
     * @param start Start offset (inclusive), 0-based. Negative values count from the end of the string
     * @param end End offset (inclusive). Negative values count from the end of the string
     * @return The position of the first bit set to the specified value
     * @note Returns -1 if no bit is found
     * @note Time complexity: O(N) where N is the number of bytes examined
     * @see https://redis.io/commands/bitpos
     */
    long long
    bitpos(const std::string &key, bool bit, long long start = 0, long long end = -1) {
        if (key.empty()) {
            return -1;
        }
        return derived()
            .template command<long long>("BITPOS", key, bit ? 1 : 0, start, end)
            .result();
    }

    /**
     * @brief Asynchronous version of the BITPOS command.
     *
     * @param func Callback function to handle the result
     * @param key The key storing the string value
     * @param bit The bit value to search for (0 or 1)
     * @param start Start offset (inclusive), 0-based. Negative values count from the end of the string
     * @param end End offset (inclusive). Negative values count from the end of the string
     * @return Reference to the derived class
     * @see https://redis.io/commands/bitpos
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    bitpos(Func &&func, const std::string &key, bool bit, long long start = 0, long long end = -1) {
        return derived()
            .template command<long long>(std::forward<Func>(func), "BITPOS", key, bit ? 1 : 0, start, end);
    }

    /**
     * @brief Get the value of a bit at offset in the string value stored at key.
     * 
     * Returns the bit value at offset in the string value stored at key.
     *
     * @param key The key storing the string value
     * @param offset The bit offset
     * @return The bit value (0 or 1)
     * @note Time complexity: O(1)
     * @see https://redis.io/commands/getbit
     */
    bool
    getbit(const std::string &key, long long offset) {
        if (key.empty()) {
            return false;
        }
        return derived()
            .template command<long long>("GETBIT", key, offset)
            .result() == 1;
    }

    /**
     * @brief Asynchronous version of the GETBIT command.
     *
     * @param func Callback function to handle the result
     * @param key The key storing the string value
     * @param offset The bit offset
     * @return Reference to the derived class
     * @see https://redis.io/commands/getbit
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    getbit(Func &&func, const std::string &key, long long offset) {
        return derived()
            .template command<long long>(std::forward<Func>(func), "GETBIT", key, offset);
    }

    /**
     * @brief Set or clear the bit at offset in the string value stored at key.
     * 
     * Sets or clears the bit at offset in the string value stored at key.
     *
     * @param key The key storing the string value
     * @param offset The bit offset
     * @param value The bit value to set (0 or 1)
     * @return The original bit value stored at offset
     * @note Time complexity: O(1)
     * @see https://redis.io/commands/setbit
     */
    bool
    setbit(const std::string &key, long long offset, bool value) {
        if (key.empty()) {
            return false;
        }
        return derived()
            .template command<long long>("SETBIT", key, offset, static_cast<int>(value))
            .result() == 1;
    }

    /**
     * @brief Asynchronous version of the SETBIT command.
     *
     * @param func Callback function to handle the result
     * @param key The key storing the string value
     * @param offset The bit offset
     * @param value The bit value to set (0 or 1)
     * @return Reference to the derived class
     * @see https://redis.io/commands/setbit
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    setbit(Func &&func, const std::string &key, long long offset, bool value) {
        return derived()
            .template command<long long>(std::forward<Func>(func), "SETBIT", key, offset, static_cast<int>(value));
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_BITMAP_COMMANDS_H 