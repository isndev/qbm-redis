/**
 * @file qbm/redis/stats.h
 * @brief What a qbm-redis client or consumer has done -- `stats()`, the `on_command_complete` hook, the latency
 *        histograms -- and the per-verb traits they rest on (Huly QB-134). The mirror of qbm-pgsql's
 *        `qb::pg::stats` and `qb::pg::query_info`.
 *
 * The counts are monotonic since the object was constructed (two snapshots give a rate; a reconnection resets
 * nothing) and are plain increments on paths the client already runs. Latency is opt-in
 * (`set_latency_stats(true)`): until then the client reads no clock for it and holds no histogram. The
 * `on_command_complete` hook is empty by default; without one a completion pays one predictable branch.
 *
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2026 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)
 * @ingroup Redis
 */
#ifndef QBM_REDIS_STATS_H
#define QBM_REDIS_STATS_H

#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <qb/io/reconnect.h>
#include <qb/system/container/unordered_map.h>
#include <qb/system/time.h>

namespace qb::redis {

/**
 * @brief The command family of a verb: the command group (the `*_commands.h` mixin) that issues it.
 * @details `other` is a verb the client does not issue itself -- a module command sent through the generic
 *          `command<T>("MY.CMD", ...)`.
 */
enum class command_family : std::uint8_t {
    acl,
    bitmap,
    cluster,
    connection,
    function,
    geo,
    hash,
    hyperloglog,
    key,
    list,
    module,
    publish,
    scripting,
    server,
    set,
    sorted_set,
    stream,
    string,
    subscription,
    transaction,
    other,
};

/// Number of `qb::redis::command_family` values.
inline constexpr std::size_t command_families = 21;

/** @brief The name of a `command_family`, for log lines and metrics labels. */
[[nodiscard]] constexpr std::string_view
command_family_name(command_family const family) noexcept {
    switch (family) {
        case command_family::acl:
            return "acl";
        case command_family::bitmap:
            return "bitmap";
        case command_family::cluster:
            return "cluster";
        case command_family::connection:
            return "connection";
        case command_family::function:
            return "function";
        case command_family::geo:
            return "geo";
        case command_family::hash:
            return "hash";
        case command_family::hyperloglog:
            return "hyperloglog";
        case command_family::key:
            return "key";
        case command_family::list:
            return "list";
        case command_family::module:
            return "module";
        case command_family::publish:
            return "publish";
        case command_family::scripting:
            return "scripting";
        case command_family::server:
            return "server";
        case command_family::set:
            return "set";
        case command_family::sorted_set:
            return "sorted_set";
        case command_family::stream:
            return "stream";
        case command_family::string:
            return "string";
        case command_family::subscription:
            return "subscription";
        case command_family::transaction:
            return "transaction";
        case command_family::other:
            return "other";
    }
    return "?";
}

/**
 * @brief What the client knows of a verb: its family and how it may be handled.
 * @details One table, looked up once per command -- the lookup the command watchdog already needed for
 *          `blocking`. `read_only` and `idempotent` describe the verb in every form it takes: a verb with an
 *          option that writes (`SORT ... STORE`) or that accumulates (`ZADD ... INCR`) is neither.
 */
struct command_traits {
    command_family family     = command_family::other;
    bool           blocking   = false; ///< parks server-side with its own timeout: the command watchdog is suspended
    bool           read_only  = false; ///< reads only: replaying it cannot change the data set
    bool           idempotent = false; ///< writes, but a replay with the same arguments leaves the same state
};

namespace detail {

/// One row of the command table.
struct command_row {
    std::string_view verb;
    command_traits   traits;
};

/**
 * @brief Every verb the client issues: the command groups' (the unit test `unit/stats/command-traits.cpp` reads
 *        the groups' sources and refuses any drift) and MONITOR. Sorted by verb; HAND-MAINTAINED -- a verb added
 *        to a command group is added here, with its family and flags.
 */
inline constexpr std::array<command_row, 225> command_table{{
    {"ACL", {command_family::acl, false, false, false}},
    {"APPEND", {command_family::string, false, false, false}},
    {"ASKING", {command_family::cluster, false, false, false}},
    {"AUTH", {command_family::connection, false, false, false}},
    {"BGREWRITEAOF", {command_family::server, false, false, false}},
    {"BGSAVE", {command_family::server, false, false, false}},
    {"BITCOUNT", {command_family::bitmap, false, true, false}},
    {"BITFIELD", {command_family::bitmap, false, false, false}},
    {"BITFIELD_RO", {command_family::bitmap, false, true, false}},
    {"BITOP", {command_family::bitmap, false, false, false}},
    {"BITPOS", {command_family::bitmap, false, true, false}},
    {"BLMOVE", {command_family::list, true, false, false}},
    {"BLMPOP", {command_family::list, true, false, false}},
    {"BLPOP", {command_family::list, true, false, false}},
    {"BRPOP", {command_family::list, true, false, false}},
    {"BRPOPLPUSH", {command_family::list, true, false, false}},
    {"BZMPOP", {command_family::sorted_set, true, false, false}},
    {"BZPOPMAX", {command_family::sorted_set, true, false, false}},
    {"BZPOPMIN", {command_family::sorted_set, true, false, false}},
    {"CLIENT", {command_family::server, false, false, false}},
    {"CLUSTER", {command_family::cluster, false, false, false}},
    {"COMMAND", {command_family::server, false, false, false}},
    {"CONFIG", {command_family::server, false, false, false}},
    {"COPY", {command_family::key, false, false, false}},
    {"DBSIZE", {command_family::server, false, true, false}},
    {"DEBUG", {command_family::server, false, false, false}},
    {"DECR", {command_family::string, false, false, false}},
    {"DECRBY", {command_family::string, false, false, false}},
    {"DEL", {command_family::key, false, false, true}},
    {"DISCARD", {command_family::transaction, false, false, false}},
    {"DUMP", {command_family::key, false, true, false}},
    {"ECHO", {command_family::connection, false, true, false}},
    {"EVAL", {command_family::scripting, false, false, false}},
    {"EVALSHA", {command_family::scripting, false, false, false}},
    {"EVALSHA_RO", {command_family::scripting, false, true, false}},
    {"EVAL_RO", {command_family::scripting, false, true, false}},
    {"EXEC", {command_family::transaction, false, false, false}},
    {"EXISTS", {command_family::key, false, true, false}},
    {"EXPIRE", {command_family::key, false, false, false}},
    {"EXPIREAT", {command_family::key, false, false, true}},
    {"EXPIRETIME", {command_family::key, false, true, false}},
    {"FAILOVER", {command_family::server, false, false, false}},
    {"FCALL", {command_family::function, false, false, false}},
    {"FCALL_RO", {command_family::function, false, true, false}},
    {"FLUSHALL", {command_family::server, false, false, false}},
    {"FLUSHDB", {command_family::server, false, false, false}},
    {"FUNCTION", {command_family::function, false, false, false}},
    {"GEOADD", {command_family::geo, false, false, false}},
    {"GEODIST", {command_family::geo, false, true, false}},
    {"GEOHASH", {command_family::geo, false, true, false}},
    {"GEOPOS", {command_family::geo, false, true, false}},
    {"GEORADIUS", {command_family::geo, false, false, false}},
    {"GEORADIUSBYMEMBER", {command_family::geo, false, false, false}},
    {"GEOSEARCH", {command_family::geo, false, true, false}},
    {"GET", {command_family::string, false, true, false}},
    {"GETBIT", {command_family::bitmap, false, true, false}},
    {"GETDEL", {command_family::string, false, false, false}},
    {"GETEX", {command_family::string, false, false, false}},
    {"GETRANGE", {command_family::string, false, true, false}},
    {"GETSET", {command_family::string, false, false, false}},
    {"HDEL", {command_family::hash, false, false, true}},
    {"HELLO", {command_family::connection, false, false, false}},
    {"HEXISTS", {command_family::hash, false, true, false}},
    {"HGET", {command_family::hash, false, true, false}},
    {"HGETALL", {command_family::hash, false, true, false}},
    {"HINCRBY", {command_family::hash, false, false, false}},
    {"HINCRBYFLOAT", {command_family::hash, false, false, false}},
    {"HKEYS", {command_family::hash, false, true, false}},
    {"HLEN", {command_family::hash, false, true, false}},
    {"HMGET", {command_family::hash, false, true, false}},
    {"HMSET", {command_family::hash, false, false, true}},
    {"HSCAN", {command_family::hash, false, true, false}},
    {"HSET", {command_family::hash, false, false, true}},
    {"HSETNX", {command_family::hash, false, false, false}},
    {"HSTRLEN", {command_family::hash, false, true, false}},
    {"HVALS", {command_family::hash, false, true, false}},
    {"INCR", {command_family::string, false, false, false}},
    {"INCRBY", {command_family::string, false, false, false}},
    {"INCRBYFLOAT", {command_family::string, false, false, false}},
    {"INFO", {command_family::server, false, true, false}},
    {"KEYS", {command_family::key, false, true, false}},
    {"LASTSAVE", {command_family::server, false, true, false}},
    {"LATENCY", {command_family::server, false, false, false}},
    {"LCS", {command_family::string, false, true, false}},
    {"LINDEX", {command_family::list, false, true, false}},
    {"LINSERT", {command_family::list, false, false, false}},
    {"LLEN", {command_family::list, false, true, false}},
    {"LMOVE", {command_family::list, false, false, false}},
    {"LMPOP", {command_family::list, false, false, false}},
    {"LPOP", {command_family::list, false, false, false}},
    {"LPOS", {command_family::list, false, true, false}},
    {"LPUSH", {command_family::list, false, false, false}},
    {"LPUSHX", {command_family::list, false, false, false}},
    {"LRANGE", {command_family::list, false, true, false}},
    {"LREM", {command_family::list, false, false, false}},
    {"LSET", {command_family::list, false, false, false}},
    {"LTRIM", {command_family::list, false, false, false}},
    {"MEMORY", {command_family::server, false, false, false}},
    {"MGET", {command_family::string, false, true, false}},
    {"MIGRATE", {command_family::key, false, false, false}},
    {"MODULE", {command_family::module, false, false, false}},
    {"MONITOR", {command_family::server, false, false, false}},
    {"MOVE", {command_family::key, false, false, false}},
    {"MSET", {command_family::string, false, false, true}},
    {"MSETNX", {command_family::string, false, false, false}},
    {"MULTI", {command_family::transaction, false, false, false}},
    {"OBJECT", {command_family::key, false, true, false}},
    {"PERSIST", {command_family::key, false, false, true}},
    {"PEXPIRE", {command_family::key, false, false, false}},
    {"PEXPIREAT", {command_family::key, false, false, true}},
    {"PEXPIRETIME", {command_family::key, false, true, false}},
    {"PFADD", {command_family::hyperloglog, false, false, false}},
    {"PFCOUNT", {command_family::hyperloglog, false, true, false}},
    {"PFMERGE", {command_family::hyperloglog, false, false, false}},
    {"PING", {command_family::connection, false, true, false}},
    {"PSETEX", {command_family::string, false, false, true}},
    {"PSUBSCRIBE", {command_family::subscription, false, false, false}},
    {"PSYNC", {command_family::server, false, false, false}},
    {"PTTL", {command_family::key, false, true, false}},
    {"PUBLISH", {command_family::publish, false, false, false}},
    {"PUNSUBSCRIBE", {command_family::subscription, false, false, false}},
    {"QUIT", {command_family::connection, false, false, false}},
    {"RANDOMKEY", {command_family::key, false, true, false}},
    {"READONLY", {command_family::cluster, false, false, false}},
    {"READWRITE", {command_family::cluster, false, false, false}},
    {"RENAME", {command_family::key, false, false, false}},
    {"RENAMENX", {command_family::key, false, false, false}},
    {"RESET", {command_family::connection, false, false, false}},
    {"RESTORE", {command_family::key, false, false, false}},
    {"ROLE", {command_family::server, false, false, false}},
    {"RPOP", {command_family::list, false, false, false}},
    {"RPOPLPUSH", {command_family::list, false, false, false}},
    {"RPUSH", {command_family::list, false, false, false}},
    {"RPUSHX", {command_family::list, false, false, false}},
    {"SADD", {command_family::set, false, false, true}},
    {"SAVE", {command_family::server, false, false, false}},
    {"SCAN", {command_family::key, false, true, false}},
    {"SCARD", {command_family::set, false, true, false}},
    {"SCRIPT", {command_family::scripting, false, false, false}},
    {"SDIFF", {command_family::set, false, true, false}},
    {"SDIFFSTORE", {command_family::set, false, false, false}},
    {"SELECT", {command_family::connection, false, false, false}},
    {"SET", {command_family::string, false, false, true}},
    {"SETBIT", {command_family::bitmap, false, false, true}},
    {"SETEX", {command_family::string, false, false, true}},
    {"SETNX", {command_family::string, false, false, false}},
    {"SETRANGE", {command_family::string, false, false, false}},
    {"SHUTDOWN", {command_family::server, false, false, false}},
    {"SINTER", {command_family::set, false, true, false}},
    {"SINTERCARD", {command_family::set, false, true, false}},
    {"SINTERSTORE", {command_family::set, false, false, false}},
    {"SISMEMBER", {command_family::set, false, true, false}},
    {"SLAVEOF", {command_family::server, false, false, false}},
    {"SLOWLOG", {command_family::server, false, false, false}},
    {"SMEMBERS", {command_family::set, false, true, false}},
    {"SMISMEMBER", {command_family::set, false, true, false}},
    {"SMOVE", {command_family::set, false, false, false}},
    {"SORT", {command_family::key, false, false, false}},
    {"SORT_RO", {command_family::key, false, true, false}},
    {"SPOP", {command_family::set, false, false, false}},
    {"SRANDMEMBER", {command_family::set, false, true, false}},
    {"SREM", {command_family::set, false, false, true}},
    {"SSCAN", {command_family::set, false, true, false}},
    {"STRLEN", {command_family::string, false, true, false}},
    {"SUBSCRIBE", {command_family::subscription, false, false, false}},
    {"SUBSTR", {command_family::string, false, true, false}},
    {"SUNION", {command_family::set, false, true, false}},
    {"SUNIONSTORE", {command_family::set, false, false, false}},
    {"SWAPDB", {command_family::connection, false, false, false}},
    {"SYNC", {command_family::server, false, false, false}},
    {"TIME", {command_family::server, false, true, false}},
    {"TOUCH", {command_family::key, false, false, false}},
    {"TTL", {command_family::key, false, true, false}},
    {"TYPE", {command_family::key, false, true, false}},
    {"UNLINK", {command_family::key, false, false, true}},
    {"UNSUBSCRIBE", {command_family::subscription, false, false, false}},
    {"UNWATCH", {command_family::transaction, false, false, false}},
    {"WAIT", {command_family::key, true, false, false}},
    {"WAITAOF", {command_family::key, true, false, false}},
    {"WATCH", {command_family::transaction, false, false, false}},
    {"XACK", {command_family::stream, false, false, false}},
    {"XADD", {command_family::stream, false, false, false}},
    {"XAUTOCLAIM", {command_family::stream, false, false, false}},
    {"XCLAIM", {command_family::stream, false, false, false}},
    {"XDEL", {command_family::stream, false, false, false}},
    {"XGROUP", {command_family::stream, false, false, false}},
    {"XINFO", {command_family::stream, false, false, false}},
    {"XLEN", {command_family::stream, false, true, false}},
    {"XPENDING", {command_family::stream, false, false, false}},
    {"XRANGE", {command_family::stream, false, true, false}},
    {"XREAD", {command_family::stream, true, false, false}},
    {"XREADGROUP", {command_family::stream, true, false, false}},
    {"XREVRANGE", {command_family::stream, false, true, false}},
    {"XTRIM", {command_family::stream, false, false, false}},
    {"ZADD", {command_family::sorted_set, false, false, false}},
    {"ZCARD", {command_family::sorted_set, false, true, false}},
    {"ZCOUNT", {command_family::sorted_set, false, true, false}},
    {"ZDIFF", {command_family::sorted_set, false, true, false}},
    {"ZDIFFSTORE", {command_family::sorted_set, false, false, false}},
    {"ZINCRBY", {command_family::sorted_set, false, false, false}},
    {"ZINTER", {command_family::sorted_set, false, true, false}},
    {"ZINTERCARD", {command_family::sorted_set, false, true, false}},
    {"ZINTERSTORE", {command_family::sorted_set, false, false, false}},
    {"ZLEXCOUNT", {command_family::sorted_set, false, true, false}},
    {"ZMPOP", {command_family::sorted_set, false, false, false}},
    {"ZMSCORE", {command_family::sorted_set, false, true, false}},
    {"ZPOPMAX", {command_family::sorted_set, false, false, false}},
    {"ZPOPMIN", {command_family::sorted_set, false, false, false}},
    {"ZRANDMEMBER", {command_family::sorted_set, false, true, false}},
    {"ZRANGE", {command_family::sorted_set, false, true, false}},
    {"ZRANGEBYLEX", {command_family::sorted_set, false, true, false}},
    {"ZRANGEBYSCORE", {command_family::sorted_set, false, true, false}},
    {"ZRANGESTORE", {command_family::sorted_set, false, false, false}},
    {"ZRANK", {command_family::sorted_set, false, true, false}},
    {"ZREM", {command_family::sorted_set, false, false, true}},
    {"ZREMRANGEBYLEX", {command_family::sorted_set, false, false, false}},
    {"ZREMRANGEBYRANK", {command_family::sorted_set, false, false, false}},
    {"ZREMRANGEBYSCORE", {command_family::sorted_set, false, false, false}},
    {"ZREVRANGE", {command_family::sorted_set, false, true, false}},
    {"ZREVRANGEBYLEX", {command_family::sorted_set, false, true, false}},
    {"ZREVRANGEBYSCORE", {command_family::sorted_set, false, true, false}},
    {"ZREVRANK", {command_family::sorted_set, false, true, false}},
    {"ZSCAN", {command_family::sorted_set, false, true, false}},
    {"ZSCORE", {command_family::sorted_set, false, true, false}},
    {"ZUNIONSTORE", {command_family::sorted_set, false, false, false}},
}};

/// The index of `verb` (exact spelling) in `command_table`, or `command_table.size()` when it is not there.
[[nodiscard]] inline std::size_t
command_index(std::string_view const verb) noexcept {
    static const qb::unordered_flat_map<std::string_view, std::uint16_t> index = [] {
        qb::unordered_flat_map<std::string_view, std::uint16_t> m;
        m.reserve(command_table.size());
        for (std::size_t i = 0; i < command_table.size(); ++i)
            m.emplace(command_table[i].verb, static_cast<std::uint16_t>(i));
        return m;
    }();
    const auto it = index.find(verb);
    return it == index.end() ? command_table.size() : it->second;
}

/// `command_index` of `verb` in upper case: the cold retry for a verb spelled otherwise (`"get"`).
[[nodiscard]] inline std::size_t
command_index_folded(std::string_view const verb) noexcept {
    std::array<char, 32> upper{};
    if (verb.size() > upper.size())
        return command_table.size();
    for (std::size_t i = 0; i < verb.size(); ++i) {
        const char c = verb[i];
        upper[i]     = (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
    }
    return command_index(std::string_view{upper.data(), verb.size()});
}

} // namespace detail

/**
 * @brief The traits of `verb`, in any letter case; `other` and all-false for a verb the client does not issue
 *        itself.
 */
[[nodiscard]] inline command_traits
command_traits_of(std::string_view const verb) noexcept {
    auto i = detail::command_index(verb);
    if (i == detail::command_table.size())
        i = detail::command_index_folded(verb);
    return i < detail::command_table.size() ? detail::command_table[i].traits : command_traits{};
}

/// How a command ended (`command_info::outcome`).
enum class command_outcome : std::uint8_t {
    ok,     ///< the server answered with a value
    error,  ///< the server answered with an error reply
    failed, ///< no answer: refused locally, the link lost, a timeout, the client down
};

/**
 * @brief One completed command, as `on_command_complete` reports it (qbm-pgsql's `query_info`).
 * @details The views are valid only during the call: copy what you keep. The arguments are not reported --
 *          they may carry credentials or personal data.
 */
struct command_info {
    /// The verb's family.
    command_family family{};
    /// The verb; empty only for an unknown verb past the client's 256 distinct ones.
    std::string_view name;
    /// From the command's send to its completion when latency stats are on; zero otherwise, and for a command
    /// that never left the client.
    qb::duration duration{};
    /// Whether the server answered with a value (a value the callback then fails to decode is still one).
    bool ok = false;
    /// ok, error (the server's error reply) or failed (no answer).
    command_outcome outcome{};
    /// The failure when `!ok`: the server's error text, or why there was no answer; empty on success.
    std::string_view error;
};

/**
 * @brief A latency distribution: bucket i counts the commands that took [2^i, 2^(i+1)) microseconds (bucket 0
 *        also everything under one microsecond; the last one everything past 2^31 us).
 */
struct latency_histogram {
    std::array<std::uint64_t, 32> buckets{};

    /// Record one span.
    void
    record(qb::duration const span) noexcept {
        const auto us = std::chrono::duration_cast<std::chrono::microseconds>(span).count();
        const auto u  = static_cast<std::uint64_t>(us > 0 ? us : 0);
        const auto i  = u == 0 ? std::size_t{0} : static_cast<std::size_t>(std::bit_width(u) - 1);
        ++buckets[i < buckets.size() ? i : buckets.size() - 1];
    }

    /// Spans recorded.
    [[nodiscard]] std::uint64_t
    count() const noexcept {
        std::uint64_t n = 0;
        for (auto const b : buckets)
            n += b;
        return n;
    }

    /**
     * @brief The upper bound of the bucket that holds the `p`-th quantile (0 < p <= 1): p50, p95 and p99 are
     *        `percentile(0.50)`, `percentile(0.95)`, `percentile(0.99)`. Zero when nothing was recorded.
     */
    [[nodiscard]] qb::duration
    percentile(double const p) const noexcept {
        const std::uint64_t total = count();
        if (total == 0)
            return qb::duration::zero();
        const double  wanted = p * static_cast<double>(total);
        std::uint64_t want   = static_cast<std::uint64_t>(wanted);
        if (static_cast<double>(want) < wanted)
            ++want; // the rank of the p-th quantile, rounded up
        if (want == 0)
            want = 1;
        std::uint64_t seen = 0;
        for (std::size_t i = 0; i < buckets.size(); ++i) {
            seen += buckets[i];
            if (seen >= want)
                return std::chrono::microseconds{std::int64_t{1} << (i + 1)};
        }
        return std::chrono::microseconds{std::int64_t{1} << buckets.size()};
    }
};

/**
 * @brief A snapshot of what one client or consumer has done (`stats()`).
 * @details Every count is monotonic since construction: two snapshots give a rate, and a reconnection resets
 *          nothing. On a client every command is counted once, when it completes -- answered, refused by the
 *          server, or failed without an answer, refused locally included: `commands` equals the callbacks the
 *          client called. On a consumer the counts are its connection's and its stream's.
 */
struct stats {
    std::uint64_t                               commands = 0; ///< commands completed (a client's)
    std::uint64_t                               errors   = 0; ///< of `commands`, those that did not succeed: an error reply, or no answer
    std::uint64_t                               failed   = 0; ///< of `errors`, those the server never answered
    std::uint64_t                               held     = 0; ///< commands that waited for the client to be ready
    std::array<std::uint64_t, command_families> by_family{};  ///< `commands`, by family (`command_family`)
    std::uint64_t reconnects        = 0; ///< ready connections reached after a lost one (`disconnect()` ends the session: none)
    std::uint64_t gaps              = 0; ///< gap markers a consumer put in its stream: after a reconnection, or an overflow
    std::uint64_t messages_received = 0; ///< pub/sub messages a consumer received
    std::uint64_t messages_dropped  = 0; ///< messages a `co_consumer` dropped because its queue was full
};

} // namespace qb::redis

#endif // QBM_REDIS_STATS_H
