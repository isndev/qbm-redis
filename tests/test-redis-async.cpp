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

#include <gtest/gtest.h>
#include <qb/io/async.h>
#include <thread>
#include "redis/redis.h"

using namespace qb::io;

#define REDIS_URI \
    { "tcp://10.3.3.3:6379" }

auto check_ok = [](auto &&reply) {
    EXPECT_TRUE(reply.ok);
};
auto check_nok = [](auto &&reply) {
    EXPECT_FALSE(reply.ok);
};

#define MESSAGE "hello"
class TestRedisPubSub : public qb::redis::tcp::consumer<TestRedisPubSub> {
    std::vector<std::string> topics;
    std::vector<std::string> ptopics;
    std::string publish;
    std::string message;

public:
    TestRedisPubSub(
        const qb::io::uri &uri, std::vector<std::string> topics, std::vector<std::string> ptopics)
        : qb::redis::tcp::consumer<TestRedisPubSub>(uri)
        , topics(topics)
        , ptopics(ptopics) {}

    bool
    connect() {
        auto ret = static_cast<qb::redis::tcp::consumer<TestRedisPubSub> &>(*this).connect();
        for (auto topic : topics)
            ret &= bool(subscribe(topic).channel);
        for (auto topic : ptopics)
            ret &= bool(psubscribe(topic).channel);
        return ret;
    }

    std::size_t counter{0};
    void
    on(qb::redis::reply::message &&msg) {
        EXPECT_EQ(msg.message, MESSAGE);
        ++counter;
    }
};

TEST(Redis, ASYNC_CONNECT) {
    async::init();
    qb::redis::tcp::client redis{REDIS_URI};
    auto status = true;
    redis.connect([&status](bool connected){
        EXPECT_TRUE(connected);
        status = false;
    }, 3);

    while (status)
        qb::io::async::run(EVRUN_ONCE);
    EXPECT_TRUE(redis.flushall());
}

TEST(Redis, ASYNC_COMMANDS_PUBSUB) {
    async::init();

    qb::redis::tcp::client publisher{REDIS_URI};
    TestRedisPubSub consumer{REDIS_URI, {"topic::a*"}, {"topic::a*", "topic::b*"}};

    if (!publisher.connect() || !consumer.connect())
        throw std::runtime_error("not connected");

    EXPECT_TRUE(publisher.publish("topic::a*", MESSAGE) != 0);
    EXPECT_TRUE(publisher.publish("topic::aa", MESSAGE) != 0);
    EXPECT_TRUE(publisher.publish("topic::babe", MESSAGE) != 0);
    EXPECT_TRUE(publisher.publish("topic::c", MESSAGE) == 0);

    publisher.await();
    EXPECT_EQ(consumer.counter, 4);
}

TEST(Redis, ASYNC_COMMANDS_PUBSUB_CALLBACK) {
    async::init();
    std::size_t counter{0};
    auto status = true;
    qb::redis::tcp::cb_consumer consumer{REDIS_URI, [&counter](auto &&msg){
                                       EXPECT_EQ(msg.message, MESSAGE);
                                       ++counter;
                                   }};
    consumer.on_disconnected([&status](auto const &ev) {
        EXPECT_TRUE(ev.reason == 1);
        status = false;
    });
    qb::redis::tcp::client publisher{REDIS_URI};

    if (!publisher.connect() || !consumer.connect())
        throw std::runtime_error("not connected");

    EXPECT_TRUE(consumer.subscribe("topic::a*").num);
    EXPECT_TRUE(consumer.psubscribe("topic::a*").num);
    EXPECT_TRUE(consumer.psubscribe("topic::b*").num);
    EXPECT_TRUE(publisher.publish("topic::a*", MESSAGE) != 0);
    EXPECT_TRUE(publisher.publish("topic::aa", MESSAGE) != 0);
    EXPECT_TRUE(publisher.publish("topic::babe", MESSAGE) != 0);
    EXPECT_TRUE(publisher.publish("topic::c", MESSAGE) == 0);

    publisher.await();
    EXPECT_EQ(counter, 4);
    consumer.disconnect(1);
    while (status)
        qb::io::async::run(EVRUN_ONCE);
}

TEST(Redis, ASYNC_COMMANDS_VOID) {
    async::init();
    qb::redis::tcp::client redis{REDIS_URI};

    if (!redis.connect())
        throw std::runtime_error("not connected");

    redis
        //.auth(check_nok, "hello")
        //.auth(check_nok, "user", "password")
        .select(check_ok, 1)
        .swapdb(check_ok, 1, 2)
        .flushdb(check_ok)
        .flushall(check_ok)
        .flushdb(check_ok, true)
        .flushall(check_ok, true)
        .ping([](auto &&reply) {
            EXPECT_EQ(reply.result, "PONG");
        })
        .ping(
            [](auto &&reply) {
                EXPECT_EQ(reply.result, "MY PONG");
            },
            "MY PONG")
        .echo(
            [](auto &&reply) {
                EXPECT_EQ(reply.result, "end");
            },
            "end")
        .await();
}

TEST(Redis, ASYNC_COMMANDS_SET_GET) {
    async::init();
    qb::redis::tcp::client redis{REDIS_URI};

    if (!redis.connect())
        throw std::runtime_error("not connected");
    auto res = redis.randomkey();
    redis.flushall(check_ok)
        .set(
            [](auto &&reply) {
                EXPECT_TRUE(reply.ok);
            },
            "key1",
            "value1",
            std::chrono::milliseconds{100000})
        .setex(check_ok, "key1", 10, "value2")
        .setnx(check_nok, "key1", "value1")
        .getset(
            [](auto &&reply) {
                EXPECT_EQ(reply.result.value(), "value2");
            },
            "key1",
            "value1")
        .mset(
            [](auto &&reply) {
                EXPECT_TRUE(reply.ok);
            },
            "key2",
            "value2",
            std::vector<std::string>{"key3", "value3"},
            "key4",
            "value4")

        .get(
            [](auto &&reply) {
                EXPECT_TRUE(reply.ok);
                EXPECT_EQ(reply.result.value(), "value1");
            },
            "key1")
        .mget(
            [](auto &&reply) {
                EXPECT_TRUE(reply.ok);
                EXPECT_TRUE(reply.result.size() == 3);
                EXPECT_EQ(reply.result[0].value(), "value2");
                EXPECT_EQ(reply.result[1].value(), "value3");
                EXPECT_EQ(reply.result[2].value(), "value4");
            },
            "key2",
            std::vector<std::string>{"key3", "key4"})

        .setrange(check_ok, "key4", 1, "ALUE4")
        .getrange(
            [](auto &&reply) {
                EXPECT_TRUE(reply.ok);
                EXPECT_EQ(reply.result, "vALUE4");
            },
            "key4",
            0,
            -1)
        .psetex(check_ok, "key4", 1200000, "value4")
        .del(
            [](auto &&reply) {
                EXPECT_EQ(reply.result, 2);
            },
            "key3",
            std::vector<std::string>{"key4"})
        .keys([](auto &&reply) {
            EXPECT_EQ(reply.result.size(), 2);
        })
        .msetnx(check_ok, "key3", "value3", std::make_pair("key4", "value4"))
        .exists(
            [](auto &&reply) {
                EXPECT_EQ(reply.result, 2);
            },
            "key3",
            std::vector<std::string>{"key4"})

        .rename(check_ok, "key4", "KEY4")
        .renamenx(check_nok, "KEY4", "key1")
        .scan(
            [](auto &&reply) {
                EXPECT_EQ(reply.result.items.size(), 3);
            },
            0,
            "key*")
        .await();
}

TEST(Redis, ASYNC_COMMANDS_NUMERIC) {
    async::init();
    qb::redis::tcp::client redis{REDIS_URI};

    if (!redis.connect())
        throw std::runtime_error("not connected");

    redis.flushall(check_ok)
        .set(check_ok, "key", "0")
        .incr(check_ok, "key")
        .decr(check_ok, "key")
        .incrby(check_ok, "key", 3)
        .decrby(check_ok, "key", 3)
        .incrby(check_ok, "key", -3)
        .decrby(check_ok, "key", -3)
        .incrbyfloat(
            [](auto &&reply) {
                EXPECT_EQ(reply.result, 1.5);
            },
            "key",
            1.5)
        .await();
}

TEST(Redis, ASYNC_COMMANDS_BITS) {
    async::init();
    qb::redis::tcp::client redis{REDIS_URI};

    if (!redis.connect())
        throw std::runtime_error("not connected");

    const auto key = "key";
    redis.flushall(check_ok)
        .set(check_ok, key, "")
        .bitcount(
            [](auto &&reply) {
                EXPECT_EQ(reply.result, 0);
            },
            key)
        .getbit(
            [](auto &&reply) {
                EXPECT_EQ(reply.result, 0);
            },
            key,
            5)
        .setbit(check_ok, key, 1, 1)
        .setbit(check_ok, key, 2, 1)
        .setbit(check_ok, key, 3, 1)
        .setbit(check_ok, key, 7, 1)
        .setbit(check_ok, key, 9, 1)
        .setbit(check_ok, key, 10, 1)
        .setbit(check_ok, key, 14, 1)
        .get(
            [](auto &&reply) {
                EXPECT_TRUE(reply.result);
                EXPECT_EQ(reply.result.value(), "qb");
            },
            key)
        .bitpos(
            [](auto &&reply) {
                EXPECT_EQ(reply.result, 1);
            },
            key,
            1,
            0,
            -1)
        .setbit(check_ok, key, 0, std::bitset<8>{"01000010"})
        .get(
            [](auto &&reply) {
                EXPECT_TRUE(reply.result);
                EXPECT_EQ(reply.result.value(), "Bb");
            },
            key)
        .bitop(check_ok, qb::redis::BitOp::XOR, "xor", key, key)
        .bitcount(
            [](auto &&reply) {
                EXPECT_EQ(reply.result, 0);
            },
            "xor")
        .await();
}

TEST(Redis, ASYNC_COMMANDS_STR) {
    async::init();
    qb::redis::tcp::client redis{REDIS_URI};

    if (!redis.connect())
        throw std::runtime_error("not connected");

    std::string key = "key";
    std::string val = "value";
    redis.flushall(check_ok)
        .set(check_ok, key, val)
        .append(check_ok, key, val)
        .append(check_ok, key, val)
        .append(check_ok, key, {})
        .strlen(
            [&val](auto &&reply) {
                EXPECT_EQ(reply.result, val.size() * 3);
            },
            key)
        .del(check_ok, key)
        .append(check_ok, key, {})
        .del(check_ok, key)
        .getrange(
            [](auto &&reply) {
                EXPECT_TRUE(reply.result.empty());
            },
            key,
            0,
            2)
        .set(check_ok, key, val)
        .getrange(
            [&val](auto &&reply) {
                EXPECT_EQ(reply.result, val.substr(1, 2));
            },
            key,
            1,
            2)
        .setrange(check_ok, key, val.size(), val)
        .getrange(
            [&val](auto reply) {
                EXPECT_EQ(reply.result, val + val);
            },
            key,
            0,
            -1)
        .await();
}

TEST(Redis, ASYNC_COMMANDS_GEO) {
    async::init();
    qb::redis::tcp::client redis{REDIS_URI};

    if (!redis.connect())
        throw std::runtime_error("not connected");

    redis.flushall(check_ok)
        .geoadd(
            [](auto &&reply) {
                EXPECT_EQ(reply.result, 2);
            },
            "Sicily",
            13.361389,
            38.115556,
            "Palermo",
            std::make_tuple(15.087269, 37.502669, "Catania"))
        .geodist(check_ok, "Sicily", "Palermo", "Catania", qb::redis::GeoUnit::M)
        .geopos(
            [](auto &&reply) {
                EXPECT_EQ(reply.result.size(), 3);
                EXPECT_TRUE(reply.result[0]);
                EXPECT_TRUE(reply.result[1]);
                EXPECT_FALSE(reply.result[2]);
            },
            "Sicily",
            "Palermo",
            "Catania",
            "not_exist")
        .geohash(
            [](auto &&reply) {
                EXPECT_TRUE(reply.result.size() > 0);
                EXPECT_EQ(reply.result.size(), 3);
                EXPECT_EQ(reply.result[0], "sqc8b49rny0");
                EXPECT_EQ(reply.result[1], "sqdtr74hyu0");
                EXPECT_FALSE(reply.result[2]);
            },
            "Sicily",
            "Palermo",
            "Catania",
            "not_exist")
        .await();
}

TEST(Redis, ASYNC_COMMANDS_SET) {
    async::init();
    qb::redis::tcp::client redis{REDIS_URI};

    if (!redis.connect())
        throw std::runtime_error("not connected");

    const auto key = "key";
    redis
        .flushall(check_ok)

        .await();
}
