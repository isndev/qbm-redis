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

#ifndef QBM_REDIS_STREAM_COMMANDS_H
#define QBM_REDIS_STREAM_COMMANDS_H
#include "reply.h"

namespace qb::redis {

/**
 * @class stream_commands
 * @brief Provides Redis stream command implementations.
 *
 * This class implements Redis commands for working with Redis streams,
 * including operations for adding, reading, and managing stream entries.
 * Redis streams are append-only data structures that can be used for
 * message queues, event sourcing, and other time-series data use cases.
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
    stream_id
    xadd(const std::string                                      &key,
         const std::vector<std::pair<std::string, std::string>> &entries,
         const std::optional<std::string>                       &id = std::nullopt) {
        std::string id_str =
            derived()
                .template command<std::string>("XADD", key, id ? *id : "*", entries)
                .result();
        return parse_stream_id(id_str);
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
    xadd(Func &&func, const std::string &key,
         const std::vector<std::pair<std::string, std::string>> &entries,
         const std::optional<std::string>                       &id = std::nullopt) {
        return derived().template command<stream_id>(std::forward<Func>(func), "XADD",
                                                     key, id ? *id : "*", entries);
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
     * @brief Get the length of a stream
     *
     * Returns the number of entries in the specified stream. If the stream
     * doesn't exist, returns 0.
     *
     * @param key The key of the stream to get the length of
     * @return The number of entries in the stream
     */
    long long
    xlen(const std::string &key) {
        return derived().template command<long long>("XLEN", key).result();
    }

    /**
     * @brief Asynchronous version of xlen
     *
     * @tparam Func Callback function type that accepts a Reply<long long>
     * @param func Callback function to be called with the result
     * @param key The key of the stream to get the length of
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    xlen(Func &&func, const std::string &key) {
        return derived().template command<long long>(std::forward<Func>(func), "XLEN",
                                                     key);
    }

    /**
     * @brief Delete entries from a stream
     *
     * Removes the specified entries from the stream by their IDs.
     *
     * @param key The key of the stream to delete entries from
     * @param ids Variadic list of entry IDs to delete
     * @return The number of entries actually deleted
     */
    template <typename... Ids>
    long long
    xdel(const std::string &key, Ids &&...ids) {
        return derived()
            .template command<long long>("XDEL", key, std::forward<Ids>(ids)...)
            .result();
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
        return derived().template command<long long>(std::forward<Func>(func), "XDEL",
                                                     key, std::forward<Ids>(ids)...);
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
    status
    xgroup_create(const std::string &key, const std::string &group,
                  const std::string &id, bool mkstream = false) {
        std::vector<std::string> args;
        args.push_back("CREATE");
        args.push_back(key);
        args.push_back(group);
        args.push_back(id);
        if (mkstream) {
            args.push_back("MKSTREAM");
        }
        return derived().template command<status>("XGROUP", args).result();
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
    xgroup_create(Func &&func, const std::string &key, const std::string &group,
                  const std::string &id, bool mkstream = false) {
        std::vector<std::string> args;
        args.push_back("CREATE");
        args.push_back(key);
        args.push_back(group);
        args.push_back(id);
        if (mkstream) {
            args.push_back("MKSTREAM");
        }
        return derived().template command<status>(std::forward<Func>(func), "XGROUP",
                                                  args);
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
    long long
    xgroup_destroy(const std::string &key, const std::string &group) {
        return derived()
            .template command<long long>("XGROUP", "DESTROY", key, group)
            .result();
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
        return derived().template command<long long>(std::forward<Func>(func), "XGROUP",
                                                     "DESTROY", key, group);
    }

    /**
     * @brief Delete a consumer from a group
     *
     * Removes a consumer from the specified consumer group. This operation
     * will also remove all pending messages for that consumer.
     *
     * @param key The key of the stream containing the group
     * @param group The name of the consumer group
     * @param consumer The name of the consumer to delete
     * @return The number of pending messages that were deleted
     */
    long long
    xgroup_delconsumer(const std::string &key, const std::string &group,
                       const std::string &consumer) {
        return derived()
            .template command<long long>("XGROUP", "DELCONSUMER", key, group, consumer)
            .result();
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
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    xgroup_delconsumer(Func &&func, const std::string &key, const std::string &group,
                       const std::string &consumer) {
        return derived().template command<long long>(
            std::forward<Func>(func), "XGROUP", "DELCONSUMER", key, group, consumer);
    }

    /**
     * @brief Acknowledge messages in a group
     *
     * Marks messages as processed in a consumer group. This is used to track
     * which messages have been successfully processed by consumers.
     *
     * @param key The key of the stream containing the group
     * @param group The name of the consumer group
     * @param ids Variadic list of message IDs to acknowledge
     * @return The number of messages that were acknowledged
     */
    template <typename... Ids>
    long long
    xack(const std::string &key, const std::string &group, Ids &&...ids) {
        return derived()
            .template command<long long>("XACK", key, group, std::forward<Ids>(ids)...)
            .result();
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
     */
    template <typename Func, typename... Ids>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    xack(Func &&func, const std::string &key, const std::string &group, Ids &&...ids) {
        return derived().template command<long long>(
            std::forward<Func>(func), "XACK", key, group, std::forward<Ids>(ids)...);
    }

    /**
     * @brief Trim a stream to a maximum length
     *
     * Trims the stream to the specified maximum length by removing the oldest entries.
     * This command is useful for managing memory usage of streams.
     *
     * @param key The key of the stream to trim
     * @param maxlen The maximum length to trim the stream to
     * @param approximate Whether to use approximate trimming. If true, Redis will
     *                    use a probabilistic algorithm that may not be exact but is
     *                    more efficient
     * @return The number of entries removed from the stream
     */
    long long
    xtrim(const std::string &key, long long maxlen, bool approximate = false) {
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
        return derived().template command<long long>("XTRIM", args).result();
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
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    xtrim(Func &&func, const std::string &key, long long maxlen,
          bool approximate = false) {
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
        return derived().template command<long long>(std::forward<Func>(func), "XTRIM",
                                                     args);
    }

    /**
     * @brief Get the number of pending messages in a consumer group
     *
     * Returns the number of pending messages for a consumer group, optionally
     * filtered by consumer.
     *
     * @param key The key of the stream containing the group
     * @param group The name of the consumer group
     * @param consumer Optional consumer name to filter by
     * @return The number of pending messages
     */
    long long
    xpending(const std::string &key, const std::string &group,
             const std::optional<std::string> &consumer = std::nullopt) {
        std::vector<std::string> args;
        args.push_back(key);
        args.push_back(group);
        if (consumer) {
            args.push_back("-"); // start
            args.push_back("+"); // end
            args.push_back("1"); // count
            args.push_back(*consumer);
        }
        return derived().template command<long long>("XPENDING", args).result();
    }

    /**
     * @brief Asynchronous version of xpending
     *
     * @tparam Func Callback function type that accepts a Reply<long long>
     * @param func Callback function to be called with the result
     * @param key The key of the stream containing the group
     * @param group The name of the consumer group
     * @param consumer Optional consumer name to filter by
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    xpending(Func &&func, const std::string &key, const std::string &group,
             const std::optional<std::string> &consumer = std::nullopt) {
        std::vector<std::string> args;
        args.push_back(key);
        args.push_back(group);
        if (consumer) {
            args.push_back("-"); // start
            args.push_back("+"); // end
            args.push_back("1"); // count
            args.push_back(*consumer);
        }
        return derived().template command<long long>(std::forward<Func>(func),
                                                     "XPENDING", args);
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
     * @return Vector of stream entries, or nullopt if no messages available
     */
    map_stream_entry_list
    xreadgroup(const std::string &key, const std::string &group,
               const std::string &consumer, const std::string &id,
               std::optional<long long> count = std::nullopt,
               std::optional<long long> block = std::nullopt) {
        std::vector<std::string> args = {"GROUP", group, consumer};
        if (count)
            args.insert(args.end(), {"COUNT", std::to_string(*count)});
        if (block)
            args.insert(args.end(), {"BLOCK", std::to_string(*block)});
        return derived()
            .template command<map_stream_entry_list>("XREADGROUP", args, "STREAMS", key,
                                                     id)
            .result();
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<map_stream_entry_list> &&>,
                     Derived &>
    xreadgroup(Func &&func, const std::string &key, const std::string &group,
               const std::string &consumer, const std::string &id,
               std::optional<long long> count = std::nullopt,
               std::optional<long long> block = std::nullopt) {
        std::vector<std::string> args = {"GROUP", group, consumer};
        if (count)
            args.insert(args.end(), {"COUNT", std::to_string(*count)});
        if (block)
            args.insert(args.end(), {"BLOCK", std::to_string(*block)});
        return derived().template command<map_stream_entry_list>(
            std::forward<Func>(func), "XREADGROUP", args, "STREAMS", key, id);
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
     * @return Map of stream entries by key, or nullopt if no messages available
     */
    map_stream_entry_list
    xreadgroup(const std::vector<std::string> &keys, const std::string &group,
               const std::string &consumer, const std::vector<std::string> &ids,
               std::optional<long long> count = std::nullopt,
               std::optional<long long> block = std::nullopt) {
        if (keys.empty() || keys.size() != ids.size()) {
            throw std::invalid_argument(
                "Keys and IDs must be non-empty and have the same size");
        }

        std::vector<std::string> args = {"GROUP", group, consumer};
        if (count)
            args.insert(args.end(), {"COUNT", std::to_string(*count)});
        if (block)
            args.insert(args.end(), {"BLOCK", std::to_string(*block)});

        return derived()
            .template command<map_stream_entry_list>("XREADGROUP", args, "STREAMS", keys,
                                                     ids)
            .result();
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<map_stream_entry_list> &&>,
                     Derived &>
    xreadgroup(Func &&func, const std::vector<std::string> &keys,
               const std::string &group, const std::string &consumer,
               const std::vector<std::string> &ids,
               std::optional<long long>        count = std::nullopt,
               std::optional<long long>        block = std::nullopt) {
        if (keys.empty() || keys.size() != ids.size()) {
            throw std::invalid_argument(
                "Keys and IDs must be non-empty and have the same size");
        }

        std::vector<std::string> args = {"GROUP", group, consumer};
        if (count)
            args.insert(args.end(), {"COUNT", std::to_string(*count)});
        if (block)
            args.insert(args.end(), {"BLOCK", std::to_string(*block)});

        return derived().template command<map_stream_entry_list>(
            std::forward<Func>(func), "XREADGROUP", args, "STREAMS", keys, ids);
    }

    /**
     * @brief Read entries from a stream
     *
     * @param key Stream key
     * @param id ID to read from ("$" for new messages only, "0" for all messages)
     * @param count Optional maximum number of entries to read
     * @param block Optional timeout in milliseconds to block waiting for new messages
     * @return Map of stream entries by key, or nullopt if no messages available
     */
    map_stream_entry_list
    xread(const std::string &key, const std::string &id,
          std::optional<long long> count = std::nullopt,
          std::optional<long long> block = std::nullopt) {
        std::vector<std::string> args;
        if (count)
            args.insert(args.end(), {"COUNT", std::to_string(*count)});
        if (block)
            args.insert(args.end(), {"BLOCK", std::to_string(*block)});

        return derived()
            .template command<map_stream_entry_list>("XREAD", args, "STREAMS", key, id)
            .result();
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<map_stream_entry_list> &&>,
                     Derived &>
    xread(Func &&func, const std::string &key, const std::string &id,
          std::optional<long long> count = std::nullopt,
          std::optional<long long> block = std::nullopt) {
        std::vector<std::string> args;
        if (count)
            args.insert(args.end(), {"COUNT", std::to_string(*count)});
        if (block)
            args.insert(args.end(), {"BLOCK", std::to_string(*block)});

        return derived().template command<map_stream_entry_list>(
            std::forward<Func>(func), "XREAD", args, "STREAMS", key, id);
    }

    /**
     * @brief Read entries from multiple streams
     *
     * @param keys Vector of stream keys
     * @param ids Vector of IDs to read from, one per stream
     * @param count Optional maximum number of entries to read
     * @param block Optional timeout in milliseconds to block waiting for new messages
     * @return Map of stream entries by key, or nullopt if no messages available
     */
    map_stream_entry_list
    xread(const std::vector<std::string> &keys, const std::vector<std::string> &ids,
          std::optional<long long> count = std::nullopt,
          std::optional<long long> block = std::nullopt) {
        if (keys.empty() || keys.size() != ids.size()) {
            throw std::invalid_argument(
                "Keys and IDs must be non-empty and have the same size");
        }

        std::vector<std::string> args;
        if (count)
            args.insert(args.end(), {"COUNT", std::to_string(*count)});
        if (block)
            args.insert(args.end(), {"BLOCK", std::to_string(*block)});

        return derived()
            .template command<map_stream_entry_list>("XREAD", args, "STREAMS", keys, ids)
            .result();
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<map_stream_entry_list> &&>,
                     Derived &>
    xread(Func &&func, const std::vector<std::string> &keys,
          const std::vector<std::string> &ids,
          std::optional<long long>        count = std::nullopt,
          std::optional<long long>        block = std::nullopt) {
        if (keys.empty() || keys.size() != ids.size()) {
            throw std::invalid_argument(
                "Keys and IDs must be non-empty and have the same size");
        }

        std::vector<std::string> args;
        if (count)
            args.insert(args.end(), {"COUNT", std::to_string(*count)});
        if (block)
            args.insert(args.end(), {"BLOCK", std::to_string(*block)});

        return derived().template command<map_stream_entry_list>(
            std::forward<Func>(func), "XREAD", args, "STREAMS", keys, ids);
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_STREAM_COMMANDS_H