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

#ifndef QBM_REDIS_GEO_COMMANDS_H
#define QBM_REDIS_GEO_COMMANDS_H
#include "reply.h"

namespace qb::redis {

template <typename Derived>
class geo_commands {
public:
    template <typename... Members>
    long long
    geoadd(const std::string &key, Members &&...members) {
        return static_cast<Derived &>(*this)
            .template command<long long>("GEOADD", key, std::forward<Members>(members)...)
            .result;
    }
    template <typename Func, typename... Members>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    geoadd(Func &&func, const std::string &key, Members &&...members) {
        return static_cast<Derived &>(*this).template command<long long>(
            std::forward<Func>(func),
            "GEOADD",
            key,
            std::forward<Members>(members)...);
    }

    std::optional<double>
    geodist(
        const std::string &key, const std::string &member1, const std::string &member2,
        GeoUnit unit = GeoUnit::M) {
        return static_cast<Derived &>(*this)
            .template command<std::optional<double>>("GEODIST", key, member1, member2, std::to_string(unit))
            .result;
    }
    template <typename Func>
    Derived &
    geodist(
        Func &&func, const std::string &key, const std::string &member1, const std::string &member2,
        GeoUnit unit = GeoUnit::M) {
        return static_cast<Derived &>(*this).template command<std::optional<double>>(
            std::forward<Func>(func),
            "GEODIST",
            key,
            member1,
            member2,
            std::to_string(unit));
    }

    template <typename... Members>
    std::vector<std::optional<std::string>>
    geohash(const std::string &key, Members &&...members) {
        return static_cast<Derived &>(*this)
            .template command<std::vector<std::optional<std::string>>>(
                "GEOHASH",
                key,
                std::forward<Members>(members)...)
            .result;
    }
    template <typename Func, typename... Members>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::optional<std::string>>> &&>, Derived &>
    geohash(Func &&func, const std::string &key, Members &&...members) {
        return static_cast<Derived &>(*this).template command<std::vector<std::optional<std::string>>>(
            std::forward<Func>(func),
            "GEOHASH",
            key,
            std::forward<Members>(members)...);
    }

    template <typename... Members>
    std::vector<std::optional<std::pair<double, double>>>
    geopos(const std::string &key, Members &&...members) {
        return static_cast<Derived &>(*this)
            .template command<std::vector<std::optional<std::pair<double, double>>>>(
                "GEOPOS",
                key,
                std::forward<Members>(members)...)
            .result;
    }
    template <typename Func, typename... Members>
    std::enable_if_t<
        std::is_invocable_v<Func, Reply<std::vector<std::optional<std::pair<double, double>>>> &&>,
        Derived &>
    geopos(Func &&func, const std::string &key, Members &&...members) {
        return static_cast<Derived &>(*this)
            .template command<std::vector<std::optional<std::pair<double, double>>>>(
                std::forward<Func>(func),
                "GEOPOS",
                key,
                std::forward<Members>(members)...);
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_HYPERLOG_COMMANDS_H
