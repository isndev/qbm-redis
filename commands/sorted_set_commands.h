/**
 * @file qbm/redis/commands/sorted_set_commands.h
 * @brief Redis sorted set command mixin for the qb Redis module.
 *
 * Provides the @c sorted_set_commands CRTP mixin, exposing the full Redis
 * sorted-set command family (ZADD, ZRANGE, ZRANGEBYSCORE, ZPOP*, ZSCAN,
 * set-algebra such as ZUNIONSTORE/ZINTER/ZDIFF, and the blocking BZ* variants).
 * Each command is offered both as a coroutine-awaitable form and as a
 * callback-based asynchronous form for the qb actor event loop.
 *
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2026 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)
 * @ingroup Redis
 */
#ifndef QBM_REDIS_SORTED_SET_COMMANDS_H
#define QBM_REDIS_SORTED_SET_COMMANDS_H
#include <chrono>
#include <utility>
#include "../reply.h"

namespace qb::redis {

/**
 * @class sorted_set_commands
 * @brief Provides Redis sorted set command implementations.
 *
 * This class implements Redis sorted set operations, which provide an ordered collection
 * of unique strings, sorted by associated scores. Commands return awaiters for coroutine-first async I/O.
 *
 * Redis sorted sets are particularly useful for ranking data, implementing leaderboards,
 * and efficiently retrieving ranges of elements based on their ordering.
 *
 * @tparam Derived The derived class type (CRTP pattern)
 */
template <typename Derived>
class sorted_set_commands {
private:
    /**
     * @brief Returns the derived handler via the CRTP downcast.
     * @return Reference to the @c Derived instance.
     */
    constexpr Derived &
    derived() {
        return static_cast<Derived &>(*this);
    }

    /**
     * @class scanner
     * @brief Helper class for implementing incremental scanning of sorted sets
     *
     * Uses shared_ptr for automatic memory management. Safe even if exceptions occur.
     *
     * @tparam Func Callback function type
     */
    template <typename Func>
    class scanner : public std::enable_shared_from_this<scanner<Func>> {
        Derived                                                                  &_handler;
        std::string                                                               _key;
        std::string                                                               _pattern;
        Func                                                                      _func;
        qb::redis::Reply<qb::redis::scan<qb::unordered_map<std::string, double>>> _reply;
        bool                                                                      _started{false};

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
            , _func(std::forward<Func>(func)) {}

        /**
         * @brief Start the scanning process
         */
        void
        start() {
            if (!_started) {
                _started  = true;
                auto self = this->shared_from_this();
                _handler.zscan([self](auto &&reply) { (*self)(std::forward<decltype(reply)>(reply)); }, _key, 0, _pattern, 100);
            }
        }

        /**
         * @brief Processes scan results and continues scanning if needed
         *
         * @param reply The scan operation reply
         */
        void
        operator()(qb::redis::Reply<qb::redis::scan<qb::unordered_map<std::string, double>>> &&reply) {
            _reply.ok() = reply.ok();
            std::move(reply.result().items.begin(), reply.result().items.end(),
                      std::inserter(_reply.result().items, _reply.result().items.end()));
            if (reply.ok() && reply.result().cursor) {
                auto self = this->shared_from_this();
                _handler.zscan([self](auto &&reply) { (*self)(std::forward<decltype(reply)>(reply)); }, _key, reply.result().cursor, _pattern,
                               100);
            } else {
                try {
                    _func(std::move(_reply));
                } catch (std::exception const &e) {
                    LOG_WARN("[qbm][redis] sorted_set scanner callback failed: " << e.what());
                }
            }
        }

        /**
         * @brief Factory method to create and start a scanner safely
         */
        static void
        create_and_start(Derived &handler, std::string key, std::string pattern, Func &&func) {
            auto ptr = std::make_shared<scanner>(handler, std::move(key), std::move(pattern), std::forward<Func>(func));
            ptr->start();
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
    auto
    bzpopmax(const std::vector<std::string> &keys, long long timeout) {
        return derived().template make_coro_command<std::optional<std::tuple<std::string, std::string, double>>>(
            [this, keys, timeout](auto &&callback) { this->bzpopmax(std::move(callback), keys, timeout); });
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::tuple<std::string, std::string, double>>> &&>, Derived &>
    bzpopmax(Func &&func, const std::vector<std::string> &keys, long long timeout) {
        return derived().template command<std::optional<std::tuple<std::string, std::string, double>>>(std::forward<Func>(func), "BZPOPMAX",
                                                                                                       keys, timeout);
    }

    /**
     * @brief Coroutine-awaitable BZPOPMAX accepting a std::chrono timeout.
     *
     * @param keys Keys where sorted sets are stored.
     * @param timeout Blocking timeout; 0 means block forever.
     * @return Awaiter yielding an optional tuple of (key, member, score).
     */
    auto
    bzpopmax(const std::vector<std::string> &keys, const std::chrono::seconds &timeout = std::chrono::seconds{0}) {
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::tuple<std::string, std::string, double>>> &&>, Derived &>
    bzpopmax(Func &&func, const std::vector<std::string> &keys, const std::chrono::seconds &timeout = std::chrono::seconds{0}) {
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
    auto
    bzpopmin(const std::vector<std::string> &keys, long long timeout) {
        return derived().template make_coro_command<std::optional<std::tuple<std::string, std::string, double>>>(
            [this, keys, timeout](auto &&callback) { this->bzpopmin(std::move(callback), keys, timeout); });
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::tuple<std::string, std::string, double>>> &&>, Derived &>
    bzpopmin(Func &&func, const std::vector<std::string> &keys, long long timeout) {
        return derived().template command<std::optional<std::tuple<std::string, std::string, double>>>(std::forward<Func>(func), "BZPOPMIN",
                                                                                                       keys, timeout);
    }

    /**
     * @brief Coroutine-awaitable BZPOPMIN accepting a std::chrono timeout.
     *
     * @param keys Keys where sorted sets are stored.
     * @param timeout Blocking timeout; 0 means block forever.
     * @return Awaiter yielding an optional tuple of (key, member, score).
     */
    auto
    bzpopmin(const std::vector<std::string> &keys, const std::chrono::seconds &timeout = std::chrono::seconds{0}) {
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::tuple<std::string, std::string, double>>> &&>, Derived &>
    bzpopmin(Func &&func, const std::vector<std::string> &keys, const std::chrono::seconds &timeout = std::chrono::seconds{0}) {
        return bzpopmin(std::forward<Func>(func), keys, timeout.count());
    }

    /**
     * @brief Adds one or more scored members to a sorted set (coroutine awaitable).
     *
     * @param key Key where the sorted set is stored.
     * @param members Vector of score_member objects to add.
     * @param type Update policy (ALWAYS, EXIST, or NOT_EXIST).
     * @param changed If true, return the number of changed members rather than only new ones.
     * @return Awaiter yielding the number of added (or changed) members.
     */
    auto
    zadd(const std::string &key, const std::vector<score_member> &members, UpdateType type = UpdateType::ALWAYS, bool changed = false) {
        return derived().template make_coro_command<long long>(
            [this, key, members, type, changed](auto &&callback) { this->zadd(std::move(callback), key, members, type, changed); });
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
    zadd(Func &&func, const std::string &key, const std::vector<score_member> &members, UpdateType type = UpdateType::ALWAYS,
         bool changed = false) {
        std::optional<std::string> opt_up, opt_ch;

        if (type != UpdateType::ALWAYS)
            opt_up = to_string(type);

        if (changed)
            opt_ch = "CH";
        return derived().template command<long long>(std::forward<Func>(func), "ZADD", key, opt_up, opt_ch, members);
    }

    /**
     * @brief Gets the number of members in a sorted set
     *
     * @param key Key where the sorted set is stored
     * @return Number of members in the sorted set
     */
    auto
    zcard(const std::string &key) {
        return derived().template make_coro_command<long long>([this, key](auto &&callback) { this->zcard(std::move(callback), key); });
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
        return derived().template command<long long>(std::forward<Func>(func), "ZCARD", key);
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
    auto
    zcount(const std::string &key, const Interval &interval) {
        return derived().template make_coro_command<long long>(
            [this, key, interval](auto &&callback) { this->zcount(std::move(callback), key, interval); });
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
        return derived().template command<long long>(std::forward<Func>(func), "ZCOUNT", key, interval.lower(), interval.upper());
    }

    /**
     * @brief Increments the score of a sorted set member (coroutine awaitable).
     *
     * @param key Key where the sorted set is stored.
     * @param increment Amount to add to the member's score.
     * @param member Member whose score is incremented.
     * @return Awaiter yielding the member's new score.
     */
    auto
    zincrby(const std::string &key, double increment, const std::string &member) {
        return derived().template make_coro_command<double>(
            [this, key, increment, member](auto &&callback) { this->zincrby(std::move(callback), key, increment, member); });
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
    zincrby(Func &&func, const std::string &key, double increment, const std::string &member) {
        return derived().template command<double>(std::forward<Func>(func), "ZINCRBY", key, increment, member);
    }

    /**
     * @brief Unions multiple sorted sets and stores the result (coroutine awaitable).
     *
     * @param destination Key where the resulting sorted set is stored.
     * @param keys Keys of the source sorted sets.
     * @param weights Optional per-set score multipliers.
     * @param type Aggregation applied to combined scores (SUM, MIN, or MAX).
     * @return Awaiter yielding the number of members in the resulting set.
     */
    auto
    zunionstore(const std::string &destination, const std::vector<std::string> &keys, const std::vector<double> &weights = {},
                Aggregation type = Aggregation::SUM) {
        return derived().template make_coro_command<long long>([this, destination, keys, weights, type](auto &&callback) {
            this->zunionstore(std::move(callback), destination, keys, weights, type);
        });
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
    zunionstore(Func &&func, const std::string &destination, const std::vector<std::string> &keys, const std::vector<double> &weights = {},
                Aggregation type = Aggregation::SUM) {
        std::optional<std::string> opt;
        if (!weights.empty())
            opt = "WEIGHTS";
        return derived().template command<long long>(std::forward<Func>(func), "ZUNIONSTORE", destination, keys.size(), keys, opt, weights,
                                                     "AGGREGATE", to_string(type));
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
    auto
    zinterstore(const std::string &destination, const std::vector<std::string> &keys, const std::vector<double> &weights = {},
                Aggregation type = Aggregation::SUM) {
        return derived().template make_coro_command<long long>([this, destination, keys, weights, type](auto &&callback) {
            this->zinterstore(std::move(callback), destination, keys, weights, type);
        });
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
    zinterstore(Func &&func, const std::string &destination, const std::vector<std::string> &keys, const std::vector<double> &weights = {},
                Aggregation type = Aggregation::SUM) {
        std::optional<std::string> opt;
        if (!weights.empty())
            opt = "WEIGHTS";
        return derived().template command<long long>(std::forward<Func>(func), "ZINTERSTORE", destination, keys.size(), keys, opt, weights,
                                                     "AGGREGATE", to_string(type));
    }

    /**
     * @brief Counts members within a lexicographical range (coroutine awaitable).
     *
     * @tparam Interval Type exposing lower() and upper() bound accessors.
     * @param key Key where the sorted set is stored.
     * @param interval Lexicographical interval to count over.
     * @return Awaiter yielding the number of matching members.
     */
    template <typename Interval>
    auto
    zlexcount(const std::string &key, const Interval &interval) {
        return derived().template make_coro_command<long long>(
            [this, key, interval](auto &&callback) { this->zlexcount(std::move(callback), key, interval); });
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
        return derived().template command<long long>(std::forward<Func>(func), "ZLEXCOUNT", key, interval.lower(), interval.upper());
    }

    /**
     * @brief Removes and returns members with the highest scores (coroutine awaitable).
     *
     * @param key Key where the sorted set is stored.
     * @param count Number of members to pop (default 1).
     * @return Awaiter yielding the removed score_member objects.
     */
    auto
    zpopmax(const std::string &key, long long count = 1) {
        return derived().template make_coro_command<std::vector<score_member>>(
            [this, key, count](auto &&callback) { this->zpopmax(std::move(callback), key, count); });
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<score_member>> &&>, Derived &>
    zpopmax(Func &&func, const std::string &key, long long count = 1) {
        return derived().template command<std::vector<score_member>>(std::forward<Func>(func), "ZPOPMAX", key, count);
    }

    /**
     * @brief Removes and returns members with the lowest scores from a sorted set
     *
     * @param key Key where the sorted set is stored
     * @param count Number of members to pop (default is 1)
     * @return Vector of score_member objects that were removed
     */
    auto
    zpopmin(const std::string &key, long long count = 1) {
        return derived().template make_coro_command<std::vector<score_member>>(
            [this, key, count](auto &&callback) { this->zpopmin(std::move(callback), key, count); });
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<score_member>> &&>, Derived &>
    zpopmin(Func &&func, const std::string &key, long long count = 1) {
        return derived().template command<std::vector<score_member>>(std::forward<Func>(func), "ZPOPMIN", key, count);
    }

    /**
     * @brief Returns a range of members by index, with scores (coroutine awaitable).
     *
     * @param key Key where the sorted set is stored.
     * @param start Start index (0-based; negative counts from the end).
     * @param stop Stop index, inclusive (negative counts from the end).
     * @return Awaiter yielding the member-score pairs in the range.
     */
    auto
    zrange(const std::string &key, long long start, long long stop) {
        return derived().template make_coro_command<std::vector<score_member>>(
            [this, key, start, stop](auto &&callback) { this->zrange(std::move(callback), key, start, stop); });
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<score_member>> &&>, Derived &>
    zrange(Func &&func, const std::string &key, long long start, long long stop) {
        return derived().template command<std::vector<score_member>>(std::forward<Func>(func), "ZRANGE", key, start, stop, "WITHSCORES");
    }

    /**
     * @brief Returns members within a lexicographical range (coroutine awaitable).
     *
     * @tparam Interval Type exposing lower() and upper() bound accessors.
     * @param key Key where the sorted set is stored.
     * @param interval Lexicographical interval to query.
     * @param opts Limit options for pagination.
     * @return Awaiter yielding the matching members.
     */
    template <typename Interval>
    auto
    zrangebylex(const std::string &key, Interval const &interval, const LimitOptions &opts = {}) {
        return derived().template make_coro_command<std::vector<std::string>>(
            [this, key, interval, opts](auto &&callback) { this->zrangebylex(std::move(callback), key, interval, opts); });
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
        // A negative offset means "no LIMIT clause" (documented suppress form). The three parts must
        // then be OMITTED entirely: emitting empty strings sent `... LIMIT? "" "" ""` (empty bulks,
        // count 1 each) which Redis rejects as a syntax error. std::optional drops a nullopt from
        // both the arg count and the payload, so the suppress path becomes a clean `ZRANGEBYLEX
        // key min max` with no trailing tokens.
        const bool                 use_limit = opts.offset >= 0;
        std::optional<std::string> limit_kw  = use_limit ? std::optional<std::string>("LIMIT") : std::nullopt;
        std::optional<long long>   limit_off = use_limit ? std::optional<long long>(opts.offset) : std::nullopt;
        std::optional<long long>   limit_cnt = use_limit ? std::optional<long long>(opts.count) : std::nullopt;
        return derived().template command<std::vector<std::string>>(
            std::forward<Func>(func), "ZRANGEBYLEX", key, interval.lower(), interval.upper(), limit_kw, limit_off, limit_cnt);
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
    auto
    zrangebyscore(const std::string &key, Interval const &interval, const LimitOptions &opts = {}) {
        return derived().template make_coro_command<std::vector<score_member>>(
            [this, key, &interval, &opts](auto &&callback) { this->zrangebyscore(std::move(callback), key, interval, opts); });
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<score_member>> &&>, Derived &>
    zrangebyscore(Func &&func, const std::string &key, Interval const &interval, const LimitOptions &opts = {}) {
        // See zrangebylex: a negative offset suppresses LIMIT and the parts must be omitted (not sent
        // as empty bulks, which Redis rejects with a syntax error). WITHSCORES is always appended.
        const bool                 use_limit = opts.offset >= 0;
        std::optional<std::string> limit_kw  = use_limit ? std::optional<std::string>("LIMIT") : std::nullopt;
        std::optional<long long>   limit_off = use_limit ? std::optional<long long>(opts.offset) : std::nullopt;
        std::optional<long long>   limit_cnt = use_limit ? std::optional<long long>(opts.count) : std::nullopt;
        return derived().template command<std::vector<score_member>>(
            std::forward<Func>(func), "ZRANGEBYSCORE", key, interval.lower(), interval.upper(), limit_kw, limit_off, limit_cnt, "WITHSCORES");
    }

    /**
     * @brief Returns the rank of a member, low to high (coroutine awaitable).
     *
     * @param key Key where the sorted set is stored.
     * @param member Member whose rank is requested.
     * @return Awaiter yielding the 0-based rank, or nullopt if the member is absent.
     */
    auto
    zrank(const std::string &key, const std::string &member) {
        return derived().template make_coro_command<std::optional<long long>>(
            [this, key, member](auto &&callback) { this->zrank(std::move(callback), key, member); });
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<long long>> &&>, Derived &>
    zrank(Func &&func, const std::string &key, const std::string &member) {
        return derived().template command<std::optional<long long>>(std::forward<Func>(func), "ZRANK", key, member);
    }

    /**
     * @brief Removes one or more members from a sorted set (coroutine awaitable).
     *
     * @param key Key where the sorted set is stored.
     * @param members Members to remove.
     * @return Awaiter yielding the number of members removed.
     */
    auto
    zrem(const std::string &key, const std::vector<std::string> &members) {
        return derived().template make_coro_command<long long>(
            [this, key, members](auto &&callback) { this->zrem(std::move(callback), key, members); });
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
        return derived().template command<long long>(std::forward<Func>(func), "ZREM", key, members);
    }

    /**
     * @brief Removes members within a lexicographical range (coroutine awaitable).
     *
     * @tparam Interval Type exposing lower() and upper() bound accessors.
     * @param key Key where the sorted set is stored.
     * @param interval Lexicographical interval to remove.
     * @return Awaiter yielding the number of members removed.
     */
    template <typename Interval>
    auto
    zremrangebylex(const std::string &key, Interval const &interval) {
        return derived().template make_coro_command<long long>(
            [this, key, interval](auto &&callback) { this->zremrangebylex(std::move(callback), key, interval); });
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
        return derived().template command<long long>(std::forward<Func>(func), "ZREMRANGEBYLEX", key, interval.lower(), interval.upper());
    }

    /**
     * @brief Removes members from a sorted set that are within a range of indices
     *
     * @param key Key where the sorted set is stored
     * @param start Start index (0-based, can be negative to count from the end)
     * @param stop Stop index (inclusive, can be negative to count from the end)
     * @return Number of members removed
     */
    auto
    zremrangebyrank(const std::string &key, long long start, long long stop) {
        return derived().template make_coro_command<long long>(
            [this, key, start, stop](auto &&callback) { this->zremrangebyrank(std::move(callback), key, start, stop); });
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
    zremrangebyrank(Func &&func, const std::string &key, long long start, long long stop) {
        return derived().template command<long long>(std::forward<Func>(func), "ZREMRANGEBYRANK", key, start, stop);
    }

    /**
     * @brief Removes members within a score range (coroutine awaitable).
     *
     * @tparam Interval Type exposing lower() and upper() bound accessors.
     * @param key Key where the sorted set is stored.
     * @param interval Score interval to remove.
     * @return Awaiter yielding the number of members removed.
     */
    template <typename Interval>
    auto
    zremrangebyscore(const std::string &key, Interval const &interval) {
        return derived().template make_coro_command<long long>(
            [this, key, interval](auto &&callback) { this->zremrangebyscore(std::move(callback), key, interval); });
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
        return derived().template command<long long>(std::forward<Func>(func), "ZREMRANGEBYSCORE", key, interval.lower(), interval.upper());
    }

    /**
     * @brief Returns a range of members by index, high to low (coroutine awaitable).
     *
     * @param key Key where the sorted set is stored.
     * @param start Start index (0-based; negative counts from the end).
     * @param stop Stop index, inclusive (negative counts from the end).
     * @return Awaiter yielding the member-score pairs in descending order.
     */
    auto
    zrevrange(const std::string &key, long long start, long long stop) {
        return derived().template make_coro_command<std::vector<score_member>>(
            [this, key, start, stop](auto &&callback) { this->zrevrange(std::move(callback), key, start, stop); });
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<score_member>> &&>, Derived &>
    zrevrange(Func &&func, const std::string &key, long long start, long long stop) {
        return derived().template command<std::vector<score_member>>(std::forward<Func>(func), "ZREVRANGE", key, start, stop, "WITHSCORES");
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
    auto
    zrevrangebylex(const std::string &key, Interval const &interval, const LimitOptions &opt = {}) {
        return derived().template make_coro_command<std::vector<std::string>>(
            [this, key, interval, opt](auto &&callback) { this->zrevrangebylex(std::move(callback), key, interval, opt); });
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
        // Coherent with zrangebylex: a negative offset suppresses LIMIT (omit the triplet) instead of
        // emitting `LIMIT -1 -1` (an invalid offset). Default {0,-1} still sends `LIMIT 0 -1` (= all).
        const bool                 use_limit = opt.offset >= 0;
        std::optional<std::string> limit_kw  = use_limit ? std::optional<std::string>("LIMIT") : std::nullopt;
        std::optional<long long>   limit_off = use_limit ? std::optional<long long>(opt.offset) : std::nullopt;
        std::optional<long long>   limit_cnt = use_limit ? std::optional<long long>(opt.count) : std::nullopt;
        return derived().template command<std::vector<std::string>>(std::forward<Func>(func), "ZREVRANGEBYLEX", key, interval.upper(),
                                                                    interval.lower(), limit_kw, limit_off, limit_cnt);
    }

    /**
     * @brief Returns members within a score range, high to low (coroutine awaitable).
     *
     * @tparam Interval Type exposing lower() and upper() bound accessors.
     * @param key Key where the sorted set is stored.
     * @param interval Score interval to query.
     * @param opt Limit options for pagination.
     * @return Awaiter yielding the member-score pairs in descending order.
     */
    template <typename Interval>
    auto
    zrevrangebyscore(const std::string &key, Interval const &interval, const LimitOptions &opt = {}) {
        return derived().template make_coro_command<std::vector<score_member>>(
            [this, key, &interval, &opt](auto &&callback) { this->zrevrangebyscore(std::move(callback), key, interval, opt); });
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<score_member>> &&>, Derived &>
    zrevrangebyscore(Func &&func, const std::string &key, Interval const &interval, const LimitOptions &opt = {}) {
        // Coherent with zrangebyscore: negative offset suppresses LIMIT; WITHSCORES stays before it.
        const bool                 use_limit = opt.offset >= 0;
        std::optional<std::string> limit_kw  = use_limit ? std::optional<std::string>("LIMIT") : std::nullopt;
        std::optional<long long>   limit_off = use_limit ? std::optional<long long>(opt.offset) : std::nullopt;
        std::optional<long long>   limit_cnt = use_limit ? std::optional<long long>(opt.count) : std::nullopt;
        return derived().template command<std::vector<score_member>>(std::forward<Func>(func), "ZREVRANGEBYSCORE", key, interval.upper(),
                                                                     interval.lower(), "WITHSCORES", limit_kw, limit_off, limit_cnt);
    }

    /**
     * @brief Returns the rank of a member, high to low (coroutine awaitable).
     *
     * @param key Key where the sorted set is stored.
     * @param member Member whose rank is requested.
     * @return Awaiter yielding the 0-based reverse rank, or nullopt if absent.
     */
    auto
    zrevrank(const std::string &key, const std::string &member) {
        return derived().template make_coro_command<std::optional<long long>>(
            [this, key, member](auto &&callback) { this->zrevrank(std::move(callback), key, member); });
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
        return derived().template command<std::optional<long long>>(std::forward<Func>(func), "ZREVRANK", key, member);
    }

    /**
     * @brief Incrementally iterates sorted set members (coroutine awaitable).
     *
     * @param key Key where the sorted set is stored.
     * @param cursor Cursor position to resume iteration from (0 to start).
     * @param pattern Glob pattern used to filter members.
     * @param count Hint for the number of elements returned per call.
     * @return Awaiter yielding the next cursor and the matched member-score map.
     */
    auto
    zscan(const std::string &key, long long cursor, const std::string &pattern = "*", long long count = 10) {
        return derived().template make_coro_command<qb::redis::scan<qb::unordered_map<std::string, double>>>(
            [this, key, cursor, pattern, count](auto &&callback) { this->zscan(std::move(callback), key, cursor, pattern, count); });
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::redis::scan<qb::unordered_map<std::string, double>>> &&>, Derived &>
    zscan(Func &&func, const std::string &key, long long cursor, const std::string &pattern = "*", long long count = 10) {
        if (key.empty()) {
            return derived();
        }
        return derived().template command<qb::redis::scan<qb::unordered_map<std::string, double>>>(std::forward<Func>(func), "ZSCAN", key,
                                                                                                   cursor, "MATCH", pattern, "COUNT", count);
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::redis::scan<qb::unordered_map<std::string, double>>> &&>, Derived &>
    zscan(Func &&func, const std::string &key, const std::string &pattern = "*") {
        scanner<Func>::create_and_start(derived(), key, pattern, std::forward<Func>(func));
        return derived();
    }

    /**
     * @brief Returns the score of a sorted set member (coroutine awaitable).
     *
     * @param key Key where the sorted set is stored.
     * @param member Member whose score is requested.
     * @return Awaiter yielding the score, or nullopt if the member is absent.
     */
    auto
    zscore(const std::string &key, const std::string &member) {
        return derived().template make_coro_command<std::optional<double>>(
            [this, key, member](auto &&callback) { this->zscore(std::move(callback), key, member); });
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<double>> &&>, Derived &>
    zscore(Func &&func, const std::string &key, const std::string &member) {
        return derived().template command<std::optional<double>>(std::forward<Func>(func), "ZSCORE", key, member);
    }

    // =============== Set-algebra and extended sorted set commands ===============

    /**
     * @brief Return the difference between the first and successive sorted sets (coroutine awaitable)
     *
     * @param keys Keys of sorted sets (first is subtracted from others)
     * @return redis_awaiter yielding Reply<std::vector<std::string>>
     * @see https://redis.io/commands/zdiff
     */
    auto
    zdiff(const std::vector<std::string> &keys) {
        return derived().template make_coro_command<std::vector<std::string>>(
            [this, keys](auto &&callback) { this->zdiff(std::move(callback), keys); });
    }
    /**
     * @brief Return the difference between sorted sets with scores (coroutine awaitable)
     *
     * @param keys Keys of sorted sets (first is subtracted from others)
     * @return redis_awaiter yielding Reply<std::vector<score_member>>
     * @see https://redis.io/commands/zdiff
     */
    auto
    zdiffWithScores(const std::vector<std::string> &keys) {
        return derived().template make_coro_command<std::vector<score_member>>(
            [this, keys](auto &&callback) { this->zdiffWithScores(std::move(callback), keys); });
    }
    /**
     * @brief Asynchronous version of zdiff
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @param keys Keys of sorted sets
     * @return Reference to the derived class
     * @see https://redis.io/commands/zdiff
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>, Derived &>
    zdiff(Func &&func, const std::vector<std::string> &keys) {
        return derived().template command<std::vector<std::string>>(std::forward<Func>(func), "ZDIFF", keys.size(), keys);
    }
    /**
     * @brief Asynchronous version of zdiffWithScores
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @param keys Keys of sorted sets
     * @return Reference to the derived class
     * @see https://redis.io/commands/zdiff
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<score_member>> &&>, Derived &>
    zdiffWithScores(Func &&func, const std::vector<std::string> &keys) {
        return derived().template command<std::vector<score_member>>(std::forward<Func>(func), "ZDIFF", keys.size(), keys, "WITHSCORES");
    }

    /**
     * @brief Store the difference of sorted sets in destination (coroutine awaitable)
     *
     * @param destination Destination key for the result
     * @param keys Keys of sorted sets (first is subtracted from others)
     * @return redis_awaiter yielding Reply<long long> (number of elements in result)
     * @see https://redis.io/commands/zdiffstore
     */
    auto
    zdiffstore(const std::string &destination, const std::vector<std::string> &keys) {
        return derived().template make_coro_command<long long>(
            [this, destination, keys](auto &&callback) { this->zdiffstore(std::move(callback), destination, keys); });
    }
    /**
     * @brief Asynchronous version of zdiffstore
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @param destination Destination key
     * @param keys Keys of sorted sets
     * @return Reference to the derived class
     * @see https://redis.io/commands/zdiffstore
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    zdiffstore(Func &&func, const std::string &destination, const std::vector<std::string> &keys) {
        return derived().template command<long long>(std::forward<Func>(func), "ZDIFFSTORE", destination, keys.size(), keys);
    }

    /**
     * @brief Intersect multiple sorted sets (coroutine awaitable)
     *
     * @param keys Keys of sorted sets
     * @param weights Optional weights for each set
     * @param type Aggregation type for scores (SUM, MIN, MAX)
     * @return redis_awaiter yielding Reply<std::vector<std::string>>
     * @see https://redis.io/commands/zinter
     */
    auto
    zinter(const std::vector<std::string> &keys, const std::vector<double> &weights = {}, Aggregation type = Aggregation::SUM) {
        return derived().template make_coro_command<std::vector<std::string>>(
            [this, keys, weights, type](auto &&callback) { this->zinter(std::move(callback), keys, weights, type); });
    }
    /**
     * @brief Intersect multiple sorted sets with scores (coroutine awaitable)
     *
     * @param keys Keys of sorted sets
     * @param weights Optional weights for each set
     * @param type Aggregation type for scores (SUM, MIN, MAX)
     * @return redis_awaiter yielding Reply<std::vector<score_member>>
     * @see https://redis.io/commands/zinter
     */
    auto
    zinterWithScores(const std::vector<std::string> &keys, const std::vector<double> &weights = {}, Aggregation type = Aggregation::SUM) {
        return derived().template make_coro_command<std::vector<score_member>>(
            [this, keys, weights, type](auto &&callback) { this->zinterWithScores(std::move(callback), keys, weights, type); });
    }

    /**
     * @brief Asynchronous version of zinter
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @param keys Keys of sorted sets
     * @param weights Optional weights
     * @param type Aggregation type
     * @return Reference to the derived class
     * @see https://redis.io/commands/zinter
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>, Derived &>
    zinter(Func &&func, const std::vector<std::string> &keys, const std::vector<double> &weights = {}, Aggregation type = Aggregation::SUM) {
        std::vector<std::string> opt;
        if (!weights.empty()) {
            opt.push_back("WEIGHTS");
            for (double w : weights)
                opt.push_back(std::to_string(w));
        }
        opt.push_back("AGGREGATE");
        opt.push_back(to_string(type));
        return derived().template command<std::vector<std::string>>(std::forward<Func>(func), "ZINTER", keys.size(), keys, opt);
    }

    /**
     * @brief Asynchronous version of zinterWithScores
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @param keys Keys of sorted sets
     * @param weights Optional weights
     * @param type Aggregation type
     * @return Reference to the derived class
     * @see https://redis.io/commands/zinter
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<score_member>> &&>, Derived &>
    zinterWithScores(Func &&func, const std::vector<std::string> &keys, const std::vector<double> &weights = {},
                     Aggregation type = Aggregation::SUM) {
        std::vector<std::string> opt;
        if (!weights.empty()) {
            opt.push_back("WEIGHTS");
            for (double w : weights)
                opt.push_back(std::to_string(w));
        }
        opt.push_back("AGGREGATE");
        opt.push_back(to_string(type));
        opt.push_back("WITHSCORES");
        return derived().template command<std::vector<score_member>>(std::forward<Func>(func), "ZINTER", keys.size(), keys, opt);
    }

    /**
     * @brief Returns the cardinality of the intersection of sorted sets (coroutine awaitable).
     *
     * @param keys Keys of the source sorted sets.
     * @param limit Optional cap on the cardinality to compute (nullopt for no limit).
     * @return Awaiter yielding the intersection cardinality.
     * @see https://redis.io/commands/zintercard
     */
    auto
    zintercard(const std::vector<std::string> &keys, std::optional<long long> limit = std::nullopt) {
        return derived().template make_coro_command<long long>(
            [this, keys, limit](auto &&callback) { this->zintercard(std::move(callback), keys, limit); });
    }
    /**
     * @brief Asynchronous version of zintercard.
     *
     * @tparam Func Callback function type.
     * @param func Callback function to handle the result.
     * @param keys Keys of the source sorted sets.
     * @param limit Optional cap on the cardinality to compute (nullopt for no limit).
     * @return Reference to the derived class.
     * @see https://redis.io/commands/zintercard
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    zintercard(Func &&func, const std::vector<std::string> &keys, std::optional<long long> limit = std::nullopt) {
        std::vector<std::string> opt;
        if (limit) {
            opt.push_back("LIMIT");
            opt.push_back(std::to_string(*limit));
        }
        return derived().template command<long long>(std::forward<Func>(func), "ZINTERCARD", keys.size(), keys, opt);
    }

    /**
     * @brief Pops members with the lowest/highest scores from the first non-empty
     * sorted set (coroutine awaitable).
     *
     * @param keys Keys to check, in order.
     * @param min_or_max "MIN" or "MAX" selecting which end to pop.
     * @param count Number of members to pop (default 1).
     * @return Awaiter yielding an optional pair of (key, popped members).
     * @see https://redis.io/commands/zmpop
     */
    auto
    zmpop(const std::vector<std::string> &keys, const std::string &min_or_max, long long count = 1) {
        return derived().template make_coro_command<std::optional<std::pair<std::string, std::vector<score_member>>>>(
            [this, keys, min_or_max, count](auto &&callback) { this->zmpop(std::move(callback), keys, min_or_max, count); });
    }
    /**
     * @brief Asynchronous version of zmpop.
     *
     * @tparam Func Callback function type.
     * @param func Callback function to handle the result.
     * @param keys Keys to check, in order.
     * @param min_or_max "MIN" or "MAX" selecting which end to pop.
     * @param count Number of members to pop (default 1).
     * @return Reference to the derived class.
     * @see https://redis.io/commands/zmpop
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::pair<std::string, std::vector<score_member>>>> &&>, Derived &>
    zmpop(Func &&func, const std::vector<std::string> &keys, const std::string &min_or_max, long long count = 1) {
        if (keys.empty()) {
            fail_client<std::optional<std::pair<std::string, std::vector<score_member>>>>(std::forward<Func>(func), "ZMPOP requires at least one key");
            return derived();
        }
        std::vector<std::string> opt;
        if (count > 1) {
            opt.push_back("COUNT");
            opt.push_back(std::to_string(count));
        }
        return derived().template command<std::optional<std::pair<std::string, std::vector<score_member>>>>(std::forward<Func>(func), "ZMPOP",
                                                                                                            keys.size(), keys, min_or_max, opt);
    }

    /**
     * @brief Gets the scores of multiple members (coroutine awaitable).
     *
     * @param key Key where the sorted set is stored.
     * @param members Members whose scores are requested.
     * @return Awaiter yielding one optional score per member (nullopt if absent).
     * @see https://redis.io/commands/zmscore
     */
    auto
    zmscore(const std::string &key, const std::vector<std::string> &members) {
        return derived().template make_coro_command<std::vector<std::optional<double>>>(
            [this, key, members](auto &&callback) { this->zmscore(std::move(callback), key, members); });
    }
    /**
     * @brief Asynchronous version of zmscore.
     *
     * @tparam Func Callback function type.
     * @param func Callback function to handle the result.
     * @param key Key where the sorted set is stored.
     * @param members Members whose scores are requested.
     * @return Reference to the derived class.
     * @see https://redis.io/commands/zmscore
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::optional<double>>> &&>, Derived &>
    zmscore(Func &&func, const std::string &key, const std::vector<std::string> &members) {
        return derived().template command<std::vector<std::optional<double>>>(std::forward<Func>(func), "ZMSCORE", key, members);
    }

    /**
     * @brief Gets a single random member from a sorted set (coroutine awaitable).
     *
     * @param key Key where the sorted set is stored.
     * @return Awaiter yielding a random member, or nullopt if the set is empty.
     * @see https://redis.io/commands/zrandmember
     */
    auto
    zrandmember(const std::string &key) {
        return derived().template make_coro_command<std::optional<std::string>>(
            [this, key](auto &&callback) { this->zrandmember(std::move(callback), key); });
    }
    /**
     * @brief Asynchronous version of zrandmember.
     *
     * @tparam Func Callback function type.
     * @param func Callback function to handle the result.
     * @param key Key where the sorted set is stored.
     * @return Reference to the derived class.
     * @see https://redis.io/commands/zrandmember
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::string>> &&>, Derived &>
    zrandmember(Func &&func, const std::string &key) {
        return derived().template command<std::optional<std::string>>(std::forward<Func>(func), "ZRANDMEMBER", key);
    }

    /**
     * @brief Get multiple random members from a sorted set (coroutine awaitable)
     *
     * @param key Key where the sorted set is stored
     * @param count Number of random members to return
     * @return redis_awaiter yielding Reply<std::vector<std::string>>
     * @see https://redis.io/commands/zrandmember
     */
    auto
    zrandmemberCount(const std::string &key, long long count) {
        return derived().template make_coro_command<std::vector<std::string>>(
            [this, key, count](auto &&callback) { this->zrandmemberCount(std::move(callback), key, count); });
    }
    /**
     * @brief Get random members with scores from a sorted set (coroutine awaitable)
     *
     * @param key Key where the sorted set is stored
     * @param count Number of random members to return
     * @return redis_awaiter yielding Reply<std::vector<score_member>>
     * @see https://redis.io/commands/zrandmember
     */
    auto
    zrandmemberWithScores(const std::string &key, long long count) {
        return derived().template make_coro_command<std::vector<score_member>>(
            [this, key, count](auto &&callback) { this->zrandmemberWithScores(std::move(callback), key, count); });
    }

    /**
     * @brief Asynchronous version of zrandmemberCount
     * @see https://redis.io/commands/zrandmember
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>, Derived &>
    zrandmemberCount(Func &&func, const std::string &key, long long count) {
        return derived().template command<std::vector<std::string>>(std::forward<Func>(func), "ZRANDMEMBER", key, count);
    }

    /**
     * @brief Asynchronous version of zrandmemberWithScores
     * @see https://redis.io/commands/zrandmember
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<score_member>> &&>, Derived &>
    zrandmemberWithScores(Func &&func, const std::string &key, long long count) {
        return derived().template command<std::vector<score_member>>(std::forward<Func>(func), "ZRANDMEMBER", key, count, "WITHSCORES");
    }

    /**
     * @brief Stores a range of members from one sorted set into another key
     * (coroutine awaitable).
     *
     * @param dst Destination key for the resulting sorted set.
     * @param src Source key to read the range from.
     * @param min Lower bound of the range (index or score/lex bound per options).
     * @param max Upper bound of the range (index or score/lex bound per options).
     * @param options Additional ZRANGESTORE options (e.g. BYSCORE, BYLEX, REV, LIMIT).
     * @return Awaiter yielding the number of members stored.
     * @see https://redis.io/commands/zrangestore
     */
    auto
    zrangestore(const std::string &dst, const std::string &src, const std::string &min, const std::string &max,
                const std::vector<std::string> &options = {}) {
        return derived().template make_coro_command<long long>(
            [this, dst, src, min, max, options](auto &&callback) { this->zrangestore(std::move(callback), dst, src, min, max, options); });
    }
    /**
     * @brief Asynchronous version of zrangestore.
     *
     * @tparam Func Callback function type.
     * @param func Callback function to handle the result.
     * @param dst Destination key for the resulting sorted set.
     * @param src Source key to read the range from.
     * @param min Lower bound of the range (index or score/lex bound per options).
     * @param max Upper bound of the range (index or score/lex bound per options).
     * @param options Additional ZRANGESTORE options (e.g. BYSCORE, BYLEX, REV, LIMIT).
     * @return Reference to the derived class.
     * @see https://redis.io/commands/zrangestore
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    zrangestore(Func &&func, const std::string &dst, const std::string &src, const std::string &min, const std::string &max,
                const std::vector<std::string> &options = {}) {
        std::vector<std::string> args = {dst, src, min, max};
        args.insert(args.end(), options.begin(), options.end());
        return derived().template command<long long>(std::forward<Func>(func), "ZRANGESTORE", args);
    }

    /**
     * @brief Blocking variant of ZMPOP (coroutine awaitable).
     *
     * @param keys Keys to check, in order.
     * @param timeout Blocking timeout in seconds; 0 means block forever.
     * @param min_or_max "MIN" or "MAX" selecting which end to pop.
     * @param count Number of members to pop (default 1).
     * @return Awaiter yielding an optional pair of (key, popped members).
     * @see https://redis.io/commands/bzmpop
     */
    auto
    bzmpop(const std::vector<std::string> &keys, long long timeout, const std::string &min_or_max, long long count = 1) {
        return derived().template make_coro_command<std::optional<std::pair<std::string, std::vector<score_member>>>>(
            [this, keys, timeout, min_or_max, count](auto &&callback) { this->bzmpop(std::move(callback), keys, timeout, min_or_max, count); });
    }
    /**
     * @brief Asynchronous version of bzmpop.
     *
     * @tparam Func Callback function type.
     * @param func Callback function to handle the result.
     * @param keys Keys to check, in order.
     * @param timeout Blocking timeout in seconds; 0 means block forever.
     * @param min_or_max "MIN" or "MAX" selecting which end to pop.
     * @param count Number of members to pop (default 1).
     * @return Reference to the derived class.
     * @see https://redis.io/commands/bzmpop
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::pair<std::string, std::vector<score_member>>>> &&>, Derived &>
    bzmpop(Func &&func, const std::vector<std::string> &keys, long long timeout, const std::string &min_or_max, long long count = 1) {
        if (keys.empty()) {
            fail_client<std::optional<std::pair<std::string, std::vector<score_member>>>>(std::forward<Func>(func), "BZMPOP requires at least one key");
            return derived();
        }
        std::vector<std::string> opt;
        if (count > 1) {
            opt.push_back("COUNT");
            opt.push_back(std::to_string(count));
        }
        return derived().template command<std::optional<std::pair<std::string, std::vector<score_member>>>>(
            std::forward<Func>(func), "BZMPOP", timeout, keys.size(), keys, min_or_max, opt);
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_SORTED_SET_COMMANDS_H
