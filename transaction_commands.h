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

#ifndef QBM_REDIS_TRANSACTION_COMMANDS_H
#define QBM_REDIS_TRANSACTION_COMMANDS_H

#include "reply.h"

namespace qb::redis {

/**
 * @class transaction_commands
 * @brief Provides Redis transaction command implementations.
 *
 * This class implements Redis commands for handling transactions, including
 * MULTI, EXEC, DISCARD, WATCH, and UNWATCH. Each command has both synchronous
 * and asynchronous versions.
 *
 * Redis transactions allow the execution of a group of commands in a single step,
 * with two important guarantees:
 * 1. All commands in a transaction are serialized and executed sequentially
 * 2. Either all of the commands or none are processed
 *
 * @tparam Derived The derived class type (CRTP pattern)
 */
template <typename Derived>
class transaction_commands {
private:
    constexpr Derived &
    derived() {
        return static_cast<Derived &>(*this);
    }
    bool exec_flag_ = false;
public:
    /**
     * @brief Marks the start of a transaction block.
     * 
     * All commands after this call will be queued for atomic execution using EXEC.
     *
     * @return status object with the result
     * @note Time complexity: O(1)
     * @see https://redis.io/commands/multi
     */
    status
    multi() {
        auto reply = derived().template command<status>("MULTI");
        exec_flag_ = reply.ok();
        return reply.result();
    }

    /**
     * @brief Asynchronous version of multi.
     *
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     * @see https://redis.io/commands/multi
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    multi(Func &&func) {
        return derived().template command<status>(
            [this, func = std::forward<Func>(func)](auto &&reply) mutable {
                exec_flag_ = reply.ok();
                std::move(func)(std::forward<decltype(reply)>(reply));
            },
            "MULTI");
    }

    /**
     * @brief Executes all commands issued after MULTI.
     *
     * Executes all previously queued commands in a transaction and restores
     * the connection state to normal.
     *
     * @tparam Result Result type for the transaction commands
     * @return Vector containing the results of all commands in the transaction
     * @note Time complexity: O(N) where N is the number of commands in the transaction
     * @see https://redis.io/commands/exec
     */
    template <typename Result>
    std::vector<Result>
    exec() {
        exec_flag_ = false;
        return derived().template command<std::vector<Result>>("EXEC").result();
    }

    /**
     * @brief Asynchronous version of exec.
     *
     * @tparam Result Result type for the transaction commands
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     * @see https://redis.io/commands/exec
     */
    template <typename Result, typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<Result>> &&>, Derived &>
    exec(Func &&func) {
        exec_flag_ = false;
        return derived().template command<std::vector<Result>>(
            [this, func = std::forward<Func>(func)](auto &&reply) mutable {
                exec_flag_ = false;
                std::move(func)(std::forward<decltype(reply)>(reply));
            },
            "EXEC");
    }

    /**
     * @brief Discards all commands issued after MULTI.
     *
     * Flushes all previously queued commands in a transaction and restores
     * the connection state to normal.
     *
     * @return status object with the result
     * @note Time complexity: O(1)
     * @see https://redis.io/commands/discard
     */
    status
    discard() {
        exec_flag_ = false;
        return derived().template command<status>("DISCARD").result();
    }

    /**
     * @brief Asynchronous version of discard.
     *
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     * @see https://redis.io/commands/discard
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    discard(Func &&func) {
        return derived().template command<status>(
            [this, func = std::forward<Func>(func)](auto &&reply) mutable {
                exec_flag_ = false;
                std::move(func)(std::forward<decltype(reply)>(reply));
            },
            "DISCARD");
    }

    /**
     * @brief Watches the given keys for changes.
     *
     * Marks the given keys to be watched for conditional execution of a transaction.
     *
     * @param key Key to watch
     * @return status object with the result
     * @note Time complexity: O(1) for every key
     * @see https://redis.io/commands/watch
     */
    status
    watch(const std::string &key) {
        if (key.empty()) {
            return status("");
        }
        return derived().template command<status>("WATCH", key).result();
    }

    /**
     * @brief Asynchronous version of watch for a single key.
     *
     * @param func Callback function to handle the result
     * @param key Key to watch
     * @return Reference to the derived class
     * @see https://redis.io/commands/watch
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    watch(Func &&func, const std::string &key) {
        if (key.empty()) {
            return derived();
        }
        return derived().template command<status>(std::forward<Func>(func), "WATCH", key);
    }

    /**
     * @brief Watches multiple keys for changes.
     *
     * @param keys Vector of keys to watch
     * @return status object with the result
     * @note Time complexity: O(N) where N is the number of keys to watch
     * @see https://redis.io/commands/watch
     */
    status
    watch(const std::vector<std::string> &keys) {
        if (keys.empty()) {
            return status("");
        }
        return derived().template command<status>("WATCH", keys).result();
    }

    /**
     * @brief Asynchronous version of watch for multiple keys.
     *
     * @param func Callback function to handle the result
     * @param keys Vector of keys to watch
     * @return Reference to the derived class
     * @see https://redis.io/commands/watch
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    watch(Func &&func, const std::vector<std::string> &keys) {
        if (keys.empty()) {
            return derived();
        }
        return derived().template command<status>(std::forward<Func>(func), "WATCH", keys);
    }

    /**
     * @brief Unwatches all previously watched keys.
     *
     * Flushes all the watched keys for a transaction.
     *
     * @return status object with the result
     * @note Time complexity: O(1)
     * @see https://redis.io/commands/unwatch
     */
    status
    unwatch() {
        return derived().template command<status>("UNWATCH").result();
    }

    /**
     * @brief Asynchronous version of unwatch.
     *
     * @param func Callback function to handle the result
     * @return Reference to the derived class
     * @see https://redis.io/commands/unwatch
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    unwatch(Func &&func) {
        return derived().template command<status>(std::forward<Func>(func), "UNWATCH");
    }

    /**
     * @brief Checks if currently in a transaction.
     *
     * @return true if in a transaction, false otherwise
     * @note Time complexity: O(1)
     */
    bool
    is_in_multi() const {
        return exec_flag_;
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_TRANSACTION_COMMANDS_H 