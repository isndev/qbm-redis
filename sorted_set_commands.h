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

template <typename Derived>
class sorted_set_commands {
    template <typename Func>
    class scanner {
        Derived &_handler;
        std::string _key;
        std::string _pattern;
        Func _func;
        qb::redis::Reply<qb::redis::reply::scan<qb::unordered_map<std::string, double>>> _reply;

    public:
        scanner(Derived &handler, std::string key, std::string pattern, Func &&func)
            : _handler(handler)
            , _key(std::move(key))
            , _pattern(std::move(pattern))
            , _func(std::forward<Func>(func)) {
            _handler.zscan(std::ref(*this), _key, 0, _pattern, 100);
        }

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

    template <typename... Keys>
    std::optional<std::tuple<std::string, std::string, double>>
    bzpopmax(Keys &&...keys, const std::chrono::seconds &timeout = std::chrono::seconds{0}) {
        return bzpopmax(std::forward<Keys>(keys)..., timeout.count());
    }
    template <typename Func, typename... Keys>
    std::enable_if_t<
        std::is_invocable_v<Func, Reply<std::optional<std::tuple<std::string, std::string, double>>> &&>, Derived &>
    bzpopmax(Func &&func, Keys &&...keys, const std::chrono::seconds &timeout = std::chrono::seconds{0}) {
        return bzpopmax(std::forward<Func>(func), std::forward<Keys>(keys)..., timeout.count());
    }

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

    template <typename... Keys>
    std::optional<std::tuple<std::string, std::string, double>>
    bzpopmin(Keys &&...keys, const std::chrono::seconds &timeout = std::chrono::seconds{0}) {
        return bzpopmin(std::forward<Keys>(keys)..., timeout.count());
    }
    template <typename Func, typename... Keys>
    std::enable_if_t<
        std::is_invocable_v<Func, Reply<std::optional<std::tuple<std::string, std::string, double>>> &&>, Derived &>
    bzpopmin(Func &&func, Keys &&...keys, const std::chrono::seconds &timeout = std::chrono::seconds{0}) {
        return bzpopmin(std::forward<Func>(func), std::forward<Keys>(keys)..., timeout.count());
    }

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

    long long
    zcard(const std::string &key) {
        return static_cast<Derived &>(*this).template command<long long>("ZCARD", key).result;
    }
    template <typename Func>
    Derived &
    zcard(Func &&func, const std::string &key) {
        return static_cast<Derived &>(*this).template command<long long>(std::forward<Func>(func), "ZCARD", key);
    }

    template <typename Interval>
    long long
    zcount(const std::string &key, const Interval &interval) {
        return static_cast<Derived &>(*this)
            .template command<long long>("ZCOUNT", key, interval.lower(), interval.upper())
            .result;
    }
    template <typename Func, typename Interval>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    zcount(Func &&func, const std::string &key, const Interval &interval) {
        return static_cast<Derived &>(*this)
            .template command<long long>(std::forward<Func>(func), "ZCOUNT", key, interval.lower(), interval.upper());
    }

    double
    zincrby(const std::string &key, double increment, const std::string &member) {
        return static_cast<Derived &>(*this).template command<double>("ZINCRBY", key, increment, member).result;
    }
    template <typename Func>
    Derived &
    zincrby(Func &&func, const std::string &key, double increment, const std::string &member) {
        return static_cast<Derived &>(*this)
            .template command<double>(std::forward<Func>(func), "ZINCRBY", key, increment, member);
    }

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

    inline long long
    zinterstore(
        const std::string &destination, const std::string &key, double weight = 1.,
        Aggregation type = Aggregation::SUM) {
        std::optional<std::string> opt;
        return zinterstore(destination, std::vector{key}, std::vector{weight}, type);
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    zinterstore(
        Func &&func, const std::string &destination, const std::string &key, double weight = 1.,
        Aggregation type = Aggregation::SUM) {
        return zinterstore(std::forward<Func>(func), destination, std::vector{key}, std::vector{weight}, type);
    }

    template <typename Interval>
    long long
    zlexcount(const std::string &key, const Interval &interval) {
        return static_cast<Derived &>(*this)
            .template command<long long>("ZLEXCOUNT", key, interval.lower(), interval.upper())
            .result;
    }
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

    template <typename... Keys>
    std::vector<std::pair<std::string, double>>
    zpopmax(const std::string &key, long long count = 1) {
        return static_cast<Derived &>(*this)
            .template command<std::vector<std::pair<std::string, double>>>("ZPOPMAX", key, count)
            .result;
    }
    template <typename Func, typename... Keys>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::pair<std::string, double>>> &&>, Derived &>
    zpopmax(Func &&func, const std::string &key, long long count = 1) {
        return static_cast<Derived &>(*this).template command<std::vector<std::pair<std::string, double>>>(
            std::forward<Func>(func),
            "ZPOPMAX",
            key,
            count);
    }

    template <typename... Keys>
    std::vector<std::pair<std::string, double>>
    zpopmin(const std::string &key, long long count = 1) {
        return static_cast<Derived &>(*this)
            .template command<std::vector<std::pair<std::string, double>>>("ZPOPMIN", key, count)
            .result;
    }
    template <typename Func, typename... Keys>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::pair<std::string, double>>> &&>, Derived &>
    zpopmin(Func &&func, const std::string &key, long long count = 1) {
        return static_cast<Derived &>(*this).template command<std::vector<std::pair<std::string, double>>>(
            std::forward<Func>(func),
            "ZPOPMIN",
            key,
            count);
    }

    std::vector<std::pair<std::string, double>>
    zrange(const std::string &key, long long start, long long stop) {
        return static_cast<Derived &>(*this)
            .template command<std::vector<std::pair<std::string, double>>>("ZRANGE", key, start, stop, "WITHSCORES")
            .result;
    }
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

    std::optional<long long>
    zrank(const std::string &key, const std::string &member) {
        return static_cast<Derived &>(*this).template command<std::optional<long long>>("ZRANK", key, member).result;
    }
    template <typename Func>
    Derived &
    zrank(Func &&func, const std::string &key, const std::string &member) {
        return static_cast<Derived &>(*this)
            .template command<std::optional<long long>>(std::forward<Func>(func), "ZRANK", key, member);
    }

    template <typename... Members>
    long long
    zrem(const std::string &key, Members &&...members) {
        return static_cast<Derived &>(*this)
            .template command<long long>("ZREM", key, std::forward<Members>(members)...)
            .result;
    }
    template <typename Func, typename... Members>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    zrem(Func &&func, const std::string &key, Members &&...members) {
        return static_cast<Derived &>(*this)
            .template command<long long>(std::forward<Func>(func), "ZREM", key, std::forward<Members>(members)...);
    }

    template <typename Interval>
    long long
    zremrangebylex(const std::string &key, Interval const &interval) {
        return static_cast<Derived &>(*this)
            .template command<long long>("ZREMRANGEBYLEX", key, interval.lower(), interval.upper())
            .result;
    }
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

    long long
    zremrangebyrank(const std::string &key, long long start, long long stop) {
        return static_cast<Derived &>(*this).template command<long long>("ZREMRANGEBYRANK", key, start, stop).result;
    }
    template <typename Func>
    Derived &
    zremrangebyrank(Func &&func, const std::string &key, long long start, long long stop) {
        return static_cast<Derived &>(*this)
            .template command<long long>(std::forward<Func>(func), "ZREMRANGEBYRANK", key, start, stop);
    }

    template <typename Interval>
    long long
    zremrangebyscore(const std::string &key, Interval const &interval) {
        return static_cast<Derived &>(*this)
            .template command<long long>("ZREMRANGEBYSCORE", key, interval.lower(), interval.upper())
            .result;
    }
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

    std::vector<std::pair<std::string, double>>
    zrevrange(const std::string &key, long long start, long long stop) {
        return static_cast<Derived &>(*this)
            .template command<std::vector<std::pair<std::string, double>>>("ZREVRANGE", key, start, stop, "WITHSCORES")
            .result;
    }
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

    std::optional<long long>
    zrevrank(const std::string &key, const std::string &member) {
        return static_cast<Derived &>(*this).template command<std::optional<long long>>("ZREVRANK", key, member).result;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<long long>> &&>, Derived &>
    zrevrank(Func &&func, const std::string &key, const std::string &member) {
        return static_cast<Derived &>(*this)
            .template command<std::optional<long long>>(std::forward<Func>(func), "ZREVRANK", key, member);
    }

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
    template <typename Func>
    std::enable_if_t<
        std::is_invocable_v<Func, Reply<reply::scan<qb::unordered_map<std::string, double>>> &&>, Derived &>
    zscan(Func &&func, std::string const &key, const std::string &pattern = "*") {
        new scanner<Func>(static_cast<Derived &>(*this), key, pattern, std::forward<Func>(func));
        return static_cast<Derived &>(*this);
    }

    std::optional<double>
    zscore(const std::string &key, const std::string &member) {
        return static_cast<Derived &>(*this).template command<std::optional<double>>("ZSCORE", key, member).result;
    }
    template <typename Func>
    Derived &
    zscore(Func &&func, const std::string &key, const std::string &member) {
        return static_cast<Derived &>(*this)
            .template command<std::optional<double>>(std::forward<Func>(func), "ZSCORE", key, member);
    }

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

    inline long long
    zunionstore(
        const std::string &destination, const std::string &key, double weight = 1.,
        Aggregation type = Aggregation::SUM) {
        std::optional<std::string> opt;
        return zunionstore(destination, std::vector{key}, std::vector{weight}, type);
    }
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
