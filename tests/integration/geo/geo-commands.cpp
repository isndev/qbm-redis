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

/**
 * @file integration/geo/geo-commands.cpp
 * @brief Live RESP2/RESP3 integration tests for the qbm-redis geospatial command mixin.
 *
 * Restructured from `test-geo-commands.cpp`:
 *   - the 3 legacy smoke dups (GEOADD_GEOPOS, GEOHASH_STRING, GEODIST_DOUBLE) and the 2 dead
 *     trailing "Test async …" comment stubs are deleted;
 *   - distance is now pinned to the canonical Palermo↔Catania ≈ 166.27 km fixture (the value
 *     Redis itself documents) instead of a vacuous `> 0`;
 *   - the COUNT-1 option asserts an EXACT `size() == 1`, not `<= 1` (which passed on an empty
 *     reply);
 *   - busy-spins → shared `run_coro_test_until` watchdog.
 *
 * NOTE: the typed wrapper for GEORADIUS / GEOSEARCH returns only the member names
 * (`std::vector<std::string>`); it drops the WITHDIST / WITHCOORD payloads. The known
 * distance / coordinate facts are therefore asserted through GEODIST and GEOPOS (which DO
 * decode them) rather than by parsing the dropped radius-search payloads.
 */

#include <algorithm>
#include <gtest/gtest.h>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include "../../shared/redis_integration_fixture.h"
#include <qbm/redis/redis.h>

using namespace qb::io;
using namespace qb::redis::test;

namespace {
// Canonical Sicilian fixture (the coordinates from the Redis GEO docs).
constexpr double kPalermoLon = 13.361389;
constexpr double kPalermoLat = 38.115556;
constexpr double kCataniaLon = 15.087269;
constexpr double kCataniaLat = 37.502669;
// Redis documents GEODIST Palermo Catania km ≈ 166.2742.
constexpr double kPalermoCataniaKm = 166.2742;
} // namespace

class GeoProtocolModesTest : public ProtocolModesTestBase {};

INSTANTIATE_PROTOCOL_MODES(GeoProtocolModesTest);

// GEOADD: returns the count of NEWLY added members.
TEST_P(GeoProtocolModesTest, GEOADD) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("geoadd");

        auto reply1 = co_await redis.geoadd(key, kPalermoLon, kPalermoLat, "Palermo");
        EXPECT_TRUE(reply1.ok()) << reply1.error();
        EXPECT_EQ(reply1.result(), 1);

        auto reply2 = co_await redis.geoadd(key, kCataniaLon, kCataniaLat, "Catania", 13.583333, 37.316667, "Agrigento");
        EXPECT_TRUE(reply2.ok()) << reply2.error();
        EXPECT_EQ(reply2.result(), 2);

        // Re-adding an existing member updates but adds nothing new → 0.
        auto reply3 = co_await redis.geoadd(key, kPalermoLon, kPalermoLat, "Palermo");
        EXPECT_TRUE(reply3.ok()) << reply3.error();
        EXPECT_EQ(reply3.result(), 0);

        CO_IGNORE(co_await redis.del(key));
        completed = true;
    });
    run_coro_test_until(completed);
}

// GEODIST: the Palermo↔Catania distance matches the documented value in m and km.
TEST_P(GeoProtocolModesTest, GEODIST) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("geodist");

        EXPECT_TRUE((co_await redis.geoadd(key, kPalermoLon, kPalermoLat, "Palermo", kCataniaLon, kCataniaLat, "Catania")).ok());

        // Meters (default unit) ≈ 166274 m.
        auto reply1 = co_await redis.geodist(key, "Palermo", "Catania");
        EXPECT_TRUE(reply1.ok()) << reply1.error();
        if (!(reply1.result().has_value())) {
            ADD_FAILURE() << "precondition failed: reply1.result().has_value()";
            co_return;
        }
        EXPECT_NEAR(*reply1.result(), kPalermoCataniaKm * 1000.0, 50.0);

        // Kilometers ≈ 166.27 km.
        auto reply2 = co_await redis.geodist(key, "Palermo", "Catania", qb::redis::GeoUnit::KM);
        EXPECT_TRUE(reply2.ok()) << reply2.error();
        if (!(reply2.result().has_value())) {
            ADD_FAILURE() << "precondition failed: reply2.result().has_value()";
            co_return;
        }
        EXPECT_NEAR(*reply2.result(), kPalermoCataniaKm, 0.1);

        // Distance to a missing member is nil.
        auto reply3 = co_await redis.geodist(key, "Palermo", "Nowhere");
        EXPECT_TRUE(reply3.ok()) << reply3.error();
        EXPECT_FALSE(reply3.result().has_value());

        CO_IGNORE(co_await redis.del(key));
        completed = true;
    });
    run_coro_test_until(completed);
}

// GEOHASH: non-empty 11-char geohash for a known member; nil for a missing one.
TEST_P(GeoProtocolModesTest, GEOHASH) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("geohash");

        EXPECT_TRUE((co_await redis.geoadd(key, kPalermoLon, kPalermoLat, "Palermo")).ok());

        auto reply = co_await redis.geohash(key, "Palermo", "Missing");
        EXPECT_TRUE(reply.ok()) << reply.error();
        auto hashes = reply.result();
        if (!(hashes.size() == 2u)) {
            ADD_FAILURE() << "precondition failed: hashes.size() == 2u";
            co_return;
        }
        if (!(hashes[0].has_value())) {
            ADD_FAILURE() << "precondition failed: hashes[0].has_value()";
            co_return;
        }
        EXPECT_EQ(hashes[0]->size(), 11u); // Redis geohash strings are 11 chars
        // Palermo's documented geohash starts with "sqc8b49rny".
        EXPECT_EQ(hashes[0]->rfind("sqc8", 0), 0u);
        EXPECT_FALSE(hashes[1].has_value());

        CO_IGNORE(co_await redis.del(key));
        completed = true;
    });
    run_coro_test_until(completed);
}

// GEOPOS: round-trips the seeded coordinates; nil for a missing member.
TEST_P(GeoProtocolModesTest, GEOPOS) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("geopos");

        EXPECT_TRUE((co_await redis.geoadd(key, kPalermoLon, kPalermoLat, "Palermo")).ok());

        auto reply = co_await redis.geopos(key, "Palermo", "Missing");
        EXPECT_TRUE(reply.ok()) << reply.error();
        auto positions = reply.result();
        if (!(positions.size() == 2u)) {
            ADD_FAILURE() << "precondition failed: positions.size() == 2u";
            co_return;
        }
        if (!(positions[0].has_value())) {
            ADD_FAILURE() << "precondition failed: positions[0].has_value()";
            co_return;
        }
        // GEOADD lossily stores 52-bit geohash → ~1e-5 precision on readback.
        EXPECT_NEAR(positions[0]->longitude, kPalermoLon, 1e-4);
        EXPECT_NEAR(positions[0]->latitude, kPalermoLat, 1e-4);
        EXPECT_FALSE(positions[1].has_value());

        CO_IGNORE(co_await redis.del(key));
        completed = true;
    });
    run_coro_test_until(completed);
}

// GEORADIUS: radius from Palermo returns the exact expected member set.
TEST_P(GeoProtocolModesTest, GEORADIUS) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("georadius");

        EXPECT_TRUE((co_await redis.geoadd(key, kPalermoLon, kPalermoLat, "Palermo", kCataniaLon, kCataniaLat, "Catania")).ok());

        // 200 km from Palermo includes both (they are ~166 km apart).
        auto reply = co_await redis.georadius(key, kPalermoLon, kPalermoLat, 200, qb::redis::GeoUnit::KM);
        EXPECT_TRUE(reply.ok()) << reply.error();
        auto results = reply.result();
        if (!(results.size() == 2u)) {
            ADD_FAILURE() << "precondition failed: results.size() == 2u";
            co_return;
        }
        EXPECT_NE(std::find(results.begin(), results.end(), "Palermo"), results.end());
        EXPECT_NE(std::find(results.begin(), results.end(), "Catania"), results.end());

        // A tight 50 km radius from Palermo excludes Catania → only Palermo.
        auto tight = co_await redis.georadius(key, kPalermoLon, kPalermoLat, 50, qb::redis::GeoUnit::KM);
        EXPECT_TRUE(tight.ok()) << tight.error();
        if (!(tight.result().size() == 1u)) {
            ADD_FAILURE() << "precondition failed: tight.result().size() == 1u";
            co_return;
        }
        EXPECT_EQ(tight.result()[0], "Palermo");

        // WITHDIST/WITHCOORD options: wrapper still returns names only, but the command
        // must succeed and yield the same member set.
        // NOTE: option vectors hoisted into named locals — GCC 14.2 coroutine ICE workaround.
        std::vector<std::string> opts{"WITHDIST", "WITHCOORD"};
        auto                     reply2 = co_await redis.georadius(key, kPalermoLon, kPalermoLat, 200, qb::redis::GeoUnit::KM, opts);
        EXPECT_TRUE(reply2.ok()) << reply2.error();
        EXPECT_EQ(reply2.result().size(), 2u);

        CO_IGNORE(co_await redis.del(key));
        completed = true;
    });
    run_coro_test_until(completed);
}

// GEORADIUSBYMEMBER: radius centred on an existing member.
TEST_P(GeoProtocolModesTest, GEORADIUSBYMEMBER) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("georadiusbymember");

        EXPECT_TRUE((co_await redis.geoadd(key, kPalermoLon, kPalermoLat, "Palermo", kCataniaLon, kCataniaLat, "Catania")).ok());

        auto reply = co_await redis.georadiusbymember(key, "Palermo", 200, qb::redis::GeoUnit::KM);
        EXPECT_TRUE(reply.ok()) << reply.error();
        if (!(reply.result().size() == 2u)) {
            ADD_FAILURE() << "precondition failed: reply.result().size() == 2u";
            co_return;
        }
        EXPECT_NE(std::find(reply.result().begin(), reply.result().end(), "Palermo"), reply.result().end());

        // The member centre itself is within any non-negative radius.
        auto self = co_await redis.georadiusbymember(key, "Palermo", 1, qb::redis::GeoUnit::M);
        EXPECT_TRUE(self.ok()) << self.error();
        if (!(self.result().size() == 1u)) {
            ADD_FAILURE() << "precondition failed: self.result().size() == 1u";
            co_return;
        }
        EXPECT_EQ(self.result()[0], "Palermo");

        CO_IGNORE(co_await redis.del(key));
        completed = true;
    });
    run_coro_test_until(completed);
}

// GEOSEARCH (FROMMEMBER + BYRADIUS form supported by the wrapper).
TEST_P(GeoProtocolModesTest, GEOSEARCH) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("geosearch");

        EXPECT_TRUE((co_await redis.geoadd(key, kPalermoLon, kPalermoLat, "Palermo", kCataniaLon, kCataniaLat, "Catania")).ok());

        auto reply = co_await redis.geosearch(key, "Palermo", 200, qb::redis::GeoUnit::KM);
        EXPECT_TRUE(reply.ok()) << reply.error();
        if (!(reply.result().size() == 2u)) {
            ADD_FAILURE() << "precondition failed: reply.result().size() == 2u";
            co_return;
        }
        EXPECT_NE(std::find(reply.result().begin(), reply.result().end(), "Palermo"), reply.result().end());
        EXPECT_NE(std::find(reply.result().begin(), reply.result().end(), "Catania"), reply.result().end());

        // Tight radius from Palermo excludes Catania → exactly Palermo.
        auto tight = co_await redis.geosearch(key, "Palermo", 50, qb::redis::GeoUnit::KM);
        EXPECT_TRUE(tight.ok()) << tight.error();
        if (!(tight.result().size() == 1u)) {
            ADD_FAILURE() << "precondition failed: tight.result().size() == 1u";
            co_return;
        }
        EXPECT_EQ(tight.result()[0], "Palermo");

        CO_IGNORE(co_await redis.del(key));
        completed = true;
    });
    run_coro_test_until(completed);
}

// GEORADIUS options: COUNT and SORT produce a deterministic, EXACTLY-sized result.
TEST_P(GeoProtocolModesTest, GEORADIUS_OPTIONS) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("georadius_options");

        EXPECT_TRUE((co_await redis.geoadd(key, kPalermoLon, kPalermoLat, "Palermo", kCataniaLon, kCataniaLat, "Catania")).ok());

        // COUNT 1 → EXACTLY one result (there ARE two candidates in range).
        std::vector<std::string> opts_count{"COUNT", "1"};
        auto                     reply_count = co_await redis.georadius(key, kPalermoLon, kPalermoLat, 200, qb::redis::GeoUnit::KM, opts_count);
        EXPECT_TRUE(reply_count.ok()) << reply_count.error();
        EXPECT_EQ(reply_count.result().size(), 1u);

        // ASC sort from Palermo's coordinates → nearest first is Palermo.
        std::vector<std::string> opts_asc{"ASC"};
        auto                     reply_asc = co_await redis.georadius(key, kPalermoLon, kPalermoLat, 200, qb::redis::GeoUnit::KM, opts_asc);
        EXPECT_TRUE(reply_asc.ok()) << reply_asc.error();
        if (!(reply_asc.result().size() == 2u)) {
            ADD_FAILURE() << "precondition failed: reply_asc.result().size() == 2u";
            co_return;
        }
        EXPECT_EQ(reply_asc.result()[0], "Palermo");
        EXPECT_EQ(reply_asc.result()[1], "Catania");

        // COUNT 1 + ASC → the single nearest = Palermo.
        std::vector<std::string> opts_count_asc{"COUNT", "1", "ASC"};
        auto reply_ca = co_await redis.georadius(key, kPalermoLon, kPalermoLat, 200, qb::redis::GeoUnit::KM, opts_count_asc);
        EXPECT_TRUE(reply_ca.ok()) << reply_ca.error();
        if (!(reply_ca.result().size() == 1u)) {
            ADD_FAILURE() << "precondition failed: reply_ca.result().size() == 1u";
            co_return;
        }
        EXPECT_EQ(reply_ca.result()[0], "Palermo");

        CO_IGNORE(co_await redis.del(key));
        completed = true;
    });
    run_coro_test_until(completed);
}

// Edge cases: missing members, missing key.
TEST_P(GeoProtocolModesTest, EDGE_CASES) {
    bool completed = false;
    qb::io::async::coro_scheduler().spawn([this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("geo_edge");

        auto reply1 = co_await redis.geodist(key, "NonExistent1", "NonExistent2");
        EXPECT_TRUE(reply1.ok()) << reply1.error();
        EXPECT_FALSE(reply1.result().has_value());

        auto reply2 = co_await redis.geopos(key, "NonExistent");
        EXPECT_TRUE(reply2.ok()) << reply2.error();
        if (!(reply2.result().size() == 1u)) {
            ADD_FAILURE() << "precondition failed: reply2.result().size() == 1u";
            co_return;
        }
        EXPECT_FALSE(reply2.result()[0].has_value());

        auto reply3 = co_await redis.georadius("NonExistentKey", kPalermoLon, kPalermoLat, 200, qb::redis::GeoUnit::KM);
        EXPECT_TRUE(reply3.ok()) << reply3.error();
        EXPECT_TRUE(reply3.result().empty());

        completed = true;
    });
    run_coro_test_until(completed);
}
