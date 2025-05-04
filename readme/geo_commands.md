# `qbm-redis`: Geospatial Commands

This document covers Redis commands operating on Geospatial index values.

Reference: [Redis Geospatial Commands](https://redis.io/commands/?group=geo)

## Common Types & Reply Types

*   **`qb::redis::geo_member` Struct:** `{ double longitude; double latitude; std::string member; }`
*   **`qb::redis::geo_pos` Struct:** `{ double longitude; double latitude; }`
*   **`qb::redis::GeoUnit` Enum:** `M` (meters), `KM` (kilometers), `MI` (miles), `FT` (feet).
*   **`qb::redis::search_result` Struct:** See `types.h`. Used by radius commands with options like `WITHDIST`, `WITHCOORD`, `WITHHASH`.
*   `qb::redis::Reply<long long>`: For counts (`GEOADD`).
*   `qb::redis::Reply<std::optional<double>>`: For `GEODIST`.
*   `qb::redis::Reply<std::vector<std::optional<std::string>>>`: For `GEOHASH`.
*   `qb::redis::Reply<std::vector<std::optional<qb::redis::geo_pos>>>`: For `GEOPOS`.
*   `qb::redis::Reply<std::vector<std::string>>`: For radius searches returning only members.
*   `qb::redis::Reply<std::vector<qb::redis::search_result>>`: For radius searches returning members with additional data (distance, coordinates, hash).

## Commands

### `GEOADD key [NX|XX] [CH] longitude latitude member [longitude latitude member ...]`

Adds the specified geospatial items (latitude, longitude, name) to the specified key. Returns the number of elements added to the sorted set (excluding score updates).

*   **Sync:** `Reply<long long> geoadd(const std::string &key, Members &&...members)` (Variadic template taking `geo_member` structs)
*   **Async:** `void geoadd_async(const std::string &key, std::vector<geo_member> members, Callback<long long> cb)`
*   **Options:** `NX`, `XX`, `CH` flags can be added before the first coordinate pair (not explicitly exposed in the C++ API, might require custom command building if needed).

### `GEODIST key member1 member2 [unit]`

Returns the distance between two members in the geospatial index.

*   **Sync:** `Reply<std::optional<double>> geodist(const std::string &key, const std::string &member1, const std::string &member2, GeoUnit unit = GeoUnit::M)`
*   **Async:** `void geodist_async(const std::string &key, const std::string &member1, const std::string &member2, Callback<std::optional<double>> cb, GeoUnit unit = GeoUnit::M)`

### `GEOHASH key member [member ...]`

Returns Geohash strings representing the position of one or more members.

*   **Sync:** `Reply<std::vector<std::optional<std::string>>> geohash(const std::string &key, const std::vector<std::string> &members)`
*   **Async:** `void geohash_async(const std::string &key, const std::vector<std::string> &members, Callback<std::vector<std::optional<std::string>>> cb)`

### `GEOPOS key member [member ...]`

Returns the longitude and latitude of one or more members.

*   **Sync:** `Reply<std::vector<std::optional<geo_pos>>> geopos(const std::string &key, const std::vector<std::string> &members)`
*   **Async:** `void geopos_async(const std::string &key, const std::vector<std::string> &members, Callback<std::vector<std::optional<geo_pos>>> cb)`

### `GEORADIUS key longitude latitude radius m|km|ft|mi [WITHCOORD] [WITHDIST] [WITHHASH] [COUNT count [ANY]] [ASC|DESC] [STORE key] [STOREDIST key]`

Queries members within the given radius of a coordinate.

*   **Sync (Members Only):** `Reply<std::vector<std::string>> georadius(const std::string &key, double longitude, double latitude, double radius, GeoUnit unit)`
*   **Sync (With Options):** `Reply<std::vector<search_result>> georadius(const std::string &key, double longitude, double latitude, double radius, GeoUnit unit, const geo_radius_options &options)`
*   **Async (Members Only):** `void georadius_async(const std::string &key, double longitude, double latitude, double radius, GeoUnit unit, Callback<std::vector<std::string>> cb)`
*   **Async (With Options):** `void georadius_async(const std::string &key, double longitude, double latitude, double radius, GeoUnit unit, const geo_radius_options &options, Callback<std::vector<search_result>> cb)`
*   **`geo_radius_options` Struct:** Allows setting `with_coord`, `with_dist`, `with_hash`, `count`, `any` (for count), `order` (`ASC`/`DESC`), `store_key`, `storedist_key`.

### `GEORADIUSBYMEMBER key member radius m|km|ft|mi [WITHCOORD] [WITHDIST] [WITHHASH] [COUNT count [ANY]] [ASC|DESC] [STORE key] [STOREDIST key]`

Queries members within the given radius of an existing member.

*   **Sync (Members Only):** `Reply<std::vector<std::string>> georadiusbymember(const std::string &key, const std::string &member, double radius, GeoUnit unit)`
*   **Sync (With Options):** `Reply<std::vector<search_result>> georadiusbymember(const std::string &key, const std::string &member, double radius, GeoUnit unit, const geo_radius_options &options)`
*   **Async (Members Only):** `void georadiusbymember_async(const std::string &key, const std::string &member, double radius, GeoUnit unit, Callback<std::vector<std::string>> cb)`
*   **Async (With Options):** `void georadiusbymember_async(const std::string &key, const std::string &member, double radius, GeoUnit unit, const geo_radius_options &options, Callback<std::vector<search_result>> cb)`
*   **`geo_radius_options`:** Same as for `GEORADIUS`.

### `GEOSEARCH key [FROMMEMBER member] [FROMLONLAT longitude latitude] [BYRADIUS radius m|km|ft|mi] [BYBOX width height m|km|ft|mi] [ASC|DESC] [COUNT count [ANY]] [WITHCOORD] [WITHDIST] [WITHHASH]`

Queries members within a circular area defined by radius or a rectangular area defined by a box, centered either on a coordinate or an existing member.

*   **Sync (Members Only):** `Reply<std::vector<std::string>> geosearch(const std::string &key, const geo_search_options &options)`
*   **Sync (With Return Options):** `Reply<std::vector<search_result>> geosearch_with_results(const std::string &key, const geo_search_options &options)`
*   **Async (Members Only):** `void geosearch_async(const std::string &key, const geo_search_options &options, Callback<std::vector<std::string>> cb)`
*   **Async (With Return Options):** `void geosearch_with_results_async(const std::string &key, const geo_search_options &options, Callback<std::vector<search_result>> cb)`
*   **`geo_search_options` Struct:** More complex, allows specifying `from_member` or `from_lonlat`, `by_radius` or `by_box`, `order`, `count`, `any`, `with_coord`, `with_dist`, `with_hash`.

### `GEOSEARCHSTORE destination source [FROMMEMBER member] ... [STOREDIST]` (arguments same as GEOSEARCH)

Similar to `GEOSEARCH` but stores the results (and optionally distances) in another key.

*   **Sync:** `Reply<long long> geosearchstore(const std::string &destination, const std::string &source, const geo_search_options &options, bool storedist = false)`
*   **Async:** `void geosearchstore_async(const std::string &destination, const std::string &source, const geo_search_options &options, Callback<long long> cb, bool storedist = false)` 