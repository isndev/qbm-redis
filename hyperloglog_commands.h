/*
 * qb - C++ Actor Framework
 * Copyright (C) 2011-2026 isndev (cpp.actor). All rights reserved.
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

#ifndef QBM_REDIS_HYPERLOG_COMMANDS_H
#define QBM_REDIS_HYPERLOG_COMMANDS_H
#include "reply.h"

namespace qb::redis {

/**
 * @class hyperlog_commands
 * @brief Provides Redis HyperLogLog command implementations.
 *
 * This class implements Redis HyperLogLog commands for working with
 * probabilistic data structures that estimate the cardinality of a set
 * with minimal memory usage. HyperLogLog is excellent for counting unique
 * elements in very large datasets with a small constant memory footprint.
 *
 * @tparam Derived The derived class type (CRTP pattern)
 */
template <typename Derived>
class hyperloglog_commands {
private:
    constexpr Derived &
    derived() {
        return static_cast<Derived &>(*this);
    }

public:
    /**
     * @brief Adds elements to a HyperLogLog data structure (coroutine awaitable)
     *
     * @tparam Elements Variadic types for elements to add
     * @param key Key under which the HyperLogLog is stored
     * @param elements Elements to add to the HyperLogLog
     * @return redis_awaiter yielding Reply<bool> (true if cardinality changed)
     * @see https://redis.io/commands/pfadd
     */
    template <typename... Elements>
    auto
    pfadd(const std::string &key, Elements &&...elements) {
        return derived().template make_coro_command<bool>(
            [this, key, ... elements = std::forward<Elements>(elements)](auto &&callback) mutable {
                this->pfadd(std::move(callback), key, std::forward<decltype(elements)>(elements)...);
            });
    }

    /**
     * @brief Asynchronous version of pfadd
     *
     * @tparam Func Callback function type
     * @tparam Elements Variadic types for elements to add
     * @param func Callback function
     * @param key Key under which the HyperLogLog is stored
     * @param elements Elements to add to the HyperLogLog
     * @return Reference to the Redis handler for chaining
     * @see https://redis.io/commands/pfadd
     */
    template <typename Func, typename... Elements>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    pfadd(Func &&func, const std::string &key, Elements &&...elements) {
        return derived().template command<bool>(std::forward<Func>(func), "PFADD", key, std::forward<Elements>(elements)...);
    }

    /**
     * @brief Returns the estimated cardinality of one or more HyperLogLog structures (coroutine awaitable)
     *
     * @tparam Keys Variadic types for key names
     * @param keys Keys containing HyperLogLog structures
     * @return redis_awaiter yielding Reply<long long> (estimated cardinality)
     * @see https://redis.io/commands/pfcount
     */
    template <typename... Keys>
    auto
    pfcount(Keys &&...keys) {
        return derived().template make_coro_command<long long>([this, ... keys = std::forward<Keys>(keys)](auto &&callback) mutable {
            this->pfcount(std::move(callback), std::forward<decltype(keys)>(keys)...);
        });
    }

    /**
     * @brief Asynchronous version of pfcount
     *
     * @tparam Func Callback function type
     * @tparam Keys Variadic types for key names
     * @param func Callback function
     * @param keys Keys containing HyperLogLog structures
     * @return Reference to the Redis handler for chaining
     * @see https://redis.io/commands/pfcount
     */
    template <typename Func, typename... Keys>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    pfcount(Func &&func, Keys &&...keys) {
        return derived().template command<long long>(std::forward<Func>(func), "PFCOUNT", std::forward<Keys>(keys)...);
    }

    /**
     * @brief Merges multiple HyperLogLog structures into a destination key (coroutine awaitable)
     *
     * @tparam Keys Variadic types for source key names
     * @param destination Destination key where the merged HyperLogLog will be stored
     * @param keys Source keys containing HyperLogLog structures to merge
     * @return redis_awaiter yielding Reply<status>
     * @see https://redis.io/commands/pfmerge
     */
    template <typename... Keys>
    auto
    pfmerge(const std::string &destination, Keys &&...keys) {
        return derived().template make_coro_command<status>([this, destination, ... keys = std::forward<Keys>(keys)](auto &&callback) mutable {
            this->pfmerge(std::move(callback), destination, std::forward<decltype(keys)>(keys)...);
        });
    }

    /**
     * @brief Asynchronous version of pfmerge
     *
     * @tparam Func Callback function type
     * @tparam Keys Variadic types for source key names
     * @param func Callback function
     * @param destination Destination key where the merged HyperLogLog will be stored
     * @param keys Source keys containing HyperLogLog structures to merge
     * @return Reference to the Redis handler for chaining
     * @see https://redis.io/commands/pfmerge
     */
    template <typename Func, typename... Keys>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    pfmerge(Func &&func, const std::string &destination, Keys &&...keys) {
        return derived().template command<status>(std::forward<Func>(func), "PFMERGE", destination, std::forward<Keys>(keys)...);
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_HYPERLOG_COMMANDS_H
