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
     * @brief Adds geospatial items to a sorted set (coroutine awaitable)
     *
     * @tparam Members Variadic types for member specifications
     * @param key Key where the geospatial data is stored
     * @param members Members to add (longitude, latitude, name triplets)
     * @return Awaitable that yields Reply<long long>
     */
    template <typename... Members>
    auto geoadd(const std::string &key, Members &&...members) {
        return derived().template make_coro_command<long long>(
            [this, key, ...members = std::forward<Members>(members)](auto&& callback) mutable {
                this->geoadd(std::move(callback), key, std::forward<decltype(members)>(members)...);
            }
        );
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
     * @brief Calculates the distance between two members of a geospatial index (coroutine awaitable)
     *
     * @param key Key where the geospatial data is stored
     * @param member1 First member name
     * @param member2 Second member name
     * @param unit Unit of distance (m, km, mi, ft)
     * @return Awaitable that yields Reply<std::optional<double>>
     */
    auto geodist(const std::string &key, const std::string &member1,
                 const std::string &member2, GeoUnit unit = GeoUnit::M) {
        return derived().template make_coro_command<std::optional<double>>(
            [this, key, member1, member2, unit](auto&& callback) {
                this->geodist(std::move(callback), key, member1, member2, unit);
            }
        );
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
            to_string(unit));
    }

    /**
     * @brief Returns Geohash strings for members of a geospatial index (coroutine awaitable)
     *
     * @tparam Members Variadic types for member names
     * @param key Key where the geospatial data is stored
     * @param members Member names to get Geohash strings for
     * @return Awaitable that yields Reply<std::vector<std::optional<std::string>>>
     */
    template <typename... Members>
    auto geohash(const std::string &key, Members &&...members) {
        return derived().template make_coro_command<std::vector<std::optional<std::string>>>(
            [this, key, ...members = std::forward<Members>(members)](auto&& callback) mutable {
                this->geohash(std::move(callback), key, std::forward<decltype(members)>(members)...);
            }
        );
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
     * @brief Returns longitude and latitude of members of a geospatial index (coroutine awaitable)
     *
     * @tparam Members Variadic types for member names
     * @param key Key where the geospatial data is stored
     * @param members Member names to get coordinates for
     * @return Awaitable that yields Reply<std::vector<std::optional<geo_pos>>>
     */
    template <typename... Members>
    auto geopos(const std::string &key, Members &&...members) {
        return derived().template make_coro_command<std::vector<std::optional<geo_pos>>>(
            [this, key, ...members = std::forward<Members>(members)](auto&& callback) mutable {
                this->geopos(std::move(callback), key, std::forward<decltype(members)>(members)...);
            }
        );
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
     * point (coroutine awaitable)
     *
     * @param key Key where the geospatial data is stored
     * @param longitude Center point longitude
     * @param latitude Center point latitude
     * @param radius Radius of the search
     * @param unit Unit of distance (m, km, mi, ft)
     * @param options Optional parameters for the search (WITHCOORD, WITHDIST, WITHHASH,
     * COUNT, SORT)
     * @return Awaitable that yields Reply<std::vector<std::string>>
     */
    auto georadius(const std::string &key, double longitude, double latitude, double radius,
                   GeoUnit unit = GeoUnit::M, const std::vector<std::string> &options = {}) {
        return derived().template make_coro_command<std::vector<std::string>>(
            [this, key, longitude, latitude, radius, unit, options](auto&& callback) {
                this->georadius(std::move(callback), key, longitude, latitude, radius, unit, options);
            }
        );
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
            to_string(unit), options);
    }

    /**
     * @brief Returns members of a geospatial index that are within a radius of a given
     * member (coroutine awaitable)
     *
     * @param key Key where the geospatial data is stored
     * @param member Member to use as center point
     * @param radius Radius of the search
     * @param unit Unit of distance (m, km, mi, ft)
     * @param options Optional parameters for the search (WITHCOORD, WITHDIST, WITHHASH,
     * COUNT, SORT)
     * @return Awaitable that yields Reply<std::vector<std::string>>
     */
    auto georadiusbymember(const std::string &key, const std::string &member, double radius,
                          GeoUnit unit = GeoUnit::M, const std::vector<std::string> &options = {}) {
        return derived().template make_coro_command<std::vector<std::string>>(
            [this, key, member, radius, unit, options](auto&& callback) {
                this->georadiusbymember(std::move(callback), key, member, radius, unit, options);
            }
        );
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
            std::to_string(radius), to_string(unit), options);
    }

    /**
     * @brief Search for members in a geospatial index using various search criteria (coroutine awaitable)
     *
     * @param key Key where the geospatial data is stored
     * @param member Member to use as center point (for FROMMEMBER)
     * @param radius Radius of the search
     * @param unit Unit of distance (m, km, mi, ft)
     * @param options Optional parameters for the search (WITHCOORD, WITHDIST, WITHHASH,
     * COUNT, SORT)
     * @return Awaitable that yields Reply<std::vector<std::string>>
     */
    auto geosearch(const std::string &key, const std::string &member, double radius,
                   GeoUnit unit = GeoUnit::M, const std::vector<std::string> &options = {}) {
        return derived().template make_coro_command<std::vector<std::string>>(
            [this, key, member, radius, unit, options](auto&& callback) {
                this->geosearch(std::move(callback), key, member, radius, unit, options);
            }
        );
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
            std::to_string(radius), to_string(unit), options);
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_GEO_COMMANDS_H
