/**
 * @file qbm/redis/commands/publish_commands.h
 * @brief Redis Pub/Sub publishing command mixin for the qb Redis module.
 *
 * Provides the @ref qb::redis::publish_commands CRTP mixin implementing the
 * Redis publish-side of Pub/Sub (the PUBLISH command). The command is exposed
 * both as a coroutine-awaitable form and as a callback-based async form that
 * returns a reference to the derived handler for chaining.
 *
 *            See accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0
 *
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2026 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)
 * @ingroup Redis
 */
#ifndef QBM_REDIS_PUBLISH_COMMANDS_H
#define QBM_REDIS_PUBLISH_COMMANDS_H
#include "../reply.h"

namespace qb::redis {

/**
 * @class publish_commands
 * @brief Provides Redis publishing command implementations.
 *
 * This class implements Redis Pub/Sub publishing functionality, allowing
 * applications to send messages to channels that can be received by subscribers.
 * Each command has both synchronous and asynchronous versions.
 *
 * The PUBLISH command sends a message to all clients that have subscribed to the given
 * channel. The command returns the number of clients that received the message.
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
     * @brief Publishes a message to a channel (coroutine awaitable)
     *
     * This command sends a message to all clients that have subscribed to the given
     * channel. The command returns the number of clients that received the message.
     *
     * @param channel Channel name to publish the message to
     * @param message Message content to publish
     * @return Awaitable that yields Reply<long long> with the number of clients that received the message
     * @see https://redis.io/commands/publish
     */
    auto
    publish(const std::string &channel, const std::string &message) {
        return derived().template make_coro_command<long long>(
            [this, channel, message](auto &&callback) { this->publish(std::move(callback), channel, message); });
    }

    /**
     * @brief Asynchronous version of publish
     *
     * This command sends a message to all clients that have subscribed to the given
     * channel. The callback receives the number of clients that received the message.
     *
     * @tparam Func Callback function type
     * @param func Callback function to handle the result
     * @param channel Channel name to publish the message to
     * @param message Message content to publish
     * @return Reference to the Redis handler for chaining
     * @see https://redis.io/commands/publish
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    publish(Func &&func, const std::string &channel, const std::string &message) {
        return derived().template command<long long>(std::forward<Func>(func), "PUBLISH", channel, message);
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_PUBLISH_COMMANDS_H
