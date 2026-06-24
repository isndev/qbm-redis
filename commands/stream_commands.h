/**
 * @file qbm/redis/commands/stream_commands.h
 * @brief Redis stream command mixin for the qb Redis module.
 *
 * Provides the @ref qb::redis::stream_commands CRTP mixin implementing the Redis
 * stream command family (XADD, XREAD, XRANGE, XGROUP, XCLAIM, XPENDING, ...).
 * Every command is exposed both as a coroutine-awaitable form and as a
 * callback-based async form that returns a reference to the derived handler for
 * chaining.
 *
 *            See accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0
 *
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2026 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)
 * @ingroup Redis
 */
#ifndef QBM_REDIS_STREAM_COMMANDS_H
#define QBM_REDIS_STREAM_COMMANDS_H
#include "../reply.h"

namespace qb::redis {

/**
 * @class stream_commands
 * @brief Provides Redis stream command implementations.
 *
 * This class implements Redis commands for working with Redis streams.
 * Commands return awaiters for coroutine-first async I/O.
 *
 * @tparam Derived The derived class type (CRTP pattern)
 */
template <typename Derived>
class stream_commands {
private:
    constexpr Derived &
    derived() {
        return static_cast<Derived &>(*this);
    }

public:
    /**
     * @brief Add entries to a stream
     *
     * Adds one or more entries to the specified stream. Each entry consists of
     * field-value pairs. If the stream doesn't exist, it will be created.
     *
     * @param key The key of the stream to add entries to
     * @param entries Vector of field-value pairs to add to the stream
     * @param id Optional message ID. If not specified, Redis will auto-generate one
     * @return The ID of the added entry as a stream_id
     */
    auto
    xadd(const std::string &key, const std::vector<std::pair<std::string, std::string>> &entries,
         const std::optional<std::string> &id = std::nullopt) {
        return derived().template make_coro_command<stream_id>(
            [this, key, entries, id](auto &&callback) { this->xadd(std::move(callback), key, entries, id); });
    }

    /**
     * @brief Asynchronous version of xadd
     *
     * @tparam Func Callback function type that accepts a Reply<stream_id>
     * @param func Callback function to be called with the result
     * @param key The key of the stream to add entries to
     * @param entries Vector of field-value pairs to add to the stream
     * @param id Optional message ID. If not specified, Redis will auto-generate one
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<stream_id> &&>, Derived &>
    xadd(Func &&func, const std::string &key, const std::vector<std::pair<std::string, std::string>> &entries,
         const std::optional<std::string> &id = std::nullopt) {
        return derived().template command<stream_id>(std::forward<Func>(func), "XADD", key, id ? *id : "*", entries);
    }

    /**
     * @brief Helper function to parse a stream ID string into a stream_id struct
     *
     * @param id_str The stream ID string in the format "timestamp-sequence"
     * @return The parsed stream_id
     */
    static stream_id
    parse_stream_id(const std::string &id_str) {
        stream_id result{};
        auto      pos = id_str.find('-');
        if (pos != std::string::npos) {
            try {
                result.timestamp = std::stoll(id_str.substr(0, pos));
                result.sequence  = std::stoll(id_str.substr(pos + 1));
            } catch (const std::exception &) {
                // In case of parsing error, return default values
            }
        }
        return result;
    }

    /**
     * @brief Returns the number of entries in a stream (coroutine awaitable)
     *
     * @param key Key where the stream is stored
     * @return redis_awaiter yielding Reply<long long>
     * @see https://redis.io/commands/xlen
     */
    auto
    xlen(const std::string &key) {
        return derived().template make_coro_command<long long>([this, key](auto &&callback) { this->xlen(std::move(callback), key); });
    }

    /**
     * @brief Asynchronous version of xlen
     *
     * @tparam Func Callback function type that accepts a Reply<long long>
     * @param func Callback function to be called with the result
     * @param key The key of the stream to get the length of
     * @return Reference to the Redis handler for chaining
     * @see https://redis.io/commands/xlen
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    xlen(Func &&func, const std::string &key) {
        return derived().template command<long long>(std::forward<Func>(func), "XLEN", key);
    }

    /**
     * @brief Deletes entries from a stream (coroutine awaitable)
     *
     * @tparam Ids Variadic types for entry IDs to delete
     * @param key Key where the stream is stored
     * @param ids Entry IDs to delete
     * @return redis_awaiter yielding Reply<long long> (number of entries deleted)
     * @see https://redis.io/commands/xdel
     */
    template <typename... Ids>
    auto
    xdel(const std::string &key, Ids &&...ids) {
        return derived().template make_coro_command<long long>([this, key, ... ids = std::forward<Ids>(ids)](auto &&callback) mutable {
            this->xdel(std::move(callback), key, std::forward<Ids>(ids)...);
        });
    }

    /**
     * @brief Asynchronous version of xdel
     *
     * @tparam Func Callback function type that accepts a Reply<long long>
     * @tparam Ids Variadic types for entry IDs
     * @param func Callback function to be called with the result
     * @param key The key of the stream to delete entries from
     * @param ids Variadic list of entry IDs to delete
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func, typename... Ids>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    xdel(Func &&func, const std::string &key, Ids &&...ids) {
        return derived().template command<long long>(std::forward<Func>(func), "XDEL", key, std::forward<Ids>(ids)...);
    }

    /**
     * @brief Create a consumer group
     *
     * Creates a new consumer group for the specified stream. A consumer group
     * allows multiple consumers to process different parts of the stream.
     *
     * @param key The key of the stream to create the group for
     * @param group The name of the consumer group to create
     * @param id The starting ID for the group. Use "0" to start from the beginning
     * @param mkstream Whether to create the stream if it doesn't exist
     * @return status object indicating success or failure
     */
    auto
    xgroup_create(const std::string &key, const std::string &group, const std::string &id, bool mkstream = false) {
        return derived().template make_coro_command<status>(
            [this, key, group, id, mkstream](auto &&callback) { this->xgroup_create(std::move(callback), key, group, id, mkstream); });
    }

    /**
     * @brief Asynchronous version of xgroup_create
     *
     * @tparam Func Callback function type that accepts a Reply<status>
     * @param func Callback function to be called with the result
     * @param key The key of the stream to create the group for
     * @param group The name of the consumer group to create
     * @param id The starting ID for the group. Use "0" to start from the beginning
     * @param mkstream Whether to create the stream if it doesn't exist
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    xgroup_create(Func &&func, const std::string &key, const std::string &group, const std::string &id, bool mkstream = false) {
        std::vector<std::string> args;
        args.push_back("CREATE");
        args.push_back(key);
        args.push_back(group);
        args.push_back(id);
        if (mkstream) {
            args.push_back("MKSTREAM");
        }
        return derived().template command<status>(std::forward<Func>(func), "XGROUP", args);
    }

    /**
     * @brief Delete a consumer group
     *
     * Removes the specified consumer group from the stream. This operation
     * is irreversible and will remove all pending messages for the group.
     *
     * @param key The key of the stream containing the group
     * @param group The name of the consumer group to delete
     * @return The number of messages that were deleted
     */
    auto
    xgroup_destroy(const std::string &key, const std::string &group) {
        return derived().template make_coro_command<long long>(
            [this, key, group](auto &&callback) { this->xgroup_destroy(std::move(callback), key, group); });
    }

    /**
     * @brief Asynchronous version of xgroup_destroy
     *
     * @tparam Func Callback function type that accepts a Reply<long long>
     * @param func Callback function to be called with the result
     * @param key The key of the stream containing the group
     * @param group The name of the consumer group to delete
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    xgroup_destroy(Func &&func, const std::string &key, const std::string &group) {
        return derived().template command<long long>(std::forward<Func>(func), "XGROUP", "DESTROY", key, group);
    }

    /**
     * @brief Removes a consumer from a consumer group (coroutine awaitable)
     *
     * @param key Key where the stream is stored
     * @param group Consumer group name
     * @param consumer Consumer name to remove
     * @return redis_awaiter yielding Reply<long long> (number of pending messages deleted)
     * @see https://redis.io/commands/xgroup-delconsumer
     */
    auto
    xgroup_delconsumer(const std::string &key, const std::string &group, const std::string &consumer) {
        return derived().template make_coro_command<long long>(
            [this, key, group, consumer](auto &&callback) { this->xgroup_delconsumer(std::move(callback), key, group, consumer); });
    }

    /**
     * @brief Asynchronous version of xgroup_delconsumer
     *
     * @tparam Func Callback function type that accepts a Reply<long long>
     * @param func Callback function to be called with the result
     * @param key The key of the stream containing the group
     * @param group The name of the consumer group
     * @param consumer The name of the consumer to delete
     * @return Reference to the Redis handler for chaining
     * @see https://redis.io/commands/xgroup-delconsumer
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    xgroup_delconsumer(Func &&func, const std::string &key, const std::string &group, const std::string &consumer) {
        return derived().template command<long long>(std::forward<Func>(func), "XGROUP", "DELCONSUMER", key, group, consumer);
    }

    /**
     * @brief Acknowledges messages in a consumer group (coroutine awaitable)
     *
     * @tparam Ids Variadic types for message IDs to acknowledge
     * @param key Key where the stream is stored
     * @param group Consumer group name
     * @param ids Message IDs to acknowledge
     * @return redis_awaiter yielding Reply<long long> (number of messages acknowledged)
     * @see https://redis.io/commands/xack
     */
    template <typename... Ids>
    auto
    xack(const std::string &key, const std::string &group, Ids &&...ids) {
        return derived().template make_coro_command<long long>([this, key, group, ... ids = std::forward<Ids>(ids)](auto &&callback) mutable {
            this->xack(std::move(callback), key, group, std::forward<Ids>(ids)...);
        });
    }

    /**
     * @brief Asynchronous version of xack
     *
     * @tparam Func Callback function type that accepts a Reply<long long>
     * @tparam Ids Variadic types for message IDs
     * @param func Callback function to be called with the result
     * @param key The key of the stream containing the group
     * @param group The name of the consumer group
     * @param ids Variadic list of message IDs to acknowledge
     * @return Reference to the Redis handler for chaining
     * @see https://redis.io/commands/xack
     */
    template <typename Func, typename... Ids>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    xack(Func &&func, const std::string &key, const std::string &group, Ids &&...ids) {
        return derived().template command<long long>(std::forward<Func>(func), "XACK", key, group, std::forward<Ids>(ids)...);
    }

    /**
     * @brief Trims a stream to a maximum length (coroutine awaitable)
     *
     * @param key Key where the stream is stored
     * @param maxlen Maximum number of entries to retain
     * @param approximate If true, use approximate trimming for better performance
     * @return redis_awaiter yielding Reply<long long> (number of entries deleted)
     * @see https://redis.io/commands/xtrim
     */
    auto
    xtrim(const std::string &key, long long maxlen, bool approximate = false) {
        return derived().template make_coro_command<long long>(
            [this, key, maxlen, approximate](auto &&callback) { this->xtrim(std::move(callback), key, maxlen, approximate); });
    }

    /**
     * @brief Asynchronous version of xtrim
     *
     * @tparam Func Callback function type that accepts a Reply<long long>
     * @param func Callback function to be called with the result
     * @param key The key of the stream to trim
     * @param maxlen The maximum length to trim the stream to
     * @param approximate Whether to use approximate trimming. If true, Redis will
     *                    use a probabilistic algorithm that may not be exact but is
     *                    more efficient
     * @return Reference to the Redis handler for chaining
     * @see https://redis.io/commands/xtrim
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    xtrim(Func &&func, const std::string &key, long long maxlen, bool approximate = false) {
        std::vector<std::string> args;
        args.push_back(key);
        if (approximate) {
            args.push_back("MAXLEN");
            args.push_back("~");
        } else {
            args.push_back("MAXLEN");
            args.push_back("=");
        }
        args.push_back(std::to_string(maxlen));
        return derived().template command<long long>(std::forward<Func>(func), "XTRIM", args);
    }

    /**
     * @brief Read entries from a stream using a consumer group
     *
     * @param key Stream key
     * @param group Group name
     * @param consumer Consumer name
     * @param id ID to read from (">" for unread messages, "0" for all messages)
     * @param count Optional maximum number of entries to read
     * @param block Optional timeout in milliseconds to block waiting for new messages
     * @return qb::json structured representation of stream entries
     */
    auto
    xreadgroup(const std::string &key, const std::string &group, const std::string &consumer, const std::string &id,
               std::optional<long long> count = std::nullopt, std::optional<long long> block = std::nullopt) {
        return derived().template make_coro_command<qb::json>([this, key, group, consumer, id, count, block](auto &&callback) mutable {
            this->xreadgroup(std::move(callback), key, group, consumer, id, std::move(count), std::move(block));
        });
    }

    /**
     * @brief Asynchronous version of xreadgroup
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Stream key
     * @param group Group name
     * @param consumer Consumer name
     * @param id ID to read from (">" for unread messages, "0" for all messages)
     * @param count Optional maximum number of entries to read
     * @param block Optional timeout in milliseconds to block waiting for new messages
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::json> &&>, Derived &>
    xreadgroup(Func &&func, const std::string &key, const std::string &group, const std::string &consumer, const std::string &id,
               std::optional<long long> count = std::nullopt, std::optional<long long> block = std::nullopt) {
        std::vector<std::string> args = {"GROUP", group, consumer};
        if (count)
            args.insert(args.end(), {"COUNT", std::to_string(*count)});
        if (block)
            args.insert(args.end(), {"BLOCK", std::to_string(*block)});
        return derived().template command<qb::json>(std::forward<Func>(func), "XREADGROUP", args, "STREAMS", key, id);
    }

    /**
     * @brief Read entries from multiple streams using a consumer group
     *
     * @param keys Vector of stream keys
     * @param group Group name
     * @param consumer Consumer name
     * @param ids Vector of IDs to read from, one per stream
     * @param count Optional maximum number of entries to read
     * @param block Optional timeout in milliseconds to block waiting for new messages
     * @return qb::json structured representation of stream entries by key
     */
    auto
    xreadgroup(const std::vector<std::string> &keys, const std::string &group, const std::string &consumer, const std::vector<std::string> &ids,
               std::optional<long long> count = std::nullopt, std::optional<long long> block = std::nullopt) {
        return derived().template make_coro_command<qb::json>([this, keys, group, consumer, ids, count, block](auto &&callback) mutable {
            this->xreadgroup(std::move(callback), keys, group, consumer, ids, std::move(count), std::move(block));
        });
    }

    /**
     * @brief Asynchronous version of xreadgroup for multiple streams
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param keys Vector of stream keys
     * @param group Group name
     * @param consumer Consumer name
     * @param ids Vector of IDs to read from, one per stream
     * @param count Optional maximum number of entries to read
     * @param block Optional timeout in milliseconds to block waiting for new messages
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::json> &&>, Derived &>
    xreadgroup(Func &&func, const std::vector<std::string> &keys, const std::string &group, const std::string &consumer,
               const std::vector<std::string> &ids, std::optional<long long> count = std::nullopt,
               std::optional<long long> block = std::nullopt) {
        if (keys.empty() || keys.size() != ids.size()) {
            throw std::invalid_argument("Keys and IDs must be non-empty and have the same size");
        }

        std::vector<std::string> args = {"GROUP", group, consumer};
        if (count)
            args.insert(args.end(), {"COUNT", std::to_string(*count)});
        if (block)
            args.insert(args.end(), {"BLOCK", std::to_string(*block)});

        return derived().template command<qb::json>(std::forward<Func>(func), "XREADGROUP", args, "STREAMS", keys, ids);
    }

    /**
     * @brief Read entries from a single stream
     *
     * @param key Stream key
     * @param id ID to read from ("$" for new messages only, "0" for all messages)
     * @param count Optional maximum number of entries to read
     * @param block Optional timeout in milliseconds to block waiting for new messages
     * @return qb::json structured representation of stream entries
     * @see https://redis.io/commands/xread
     */
    auto
    xread(const std::string &key, const std::string &id, std::optional<long long> count = std::nullopt,
          std::optional<long long> block = std::nullopt) {
        return derived().template make_coro_command<qb::json>([this, key, id, count, block](auto &&callback) mutable {
            this->xread(std::move(callback), key, id, std::move(count), std::move(block));
        });
    }

    /**
     * @brief Asynchronous version of xread
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Stream key
     * @param id ID to read from ("$" for new messages only, "0" for all messages)
     * @param count Optional maximum number of entries to read
     * @param block Optional timeout in milliseconds to block waiting for new messages
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::json> &&>, Derived &>
    xread(Func &&func, const std::string &key, const std::string &id, std::optional<long long> count = std::nullopt,
          std::optional<long long> block = std::nullopt) {
        std::vector<std::string> args;
        if (count)
            args.insert(args.end(), {"COUNT", std::to_string(*count)});
        if (block)
            args.insert(args.end(), {"BLOCK", std::to_string(*block)});

        return derived().template command<qb::json>(std::forward<Func>(func), "XREAD", args, "STREAMS", key, id);
    }

    /**
     * @brief Read entries from multiple streams
     *
     * @param keys Vector of stream keys
     * @param ids Vector of IDs to read from, one per stream
     * @param count Optional maximum number of entries to read
     * @param block Optional timeout in milliseconds to block waiting for new messages
     * @return qb::json structured representation of stream entries by key
     */
    auto
    xread(const std::vector<std::string> &keys, const std::vector<std::string> &ids, std::optional<long long> count = std::nullopt,
          std::optional<long long> block = std::nullopt) {
        return derived().template make_coro_command<qb::json>([this, keys, ids, count, block](auto &&callback) mutable {
            this->xread(std::move(callback), keys, ids, std::move(count), std::move(block));
        });
    }

    /**
     * @brief Asynchronous version of xread for multiple streams
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param keys Vector of stream keys
     * @param ids Vector of IDs to read from, one per stream
     * @param count Optional maximum number of entries to read
     * @param block Optional timeout in milliseconds to block waiting for new messages
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::json> &&>, Derived &>
    xread(Func &&func, const std::vector<std::string> &keys, const std::vector<std::string> &ids, std::optional<long long> count = std::nullopt,
          std::optional<long long> block = std::nullopt) {
        if (keys.empty() || keys.size() != ids.size()) {
            throw std::invalid_argument("Keys and IDs must be non-empty and have the same size");
        }

        std::vector<std::string> args;
        if (count)
            args.insert(args.end(), {"COUNT", std::to_string(*count)});
        if (block)
            args.insert(args.end(), {"BLOCK", std::to_string(*block)});

        return derived().template command<qb::json>(std::forward<Func>(func), "XREAD", args, "STREAMS", keys, ids);
    }

    /**
     * @brief Returns information about a stream (coroutine awaitable)
     *
     * @param key Key where the stream is stored
     * @return redis_awaiter yielding Reply<qb::json>
     * @see https://redis.io/commands/xinfo-stream
     */
    auto
    xinfo_stream(const std::string &key) {
        return derived().template make_coro_command<qb::json>([this, key](auto &&callback) { this->xinfo_stream(std::move(callback), key); });
    }

    /**
     * @brief Asynchronous version of xinfo_stream
     *
     * @param func Callback function to handle the result
     * @param key The key of the stream to get information about
     * @return Reference to the derived class
     * @see https://redis.io/commands/xinfo-stream
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::json> &&>, Derived &>
    xinfo_stream(Func &&func, const std::string &key) {
        return derived().template command<qb::json>(std::forward<Func>(func), "XINFO", "STREAM", key);
    }

    /**
     * @brief Returns information about consumer groups of a stream (coroutine awaitable)
     *
     * @param key Key where the stream is stored
     * @return redis_awaiter yielding Reply<qb::json>
     * @see https://redis.io/commands/xinfo-groups
     */
    auto
    xinfo_groups(const std::string &key) {
        return derived().template make_coro_command<qb::json>([this, key](auto &&callback) { this->xinfo_groups(std::move(callback), key); });
    }

    /**
     * @brief Asynchronous version of xinfo_groups
     *
     * @param func Callback function to handle the result
     * @param key The key of the stream
     * @return Reference to the derived class
     * @see https://redis.io/commands/xinfo-groups
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::json> &&>, Derived &>
    xinfo_groups(Func &&func, const std::string &key) {
        return derived().template command<qb::json>(std::forward<Func>(func), "XINFO", "GROUPS", key);
    }

    /**
     * @brief Returns information about consumers in a consumer group (coroutine awaitable)
     *
     * @param key Key where the stream is stored
     * @param group Consumer group name
     * @return redis_awaiter yielding Reply<qb::json>
     * @see https://redis.io/commands/xinfo-consumers
     */
    auto
    xinfo_consumers(const std::string &key, const std::string &group) {
        return derived().template make_coro_command<qb::json>(
            [this, key, group](auto &&callback) { this->xinfo_consumers(std::move(callback), key, group); });
    }

    /**
     * @brief Asynchronous version of xinfo_consumers
     *
     * @param func Callback function to handle the result
     * @param key The key of the stream
     * @param group The name of the consumer group
     * @return Reference to the derived class
     * @see https://redis.io/commands/xinfo-consumers
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::json> &&>, Derived &>
    xinfo_consumers(Func &&func, const std::string &key, const std::string &group) {
        return derived().template command<qb::json>(std::forward<Func>(func), "XINFO", "CONSUMERS", key, group);
    }

    /**
     * @brief Returns help for XINFO subcommands (coroutine awaitable)
     *
     * @return redis_awaiter yielding Reply<qb::json>
     * @see https://redis.io/commands/xinfo-help
     */
    auto
    xinfo_help() {
        return derived().template make_coro_command<qb::json>([this](auto &&callback) { this->xinfo_help(std::move(callback)); });
    }

    /**
     * @brief Asynchronous version of xinfo_help
     *
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     * @see https://redis.io/commands/xinfo-help
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::json> &&>, Derived &>
    xinfo_help(Func &&func) {
        return derived().template command<qb::json>(std::forward<Func>(func), "XINFO", "HELP");
    }

    /**
     * @brief Get detailed information about pending messages in a consumer group
     *
     * This advanced version of XPENDING returns detailed information about pending
     * messages as a structured JSON object.
     *
     * @param key The key of the stream
     * @param group The name of the consumer group
     * @param start The start of the ID range (e.g., "-" for minimum ID)
     * @param end The end of the ID range (e.g., "+" for maximum ID)
     * @param count Maximum number of messages to return
     * @param consumer Optional consumer name to filter messages
     * @return qb::json object with detailed pending message information
     * @see https://redis.io/commands/xpending
     */
    auto
    xpending(const std::string &key, const std::string &group, const std::string &start = "-", const std::string &end = "+",
             long long count = 10, const std::optional<std::string> &consumer = std::nullopt) {
        return derived().template make_coro_command<qb::json>([this, key, group, start, end, count, consumer](auto &&callback) mutable {
            this->xpending(std::move(callback), key, group, start, end, count, std::move(consumer));
        });
    }

    /**
     * @brief Asynchronous version of xpending
     *
     * @param func Callback function to handle the result
     * @param key The key of the stream
     * @param group The name of the consumer group
     * @param start The start of the ID range
     * @param end The end of the ID range
     * @param count Maximum number of messages to return
     * @param consumer Optional consumer name to filter messages
     * @return Reference to the derived class
     * @see https://redis.io/commands/xpending
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::json> &&>, Derived &>
    xpending(Func &&func, const std::string &key, const std::string &group, const std::string &start = "-", const std::string &end = "+",
             long long count = 10, const std::optional<std::string> &consumer = std::nullopt) {
        std::vector<std::string> args;
        args.push_back(key);
        args.push_back(group);
        args.push_back(start);
        args.push_back(end);
        args.push_back(std::to_string(count));
        if (consumer) {
            args.push_back(*consumer);
        }
        return derived().template command<qb::json>(std::forward<Func>(func), "XPENDING", args);
    }

    /**
     * @brief Read a range of entries from a stream (coroutine awaitable)
     *
     * @param key Stream key
     * @param start Start ID ("-" for beginning)
     * @param end End ID ("+" for end)
     * @param count Optional maximum number of entries to return
     * @return redis_awaiter yielding Reply<stream_entry_list>
     * @see https://redis.io/commands/xrange
     */
    auto
    xrange(const std::string &key, const std::string &start, const std::string &end, std::optional<long long> count = std::nullopt) {
        return derived().template make_coro_command<stream_entry_list>(
            [this, key, start, end, count](auto &&callback) { this->xrange(std::move(callback), key, start, end, count); });
    }

    /**
     * @brief Asynchronous version of xrange
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @param key Stream key
     * @param start Start ID
     * @param end End ID
     * @param count Optional maximum number of entries
     * @return Reference to the derived class
     * @see https://redis.io/commands/xrange
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<stream_entry_list> &&>, Derived &>
    xrange(Func &&func, const std::string &key, const std::string &start, const std::string &end,
           std::optional<long long> count = std::nullopt) {
        std::vector<std::string> opt;
        if (count) {
            opt.push_back("COUNT");
            opt.push_back(std::to_string(*count));
        }
        return derived().template command<stream_entry_list>(std::forward<Func>(func), "XRANGE", key, start, end, opt);
    }

    /**
     * @brief Read a range of entries from a stream in reverse order (coroutine awaitable)
     *
     * @param key Stream key
     * @param end End ID (higher bound, "+" for end)
     * @param start Start ID (lower bound, "-" for beginning)
     * @param count Optional maximum number of entries to return
     * @return redis_awaiter yielding Reply<stream_entry_list>
     * @see https://redis.io/commands/xrevrange
     */
    auto
    xrevrange(const std::string &key, const std::string &end, const std::string &start, std::optional<long long> count = std::nullopt) {
        return derived().template make_coro_command<stream_entry_list>(
            [this, key, end, start, count](auto &&callback) { this->xrevrange(std::move(callback), key, end, start, count); });
    }

    /**
     * @brief Asynchronous version of xrevrange
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @param key Stream key
     * @param end End ID
     * @param start Start ID
     * @param count Optional maximum number of entries
     * @return Reference to the derived class
     * @see https://redis.io/commands/xrevrange
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<stream_entry_list> &&>, Derived &>
    xrevrange(Func &&func, const std::string &key, const std::string &end, const std::string &start,
              std::optional<long long> count = std::nullopt) {
        std::vector<std::string> opt;
        if (count) {
            opt.push_back("COUNT");
            opt.push_back(std::to_string(*count));
        }
        return derived().template command<stream_entry_list>(std::forward<Func>(func), "XREVRANGE", key, end, start, opt);
    }

    /**
     * @brief Claim pending messages for a consumer (coroutine awaitable)
     *
     * @param key Stream key
     * @param group Consumer group name
     * @param consumer Consumer name claiming the messages
     * @param min_idle_time Minimum idle time in milliseconds
     * @param ids Message IDs to claim
     * @param options Optional vector of options (e.g. "IDLE", "TIME", "RETRYCOUNT")
     * @return redis_awaiter yielding Reply<stream_entry_list>
     * @see https://redis.io/commands/xclaim
     */
    auto
    xclaim(const std::string &key, const std::string &group, const std::string &consumer, long long min_idle_time,
           const std::vector<std::string> &ids, const std::vector<std::string> &options = {}) {
        return derived().template make_coro_command<stream_entry_list>(
            [this, key, group, consumer, min_idle_time, ids, options](auto &&callback) {
                this->xclaim(std::move(callback), key, group, consumer, min_idle_time, ids, options);
            });
    }

    /**
     * @brief Asynchronous version of xclaim
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @param key Stream key
     * @param group Consumer group name
     * @param consumer Consumer name
     * @param min_idle_time Minimum idle time in milliseconds
     * @param ids Message IDs to claim
     * @param options Optional options vector
     * @return Reference to the derived class
     * @see https://redis.io/commands/xclaim
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<stream_entry_list> &&>, Derived &>
    xclaim(Func &&func, const std::string &key, const std::string &group, const std::string &consumer, long long min_idle_time,
           const std::vector<std::string> &ids, const std::vector<std::string> &options = {}) {
        std::vector<std::string> args = {key, group, consumer, std::to_string(min_idle_time)};
        args.insert(args.end(), ids.begin(), ids.end());
        args.insert(args.end(), options.begin(), options.end());
        return derived().template command<stream_entry_list>(std::forward<Func>(func), "XCLAIM", args);
    }

    /**
     * @brief Automatically claim idle pending messages (coroutine awaitable)
     *
     * @param key Stream key
     * @param group Consumer group name
     * @param consumer Consumer name claiming the messages
     * @param min_idle_time Minimum idle time in milliseconds
     * @param start ID from which to start scanning for pending messages
     * @param count Optional maximum number of messages to attempt to claim
     * @param justid If true, return only message IDs without their fields
     * @return qb::json structured representation of the claimed messages
     * @see https://redis.io/commands/xautoclaim
     */
    auto
    xautoclaim(const std::string &key, const std::string &group, const std::string &consumer, long long min_idle_time, const std::string &start,
               std::optional<long long> count = std::nullopt, bool justid = false) {
        return derived().template make_coro_command<qb::json>(
            [this, key, group, consumer, min_idle_time, start, count, justid](auto &&callback) {
                this->xautoclaim(std::move(callback), key, group, consumer, min_idle_time, start, count, justid);
            });
    }

    /**
     * @brief Asynchronous version of xautoclaim
     *
     * @tparam Func Callback function type that accepts a Reply<qb::json>
     * @param func Callback function to handle the result
     * @param key Stream key
     * @param group Consumer group name
     * @param consumer Consumer name claiming the messages
     * @param min_idle_time Minimum idle time in milliseconds
     * @param start ID from which to start scanning for pending messages
     * @param count Optional maximum number of messages to attempt to claim
     * @param justid If true, return only message IDs without their fields
     * @return Reference to the derived class
     * @see https://redis.io/commands/xautoclaim
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::json> &&>, Derived &>
    xautoclaim(Func &&func, const std::string &key, const std::string &group, const std::string &consumer, long long min_idle_time,
               const std::string &start, std::optional<long long> count = std::nullopt, bool justid = false) {
        std::vector<std::string> opt;
        if (count) {
            opt.push_back("COUNT");
            opt.push_back(std::to_string(*count));
        }
        if (justid)
            opt.push_back("JUSTID");
        return derived().template command<qb::json>(std::forward<Func>(func), "XAUTOCLAIM", key, group, consumer, std::to_string(min_idle_time),
                                                    start, opt);
    }

    /**
     * @brief Set the last delivered ID of a consumer group (coroutine awaitable)
     *
     * @param key Stream key
     * @param group Consumer group name
     * @param id The new last-delivered ID for the group
     * @param entries_read Optional number of entries already read by the group
     * @return status object indicating success or failure
     * @see https://redis.io/commands/xgroup-setid
     */
    auto
    xgroupSetid(const std::string &key, const std::string &group, const std::string &id, std::optional<long long> entries_read = std::nullopt) {
        return derived().template make_coro_command<status>(
            [this, key, group, id, entries_read](auto &&callback) { this->xgroupSetid(std::move(callback), key, group, id, entries_read); });
    }

    /**
     * @brief Asynchronous version of xgroupSetid
     *
     * @tparam Func Callback function type that accepts a Reply<status>
     * @param func Callback function to handle the result
     * @param key Stream key
     * @param group Consumer group name
     * @param id The new last-delivered ID for the group
     * @param entries_read Optional number of entries already read by the group
     * @return Reference to the derived class
     * @see https://redis.io/commands/xgroup-setid
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    xgroupSetid(Func &&func, const std::string &key, const std::string &group, const std::string &id,
                std::optional<long long> entries_read = std::nullopt) {
        std::vector<std::string> opt;
        if (entries_read) {
            opt.push_back("ENTRIESREAD");
            opt.push_back(std::to_string(*entries_read));
        }
        return derived().template command<status>(std::forward<Func>(func), "XGROUP", "SETID", key, group, id, opt);
    }

    /**
     * @brief Create a consumer in a consumer group (coroutine awaitable)
     *
     * @param key Stream key
     * @param group Consumer group name
     * @param consumer Name of the consumer to create
     * @return true if the consumer was created, false if it already existed
     * @see https://redis.io/commands/xgroup-createconsumer
     */
    auto
    xgroupCreateconsumer(const std::string &key, const std::string &group, const std::string &consumer) {
        return derived().template make_coro_command<bool>(
            [this, key, group, consumer](auto &&callback) { this->xgroupCreateconsumer(std::move(callback), key, group, consumer); });
    }

    /**
     * @brief Asynchronous version of xgroupCreateconsumer
     *
     * @tparam Func Callback function type that accepts a Reply<bool>
     * @param func Callback function to handle the result
     * @param key Stream key
     * @param group Consumer group name
     * @param consumer Name of the consumer to create
     * @return Reference to the derived class
     * @see https://redis.io/commands/xgroup-createconsumer
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    xgroupCreateconsumer(Func &&func, const std::string &key, const std::string &group, const std::string &consumer) {
        return derived().template command<bool>(std::forward<Func>(func), "XGROUP", "CREATECONSUMER", key, group, consumer);
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_STREAM_COMMANDS_H