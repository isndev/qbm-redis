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

template <typename Derived>
class subscription_commands {
public:
    reply::subscription
    subscribe(const std::string &channel) {
        return static_cast<Derived &>(*this).template command<reply::subscription>("SUBSCRIBE", channel).result;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<reply::subscription> &&>, Derived &>
    subscribe(Func &&func, const std::string &channel) {
        return static_cast<Derived &>(*this).template command<reply::subscription>(
            std::forward<Func>(func),
            "SUBSCRIBE",
            channel);
    }

    reply::subscription
    psubscribe(const std::string &channel) {
        return static_cast<Derived &>(*this).template command<reply::subscription>("PSUBSCRIBE", channel).result;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<reply::subscription> &&>, Derived &>
    psubscribe(Func &&func, const std::string &channel) {
        return static_cast<Derived &>(*this).template command<reply::subscription>(
            std::forward<Func>(func),
            "PSUBSCRIBE",
            channel);
    }

    reply::subscription
    unsubscribe(const std::string &channel) {
        return static_cast<Derived &>(*this).template command<reply::subscription>("UNSUBSCRIBE", channel).result;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<reply::subscription> &&>, Derived &>
    unsubscribe(Func &&func, const std::string &channel) {
        return static_cast<Derived &>(*this).template command<reply::subscription>(
            std::forward<Func>(func),
            "UNSUBSCRIBE",
            channel);
    }

    reply::subscription
    punsubscribe(const std::string &channel) {
        return static_cast<Derived &>(*this).template command<reply::subscription>("PUNSUBSCRIBE", channel).result;
    }
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
