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
 * It supports both exact channel matching and pattern-based subscriptions.
 *
 * @tparam Derived The derived class type (CRTP pattern)
 */
template <typename Derived>
class subscription_commands {
public:
    /**
     * @brief Subscribes to a channel
     *
     * @param channel Channel name to subscribe to
     * @return Subscription information (channel name and current channel count)
     */
    reply::subscription
    subscribe(const std::string &channel) {
        return static_cast<Derived &>(*this).template command<reply::subscription>("SUBSCRIBE", channel).result;
    }
    
    /**
     * @brief Asynchronous version of subscribe
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param channel Channel name to subscribe to
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<reply::subscription> &&>, Derived &>
    subscribe(Func &&func, const std::string &channel) {
        return static_cast<Derived &>(*this).template command<reply::subscription>(
            std::forward<Func>(func),
            "SUBSCRIBE",
            channel);
    }

    /**
     * @brief Subscribes to channels matching the given pattern
     *
     * @param channel Pattern to match channel names against
     * @return Subscription information (pattern and current pattern count)
     */
    reply::subscription
    psubscribe(const std::string &channel) {
        return static_cast<Derived &>(*this).template command<reply::subscription>("PSUBSCRIBE", channel).result;
    }
    
    /**
     * @brief Asynchronous version of psubscribe
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param channel Pattern to match channel names against
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<reply::subscription> &&>, Derived &>
    psubscribe(Func &&func, const std::string &channel) {
        return static_cast<Derived &>(*this).template command<reply::subscription>(
            std::forward<Func>(func),
            "PSUBSCRIBE",
            channel);
    }

    /**
     * @brief Unsubscribes from a channel
     *
     * @param channel Channel name to unsubscribe from
     * @return Unsubscription information (channel name and remaining channel count)
     */
    reply::subscription
    unsubscribe(const std::string &channel) {
        return static_cast<Derived &>(*this).template command<reply::subscription>("UNSUBSCRIBE", channel).result;
    }
    
    /**
     * @brief Asynchronous version of unsubscribe
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param channel Channel name to unsubscribe from
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<reply::subscription> &&>, Derived &>
    unsubscribe(Func &&func, const std::string &channel) {
        return static_cast<Derived &>(*this).template command<reply::subscription>(
            std::forward<Func>(func),
            "UNSUBSCRIBE",
            channel);
    }

    /**
     * @brief Unsubscribes from channels matching the given pattern
     *
     * @param channel Pattern to stop matching channel names against
     * @return Unsubscription information (pattern and remaining pattern count)
     */
    reply::subscription
    punsubscribe(const std::string &channel) {
        return static_cast<Derived &>(*this).template command<reply::subscription>("PUNSUBSCRIBE", channel).result;
    }
    
    /**
     * @brief Asynchronous version of punsubscribe
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param channel Pattern to stop matching channel names against
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<reply::subscription> &&>, Derived &>
    punsubscribe(Func &&func, const std::string &channel) {
        return static_cast<Derived &>(*this).template command<reply::subscription>(
            std::forward<Func>(func),
            "PUNSUBSCRIBE",
            channel);
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_SUBSCRIPTION_COMMANDS_H
