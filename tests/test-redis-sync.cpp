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
#define REDIS_URI \
    { "tcp://10.3.3.3:6379" }

using namespace qb::io;
using namespace std::chrono;

inline std::string
key_prefix(const std::string &key = "") {
    static std::string KEY_PREFIX = "qb::redis::test";
    if (!key.empty()) {
        KEY_PREFIX = key;
    }

    return KEY_PREFIX;
}

inline std::string
test_key(const std::string &k) {
    // Key prefix with hash tag,
    // so that we can call multiple-key commands on RedisCluster.
    return "{" + key_prefix() + "}::" + k;
}

TEST(Redis, SYNC_COMMANDS_CONNECTION) {
    async::init();
    qb::redis::tcp::client redis{REDIS_URI};

    if (!redis.connect() || !redis.flushall())
        throw std::runtime_error("not connected");

    auto message = std::string("hello");
    EXPECT_TRUE(redis.echo(message) == message);
    EXPECT_TRUE(redis.ping() == "PONG");
    EXPECT_TRUE(redis.ping(message) == message);
}

TEST(Redis, SYNC_COMMANDS_KEY) {
    async::init();
    qb::redis::tcp::client redis{REDIS_URI};

    if (!redis.connect() || !redis.flushall())
        throw std::runtime_error("not connected");
    auto key = test_key("key");
    auto dest = test_key("dest");
    auto new_key_name = test_key("new-key");
    auto not_exist_key = test_key("not-exist");

    EXPECT_TRUE(redis.exists(key) == 0);

    auto val = std::string("val");
    redis.set(key, val);

    EXPECT_TRUE(redis.exists(key, not_exist_key) == 1);

    auto new_val = redis.dump(key);
    EXPECT_TRUE(bool(new_val));

    redis.restore(dest, *new_val, std::chrono::seconds(1000));
    new_val = redis.get(dest);

    EXPECT_TRUE(bool(new_val) && *new_val == val);

    redis.rename(dest, new_key_name);

    EXPECT_TRUE(!redis.rename(not_exist_key, new_key_name));
    EXPECT_TRUE(redis.renamenx(new_key_name, dest));
    EXPECT_TRUE(redis.touch(not_exist_key) == 0);
    EXPECT_TRUE(redis.touch(key, dest, new_key_name) == 2);
    EXPECT_TRUE(redis.type(key) == "string");
    EXPECT_TRUE(redis.randomkey());
    EXPECT_TRUE(redis.del(new_key_name, dest) == 1);
    EXPECT_TRUE(redis.unlink(new_key_name, key) == 1);

    std::string key_pattern = "!@#$%^&()_+alseufoawhnlkszd";
    auto k1 = test_key(key_pattern + "k1");
    auto k2 = test_key(key_pattern + "k2");
    auto k3 = test_key(key_pattern + "k3");

    auto keys = {k1, k2, k3};

    redis.set(k1, "v");
    redis.set(k2, "v");
    redis.set(k3, "v");

    auto cursor = 0;
    qb::unordered_set<std::string> res;
    while (true) {
        auto scan = redis.scan(cursor, "*" + key_pattern + "*", 2);
        cursor = scan.cursor;
        std::move(scan.items.begin(), scan.items.end(), std::inserter(res, res.end()));
        if (cursor == 0) {
            break;
        }
    }
    EXPECT_TRUE(res == qb::unordered_set<std::string>(keys));
}

TEST(Redis, SYNC_COMMANDS_TTL) {
    async::init();
    qb::redis::tcp::client redis{REDIS_URI};

    if (!redis.connect() || !redis.flushall())
        throw std::runtime_error("not connected");
    auto key = test_key("ttl");
    redis.set(key, "val", seconds(100));
    auto ttl = redis.ttl(key);
    EXPECT_TRUE(ttl > 0 && ttl <= 100);
    EXPECT_TRUE(redis.persist(key));

    ttl = redis.ttl(key);
    EXPECT_TRUE(ttl == -1);
    EXPECT_TRUE(redis.expire(key, seconds(100)));

    auto tp = time_point_cast<seconds>(system_clock::now() + seconds(100));
    EXPECT_TRUE(redis.expireat(key, tp));

    ttl = redis.ttl(key);
    EXPECT_TRUE(ttl > 0);
    EXPECT_TRUE(redis.pexpire(key, milliseconds(100000)));

    auto pttl = redis.pttl(key);
    EXPECT_TRUE(pttl > 0 && pttl <= 100000);

    auto tp_milli = time_point_cast<milliseconds>(system_clock::now() + milliseconds(100000));
    EXPECT_TRUE(redis.pexpireat(key, tp_milli));
    pttl = redis.pttl(key);
    EXPECT_TRUE(pttl > 0);
}

TEST(Redis, SYNC_COMMANDS_HASH_BATCH) {
    async::init();
    qb::redis::tcp::client redis{REDIS_URI};

    if (!redis.connect() || !redis.flushall())
        throw std::runtime_error("not connected");

    auto key = test_key("hash");

    auto f1 = std::string("f1");
    auto v1 = std::string("v1");
    auto f2 = std::string("f2");
    auto v2 = std::string("v2");
    auto f3 = std::string("f3");

    redis.hmset(key, std::make_pair(f1, v1), std::make_pair(f2, v2));

    std::vector<std::string> fields = redis.hkeys(key);
    EXPECT_TRUE(fields.size() == 2);

    std::vector<std::string> vals = redis.hvals(key);
    EXPECT_TRUE(vals.size() == 2);

    qb::unordered_map<std::string, std::string> items = redis.hgetall(key);
    EXPECT_TRUE(items.size() == 2 && items[f1] == v1 && items[f2] == v2);

    std::vector<std::optional<std::string>> res = redis.hmget(key, f1, f2, f3);
    EXPECT_TRUE(
        res.size() == 3 && bool(res[0]) && *(res[0]) == v1 && bool(res[1]) && *(res[1]) == v2 && !res[2]);
}

TEST(Redis, SYNC_COMMANDS_HASH_NUMERIC) {
    async::init();
    qb::redis::tcp::client redis{REDIS_URI};

    if (!redis.connect() || !redis.flushall())
        throw std::runtime_error("not connected");

    auto key = test_key("numeric");
    auto field = "field";

    EXPECT_TRUE(redis.hincrby(key, field, 1) == 1);
    EXPECT_TRUE(redis.hincrby(key, field, -1) == 0);
    EXPECT_TRUE(redis.hincrbyfloat(key, field, 1.5) == 1.5);
}

TEST(Redis, SYNC_COMMANDS_HASH_SCAN) {
    async::init();
    qb::redis::tcp::client redis{REDIS_URI};

    if (!redis.connect() || !redis.flushall())
        throw std::runtime_error("not connected");

    auto key = test_key("hscan");

    auto items = qb::unordered_map<std::string, std::string>{
        std::make_pair("f1", "v1"),
        std::make_pair("f2", "v2"),
        std::make_pair("f3", "v3"),
    };

    redis.hmset(key, items);

    qb::unordered_map<std::string, std::string> item_map;
    auto cursor = 0;
    while (true) {
        auto scan = redis.hscan<decltype(item_map)>(key, cursor, "f*", 2);
        cursor = scan.cursor;
        item_map.merge(std::move(scan.items));
        if (cursor == 0) {
            break;
        }
    }

    EXPECT_TRUE(item_map == items);

    std::vector<std::pair<std::string, std::string>> item_vec;
    cursor = 0;
    while (true) {
        auto scan = redis.hscan<decltype(item_vec)>(key, cursor);
        cursor = scan.cursor;
        std::move(scan.items.begin(), scan.items.end(), std::back_inserter(item_vec));
        if (cursor == 0) {
            break;
        }
    }

    EXPECT_TRUE(item_vec.size() == items.size());
    for (const auto &ele : item_vec) {
        EXPECT_TRUE(items.find(ele.first) != items.end());
    }
}

TEST(Redis, SYNC_COMMANDS_LIST_LPOPPUSH) {
    async::init();
    qb::redis::tcp::client redis{REDIS_URI};

    if (!redis.connect() || !redis.flushall())
        throw std::runtime_error("not connected");

    auto key = test_key("lpoppush");

    auto item = redis.lpop(key);
    EXPECT_TRUE(!item);

    EXPECT_TRUE(redis.lpushx(key, "1") == 0);
    EXPECT_TRUE(redis.lpush(key, "1") == 1);
    EXPECT_TRUE(redis.lpushx(key, "2") == 2);
    EXPECT_TRUE(redis.lpush(key, "3", "4", "5") == 5);

    item = redis.lpop(key);
    EXPECT_TRUE(item && *item == "5");
}

TEST(Redis, SYNC_COMMANDS_LIST_RPOPPUSH) {
    async::init();
    qb::redis::tcp::client redis{REDIS_URI};

    if (!redis.connect() || !redis.flushall())
        throw std::runtime_error("not connected");

    auto key = test_key("lpoppush");

    auto item = redis.rpop(key);
    EXPECT_TRUE(!item);

    EXPECT_TRUE(redis.rpushx(key, "1") == 0);
    EXPECT_TRUE(redis.rpush(key, "1") == 1);
    EXPECT_TRUE(redis.rpushx(key, "2") == 2);
    EXPECT_TRUE(redis.rpush(key, "3", "4", "5") == 5);

    item = redis.rpop(key);
    EXPECT_TRUE(item && *item == "5");
}

TEST(Redis, SYNC_COMMANDS_LIST) {
    async::init();
    qb::redis::tcp::client redis{REDIS_URI};

    if (!redis.connect() || !redis.flushall())
        throw std::runtime_error("not connected");

    auto key = test_key("list");

    auto item = redis.lindex(key, 0);
    EXPECT_TRUE(!item);

    redis.lpush(key, "1", "2", "3", "4", "5");
    EXPECT_TRUE(redis.lrem(key, 0, "3") == 1);
    EXPECT_TRUE(redis.linsert(key, qb::redis::InsertPosition::BEFORE, "2", "3") == 5);
    EXPECT_TRUE(redis.llen(key) == 5);

    redis.lset(key, 0, "6");
    item = redis.lindex(key, 0);
    EXPECT_TRUE(item && *item == "6");

    redis.ltrim(key, 0, 2);
    std::vector<std::string> res = redis.lrange(key, 0, -1);
    EXPECT_TRUE(res == std::vector<std::string>({"6", "4", "3"}));
}

TEST(Redis, SYNC_COMMANDS_LIST_BLOCKING) {
    async::init();
    qb::redis::tcp::client redis{REDIS_URI};

    if (!redis.connect() || !redis.flushall())
        throw std::runtime_error("not connected");

    auto k1 = test_key("k1");
    auto k2 = test_key("k2");
    auto k3 = test_key("k3");

    auto keys = std::vector<std::string>{k1, k2, k3};

    std::string val("value");
    redis.lpush(k1, val);

    auto res = redis.blpop(0, k1, k2, k3);
    EXPECT_TRUE(res && *res == std::make_pair(k1, val));

    res = redis.brpop(std::chrono::seconds(1), keys);
    EXPECT_TRUE(!res);

    redis.lpush(k1, val);
    res = redis.blpop(0, k1);
    EXPECT_TRUE(res && *res == std::make_pair(k1, val));

    res = redis.blpop(std::chrono::seconds(1), k1);
    EXPECT_TRUE(!res);

    redis.lpush(k1, val);
    res = redis.brpop(0, k1);
    EXPECT_TRUE(res && *res == std::make_pair(k1, val));

    res = redis.brpop(std::chrono::seconds(1), k1);
    EXPECT_TRUE(!res);

    auto str = redis.brpoplpush(k2, k3, std::chrono::seconds(1));
    EXPECT_TRUE(!str);

    redis.lpush(k2, val);
    str = redis.brpoplpush(k2, k3);
    EXPECT_TRUE(str && *str == val);

    str = redis.rpoplpush(k3, k2);
    EXPECT_TRUE(str && *str == val);
}

TEST(Redis, SYNC_COMMANDS_HYPERLOG) {
    async::init();
    qb::redis::tcp::client redis{REDIS_URI};

    if (!redis.connect() || !redis.flushall())
        throw std::runtime_error("not connected");
    auto k1 = test_key("k1");
    auto k2 = test_key("k2");
    auto k3 = test_key("k3");

    redis.pfadd(k1, "a");
    auto members1 = std::vector{"b", "c", "d", "e", "f", "g"};
    redis.pfadd(k1, members1);

    auto cnt = redis.pfcount(k1);
    auto err = cnt * 1.0 / (1 + members1.size());
    EXPECT_TRUE(err < 1.02 && err > 0.98);

    auto members2 = std::vector{"a", "b", "c", "h", "i", "j", "k"};
    redis.pfadd(k2, members2);
    auto total = 1 + members1.size() + members2.size() - 3;

    cnt = redis.pfcount(k1, k2);
    err = cnt * 1.0 / total;
    EXPECT_TRUE(err < 1.02 && err > 0.98);

    redis.pfmerge(k3, k1, k2);
    cnt = redis.pfcount(k3);
    err = cnt * 1.0 / total;
    EXPECT_TRUE(err < 1.02 && err > 0.98);

    redis.pfmerge(k3, k1);
    EXPECT_TRUE(cnt == redis.pfcount(k3));
}

TEST(Redis, SYNC_COMMANDS_SET) {
    async::init();
    qb::redis::tcp::client redis{REDIS_URI};

    if (!redis.connect() || !redis.flushall())
        throw std::runtime_error("not connected");

    auto key = test_key("set");

    std::string m1("m1");
    std::string m2("m2");
    std::string m3("m3");

    EXPECT_TRUE(redis.sadd(key, m1) == 1);

    auto members = {m1, m2, m3};
    EXPECT_TRUE(redis.sadd(key, members) == 2);

    EXPECT_TRUE(redis.scard(key) == 3);

    EXPECT_TRUE(redis.sismember(key, m1));

    qb::unordered_set<std::string> res = redis.smembers(key);
    EXPECT_TRUE(res.find(m1) != res.end() && res.find(m2) != res.end() && res.find(m3) != res.end());

    auto ele = redis.srandmember(key);
    EXPECT_TRUE(bool(ele) && res.find(*ele) != res.end());

    std::vector<std::string> rand_members = redis.srandmember(key, 2);
    EXPECT_TRUE(rand_members.size() == 2);

    ele = redis.spop(key);
    EXPECT_TRUE(bool(ele) && res.find(*ele) != res.end());

    rand_members.clear();
    rand_members = redis.spop(key, 3);
    EXPECT_TRUE(rand_members.size() == 2);

    rand_members.clear();
    rand_members = redis.srandmember(key, 2);
    EXPECT_TRUE(rand_members.empty());

    rand_members = redis.spop(key, 2);
    EXPECT_TRUE(rand_members.empty());

    redis.sadd(key, members);
    EXPECT_TRUE(redis.srem(key, m1) == 1);
    EXPECT_TRUE(redis.srem(key, members) == 2);
    EXPECT_TRUE(redis.srem(key, members) == 0);
}

TEST(Redis, SYNC_COMMANDS_MULTISET) {
    async::init();
    qb::redis::tcp::client redis{REDIS_URI};

    if (!redis.connect() || !redis.flushall())
        throw std::runtime_error("not connected");

    auto k1 = test_key("s1");
    auto k2 = test_key("s2");
    auto k3 = test_key("s3");
    auto k4 = test_key("s4");
    auto k5 = test_key("s5");
    auto k6 = test_key("s6");

    redis.sadd(k1, "a", "c");
    redis.sadd(k2, "a", "b");
    std::vector<std::string> sdiff = redis.sdiff(k1, k1);
    EXPECT_TRUE(sdiff.empty());

    sdiff = redis.sdiff(k1, k2);
    EXPECT_TRUE(sdiff == std::vector<std::string>({"c"}));

    redis.sdiffstore(k3, k1, k2);
    sdiff.clear();
    auto members = redis.smembers(k3);
    std::move(members.begin(), members.end(), std::back_inserter(sdiff));
    EXPECT_TRUE(sdiff == std::vector<std::string>({"c"}));
    EXPECT_TRUE(redis.sdiffstore(k3, k1) == 2);
    EXPECT_TRUE(redis.sinterstore(k3, k1) == 2);
    EXPECT_TRUE(redis.sunionstore(k3, k1) == 2);

    std::vector<std::string> sinter = redis.sinter(k1, k2);
    EXPECT_TRUE(sinter == std::vector<std::string>({"a"}));

    redis.sinterstore(k4, k1, k2);
    sinter.clear();
    members = redis.smembers(k4);
    std::move(members.begin(), members.end(), std::back_inserter(sinter));
    EXPECT_TRUE(sinter == std::vector<std::string>({"a"}));

    auto u = redis.sunion(k1, k2);
    qb::unordered_set<std::string> sunion;
    std::move(u.begin(), u.end(), std::inserter(sunion, sunion.end()));
    EXPECT_TRUE(sunion == qb::unordered_set<std::string>({"a", "b", "c"}));

    redis.sunionstore(k5, k1, k2);
    sunion.clear();
    sunion = redis.smembers(k5);
    EXPECT_TRUE(sunion == qb::unordered_set<std::string>({"a", "b", "c"}));
    EXPECT_TRUE(redis.smove(k5, k6, "a"));
}

TEST(Redis, SYNC_COMMANDS_SCAN) {
    async::init();
    qb::redis::tcp::client redis{REDIS_URI};

    if (!redis.connect() || !redis.flushall())
        throw std::runtime_error("not connected");

    auto key = test_key("sscan");

    std::unordered_set<std::string> members = {"m1", "m2", "m3"};
    redis.sadd(key, members);

    std::unordered_set<std::string> res;
    long long cursor = 0;
    while (true) {
        auto scan = redis.sscan(key, cursor, "m*", 1);
        cursor = scan.cursor;
        std::move(scan.items.begin(), scan.items.end(), std::inserter(res, res.end()));
        if (cursor == 0) {
            break;
        }
    }
    EXPECT_TRUE(res == members);

    res.clear();
    cursor = 0;
    while (true) {
        auto scan = redis.sscan(key, cursor, "m*", 1);
        cursor = scan.cursor;
        std::move(scan.items.begin(), scan.items.end(), std::inserter(res, res.end()));
        if (cursor == 0) {
            break;
        }
    }
    EXPECT_TRUE(res == members);
}

TEST(Redis, SYNC_COMMANDS_STR) {
    async::init();
    qb::redis::tcp::client redis{REDIS_URI};

    if (!redis.connect() || !redis.flushall())
        throw std::runtime_error("not connected");

    auto key = test_key("str");
    std::string val("value");
    long long val_size = val.size();

    auto len1 = redis.append(key, val);
    EXPECT_TRUE(len1 == val_size);

    auto len2 = redis.append(key, val);
    EXPECT_TRUE(len2 == len1 + val_size);

    auto len3 = redis.append(key, {});
    EXPECT_TRUE(len3 == len2);

    auto len4 = redis.strlen(key);
    EXPECT_TRUE(len4 == len3);
    EXPECT_TRUE(redis.del(key) == 1);

    auto len5 = redis.append(key, {});
    EXPECT_TRUE(len5 == 0);

    redis.del(key);
    EXPECT_TRUE(redis.getrange(key, 0, 2) == "");

    redis.set(key, val);
    EXPECT_TRUE(redis.getrange(key, 1, 2) == val.substr(1, 2));

    long long new_size = val.size() * 2;
    EXPECT_TRUE(redis.setrange(key, val.size(), val) == new_size);
    EXPECT_TRUE(redis.getrange(key, 0, -1) == val + val);
}

TEST(Redis, SYNC_COMMANDS_BIT) {
    async::init();
    qb::redis::tcp::client redis{REDIS_URI};

    if (!redis.connect() || !redis.flushall())
        throw std::runtime_error("not connected");

    auto key = test_key("bit");

    EXPECT_TRUE(redis.bitcount(key) == 0);

    EXPECT_TRUE(redis.getbit(key, 5) == 0);

    EXPECT_TRUE(redis.setbit(key, 1, 1) == 0);
    EXPECT_TRUE(redis.setbit(key, 3, 1) == 0);
    EXPECT_TRUE(redis.setbit(key, 7, 1) == 0);
    EXPECT_TRUE(redis.setbit(key, 10, 1) == 0);
    EXPECT_TRUE(redis.setbit(key, 10, 0) == 1);
    EXPECT_TRUE(redis.setbit(key, 11, 1) == 0);
    EXPECT_TRUE(redis.setbit(key, 21, 1) == 0);

    // key -> 01010001, 00010000, 00000100

    EXPECT_TRUE(redis.getbit(key, 1) == 1);
    EXPECT_TRUE(redis.getbit(key, 2) == 0);
    EXPECT_TRUE(redis.getbit(key, 7) == 1);
    EXPECT_TRUE(redis.getbit(key, 10) == 0);
    EXPECT_TRUE(redis.getbit(key, 100) == 0);

    EXPECT_TRUE(redis.bitcount(key) == 5);
    EXPECT_TRUE(redis.bitcount(key, 0, 0) == 3);
    EXPECT_TRUE(redis.bitcount(key, 0, 1) == 4);
    EXPECT_TRUE(redis.bitcount(key, -2, -1) == 2);

    EXPECT_TRUE(redis.bitpos(key, 1) == 1);
    EXPECT_TRUE(redis.bitpos(key, 0) == 0);
    EXPECT_TRUE(redis.bitpos(key, 1, 1, 1) == 11);
    EXPECT_TRUE(redis.bitpos(key, 0, 1, 1) == 8);
    EXPECT_TRUE(redis.bitpos(key, 1, -1, -1) == 21);
    EXPECT_TRUE(redis.bitpos(key, 0, -1, -1) == 16);

    auto dest_key = test_key("bitop_dest");
    auto src_key1 = test_key("bitop_src1");
    auto src_key2 = test_key("bitop_src2");

    // src_key1 -> 00010000
    redis.setbit(src_key1, 3, 1);

    // src_key2 -> 00000000, 00001000
    redis.setbit(src_key2, 12, 1);

    EXPECT_TRUE(redis.bitop(qb::redis::BitOp::AND, dest_key, src_key1, src_key2) == 2);
    // dest_key -> 00000000, 00000000
    auto v = redis.get(dest_key);
    EXPECT_TRUE(v && *v == std::string(2, 0));
    EXPECT_TRUE(redis.bitop(qb::redis::BitOp::NOT, dest_key, src_key1) == 1);

    // dest_key -> 11101111
    v = redis.get(dest_key);
    EXPECT_TRUE(v && *v == std::string(1, '\xEF'));
}

TEST(Redis, SYNC_COMMANDS_NUMERIC) {
    async::init();
    qb::redis::tcp::client redis{REDIS_URI};

    if (!redis.connect() || !redis.flushall())
        throw std::runtime_error("not connected");

    auto key = test_key("numeric");

    EXPECT_TRUE(redis.incr(key) == 1);
    EXPECT_TRUE(redis.decr(key) == 0);
    EXPECT_TRUE(redis.incrby(key, 3) == 3);
    EXPECT_TRUE(redis.decrby(key, 3) == 0);
    EXPECT_TRUE(redis.incrby(key, -3) == -3);
    EXPECT_TRUE(redis.decrby(key, -3) == 0);
    EXPECT_TRUE(redis.incrbyfloat(key, 1.5) == 1.5);
}

TEST(Redis, SYNC_COMMANDS_GETSET) {
    async::init();
    qb::redis::tcp::client redis{REDIS_URI};

    if (!redis.connect() || !redis.flushall())
        throw std::runtime_error("not connected");

    auto key = test_key("getset");
    auto non_exist_key = test_key("non-existent");

    std::string val("value");
    EXPECT_TRUE(redis.set(key, val));

    auto v = redis.get(key);
    EXPECT_TRUE(v && *v == val);

    v = redis.getset(key, val + val);
    EXPECT_TRUE(v && *v == val);

    EXPECT_TRUE(!redis.set(key, val, std::chrono::milliseconds(0), qb::redis::UpdateType::NOT_EXIST));
    EXPECT_TRUE(!redis.set(non_exist_key, val, std::chrono::milliseconds(0), qb::redis::UpdateType::EXIST));

    EXPECT_TRUE(!redis.setnx(key, val));
    EXPECT_TRUE(redis.setnx(non_exist_key, val));

    auto ttl = std::chrono::seconds(10);

    redis.set(key, val, ttl);
    EXPECT_TRUE(redis.ttl(key) <= ttl.count());

    redis.setex(key, ttl, val);
    EXPECT_TRUE(redis.ttl(key) <= ttl.count());

    auto pttl = std::chrono::milliseconds(10000);

    redis.psetex(key, ttl, val);
    EXPECT_TRUE(redis.pttl(key) <= pttl.count());
}

TEST(Redis, SYNC_COMMANDS_MGETSET) {
    async::init();
    qb::redis::tcp::client redis{REDIS_URI};

    if (!redis.connect() || !redis.flushall())
        throw std::runtime_error("not connected");

    auto kvs = {
        std::make_pair(test_key("k1"), "v1"),
        std::make_pair(test_key("k2"), "v2"),
        std::make_pair(test_key("k3"), "v3")};

    std::vector<std::string> keys;
    std::vector<std::string> vals;
    for (const auto &kv : kvs) {
        keys.push_back(kv.first);
        vals.push_back(kv.second);
    }

    redis.mset(kvs);

    std::vector<std::optional<std::string>> res;
    res = redis.mget(keys);
    EXPECT_TRUE(res.size() == kvs.size());

    std::vector<std::string> res_vals;
    for (const auto &ele : res) {
        EXPECT_TRUE(bool(ele));

        res_vals.push_back(*ele);
    }

    EXPECT_TRUE(vals == res_vals);
    EXPECT_TRUE(!redis.msetnx(kvs));
}

TEST(Redis, SYNC_COMMANDS_ZSET) {
    async::init();
    qb::redis::tcp::client redis{REDIS_URI};

    if (!redis.connect() || !redis.flushall())
        throw std::runtime_error("not connected");

    auto key = test_key("zset");

    std::vector<std::pair<double, std::string>> s = {
        std::make_pair(1.2, "m1"),
        std::make_pair(2, "m2"),
        std::make_pair(3, "m3"),
    };

    const auto &ele = *(s.begin());
    EXPECT_TRUE(redis.zadd(key, {ele}, qb::redis::UpdateType::EXIST) == 0);
    EXPECT_TRUE(redis.zadd(key, s) == 3);
    EXPECT_TRUE(redis.zadd(key, {ele}, qb::redis::UpdateType::NOT_EXIST) == 0);
    EXPECT_TRUE(redis.zadd(key, s, qb::redis::UpdateType::ALWAYS, true) == 0);
    EXPECT_TRUE(redis.zcard(key) == 3);

    auto rank = redis.zrank(key, "m2");
    EXPECT_TRUE(bool(rank) && *rank == 1);
    rank = redis.zrevrank(key, "m4");
    EXPECT_TRUE(!rank);

    auto score = redis.zscore(key, "m4");
    EXPECT_TRUE(!score);

    EXPECT_TRUE(redis.zincrby(key, 1, "m3") == 4);

    score = redis.zscore(key, "m3");
    EXPECT_TRUE(score && *score == 4);

    EXPECT_TRUE(redis.zrem(key, "m1") == 1);
    EXPECT_TRUE(redis.zrem(key, "m1", "m2", "m3", "m4") == 2);
}

TEST(Redis, SYNC_COMMANDS_ZSCAN) {
    async::init();
    qb::redis::tcp::client redis{REDIS_URI};

    if (!redis.connect() || !redis.flushall())
        throw std::runtime_error("not connected");

    auto key = test_key("zscan");

    std::vector<std::pair<double, std::string>> s = {
        std::make_pair(1.2, "m1"),
        std::make_pair(2, "m2"),
        std::make_pair(3, "m3"),
    };
    redis.zadd(key, s);

    std::map<std::string, double> res;
    auto cursor = 0;
    while (true) {
        auto scan = redis.zscan(key, cursor, "m*", 2);
        cursor = scan.cursor;
        std::move(scan.items.begin(), scan.items.end(), std::inserter(res, res.end()));
        if (cursor == 0) {
            break;
        }
    }
    for (auto const &p : s) {
        EXPECT_TRUE(res.at(p.second) == p.first);
    }
}

TEST(Redis, SYNC_COMMANDS_ZSET_RANGE) {
    async::init();
    qb::redis::tcp::client redis{REDIS_URI};

    if (!redis.connect() || !redis.flushall())
        throw std::runtime_error("not connected");

    auto key = test_key("range");

    std::vector<std::pair<double, std::string>> s = {
        std::make_pair(1, "m1"),
        std::make_pair(2, "m2"),
        std::make_pair(3, "m3"),
        std::make_pair(4, "m4"),
    };
    std::vector<std::pair<std::string, double>> sKeys;
    for (const auto &p : s) {
        sKeys.push_back({p.second, p.first});
    }
    std::vector<std::pair<std::string, double>> sReversedKeys = sKeys;
    std::reverse(sReversedKeys.begin(), sReversedKeys.end());

    redis.zadd(key, s);

    EXPECT_TRUE(
        redis.zcount(key, qb::redis::UnboundedInterval<double>{}) == static_cast<long long int>(s.size()));

    std::vector<std::pair<std::string, double>> members;
    members = redis.zrange(key, 0, -1);
    EXPECT_TRUE(members.size() == s.size() && members == sKeys);

    members.clear();
    members = redis.zrevrange(key, 0, 0);
    EXPECT_TRUE(members.size() == 1 && members.at(0) == sKeys.at(s.size() - 1));

    members = redis.zrangebyscore(key, qb::redis::UnboundedInterval<double>{});
    EXPECT_TRUE(members.size() == s.size() && members == sKeys);

    qb::redis::LimitOptions limitOpts;
    limitOpts.offset = 0;
    limitOpts.count = 2;
    members = redis.zrangebyscore(key, qb::redis::UnboundedInterval<double>{}, limitOpts);
    EXPECT_TRUE(members.size() == 2 && members.at(0) == sKeys.at(0) && members.at(1) == sKeys.at(1));

    limitOpts.offset = 1;
    members = redis.zrangebyscore(key, qb::redis::UnboundedInterval<double>{}, limitOpts);
    EXPECT_TRUE(members.size() == 2 && members.at(0) == sKeys.at(1) && members.at(1) == sKeys.at(2));

    limitOpts.offset = s.size() - 1;
    members = redis.zrangebyscore(key, qb::redis::UnboundedInterval<double>{}, limitOpts);
    EXPECT_TRUE(members.size() == 1 && members.at(0) == sKeys.at(sKeys.size() - 1));

    members =
        redis.zrangebyscore(key, qb::redis::BoundedInterval<double>(1, 2, qb::redis::BoundType::RIGHT_OPEN));
    EXPECT_TRUE(members.size() == 1 && members.at(0) == sKeys.at(0));

    members =
        redis.zrevrangebyscore(key, qb::redis::BoundedInterval<double>(1, 3, qb::redis::BoundType::CLOSED));
    EXPECT_TRUE(members.size() == sReversedKeys.size() - 1);
    for (size_t i = 0; i < members.size(); i++) {
        EXPECT_TRUE(members.at(i) == sReversedKeys.at(i + 1));
    }

    limitOpts.offset = 0;
    members = redis.zrevrangebyscore(
        key,
        qb::redis::BoundedInterval<double>(1, 3, qb::redis::BoundType::CLOSED),
        limitOpts);
    EXPECT_TRUE(
        members.size() == 2 && members.at(0) == sReversedKeys.at(1) && members.at(1) == sReversedKeys.at(2));

    limitOpts.offset = 1;
    members = redis.zrevrangebyscore(
        key,
        qb::redis::BoundedInterval<double>(1, 3, qb::redis::BoundType::CLOSED),
        limitOpts);
    EXPECT_TRUE(
        members.size() == 2 && members.at(0) == sReversedKeys.at(2) && members.at(1) == sReversedKeys.at(3));

    limitOpts.offset = s.size() - 2;
    members = redis.zrevrangebyscore(
        key,
        qb::redis::BoundedInterval<double>(1, 3, qb::redis::BoundType::CLOSED),
        limitOpts);
    EXPECT_TRUE(members.size() == 1 && members.at(0) == sReversedKeys.at(s.size() - 1));

    EXPECT_TRUE(redis.zremrangebyrank(key, 0, 0) == 1);

    EXPECT_TRUE(
        redis.zremrangebyscore(
            key,
            qb::redis::BoundedInterval<double>(2, 3, qb::redis::BoundType::LEFT_OPEN)) == 1);
}

TEST(Redis, SYNC_COMMANDS_ZSET_LEX) {
    async::init();
    qb::redis::tcp::client redis{REDIS_URI};

    if (!redis.connect() || !redis.flushall())
        throw std::runtime_error("not connected");

    auto key = test_key("lex");

    redis.zadd(
        key,
        {
            std::make_pair(0, "m1"),
            std::make_pair(0, "m2"),
            std::make_pair(0, "m3"),
        });
    EXPECT_TRUE(redis.zlexcount(key, qb::redis::UnboundedInterval<std::string>{}) == 3);

    std::vector<std::string> members;
    members = redis.zrangebylex(
        key,
        qb::redis::LeftBoundedInterval<std::string>("m2", qb::redis::BoundType::OPEN));
    EXPECT_TRUE(members.size() == 1 && members[0] == "m3");

    members = redis.zrevrangebylex(
        key,
        qb::redis::RightBoundedInterval<std::string>("m1", qb::redis::BoundType::LEFT_OPEN));
    EXPECT_TRUE(members.size() == 1 && members[0] == "m1");
    EXPECT_TRUE(
        redis.zremrangebylex(
            key,
            qb::redis::BoundedInterval<std::string>("m1", "m3", qb::redis::BoundType::OPEN)) == 1);
}

TEST(Redis, SYNC_COMMANDS_ZMULTISET) {
    async::init();
    qb::redis::tcp::client redis{REDIS_URI};

    if (!redis.connect() || !redis.flushall())
        throw std::runtime_error("not connected");

    auto k1 = test_key("k1");
    auto k2 = test_key("k2");
    auto k3 = test_key("k3");

    redis.zadd(k1, {std::make_pair(1, "a"), std::make_pair(2, "b")});
    redis.zadd(k2, {std::make_pair(2, "a"), std::make_pair(3, "c")});

    EXPECT_TRUE(redis.zinterstore(k3, {k1, k2}) == 1);
    auto score = redis.zscore(k3, "a");
    EXPECT_TRUE(bool(score) && *score == 3);
    EXPECT_TRUE(redis.zinterstore(k3, k1, 2) == 2);

    redis.del(k3);

    EXPECT_TRUE(redis.zinterstore(k3, {k1, k2}, {}, qb::redis::Aggregation::MAX) == 1);
    score = redis.zscore(k3, "a");
    EXPECT_TRUE(bool(score) && *score == 2);

    redis.del(k3);

    EXPECT_TRUE(redis.zunionstore(k3, {k1, k2}, {1, 2}, qb::redis::Aggregation::MIN) == 3);
    std::vector<std::pair<std::string, double>> res = redis.zrange(k3, 0, -1);
    for (const auto &ele : res) {
        if (ele.first == "a") {
            EXPECT_TRUE(ele.second == 1);
        } else if (ele.first == "b") {
            EXPECT_TRUE(ele.second == 2);
        } else if (ele.first == "c") {
            EXPECT_TRUE(ele.second == 6);
        } else {
            EXPECT_TRUE(false);
        }
    }

    EXPECT_TRUE(redis.zunionstore(k3, k1, 2) == 2);
}

TEST(Redis, SYNC_COMMANDS_ZSET_POP) {
    async::init();
    qb::redis::tcp::client redis{REDIS_URI};

    if (!redis.connect() || !redis.flushall())
        throw std::runtime_error("not connected");

    auto key = test_key("zpop");
    auto not_exist_key = test_key("zpop_not_exist");

    redis.zadd(
        key,
        {std::make_pair(1.1, "m1"),
         std::make_pair(2.2, "m2"),
         std::make_pair(3.3, "m3"),
         std::make_pair(4.4, "m4"),
         std::make_pair(5.5, "m5"),
         std::make_pair(6.6, "m6")});

    auto item = redis.zpopmax(key);
    EXPECT_TRUE(item.size() && item[0].first == "m6");

    item = redis.zpopmax(not_exist_key);
    EXPECT_TRUE(!item.size());

    item = redis.zpopmin(key);
    EXPECT_TRUE(item.size() && item[0].first == "m1");

    item = redis.zpopmin(not_exist_key);
    EXPECT_TRUE(!item.size());

    std::vector<std::pair<std::string, double>> vec = redis.zpopmax(key, 2);
    EXPECT_TRUE(vec.size() == 2 && vec[0].first == "m5" && vec[1].first == "m4");

    vec = redis.zpopmin(key, 2);
    EXPECT_TRUE(vec.size() == 2 && vec[0].first == "m2" && vec[1].first == "m3");
}

TEST(Redis, SYNC_COMMANDS_GEO) {
    async::init();
    qb::redis::tcp::client redis{REDIS_URI};

    if (!redis.connect() || !redis.flushall())
        throw std::runtime_error("not connected");

    auto key = test_key("geo");
    auto not_exist_key = test_key("geo_not_exist");
    auto dest = test_key("dest");

    auto members = {
        std::make_tuple(10.0, 11.0, "m1"),
        std::make_tuple(10.1, 11.1, "m2"),
        std::make_tuple(10.2, 11.2, "m3")};

    EXPECT_TRUE(redis.geoadd(key, std::make_tuple(10.0, 11.0, "m1")) == 1);
    EXPECT_TRUE(redis.geoadd(key, members) == 2);

    auto dist = redis.geodist(key, "m1", "m4", qb::redis::GeoUnit::KM);
    EXPECT_TRUE(!dist);

    auto hashes = redis.geohash(key, "m1");
    EXPECT_TRUE(hashes.size() && bool(hashes[0]) && *hashes[0] == "s1zned3z8u0");
    hashes = redis.geohash(key, "m9");
    EXPECT_TRUE(hashes.size() && !bool(hashes[0]));

    hashes = redis.geohash(key, "m1", "m4");
    EXPECT_TRUE(hashes.size() == 2);
    EXPECT_TRUE(bool(hashes[0]) && *(hashes[0]) == "s1zned3z8u0" && !(hashes[1]));

    hashes = redis.geohash(key, "m4");
    EXPECT_TRUE(hashes.size() == 1 && !(hashes[0]));

    std::vector<std::optional<std::pair<double, double>>> pos;
    pos = redis.geopos(key, "m4");
    EXPECT_TRUE(pos.size() == 1 && !(pos[0]));

    auto position = redis.geopos(key, "m3");
    EXPECT_TRUE(position.size() && bool(position[0]));
    position = redis.geopos(key, "m4");
    EXPECT_TRUE(position.size() && !bool(position[0]));
}

TEST(Redis, SYNC_COMMANDS_SCRIPT) {
    async::init();
    qb::redis::tcp::client redis{REDIS_URI};

    if (!redis.connect() || !redis.flushall())
        throw std::runtime_error("not connected");

    auto key1 = test_key("k1");
    auto key2 = test_key("k2");

    std::string script = "redis.call('set', KEYS[1], 1);"
                         "redis.call('set', KEYS[2], 2);"
                         "local first = redis.call('get', KEYS[1]);"
                         "local second = redis.call('get', KEYS[2]);"
                         "return first + second";

    std::initializer_list<std::string> keys = {key1, key2};
    std::initializer_list<std::string> empty_list = {};

    auto num = redis.eval<long long>(script, keys, empty_list);
    EXPECT_TRUE(num == 3);

    num = redis.eval<long long>(script, keys, empty_list);
    EXPECT_TRUE(num == 3);

    script = "return 1";
    num = redis.eval<long long>(script, empty_list, empty_list);
    EXPECT_TRUE(num == 1);

    num = redis.eval<long long>(script, empty_list, empty_list);
    EXPECT_TRUE(num == 1);

    auto script_with_args = "return {ARGV[1] + 1, ARGV[2] + 2, ARGV[3] + 3}";
    std::initializer_list<std::string> args = {"1", "2", "3"};
    std::vector<long long> res = redis.eval<std::vector<long long>>(script_with_args, empty_list, args);
    EXPECT_TRUE(res == std::vector<long long>({2, 4, 6}));

    res = redis.eval<std::vector<long long>>(script_with_args, empty_list, args);
    EXPECT_TRUE(res == std::vector<long long>({2, 4, 6}));

    auto sha1 = redis.script_load(script);
    num = redis.evalsha<long long>(sha1, {}, {});
    EXPECT_TRUE(num == 1);

    num = redis.evalsha<long long>(sha1, empty_list);
    EXPECT_TRUE(num == 1);

    auto sha2 = redis.script_load(script_with_args);
    res = redis.evalsha<std::vector<long long>>(sha2, empty_list, args);
    EXPECT_TRUE(res == std::vector<long long>({2, 4, 6}));

    res = redis.evalsha<std::vector<long long>>(sha2, empty_list, args);
    EXPECT_TRUE(res == std::vector<long long>({2, 4, 6}));

    std::vector<bool> exist_res = redis.script_exists(sha1, sha2, "not exist");
    EXPECT_TRUE(exist_res == std::vector<bool>({true, true, false}));

    EXPECT_TRUE(redis.script_exists(sha1)[0]);
    auto ret = redis.script_exists("not exist");
    EXPECT_TRUE(!ret[0]);
}