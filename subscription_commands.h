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
 * Each command has both coroutine and asynchronous versions.
 *
 * Redis Pub/Sub is a messaging paradigm where senders (publishers) send messages to
 * specific channels without knowledge of which receivers (subscribers) will receive
 * them. Subscribers express interest in specific channels or patterns of channels and
 * receive only messages that are of interest.
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
     * @brief Subscribes to one or more channels (coroutine awaitable)
     *
     * This command subscribes the client to the specified channels. Once the client
     * enters the subscribed state, it can no longer issue any other commands except
     * additional subscription commands.
     *
     * @param channel Channel name to subscribe to
     * @return redis_awaiter yielding Reply<qb::redis::subscription>
     * @see https://redis.io/commands/subscribe
     */
    auto subscribe(const std::string &channel) {
        return derived().template make_coro_command<qb::redis::subscription>(
            [this, channel](auto&& callback) {
                this->subscribe(std::move(callback), channel);
            }
        );
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::redis::subscription> &&>,
                     Derived &>
    subscribe(Func &&func, const std::string &channel) {
        if (channel.empty()) {
            Reply<qb::redis::subscription> reply;
            reply.ok() = false;
            reply.error() = "Channel name cannot be empty";
            func(std::move(reply));
            return derived();
        }
        return derived().pubsub_command(std::forward<Func>(func), "SUBSCRIBE",
                                        false, false,
                                        std::vector<std::string>{channel});
    }

    /**
     * @brief Subscribes to multiple channels (coroutine awaitable)
     *
     * This version allows subscribing to multiple channels at once.
     *
     * @param channels Vector of channel names to subscribe to
     * @return redis_awaiter yielding Reply<qb::redis::subscription>
     * @see https://redis.io/commands/subscribe
     */
    auto subscribe(const std::vector<std::string> &channels) {
        return derived().template make_coro_command<qb::redis::subscription>(
            [this, channels](auto&& callback) {
                this->subscribe(std::move(callback), channels);
            }
        );
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::redis::subscription> &&>,
                     Derived &>
    subscribe(Func &&func, const std::vector<std::string> &channels) {
        if (channels.empty()) {
            Reply<qb::redis::subscription> reply;
            reply.ok() = false;
            reply.error() = "Channel list cannot be empty";
            func(std::move(reply));
            return derived();
        }
        return derived().pubsub_command(std::forward<Func>(func), "SUBSCRIBE",
                                        false, false, channels);
    }

    /**
     * @brief Unsubscribes from one or more channels (coroutine awaitable)
     *
     * This command unsubscribes the client from the given channels, or from all
     * channels if none is given.
     *
     * @param channel Channel name to unsubscribe from (empty string to unsubscribe from
     * all)
     * @return redis_awaiter yielding Reply<qb::redis::subscription>
     * @see https://redis.io/commands/unsubscribe
     */
    auto unsubscribe(const std::string &channel = "") {
        return derived().template make_coro_command<qb::redis::subscription>(
            [this, channel](auto&& callback) {
                this->unsubscribe(std::move(callback), channel);
            }
        );
    }

    /**
     * @brief Asynchronous version of unsubscribe
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @param channel Channel name to unsubscribe from (empty string to unsubscribe from
     * all)
     * @return Reference to the derived class for chaining
     * @see https://redis.io/commands/unsubscribe
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::redis::subscription> &&>,
                     Derived &>
    unsubscribe(Func &&func, const std::string &channel = "") {
        if (channel.empty()) {
            return derived().pubsub_command(std::forward<Func>(func), "UNSUBSCRIBE",
                                            true, false, std::vector<std::string>{});
        }
        return derived().pubsub_command(std::forward<Func>(func), "UNSUBSCRIBE",
                                        true, false,
                                        std::vector<std::string>{channel});
    }

    /**
     * @brief Unsubscribes from multiple channels (coroutine awaitable)
     *
     * @param channels Vector of channel names to unsubscribe from
     * @return redis_awaiter yielding Reply<qb::redis::subscription>
     * @see https://redis.io/commands/unsubscribe
     */
    auto unsubscribe(const std::vector<std::string> &channels) {
        return derived().template make_coro_command<qb::redis::subscription>(
            [this, channels](auto&& callback) {
                this->unsubscribe(std::move(callback), channels);
            }
        );
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::redis::subscription> &&>,
                     Derived &>
    unsubscribe(Func &&func, const std::vector<std::string> &channels) {
        return derived().pubsub_command(std::forward<Func>(func), "UNSUBSCRIBE",
                                        true, false, channels);
    }

    // =============== Pattern Subscription Commands ===============

    /**
     * @brief Subscribes to channels matching the given pattern (coroutine awaitable)
     *
     * This command subscribes the client to channels matching the given patterns.
     * Supported glob-style patterns:
     * - h?llo subscribes to hello, hallo, hxllo, etc.
     * - h*llo subscribes to hllo, heeeello, etc.
     * - h[ae]llo subscribes to hello and hallo, but not hillo
     *
     * @param pattern Pattern to match channel names against
     * @return redis_awaiter yielding Reply<qb::redis::subscription>
     * @see https://redis.io/commands/psubscribe
     */
    auto psubscribe(const std::string &pattern) {
        return derived().template make_coro_command<qb::redis::subscription>(
            [this, pattern](auto&& callback) {
                this->psubscribe(std::move(callback), pattern);
            }
        );
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::redis::subscription> &&>,
                     Derived &>
    psubscribe(Func &&func, const std::string &pattern) {
        if (pattern.empty()) {
            Reply<qb::redis::subscription> reply;
            reply.ok() = false;
            reply.error() = "Pattern cannot be empty";
            func(std::move(reply));
            return derived();
        }
        return derived().pubsub_command(std::forward<Func>(func), "PSUBSCRIBE",
                                        false, true,
                                        std::vector<std::string>{pattern});
    }

    /**
     * @brief Subscribes to multiple patterns (coroutine awaitable)
     *
     * @param patterns Vector of patterns to match channel names against
     * @return redis_awaiter yielding Reply<qb::redis::subscription>
     * @see https://redis.io/commands/psubscribe
     */
    auto psubscribe(const std::vector<std::string> &patterns) {
        return derived().template make_coro_command<qb::redis::subscription>(
            [this, patterns](auto&& callback) {
                this->psubscribe(std::move(callback), patterns);
            }
        );
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::redis::subscription> &&>,
                     Derived &>
    psubscribe(Func &&func, const std::vector<std::string> &patterns) {
        if (patterns.empty()) {
            Reply<qb::redis::subscription> reply;
            reply.ok() = false;
            reply.error() = "Pattern list cannot be empty";
            func(std::move(reply));
            return derived();
        }
        return derived().pubsub_command(std::forward<Func>(func), "PSUBSCRIBE",
                                        false, true, patterns);
    }

    /**
     * @brief Unsubscribes from channels matching the given pattern (coroutine awaitable)
     *
     * This command unsubscribes the client from the given patterns, or from all
     * patterns if none is given.
     *
     * @param pattern Pattern to stop matching channel names against (empty string to
     * unsubscribe from all patterns)
     * @return redis_awaiter yielding Reply<qb::redis::subscription>
     * @see https://redis.io/commands/punsubscribe
     */
    auto punsubscribe(const std::string &pattern = "") {
        return derived().template make_coro_command<qb::redis::subscription>(
            [this, pattern](auto&& callback) {
                this->punsubscribe(std::move(callback), pattern);
            }
        );
    }

    /**
     * @brief Asynchronous version of punsubscribe
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @param pattern Pattern to stop matching channel names against (empty string to
     * unsubscribe from all patterns)
     * @return Reference to the derived class for chaining
     * @see https://redis.io/commands/punsubscribe
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::redis::subscription> &&>,
                     Derived &>
    punsubscribe(Func &&func, const std::string &pattern = "") {
        if (pattern.empty()) {
            return derived().pubsub_command(std::forward<Func>(func), "PUNSUBSCRIBE",
                                            true, true, std::vector<std::string>{});
        }
        return derived().pubsub_command(std::forward<Func>(func), "PUNSUBSCRIBE",
                                        true, true,
                                        std::vector<std::string>{pattern});
    }

    /**
     * @brief Unsubscribes from multiple patterns (coroutine awaitable)
     *
     * @param patterns Vector of patterns to stop matching channel names against
     * @return redis_awaiter yielding Reply<qb::redis::subscription>
     * @see https://redis.io/commands/punsubscribe
     */
    auto punsubscribe(const std::vector<std::string> &patterns) {
        return derived().template make_coro_command<qb::redis::subscription>(
            [this, patterns](auto&& callback) {
                this->punsubscribe(std::move(callback), patterns);
            }
        );
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
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::redis::subscription> &&>,
                     Derived &>
    punsubscribe(Func &&func, const std::vector<std::string> &patterns) {
        return derived().pubsub_command(std::forward<Func>(func), "PUNSUBSCRIBE",
                                        true, true, patterns);
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_SUBSCRIPTION_COMMANDS_H
