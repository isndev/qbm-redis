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

#ifndef QBM_REDIS_HYPERLOG_COMMANDS_H
#define QBM_REDIS_HYPERLOG_COMMANDS_H
#include "reply.h"

namespace qb::redis {

template <typename Derived>
class hyperlog_commands {
public:
    template <typename... Elements>
    bool
    pfadd(const std::string &key, Elements &&...elements) {
        return static_cast<Derived &>(*this)
            .template command<bool>("PFADD", key, std::forward<Elements>(elements)...)
            .ok;
    }
    template <typename Func, typename... Elements>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    pfadd(Func &&func, const std::string &key, Elements &&...elements) {
        return static_cast<Derived &>(*this).template command<bool>(
            std::forward<Func>(func),
            "PFADD",
            key,
            std::forward<Elements>(elements)...);
    }

    template <typename... Keys>
    long long
    pfcount(Keys &&...keys) {
        return static_cast<Derived &>(*this)
            .template command<long long>("PFCOUNT", std::forward<Keys>(keys)...)
            .result;
    }
    template <typename Func, typename... Keys>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    pfcount(Func &&func, Keys &&...keys) {
        return static_cast<Derived &>(*this).template command<long long>(
            std::forward<Func>(func),
            "PFCOUNT",
            std::forward<Keys>(keys)...);
    }

    template <typename... Keys>
    bool
    pfmerge(const std::string &destination, Keys &&...keys) {
        return static_cast<Derived &>(*this)
            .template command<void>("PFMERGE", destination, std::forward<Keys>(keys)...)
            .ok;
    }
    template <typename Func, typename... Keys>
    std::enable_if_t<std::is_invocable_v<Func, Reply<void> &&>, Derived &>
    pfmerge(Func &&func, const std::string &destination, Keys &&...keys) {
        return static_cast<Derived &>(*this).template command<void>(
            std::forward<Func>(func),
            "PFMERGE",
            destination,
            std::forward<Keys>(keys)...);
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_HYPERLOG_COMMANDS_H
