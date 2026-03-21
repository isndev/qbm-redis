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
    /// True after successful MULTI until EXEC or DISCARD completes (client-side hint only).
    bool in_multi_ = false;

public:
    /**
     * @brief Marks the start of a transaction block (coroutine awaitable).
     *
     * All commands after this call will be queued for atomic execution using EXEC.
     *
     * @return Awaitable that yields Reply<status>
     * @note Time complexity: O(1)
     * @see https://redis.io/commands/multi
     */
    auto multi() {
        return derived().template make_coro_command<status>(
            [this](auto&& callback) {
                this->multi(std::move(callback));
            }
        );
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
                in_multi_ = reply.ok();
                std::move(func)(std::forward<decltype(reply)>(reply));
            },
            "MULTI");
    }

    /**
     * @brief Executes all commands issued after MULTI (coroutine awaitable).
     *
     * Executes all previously queued commands in a transaction and restores
     * the connection state to normal.
     *
     * @tparam Result Result type for the transaction commands
     * @return Awaitable that yields Reply<std::vector<Result>>
     * @note Time complexity: O(N) where N is the number of commands in the transaction
     * @see https://redis.io/commands/exec
     */
    template <typename Result>
    auto exec() {
        return derived().template make_coro_command<std::vector<Result>>(
            [this](auto&& callback) {
                this->exec<Result>(std::move(callback));
            }
        );
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
        in_multi_ = false;
        return derived().template command<std::vector<Result>>(
            [this, func = std::forward<Func>(func)](auto &&reply) mutable {
                in_multi_ = false;
                std::move(func)(std::forward<decltype(reply)>(reply));
            },
            "EXEC");
    }

    /**
     * @brief Discards all commands issued after MULTI (coroutine awaitable).
     *
     * Flushes all previously queued commands in a transaction and restores
     * the connection state to normal.
     *
     * @return Awaitable that yields Reply<status>
     * @note Time complexity: O(1)
     * @see https://redis.io/commands/discard
     */
    auto discard() {
        return derived().template make_coro_command<status>(
            [this](auto&& callback) {
                this->discard(std::move(callback));
            }
        );
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
                in_multi_ = false;
                std::move(func)(std::forward<decltype(reply)>(reply));
            },
            "DISCARD");
    }

    /**
     * @brief Watches the given keys for changes (coroutine awaitable).
     *
     * Marks the given keys to be watched for conditional execution of a transaction.
     *
     * @param key Key to watch
     * @return Awaitable that yields Reply<status>
     * @note Time complexity: O(1) for every key
     * @see https://redis.io/commands/watch
     */
    auto watch(const std::string &key) {
        return derived().template make_coro_command<status>(
            [this, key](auto&& callback) {
                this->watch(std::move(callback), key);
            }
        );
    }

    /**
     * @brief Asynchronous version of watch for a single key.
     *
     * @param func Callback function to handle the result
     * @param key Key to watch
     * @return Reference to the derived class
     * @note Invokes callback with Reply ok()=false and error() set if key is empty
     * @see https://redis.io/commands/watch
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    watch(Func &&func, const std::string &key) {
        if (key.empty()) {
            Reply<status> reply;
            reply.ok() = false;
            reply.error() = "Key cannot be empty";
            func(std::move(reply));
            return derived();
        }
        return derived().template command<status>(std::forward<Func>(func), "WATCH",
                                                  key);
    }

    /**
     * @brief Watches multiple keys for changes (coroutine awaitable).
     *
     * @param keys Vector of keys to watch
     * @return Awaitable that yields Reply<status>
     * @note Time complexity: O(N) where N is the number of keys to watch
     * @see https://redis.io/commands/watch
     */
    auto watch(const std::vector<std::string> &keys) {
        return derived().template make_coro_command<status>(
            [this, keys](auto&& callback) {
                this->watch(std::move(callback), keys);
            }
        );
    }

    /**
     * @brief Asynchronous version of watch for multiple keys.
     *
     * @param func Callback function to handle the result
     * @param keys Vector of keys to watch
     * @return Reference to the derived class
     * @note Invokes callback with Reply ok()=false and error() set if keys is empty
     * @see https://redis.io/commands/watch
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    watch(Func &&func, const std::vector<std::string> &keys) {
        if (keys.empty()) {
            Reply<status> reply;
            reply.ok() = false;
            reply.error() = "Key list cannot be empty";
            func(std::move(reply));
            return derived();
        }
        return derived().template command<status>(std::forward<Func>(func), "WATCH",
                                                  keys);
    }

    /**
     * @brief Unwatches all previously watched keys (coroutine awaitable).
     *
     * Flushes all the watched keys for a transaction.
     *
     * @return Awaitable that yields Reply<status>
     * @note Time complexity: O(1)
     * @see https://redis.io/commands/unwatch
     */
    auto unwatch() {
        return derived().template make_coro_command<status>(
            [this](auto&& callback) {
                this->unwatch(std::move(callback));
            }
        );
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
        return in_multi_;
    }

    /**
     * @brief Clear client-side MULTI state after disconnect or protocol reset
     *
     * The server no longer has a transaction open; this avoids stale is_in_multi().
     */
    void
    reset_transaction_state() noexcept {
        in_multi_ = false;
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_TRANSACTION_COMMANDS_H