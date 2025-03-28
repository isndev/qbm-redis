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

#ifndef QBM_REDIS_SUBSCRIPTION_COMMANDS_H
#define QBM_REDIS_SUBSCRIPTION_COMMANDS_H
#include "reply.h"

namespace qb::redis {

/**
 * @class subscription_commands
 * @brief Provides Redis subscription command implementations.
 *
 * This class implements Redis Pub/Sub subscription commands, allowing
 * applications to subscribe to channels and receive published messages.
 * Each command has both synchronous and asynchronous versions.
 *
 * Redis Pub/Sub is a messaging paradigm where senders (publishers) send messages to specific channels
 * without knowledge of which receivers (subscribers) will receive them. Subscribers express interest
 * in specific channels or patterns of channels and receive only messages that are of interest.
 *
 * This class supports both exact channel matching and pattern-based subscriptions.
 *
 * @tparam Derived The derived class type (CRTP pattern)
 */
template <typename Derived>
class subscription_commands {
private:
    constexpr Derived &
    derived() {
        return static_cast<Derived &>(*this);
    }
public:
    // =============== Channel Subscription Commands ===============

    /**
     * @brief Subscribes to one or more channels
     * 
     * This command subscribes the client to the specified channels. Once the client
     * enters the subscribed state, it can no longer issue any other commands except
     * additional subscription commands.
     *
     * @param channel Channel name to subscribe to
     * @return Subscription information (channel name and current channel count)
     * @see https://redis.io/commands/subscribe
     */
    qb::redis::subscription
    subscribe(const std::string &channel) {
        if (channel.empty()) {
            return qb::redis::subscription{};
        }
        return derived().template command<qb::redis::subscription>("SUBSCRIBE", channel).result();
    }
    
    /**
     * @brief Asynchronous version of subscribe
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @param channel Channel name to subscribe to
     * @return Reference to the derived class for chaining
     * @see https://redis.io/commands/subscribe
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::redis::subscription> &&>, Derived &>
    subscribe(Func &&func, const std::string &channel) {
        if (channel.empty()) {
            Reply<qb::redis::subscription> reply;
            reply.ok() = false;
            func(std::move(reply));
            return derived();
        }
        return derived().template command<qb::redis::subscription>(
            std::forward<Func>(func),
            "SUBSCRIBE",
            channel);
    }

    /**
     * @brief Subscribes to multiple channels
     * 
     * This version allows subscribing to multiple channels at once.
     *
     * @param channels Vector of channel names to subscribe to
     * @return Subscription information for the last channel (channel name and current channel count)
     * @see https://redis.io/commands/subscribe
     */
    qb::redis::subscription
    subscribe(const std::vector<std::string> &channels) {
        if (channels.empty()) {
            return qb::redis::subscription{};
        }
        return derived().template command<qb::redis::subscription>("SUBSCRIBE", channels).result();
    }
    
    /**
     * @brief Asynchronous version of subscribe for multiple channels
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @param channels Vector of channel names to subscribe to
     * @return Reference to the derived class for chaining
     * @see https://redis.io/commands/subscribe
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::redis::subscription> &&>, Derived &>
    subscribe(Func &&func, const std::vector<std::string> &channels) {
        if (channels.empty()) {
            Reply<qb::redis::subscription> reply;
            reply.ok() = false;
            func(std::move(reply));
            return derived();
        }
        return derived().template command<qb::redis::subscription>(
            std::forward<Func>(func),
            "SUBSCRIBE",
            channels);
    }

    /**
     * @brief Unsubscribes from one or more channels
     *
     * This command unsubscribes the client from the given channels, or from all
     * channels if none is given.
     *
     * @param channel Channel name to unsubscribe from (empty string to unsubscribe from all)
     * @return Unsubscription information (channel name and remaining channel count)
     * @see https://redis.io/commands/unsubscribe
     */
    qb::redis::subscription
    unsubscribe(const std::string &channel = "") {
        if (channel.empty()) {
            return derived().template command<qb::redis::subscription>("UNSUBSCRIBE").result();
        }
        return derived().template command<qb::redis::subscription>("UNSUBSCRIBE", channel).result();
    }
    
    /**
     * @brief Asynchronous version of unsubscribe
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @param channel Channel name to unsubscribe from (empty string to unsubscribe from all)
     * @return Reference to the derived class for chaining
     * @see https://redis.io/commands/unsubscribe
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::redis::subscription> &&>, Derived &>
    unsubscribe(Func &&func, const std::string &channel = "") {
        if (channel.empty()) {
            return derived().template command<qb::redis::subscription>(
                std::forward<Func>(func),
                "UNSUBSCRIBE");
        }
        return derived().template command<qb::redis::subscription>(
            std::forward<Func>(func),
            "UNSUBSCRIBE",
            channel);
    }

    /**
     * @brief Unsubscribes from multiple channels
     *
     * @param channels Vector of channel names to unsubscribe from
     * @return Unsubscription information for the last channel (channel name and remaining channel count)
     * @see https://redis.io/commands/unsubscribe
     */
    qb::redis::subscription
    unsubscribe(const std::vector<std::string> &channels) {
        if (channels.empty()) {
            return derived().template command<qb::redis::subscription>("UNSUBSCRIBE").result();
        }
        return derived().template command<qb::redis::subscription>("UNSUBSCRIBE", channels).result();
    }
    
    /**
     * @brief Asynchronous version of unsubscribe for multiple channels
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @param channels Vector of channel names to unsubscribe from
     * @return Reference to the derived class for chaining
     * @see https://redis.io/commands/unsubscribe
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::redis::subscription> &&>, Derived &>
    unsubscribe(Func &&func, const std::vector<std::string> &channels) {
        if (channels.empty()) {
            return derived().template command<qb::redis::subscription>(
                std::forward<Func>(func),
                "UNSUBSCRIBE");
        }
        return derived().template command<qb::redis::subscription>(
            std::forward<Func>(func),
            "UNSUBSCRIBE",
            channels);
    }

    // =============== Pattern Subscription Commands ===============

    /**
     * @brief Subscribes to channels matching the given pattern
     *
     * This command subscribes the client to channels matching the given patterns.
     * Supported glob-style patterns:
     * - h?llo subscribes to hello, hallo, hxllo, etc.
     * - h*llo subscribes to hllo, heeeello, etc.
     * - h[ae]llo subscribes to hello and hallo, but not hillo
     *
     * @param pattern Pattern to match channel names against
     * @return Subscription information (pattern and current pattern count)
     * @see https://redis.io/commands/psubscribe
     */
    qb::redis::subscription
    psubscribe(const std::string &pattern) {
        if (pattern.empty()) {
            return qb::redis::subscription{};
        }
        return derived().template command<qb::redis::subscription>("PSUBSCRIBE", pattern).result();
    }
    
    /**
     * @brief Asynchronous version of psubscribe
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @param pattern Pattern to match channel names against
     * @return Reference to the derived class for chaining
     * @see https://redis.io/commands/psubscribe
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::redis::subscription> &&>, Derived &>
    psubscribe(Func &&func, const std::string &pattern) {
        if (pattern.empty()) {
            Reply<qb::redis::subscription> reply;
            reply.ok() = false;
            func(std::move(reply));
            return derived();
        }
        return derived().template command<qb::redis::subscription>(
            std::forward<Func>(func),
            "PSUBSCRIBE",
            pattern);
    }

    /**
     * @brief Subscribes to multiple patterns
     *
     * @param patterns Vector of patterns to match channel names against
     * @return Subscription information for the last pattern (pattern and current pattern count)
     * @see https://redis.io/commands/psubscribe
     */
    qb::redis::subscription
    psubscribe(const std::vector<std::string> &patterns) {
        if (patterns.empty()) {
            return qb::redis::subscription{};
        }
        return derived().template command<qb::redis::subscription>("PSUBSCRIBE", patterns).result();
    }
    
    /**
     * @brief Asynchronous version of psubscribe for multiple patterns
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @param patterns Vector of patterns to match channel names against
     * @return Reference to the derived class for chaining
     * @see https://redis.io/commands/psubscribe
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::redis::subscription> &&>, Derived &>
    psubscribe(Func &&func, const std::vector<std::string> &patterns) {
        if (patterns.empty()) {
            Reply<qb::redis::subscription> reply;
            reply.ok() = false;
            func(std::move(reply));
            return derived();
        }
        return derived().template command<qb::redis::subscription>(
            std::forward<Func>(func),
            "PSUBSCRIBE",
            patterns);
    }

    /**
     * @brief Unsubscribes from channels matching the given pattern
     *
     * This command unsubscribes the client from the given patterns, or from all
     * patterns if none is given.
     *
     * @param pattern Pattern to stop matching channel names against (empty string to unsubscribe from all patterns)
     * @return Unsubscription information (pattern and remaining pattern count)
     * @see https://redis.io/commands/punsubscribe
     */
    qb::redis::subscription
    punsubscribe(const std::string &pattern = "") {
        if (pattern.empty()) {
            return derived().template command<qb::redis::subscription>("PUNSUBSCRIBE").result();
        }
        return derived().template command<qb::redis::subscription>("PUNSUBSCRIBE", pattern).result();
    }
    
    /**
     * @brief Asynchronous version of punsubscribe
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @param pattern Pattern to stop matching channel names against (empty string to unsubscribe from all patterns)
     * @return Reference to the derived class for chaining
     * @see https://redis.io/commands/punsubscribe
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::redis::subscription> &&>, Derived &>
    punsubscribe(Func &&func, const std::string &pattern = "") {
        if (pattern.empty()) {
            return derived().template command<qb::redis::subscription>(
                std::forward<Func>(func),
                "PUNSUBSCRIBE");
        }
        return derived().template command<qb::redis::subscription>(
            std::forward<Func>(func),
            "PUNSUBSCRIBE",
            pattern);
    }

    /**
     * @brief Unsubscribes from multiple patterns
     *
     * @param patterns Vector of patterns to stop matching channel names against
     * @return Unsubscription information for the last pattern (pattern and remaining pattern count)
     * @see https://redis.io/commands/punsubscribe
     */
    qb::redis::subscription
    punsubscribe(const std::vector<std::string> &patterns) {
        if (patterns.empty()) {
            return derived().template command<qb::redis::subscription>("PUNSUBSCRIBE").result();
        }
        return derived().template command<qb::redis::subscription>("PUNSUBSCRIBE", patterns).result();
    }
    
    /**
     * @brief Asynchronous version of punsubscribe for multiple patterns
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @param patterns Vector of patterns to stop matching channel names against
     * @return Reference to the derived class for chaining
     * @see https://redis.io/commands/punsubscribe
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::redis::subscription> &&>, Derived &>
    punsubscribe(Func &&func, const std::vector<std::string> &patterns) {
        if (patterns.empty()) {
            return derived().template command<qb::redis::subscription>(
                std::forward<Func>(func),
                "PUNSUBSCRIBE");
        }
        return derived().template command<qb::redis::subscription>(
            std::forward<Func>(func),
            "PUNSUBSCRIBE",
            patterns);
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_SUBSCRIPTION_COMMANDS_H
