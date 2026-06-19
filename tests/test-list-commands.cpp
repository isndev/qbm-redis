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
using namespace qb::redis;

// ============================================================================
// Fixture: all tests run in both RESP2 and RESP3
// ============================================================================

class ListProtocolModesTest : public ProtocolModesTestBase {};

INSTANTIATE_PROTOCOL_MODES(ListProtocolModesTest);

class TestListCommands : public list_commands<TestListCommands> {
public:
    template <typename T>
    Reply<T>
    command(const std::string &cmd, const std::initializer_list<std::string> &args) {
        return Reply<T>();
    }

    template <typename T>
    Reply<T>
    command(const std::string &cmd, const std::vector<std::string> &args) {
        return Reply<T>();
    }

    template <typename T, typename Func>
    Reply<T>
    command(Func &&func, const std::string &cmd, const std::initializer_list<std::string> &args) {
        return Reply<T>();
    }

    template <typename T, typename Func>
    Reply<T>
    command(Func &&func, const std::string &cmd, const std::vector<std::string> &args) {
        return Reply<T>();
    }

    template <typename T, typename Func>
    Reply<T>
    command(Func &&func, const std::string &cmd, const std::vector<std::string>::const_iterator begin,
            const std::vector<std::string>::const_iterator end) {
        return Reply<T>();
    }
};

/*
 * COROUTINE TESTS
 */

// Test basic LPUSH, RPUSH, LLEN operations
TEST_P(ListProtocolModesTest, CORO_LIST_COMMANDS_PUSH) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("basic");

        // Test LPUSH (left push)
        auto reply1 = co_await redis.lpush(key, "item1");
        EXPECT_TRUE(reply1.ok());
        EXPECT_EQ(reply1.result(), 1);

        auto reply2 = co_await redis.lpush(key, "item2");
        EXPECT_TRUE(reply2.ok());
        EXPECT_EQ(reply2.result(), 2);

        auto reply3 = co_await redis.lpush(key, "item3");
        EXPECT_TRUE(reply3.ok());
        EXPECT_EQ(reply3.result(), 3);

        // Test LLEN (list length)
        auto reply_len = co_await redis.llen(key);
        EXPECT_TRUE(reply_len.ok());
        EXPECT_EQ(reply_len.result(), 3);

        // Test RPUSH (right push)
        auto reply4 = co_await redis.rpush(key, "item4");
        EXPECT_TRUE(reply4.ok());
        EXPECT_EQ(reply4.result(), 4);

        auto reply5 = co_await redis.rpush(key, "item5");
        EXPECT_TRUE(reply5.ok());
        EXPECT_EQ(reply5.result(), 5);

        // Verify length again
        auto reply_len2 = co_await redis.llen(key);
        EXPECT_TRUE(reply_len2.ok());
        EXPECT_EQ(reply_len2.result(), 5);

        // Cleanup
        (void) co_await redis.del(key);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test LPOP, RPOP operations
TEST_P(ListProtocolModesTest, CORO_LIST_COMMANDS_POP) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("pop");

        // Setup list
        (void) co_await redis.rpush(key, "item1");
        (void) co_await redis.rpush(key, "item2");
        (void) co_await redis.rpush(key, "item3");
        (void) co_await redis.rpush(key, "item4");
        (void) co_await redis.rpush(key, "item5");

        // Test LPOP (pop from left)
        auto left_reply = co_await redis.lpop(key);
        EXPECT_TRUE(left_reply.ok());
        EXPECT_TRUE(left_reply.result().has_value());
        EXPECT_EQ(*left_reply.result(), "item1");

        // Test RPOP (pop from right)
        auto right_reply = co_await redis.rpop(key);
        EXPECT_TRUE(right_reply.ok());
        EXPECT_TRUE(right_reply.result().has_value());
        EXPECT_EQ(*right_reply.result(), "item5");

        // Verify length after pops
        auto len_reply = co_await redis.llen(key);
        EXPECT_TRUE(len_reply.ok());
        EXPECT_EQ(len_reply.result(), 3);

        // Test multiple LPOP
        auto left_items_reply = co_await redis.lpop(key, 2);
        EXPECT_TRUE(left_items_reply.ok());
        EXPECT_EQ(left_items_reply.result().size(), 2);
        EXPECT_EQ(left_items_reply.result()[0], "item2");
        EXPECT_EQ(left_items_reply.result()[1], "item3");

        // Verify length again
        auto len_reply2 = co_await redis.llen(key);
        EXPECT_TRUE(len_reply2.ok());
        EXPECT_EQ(len_reply2.result(), 1);

        // Test LPOP on empty list after removing last item
        (void) co_await redis.lpop(key);
        auto empty_reply = co_await redis.lpop(key);
        EXPECT_TRUE(empty_reply.ok());
        EXPECT_FALSE(empty_reply.result().has_value());

        // Cleanup
        (void) co_await redis.del(key);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test LRANGE operation
TEST_P(ListProtocolModesTest, CORO_LIST_COMMANDS_RANGE) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("range");

        // Setup list
        (void) co_await redis.rpush(key, "item1");
        (void) co_await redis.rpush(key, "item2");
        (void) co_await redis.rpush(key, "item3");
        (void) co_await redis.rpush(key, "item4");
        (void) co_await redis.rpush(key, "item5");

        // Test LRANGE
        auto all_reply = co_await redis.lrange(key, 0, -1);
        EXPECT_TRUE(all_reply.ok());
        EXPECT_EQ(all_reply.result().size(), 5);
        EXPECT_EQ(all_reply.result()[0], "item1");
        EXPECT_EQ(all_reply.result()[4], "item5");

        // Test partial range
        auto subset_reply = co_await redis.lrange(key, 1, 3);
        EXPECT_TRUE(subset_reply.ok());
        EXPECT_EQ(subset_reply.result().size(), 3);
        EXPECT_EQ(subset_reply.result()[0], "item2");
        EXPECT_EQ(subset_reply.result()[2], "item4");

        // Test negative indices
        auto last_two_reply = co_await redis.lrange(key, -2, -1);
        EXPECT_TRUE(last_two_reply.ok());
        EXPECT_EQ(last_two_reply.result().size(), 2);
        EXPECT_EQ(last_two_reply.result()[0], "item4");
        EXPECT_EQ(last_two_reply.result()[1], "item5");

        // Cleanup
        (void) co_await redis.del(key);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test LSET, LINDEX operations
TEST_P(ListProtocolModesTest, CORO_LIST_COMMANDS_INDEX) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("index");

        // Setup list
        (void) co_await redis.rpush(key, "item1");
        (void) co_await redis.rpush(key, "item2");
        (void) co_await redis.rpush(key, "item3");
        (void) co_await redis.rpush(key, "item4");
        (void) co_await redis.rpush(key, "item5");

        // Test LINDEX
        auto item_reply = co_await redis.lindex(key, 2);
        EXPECT_TRUE(item_reply.ok());
        EXPECT_TRUE(item_reply.result().has_value());
        EXPECT_EQ(*item_reply.result(), "item3");

        // Test negative index
        auto last_reply = co_await redis.lindex(key, -1);
        EXPECT_TRUE(last_reply.ok());
        EXPECT_TRUE(last_reply.result().has_value());
        EXPECT_EQ(*last_reply.result(), "item5");

        // Test LSET
        auto set_reply = co_await redis.lset(key, 1, "replaced");
        EXPECT_TRUE(set_reply.ok());
        EXPECT_TRUE(set_reply.result().ok());

        // Verify the change
        auto modified_reply = co_await redis.lindex(key, 1);
        EXPECT_TRUE(modified_reply.ok());
        EXPECT_TRUE(modified_reply.result().has_value());
        EXPECT_EQ(*modified_reply.result(), "replaced");

        // Cleanup
        (void) co_await redis.del(key);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test LTRIM operation
TEST_P(ListProtocolModesTest, CORO_LIST_COMMANDS_TRIM) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("trim");

        // Setup list
        (void) co_await redis.rpush(key, "item1");
        (void) co_await redis.rpush(key, "item2");
        (void) co_await redis.rpush(key, "item3");
        (void) co_await redis.rpush(key, "item4");
        (void) co_await redis.rpush(key, "item5");

        // Test LTRIM
        auto trim_reply = co_await redis.ltrim(key, 1, 3);
        EXPECT_TRUE(trim_reply.ok());
        EXPECT_TRUE(trim_reply.result().ok());

        // Verify the result
        auto items_reply = co_await redis.lrange(key, 0, -1);
        EXPECT_TRUE(items_reply.ok());
        EXPECT_EQ(items_reply.result().size(), 3);
        EXPECT_EQ(items_reply.result()[0], "item2");
        EXPECT_EQ(items_reply.result()[2], "item4");

        // Cleanup
        (void) co_await redis.del(key);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test LREM operation
TEST_P(ListProtocolModesTest, CORO_LIST_COMMANDS_REMOVE) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("remove");

        // Setup list with duplicates
        (void) co_await redis.rpush(key, "item1");
        (void) co_await redis.rpush(key, "item2");
        (void) co_await redis.rpush(key, "item3");
        (void) co_await redis.rpush(key, "item2");
        (void) co_await redis.rpush(key, "item4");
        (void) co_await redis.rpush(key, "item2");
        (void) co_await redis.rpush(key, "item5");

        // Test LREM (remove 2 occurrences of "item2")
        auto rem_reply = co_await redis.lrem(key, 2, "item2");
        EXPECT_TRUE(rem_reply.ok());
        EXPECT_EQ(rem_reply.result(), 2);

        // Verify the result
        auto items_reply = co_await redis.lrange(key, 0, -1);
        EXPECT_TRUE(items_reply.ok());
        EXPECT_EQ(items_reply.result().size(), 5);
        EXPECT_EQ(std::count(items_reply.result().begin(), items_reply.result().end(), "item2"), 1);

        // Test LREM with negative count (remove from right)
        auto rem_reply2 = co_await redis.lrem(key, -1, "item2");
        EXPECT_TRUE(rem_reply2.ok());
        EXPECT_EQ(rem_reply2.result(), 1);

        // Verify all "item2" are gone
        auto items_reply2 = co_await redis.lrange(key, 0, -1);
        EXPECT_TRUE(items_reply2.ok());
        EXPECT_EQ(std::count(items_reply2.result().begin(), items_reply2.result().end(), "item2"), 0);

        // Cleanup
        (void) co_await redis.del(key);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test LINSERT operation
TEST_P(ListProtocolModesTest, CORO_LIST_COMMANDS_INSERT) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("insert");

        // Setup list
        (void) co_await redis.rpush(key, "item1");
        (void) co_await redis.rpush(key, "item3");

        // Test LINSERT before
        auto insert_reply = co_await redis.linsert(key, InsertPosition::BEFORE, "item3", "item2");
        EXPECT_TRUE(insert_reply.ok());
        EXPECT_EQ(insert_reply.result(), 3);

        // Verify order
        auto items_reply = co_await redis.lrange(key, 0, -1);
        EXPECT_TRUE(items_reply.ok());
        EXPECT_EQ(items_reply.result().size(), 3);
        EXPECT_EQ(items_reply.result()[0], "item1");
        EXPECT_EQ(items_reply.result()[1], "item2");
        EXPECT_EQ(items_reply.result()[2], "item3");

        // Test LINSERT after
        auto insert_reply2 = co_await redis.linsert(key, InsertPosition::AFTER, "item3", "item4");
        EXPECT_TRUE(insert_reply2.ok());
        EXPECT_EQ(insert_reply2.result(), 4);

        // Verify
        auto items_reply2 = co_await redis.lrange(key, 0, -1);
        EXPECT_TRUE(items_reply2.ok());
        EXPECT_EQ(items_reply2.result()[3], "item4");

        // Cleanup
        (void) co_await redis.del(key);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test blocking operations (BLPOP, BRPOP)
TEST_P(ListProtocolModesTest, CORO_LIST_COMMANDS_BLOCKING) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key1 = protocol_key("blocking1");
        std::string key2 = protocol_key("blocking2");

        // Setup list for key1
        (void) co_await redis.rpush(key1, "item1");

        // Test BLPOP with timeout
        auto result_reply = co_await redis.blpop({key1, key2}, 1);
        EXPECT_TRUE(result_reply.ok());
        EXPECT_TRUE(result_reply.result().has_value());
        if (result_reply.result().has_value()) {
            EXPECT_EQ(result_reply.result()->first, key1);
            EXPECT_EQ(result_reply.result()->second, "item1");
        }

        // Test BLPOP on empty lists with short timeout
        auto empty_reply = co_await redis.blpop({key1, key2}, 1);
        EXPECT_TRUE(empty_reply.ok());
        EXPECT_FALSE(empty_reply.result().has_value());

        // Test BRPOP
        (void) co_await redis.rpush(key2, "item2");
        auto rpop_reply = co_await redis.brpop({key1, key2}, 1);
        EXPECT_TRUE(rpop_reply.ok());
        EXPECT_TRUE(rpop_reply.result().has_value());
        if (rpop_reply.result().has_value()) {
            EXPECT_EQ(rpop_reply.result()->first, key2);
            EXPECT_EQ(rpop_reply.result()->second, "item2");
        }

        // Cleanup
        (void) co_await redis.del(key1, key2);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test LMOVE operation
TEST_P(ListProtocolModesTest, CORO_LIST_COMMANDS_MOVE) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string source = protocol_key("source");
        std::string dest   = protocol_key("dest");

        // Setup source list
        (void) co_await redis.rpush(source, "item1");
        (void) co_await redis.rpush(source, "item2");
        (void) co_await redis.rpush(source, "item3");

        // Test moving from right to left
        auto moved_reply = co_await redis.lmove(source, dest, ListPosition::RIGHT, ListPosition::LEFT);
        EXPECT_TRUE(moved_reply.ok());
        EXPECT_TRUE(moved_reply.result().has_value());
        EXPECT_EQ(*moved_reply.result(), "item3");

        // Verify source and destination
        auto source_reply = co_await redis.lrange(source, 0, -1);
        auto dest_reply   = co_await redis.lrange(dest, 0, -1);
        EXPECT_TRUE(source_reply.ok());
        EXPECT_TRUE(dest_reply.ok());
        EXPECT_EQ(source_reply.result().size(), 2);
        EXPECT_EQ(dest_reply.result().size(), 1);
        EXPECT_EQ(dest_reply.result()[0], "item3");

        // Test moving from left to right
        auto moved_reply2 = co_await redis.lmove(source, dest, ListPosition::LEFT, ListPosition::RIGHT);
        EXPECT_TRUE(moved_reply2.ok());
        EXPECT_TRUE(moved_reply2.result().has_value());
        EXPECT_EQ(*moved_reply2.result(), "item1");

        // Verify final state
        auto source_reply2 = co_await redis.lrange(source, 0, -1);
        auto dest_reply2   = co_await redis.lrange(dest, 0, -1);
        EXPECT_TRUE(source_reply2.ok());
        EXPECT_TRUE(dest_reply2.ok());
        EXPECT_EQ(source_reply2.result().size(), 1);
        EXPECT_EQ(dest_reply2.result().size(), 2);
        EXPECT_EQ(dest_reply2.result()[1], "item1");

        // Cleanup
        (void) co_await redis.del(source, dest);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test LPOS operation
TEST_P(ListProtocolModesTest, CORO_LIST_COMMANDS_POS) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("pos");

        // Setup list with duplicates
        (void) co_await redis.rpush(key, "item1");
        (void) co_await redis.rpush(key, "item2");
        (void) co_await redis.rpush(key, "item3");
        (void) co_await redis.rpush(key, "item2");
        (void) co_await redis.rpush(key, "item4");
        (void) co_await redis.rpush(key, "item2");
        (void) co_await redis.rpush(key, "item5");

        // Test basic LPOS
        auto positions_reply = co_await redis.lpos(key, "item2");
        EXPECT_TRUE(positions_reply.ok());
        EXPECT_EQ(positions_reply.result().size(), 3);
        EXPECT_EQ(positions_reply.result()[0], 1);
        EXPECT_EQ(positions_reply.result()[1], 3);
        EXPECT_EQ(positions_reply.result()[2], 5);

        // Test LPOS with rank
        auto positions_rank = co_await redis.lpos(key, "item2", 2);
        EXPECT_TRUE(positions_rank.ok());
        EXPECT_EQ(positions_rank.result().size(), 2);
        EXPECT_EQ(positions_rank.result()[0], 3);

        // Test LPOS with count
        auto positions_count = co_await redis.lpos(key, "item2", std::nullopt, 2);
        EXPECT_TRUE(positions_count.ok());
        EXPECT_EQ(positions_count.result().size(), 2);
        EXPECT_EQ(positions_count.result()[0], 1);
        EXPECT_EQ(positions_count.result()[1], 3);

        // Test LPOS with maxlen
        auto positions_maxlen = co_await redis.lpos(key, "item2", std::nullopt, std::nullopt, 4);
        EXPECT_TRUE(positions_maxlen.ok());
        EXPECT_EQ(positions_maxlen.result().size(), 2);
        EXPECT_EQ(positions_maxlen.result()[0], 1);
        EXPECT_EQ(positions_maxlen.result()[1], 3);

        // Test LPOS for non-existent element
        auto positions_none = co_await redis.lpos(key, "nonexistent");
        EXPECT_TRUE(positions_none.ok());
        EXPECT_TRUE(positions_none.result().empty());

        // Cleanup
        (void) co_await redis.del(key);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test multiple push operations
TEST_P(ListProtocolModesTest, CORO_LIST_COMMANDS_MULTIPLE_PUSH) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key = protocol_key("multiple-push");

        // Test multiple LPUSH
        auto reply1 = co_await redis.lpush(key, "item1");
        EXPECT_TRUE(reply1.ok());
        EXPECT_EQ(reply1.result(), 1);

        auto reply2 = co_await redis.lpush(key, "item2");
        EXPECT_TRUE(reply2.ok());
        EXPECT_EQ(reply2.result(), 2);

        auto reply3 = co_await redis.lpush(key, "item3");
        EXPECT_TRUE(reply3.ok());
        EXPECT_EQ(reply3.result(), 3);

        // Verify order (should be reversed)
        auto items_reply = co_await redis.lrange(key, 0, -1);
        EXPECT_TRUE(items_reply.ok());
        EXPECT_EQ(items_reply.result().size(), 3);
        EXPECT_EQ(items_reply.result()[0], "item3");
        EXPECT_EQ(items_reply.result()[2], "item1");

        // Test multiple RPUSH
        auto reply4 = co_await redis.rpush(key, "item4");
        EXPECT_TRUE(reply4.ok());
        EXPECT_EQ(reply4.result(), 4);

        auto reply5 = co_await redis.rpush(key, "item5");
        EXPECT_TRUE(reply5.ok());
        EXPECT_EQ(reply5.result(), 5);

        auto reply6 = co_await redis.rpush(key, "item6");
        EXPECT_TRUE(reply6.ok());
        EXPECT_EQ(reply6.result(), 6);

        // Verify order
        auto items_reply2 = co_await redis.lrange(key, 0, -1);
        EXPECT_TRUE(items_reply2.ok());
        EXPECT_EQ(items_reply2.result().size(), 6);
        EXPECT_EQ(items_reply2.result()[3], "item4");
        EXPECT_EQ(items_reply2.result()[5], "item6");

        // Cleanup
        (void) co_await redis.del(key);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test LPUSHX and RPUSHX
TEST_P(ListProtocolModesTest, CORO_LIST_COMMANDS_PUSHX) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string existing_key = protocol_key("pushx-existing");
        std::string new_key      = protocol_key("pushx-new");

        // Setup existing list
        (void) co_await redis.rpush(existing_key, "item1");

        // Test LPUSHX on existing list
        auto reply1 = co_await redis.lpushx(existing_key, "item0");
        EXPECT_TRUE(reply1.ok());
        EXPECT_EQ(reply1.result(), 2);

        // Test LPUSHX on non-existing list
        auto reply2 = co_await redis.lpushx(new_key, "item1");
        EXPECT_TRUE(reply2.ok());
        EXPECT_EQ(reply2.result(), 0);

        // Test RPUSHX on existing list
        auto reply3 = co_await redis.rpushx(existing_key, "item2");
        EXPECT_TRUE(reply3.ok());
        EXPECT_EQ(reply3.result(), 3);

        // Test RPUSHX on non-existing list
        auto reply4 = co_await redis.rpushx(new_key, "item1");
        EXPECT_TRUE(reply4.ok());
        EXPECT_EQ(reply4.result(), 0);

        // Cleanup
        (void) co_await redis.del(existing_key);
        (void) co_await redis.del(new_key);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test RPOPLPUSH
TEST_P(ListProtocolModesTest, CORO_LIST_COMMANDS_RPOPLPUSH) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string source = protocol_key("rpoplpush-source");
        std::string dest   = protocol_key("rpoplpush-dest");

        // Setup source list
        (void) co_await redis.rpush(source, "item1");
        (void) co_await redis.rpush(source, "item2");
        (void) co_await redis.rpush(source, "item3");

        // Test RPOPLPUSH
        auto reply1 = co_await redis.rpoplpush(source, dest);
        EXPECT_TRUE(reply1.ok());
        EXPECT_TRUE(reply1.result().has_value());
        EXPECT_EQ(*reply1.result(), "item3");

        // Verify source
        auto source_reply = co_await redis.lrange(source, 0, -1);
        EXPECT_TRUE(source_reply.ok());
        EXPECT_EQ(source_reply.result().size(), 2);

        // Verify dest
        auto dest_reply = co_await redis.lrange(dest, 0, -1);
        EXPECT_TRUE(dest_reply.ok());
        EXPECT_EQ(dest_reply.result().size(), 1);
        EXPECT_EQ(dest_reply.result()[0], "item3");

        // Cleanup
        (void) co_await redis.del(source, dest);
        completed = true;
    };

    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed) {
        qb::io::async::run(EVRUN_NOWAIT);
    }
}

// Test BLMPOP (blocking LMPOP - with data, returns immediately)
TEST_P(ListProtocolModesTest, CORO_LIST_COMMANDS_BLMPOP) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key1 = protocol_key("blmpop1");
        std::string key2 = protocol_key("blmpop2");
        (void) co_await redis.del(key1, key2);
        (void) co_await redis.rpush(key2, "x", "y", "z");

        auto r = co_await redis.blmpop({key1, key2}, qb::redis::ListPosition::RIGHT, 1, 2);
        EXPECT_TRUE(r.ok());
        EXPECT_TRUE(r.result().has_value());
        if (r.result()) {
            EXPECT_EQ(r.result()->first, key2);
            EXPECT_EQ(r.result()->second.size(), 2u);
            EXPECT_EQ(r.result()->second[0], "z");
            EXPECT_EQ(r.result()->second[1], "y");
        }
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);
}

// Test LMPOP
TEST_P(ListProtocolModesTest, CORO_LIST_COMMANDS_LMPOP) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string key1 = protocol_key("lmpop1");
        std::string key2 = protocol_key("lmpop2");
        (void) co_await redis.del(key1, key2);
        (void) co_await redis.rpush(key2, "a", "b", "c");

        auto r = co_await redis.lmpop({key1, key2}, qb::redis::ListPosition::LEFT, 2);
        EXPECT_TRUE(r.ok());
        EXPECT_TRUE(r.result().has_value());
        if (r.result()) {
            EXPECT_EQ(r.result()->first, key2);
            EXPECT_EQ(r.result()->second.size(), 2u);
            EXPECT_EQ(r.result()->second[0], "a");
            EXPECT_EQ(r.result()->second[1], "b");
        }
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);
}

// Test BLMOVE
TEST_P(ListProtocolModesTest, CORO_LIST_COMMANDS_BLMOVE) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string src = protocol_key("blmove_src");
        std::string dst = protocol_key("blmove_dst");
        (void) co_await redis.del(src, dst);
        (void) co_await redis.rpush(src, "x");

        auto r = co_await redis.blmove(src, dst, qb::redis::ListPosition::RIGHT, qb::redis::ListPosition::LEFT, 1);
        EXPECT_TRUE(r.ok());
        EXPECT_TRUE(r.result().has_value());
        if (r.result())
            EXPECT_EQ(*r.result(), "x");
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);
}

// Test BRPOPLPUSH
TEST_P(ListProtocolModesTest, CORO_LIST_COMMANDS_BRPOPLPUSH) {
    bool completed = false;
    auto test_task = [this, &completed]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3_VAR(completed);
        std::string src = protocol_key("brpoplpush_src");
        std::string dst = protocol_key("brpoplpush_dst");
        (void) co_await redis.del(src, dst);
        (void) co_await redis.rpush(src, "item");

        auto r = co_await redis.brpoplpush(src, dst, 1);
        EXPECT_TRUE(r.ok());
        EXPECT_TRUE(r.result().has_value());
        if (r.result())
            EXPECT_EQ(*r.result(), "item");
        completed = true;
    };
    qb::io::async::coro_scheduler().spawn(test_task());
    while (!completed)
        qb::io::async::run(EVRUN_NOWAIT);
}

// ============================================================================
// RESP2/RESP3 protocol mode tests
// ============================================================================

TEST_P(ListProtocolModesTest, LPUSH_LRANGE) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k      = protocol_key("list");
        auto push_r = co_await redis.lpush(k, "a", "b", "c");
        EXPECT_TRUE(push_r.ok()) << push_r.error();
        if (push_r.ok())
            EXPECT_EQ(push_r.result(), 3);
        auto range_r = co_await redis.lrange(k, 0, -1);
        EXPECT_TRUE(range_r.ok()) << range_r.error();
        if (range_r.ok())
            EXPECT_EQ(range_r.result(), (std::vector<std::string>{"c", "b", "a"}));
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(ListProtocolModesTest, RPUSH_LLEN_LINDEX) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k = protocol_key("rpush");
        (void) co_await redis.rpush(k, "x", "y", "z");
        auto len_r = co_await redis.llen(k);
        EXPECT_TRUE(len_r.ok()) << len_r.error();
        if (len_r.ok())
            EXPECT_EQ(len_r.result(), 3);
        auto idx_r = co_await redis.lindex(k, 1);
        EXPECT_TRUE(idx_r.ok()) << idx_r.error();
        if (idx_r.ok() && idx_r.result())
            EXPECT_EQ(*idx_r.result(), "y");
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(ListProtocolModesTest, LPOP_OPTIONAL) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k = protocol_key("lpop");
        (void) co_await redis.lpush(k, "only");
        auto r = co_await redis.lpop(k);
        EXPECT_TRUE(r.ok()) << r.error();
        EXPECT_TRUE(r.result().has_value() && *r.result() == "only");
        auto empty_r = co_await redis.lpop(k);
        EXPECT_TRUE(empty_r.ok());
        EXPECT_FALSE(empty_r.result().has_value());
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(ListProtocolModesTest, RPOP_OPTIONAL) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k = protocol_key("rpop");
        (void) co_await redis.rpush(k, "tail");
        auto r = co_await redis.rpop(k);
        EXPECT_TRUE(r.ok()) << r.error();
        EXPECT_TRUE(r.result().has_value() && *r.result() == "tail");
        auto empty_r = co_await redis.rpop(k);
        EXPECT_TRUE(empty_r.ok());
        EXPECT_FALSE(empty_r.result().has_value());
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(ListProtocolModesTest, LREM_INTEGER) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k = protocol_key("lrem");
        (void) co_await redis.rpush(k, "a", "b", "a", "c");
        auto r = co_await redis.lrem(k, 1, "a");
        EXPECT_TRUE(r.ok()) << r.error();
        if (r.ok())
            EXPECT_EQ(r.result(), 1);
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

TEST_P(ListProtocolModesTest, LMPOP_LMOVE) {
    bool done = false;
    qb::io::async::coro_scheduler().spawn([this, &done]() -> qb::io::async::task<void> {
        PROTOCOL_ENSURE_RESP3();
        auto k = protocol_key("lmpop");
        (void) co_await redis.rpush(k, "a", "b", "c");
        auto lmpop_r = co_await redis.lmpop({k}, qb::redis::ListPosition::LEFT, 2);
        EXPECT_TRUE(lmpop_r.ok()) << lmpop_r.error();
        if (lmpop_r.ok() && lmpop_r.result()) {
            EXPECT_EQ(lmpop_r.result()->first, k);
            EXPECT_EQ(lmpop_r.result()->second.size(), 2u);
        }
        auto k2 = protocol_key("lmove_src");
        auto k3 = protocol_key("lmove_dst");
        (void) co_await redis.rpush(k2, "x");
        (void) co_await redis.lmove(k2, k3, qb::redis::ListPosition::LEFT, qb::redis::ListPosition::RIGHT);
        auto dst_r = co_await redis.lrange(k3, 0, -1);
        EXPECT_TRUE(dst_r.ok() && dst_r.result().size() == 1);
        done = true;
    });
    while (!done)
        qb::io::async::run(EVRUN_NOWAIT);
}

// Main function to run the tests
int
main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
