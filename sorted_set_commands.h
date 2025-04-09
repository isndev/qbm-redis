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
private:
    constexpr Derived &
    derived() {
        return static_cast<Derived &>(*this);
    }

    /**
     * @class scanner
     * @brief Helper class for implementing incremental scanning of sorted sets
     *
     * @tparam Func Callback function type
     */
    template <typename Func>
    class scanner {
        Derived    &_handler;
        std::string _key;
        std::string _pattern;
        Func        _func;
        size_t      _cursor{0};
        qb::redis::Reply<qb::redis::scan<qb::unordered_map<std::string, double>>> _reply;

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
        operator()(
            qb::redis::Reply<qb::redis::scan<qb::unordered_map<std::string, double>>>
                &&reply) {
            _reply.ok() = reply.ok();
            std::move(reply.result().items.begin(), reply.result().items.end(),
                      std::inserter(_reply.result().items, _reply.result().items.end()));
            if (reply.ok() && reply.result().cursor)
                _handler.zscan(std::ref(*this), _key, reply.result().cursor, _pattern,
                               100);
            else {
                _func(std::move(_reply));
                delete this;
            }
        }
    };

public:
    /**
     * @brief Removes and returns the member with the highest score from a sorted set,
     * blocking if set is empty
     *
     * @param keys Keys where sorted sets are stored
     * @param timeout Timeout in seconds, 0 means block forever
     * @return Optional tuple containing key name, member name, and score
     */
    std::optional<std::tuple<std::string, std::string, double>>
    bzpopmax(const std::vector<std::string> &keys, long long timeout) {
        return derived()
            .template command<
                std::optional<std::tuple<std::string, std::string, double>>>(
                "BZPOPMAX", keys, timeout)
            .result();
    }

    /**
     * @brief Asynchronous version of bzpopmax
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param keys Keys where sorted sets are stored
     * @param timeout Timeout in seconds, 0 means block forever
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<
        std::is_invocable_v<
            Func, Reply<std::optional<std::tuple<std::string, std::string, double>>> &&>,
        Derived &>
    bzpopmax(Func &&func, const std::vector<std::string> &keys, long long timeout) {
        return derived()
            .template command<
                std::optional<std::tuple<std::string, std::string, double>>>(
                std::forward<Func>(func), "BZPOPMAX", keys, timeout);
    }

    /**
     * @brief Overload of bzpopmax accepting std::chrono::seconds for timeout
     *
     * @param keys Keys where sorted sets are stored
     * @param timeout Timeout as std::chrono::seconds, 0 means block forever
     * @return Optional tuple containing key name, member name, and score
     */
    std::optional<std::tuple<std::string, std::string, double>>
    bzpopmax(const std::vector<std::string> &keys,
             const std::chrono::seconds     &timeout = std::chrono::seconds{0}) {
        return bzpopmax(keys, timeout.count());
    }

    /**
     * @brief Asynchronous version of bzpopmax with std::chrono::seconds
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param keys Keys where sorted sets are stored
     * @param timeout Timeout as std::chrono::seconds, 0 means block forever
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<
        std::is_invocable_v<
            Func, Reply<std::optional<std::tuple<std::string, std::string, double>>> &&>,
        Derived &>
    bzpopmax(Func &&func, const std::vector<std::string> &keys,
             const std::chrono::seconds &timeout = std::chrono::seconds{0}) {
        return bzpopmax(std::forward<Func>(func), keys, timeout.count());
    }

    /**
     * @brief Removes and returns the member with the lowest score from a sorted set,
     * blocking if set is empty
     *
     * @param keys Keys where sorted sets are stored
     * @param timeout Timeout in seconds, 0 means block forever
     * @return Optional tuple containing key name, member name, and score
     */
    std::optional<std::tuple<std::string, std::string, double>>
    bzpopmin(const std::vector<std::string> &keys, long long timeout) {
        return derived()
            .template command<
                std::optional<std::tuple<std::string, std::string, double>>>(
                "BZPOPMIN", keys, timeout)
            .result();
    }

    /**
     * @brief Asynchronous version of bzpopmin
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param keys Keys where sorted sets are stored
     * @param timeout Timeout in seconds, 0 means block forever
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<
        std::is_invocable_v<
            Func, Reply<std::optional<std::tuple<std::string, std::string, double>>> &&>,
        Derived &>
    bzpopmin(Func &&func, const std::vector<std::string> &keys, long long timeout) {
        return derived()
            .template command<
                std::optional<std::tuple<std::string, std::string, double>>>(
                std::forward<Func>(func), "BZPOPMIN", keys, timeout);
    }

    /**
     * @brief Overload of bzpopmin accepting std::chrono::seconds for timeout
     *
     * @param keys Keys where sorted sets are stored
     * @param timeout Timeout as std::chrono::seconds, 0 means block forever
     * @return Optional tuple containing key name, member name, and score
     */
    std::optional<std::tuple<std::string, std::string, double>>
    bzpopmin(const std::vector<std::string> &keys,
             const std::chrono::seconds     &timeout = std::chrono::seconds{0}) {
        return bzpopmin(keys, timeout.count());
    }

    /**
     * @brief Asynchronous version of bzpopmin with std::chrono::seconds
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param keys Keys where sorted sets are stored
     * @param timeout Timeout as std::chrono::seconds, 0 means block forever
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<
        std::is_invocable_v<
            Func, Reply<std::optional<std::tuple<std::string, std::string, double>>> &&>,
        Derived &>
    bzpopmin(Func &&func, const std::vector<std::string> &keys,
             const std::chrono::seconds &timeout = std::chrono::seconds{0}) {
        return bzpopmin(std::forward<Func>(func), keys, timeout.count());
    }

    /**
     * @brief Adds one or more members to a sorted set, or updates the score if member
     * already exists
     *
     * @param key Key where the sorted set is stored
     * @param members Vector of score_member objects to add
     * @param type Update type (ALWAYS, EXIST, or NOT_EXIST)
     * @param changed If true, return number of changed elements, not just new elements
     * @return Number of elements added to the sorted set
     */
    long long
    zadd(const std::string &key, const std::vector<score_member> &members,
         UpdateType type = UpdateType::ALWAYS, bool changed = false) {
        std::optional<std::string> opt_up, opt_ch;

        if (type != UpdateType::ALWAYS)
            opt_up = std::to_string(type);

        if (changed)
            opt_ch = "CH";

        return derived()
            .template command<long long>("ZADD", key, opt_up, opt_ch, members)
            .result();
    }

    /**
     * @brief Asynchronous version of zadd
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the sorted set is stored
     * @param members Vector of score_member objects to add
     * @param type Update type (ALWAYS, EXIST, or NOT_EXIST)
     * @param changed If true, return number of changed elements, not just new elements
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    zadd(Func &&func, const std::string &key, const std::vector<score_member> &members,
         UpdateType type = UpdateType::ALWAYS, bool changed = false) {
        std::optional<std::string> opt_up, opt_ch;

        if (type != UpdateType::ALWAYS)
            opt_up = std::to_string(type);

        if (changed)
            opt_ch = "CH";
        return derived().template command<long long>(std::forward<Func>(func), "ZADD",
                                                     key, opt_up, opt_ch, members);
    }

    /**
     * @brief Gets the number of members in a sorted set
     *
     * @param key Key where the sorted set is stored
     * @return Number of members in the sorted set
     */
    long long
    zcard(const std::string &key) {
        return derived().template command<long long>("ZCARD", key).result();
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    zcard(Func &&func, const std::string &key) {
        return derived().template command<long long>(std::forward<Func>(func), "ZCARD",
                                                     key);
    }

    /**
     * @brief Counts the number of members in a sorted set with scores within the given
     * interval
     *
     * @tparam Interval Type of the score interval
     * @param key Key where the sorted set is stored
     * @param interval Interval object with lower() and upper() methods
     * @return Number of members in the sorted set with scores in the interval
     */
    template <typename Interval>
    long long
    zcount(const std::string &key, const Interval &interval) {
        return derived()
            .template command<long long>("ZCOUNT", key, interval.lower(),
                                         interval.upper())
            .result();
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
        return derived().template command<long long>(
            std::forward<Func>(func), "ZCOUNT", key, interval.lower(), interval.upper());
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
        return derived()
            .template command<double>("ZINCRBY", key, increment, member)
            .result();
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<double> &&>, Derived &>
    zincrby(Func &&func, const std::string &key, double increment,
            const std::string &member) {
        return derived().template command<double>(std::forward<Func>(func), "ZINCRBY",
                                                  key, increment, member);
    }

    /**
     * @brief Computes the union of multiple sorted sets and stores the result in a new
     * key
     *
     * @param destination Key where the resulting sorted set will be stored
     * @param keys Keys where the source sorted sets are stored
     * @param weights Vector of weights to apply to each sorted set
     * @param type Aggregation type (SUM, MIN, or MAX)
     * @return Number of members in the resulting sorted set
     */
    long long
    zunionstore(const std::string &destination, const std::vector<std::string> &keys,
                const std::vector<double> &weights = {},
                Aggregation                type    = Aggregation::SUM) {
        std::optional<std::string> opt;
        if (!weights.empty())
            opt = "WEIGHTS";
        return derived()
            .template command<long long>("ZUNIONSTORE", destination, keys.size(), keys,
                                         opt, weights, "AGGREGATE", std::to_string(type))
            .result();
    }

    /**
     * @brief Asynchronous version of zunionstore
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param destination Key where the resulting sorted set will be stored
     * @param keys Keys where the source sorted sets are stored
     * @param weights Vector of weights to apply to each sorted set
     * @param type Aggregation type (SUM, MIN, or MAX)
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    zunionstore(Func &&func, const std::string &destination,
                const std::vector<std::string> &keys,
                const std::vector<double>      &weights = {},
                Aggregation                     type    = Aggregation::SUM) {
        std::optional<std::string> opt;
        if (!weights.empty())
            opt = "WEIGHTS";
        return derived().template command<long long>(
            std::forward<Func>(func), "ZUNIONSTORE", destination, keys.size(), keys, opt,
            weights, "AGGREGATE", std::to_string(type));
    }

    /**
     * @brief Intersects multiple sorted sets and stores the result in a new key
     *
     * @param destination Key where the resulting sorted set will be stored
     * @param keys Keys where the source sorted sets are stored
     * @param weights Vector of weights to apply to each sorted set
     * @param type Aggregation type (SUM, MIN, or MAX)
     * @return Number of members in the resulting sorted set
     */
    long long
    zinterstore(const std::string &destination, const std::vector<std::string> &keys,
                const std::vector<double> &weights = {},
                Aggregation                type    = Aggregation::SUM) {
        std::optional<std::string> opt;
        if (!weights.empty())
            opt = "WEIGHTS";
        return derived()
            .template command<long long>("ZINTERSTORE", destination, keys.size(), keys,
                                         opt, weights, "AGGREGATE", std::to_string(type))
            .result();
    }

    /**
     * @brief Asynchronous version of zinterstore
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param destination Key where the resulting sorted set will be stored
     * @param keys Keys where the source sorted sets are stored
     * @param weights Vector of weights to apply to each sorted set
     * @param type Aggregation type (SUM, MIN, or MAX)
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    zinterstore(Func &&func, const std::string &destination,
                const std::vector<std::string> &keys,
                const std::vector<double>      &weights = {},
                Aggregation                     type    = Aggregation::SUM) {
        std::optional<std::string> opt;
        if (!weights.empty())
            opt = "WEIGHTS";
        return derived().template command<long long>(
            std::forward<Func>(func), "ZINTERSTORE", destination, keys.size(), keys, opt,
            weights, "AGGREGATE", std::to_string(type));
    }

    /**
     * @brief Counts the number of members in a sorted set between a lexicographical
     * range
     *
     * @tparam Interval Type of the lexicographical interval
     * @param key Key where the sorted set is stored
     * @param interval Interval object with lower() and upper() methods
     * @return Number of members in the sorted set within the lexicographical range
     */
    template <typename Interval>
    long long
    zlexcount(const std::string &key, const Interval &interval) {
        return derived()
            .template command<long long>("ZLEXCOUNT", key, interval.lower(),
                                         interval.upper())
            .result();
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
        return derived().template command<long long>(std::forward<Func>(func),
                                                     "ZLEXCOUNT", key, interval.lower(),
                                                     interval.upper());
    }

    /**
     * @brief Removes and returns members with the highest scores from a sorted set
     *
     * @param key Key where the sorted set is stored
     * @param count Number of members to pop (default is 1)
     * @return Vector of score_member objects that were removed
     */
    std::vector<score_member>
    zpopmax(const std::string &key, long long count = 1) {
        return derived()
            .template command<std::vector<score_member>>("ZPOPMAX", key, count)
            .result();
    }

    /**
     * @brief Asynchronous version of zpopmax
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the sorted set is stored
     * @param count Number of members to pop (default is 1)
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<score_member>> &&>,
                     Derived &>
    zpopmax(Func &&func, const std::string &key, long long count = 1) {
        return derived().template command<std::vector<score_member>>(
            std::forward<Func>(func), "ZPOPMAX", key, count);
    }

    /**
     * @brief Removes and returns members with the lowest scores from a sorted set
     *
     * @param key Key where the sorted set is stored
     * @param count Number of members to pop (default is 1)
     * @return Vector of score_member objects that were removed
     */
    std::vector<score_member>
    zpopmin(const std::string &key, long long count = 1) {
        return derived()
            .template command<std::vector<score_member>>("ZPOPMIN", key, count)
            .result();
    }

    /**
     * @brief Asynchronous version of zpopmin
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the sorted set is stored
     * @param count Number of members to pop (default is 1)
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<score_member>> &&>,
                     Derived &>
    zpopmin(Func &&func, const std::string &key, long long count = 1) {
        return derived().template command<std::vector<score_member>>(
            std::forward<Func>(func), "ZPOPMIN", key, count);
    }

    /**
     * @brief Gets members in a sorted set with their scores within a specified range of
     * indices
     *
     * @param key Key where the sorted set is stored
     * @param start Start index (0-based, can be negative to count from the end)
     * @param stop Stop index (inclusive, can be negative to count from the end)
     * @return Vector of member-score pairs within the range
     */
    std::vector<score_member>
    zrange(const std::string &key, long long start, long long stop) {
        return derived()
            .template command<std::vector<score_member>>("ZRANGE", key, start, stop,
                                                         "WITHSCORES")
            .result();
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<score_member>> &&>,
                     Derived &>
    zrange(Func &&func, const std::string &key, long long start, long long stop) {
        return derived().template command<std::vector<score_member>>(
            std::forward<Func>(func), "ZRANGE", key, start, stop, "WITHSCORES");
    }

    /**
     * @brief Gets members in a sorted set that have scores within a lexicographical
     * range
     *
     * @tparam Interval Type of the lexicographical interval
     * @param key Key where the sorted set is stored
     * @param interval Interval object with lower() and upper() methods
     * @param opts Limit options for pagination
     * @return Vector of members within the lexicographical range
     */
    template <typename Interval>
    std::vector<std::string>
    zrangebylex(const std::string &key, Interval const &interval,
                const LimitOptions &opts = {}) {
        return derived()
            .template command<std::vector<std::string>>(
                "ZRANGEBYLEX", key, interval.lower(), interval.upper(), "LIMIT",
                opts.offset, opts.count)
            .result();
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>,
                     Derived &>
    zrangebylex(Func &&func, const std::string &key, Interval const &interval,
                const LimitOptions &opts = {}) {
        return derived().template command<std::vector<std::string>>(
            std::forward<Func>(func), "ZRANGEBYLEX", key, interval.lower(),
            interval.upper(), opts.offset >= 0 ? "LIMIT" : "",
            opts.offset >= 0 ? std::to_string(opts.offset) : "",
            opts.offset >= 0 ? std::to_string(opts.count) : "");
    }

    /**
     * @brief Gets members in a sorted set that have scores within a specified score
     * range
     *
     * @tparam Interval Type of the score interval
     * @param key Key where the sorted set is stored
     * @param interval Interval object with lower() and upper() methods
     * @param opts Limit options for pagination
     * @return Vector of member-score pairs within the score range
     */
    template <typename Interval>
    std::vector<score_member>
    zrangebyscore(const std::string &key, Interval const &interval,
                  const LimitOptions &opts = {}) {
        return derived()
            .template command<std::vector<score_member>>(
                "ZRANGEBYSCORE", key, interval.lower(), interval.upper(), "WITHSCORES",
                "LIMIT", opts.offset, opts.count)
            .result();
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<score_member>> &&>,
                     Derived &>
    zrangebyscore(Func &&func, const std::string &key, Interval const &interval,
                  const LimitOptions &opts = {}) {
        return derived().template command<std::vector<score_member>>(
            std::forward<Func>(func), "ZRANGEBYSCORE", key, interval.lower(),
            interval.upper(), opts.offset >= 0 ? "LIMIT" : "",
            opts.offset >= 0 ? std::to_string(opts.offset) : "",
            opts.offset >= 0 ? std::to_string(opts.count) : "", "WITHSCORES");
    }

    /**
     * @brief Gets the rank of a member in a sorted set, with scores ordered from low to
     * high
     *
     * @param key Key where the sorted set is stored
     * @param member Member whose rank is requested
     * @return Optional rank of the member (0-based), or nullopt if member doesn't exist
     */
    std::optional<long long>
    zrank(const std::string &key, const std::string &member) {
        return derived()
            .template command<std::optional<long long>>("ZRANK", key, member)
            .result();
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<long long>> &&>,
                     Derived &>
    zrank(Func &&func, const std::string &key, const std::string &member) {
        return derived().template command<std::optional<long long>>(
            std::forward<Func>(func), "ZRANK", key, member);
    }

    /**
     * @brief Removes one or more members from a sorted set
     *
     * @param key Key where the sorted set is stored
     * @param members Initializer list of members to remove
     * @return Number of members removed from the sorted set
     */
    long long
    zrem(const std::string &key, const std::vector<std::string> &members) {
        return derived().template command<long long>("ZREM", key, members).result();
    }

    /**
     * @brief Asynchronous version of zrem
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the sorted set is stored
     * @param members Initializer list of members to remove
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    zrem(Func &&func, const std::string &key, const std::vector<std::string> &members) {
        return derived().template command<long long>(std::forward<Func>(func), "ZREM",
                                                     key, members);
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
        return derived()
            .template command<long long>("ZREMRANGEBYLEX", key, interval.lower(),
                                         interval.upper())
            .result();
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
        return derived().template command<long long>(std::forward<Func>(func),
                                                     "ZREMRANGEBYLEX", key,
                                                     interval.lower(), interval.upper());
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
        return derived()
            .template command<long long>("ZREMRANGEBYRANK", key, start, stop)
            .result();
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    zremrangebyrank(Func &&func, const std::string &key, long long start,
                    long long stop) {
        return derived().template command<long long>(
            std::forward<Func>(func), "ZREMRANGEBYRANK", key, start, stop);
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
        return derived()
            .template command<long long>("ZREMRANGEBYSCORE", key, interval.lower(),
                                         interval.upper())
            .result();
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
        return derived().template command<long long>(std::forward<Func>(func),
                                                     "ZREMRANGEBYSCORE", key,
                                                     interval.lower(), interval.upper());
    }

    /**
     * @brief Gets members in a sorted set with their scores within a specified range of
     * indices, ordered from high to low
     *
     * @param key Key where the sorted set is stored
     * @param start Start index (0-based, can be negative to count from the end)
     * @param stop Stop index (inclusive, can be negative to count from the end)
     * @return Vector of member-score pairs within the range
     */
    std::vector<score_member>
    zrevrange(const std::string &key, long long start, long long stop) {
        return derived()
            .template command<std::vector<score_member>>("ZREVRANGE", key, start, stop,
                                                         "WITHSCORES")
            .result();
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<score_member>> &&>,
                     Derived &>
    zrevrange(Func &&func, const std::string &key, long long start, long long stop) {
        return derived().template command<std::vector<score_member>>(
            std::forward<Func>(func), "ZREVRANGE", key, start, stop, "WITHSCORES");
    }

    /**
     * @brief Gets members in a sorted set that have scores within a lexicographical
     * range, ordered from high to low
     *
     * @tparam Interval Type of the lexicographical interval
     * @param key Key where the sorted set is stored
     * @param interval Interval object with lower() and upper() methods
     * @param opt Limit options for pagination
     * @return Vector of members within the lexicographical range
     */
    template <typename Interval>
    std::vector<std::string>
    zrevrangebylex(const std::string &key, Interval const &interval,
                   const LimitOptions &opt = {}) {
        return derived()
            .template command<std::vector<std::string>>(
                "ZREVRANGEBYLEX", key, interval.upper(), interval.lower(), "LIMIT",
                opt.offset, opt.count)
            .result();
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>,
                     Derived &>
    zrevrangebylex(Func &&func, const std::string &key, Interval const &interval,
                   const LimitOptions &opt = {}) {
        return derived().template command<std::vector<std::string>>(
            std::forward<Func>(func), "ZREVRANGEBYLEX", key, interval.upper(),
            interval.lower(), "LIMIT", opt.offset, opt.count);
    }

    /**
     * @brief Gets members in a sorted set that have scores within a specified score
     * range, ordered from high to low
     *
     * @tparam Interval Type of the score interval
     * @param key Key where the sorted set is stored
     * @param interval Interval object with lower() and upper() methods
     * @param opt Limit options for pagination
     * @return Vector of member-score pairs within the score range
     */
    template <typename Interval>
    std::vector<score_member>
    zrevrangebyscore(const std::string &key, Interval const &interval,
                     const LimitOptions &opt = {}) {
        return derived()
            .template command<std::vector<score_member>>(
                "ZREVRANGEBYSCORE", key, interval.upper(), interval.lower(),
                "WITHSCORES", "LIMIT", opt.offset, opt.count)
            .result();
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<score_member>> &&>,
                     Derived &>
    zrevrangebyscore(Func &&func, const std::string &key, Interval const &interval,
                     const LimitOptions &opt = {}) {
        return derived().template command<std::vector<score_member>>(
            std::forward<Func>(func), "ZREVRANGEBYSCORE", key, interval.upper(),
            interval.lower(), "WITHSCORES", "LIMIT", opt.offset, opt.count);
    }

    /**
     * @brief Gets the rank of a member in a sorted set, with scores ordered from high to
     * low
     *
     * @param key Key where the sorted set is stored
     * @param member Member whose rank is requested
     * @return Optional rank of the member (0-based), or nullopt if member doesn't exist
     */
    std::optional<long long>
    zrevrank(const std::string &key, const std::string &member) {
        return derived()
            .template command<std::optional<long long>>("ZREVRANK", key, member)
            .result();
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<long long>> &&>,
                     Derived &>
    zrevrank(Func &&func, const std::string &key, const std::string &member) {
        return derived().template command<std::optional<long long>>(
            std::forward<Func>(func), "ZREVRANK", key, member);
    }

    /**
     * @brief Incrementally iterate sorted set elements and their scores
     *
     * @param key Key where the sorted set is stored
     * @param cursor Cursor position to start iteration from
     * @param pattern Pattern to filter members
     * @param count Hint for how many elements to return per call
     * @return Scan result containing next cursor and member-score pairs
     */
    qb::redis::scan<qb::unordered_map<std::string, double>>
    zscan(const std::string &key, long long cursor, const std::string &pattern = "*",
          long long count = 10) {
        if (key.empty()) {
            return {};
        }
        return derived()
            .template command<qb::redis::scan<qb::unordered_map<std::string, double>>>(
                "ZSCAN", key, cursor, "MATCH", pattern, "COUNT", count)
            .result();
    }

    /**
     * @brief Asynchronous version of zscan
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the sorted set is stored
     * @param cursor Cursor position to start iteration from
     * @param pattern Pattern to filter members
     * @param count Hint for how many elements to return per call
     * @return Reference to the derived class
     */
    template <typename Func>
    std::enable_if_t<
        std::is_invocable_v<
            Func, Reply<qb::redis::scan<qb::unordered_map<std::string, double>>> &&>,
        Derived &>
    zscan(Func &&func, const std::string &key, long long cursor,
          const std::string &pattern = "*", long long count = 10) {
        if (key.empty()) {
            return derived();
        }
        return derived()
            .template command<qb::redis::scan<qb::unordered_map<std::string, double>>>(
                std::forward<Func>(func), "ZSCAN", key, cursor, "MATCH", pattern,
                "COUNT", count);
    }

    /**
     * @brief Automatically iterates through all sorted set elements matching a pattern
     *
     * This version manages cursor iteration internally, collecting all results
     * and calling the callback once with the complete result set.
     *
     * @tparam Func Callback function type
     * @param func Callback function to process complete results
     * @param key Key where the sorted set is stored
     * @param pattern Pattern to filter members
     * @return Reference to the derived class
     */
    template <typename Func>
    std::enable_if_t<
        std::is_invocable_v<
            Func, Reply<qb::redis::scan<qb::unordered_map<std::string, double>>> &&>,
        Derived &>
    zscan(Func &&func, const std::string &key, const std::string &pattern = "*") {
        new scanner<Func>(derived(), key, pattern, std::forward<Func>(func));
        return derived();
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
        return derived()
            .template command<std::optional<double>>("ZSCORE", key, member)
            .result();
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<double>> &&>,
                     Derived &>
    zscore(Func &&func, const std::string &key, const std::string &member) {
        return derived().template command<std::optional<double>>(
            std::forward<Func>(func), "ZSCORE", key, member);
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_SORTED_SET_COMMANDS_H
