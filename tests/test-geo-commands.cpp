/*
 * qb - C++ Actor Framework
 * Copyright (C) 2011-2026 isndev (cpp.actor). All rights reserved.
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
#include <qb/io/async/coroutine.h>
#include <thread>
#include "../redis.h"
#include "protocol_test_common.h"

using namespace qb::io;
using namespace std::chrono;

// ============================================================================
// Fixture: all tests run in both RESP2 and RESP3
// ============================================================================

class GeoProtocolModesTest : public ProtocolModesTestBase {};

INSTANTIATE_PROTOCOL_MODES(GeoProtocolModesTest);

/*
 * COROUTINE TESTS
 */

// Test GEOADD using coroutines
TEST_P(GeoProtocolModesTest, CORO_GEO_COMMANDS_GEOADD) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("coro_geoadd");

        // Test adding single location
        auto reply1 = co_await redis.geoadd(key, 13.361389, 38.115556, "Palermo");
        EXPECT_TRUE(reply1.ok());
        EXPECT_EQ(reply1.result(), 1);

        // Test adding multiple locations
        auto reply2 = co_await redis.geoadd(key, 15.087269, 37.502669, "Catania", 13.583333, 37.316667, "Agrigento");
        EXPECT_TRUE(reply2.ok());
        EXPECT_EQ(reply2.result(), 2);

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test GEODIST using coroutines
TEST_P(GeoProtocolModesTest, CORO_GEO_COMMANDS_GEODIST) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("coro_geodist");

        // Add test locations
        (void) co_await redis.geoadd(key, 13.361389, 38.115556, "Palermo", 15.087269, 37.502669, "Catania");

        // Test distance in meters (default)
        auto reply1 = co_await redis.geodist(key, "Palermo", "Catania");
        EXPECT_TRUE(reply1.ok());
        EXPECT_TRUE(reply1.result().has_value());
        EXPECT_GT(*reply1.result(), 0);

        // Test distance in kilometers
        auto reply2 = co_await redis.geodist(key, "Palermo", "Catania", qb::redis::GeoUnit::KM);
        EXPECT_TRUE(reply2.ok());
        EXPECT_TRUE(reply2.result().has_value());
        EXPECT_GT(*reply2.result(), 0);

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test GEOHASH using coroutines
TEST_P(GeoProtocolModesTest, CORO_GEO_COMMANDS_GEOHASH) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("coro_geohash");

        // Add test location
        (void) co_await redis.geoadd(key, 13.361389, 38.115556, "Palermo");

        // Test getting geohash
        auto reply = co_await redis.geohash(key, "Palermo");
        EXPECT_TRUE(reply.ok());
        auto hashes = reply.result();
        EXPECT_EQ(hashes.size(), 1);
        EXPECT_TRUE(hashes[0].has_value());
        EXPECT_FALSE(hashes[0]->empty());

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test GEOPOS using coroutines
TEST_P(GeoProtocolModesTest, CORO_GEO_COMMANDS_GEOPOS) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("coro_geopos");

        // Add test location
        (void) co_await redis.geoadd(key, 13.361389, 38.115556, "Palermo");

        // Test getting position
        auto reply = co_await redis.geopos(key, "Palermo");
        EXPECT_TRUE(reply.ok());
        auto positions = reply.result();
        EXPECT_EQ(positions.size(), 1);
        EXPECT_TRUE(positions[0].has_value());
        EXPECT_NEAR(positions[0]->longitude, 13.361389, 0.000001);
        EXPECT_NEAR(positions[0]->latitude, 38.115556, 0.000001);

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test GEORADIUS using coroutines
TEST_P(GeoProtocolModesTest, CORO_GEO_COMMANDS_GEORADIUS) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("coro_georadius");

        // Add test locations
        (void) co_await redis.geoadd(key, 13.361389, 38.115556, "Palermo", 15.087269, 37.502669, "Catania");

        // Test radius search from Palermo
        auto reply = co_await redis.georadius(key, 13.361389, 38.115556, 200, qb::redis::GeoUnit::KM);
        EXPECT_TRUE(reply.ok());
        auto results = reply.result();
        EXPECT_FALSE(results.empty());
        EXPECT_TRUE(std::find(results.begin(), results.end(), "Palermo") != results.end());

        // Test with options
        auto reply2 =
            co_await redis.georadius(key, 13.361389, 38.115556, 200, qb::redis::GeoUnit::KM, std::vector<std::string>{"WITHDIST", "WITHCOORD"});
        EXPECT_TRUE(reply2.ok());
        EXPECT_FALSE(reply2.result().empty());

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test GEORADIUSBYMEMBER using coroutines
TEST_P(GeoProtocolModesTest, CORO_GEO_COMMANDS_GEORADIUSBYMEMBER) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("coro_georadiusbymember");

        // Add test locations
        (void) co_await redis.geoadd(key, 13.361389, 38.115556, "Palermo", 15.087269, 37.502669, "Catania");

        // Test radius search from Palermo
        auto reply = co_await redis.georadiusbymember(key, "Palermo", 200, qb::redis::GeoUnit::KM);
        EXPECT_TRUE(reply.ok());
        auto results = reply.result();
        EXPECT_FALSE(results.empty());
        EXPECT_TRUE(std::find(results.begin(), results.end(), "Palermo") != results.end());

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test GEOSEARCH using coroutines
TEST_P(GeoProtocolModesTest, CORO_GEO_COMMANDS_GEOSEARCH) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("coro_geosearch");

        // Add test locations
        (void) co_await redis.geoadd(key, 13.361389, 38.115556, "Palermo", 15.087269, 37.502669, "Catania");

        // Test search from Palermo
        auto reply = co_await redis.geosearch(key, "Palermo", 200, qb::redis::GeoUnit::KM);
        EXPECT_TRUE(reply.ok());
        auto results = reply.result();
        EXPECT_FALSE(results.empty());
        EXPECT_TRUE(std::find(results.begin(), results.end(), "Palermo") != results.end());

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test GEORADIUS with additional options using coroutines
TEST_P(GeoProtocolModesTest, CORO_GEO_COMMANDS_GEORADIUS_OPTIONS) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("coro_georadius_options");

        // Add test locations
        (void) co_await redis.geoadd(key, 13.361389, 38.115556, "Palermo", 15.087269, 37.502669, "Catania");

        // Test with WITHDIST option
        auto reply1 = co_await redis.georadius(key, 13.361389, 38.115556, 200, qb::redis::GeoUnit::KM, std::vector<std::string>{"WITHDIST"});
        EXPECT_TRUE(reply1.ok());
        EXPECT_FALSE(reply1.result().empty());

        // Test with COUNT option
        auto reply2 = co_await redis.georadius(key, 13.361389, 38.115556, 200, qb::redis::GeoUnit::KM, std::vector<std::string>{"COUNT", "1"});
        EXPECT_TRUE(reply2.ok());
        EXPECT_LE(reply2.result().size(), 1);

        // Test with SORT option
        auto reply3 = co_await redis.georadius(key, 13.361389, 38.115556, 200, qb::redis::GeoUnit::KM, std::vector<std::string>{"ASC"});
        EXPECT_TRUE(reply3.ok());
        EXPECT_FALSE(reply3.result().empty());

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test edge cases using coroutines
TEST_P(GeoProtocolModesTest, CORO_GEO_COMMANDS_EDGE_CASES) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("coro_geo_edge_cases");

        // Test GEODIST with non-existent members
        auto reply1 = co_await redis.geodist(key, "NonExistent1", "NonExistent2");
        EXPECT_TRUE(reply1.ok());
        EXPECT_FALSE(reply1.result().has_value());

        // Test GEOPOS with non-existent member
        auto reply2 = co_await redis.geopos(key, "NonExistent");
        EXPECT_TRUE(reply2.ok());
        EXPECT_EQ(reply2.result().size(), 1);
        EXPECT_FALSE(reply2.result()[0].has_value());

        // Test with empty key
        auto reply3 = co_await redis.georadius("NonExistentKey", 13.361389, 38.115556, 200, qb::redis::GeoUnit::KM);
        EXPECT_TRUE(reply3.ok());
        EXPECT_TRUE(reply3.result().empty());

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

TEST_P(GeoProtocolModesTest, GEOADD_GEOPOS) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k     = protocol_key("geo");
        auto add_r = co_await redis.geoadd(k, 13.361389, 38.115556, "Palermo");
        EXPECT_TRUE(add_r.ok()) << add_r.error();
        if (add_r.ok())
            EXPECT_EQ(add_r.result(), 1);
        auto pos_r = co_await redis.geopos(k, "Palermo");
        EXPECT_TRUE(pos_r.ok()) << pos_r.error();
        if (pos_r.ok() && !pos_r.result().empty() && pos_r.result()[0]) {
            EXPECT_NEAR(pos_r.result()[0]->longitude, 13.36, 0.01);
            EXPECT_NEAR(pos_r.result()[0]->latitude, 38.11, 0.01);
        }
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(GeoProtocolModesTest, GEOHASH_STRING) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k = protocol_key("geo");
        (void) co_await redis.geoadd(k, 13.361389, 38.115556, "Palermo");
        auto r = co_await redis.geohash(k, "Palermo");
        EXPECT_TRUE(r.ok()) << r.error();
        if (r.ok() && !r.result().empty()) {
            auto const &first = r.result().front();
            EXPECT_TRUE(first.has_value() && !first->empty());
        }
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(GeoProtocolModesTest, GEODIST_DOUBLE) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k = protocol_key("geodist");
        (void) co_await redis.geoadd(k, 13.361389, 38.115556, "Palermo", 15.087269, 37.502669, "Catania");
        auto r = co_await redis.geodist(k, "Palermo", "Catania", qb::redis::GeoUnit::KM);
        EXPECT_TRUE(r.ok()) << r.error();
        if (r.ok() && r.result())
            EXPECT_GT(*r.result(), 0.0);
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

// Test async GEOADD
// Test async GEODIST