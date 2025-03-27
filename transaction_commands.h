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

#ifndef QBM_REDIS_TRANSACTION_COMMANDS_H
#define QBM_REDIS_TRANSACTION_COMMANDS_H
#include "reply.h"

namespace qb::redis {

/**
 * @class transaction_commands
 * @brief Provides Redis transaction command implementations.
 *
 * This class implements Redis commands for handling transactions, including
 * MULTI, EXEC, DISCARD, WATCH, and UNWATCH.
 *
 * @tparam Derived The derived class type (CRTP pattern)
 */
template <typename Derived>
class transaction_commands {
private:
    bool exec_flag_ = false;

    Derived &
    derived() {
        return static_cast<Derived &>(*this);
    }

public:
    /**
     * @brief Marks the start of a transaction block
     *
     * @return true if the transaction started successfully, false otherwise
     */
    bool
    multi() {
        auto reply = derived().template command<void>("MULTI");
        exec_flag_ = reply.ok;
        return reply.ok;
    }

    /**
     * @brief Asynchronous version of multi
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    multi(Func &&func) {
        return derived().template command<void>(
            [self = derived(), func = std::forward<Func>(func)](auto &&reply) mutable {
                self.exec_flag_ = reply.ok;
                Reply<bool> r;
                r.ok = reply.ok;
                std::move(func)(std::move(r));
            },
            "MULTI");
    }

    /**
     * @brief Executes all commands issued after MULTI
     *
     * @tparam Result Result type for the transaction commands
     * @return Vector containing the results of all commands in the transaction
     */
    template <typename Result>
    std::vector<Result>
    exec() {
        return derived().template command<std::vector<Result>>("EXEC").result;
    }

    /**
     * @brief Asynchronous version of exec
     *
     * @tparam Func Callback function type
     * @tparam Result Result type for the transaction commands
     * @param func Callback function
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func, typename Result>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<Result>> &&>, Derived &>
    exec(Func &&func) {
        exec_flag_ = false;
        return derived().template command<std::vector<Result>>(std::forward<Func>(func), "EXEC");
    }

    /**
     * @brief Discards all commands issued after MULTI
     *
     * @return true if the transaction was discarded successfully, false otherwise
     */
    bool
    discard() {
        exec_flag_ = false;
        return derived().template command<void>("DISCARD").ok;
    }

    /**
     * @brief Asynchronous version of discard
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<void> &&>, Derived &>
    discard(Func &&func) {
        exec_flag_ = false;
        return derived().template command<void>(std::forward<Func>(func), "DISCARD");
    }

    /**
     * @brief Watches the given keys for changes
     *
     * @param key Key to watch
     * @return true if successful, false otherwise
     */
    bool
    watch(const std::string &key) {
        return derived().template command<void>("WATCH", key).ok;
    }

    /**
     * @brief Asynchronous version of watch for a single key
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key to watch
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    watch(Func &&func, const std::string &key) {
        return derived().template command<void>(std::forward<Func>(func), "WATCH", key);
    }

    /**
     * @brief Watches multiple keys for changes
     *
     * @param keys Vector of keys to watch
     * @return true if successful, false otherwise
     */
    bool
    watch(const std::vector<std::string> &keys) {
        return derived().template command_argv<void>("WATCH", keys).ok;
    }

    /**
     * @brief Asynchronous version of watch for multiple keys
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param keys Vector of keys to watch
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func, typename... Args>
    std::enable_if_t<std::is_invocable_v<Func, Reply<void> &&>, Derived &>
    watch(Func &&func, Args &&...args) {
        return static_cast<Derived &>(*this).template command<void>(
            std::forward<Func>(func), "WATCH", std::forward<Args>(args)...);
    }

    /**
     * @brief Unwatches all previously watched keys
     *
     * @return true if successful, false otherwise
     */
    bool
    unwatch() {
        return derived().template command<void>("UNWATCH").ok;
    }

    /**
     * @brief Asynchronous version of unwatch
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<void> &&>, Derived &>
    unwatch(Func &&func) {
        return derived().template command<void>(std::forward<Func>(func), "UNWATCH");
    }

    /**
     * @brief Checks if currently in a transaction
     *
     * @return true if in a transaction, false otherwise
     */
    bool
    is_in_multi() const {
        return exec_flag_;
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_TRANSACTION_COMMANDS_H 