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

#ifndef QBM_REDIS_SET_COMMANDS_H
#define QBM_REDIS_SET_COMMANDS_H
#include "reply.h"

namespace qb::redis {

/**
 * @class set_commands
 * @brief Provides Redis set command implementations.
 *
 * This class implements Redis set operations, which provide an unordered collection
 * of unique strings. Each command has both synchronous and asynchronous versions.
 *
 * Redis sets are particularly useful for expressing relations between objects and
 * for quickly checking membership of elements.
 *
 * @tparam Derived The derived class type (CRTP pattern)
 */
template <typename Derived>
class set_commands {
    /**
     * @class scanner
     * @brief Helper class for implementing incremental scanning of sets
     * 
     * @tparam Func Callback function type
     */
    template <typename Func>
    class scanner {
        Derived &_handler;
        std::string _key;
        std::string _pattern;
        Func _func;
        qb::redis::Reply<qb::redis::reply::scan<>> _reply;

    public:
        /**
         * @brief Constructs a scanner for set elements
         * 
         * @param handler The Redis handler
         * @param key Key where the set is stored
         * @param pattern Pattern to filter set members
         * @param func Callback function to process results
         */
        scanner(Derived &handler, std::string key, std::string pattern, Func &&func)
            : _handler(handler)
            , _key(std::move(key))
            , _pattern(std::move(pattern))
            , _func(std::forward<Func>(func)) {
            _handler.sscan(std::ref(*this), _key, 0, _pattern, 100);
        }

        /**
         * @brief Processes scan results and continues scanning if needed
         * 
         * @param reply The scan operation reply
         */
        void
        operator()(qb::redis::Reply<qb::redis::reply::scan<>> &&reply) {
            _reply.ok = reply.ok;
            std::move(reply.result.items.begin(), reply.result.items.end(), std::back_inserter(_reply.result.items));
            if (reply.ok && reply.result.cursor)
                _handler.sscan(std::ref(*this), _key, reply.result.cursor, _pattern, 100);
            else {
                _func(std::move(_reply));
                delete this;
            }
        }
    };

public:
    /**
     * @brief Adds members to a set
     * 
     * @tparam Members Variadic types for set members
     * @param key Key where the set is stored
     * @param members Members to add to the set
     * @return Number of members that were added to the set
     */
    template <typename... Members>
    long long
    sadd(const std::string &key, Members &&...members) {
        return static_cast<Derived &>(*this)
            .template command<long long>("SADD", key, std::forward<Members>(members)...)
            .result;
    }

    /**
     * @brief Asynchronous version of sadd
     * 
     * @tparam Func Callback function type
     * @tparam Members Variadic types for set members
     * @param func Callback function
     * @param key Key where the set is stored
     * @param members Members to add to the set
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func, typename... Members>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    sadd(Func &&func, const std::string &key, Members &&...members) {
        return static_cast<Derived &>(*this)
            .template command<long long>(std::forward<Func>(func), "SADD", key, std::forward<Members>(members)...);
    }

    /**
     * @brief Gets the number of members in a set
     * 
     * @param key Key where the set is stored
     * @return Number of members in the set
     */
    long long
    scard(const std::string &key) {
        return static_cast<Derived &>(*this).template command<long long>("SCARD", key).result;
    }

    /**
     * @brief Asynchronous version of scard
     * 
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the set is stored
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    Derived &
    scard(Func &&func, const std::string &key) {
        return static_cast<Derived &>(*this).template command<long long>(std::forward<Func>(func), "SCARD", key);
    }

    /**
     * @brief Subtracts multiple sets
     * 
     * @tparam Keys Variadic types for key names
     * @param keys Keys where the sets are stored
     * @return Members of the resulting set (difference between first set and all others)
     */
    template <typename... Keys>
    std::vector<std::string>
    sdiff(Keys &&...keys) {
        return static_cast<Derived &>(*this)
            .template command<std::vector<std::string>>("SDIFF", std::forward<Keys>(keys)...)
            .result;
    }

    /**
     * @brief Asynchronous version of sdiff
     * 
     * @tparam Func Callback function type
     * @tparam Keys Variadic types for key names
     * @param func Callback function
     * @param keys Keys where the sets are stored
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func, typename... Keys>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>, Derived &>
    sdiff(Func &&func, Keys &&...keys) {
        return static_cast<Derived &>(*this).template command<std::vector<std::string>>(
            std::forward<Func>(func),
            "SDIFF",
            std::forward<Keys>(keys)...);
    }

    /**
     * @brief Subtracts multiple sets and stores the result in a key
     * 
     * @tparam Keys Variadic types for source key names
     * @param destination Destination key where the resulting set will be stored
     * @param keys Source keys where the sets are stored
     * @return Number of members in the resulting set
     */
    template <typename... Keys>
    long long
    sdiffstore(const std::string &destination, Keys &&...keys) {
        return static_cast<Derived &>(*this)
            .template command<long long>("SDIFFSTORE", destination, std::forward<Keys>(keys)...)
            .result;
    }

    /**
     * @brief Asynchronous version of sdiffstore
     * 
     * @tparam Func Callback function type
     * @tparam Keys Variadic types for source key names
     * @param func Callback function
     * @param destination Destination key where the resulting set will be stored
     * @param keys Source keys where the sets are stored
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func, typename... Keys>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    sdiffstore(Func &&func, const std::string &destination, Keys &&...keys) {
        return static_cast<Derived &>(*this).template command<long long>(
            std::forward<Func>(func),
            "SDIFFSTORE",
            destination,
            std::forward<Keys>(keys)...);
    }

    /**
     * @brief Intersects multiple sets
     * 
     * @tparam Keys Variadic types for key names
     * @param keys Keys where the sets are stored
     * @return Members of the resulting set (intersection of all sets)
     */
    template <typename... Keys>
    std::vector<std::string>
    sinter(Keys &&...keys) {
        return static_cast<Derived &>(*this)
            .template command<std::vector<std::string>>("SINTER", std::forward<Keys>(keys)...)
            .result;
    }

    /**
     * @brief Asynchronous version of sinter
     * 
     * @tparam Func Callback function type
     * @tparam Keys Variadic types for key names
     * @param func Callback function
     * @param keys Keys where the sets are stored
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func, typename... Keys>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>, Derived &>
    sinter(Func &&func, Keys &&...keys) {
        return static_cast<Derived &>(*this).template command<std::vector<std::string>>(
            std::forward<Func>(func),
            "SINTER",
            std::forward<Keys>(keys)...);
    }

    /**
     * @brief Intersects multiple sets and stores the result in a key
     * 
     * @tparam Keys Variadic types for source key names
     * @param destination Destination key where the resulting set will be stored
     * @param keys Source keys where the sets are stored
     * @return Number of members in the resulting set
     */
    template <typename... Keys>
    long long
    sinterstore(const std::string &destination, Keys &&...keys) {
        return static_cast<Derived &>(*this)
            .template command<long long>("SINTERSTORE", destination, std::forward<Keys>(keys)...)
            .result;
    }

    /**
     * @brief Asynchronous version of sinterstore
     * 
     * @tparam Func Callback function type
     * @tparam Keys Variadic types for source key names
     * @param func Callback function
     * @param destination Destination key where the resulting set will be stored
     * @param keys Source keys where the sets are stored
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func, typename... Keys>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    sinterstore(Func &&func, const std::string &destination, Keys &&...keys) {
        return static_cast<Derived &>(*this).template command<long long>(
            std::forward<Func>(func),
            "SINTERSTORE",
            destination,
            std::forward<Keys>(keys)...);
    }

    /**
     * @brief Determines if a member is in a set
     * 
     * @param key Key where the set is stored
     * @param member Member to check
     * @return true if the member exists in the set, false otherwise
     */
    bool
    sismember(const std::string &key, const std::string &member) {
        return static_cast<Derived &>(*this).template command<bool>("SISMEMBER", key, member).ok;
    }

    /**
     * @brief Asynchronous version of sismember
     * 
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the set is stored
     * @param member Member to check
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    Derived &
    sismember(Func &&func, const std::string &key, const std::string &member) {
        return static_cast<Derived &>(*this).template command<bool>(std::forward<Func>(func), "SISMEMBER", key, member);
    }

    /**
     * @brief Gets all members with a specific pattern from a set
     * 
     * @tparam Members Variadic types for member patterns
     * @param key Key where the set is stored
     * @param members Patterns to match members
     * @return Set of matching members (as optional strings)
     */
    template <typename... Members>
    std::enable_if_t<sizeof...(Members), qb::unordered_set<std::optional<std::string>>>
    smembers(const std::string &key, Members &&...members) {
        return static_cast<Derived &>(*this)
            .template command<qb::unordered_set<std::optional<std::string>>>(
                "SMEMBERS",
                key,
                std::forward<Members>(members)...)
            .result;
    }

    /**
     * @brief Asynchronous version of smembers with pattern matching
     * 
     * @tparam Func Callback function type
     * @tparam Members Variadic types for member patterns
     * @param func Callback function
     * @param key Key where the set is stored
     * @param members Patterns to match members
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func, typename... Members>
    std::enable_if_t<
        sizeof...(Members) && std::is_invocable_v<Func, Reply<qb::unordered_set<std::optional<std::string>>> &&>,
        Derived &>
    smembers(Func &&func, const std::string &key, Members &&...members) {
        return static_cast<Derived &>(*this).template command<qb::unordered_set<std::optional<std::string>>>(
            std::forward<Func>(func),
            "SMEMBERS",
            key,
            std::forward<Members>(members)...);
    }

    /**
     * @brief Gets all members of a set
     * 
     * @param key Key where the set is stored
     * @return Set of all members
     */
    qb::unordered_set<std::string>
    smembers(const std::string &key) {
        return static_cast<Derived &>(*this).template command<qb::unordered_set<std::string>>("SMEMBERS", key).result;
    }

    /**
     * @brief Asynchronous version of smembers
     * 
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the set is stored
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::unordered_set<std::string>> &&>, Derived &>
    smembers(Func &&func, const std::string &key) {
        return static_cast<Derived &>(*this).template command<qb::unordered_set<std::string>>(
            std::forward<Func>(func),
            "SMEMBERS",
            key);
    }

    /**
     * @brief Moves a member from one set to another
     * 
     * @param source Source key where the set is stored
     * @param destination Destination key where the set is stored
     * @param member Member to move
     * @return true if the member was moved, false if the member was not in the source set
     */
    bool
    smove(const std::string &source, const std::string &destination, const std::string &member) {
        return static_cast<Derived &>(*this).template command<bool>("SMOVE", source, destination, member).ok;
    }

    /**
     * @brief Asynchronous version of smove
     * 
     * @tparam Func Callback function type
     * @param func Callback function
     * @param source Source key where the set is stored
     * @param destination Destination key where the set is stored
     * @param member Member to move
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    Derived &
    smove(Func &&func, const std::string &source, const std::string &destination, const std::string &member) {
        return static_cast<Derived &>(*this)
            .template command<bool>(std::forward<Func>(func), "SMOVE", source, destination, member);
    }

    /**
     * @brief Removes and returns a random member from a set
     * 
     * @param key Key where the set is stored
     * @return The removed member, or std::nullopt if the set is empty
     */
    std::optional<std::string>
    spop(const std::string &key) {
        return static_cast<Derived &>(*this).template command<std::optional<std::string>>("SPOP", key).result;
    }

    /**
     * @brief Asynchronous version of spop
     * 
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the set is stored
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    Derived &
    spop(Func &&func, const std::string &key) {
        return static_cast<Derived &>(*this).template command<std::optional<std::string>>(
            std::forward<Func>(func),
            "SPOP",
            key);
    }

    /**
     * @brief Removes and returns multiple random members from a set
     * 
     * @param key Key where the set is stored
     * @param count Number of members to pop
     * @return Vector of removed members
     */
    std::vector<std::string>
    spop(const std::string &key, long long count) {
        return static_cast<Derived &>(*this).template command<std::vector<std::string>>("SPOP", key, count).result;
    }

    /**
     * @brief Asynchronous version of spop for multiple members
     * 
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the set is stored
     * @param count Number of members to pop
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    Derived &
    spop(Func &&func, const std::string &key, long long count) {
        return static_cast<Derived &>(*this)
            .template command<std::vector<std::string>>(std::forward<Func>(func), "SPOP", key, count);
    }

    /**
     * @brief Gets a random member from a set
     * 
     * @param key Key where the set is stored
     * @return A random member, or std::nullopt if the set is empty
     */
    std::optional<std::string>
    srandmember(const std::string &key) {
        return static_cast<Derived &>(*this).template command<std::optional<std::string>>("SRANDMEMBER", key).result;
    }

    /**
     * @brief Asynchronous version of srandmember
     * 
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the set is stored
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    Derived &
    srandmember(Func &&func, const std::string &key) {
        return static_cast<Derived &>(*this).template command<std::optional<std::string>>(
            std::forward<Func>(func),
            "SRANDMEMBER",
            key);
    }

    /**
     * @brief Gets multiple random members from a set
     * 
     * @param key Key where the set is stored
     * @param count Number of members to return
     * @return Vector of random members
     * @note If count is positive, result contains unique elements
     *       If count is negative, result may contain duplicate elements
     */
    std::vector<std::string>
    srandmember(const std::string &key, long long count) {
        return static_cast<Derived &>(*this)
            .template command<std::vector<std::string>>("SRANDMEMBER", key, count)
            .result;
    }

    /**
     * @brief Asynchronous version of srandmember for multiple members
     * 
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the set is stored
     * @param count Number of members to return
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    Derived &
    srandmember(Func &&func, const std::string &key, long long count) {
        return static_cast<Derived &>(*this)
            .template command<std::vector<std::string>>(std::forward<Func>(func), "SRANDMEMBER", key, count);
    }

    /**
     * @brief Removes members from a set
     * 
     * @tparam Members Variadic types for members to remove
     * @param key Key where the set is stored
     * @param members Members to remove
     * @return Number of members that were removed
     */
    template <typename... Members>
    long long
    srem(const std::string &key, Members &&...members) {
        return static_cast<Derived &>(*this)
            .template command<long long>("SREM", key, std::forward<Members>(members)...)
            .result;
    }

    /**
     * @brief Asynchronous version of srem
     * 
     * @tparam Func Callback function type
     * @tparam Members Variadic types for members to remove
     * @param func Callback function
     * @param key Key where the set is stored
     * @param members Members to remove
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func, typename... Members>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    srem(Func &&func, const std::string &key, Members &&...members) {
        return static_cast<Derived &>(*this)
            .template command<long long>(std::forward<Func>(func), "SREM", key, std::forward<Members>(members)...);
    }

    /**
     * @brief Incrementally iterates set elements
     * 
     * @param key Key where the set is stored
     * @param cursor Cursor position to start iteration from
     * @param pattern Pattern to filter members
     * @param count Hint for how many elements to return per call
     * @return Scan result containing next cursor and matching elements
     */
    reply::scan<>
    sscan(const std::string &key, long long cursor, const std::string &pattern = "*", long long count = 10) {
        return static_cast<Derived &>(*this)
            .template command<reply::scan<>>("SSCAN", key, cursor, "MATCH", pattern, "COUNT", count)
            .result;
    }

    /**
     * @brief Asynchronous version of sscan
     * 
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the set is stored
     * @param cursor Cursor position to start iteration from
     * @param pattern Pattern to filter members
     * @param count Hint for how many elements to return per call
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    Derived &
    sscan(
        Func &&func, const std::string &key, long long cursor, const std::string &pattern = "*", long long count = 10) {
        return static_cast<Derived &>(*this).template command<reply::scan<>>(
            std::forward<Func>(func),
            "SSCAN",
            key,
            cursor,
            "MATCH",
            pattern,
            "COUNT",
            count);
    }

    /**
     * @brief Automatically iterates through all set elements matching a pattern
     * 
     * This version manages cursor iteration internally, collecting all results
     * and calling the callback once with the complete result set.
     * 
     * @tparam Func Callback function type
     * @param func Callback function to process complete results
     * @param key Key where the set is stored
     * @param pattern Pattern to filter members
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<reply::scan<>> &&>, Derived &>
    sscan(Func &&func, const std::string &key, const std::string &pattern = "*") {
        new scanner<Func>(static_cast<Derived &>(*this), key, pattern, std::forward<Func>(func));
        return static_cast<Derived &>(*this);
    }

    /**
     * @brief Adds multiple sets
     * 
     * @tparam Keys Variadic types for key names
     * @param keys Keys where the sets are stored
     * @return Members of the resulting set (union of all sets)
     */
    template <typename... Keys>
    std::vector<std::string>
    sunion(Keys &&...keys) {
        return static_cast<Derived &>(*this)
            .template command<std::vector<std::string>>("SUNION", std::forward<Keys>(keys)...)
            .result;
    }

    /**
     * @brief Asynchronous version of sunion
     * 
     * @tparam Func Callback function type
     * @tparam Keys Variadic types for key names
     * @param func Callback function
     * @param keys Keys where the sets are stored
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func, typename... Keys>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>, Derived &>
    sunion(Func &&func, Keys &&...keys) {
        return static_cast<Derived &>(*this).template command<std::vector<std::string>>(
            std::forward<Func>(func),
            "SUNION",
            std::forward<Keys>(keys)...);
    }

    /**
     * @brief Adds multiple sets and stores the result in a key
     * 
     * @tparam Keys Variadic types for source key names
     * @param destination Destination key where the resulting set will be stored
     * @param keys Source keys where the sets are stored
     * @return Number of members in the resulting set
     */
    template <typename... Keys>
    long long
    sunionstore(const std::string &destination, Keys &&...keys) {
        return static_cast<Derived &>(*this)
            .template command<long long>("SUNIONSTORE", destination, std::forward<Keys>(keys)...)
            .result;
    }

    /**
     * @brief Asynchronous version of sunionstore
     * 
     * @tparam Func Callback function type
     * @tparam Keys Variadic types for source key names
     * @param func Callback function
     * @param destination Destination key where the resulting set will be stored
     * @param keys Source keys where the sets are stored
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func, typename... Keys>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    sunionstore(Func &&func, const std::string &destination, Keys &&...keys) {
        return static_cast<Derived &>(*this).template command<long long>(
            std::forward<Func>(func),
            "SUNIONSTORE",
            destination,
            std::forward<Keys>(keys)...);
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_SET_COMMANDS_H
