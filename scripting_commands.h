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

#ifndef QBM_REDIS_SCRIPTING_COMMANDS_H
#define QBM_REDIS_SCRIPTING_COMMANDS_H
#include "reply.h"

namespace qb::redis {

template <typename Derived>
class scripting_commands {
public:
    template <typename Ret>
    inline Ret
    eval(
        const std::string &script, const std::vector<std::string> &keys = {},
        const std::vector<std::string> &args = {}) {
        return static_cast<Derived &>(*this).template command<Ret>("EVAL", script, keys.size(), keys, args).result;
    }
    template <typename Func, typename Ret>
    std::enable_if_t<std::is_invocable_v<Func, Reply<Ret> &&>, Derived &>
    eval(
        Func &&func, const std::string &script, const std::vector<std::string> &keys = {},
        const std::vector<std::string> &args = {}) {
        return static_cast<Derived &>(*this)
            .template command<Ret>(std::forward<Func>(func), "EVAL", script, keys.size(), keys, args);
    }

    template <typename Ret>
    inline Ret
    evalsha(
        const std::string &script, const std::vector<std::string> &keys = {},
        const std::vector<std::string> &args = {}) {
        return static_cast<Derived &>(*this).template command<Ret>("EVALSHA", script, keys.size(), keys, args).result;
    }
    template <typename Func, typename Ret>
    std::enable_if_t<std::is_invocable_v<Func, Reply<Ret> &&>, Derived &>
    evalsha(
        Func &&func, const std::string &script, const std::vector<std::string> &keys = {},
        const std::vector<std::string> &args = {}) {
        return static_cast<Derived &>(*this)
            .template command<Ret>(std::forward<Func>(func), "EVALSHA", script, keys.size(), keys, args);
    }

    template <typename... Keys>
    std::vector<bool>
    script_exists(Keys &&...keys) {
        return static_cast<Derived &>(*this)
            .template command<std::vector<bool>>("SCRIPT", "EXISTS", std::forward<Keys>(keys)...)
            .result;
    }
    template <typename Func, typename... Keys>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<bool>> &&>, Derived &>
    script_exists(Func &&func, Keys &&...keys) {
        return static_cast<Derived &>(*this).template command<std::vector<bool>>(
            std::forward<Func>(func),
            "SCRIPT",
            "EXISTS",
            std::forward<Keys>(keys)...);
    }

    inline bool
    script_flush() {
        return static_cast<Derived &>(*this).template command<void>("SCRIPT", "FLUSH").ok;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<void> &&>, Derived &>
    script_flush(Func &&func) {
        return static_cast<Derived &>(*this).template command<void>(std::forward<Func>(func), "SCRIPT", "FLUSH");
    }

    inline bool
    script_kill() {
        return static_cast<Derived &>(*this).template command<void>("SCRIPT", "KILL").ok;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<void> &&>, Derived &>
    script_kill(Func &&func) {
        return static_cast<Derived &>(*this).template command<void>(std::forward<Func>(func), "SCRIPT", "KILL");
    }

    inline std::string
    script_load(std::string const &script) {
        return static_cast<Derived &>(*this).template command<std::string>("SCRIPT", "LOAD", script).result;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<void> &&>, Derived &>
    script_kill(Func &&func, std::string const &script) {
        return static_cast<Derived &>(*this)
            .template command<std::string>(std::forward<Func>(func), "SCRIPT", "LOAD", script);
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_SCRIPTING_COMMANDS_H
