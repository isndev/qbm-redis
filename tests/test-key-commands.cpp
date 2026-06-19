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
#include "../redis.h"
#include "protocol_test_common.h"

using namespace qb::io;
using namespace std::chrono;

// ============================================================================
// Fixture: all tests run in both RESP2 and RESP3
// ============================================================================

class KeyProtocolModesTest : public ProtocolModesTestBase {};

INSTANTIATE_PROTOCOL_MODES(KeyProtocolModesTest);

/*
 * COROUTINE TESTS
 */

TEST_P(KeyProtocolModesTest, CORO_KEY_COMMANDS_DEL) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key1 = protocol_key("del1");
        std::string key2 = protocol_key("del2");
        std::string key3 = protocol_key("del3");
        (void) co_await redis.set(key1, "value1");
        (void) co_await redis.set(key2, "value2");
        (void) co_await redis.set(key3, "value3");

        auto del1_reply = co_await redis.del(key1);
        EXPECT_TRUE(del1_reply.ok());
        EXPECT_EQ(del1_reply.result(), 1);
        auto ex1_reply = co_await redis.exists(key1);
        EXPECT_TRUE(ex1_reply.ok());
        EXPECT_EQ(ex1_reply.result(), 0);

        auto del2_reply = co_await redis.del(key2, key3);
        EXPECT_TRUE(del2_reply.ok());
        EXPECT_EQ(del2_reply.result(), 2);
        auto ex2_reply = co_await redis.exists(key2);
        auto ex3_reply = co_await redis.exists(key3);
        EXPECT_TRUE(ex2_reply.ok() && ex3_reply.ok());
        EXPECT_EQ(ex2_reply.result(), 0);
        EXPECT_EQ(ex3_reply.result(), 0);

        auto del0_reply = co_await redis.del("non_existent_key");
        EXPECT_TRUE(del0_reply.ok());
        EXPECT_EQ(del0_reply.result(), 0);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

TEST_P(KeyProtocolModesTest, CORO_KEY_COMMANDS_DUMP) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key   = protocol_key("dump");
        std::string value = "test_value";
        (void) co_await redis.set(key, value);

        auto dump_reply = co_await redis.dump(key);
        EXPECT_TRUE(dump_reply.ok());
        EXPECT_TRUE(dump_reply.result().has_value());

        std::string new_key       = protocol_key("restored");
        auto        restore_reply = co_await redis.restore(new_key, *dump_reply.result(), 0);
        EXPECT_TRUE(restore_reply.ok());

        auto get_reply = co_await redis.get(new_key);
        EXPECT_TRUE(get_reply.ok());
        EXPECT_TRUE(get_reply.result().has_value());
        EXPECT_EQ(*get_reply.result(), value);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

TEST_P(KeyProtocolModesTest, CORO_KEY_COMMANDS_EXISTS) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key1 = protocol_key("exists1");
        std::string key2 = protocol_key("exists2");
        std::string key3 = protocol_key("exists3");
        (void) co_await redis.set(key1, "value1");
        (void) co_await redis.set(key2, "value2");

        auto ex1_reply = co_await redis.exists(key1);
        EXPECT_TRUE(ex1_reply.ok());
        EXPECT_EQ(ex1_reply.result(), 1);
        auto ex3_reply = co_await redis.exists(key3);
        EXPECT_TRUE(ex3_reply.ok());
        EXPECT_EQ(ex3_reply.result(), 0);

        auto ex_multi_reply = co_await redis.exists(key1, key2, key3);
        EXPECT_TRUE(ex_multi_reply.ok());
        EXPECT_EQ(ex_multi_reply.result(), 2);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

TEST_P(KeyProtocolModesTest, CORO_KEY_COMMANDS_KEYS) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string suffix = (GetParam() == ProtocolMode::RESP3 ? ":resp3" : ":resp2");
        // protocol_key() adds ":pid<N>"; glob must allow trailing segment after :resp2/:resp3
        std::string pattern = std::string("keys*") + suffix + "*";
        std::string key1    = protocol_key("keys1");
        std::string key2    = protocol_key("keys2");
        std::string key3    = protocol_key("other");
        (void) co_await redis.set(key1, "value1");
        (void) co_await redis.set(key2, "value2");
        (void) co_await redis.set(key3, "value3");

        auto keys_reply = co_await redis.keys(pattern);
        EXPECT_TRUE(keys_reply.ok());
        auto keys = keys_reply.result();
        EXPECT_EQ(keys.size(), 2);
        EXPECT_TRUE(std::find(keys.begin(), keys.end(), key1) != keys.end());
        EXPECT_TRUE(std::find(keys.begin(), keys.end(), key2) != keys.end());
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

TEST_P(KeyProtocolModesTest, CORO_KEY_COMMANDS_RANDOMKEY) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto empty_reply = co_await redis.randomkey();
        EXPECT_TRUE(empty_reply.ok());
        EXPECT_FALSE(empty_reply.result().has_value());

        std::string key1 = protocol_key("random1");
        std::string key2 = protocol_key("random2");
        std::string key3 = protocol_key("random3");
        (void) co_await redis.set(key1, "value1");
        (void) co_await redis.set(key2, "value2");
        (void) co_await redis.set(key3, "value3");

        auto random_reply = co_await redis.randomkey();
        EXPECT_TRUE(random_reply.ok());
        EXPECT_TRUE(random_reply.result().has_value());
        EXPECT_TRUE(*random_reply.result() == key1 || *random_reply.result() == key2 || *random_reply.result() == key3);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

TEST_P(KeyProtocolModesTest, CORO_KEY_COMMANDS_SCAN) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string              suffix  = (GetParam() == ProtocolMode::RESP3 ? ":resp3" : ":resp2");
        std::string              pattern = std::string("scan*") + suffix + "*";
        std::vector<std::string> keys;
        for (int i = 0; i < 10; ++i) {
            std::string k = std::string("scan") + std::to_string(i);
            (void) co_await redis.set(protocol_key(k.c_str()), "value" + std::to_string(i));
        }
        long long cursor = 0;
        do {
            auto scan_reply = co_await redis.scan(cursor, pattern, 5);
            EXPECT_TRUE(scan_reply.ok());
            cursor = scan_reply.result().cursor;
            keys.insert(keys.end(), scan_reply.result().items.begin(), scan_reply.result().items.end());
        } while (cursor != 0);
        EXPECT_EQ(keys.size(), 10);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

TEST_P(KeyProtocolModesTest, CORO_KEY_COMMANDS_TOUCH) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key1 = protocol_key("touch1");
        std::string key2 = protocol_key("touch2");
        std::string key3 = protocol_key("touch3");
        (void) co_await redis.set(key1, "value1");
        (void) co_await redis.set(key2, "value2");

        auto touch_reply = co_await redis.touch(key1, key2, key3);
        EXPECT_TRUE(touch_reply.ok());
        EXPECT_EQ(touch_reply.result(), 2);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

TEST_P(KeyProtocolModesTest, CORO_KEY_COMMANDS_TYPE) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string string_key = protocol_key("type_string");
        std::string list_key   = protocol_key("type_list");
        std::string set_key    = protocol_key("type_set");
        std::string hash_key   = protocol_key("type_hash");
        std::string zset_key   = protocol_key("type_zset");
        (void) co_await redis.set(string_key, "value");
        (void) co_await redis.lpush(list_key, "value");
        (void) co_await redis.sadd(set_key, "value");
        (void) co_await redis.hset(hash_key, "field", "value");
        (void) co_await redis.zadd(zset_key, {{1.0, "value"}});

        auto t1 = co_await redis.type(string_key);
        auto t2 = co_await redis.type(list_key);
        auto t3 = co_await redis.type(set_key);
        auto t4 = co_await redis.type(hash_key);
        auto t5 = co_await redis.type(zset_key);
        auto t6 = co_await redis.type("non_existent_key");
        EXPECT_TRUE(t1.ok() && t2.ok() && t3.ok() && t4.ok() && t5.ok() && t6.ok());
        EXPECT_EQ(t1.result(), "string");
        EXPECT_EQ(t2.result(), "list");
        EXPECT_EQ(t3.result(), "set");
        EXPECT_EQ(t4.result(), "hash");
        EXPECT_EQ(t5.result(), "zset");
        EXPECT_EQ(t6.result(), "none");
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

TEST_P(KeyProtocolModesTest, CORO_KEY_COMMANDS_UNLINK) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key1 = protocol_key("unlink1");
        std::string key2 = protocol_key("unlink2");
        std::string key3 = protocol_key("unlink3");
        (void) co_await redis.set(key1, "value1");
        (void) co_await redis.set(key2, "value2");

        auto unlink_reply = co_await redis.unlink(key1, key2, key3);
        EXPECT_TRUE(unlink_reply.ok());
        EXPECT_EQ(unlink_reply.result(), 2);
        auto ex1 = co_await redis.exists(key1);
        auto ex2 = co_await redis.exists(key2);
        auto ex3 = co_await redis.exists(key3);
        EXPECT_TRUE(ex1.ok() && ex2.ok() && ex3.ok());
        EXPECT_EQ(ex1.result(), 0);
        EXPECT_EQ(ex2.result(), 0);
        EXPECT_EQ(ex3.result(), 0);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test async DEL command
// Test async TYPE command
// Test async UNLINK command
// Test EXPIREAT and PEXPIREAT (integer and chrono overloads)
TEST_P(KeyProtocolModesTest, CORO_KEY_COMMANDS_EXPIREAT_PEXPIREAT) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("expireat");
        (void) co_await redis.set(key, "value");

        long long now_s = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

        // EXPIREAT with a future Unix timestamp
        auto expireat_reply = co_await redis.expireat(key, now_s + 60);
        EXPECT_TRUE(expireat_reply.ok());
        EXPECT_TRUE(expireat_reply.result());

        auto ttl_reply = co_await redis.ttl(key);
        EXPECT_TRUE(ttl_reply.ok());
        EXPECT_GT(ttl_reply.result(), 0);

        // PEXPIREAT with millisecond timestamp
        long long now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

        auto pexpireat_reply = co_await redis.pexpireat(key, now_ms + 60000);
        EXPECT_TRUE(pexpireat_reply.ok());
        EXPECT_TRUE(pexpireat_reply.result());

        auto pttl_reply = co_await redis.pttl(key);
        EXPECT_TRUE(pttl_reply.ok());
        EXPECT_GT(pttl_reply.result(), 0);

        // Chrono overloads
        auto expire_chrono = co_await redis.expire(key, std::chrono::seconds{30});
        EXPECT_TRUE(expire_chrono.ok());
        EXPECT_TRUE(expire_chrono.result());

        auto pexpire_chrono = co_await redis.pexpire(key, std::chrono::milliseconds{30000});
        EXPECT_TRUE(pexpire_chrono.ok());
        EXPECT_TRUE(pexpire_chrono.result());

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test RENAME and RENAMENX
TEST_P(KeyProtocolModesTest, CORO_KEY_COMMANDS_RENAME_RENAMENX) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string src  = protocol_key("rename_src");
        std::string dst  = protocol_key("rename_dst");
        std::string dst2 = protocol_key("rename_dst2");
        (void) co_await redis.set(src, "hello");

        // RENAME
        auto rename_reply = co_await redis.rename(src, dst);
        EXPECT_TRUE(rename_reply.ok());

        auto ex_old = co_await redis.exists(src);
        EXPECT_EQ(ex_old.result(), 0);

        auto get_new = co_await redis.get(dst);
        EXPECT_TRUE(get_new.ok());
        EXPECT_EQ(*get_new.result(), "hello");

        // RENAMENX to new destination (succeeds)
        auto renamenx_ok = co_await redis.renamenx(dst, dst2);
        EXPECT_TRUE(renamenx_ok.ok());
        EXPECT_TRUE(renamenx_ok.result());

        // RENAMENX to existing destination (fails)
        (void) co_await redis.set(src, "v1");
        (void) co_await redis.set(dst, "v2");
        auto renamenx_fail = co_await redis.renamenx(src, dst);
        EXPECT_TRUE(renamenx_fail.ok());
        EXPECT_FALSE(renamenx_fail.result());

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test MOVE (between databases)
TEST_P(KeyProtocolModesTest, CORO_KEY_COMMANDS_MOVE) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("move_key");
        (void) co_await redis.set(key, "move_value");

        auto move_reply = co_await redis.move(key, 1LL);
        EXPECT_TRUE(move_reply.ok());

        if (move_reply.result()) {
            // Key was moved from db 0 — it should no longer exist here
            auto ex_reply = co_await redis.exists(key);
            EXPECT_EQ(ex_reply.result(), 0);

            // Cleanup key from db 1
            (void) co_await redis.select(1);
            (void) co_await redis.del(key);
            (void) co_await redis.select(0);
        }

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test COPY
TEST_P(KeyProtocolModesTest, CORO_KEY_COMMANDS_COPY) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string src = protocol_key("copy_src");
        std::string dst = protocol_key("copy_dst");
        (void) co_await redis.set(src, "copy_value");

        auto copy_reply = co_await redis.copyKey(src, dst);
        EXPECT_TRUE(copy_reply.ok());
        EXPECT_TRUE(copy_reply.result());

        auto get_reply = co_await redis.get(dst);
        EXPECT_TRUE(get_reply.ok());
        EXPECT_TRUE(get_reply.result().has_value());
        EXPECT_EQ(*get_reply.result(), "copy_value");

        // COPY with REPLACE
        (void) co_await redis.set(dst, "old");
        auto copy_replace = co_await redis.copyKey(src, dst, std::nullopt, true);
        EXPECT_TRUE(copy_replace.ok());
        EXPECT_TRUE(copy_replace.result());
        auto get2 = co_await redis.get(dst);
        EXPECT_TRUE(get2.ok() && get2.result());
        EXPECT_EQ(*get2.result(), "copy_value");
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);
}

// Test EXPIRETIME and PEXPIRETIME
TEST_P(KeyProtocolModesTest, CORO_KEY_COMMANDS_EXPIRETIME_PEXPIRETIME) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("expiretime");
        (void) co_await redis.set(key, "v");
        (void) co_await redis.expire(key, 120);

        auto et = co_await redis.expiretime(key);
        EXPECT_TRUE(et.ok());
        if (et.ok() && et.result() > 0) {
            EXPECT_GT(et.result(),
                      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        }

        auto pet = co_await redis.pexpiretime(key);
        EXPECT_TRUE(pet.ok());
        if (pet.ok() && pet.result() > 0) {
            EXPECT_GT(pet.result(),
                      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        }

        std::string no_key = protocol_key("nonexistent");
        auto        et_nil = co_await redis.expiretime(no_key);
        EXPECT_TRUE(et_nil.ok());
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);
}

// Test OBJECT ENCODING
TEST_P(KeyProtocolModesTest, CORO_KEY_COMMANDS_OBJECT_ENCODING) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("obj_enc");
        (void) co_await redis.set(key, "value");

        auto enc = co_await redis.objectEncoding(key);
        EXPECT_TRUE(enc.ok());
        EXPECT_TRUE(enc.result().has_value());
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);
}

// Test OBJECT FREQ, IDLETIME, REFCOUNT
TEST_P(KeyProtocolModesTest, CORO_KEY_COMMANDS_OBJECT_FREQ_IDLETIME_REFCOUNT) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("obj_sub");
        (void) co_await redis.set(key, "value");

        // OBJECT REFCOUNT - returns refcount or null for non-refcounted
        auto ref_r = co_await redis.objectRefcount(key);
        EXPECT_TRUE(ref_r.ok());

        // OBJECT IDLETIME - returns idle seconds (LRU) or null
        auto idle_r = co_await redis.objectIdletime(key);
        EXPECT_TRUE(idle_r.ok());

        // OBJECT FREQ - returns frequency (LFU) or null; requires maxmemory-policy *lfu
        // May fail with ERR on non-LFU keys or older Redis
        auto freq_r = co_await redis.objectFreq(key);
        if (!freq_r.ok()) {
            std::string e(freq_r.error());
            EXPECT_TRUE(e.find("LFU") != std::string::npos || e.find("maxmemory") != std::string::npos || e.find("ERR") != std::string::npos);
        }

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);
}

// Test MIGRATE (expects error when no target Redis; or success if replica available)
TEST_P(KeyProtocolModesTest, CORO_KEY_COMMANDS_MIGRATE) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("migrate_key");
        (void) co_await redis.set(key, "migrate_val");

        // MIGRATE to port 16380 (typically no Redis) - expect error or success if replica runs
        auto r = co_await redis.migrate("127.0.0.1", 16380, key, 0, 500, true);
        if (r.ok()) {
            EXPECT_TRUE(r.result().ok());
        } else {
            std::string err(r.error());
            EXPECT_TRUE(err.find("ERR") != std::string::npos || err.find("Connection") != std::string::npos
                        || err.find("refused") != std::string::npos || err.find("timeout") != std::string::npos
                        || err.find("IO") != std::string::npos);
        }
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);
}

// Test SORT and SORT_RO
TEST_P(KeyProtocolModesTest, CORO_KEY_COMMANDS_SORT) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("sort_list");
        (void) co_await redis.del(key);
        (void) co_await redis.lpush(key, "3", "1", "2", "4");

        auto sort_r = co_await redis.sortKey(key);
        EXPECT_TRUE(sort_r.ok());
        if (sort_r.ok() && sort_r.result().size() >= 4) {
            EXPECT_EQ(sort_r.result()[0], "1");
            EXPECT_EQ(sort_r.result()[3], "4");
        }

        auto sort_ro_r = co_await redis.sortKeyRo(key);
        EXPECT_TRUE(sort_ro_r.ok());
        if (sort_ro_r.ok() && sort_ro_r.result().size() >= 4) {
            EXPECT_EQ(sort_ro_r.result()[0], "1");
        }

        std::string dest    = protocol_key("sort_dest");
        auto        store_r = co_await redis.sortKeyStore(key, dest);
        EXPECT_TRUE(store_r.ok());
        EXPECT_EQ(store_r.result(), 4);
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);
}

// Test WAITAOF (Redis 7.2+; may fail on older versions with ERR unknown command)
TEST_P(KeyProtocolModesTest, CORO_KEY_COMMANDS_WAITAOF) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        auto r = co_await redis.waitaof(0, 0, 100);
        if (r.ok()) {
            EXPECT_GE(r.result(), 0);
        }
        // WAITAOF requires Redis 7.2+; older versions return ERR unknown command
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);
}

// Test WAIT
TEST_P(KeyProtocolModesTest, CORO_KEY_COMMANDS_WAIT) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        // In standalone mode WAIT returns 0 immediately (no replicas)
        auto wait_reply = co_await redis.wait(0LL, 100LL);
        EXPECT_TRUE(wait_reply.ok());
        EXPECT_GE(wait_reply.result(), 0);

        // Chrono overload
        auto wait_chrono = co_await redis.wait(0LL, std::chrono::milliseconds{100});
        EXPECT_TRUE(wait_chrono.ok());
        EXPECT_GE(wait_chrono.result(), 0);

        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

TEST_P(KeyProtocolModesTest, EXISTS_DEL) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k = protocol_key("exists");
        (void) co_await redis.set(k, "x");
        auto exists_r = co_await redis.exists(k);
        EXPECT_TRUE(exists_r.ok()) << exists_r.error();
        if (exists_r.ok())
            EXPECT_EQ(exists_r.result(), 1);
        auto del_r = co_await redis.del(k);
        EXPECT_TRUE(del_r.ok()) << del_r.error();
        if (del_r.ok())
            EXPECT_EQ(del_r.result(), 1);
        auto exists2_r = co_await redis.exists(k);
        EXPECT_TRUE(exists2_r.ok()) << exists2_r.error();
        if (exists2_r.ok())
            EXPECT_EQ(exists2_r.result(), 0);
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(KeyProtocolModesTest, SETEX_TTL_TYPE) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k       = protocol_key("ttl");
        auto setex_r = co_await redis.setex(k, 60, "val");
        EXPECT_TRUE(setex_r.ok()) << setex_r.error();
        auto ttl_r = co_await redis.ttl(k);
        EXPECT_TRUE(ttl_r.ok()) << ttl_r.error();
        if (ttl_r.ok())
            EXPECT_GE(ttl_r.result(), 55);
        auto type_r = co_await redis.type(k);
        EXPECT_TRUE(type_r.ok()) << type_r.error();
        if (type_r.ok())
            EXPECT_EQ(type_r.result(), "string");
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(KeyProtocolModesTest, EXPIRE_RENAME) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k = protocol_key("key");
        (void) co_await redis.set(k, "x");
        auto exp_r = co_await redis.expire(k, 120);
        EXPECT_TRUE(exp_r.ok()) << exp_r.error();
        if (exp_r.ok())
            EXPECT_TRUE(exp_r.result());
        auto rename_r = co_await redis.rename(k, std::string(k) + ":renamed");
        EXPECT_TRUE(rename_r.ok()) << rename_r.error();
        auto get_r = co_await redis.get(std::string(k) + ":renamed");
        if (get_r.ok() && get_r.result())
            EXPECT_EQ(*get_r.result(), "x");
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(KeyProtocolModesTest, SCAN) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto r = co_await redis.scan(0, "*", 10);
        EXPECT_TRUE(r.ok()) << r.error();
        if (r.ok())
            EXPECT_GE(r.result().items.size(), 0u);
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(KeyProtocolModesTest, RANDOMKEY_OPTIONAL) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto empty_r = co_await redis.randomkey();
        EXPECT_TRUE(empty_r.ok()) << empty_r.error();
        EXPECT_FALSE(empty_r.result().has_value());
        auto k = protocol_key("rand");
        (void) co_await redis.set(k, "x");
        auto r = co_await redis.randomkey();
        EXPECT_TRUE(r.ok()) << r.error();
        EXPECT_TRUE(r.result().has_value());
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(KeyProtocolModesTest, PTTL_INTEGER) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k = protocol_key("pttl");
        (void) co_await redis.setex(k, 60, "v");
        auto r = co_await redis.pttl(k);
        EXPECT_TRUE(r.ok()) << r.error();
        if (r.ok())
            EXPECT_GT(r.result(), 0);
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(KeyProtocolModesTest, COPY_EXPIRETIME) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k = protocol_key("copy");
        (void) co_await redis.set(k, "x");
        auto copy_r = co_await redis.copyKey(k, std::string(k) + ":dst");
        EXPECT_TRUE(copy_r.ok()) << copy_r.error();
        if (copy_r.ok())
            EXPECT_TRUE(copy_r.result());
        auto et_r = co_await redis.expiretime(k);
        EXPECT_TRUE(et_r.ok()) << et_r.error();
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(KeyProtocolModesTest, PERSIST_BOOLEAN) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k = protocol_key("persist");
        (void) co_await redis.setex(k, 60, "v");
        auto r = co_await redis.persist(k);
        EXPECT_TRUE(r.ok()) << r.error();
        if (r.ok())
            EXPECT_TRUE(r.result());
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

int
main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}