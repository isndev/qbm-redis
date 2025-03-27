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

/**
 * @class geo_commands
 * @brief Provides Redis geospatial command implementations.
 *
 * This class implements Redis geospatial commands for storing and querying
 * geospatial data in Redis. These commands allow for storing coordinates,
 * calculating distances, and performing radius searches.
 *
 * @tparam Derived The derived class type (CRTP pattern)
 */
template <typename Derived>
class geo_commands {
public:
    /**
     * @brief Adds geospatial items to a sorted set
     *
     * @tparam Members Variadic types for member specifications
     * @param key Key where the geospatial data is stored
     * @param members Members to add (longitude, latitude, name triplets)
     * @return Number of elements added to the sorted set
     */
    template <typename... Members>
    long long
    geoadd(const std::string &key, Members &&...members) {
        return static_cast<Derived &>(*this)
            .template command<long long>("GEOADD", key, std::forward<Members>(members)...)
            .result;
    }

    /**
     * @brief Asynchronous version of geoadd
     *
     * @tparam Func Callback function type
     * @tparam Members Variadic types for member specifications
     * @param func Callback function
     * @param key Key where the geospatial data is stored
     * @param members Members to add (longitude, latitude, name triplets)
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func, typename... Members>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    geoadd(Func &&func, const std::string &key, Members &&...members) {
        return static_cast<Derived &>(*this)
            .template command<long long>(std::forward<Func>(func), "GEOADD", key, std::forward<Members>(members)...);
    }

    /**
     * @brief Calculates the distance between two members of a geospatial index
     *
     * @param key Key where the geospatial data is stored
     * @param member1 First member name
     * @param member2 Second member name
     * @param unit Unit of distance (m, km, mi, ft)
     * @return Distance between the two members in the specified unit, or nullopt if one or both members don't exist
     */
    std::optional<double>
    geodist(const std::string &key, const std::string &member1, const std::string &member2, GeoUnit unit = GeoUnit::M) {
        return static_cast<Derived &>(*this)
            .template command<std::optional<double>>("GEODIST", key, member1, member2, std::to_string(unit))
            .result;
    }

    /**
     * @brief Asynchronous version of geodist
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the geospatial data is stored
     * @param member1 First member name
     * @param member2 Second member name
     * @param unit Unit of distance (m, km, mi, ft)
     * @return Reference to the Redis handler for chaining
     */
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

    /**
     * @brief Returns Geohash strings for members of a geospatial index
     *
     * @tparam Members Variadic types for member names
     * @param key Key where the geospatial data is stored
     * @param members Member names to get Geohash strings for
     * @return Vector of optional Geohash strings, where nullopt indicates that the member was not found
     */
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

    /**
     * @brief Asynchronous version of geohash
     *
     * @tparam Func Callback function type
     * @tparam Members Variadic types for member names
     * @param func Callback function
     * @param key Key where the geospatial data is stored
     * @param members Member names to get Geohash strings for
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func, typename... Members>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::optional<std::string>>> &&>, Derived &>
    geohash(Func &&func, const std::string &key, Members &&...members) {
        return static_cast<Derived &>(*this).template command<std::vector<std::optional<std::string>>>(
            std::forward<Func>(func),
            "GEOHASH",
            key,
            std::forward<Members>(members)...);
    }

    /**
     * @brief Returns longitude and latitude of members of a geospatial index
     *
     * @tparam Members Variadic types for member names
     * @param key Key where the geospatial data is stored
     * @param members Member names to get coordinates for
     * @return Vector of optional coordinate pairs, where nullopt indicates that the member was not found
     */
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

    /**
     * @brief Asynchronous version of geopos
     *
     * @tparam Func Callback function type
     * @tparam Members Variadic types for member names
     * @param func Callback function
     * @param key Key where the geospatial data is stored
     * @param members Member names to get coordinates for
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func, typename... Members>
    std::enable_if_t<
        std::is_invocable_v<Func, Reply<std::vector<std::optional<std::pair<double, double>>>> &&>, Derived &>
    geopos(Func &&func, const std::string &key, Members &&...members) {
        return static_cast<Derived &>(*this).template command<std::vector<std::optional<std::pair<double, double>>>>(
            std::forward<Func>(func),
            "GEOPOS",
            key,
            std::forward<Members>(members)...);
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_GEO_COMMANDS_H
