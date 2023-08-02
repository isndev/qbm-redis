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

#ifndef QBM_REDIS_PUBLISH_COMMANDS_H
#define QBM_REDIS_PUBLISH_COMMANDS_H
#include "reply.h"

namespace qb::redis {

template <typename Derived>
class publish_commands {
public:
    long long
    publish(const std::string &channel, const std::string &message) {
        return static_cast<Derived &>(*this).template command<long long>("PUBLISH", channel, message).result;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    publish(Func &&func, const std::string &channel, const std::string &message) {
        return static_cast<Derived &>(*this)
            .template command<long long>(std::forward<Func>(func), "PUBLISH", channel, message);
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_PUBLISH_COMMANDS_H
