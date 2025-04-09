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
private:
    constexpr Derived &
    derived() {
        return static_cast<Derived &>(*this);
    }

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
        return derived()
            .template command<long long>("GEOADD", key,
                                         std::forward<Members>(members)...)
            .result();
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
        return derived().template command<long long>(
            std::forward<Func>(func), "GEOADD", key, std::forward<Members>(members)...);
    }

    /**
     * @brief Calculates the distance between two members of a geospatial index
     *
     * @param key Key where the geospatial data is stored
     * @param member1 First member name
     * @param member2 Second member name
     * @param unit Unit of distance (m, km, mi, ft)
     * @return Distance between the two members in the specified unit, or nullopt if one
     * or both members don't exist
     */
    std::optional<double>
    geodist(const std::string &key, const std::string &member1,
            const std::string &member2, GeoUnit unit = GeoUnit::M) {
        return derived()
            .template command<std::optional<double>>("GEODIST", key, member1, member2,
                                                     std::to_string(unit))
            .result();
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
    geodist(Func &&func, const std::string &key, const std::string &member1,
            const std::string &member2, GeoUnit unit = GeoUnit::M) {
        return derived().template command<std::optional<double>>(
            std::forward<Func>(func), "GEODIST", key, member1, member2,
            std::to_string(unit));
    }

    /**
     * @brief Returns Geohash strings for members of a geospatial index
     *
     * @tparam Members Variadic types for member names
     * @param key Key where the geospatial data is stored
     * @param members Member names to get Geohash strings for
     * @return Vector of optional Geohash strings, where nullopt indicates that the
     * member was not found
     */
    template <typename... Members>
    std::vector<std::optional<std::string>>
    geohash(const std::string &key, Members &&...members) {
        return derived()
            .template command<std::vector<std::optional<std::string>>>(
                "GEOHASH", key, std::forward<Members>(members)...)
            .result();
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
    std::enable_if_t<
        std::is_invocable_v<Func, Reply<std::vector<std::optional<std::string>>> &&>,
        Derived &>
    geohash(Func &&func, const std::string &key, Members &&...members) {
        return derived().template command<std::vector<std::optional<std::string>>>(
            std::forward<Func>(func), "GEOHASH", key, std::forward<Members>(members)...);
    }

    /**
     * @brief Returns longitude and latitude of members of a geospatial index
     *
     * @tparam Members Variadic types for member names
     * @param key Key where the geospatial data is stored
     * @param members Member names to get coordinates for
     * @return Vector of optional coordinate pairs, where nullopt indicates that the
     * member was not found
     */
    template <typename... Members>
    std::vector<std::optional<geo_pos>>
    geopos(const std::string &key, Members &&...members) {
        return derived()
            .template command<std::vector<std::optional<geo_pos>>>(
                "GEOPOS", key, std::forward<Members>(members)...)
            .result();
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
        std::is_invocable_v<Func, Reply<std::vector<std::optional<geo_pos>>> &&>,
        Derived &>
    geopos(Func &&func, const std::string &key, Members &&...members) {
        return derived().template command<std::vector<std::optional<geo_pos>>>(
            std::forward<Func>(func), "GEOPOS", key, std::forward<Members>(members)...);
    }

    /**
     * @brief Returns members of a geospatial index that are within a radius of a given
     * point
     *
     * @param key Key where the geospatial data is stored
     * @param longitude Center point longitude
     * @param latitude Center point latitude
     * @param radius Radius of the search
     * @param unit Unit of distance (m, km, mi, ft)
     * @param options Optional parameters for the search (WITHCOORD, WITHDIST, WITHHASH,
     * COUNT, SORT)
     * @return Vector of members within the radius
     */
    std::vector<std::string>
    georadius(const std::string &key, double longitude, double latitude, double radius,
              GeoUnit unit = GeoUnit::M, const std::vector<std::string> &options = {}) {
        return derived()
            .template command<std::vector<std::string>>("GEORADIUS", key, longitude,
                                                        latitude, radius,
                                                        std::to_string(unit), options)
            .result();
    }

    /**
     * @brief Asynchronous version of georadius
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the geospatial data is stored
     * @param longitude Center point longitude
     * @param latitude Center point latitude
     * @param radius Radius of the search
     * @param unit Unit of distance (m, km, mi, ft)
     * @param options Optional parameters for the search (WITHCOORD, WITHDIST, WITHHASH,
     * COUNT, SORT)
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>,
                     Derived &>
    georadius(Func &&func, const std::string &key, double longitude, double latitude,
              double radius, GeoUnit unit = GeoUnit::M,
              const std::vector<std::string> &options = {}) {
        return derived().template command<std::vector<std::string>>(
            std::forward<Func>(func), "GEORADIUS", key, longitude, latitude, radius,
            std::to_string(unit), options);
    }

    /**
     * @brief Returns members of a geospatial index that are within a radius of a given
     * member
     *
     * @param key Key where the geospatial data is stored
     * @param member Member to use as center point
     * @param radius Radius of the search
     * @param unit Unit of distance (m, km, mi, ft)
     * @param options Optional parameters for the search (WITHCOORD, WITHDIST, WITHHASH,
     * COUNT, SORT)
     * @return Vector of members within the radius
     */
    std::vector<std::string>
    georadiusbymember(const std::string &key, const std::string &member, double radius,
                      GeoUnit                         unit    = GeoUnit::M,
                      const std::vector<std::string> &options = {}) {
        return derived()
            .template command<std::vector<std::string>>(
                "GEORADIUSBYMEMBER", key, member, radius, std::to_string(unit), options)
            .result();
    }

    /**
     * @brief Asynchronous version of georadiusbymember
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the geospatial data is stored
     * @param member Member to use as center point
     * @param radius Radius of the search
     * @param unit Unit of distance (m, km, mi, ft)
     * @param options Optional parameters for the search (WITHCOORD, WITHDIST, WITHHASH,
     * COUNT, SORT)
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>,
                     Derived &>
    georadiusbymember(Func &&func, const std::string &key, const std::string &member,
                      double radius, GeoUnit unit = GeoUnit::M,
                      const std::vector<std::string> &options = {}) {
        return derived().template command<std::vector<std::string>>(
            std::forward<Func>(func), "GEORADIUSBYMEMBER", key, member,
            std::to_string(radius), std::to_string(unit), options);
    }

    /**
     * @brief Search for members in a geospatial index using various search criteria
     *
     * @param key Key where the geospatial data is stored
     * @param longitude Center point longitude (for FROMMEMBER)
     * @param latitude Center point latitude (for FROMMEMBER)
     * @param member Member to use as center point (for FROMMEMBER)
     * @param radius Radius of the search
     * @param unit Unit of distance (m, km, mi, ft)
     * @param options Optional parameters for the search (WITHCOORD, WITHDIST, WITHHASH,
     * COUNT, SORT)
     * @return Vector of members matching the search criteria
     */
    std::vector<std::string>
    geosearch(const std::string &key, const std::string &member, double radius,
              GeoUnit unit = GeoUnit::M, const std::vector<std::string> &options = {}) {
        return derived()
            .template command<std::vector<std::string>>(
                "GEOSEARCH", key, "FROMMEMBER", member, "BYRADIUS",
                std::to_string(radius), std::to_string(unit), options)
            .result();
    }

    /**
     * @brief Asynchronous version of geosearch
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the geospatial data is stored
     * @param member Member to use as center point
     * @param radius Radius of the search
     * @param unit Unit of distance (m, km, mi, ft)
     * @param options Optional parameters for the search (WITHCOORD, WITHDIST, WITHHASH,
     * COUNT, SORT)
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>,
                     Derived &>
    geosearch(Func &&func, const std::string &key, const std::string &member,
              double radius, GeoUnit unit = GeoUnit::M,
              const std::vector<std::string> &options = {}) {
        return derived().template command<std::vector<std::string>>(
            std::forward<Func>(func), "GEOSEARCH", key, "FROMMEMBER", member, "BYRADIUS",
            std::to_string(radius), std::to_string(unit), options);
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_GEO_COMMANDS_H
