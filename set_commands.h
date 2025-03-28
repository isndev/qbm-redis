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
private:
    constexpr Derived &
    derived() {
        return static_cast<Derived &>(*this);
    }

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
        size_t _cursor{0};
        qb::redis::Reply<qb::redis::scan<>> _reply;

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
        operator()(qb::redis::Reply<qb::redis::scan<>> &&reply) {
            _reply.ok() = reply.ok();
            std::move(reply.result().items.begin(), reply.result().items.end(), std::back_inserter(_reply.result().items));
            if (reply.ok() && reply.result().cursor)
                _handler.sscan(std::ref(*this), _key, reply.result().cursor, _pattern, 100);
            else {
                _func(std::move(_reply));
                delete this;
            }
        }
    };

public:
    // =============== Basic Set Operations ===============

    /**
     * @brief Adds members to a set
     * 
     * @tparam Members Variadic types for set members
     * @param key Key where the set is stored
     * @param members Members to add to the set
     * @return Number of members that were added to the set
     * @note Time complexity: O(1) for each element added
     * @see https://redis.io/commands/sadd
     */
    template <typename... Members>
    long long
    sadd(const std::string &key, Members &&...members) {
        if (key.empty() || sizeof...(members) == 0) {
            return 0;
        }
        return derived().template command<long long>("SADD", key, std::forward<Members>(members)...).result();
    }

    /**
     * @brief Asynchronous version of sadd
     * 
     * @tparam Func Callback function type
     * @tparam Members Variadic types for set members
     * @param func Callback function
     * @param key Key where the set is stored
     * @param members Members to add to the set
     * @return Reference to the derived class
     * @see https://redis.io/commands/sadd
     */
    template <typename Func, typename... Members>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    sadd(Func &&func, const std::string &key, Members &&...members) {
        if (key.empty() || sizeof...(members) == 0) {
            return derived();
        }
        return derived().template command<long long>(std::forward<Func>(func), "SADD", key, std::forward<Members>(members)...);
    }

    /**
     * @brief Gets the number of members in a set
     * 
     * @param key Key where the set is stored
     * @return Number of members in the set
     * @note Time complexity: O(1)
     * @see https://redis.io/commands/scard
     */
    long long
    scard(const std::string &key) {
        if (key.empty()) {
            return 0;
        }
        return derived().template command<long long>("SCARD", key).result();
    }

    /**
     * @brief Asynchronous version of scard
     * 
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the set is stored
     * @return Reference to the derived class
     * @see https://redis.io/commands/scard
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    scard(Func &&func, const std::string &key) {
        if (key.empty()) {
            return derived();
        }
        return derived().template command<long long>(std::forward<Func>(func), "SCARD", key);
    }

    // =============== Set Operations ===============

    /**
     * @brief Subtracts multiple sets
     * 
     * @param keys Keys where the sets are stored
     * @return Members of the resulting set (difference between first set and all others)
     * @note Time complexity: O(N) where N is the total number of elements in all sets
     * @see https://redis.io/commands/sdiff
     */
    std::vector<std::string>
    sdiff(const std::vector<std::string> &keys) {
        if (keys.size() == 0) {
            return {};
        }

        return derived().template command<std::vector<std::string>>("SDIFF", keys).result();
    }

    /**
     * @brief Asynchronous version of sdiff
     * 
     * @tparam Func Callback function type
     * @param func Callback function
     * @param keys Keys where the sets are stored
     * @return Reference to the derived class
     * @see https://redis.io/commands/sdiff
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>, Derived &>
    sdiff(Func &&func, const std::vector<std::string> &keys) {
        if (keys.size() == 0) {
            return derived();
        }

        return derived().template command<std::vector<std::string>>(
            std::forward<Func>(func),
            "SDIFF",
            keys);
    }

    /**
     * @brief Subtracts multiple sets and stores the result in a key
     * 
     * @param destination Destination key where the resulting set will be stored
     * @param keys Source keys where the sets are stored
     * @return Number of members in the resulting set
     * @note Time complexity: O(N) where N is the total number of elements in all sets
     * @see https://redis.io/commands/sdiffstore
     */
    long long
    sdiffstore(const std::string &destination, const std::vector<std::string> &keys) {
        if (destination.empty() || keys.size() == 0) {
            return 0;
        }

        return derived().template command<long long>("SDIFFSTORE", destination, keys).result();
    }

    /**
     * @brief Asynchronous version of sdiffstore
     * 
     * @tparam Func Callback function type
     * @param func Callback function
     * @param destination Destination key where the resulting set will be stored
     * @param keys Source keys where the sets are stored
     * @return Reference to the derived class
     * @see https://redis.io/commands/sdiffstore
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    sdiffstore(Func &&func, const std::string &destination, const std::vector<std::string> &keys) {
        if (destination.empty() || keys.size() == 0) {
            return derived();
        }

        return derived().template command<long long>(
            std::forward<Func>(func),
            "SDIFFSTORE",
            destination,
            keys);
    }

    /**
     * @brief Intersects multiple sets
     * 
     * @param keys Keys where the sets are stored
     * @return Members of the resulting set (intersection of all sets)
     * @note Time complexity: O(N*M) worst case where N is the size of the smallest set and M is the number of sets
     * @see https://redis.io/commands/sinter
     */
    std::vector<std::string>
    sinter(const std::vector<std::string> &keys) {
        if (keys.size() == 0) {
            return {};
        }
        return derived().template command<std::vector<std::string>>("SINTER", keys).result();
    }

    /**
     * @brief Asynchronous version of sinter
     * 
     * @tparam Func Callback function type
     * @param func Callback function
     * @param keys Keys where the sets are stored
     * @return Reference to the derived class
     * @see https://redis.io/commands/sinter
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>, Derived &>
    sinter(Func &&func, const std::vector<std::string> &keys) {
        if (keys.size() == 0) {
            return derived();
        }

        return derived().template command<std::vector<std::string>>(
            std::forward<Func>(func),
            "SINTER",
            keys);
    }

    /**
     * @brief Gets the cardinality of the intersection of multiple sets
     * 
     * @tparam Keys Variadic types for key names
     * @param keys Keys where the sets are stored
     * @param limit Maximum number of elements to count (optional)
     * @return Number of elements in the intersection
     * @note Time complexity: O(N*M) worst case where N is the size of the smallest set and M is the number of sets
     * @see https://redis.io/commands/sintercard
     */
    long long
    sintercard(const std::vector<std::string> &keys, std::optional<long long> limit = std::nullopt) {
        if (keys.size() == 0) {
            return 0;
        }
        std::vector<std::string> args;
        args.reserve(2);
        if (limit) {
            args.push_back("LIMIT");
            args.push_back(std::to_string(*limit));
        }
        return derived().template command<long long>("SINTERCARD", keys.size(), keys, args).result();
    }

    /**
     * @brief Asynchronous version of sintercard
     * 
     * @tparam Func Callback function type
     * @param func Callback function
     * @param keys Keys where the sets are stored
     * @param limit Maximum number of elements to count (optional)
     * @return Reference to the derived class
     * @see https://redis.io/commands/sintercard
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    sintercard(Func &&func, const std::vector<std::string> &keys, std::optional<long long> limit = std::nullopt) {
        if (keys.size() == 0) {
            return derived();
        }
        std::vector<std::string> args;
        args.reserve(2);
        if (limit) {
            args.push_back("LIMIT");
            args.push_back(std::to_string(*limit));
        }
        return derived().template command<long long>(std::forward<Func>(func), "SINTERCARD", keys.size(), keys, args);
    }

    /**
     * @brief Intersects multiple sets and stores the result in a key
     * 
     * @param destination Destination key where the resulting set will be stored
     * @param keys Source keys where the sets are stored
     * @return Number of members in the resulting set
     * @note Time complexity: O(N*M) worst case where N is the size of the smallest set and M is the number of sets
     * @see https://redis.io/commands/sinterstore
     */
    long long
    sinterstore(const std::string &destination, const std::vector<std::string> &keys) {
        if (destination.empty() || keys.size() == 0) {
            return 0;
        }
        std::vector<std::string> args;
        return derived().template command<long long>("SINTERSTORE", destination, keys).result();
    }

    /**
     * @brief Asynchronous version of sinterstore
     * 
     * @tparam Func Callback function type
     * @param func Callback function
     * @param destination Destination key where the resulting set will be stored
     * @param keys Source keys where the sets are stored
     * @return Reference to the derived class
     * @see https://redis.io/commands/sinterstore
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    sinterstore(Func &&func, const std::string &destination, const std::vector<std::string> &keys) {
        if (destination.empty() || keys.size() == 0) {
            return derived();
        }
        return derived().template command<long long>(
            std::forward<Func>(func),
            "SINTERSTORE",
            destination,
            keys);
    }

    /**
     * @brief Determines if a member is in a set
     * 
     * @param key Key where the set is stored
     * @param member Member to check
     * @return true if the member exists in the set, false otherwise
     * @note Time complexity: O(1)
     * @see https://redis.io/commands/sismember
     */
    bool
    sismember(const std::string &key, const std::string &member) {
        if (key.empty() || member.empty()) {
            return false;
        }
        return derived().template command<bool>("SISMEMBER", key, member).result();
    }

    /**
     * @brief Asynchronous version of sismember
     * 
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the set is stored
     * @param member Member to check
     * @return Reference to the derived class
     * @see https://redis.io/commands/sismember
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    sismember(Func &&func, const std::string &key, const std::string &member) {
        if (key.empty() || member.empty()) {
            return derived();
        }
        return derived().template command<bool>(std::forward<Func>(func), "SISMEMBER", key, member);
    }

    /**
     * @brief Determines if multiple members are in a set
     * 
     * @tparam Members Variadic types for members to check
     * @param key Key where the set is stored
     * @param members Members to check
     * @return Vector of boolean values indicating membership
     * @note Time complexity: O(N) where N is the number of members to check
     * @see https://redis.io/commands/smismember
     */
    template <typename... Members>
    std::vector<bool>
    smismember(const std::string &key, Members &&...members) {
        if (key.empty() || sizeof...(members) == 0) {
            return {};
        }
        return derived().template command<std::vector<bool>>("SMISMEMBER", key, std::forward<Members>(members)...).result();
    }

    /**
     * @brief Asynchronous version of smismember
     * 
     * @tparam Func Callback function type
     * @tparam Members Variadic types for members to check
     * @param func Callback function
     * @param key Key where the set is stored
     * @param members Members to check
     * @return Reference to the derived class
     * @see https://redis.io/commands/smismember
     */
    template <typename Func, typename... Members>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<bool>> &&>, Derived &>
    smismember(Func &&func, const std::string &key, Members &&...members) {
        if (key.empty() || sizeof...(members) == 0) {
            return derived();
        }
        return derived().template command<std::vector<bool>>(
            std::forward<Func>(func),
            "SMISMEMBER",
            key,
            std::forward<Members>(members)...);
    }

    /**
     * @brief Gets all members of a set
     * 
     * @param key Key where the set is stored
     * @return Set of all members
     * @note Time complexity: O(N) where N is the size of the set
     * @see https://redis.io/commands/smembers
     */
    qb::unordered_set<std::string>
    smembers(const std::string &key) {
        if (key.empty()) {
            return {};
        }
        return derived().template command<qb::unordered_set<std::string>>("SMEMBERS", key).result();
    }

    /**
     * @brief Asynchronous version of smembers
     * 
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the set is stored
     * @return Reference to the derived class
     * @see https://redis.io/commands/smembers
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::unordered_set<std::string>> &&>, Derived &>
    smembers(Func &&func, const std::string &key) {
        if (key.empty()) {
            return derived();
        }
        return derived().template command<qb::unordered_set<std::string>>(
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
     * @note Time complexity: O(1)
     * @see https://redis.io/commands/smove
     */
    bool
    smove(const std::string &source, const std::string &destination, const std::string &member) {
        if (source.empty() || destination.empty() || member.empty()) {
            return false;
        }
        return derived().template command<bool>("SMOVE", source, destination, member).result();
    }

    /**
     * @brief Asynchronous version of smove
     * 
     * @tparam Func Callback function type
     * @param func Callback function
     * @param source Source key where the set is stored
     * @param destination Destination key where the set is stored
     * @param member Member to move
     * @return Reference to the derived class
     * @see https://redis.io/commands/smove
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    smove(Func &&func, const std::string &source, const std::string &destination, const std::string &member) {
        if (source.empty() || destination.empty() || member.empty()) {
            return derived();
        }
        return derived().template command<bool>(
            std::forward<Func>(func),
            "SMOVE",
            source,
            destination,
            member);
    }

    /**
     * @brief Removes and returns a random member from a set
     * 
     * @param key Key where the set is stored
     * @return The removed member, or std::nullopt if the set is empty
     * @note Time complexity: O(1)
     * @see https://redis.io/commands/spop
     */
    std::optional<std::string>
    spop(const std::string &key) {
        if (key.empty()) {
            return std::nullopt;
        }
        return derived().template command<std::optional<std::string>>("SPOP", key).result();
    }

    /**
     * @brief Asynchronous version of spop
     * 
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the set is stored
     * @return Reference to the derived class
     * @see https://redis.io/commands/spop
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::string>> &&>, Derived &>
    spop(Func &&func, const std::string &key) {
        if (key.empty()) {
            return derived();
        }
        return derived().template command<std::optional<std::string>>(
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
     * @note Time complexity: O(N) where N is the number of members to pop
     * @see https://redis.io/commands/spop
     */
    std::vector<std::string>
    spop(const std::string &key, long long count) {
        if (key.empty() || count < 1) {
            return {};
        }
        return derived().template command<std::vector<std::string>>("SPOP", key, count).result();
    }

    /**
     * @brief Asynchronous version of spop for multiple members
     * 
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the set is stored
     * @param count Number of members to pop
     * @return Reference to the derived class
     * @see https://redis.io/commands/spop
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>, Derived &>
    spop(Func &&func, const std::string &key, long long count) {
        if (key.empty() || count < 1) {
            return derived();
        }
        return derived().template command<std::vector<std::string>>(
            std::forward<Func>(func),
            "SPOP",
            key,
            count);
    }

    /**
     * @brief Gets a random member from a set
     * 
     * @param key Key where the set is stored
     * @return A random member, or std::nullopt if the set is empty
     * @note Time complexity: O(1)
     * @see https://redis.io/commands/srandmember
     */
    std::optional<std::string>
    srandmember(const std::string &key) {
        if (key.empty()) {
            return std::nullopt;
        }
        return derived().template command<std::optional<std::string>>("SRANDMEMBER", key).result();
    }

    /**
     * @brief Asynchronous version of srandmember
     * 
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the set is stored
     * @return Reference to the derived class
     * @see https://redis.io/commands/srandmember
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::string>> &&>, Derived &>
    srandmember(Func &&func, const std::string &key) {
        if (key.empty()) {
            return derived();
        }
        return derived().template command<std::optional<std::string>>(
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
     * @note Time complexity: O(N) where N is the absolute value of count
     * @see https://redis.io/commands/srandmember
     */
    std::vector<std::string>
    srandmember(const std::string &key, long long count) {
        if (key.empty()) {
            return {};
        }
        return derived().template command<std::vector<std::string>>("SRANDMEMBER", key, count).result();
    }

    /**
     * @brief Asynchronous version of srandmember for multiple members
     * 
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the set is stored
     * @param count Number of members to return
     * @return Reference to the derived class
     * @see https://redis.io/commands/srandmember
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>, Derived &>
    srandmember(Func &&func, const std::string &key, long long count) {
        if (key.empty()) {
            return derived();
        }
        return derived().template command<std::vector<std::string>>(
            std::forward<Func>(func),
            "SRANDMEMBER",
            key,
            count);
    }

    /**
     * @brief Removes members from a set
     * 
     * @tparam Members Variadic types for members to remove
     * @param key Key where the set is stored
     * @param members Members to remove
     * @return Number of members that were removed
     * @note Time complexity: O(N) where N is the number of members to be removed
     * @see https://redis.io/commands/srem
     */
    template <typename... Members>
    long long
    srem(const std::string &key, Members &&...members) {
        if (key.empty() || sizeof...(members) == 0) {
            return 0;
        }
        return derived().template command<long long>("SREM", key, std::forward<Members>(members)...).result();
    }

    /**
     * @brief Asynchronous version of srem
     * 
     * @tparam Func Callback function type
     * @tparam Members Variadic types for members to remove
     * @param func Callback function
     * @param key Key where the set is stored
     * @param members Members to remove
     * @return Reference to the derived class
     * @see https://redis.io/commands/srem
     */
    template <typename Func, typename... Members>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    srem(Func &&func, const std::string &key, Members &&...members) {
        if (key.empty() || sizeof...(members) == 0) {
            return derived();
        }
        return derived().template command<long long>(
            std::forward<Func>(func),
            "SREM",
            key,
            std::forward<Members>(members)...);
    }

    // =============== Set Scanning Operations ===============

    /**
     * @brief Incrementally iterates set elements
     * 
     * @param key Key where the set is stored
     * @param cursor Cursor position to start iteration from
     * @param pattern Pattern to filter members
     * @param count Hint for how many elements to return per call
     * @return Scan result containing next cursor and matching elements
     * @note Time complexity: O(1) for every call. O(N) for a complete iteration
     * @see https://redis.io/commands/sscan
     */
    scan<>
    sscan(const std::string &key, long long cursor, const std::string &pattern = "*", long long count = 10) {
        if (key.empty()) {
            return {};
        }
        return derived().template command<scan<>>(
            "SSCAN",
            key,
            cursor,
            "MATCH",
            pattern,
            "COUNT",
            count)
            .result();
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
     * @return Reference to the derived class
     * @see https://redis.io/commands/sscan
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<scan<>> &&>, Derived &>
    sscan(Func &&func, const std::string &key, long long cursor, const std::string &pattern = "*", long long count = 10) {
        if (key.empty()) {
            return derived();
        }
        return derived().template command<scan<>>(
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
     * @return Reference to the derived class
     * @see https://redis.io/commands/sscan
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<scan<>> &&>, Derived &>
    sscan(Func &&func, const std::string &key, const std::string &pattern = "*") {
        if (key.empty()) {
            return derived();
        }
        new scanner<Func>(derived(), key, pattern, std::forward<Func>(func));
        return derived();
    }

    // =============== Set Operations ===============

    /**
     * @brief Adds multiple sets
     * 
     * @param keys Keys where the sets are stored
     * @return Members of the resulting set (union of all sets)
     * @note Time complexity: O(N) where N is the total number of elements in all sets
     * @see https://redis.io/commands/sunion
     */
    std::vector<std::string>
    sunion(const std::vector<std::string> &keys) {
        if (keys.size() == 0) {
            return {};
        }
        std::vector<std::string> args;
        args.reserve(keys.size());
        args.insert(args.end(), keys.begin(), keys.end());
        return derived().template command<std::vector<std::string>>("SUNION", args).result();
    }

    /**
     * @brief Asynchronous version of sunion
     * 
     * @tparam Func Callback function type
     * @param func Callback function
     * @param keys Keys where the sets are stored
     * @return Reference to the derived class
     * @see https://redis.io/commands/sunion
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>, Derived &>
    sunion(Func &&func, const std::vector<std::string> &keys) {
        if (keys.size() == 0) {
            return derived();
        }
        std::vector<std::string> args;
        args.reserve(keys.size());
        args.insert(args.end(), keys.begin(), keys.end());
        return derived().template command<std::vector<std::string>>(
            std::forward<Func>(func),
            "SUNION",
            args);
    }

    /**
     * @brief Adds multiple sets and stores the result in a key
     * 
     * @param destination Destination key where the resulting set will be stored
     * @param keys Source keys where the sets are stored
     * @return Number of members in the resulting set
     * @note Time complexity: O(N) where N is the total number of elements in all sets
     * @see https://redis.io/commands/sunionstore
     */
    long long
    sunionstore(const std::string &destination, const std::vector<std::string> &keys) {
        if (destination.empty() || keys.size() == 0) {
            return 0;
        }
        return derived().template command<long long>("SUNIONSTORE", destination, keys).result();
    }

    /**
     * @brief Asynchronous version of sunionstore
     * 
     * @tparam Func Callback function type
     * @param func Callback function
     * @param destination Destination key where the resulting set will be stored
     * @param keys Source keys where the sets are stored
     * @return Reference to the derived class
     * @see https://redis.io/commands/sunionstore
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    sunionstore(Func &&func, const std::string &destination, const std::vector<std::string> &keys) {
        if (destination.empty() || keys.size() == 0) {
            return derived();
        }
        return derived().template command<long long>(
            std::forward<Func>(func),
            "SUNIONSTORE",
            destination,
            keys);
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_SET_COMMANDS_H
