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

#include <gtest/gtest.h>
#include <qb/io/async.h>
#include <thread>
#include "../redis.h"

// Redis Configuration
#define REDIS_URI \
    { "tcp://localhost:6379" }

using namespace qb::io;
using namespace std::chrono;

// Helper function to generate unique key prefixes
inline std::string
key_prefix(const std::string &key = "") {
    static int counter = 0;
    std::string prefix = "qb::redis::geo-test:" + std::to_string(++counter);
    
    if (key.empty()) {
        return prefix;
    }
    
    return prefix + ":" + key;
}

// Helper function to generate test keys
inline std::string
test_key(const std::string &k) {
    return "{" + key_prefix() + "}::" + k;
}

// Test fixture for Redis Geo commands
class RedisGeoTest : public ::testing::Test {
protected:
    qb::redis::tcp::client redis{REDIS_URI};
    
    void SetUp() override {
        async::init();
        if (!redis.connect() || !redis.flushall())
            throw std::runtime_error("Failed to connect to Redis");

        // Wait for connection to be established
        redis.await();
        TearDown();
    }
    
    void TearDown() override {
        // Cleanup after tests
        redis.flushall();
        redis.await();
    }
};

/*
 * SYNCHRONOUS TESTS
 */

// Test GEOADD
TEST_F(RedisGeoTest, SYNC_GEO_COMMANDS_GEOADD) {
    std::string key = test_key("geoadd");
    
    // Test adding single location
    EXPECT_EQ(redis.geoadd(key, 13.361389, 38.115556, "Palermo"), 1);
    
    // Test adding multiple locations
    EXPECT_EQ(redis.geoadd(key, 15.087269, 37.502669, "Catania", 13.583333, 37.316667, "Agrigento"), 2);
}

// Test GEODIST
TEST_F(RedisGeoTest, SYNC_GEO_COMMANDS_GEODIST) {
    std::string key = test_key("geodist");
    
    // Add test locations
    redis.geoadd(key, 13.361389, 38.115556, "Palermo", 15.087269, 37.502669, "Catania");
    
    // Test distance in meters (default)
    auto dist_m = redis.geodist(key, "Palermo", "Catania");
    EXPECT_TRUE(dist_m.has_value());
    EXPECT_GT(*dist_m, 0);
    
    // Test distance in kilometers
    auto dist_km = redis.geodist(key, "Palermo", "Catania", qb::redis::GeoUnit::KM);
    EXPECT_TRUE(dist_km.has_value());
    EXPECT_GT(*dist_km, 0);
    EXPECT_LT(*dist_km, *dist_m);
}

// Test GEOHASH
TEST_F(RedisGeoTest, SYNC_GEO_COMMANDS_GEOHASH) {
    std::string key = test_key("geohash");
    
    // Add test location
    redis.geoadd(key, 13.361389, 38.115556, "Palermo");
    
    // Test getting geohash
    auto hashes = redis.geohash(key, "Palermo");
    EXPECT_EQ(hashes.size(), 1);
    EXPECT_TRUE(hashes[0].has_value());
    EXPECT_FALSE(hashes[0]->empty());
}

// Test GEOPOS
TEST_F(RedisGeoTest, SYNC_GEO_COMMANDS_GEOPOS) {
    std::string key = test_key("geopos");
    
    // Add test location
    redis.geoadd(key, 13.361389, 38.115556, "Palermo");
    
    // Test getting position
    auto positions = redis.geopos(key, "Palermo");
    EXPECT_EQ(positions.size(), 1);
    EXPECT_TRUE(positions[0].has_value());
    EXPECT_NEAR(positions[0]->longitude, 13.361389, 0.000001);
    EXPECT_NEAR(positions[0]->latitude, 38.115556, 0.000001);
}

// Test GEORADIUS
TEST_F(RedisGeoTest, SYNC_GEO_COMMANDS_GEORADIUS) {
    std::string key = test_key("georadius");
    
    // Add test locations
    redis.geoadd(key, 13.361389, 38.115556, "Palermo", 15.087269, 37.502669, "Catania");
    
    // Test radius search from Palermo
    auto results = redis.georadius(key, 13.361389, 38.115556, 200, qb::redis::GeoUnit::KM);
    EXPECT_FALSE(results.empty());
    EXPECT_TRUE(std::find(results.begin(), results.end(), "Palermo") != results.end());
    
    // Test with options
    auto results_with_dist = redis.georadius(key, 13.361389, 38.115556, 200, qb::redis::GeoUnit::KM,
                                           {"WITHDIST", "WITHCOORD"});
    EXPECT_FALSE(results_with_dist.empty());
}

// Test GEORADIUSBYMEMBER
TEST_F(RedisGeoTest, SYNC_GEO_COMMANDS_GEORADIUSBYMEMBER) {
    std::string key = test_key("georadiusbymember");
    
    // Add test locations
    redis.geoadd(key, 13.361389, 38.115556, "Palermo", 15.087269, 37.502669, "Catania");
    
    // Test radius search from Palermo
    auto results = redis.georadiusbymember(key, "Palermo", 200, qb::redis::GeoUnit::KM);
    EXPECT_FALSE(results.empty());
    EXPECT_TRUE(std::find(results.begin(), results.end(), "Palermo") != results.end());
}

// Test GEOSEARCH
TEST_F(RedisGeoTest, SYNC_GEO_COMMANDS_GEOSEARCH) {
    std::string key = test_key("geosearch");
    
    // Add test locations
    redis.geoadd(key, 13.361389, 38.115556, "Palermo", 15.087269, 37.502669, "Catania");
    
    // Test search from Palermo
    auto results = redis.geosearch(key, "Palermo", 200, qb::redis::GeoUnit::KM);
    EXPECT_FALSE(results.empty());
    EXPECT_TRUE(std::find(results.begin(), results.end(), "Palermo") != results.end());
}

// Test GEORADIUS with additional options
TEST_F(RedisGeoTest, SYNC_GEO_COMMANDS_GEORADIUS_OPTIONS) {
    std::string key = test_key("georadius_options");
    
    // Add test locations
    redis.geoadd(key, 13.361389, 38.115556, "Palermo", 15.087269, 37.502669, "Catania");
    
    // Test with WITHDIST option
    auto results_with_dist = redis.georadius(key, 13.361389, 38.115556, 200, qb::redis::GeoUnit::KM, {"WITHDIST"});
    EXPECT_FALSE(results_with_dist.empty());
    
    // Test with WITHCOORD option
    auto results_with_coord = redis.georadius(key, 13.361389, 38.115556, 200, qb::redis::GeoUnit::KM, {"WITHCOORD"});
    EXPECT_FALSE(results_with_coord.empty());
    
    // Test with WITHHASH option
    auto results_with_hash = redis.georadius(key, 13.361389, 38.115556, 200, qb::redis::GeoUnit::KM, {"WITHHASH"});
    EXPECT_FALSE(results_with_hash.empty());
    
    // Test with COUNT option
    auto results_with_count = redis.georadius(key, 13.361389, 38.115556, 200, qb::redis::GeoUnit::KM, {"COUNT", "1"});
    EXPECT_EQ(results_with_count.size(), 1);
    
    // Test with SORT option
    auto results_with_sort = redis.georadius(key, 13.361389, 38.115556, 200, qb::redis::GeoUnit::KM, {"ASC"});
    EXPECT_FALSE(results_with_sort.empty());
}

// Test GEORADIUSBYMEMBER with additional options
TEST_F(RedisGeoTest, SYNC_GEO_COMMANDS_GEORADIUSBYMEMBER_OPTIONS) {
    std::string key = test_key("georadiusbymember_options");
    
    // Add test locations
    redis.geoadd(key, 13.361389, 38.115556, "Palermo", 15.087269, 37.502669, "Catania");
    
    // Test with WITHDIST option
    auto results_with_dist = redis.georadiusbymember(key, "Palermo", 200, qb::redis::GeoUnit::KM, {"WITHDIST"});
    EXPECT_FALSE(results_with_dist.empty());
    
    // Test with WITHCOORD option
    auto results_with_coord = redis.georadiusbymember(key, "Palermo", 200, qb::redis::GeoUnit::KM, {"WITHCOORD"});
    EXPECT_FALSE(results_with_coord.empty());
    
    // Test with WITHHASH option
    auto results_with_hash = redis.georadiusbymember(key, "Palermo", 200, qb::redis::GeoUnit::KM, {"WITHHASH"});
    EXPECT_FALSE(results_with_hash.empty());
    
    // Test with COUNT option
    auto results_with_count = redis.georadiusbymember(key, "Palermo", 200, qb::redis::GeoUnit::KM, {"COUNT", "1"});
    EXPECT_EQ(results_with_count.size(), 1);
    
    // Test with SORT option
    auto results_with_sort = redis.georadiusbymember(key, "Palermo", 200, qb::redis::GeoUnit::KM, {"ASC"});
    EXPECT_FALSE(results_with_sort.empty());
}

// Test GEOSEARCH with additional options
TEST_F(RedisGeoTest, SYNC_GEO_COMMANDS_GEOSEARCH_OPTIONS) {
    std::string key = test_key("geosearch_options");
    
    // Add test locations
    redis.geoadd(key, 13.361389, 38.115556, "Palermo", 15.087269, 37.502669, "Catania");
    
    // Test with WITHDIST option
    auto results_with_dist = redis.geosearch(key, "Palermo", 200, qb::redis::GeoUnit::KM, {"WITHDIST"});
    EXPECT_FALSE(results_with_dist.empty());
    
    // Test with WITHCOORD option
    auto results_with_coord = redis.geosearch(key, "Palermo", 200, qb::redis::GeoUnit::KM, {"WITHCOORD"});
    EXPECT_FALSE(results_with_coord.empty());
    
    // Test with WITHHASH option
    auto results_with_hash = redis.geosearch(key, "Palermo", 200, qb::redis::GeoUnit::KM, {"WITHHASH"});
    EXPECT_FALSE(results_with_hash.empty());
    
    // Test with COUNT option
    auto results_with_count = redis.geosearch(key, "Palermo", 200, qb::redis::GeoUnit::KM, {"COUNT", "1"});
    EXPECT_EQ(results_with_count.size(), 1);
    
    // Test with SORT option
    auto results_with_sort = redis.geosearch(key, "Palermo", 200, qb::redis::GeoUnit::KM, {"ASC"});
    EXPECT_FALSE(results_with_sort.empty());
}

// Test edge cases and error conditions
TEST_F(RedisGeoTest, SYNC_GEO_COMMANDS_EDGE_CASES) {
    std::string key = test_key("geo_edge_cases");
    
    // Test GEODIST with non-existent members
    auto dist = redis.geodist(key, "NonExistent1", "NonExistent2");
    EXPECT_FALSE(dist.has_value());
    
    // Test GEOPOS with non-existent member
    auto pos = redis.geopos(key, "NonExistent");
    EXPECT_EQ(pos.size(), 1);
    EXPECT_FALSE(pos[0].has_value());
    
    // Test GEOHASH with non-existent member
    auto hash = redis.geohash(key, "NonExistent");
    EXPECT_EQ(hash.size(), 1);
    EXPECT_FALSE(hash[0].has_value());
    
    // Test with empty key
    auto empty_results = redis.georadius("NonExistentKey", 13.361389, 38.115556, 200, qb::redis::GeoUnit::KM);
    EXPECT_TRUE(empty_results.empty());
    
    // Test with invalid coordinates
    try {
        redis.geoadd(key, 181.0, 91.0, "InvalidCoord");
        FAIL() << "Expected geoadd to throw with invalid coordinates";
    } catch (const std::exception& e) {
        // Expected exception
    }
}

// Test different distance units
TEST_F(RedisGeoTest, SYNC_GEO_COMMANDS_DISTANCE_UNITS) {
    std::string key = test_key("geo_distance_units");
    
    // Add test locations
    redis.geoadd(key, 13.361389, 38.115556, "Palermo", 15.087269, 37.502669, "Catania");
    
    // Test distance in meters (default)
    auto dist_m = redis.geodist(key, "Palermo", "Catania");
    EXPECT_TRUE(dist_m.has_value());
    
    // Test distance in kilometers
    auto dist_km = redis.geodist(key, "Palermo", "Catania", qb::redis::GeoUnit::KM);
    EXPECT_TRUE(dist_km.has_value());
    EXPECT_NEAR(*dist_km * 1000, *dist_m, 1.0); // Convert km to m for comparison
    
    // Test distance in miles
    auto dist_mi = redis.geodist(key, "Palermo", "Catania", qb::redis::GeoUnit::MI);
    EXPECT_TRUE(dist_mi.has_value());
    EXPECT_NEAR(*dist_mi * 1609.34, *dist_m, 1.0); // Convert mi to m for comparison
    
    // Test distance in feet
    auto dist_ft = redis.geodist(key, "Palermo", "Catania", qb::redis::GeoUnit::FT);
    EXPECT_TRUE(dist_ft.has_value());
    EXPECT_NEAR(*dist_ft * 0.3048, *dist_m, 1.0); // Convert ft to m for comparison
}

/*
 * ASYNCHRONOUS TESTS
 */

// Test async GEOADD
TEST_F(RedisGeoTest, ASYNC_GEO_COMMANDS_GEOADD) {
    std::string key = test_key("async_geoadd");
    long long result = 0;
    
    redis.geoadd([&](auto &&reply) {
        result = reply.result();
    }, key, 13.361389, 38.115556, "Palermo");
    
    redis.await();
    EXPECT_EQ(result, 1);
}

// Test async GEODIST
TEST_F(RedisGeoTest, ASYNC_GEO_COMMANDS_GEODIST) {
    std::string key = test_key("async_geodist");
    std::optional<double> distance;
    
    // Add test locations
    redis.geoadd(key, 13.361389, 38.115556, "Palermo", 15.087269, 37.502669, "Catania");
    
    redis.geodist([&](auto &&reply) {
        distance = reply.result();
    }, key, "Palermo", "Catania");
    
    redis.await();
    EXPECT_TRUE(distance.has_value());
    EXPECT_GT(*distance, 0);
}

// Test async GEOHASH
TEST_F(RedisGeoTest, ASYNC_GEO_COMMANDS_GEOHASH) {
    std::string key = test_key("async_geohash");
    std::vector<std::optional<std::string>> hashes;
    
    // Add test location
    redis.geoadd(key, 13.361389, 38.115556, "Palermo");
    
    redis.geohash([&](auto &&reply) {
        hashes = reply.result();
    }, key, "Palermo");
    
    redis.await();
    EXPECT_EQ(hashes.size(), 1);
    EXPECT_TRUE(hashes[0].has_value());
    EXPECT_FALSE(hashes[0]->empty());
}

// Test async GEOPOS
TEST_F(RedisGeoTest, ASYNC_GEO_COMMANDS_GEOPOS) {
    std::string key = test_key("async_geopos");
    std::vector<std::optional<qb::redis::geo_pos>> positions;
    
    // Add test location
    redis.geoadd(key, 13.361389, 38.115556, "Palermo");
    
    redis.geopos([&](auto &&reply) {
        positions = reply.result();
    }, key, "Palermo");
    
    redis.await();
    EXPECT_EQ(positions.size(), 1);
    EXPECT_TRUE(positions[0].has_value());
    EXPECT_NEAR(positions[0]->longitude, 13.361389, 0.000001);
    EXPECT_NEAR(positions[0]->latitude, 38.115556, 0.000001);
}

// Test async GEORADIUS
TEST_F(RedisGeoTest, ASYNC_GEO_COMMANDS_GEORADIUS) {
    std::string key = test_key("async_georadius");
    std::vector<std::string> results;
    
    // Add test locations
    redis.geoadd(key, 13.361389, 38.115556, "Palermo", 15.087269, 37.502669, "Catania");
    
    redis.georadius([&](auto &&reply) {
        results = reply.result();
    }, key, 13.361389, 38.115556, 200, qb::redis::GeoUnit::KM);
    
    redis.await();
    EXPECT_FALSE(results.empty());
    EXPECT_TRUE(std::find(results.begin(), results.end(), "Palermo") != results.end());
}

// Test async GEORADIUSBYMEMBER
TEST_F(RedisGeoTest, ASYNC_GEO_COMMANDS_GEORADIUSBYMEMBER) {
    std::string key = test_key("async_georadiusbymember");
    std::vector<std::string> results;
    
    // Add test locations
    redis.geoadd(key, 13.361389, 38.115556, "Palermo", 15.087269, 37.502669, "Catania");
    
    redis.georadiusbymember([&](auto &&reply) {
        results = reply.result();
    }, key, "Palermo", 200, qb::redis::GeoUnit::KM);
    
    redis.await();
    EXPECT_FALSE(results.empty());
    EXPECT_TRUE(std::find(results.begin(), results.end(), "Palermo") != results.end());
}

// Test async GEOSEARCH
TEST_F(RedisGeoTest, ASYNC_GEO_COMMANDS_GEOSEARCH) {
    std::string key = test_key("async_geosearch");
    std::vector<std::string> results;
    
    // Add test locations
    redis.geoadd(key, 13.361389, 38.115556, "Palermo", 15.087269, 37.502669, "Catania");
    
    redis.geosearch([&](auto &&reply) {
        results = reply.result();
    }, key, "Palermo", 200, qb::redis::GeoUnit::KM);
    
    redis.await();
    EXPECT_FALSE(results.empty());
    EXPECT_TRUE(std::find(results.begin(), results.end(), "Palermo") != results.end());
} 