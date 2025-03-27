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

#ifndef QBM_REDIS_SORTED_SET_COMMANDS_H
#define QBM_REDIS_SORTED_SET_COMMANDS_H
#include <chrono>
#include <utility>
#include "reply.h"

namespace qb::redis {

/**
 * @class sorted_set_commands
 * @brief Provides Redis sorted set command implementations.
 *
 * This class implements Redis sorted set operations, which provide an ordered collection
 * of unique strings, sorted by associated scores. Each command has both synchronous 
 * and asynchronous versions.
 *
 * Redis sorted sets are particularly useful for ranking data, implementing leaderboards,
 * and efficiently retrieving ranges of elements based on their ordering.
 *
 * @tparam Derived The derived class type (CRTP pattern)
 */
template <typename Derived>
class sorted_set_commands {
    /**
     * @class scanner
     * @brief Helper class for implementing incremental scanning of sorted sets
     * 
     * @tparam Func Callback function type
     */
    template <typename Func>
    class scanner {
        Derived &_handler;
        std::string _key;
        std::string _pattern;
        Func _func;
        qb::redis::Reply<qb::redis::reply::scan<qb::unordered_map<std::string, double>>> _reply;

    public:
        /**
         * @brief Constructs a scanner for sorted set elements
         * 
         * @param handler The Redis handler
         * @param key Key where the sorted set is stored
         * @param pattern Pattern to filter members
         * @param func Callback function to process results
         */
        scanner(Derived &handler, std::string key, std::string pattern, Func &&func)
            : _handler(handler)
            , _key(std::move(key))
            , _pattern(std::move(pattern))
            , _func(std::forward<Func>(func)) {
            _handler.zscan(std::ref(*this), _key, 0, _pattern, 100);
        }

        /**
         * @brief Processes scan results and continues scanning if needed
         * 
         * @param reply The scan operation reply
         */
        void
        operator()(qb::redis::Reply<qb::redis::reply::scan<qb::unordered_map<std::string, double>>> &&reply) {
            _reply.ok = reply.ok;
            std::move(
                reply.result.items.begin(),
                reply.result.items.end(),
                std::inserter(_reply.result.items, _reply.result.items.end()));
            if (reply.ok && reply.result.cursor)
                _handler.zscan(std::ref(*this), _key, reply.result.cursor, _pattern, 100);
            else {
                _func(std::move(_reply));
                delete this;
            }
        }
    };

public:
    /**
     * @brief Removes and returns the member with the highest score from a sorted set, blocking if set is empty
     * 
     * @tparam Keys Variadic types for key names
     * @param keys Keys where sorted sets are stored
     * @param timeout Timeout in seconds, 0 means block forever
     * @return Optional tuple containing key name, member name, and score
     */
    template <typename... Keys>
    std::optional<std::tuple<std::string, std::string, double>>
    bzpopmax(Keys &&...keys, long long timeout) {
        return static_cast<Derived &>(*this)
            .template command<std::optional<std::tuple<std::string, std::string, double>>>(
                "BZPOPMAX",
                std::forward<Keys>(keys)...,
                timeout)
            .result;
    }
    
    /**
     * @brief Asynchronous version of bzpopmax
     * 
     * @tparam Func Callback function type
     * @tparam Keys Variadic types for key names
     * @param func Callback function
     * @param keys Keys where sorted sets are stored
     * @param timeout Timeout in seconds, 0 means block forever
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func, typename... Keys>
    std::enable_if_t<
        std::is_invocable_v<Func, Reply<std::optional<std::tuple<std::string, std::string, double>>> &&>, Derived &>
    bzpopmax(Func &&func, Keys &&...keys, long long timeout) {
        return static_cast<Derived &>(*this)
            .template command<std::optional<std::tuple<std::string, std::string, double>>>(
                std::forward<Func>(func),
                "BZPOPMAX",
                std::forward<Keys>(keys)...,
                timeout);
    }

    /**
     * @brief Overload of bzpopmax accepting std::chrono::seconds for timeout
     * 
     * @tparam Keys Variadic types for key names
     * @param keys Keys where sorted sets are stored
     * @param timeout Timeout as std::chrono::seconds, 0 means block forever
     * @return Optional tuple containing key name, member name, and score
     */
    template <typename... Keys>
    std::optional<std::tuple<std::string, std::string, double>>
    bzpopmax(Keys &&...keys, const std::chrono::seconds &timeout = std::chrono::seconds{0}) {
        return bzpopmax(std::forward<Keys>(keys)..., timeout.count());
    }
    
    /**
     * @brief Asynchronous version of bzpopmax with std::chrono::seconds
     * 
     * @tparam Func Callback function type
     * @tparam Keys Variadic types for key names
     * @param func Callback function
     * @param keys Keys where sorted sets are stored
     * @param timeout Timeout as std::chrono::seconds, 0 means block forever
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func, typename... Keys>
    std::enable_if_t<
        std::is_invocable_v<Func, Reply<std::optional<std::tuple<std::string, std::string, double>>> &&>, Derived &>
    bzpopmax(Func &&func, Keys &&...keys, const std::chrono::seconds &timeout = std::chrono::seconds{0}) {
        return bzpopmax(std::forward<Func>(func), std::forward<Keys>(keys)..., timeout.count());
    }

    /**
     * @brief Removes and returns the member with the lowest score from a sorted set, blocking if set is empty
     * 
     * @tparam Keys Variadic types for key names
     * @param keys Keys where sorted sets are stored
     * @param timeout Timeout in seconds, 0 means block forever
     * @return Optional tuple containing key name, member name, and score
     */
    template <typename... Keys>
    std::optional<std::tuple<std::string, std::string, double>>
    bzpopmin(Keys &&...keys, long long timeout) {
        return static_cast<Derived &>(*this)
            .template command<std::optional<std::tuple<std::string, std::string, double>>>(
                "BZPOPMIN",
                std::forward<Keys>(keys)...,
                timeout)
            .result;
    }
    
    /**
     * @brief Asynchronous version of bzpopmin
     * 
     * @tparam Func Callback function type
     * @tparam Keys Variadic types for key names
     * @param func Callback function
     * @param keys Keys where sorted sets are stored
     * @param timeout Timeout in seconds, 0 means block forever
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func, typename... Keys>
    std::enable_if_t<
        std::is_invocable_v<Func, Reply<std::optional<std::tuple<std::string, std::string, double>>> &&>, Derived &>
    bzpopmin(Func &&func, Keys &&...keys, long long timeout) {
        return static_cast<Derived &>(*this)
            .template command<std::optional<std::tuple<std::string, std::string, double>>>(
                std::forward<Func>(func),
                "BZPOPMIN",
                std::forward<Keys>(keys)...,
                timeout);
    }

    /**
     * @brief Overload of bzpopmin accepting std::chrono::seconds for timeout
     * 
     * @tparam Keys Variadic types for key names
     * @param keys Keys where sorted sets are stored
     * @param timeout Timeout as std::chrono::seconds, 0 means block forever
     * @return Optional tuple containing key name, member name, and score
     */
    template <typename... Keys>
    std::optional<std::tuple<std::string, std::string, double>>
    bzpopmin(Keys &&...keys, const std::chrono::seconds &timeout = std::chrono::seconds{0}) {
        return bzpopmin(std::forward<Keys>(keys)..., timeout.count());
    }
    
    /**
     * @brief Asynchronous version of bzpopmin with std::chrono::seconds
     * 
     * @tparam Func Callback function type
     * @tparam Keys Variadic types for key names
     * @param func Callback function
     * @param keys Keys where sorted sets are stored
     * @param timeout Timeout as std::chrono::seconds, 0 means block forever
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func, typename... Keys>
    std::enable_if_t<
        std::is_invocable_v<Func, Reply<std::optional<std::tuple<std::string, std::string, double>>> &&>, Derived &>
    bzpopmin(Func &&func, Keys &&...keys, const std::chrono::seconds &timeout = std::chrono::seconds{0}) {
        return bzpopmin(std::forward<Func>(func), std::forward<Keys>(keys)..., timeout.count());
    }

    /**
     * @brief Adds one or more members to a sorted set, or updates scores if members already exist
     * 
     * @param key Key where the sorted set is stored
     * @param members Vector of score-member pairs to add
     * @param type Update type (ALWAYS, EXIST, or NOT_EXIST)
     * @param changed If true, return number of changed elements, not just new elements
     * @return Number of elements added to the sorted set
     */
    inline long long
    zadd(
        const std::string &key, const std::vector<std::pair<double, std::string>> &members,
        UpdateType type = UpdateType::ALWAYS, bool changed = false) {
        std::optional<std::string> opt_up, opt_ch;

        if (type != UpdateType::ALWAYS)
            opt_up = std::to_string(type);

        if (changed)
            opt_ch = "CH";

        return static_cast<Derived &>(*this).template command<long long>("ZADD", key, opt_up, opt_ch, members).result;
    }
    
    /**
     * @brief Asynchronous version of zadd
     * 
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the sorted set is stored
     * @param members Vector of score-member pairs to add
     * @param type Update type (ALWAYS, EXIST, or NOT_EXIST)
     * @param changed If true, return number of changed elements, not just new elements
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    zadd(
        Func &&func, const std::string &key, const std::vector<std::pair<double, std::string>> &members,
        UpdateType type = UpdateType::ALWAYS, bool changed = false) {
        std::optional<std::string> opt_up, opt_ch;

        if (type != UpdateType::ALWAYS)
            opt_up = std::to_string(type);

        if (changed)
            opt_ch = "CH";

        return static_cast<Derived &>(*this)
            .template command<long long>(std::forward<Func>(func), "ZADD", key, opt_up, opt_ch, members);
    }

    /**
     * @brief Gets the number of members in a sorted set
     * 
     * @param key Key where the sorted set is stored
     * @return Number of members in the sorted set
     */
    long long
    zcard(const std::string &key) {
        return static_cast<Derived &>(*this).template command<long long>("ZCARD", key).result;
    }
    
    /**
     * @brief Asynchronous version of zcard
     * 
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the sorted set is stored
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    Derived &
    zcard(Func &&func, const std::string &key) {
        return static_cast<Derived &>(*this).template command<long long>(std::forward<Func>(func), "ZCARD", key);
    }

    /**
     * @brief Counts the number of members in a sorted set with scores within the given interval
     * 
     * @tparam Interval Type of the score interval
     * @param key Key where the sorted set is stored
     * @param interval Interval object with lower() and upper() methods
     * @return Number of members in the sorted set with scores in the interval
     */
    template <typename Interval>
    long long
    zcount(const std::string &key, const Interval &interval) {
        return static_cast<Derived &>(*this)
            .template command<long long>("ZCOUNT", key, interval.lower(), interval.upper())
            .result;
    }
    
    /**
     * @brief Asynchronous version of zcount
     * 
     * @tparam Func Callback function type
     * @tparam Interval Type of the score interval
     * @param func Callback function
     * @param key Key where the sorted set is stored
     * @param interval Interval object with lower() and upper() methods
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func, typename Interval>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    zcount(Func &&func, const std::string &key, const Interval &interval) {
        return static_cast<Derived &>(*this)
            .template command<long long>(std::forward<Func>(func), "ZCOUNT", key, interval.lower(), interval.upper());
    }

    /**
     * @brief Increments the score of a member in a sorted set
     * 
     * @param key Key where the sorted set is stored
     * @param increment Amount to increment the score by
     * @param member Member whose score should be incremented
     * @return New score of the member after increment
     */
    double
    zincrby(const std::string &key, double increment, const std::string &member) {
        return static_cast<Derived &>(*this).template command<double>("ZINCRBY", key, increment, member).result;
    }
    
    /**
     * @brief Asynchronous version of zincrby
     * 
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the sorted set is stored
     * @param increment Amount to increment the score by
     * @param member Member whose score should be incremented
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    Derived &
    zincrby(Func &&func, const std::string &key, double increment, const std::string &member) {
        return static_cast<Derived &>(*this)
            .template command<double>(std::forward<Func>(func), "ZINCRBY", key, increment, member);
    }

    /**
     * @brief Intersects multiple sorted sets and stores the result in a new key
     * 
     * @param destination Key where the resulting sorted set will be stored
     * @param keys Vector of keys where the source sorted sets are stored
     * @param weights Vector of weights to apply to each sorted set
     * @param type Aggregation type (SUM, MIN, or MAX)
     * @return Number of members in the resulting sorted set
     */
    inline long long
    zinterstore(
        const std::string &destination, const std::vector<std::string> &keys, const std::vector<double> &weights = {},
        Aggregation type = Aggregation::SUM) {
        std::optional<std::string> opt;
        if (!weights.empty())
            opt = "WEIGHTS";
        return static_cast<Derived &>(*this)
            .template command<long long>(
                "ZINTERSTORE",
                destination,
                keys.size(),
                keys,
                opt,
                weights,
                "AGGREGATE",
                std::to_string(type))
            .result;
    }
    
    /**
     * @brief Asynchronous version of zinterstore
     * 
     * @tparam Func Callback function type
     * @param func Callback function
     * @param destination Key where the resulting sorted set will be stored
     * @param keys Vector of keys where the source sorted sets are stored
     * @param weights Vector of weights to apply to each sorted set
     * @param type Aggregation type (SUM, MIN, or MAX)
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    zinterstore(
        Func &&func, const std::string &destination, const std::vector<std::string> &keys,
        const std::vector<double> &weights = {}, Aggregation type = Aggregation::SUM) {
        std::optional<std::string> opt;
        if (!weights.empty())
            opt = "WEIGHTS";
        return static_cast<Derived &>(*this).template command<long long>(
            std::forward<Func>(func),
            "ZINTERSTORE",
            destination,
            keys.size(),
            keys,
            opt,
            weights,
            "AGGREGATE",
            std::to_string(type));
    }

    /**
     * @brief Simplified version of zinterstore for a single source set
     * 
     * @param destination Key where the resulting sorted set will be stored
     * @param key Key where the source sorted set is stored
     * @param weight Weight to apply to the sorted set
     * @param type Aggregation type (SUM, MIN, or MAX)
     * @return Number of members in the resulting sorted set
     */
    inline long long
    zinterstore(
        const std::string &destination, const std::string &key, double weight = 1.,
        Aggregation type = Aggregation::SUM) {
        std::optional<std::string> opt;
        return zinterstore(destination, std::vector{key}, std::vector{weight}, type);
    }
    
    /**
     * @brief Asynchronous version of simplified zinterstore
     * 
     * @tparam Func Callback function type
     * @param func Callback function
     * @param destination Key where the resulting sorted set will be stored
     * @param key Key where the source sorted set is stored
     * @param weight Weight to apply to the sorted set
     * @param type Aggregation type (SUM, MIN, or MAX)
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    zinterstore(
        Func &&func, const std::string &destination, const std::string &key, double weight = 1.,
        Aggregation type = Aggregation::SUM) {
        return zinterstore(std::forward<Func>(func), destination, std::vector{key}, std::vector{weight}, type);
    }

    /**
     * @brief Counts the number of members in a sorted set between a lexicographical range
     * 
     * @tparam Interval Type of the lexicographical interval
     * @param key Key where the sorted set is stored
     * @param interval Interval object with lower() and upper() methods
     * @return Number of members in the sorted set within the lexicographical range
     */
    template <typename Interval>
    long long
    zlexcount(const std::string &key, const Interval &interval) {
        return static_cast<Derived &>(*this)
            .template command<long long>("ZLEXCOUNT", key, interval.lower(), interval.upper())
            .result;
    }
    
    /**
     * @brief Asynchronous version of zlexcount
     * 
     * @tparam Func Callback function type
     * @tparam Interval Type of the lexicographical interval
     * @param func Callback function
     * @param key Key where the sorted set is stored
     * @param interval Interval object with lower() and upper() methods
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func, typename Interval>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    zlexcount(Func &&func, const std::string &key, const Interval &interval) {
        return static_cast<Derived &>(*this).template command<long long>(
            std::forward<Func>(func),
            "ZLEXCOUNT",
            key,
            interval.lower(),
            interval.upper());
    }

    /**
     * @brief Removes and returns members with the highest scores from a sorted set
     * 
     * @param key Key where the sorted set is stored
     * @param count Number of members to pop (default is 1)
     * @return Vector of member-score pairs that were removed
     */
    template <typename... Keys>
    std::vector<std::pair<std::string, double>>
    zpopmax(const std::string &key, long long count = 1) {
        return static_cast<Derived &>(*this)
            .template command<std::vector<std::pair<std::string, double>>>("ZPOPMAX", key, count)
            .result;
    }

    /**
     * @brief Asynchronous version of zpopmax
     * 
     * @tparam Func Callback function type
     * @tparam Keys Variadic types for key names
     * @param func Callback function
     * @param key Key where the sorted set is stored
     * @param count Number of members to pop (default is 1)
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func, typename... Keys>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::pair<std::string, double>>> &&>, Derived &>
    zpopmax(Func &&func, const std::string &key, long long count = 1) {
        return static_cast<Derived &>(*this).template command<std::vector<std::pair<std::string, double>>>(
            std::forward<Func>(func),
            "ZPOPMAX",
            key,
            count);
    }

    /**
     * @brief Removes and returns members with the lowest scores from a sorted set
     * 
     * @param key Key where the sorted set is stored
     * @param count Number of members to pop (default is 1)
     * @return Vector of member-score pairs that were removed
     */
    template <typename... Keys>
    std::vector<std::pair<std::string, double>>
    zpopmin(const std::string &key, long long count = 1) {
        return static_cast<Derived &>(*this)
            .template command<std::vector<std::pair<std::string, double>>>("ZPOPMIN", key, count)
            .result;
    }

    /**
     * @brief Asynchronous version of zpopmin
     * 
     * @tparam Func Callback function type
     * @tparam Keys Variadic types for key names
     * @param func Callback function
     * @param key Key where the sorted set is stored
     * @param count Number of members to pop (default is 1)
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func, typename... Keys>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::pair<std::string, double>>> &&>, Derived &>
    zpopmin(Func &&func, const std::string &key, long long count = 1) {
        return static_cast<Derived &>(*this).template command<std::vector<std::pair<std::string, double>>>(
            std::forward<Func>(func),
            "ZPOPMIN",
            key,
            count);
    }

    /**
     * @brief Gets members in a sorted set with their scores within a specified range of indices
     * 
     * @param key Key where the sorted set is stored
     * @param start Start index (0-based, can be negative to count from the end)
     * @param stop Stop index (inclusive, can be negative to count from the end)
     * @return Vector of member-score pairs within the range
     */
    std::vector<std::pair<std::string, double>>
    zrange(const std::string &key, long long start, long long stop) {
        return static_cast<Derived &>(*this)
            .template command<std::vector<std::pair<std::string, double>>>("ZRANGE", key, start, stop, "WITHSCORES")
            .result;
    }

    /**
     * @brief Asynchronous version of zrange
     * 
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the sorted set is stored
     * @param start Start index (0-based, can be negative to count from the end)
     * @param stop Stop index (inclusive, can be negative to count from the end)
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    Derived &
    zrange(Func &&func, const std::string &key, long long start, long long stop) {
        return static_cast<Derived &>(*this).template command<std::vector<std::pair<std::string, double>>>(
            std::forward<Func>(func),
            "ZRANGE",
            key,
            start,
            stop,
            "WITHSCORES");
    }

    /**
     * @brief Gets members in a sorted set that have scores within a lexicographical range
     * 
     * @tparam Interval Type of the lexicographical interval
     * @param key Key where the sorted set is stored
     * @param interval Interval object with lower() and upper() methods
     * @param opts Limit options for pagination
     * @return Vector of members within the lexicographical range
     */
    template <typename Interval>
    std::vector<std::string>
    zrangebylex(const std::string &key, Interval const &interval, const LimitOptions &opts = {}) {
        return static_cast<Derived &>(*this)
            .template command<std::vector<std::string>>(
                "ZRANGEBYLEX",
                key,
                interval.lower(),
                interval.upper(),
                "LIMIT",
                opts.offset,
                opts.count)
            .result;
    }

    /**
     * @brief Asynchronous version of zrangebylex
     * 
     * @tparam Func Callback function type
     * @tparam Interval Type of the lexicographical interval
     * @param func Callback function
     * @param key Key where the sorted set is stored
     * @param interval Interval object with lower() and upper() methods
     * @param opts Limit options for pagination
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func, typename Interval>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>, Derived &>
    zrangebylex(Func &&func, const std::string &key, Interval const &interval, const LimitOptions &opts = {}) {
        return static_cast<Derived &>(*this).template command<std::vector<std::string>>(
            std::forward<Func>(func),
            "ZRANGEBYLEX",
            key,
            interval.lower(),
            interval.upper(),
            "LIMIT",
            opts.offset,
            opts.count);
    }

    /**
     * @brief Gets members in a sorted set that have scores within a specified score range
     * 
     * @tparam Interval Type of the score interval
     * @param key Key where the sorted set is stored
     * @param interval Interval object with lower() and upper() methods
     * @param opts Limit options for pagination
     * @return Vector of member-score pairs within the score range
     */
    template <typename Interval>
    std::vector<std::pair<std::string, double>>
    zrangebyscore(const std::string &key, Interval const &interval, const LimitOptions &opts = {}) {
        return static_cast<Derived &>(*this)
            .template command<std::vector<std::pair<std::string, double>>>(
                "ZRANGEBYSCORE",
                key,
                interval.lower(),
                interval.upper(),
                "WITHSCORES",
                "LIMIT",
                opts.offset,
                opts.count)
            .result;
    }

    /**
     * @brief Asynchronous version of zrangebyscore
     * 
     * @tparam Func Callback function type
     * @tparam Interval Type of the score interval
     * @param func Callback function
     * @param key Key where the sorted set is stored
     * @param interval Interval object with lower() and upper() methods
     * @param opts Limit options for pagination
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func, typename Interval>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::pair<std::string, double>>> &&>, Derived &>
    zrangebyscore(Func &&func, const std::string &key, Interval const &interval, const LimitOptions &opts = {}) {
        return static_cast<Derived &>(*this).template command<std::vector<std::pair<std::string, double>>>(
            std::forward<Func>(func),
            "ZRANGEBYSCORE",
            key,
            interval.lower(),
            interval.upper(),
            "WITHSCORES",
            "LIMIT",
            opts.offset,
            opts.count);
    }

    /**
     * @brief Gets the rank of a member in a sorted set, with scores ordered from low to high
     * 
     * @param key Key where the sorted set is stored
     * @param member Member whose rank is requested
     * @return Optional rank of the member (0-based), or nullopt if member doesn't exist
     */
    std::optional<long long>
    zrank(const std::string &key, const std::string &member) {
        return static_cast<Derived &>(*this).template command<std::optional<long long>>("ZRANK", key, member).result;
    }

    /**
     * @brief Asynchronous version of zrank
     * 
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the sorted set is stored
     * @param member Member whose rank is requested
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    Derived &
    zrank(Func &&func, const std::string &key, const std::string &member) {
        return static_cast<Derived &>(*this)
            .template command<std::optional<long long>>(std::forward<Func>(func), "ZRANK", key, member);
    }

    /**
     * @brief Removes one or more members from a sorted set
     * 
     * @tparam Members Variadic types for member names
     * @param key Key where the sorted set is stored
     * @param members Members to remove
     * @return Number of members removed
     */
    template <typename... Members>
    long long
    zrem(const std::string &key, Members &&...members) {
        return static_cast<Derived &>(*this)
            .template command<long long>("ZREM", key, std::forward<Members>(members)...)
            .result;
    }

    /**
     * @brief Asynchronous version of zrem
     * 
     * @tparam Func Callback function type
     * @tparam Members Variadic types for member names
     * @param func Callback function
     * @param key Key where the sorted set is stored
     * @param members Members to remove
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func, typename... Members>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    zrem(Func &&func, const std::string &key, Members &&...members) {
        return static_cast<Derived &>(*this)
            .template command<long long>(std::forward<Func>(func), "ZREM", key, std::forward<Members>(members)...);
    }

    /**
     * @brief Removes members from a sorted set that are within a lexicographical range
     * 
     * @tparam Interval Type of the lexicographical interval
     * @param key Key where the sorted set is stored
     * @param interval Interval object with lower() and upper() methods
     * @return Number of members removed
     */
    template <typename Interval>
    long long
    zremrangebylex(const std::string &key, Interval const &interval) {
        return static_cast<Derived &>(*this)
            .template command<long long>("ZREMRANGEBYLEX", key, interval.lower(), interval.upper())
            .result;
    }

    /**
     * @brief Asynchronous version of zremrangebylex
     * 
     * @tparam Func Callback function type
     * @tparam Interval Type of the lexicographical interval
     * @param func Callback function
     * @param key Key where the sorted set is stored
     * @param interval Interval object with lower() and upper() methods
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func, typename Interval>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    zremrangebylex(Func &&func, const std::string &key, Interval const &interval) {
        return static_cast<Derived &>(*this).template command<long long>(
            std::forward<Func>(func),
            "ZREMRANGEBYLEX",
            key,
            interval.lower(),
            interval.upper());
    }

    /**
     * @brief Removes members from a sorted set that are within a range of indices
     * 
     * @param key Key where the sorted set is stored
     * @param start Start index (0-based, can be negative to count from the end)
     * @param stop Stop index (inclusive, can be negative to count from the end)
     * @return Number of members removed
     */
    long long
    zremrangebyrank(const std::string &key, long long start, long long stop) {
        return static_cast<Derived &>(*this).template command<long long>("ZREMRANGEBYRANK", key, start, stop).result;
    }

    /**
     * @brief Asynchronous version of zremrangebyrank
     * 
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the sorted set is stored
     * @param start Start index (0-based, can be negative to count from the end)
     * @param stop Stop index (inclusive, can be negative to count from the end)
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    Derived &
    zremrangebyrank(Func &&func, const std::string &key, long long start, long long stop) {
        return static_cast<Derived &>(*this)
            .template command<long long>(std::forward<Func>(func), "ZREMRANGEBYRANK", key, start, stop);
    }

    /**
     * @brief Removes members from a sorted set that have scores within a specified range
     * 
     * @tparam Interval Type of the score interval
     * @param key Key where the sorted set is stored
     * @param interval Interval object with lower() and upper() methods
     * @return Number of members removed
     */
    template <typename Interval>
    long long
    zremrangebyscore(const std::string &key, Interval const &interval) {
        return static_cast<Derived &>(*this)
            .template command<long long>("ZREMRANGEBYSCORE", key, interval.lower(), interval.upper())
            .result;
    }

    /**
     * @brief Asynchronous version of zremrangebyscore
     * 
     * @tparam Func Callback function type
     * @tparam Interval Type of the score interval
     * @param func Callback function
     * @param key Key where the sorted set is stored
     * @param interval Interval object with lower() and upper() methods
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func, typename Interval>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    zremrangebyscore(Func &&func, const std::string &key, Interval const &interval) {
        return static_cast<Derived &>(*this).template command<long long>(
            std::forward<Func>(func),
            "ZREMRANGEBYSCORE",
            key,
            interval.lower(),
            interval.upper());
    }

    /**
     * @brief Gets members in a sorted set with their scores within a specified range of indices, ordered from high to low
     * 
     * @param key Key where the sorted set is stored
     * @param start Start index (0-based, can be negative to count from the end)
     * @param stop Stop index (inclusive, can be negative to count from the end)
     * @return Vector of member-score pairs within the range
     */
    std::vector<std::pair<std::string, double>>
    zrevrange(const std::string &key, long long start, long long stop) {
        return static_cast<Derived &>(*this)
            .template command<std::vector<std::pair<std::string, double>>>("ZREVRANGE", key, start, stop, "WITHSCORES")
            .result;
    }

    /**
     * @brief Asynchronous version of zrevrange
     * 
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the sorted set is stored
     * @param start Start index (0-based, can be negative to count from the end)
     * @param stop Stop index (inclusive, can be negative to count from the end)
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    Derived &
    zrevrange(Func &&func, const std::string &key, long long start, long long stop) {
        return static_cast<Derived &>(*this).template command<std::vector<std::pair<std::string, double>>>(
            std::forward<Func>(func),
            "ZREVRANGE",
            key,
            start,
            stop,
            "WITHSCORES");
    }

    /**
     * @brief Gets members in a sorted set that have scores within a lexicographical range, ordered from high to low
     * 
     * @tparam Interval Type of the lexicographical interval
     * @param key Key where the sorted set is stored
     * @param interval Interval object with lower() and upper() methods
     * @param opt Limit options for pagination
     * @return Vector of members within the lexicographical range
     */
    template <typename Interval>
    std::vector<std::string>
    zrevrangebylex(const std::string &key, Interval const &interval, const LimitOptions &opt = {}) {
        return static_cast<Derived &>(*this)
            .template command<std::vector<std::string>>(
                "ZREVRANGEBYLEX",
                key,
                interval.upper(),
                interval.lower(),
                "LIMIT",
                opt.offset,
                opt.count)
            .result;
    }

    /**
     * @brief Asynchronous version of zrevrangebylex
     * 
     * @tparam Func Callback function type
     * @tparam Interval Type of the lexicographical interval
     * @param func Callback function
     * @param key Key where the sorted set is stored
     * @param interval Interval object with lower() and upper() methods
     * @param opt Limit options for pagination
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func, typename Interval>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>, Derived &>
    zrevrangebylex(Func &&func, const std::string &key, Interval const &interval, const LimitOptions &opt = {}) {
        return static_cast<Derived &>(*this).template command<std::vector<std::string>>(
            std::forward<Func>(func),
            "ZREVRANGEBYLEX",
            key,
            interval.upper(),
            interval.lower(),
            "LIMIT",
            opt.offset,
            opt.count);
    }

    /**
     * @brief Gets members in a sorted set that have scores within a specified score range, ordered from high to low
     * 
     * @tparam Interval Type of the score interval
     * @param key Key where the sorted set is stored
     * @param interval Interval object with lower() and upper() methods
     * @param opt Limit options for pagination
     * @return Vector of member-score pairs within the score range
     */
    template <typename Interval>
    std::vector<std::pair<std::string, double>>
    zrevrangebyscore(const std::string &key, Interval const &interval, const LimitOptions &opt = {}) {
        return static_cast<Derived &>(*this)
            .template command<std::vector<std::pair<std::string, double>>>(
                "ZREVRANGEBYSCORE",
                key,
                interval.upper(),
                interval.lower(),
                "WITHSCORES",
                "LIMIT",
                opt.offset,
                opt.count)
            .result;
    }

    /**
     * @brief Asynchronous version of zrevrangebyscore
     * 
     * @tparam Func Callback function type
     * @tparam Interval Type of the score interval
     * @param func Callback function
     * @param key Key where the sorted set is stored
     * @param interval Interval object with lower() and upper() methods
     * @param opt Limit options for pagination
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func, typename Interval>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::pair<std::string, double>>> &&>, Derived &>
    zrevrangebyscore(Func &&func, const std::string &key, Interval const &interval, const LimitOptions &opt = {}) {
        return static_cast<Derived &>(*this).template command<std::vector<std::pair<std::string, double>>>(
            std::forward<Func>(func),
            "ZREVRANGEBYSCORE",
            key,
            interval.upper(),
            interval.lower(),
            "WITHSCORES",
            "LIMIT",
            opt.offset,
            opt.count);
    }

    /**
     * @brief Gets the rank of a member in a sorted set, with scores ordered from high to low
     * 
     * @param key Key where the sorted set is stored
     * @param member Member whose rank is requested
     * @return Optional rank of the member (0-based), or nullopt if member doesn't exist
     */
    std::optional<long long>
    zrevrank(const std::string &key, const std::string &member) {
        return static_cast<Derived &>(*this).template command<std::optional<long long>>("ZREVRANK", key, member).result;
    }

    /**
     * @brief Asynchronous version of zrevrank
     * 
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the sorted set is stored
     * @param member Member whose rank is requested
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<long long>> &&>, Derived &>
    zrevrank(Func &&func, const std::string &key, const std::string &member) {
        return static_cast<Derived &>(*this)
            .template command<std::optional<long long>>(std::forward<Func>(func), "ZREVRANK", key, member);
    }

    /**
     * @brief Incrementally iterates members and scores in a sorted set
     * 
     * @param key Key where the sorted set is stored
     * @param cursor Cursor position to start iteration from
     * @param pattern Pattern to filter members
     * @param count Hint for how many members to return per iteration
     * @return Scan reply containing the next cursor and matching members with scores
     */
    reply::scan<qb::unordered_map<std::string, double>>
    zscan(const std::string &key, long long cursor, const std::string &pattern = "*", long long count = 10) {
        return static_cast<Derived &>(*this)
            .template command<reply::scan<qb::unordered_map<std::string, double>>>(
                "ZSCAN",
                key,
                cursor,
                "MATCH",
                pattern,
                "COUNT",
                count)
            .result;
    }

    /**
     * @brief Asynchronous version of zscan
     * 
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the sorted set is stored
     * @param cursor Cursor position to start iteration from
     * @param pattern Pattern to filter members
     * @param count Hint for how many members to return per iteration
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<
        std::is_invocable_v<Func, Reply<reply::scan<qb::unordered_map<std::string, double>>> &&>, Derived &>
    zscan(
        Func &&func, const std::string &key, long long cursor, const std::string &pattern = "*", long long count = 10) {
        return static_cast<Derived &>(*this).template command<reply::scan<qb::unordered_map<std::string, double>>>(
            std::forward<Func>(func),
            "ZSCAN",
            key,
            cursor,
            "MATCH",
            pattern,
            "COUNT",
            count);
    }

    /**
     * @brief Simplified version of zscan that handles iteration automatically
     * 
     * @tparam Func Callback function type
     * @param func Callback function to receive all results after iteration completes
     * @param key Key where the sorted set is stored
     * @param pattern Pattern to filter members
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<
        std::is_invocable_v<Func, Reply<reply::scan<qb::unordered_map<std::string, double>>> &&>, Derived &>
    zscan(Func &&func, std::string const &key, const std::string &pattern = "*") {
        new scanner<Func>(static_cast<Derived &>(*this), key, pattern, std::forward<Func>(func));
        return static_cast<Derived &>(*this);
    }

    /**
     * @brief Gets the score of a member in a sorted set
     * 
     * @param key Key where the sorted set is stored
     * @param member Member whose score is requested
     * @return Optional score of the member, or nullopt if member doesn't exist
     */
    std::optional<double>
    zscore(const std::string &key, const std::string &member) {
        return static_cast<Derived &>(*this).template command<std::optional<double>>("ZSCORE", key, member).result;
    }

    /**
     * @brief Asynchronous version of zscore
     * 
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the sorted set is stored
     * @param member Member whose score is requested
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    Derived &
    zscore(Func &&func, const std::string &key, const std::string &member) {
        return static_cast<Derived &>(*this)
            .template command<std::optional<double>>(std::forward<Func>(func), "ZSCORE", key, member);
    }

    /**
     * @brief Computes the union of multiple sorted sets and stores the result in a new key
     * 
     * @param destination Key where the resulting sorted set will be stored
     * @param keys Vector of keys where the source sorted sets are stored
     * @param weights Vector of weights to apply to each sorted set
     * @param type Aggregation type (SUM, MIN, or MAX)
     * @return Number of members in the resulting sorted set
     */
    inline long long
    zunionstore(
        const std::string &destination, const std::vector<std::string> &keys, const std::vector<double> &weights = {},
        Aggregation type = Aggregation::SUM) {
        std::optional<std::string> opt;
        if (!weights.empty())
            opt = "WEIGHTS";
        return static_cast<Derived &>(*this)
            .template command<long long>(
                "ZUNIONSTORE",
                destination,
                keys.size(),
                keys,
                "WEIGHTS",
                weights,
                "AGGREGATE",
                std::to_string(type))
            .result;
    }

    /**
     * @brief Asynchronous version of zunionstore
     * 
     * @tparam Func Callback function type
     * @param func Callback function
     * @param destination Key where the resulting sorted set will be stored
     * @param keys Vector of keys where the source sorted sets are stored
     * @param weights Vector of weights to apply to each sorted set
     * @param type Aggregation type (SUM, MIN, or MAX)
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    zunionstore(
        Func &&func, const std::string &destination, const std::vector<std::string> &keys,
        const std::vector<double> &weights = {}, Aggregation type = Aggregation::SUM) {
        std::optional<std::string> opt;
        if (!weights.empty())
            opt = "WEIGHTS";
        return static_cast<Derived &>(*this).template command<long long>(
            std::forward<Func>(func),
            "ZUNIONSTORE",
            destination,
            keys.size(),
            keys,
            opt,
            weights,
            "AGGREGATE",
            std::to_string(type));
    }

    /**
     * @brief Simplified version of zunionstore for a single source set
     * 
     * @param destination Key where the resulting sorted set will be stored
     * @param key Key where the source sorted set is stored
     * @param weight Weight to apply to the sorted set
     * @param type Aggregation type (SUM, MIN, or MAX)
     * @return Number of members in the resulting sorted set
     */
    inline long long
    zunionstore(
        const std::string &destination, const std::string &key, double weight = 1.,
        Aggregation type = Aggregation::SUM) {
        std::optional<std::string> opt;
        return zunionstore(destination, std::vector{key}, std::vector{weight}, type);
    }

    /**
     * @brief Asynchronous version of simplified zunionstore
     * 
     * @tparam Func Callback function type
     * @param func Callback function
     * @param destination Key where the resulting sorted set will be stored
     * @param key Key where the source sorted set is stored
     * @param weight Weight to apply to the sorted set
     * @param type Aggregation type (SUM, MIN, or MAX)
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    zunionstore(
        Func &&func, const std::string &destination, const std::string &key, double weight = 1.,
        Aggregation type = Aggregation::SUM) {
        return zunionstore(std::forward<Func>(func), destination, std::vector{key}, std::vector{weight}, type);
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_SORTED_SET_COMMANDS_H
