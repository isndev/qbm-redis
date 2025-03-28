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

#ifndef QBM_REDIS_PUBLISH_COMMANDS_H
#define QBM_REDIS_PUBLISH_COMMANDS_H
#include "reply.h"

namespace qb::redis {

/**
 * @class publish_commands
 * @brief Provides Redis publishing command implementations.
 *
 * This class implements Redis Pub/Sub publishing functionality, allowing
 * applications to send messages to channels that can be received by subscribers.
 * Each command has both synchronous and asynchronous versions.
 *
 * The PUBLISH command sends a message to all clients that have subscribed to the given channel.
 * The command returns the number of clients that received the message.
 *
 * @tparam Derived The derived class type (CRTP pattern)
 */
template <typename Derived>
class publish_commands {
private:
    constexpr Derived &
    derived() {
        return static_cast<Derived &>(*this);
    }
public:
    /**
     * @brief Publishes a message to a channel
     *
     * This command sends a message to all clients that have subscribed to the given channel.
     * The command returns the number of clients that received the message.
     *
     * @param channel Channel name to publish the message to
     * @param message Message content to publish
     * @return Number of clients that received the message (0 if no subscribers)
     * @throws std::runtime_error if channel is empty
     * @see https://redis.io/commands/publish
     */
    long long
    publish(const std::string &channel, const std::string &message) {
        return derived().template command<long long>("PUBLISH", channel, message).result();
    }
    
    /**
     * @brief Asynchronous version of publish
     *
     * This command sends a message to all clients that have subscribed to the given channel.
     * The callback receives the number of clients that received the message.
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @param channel Channel name to publish the message to
     * @param message Message content to publish
     * @return Reference to the Redis handler for chaining
     * @throws std::runtime_error if channel is empty
     * @see https://redis.io/commands/publish
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    publish(Func &&func, const std::string &channel, const std::string &message) {
        return derived()
            .template command<long long>(std::forward<Func>(func), "PUBLISH", channel, message);
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_PUBLISH_COMMANDS_H
