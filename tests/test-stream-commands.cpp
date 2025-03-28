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
#include "../redis.h"

// Redis Configuration
#define REDIS_URI \
    { "tcp://localhost:6379" }

using namespace qb::io;
using namespace std::chrono;
using namespace qb::redis;

// Generates unique key prefixes to avoid collisions between tests
inline std::string
key_prefix(const std::string &key = "") {
    static int counter = 0;
    std::string prefix = "qb::redis::stream-test:" + std::to_string(++counter);

    if (key.empty()) {
        return prefix;
    }

    return prefix + ":" + key;
}

// Generates a test key
inline std::string
test_key(const std::string &k) {
    return "{" + key_prefix() + "}::" + k;
}

// Checks connection and cleans environment before tests
class RedisTest : public ::testing::Test {
protected:
    qb::redis::tcp::client redis{REDIS_URI};

    void SetUp() override {
        async::init();
        if (!redis.connect() || !redis.flushall())
            throw std::runtime_error("Unable to connect to Redis");

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

// Test XADD command
TEST_F(RedisTest, SYNC_STREAM_COMMANDS_XADD) {
    std::string key = test_key("xadd");
    std::vector<std::pair<std::string, std::string>> entries = {
            {"field1", "value1"},
            {"field2", "value2"}
    };

    // Test basic xadd
    auto id = redis.xadd(key, entries);
    EXPECT_GT(id.timestamp, 0);
    EXPECT_GE(id.sequence, 0);

    // Test xadd with specific ID
    std::string specific_id_str =
            std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()) + "-1";
    qb::redis::stream_id expected_id{};
    auto pos = specific_id_str.find('-');
    expected_id.timestamp = std::stoll(specific_id_str.substr(0, pos));
    expected_id.sequence = std::stoll(specific_id_str.substr(pos + 1));

    auto result_id = redis.xadd(key, entries, specific_id_str);
    EXPECT_EQ(result_id.timestamp, expected_id.timestamp);
    EXPECT_EQ(result_id.sequence, expected_id.sequence);
}

// Test XLEN command
TEST_F(RedisTest, SYNC_STREAM_COMMANDS_XLEN) {
    std::string key = test_key("xlen");
    std::vector<std::pair<std::string, std::string>> entries = {
            {"field1", "value1"}
    };

    // Add entries
    redis.xadd(key, entries);
    redis.xadd(key, entries);

    // Test xlen
    EXPECT_EQ(redis.xlen(key), 2);
}

// Test XDEL command
TEST_F(RedisTest, SYNC_STREAM_COMMANDS_XDEL) {
    std::string key = test_key("xdel");
    std::vector<std::pair<std::string, std::string>> entries = {
            {"field1", "value1"}
    };

    // Add entries
    auto id1 = redis.xadd(key, entries);
    auto id2 = redis.xadd(key, entries);

    // Test xdel
    EXPECT_EQ(redis.xdel(key, id1), 1);
    EXPECT_EQ(redis.xlen(key), 1);
}

// Test XGROUP commands
TEST_F(RedisTest, SYNC_STREAM_COMMANDS_XGROUP) {
    std::string key = test_key("xgroup");
    std::string group = "test-group";
    std::string consumer = "test-consumer";

    // Create group
    EXPECT_TRUE(redis.xgroup_create(key, group, "0", true));

    // Add an entry to the stream
    std::vector<std::pair<std::string, std::string>> entries = {
            {"field1", "value1"},
            {"field2", "value2"},
            {"field3", "value3"}
    };
    auto id = redis.xadd(key, entries);
    redis.xadd(key, entries);
    redis.xadd(key, entries);
    redis.xadd(key, entries);

    // Read from the group to create the consumer
    auto read_result = redis.xreadgroup(key, group, consumer, ">");
    EXPECT_TRUE(!read_result.empty());

    // Delete consumer
    EXPECT_EQ(redis.xgroup_delconsumer(key, group, consumer), 4);

    // Delete group
    EXPECT_TRUE(redis.xgroup_destroy(key, group));
}

// Test XACK command
TEST_F(RedisTest, SYNC_STREAM_COMMANDS_XACK) {
    std::string key = test_key("xack");
    std::string group = "test-group";
    std::string consumer = "test-consumer";

    // Create group
    redis.xgroup_create(key, group, "0", true);

    // Add entry
    std::vector<std::pair<std::string, std::string>> entries = {
            {"field1", "value1"}
    };
    auto id = redis.xadd(key, entries);
    
    // Read the message with the consumer to make it pending
    auto messages = redis.xreadgroup(key, group, consumer, ">");
    ASSERT_FALSE(messages.empty());
    ASSERT_FALSE(messages[key].empty());
    
    // Now the message can be acknowledged
    EXPECT_EQ(redis.xack(key, group, id), 1);
}

// Test XTRIM command
TEST_F(RedisTest, SYNC_STREAM_COMMANDS_XTRIM) {
    std::string key = test_key("xtrim");
    std::vector<std::pair<std::string, std::string>> entries = {
            {"field1", "value1"}
    };

    // Add entries
    for (int i = 0; i < 5; i++) {
        redis.xadd(key, entries);
    }

    // Test xtrim
    EXPECT_EQ(redis.xtrim(key, 2), 3);
    EXPECT_EQ(redis.xlen(key), 2);
}

// Test XPENDING command
TEST_F(RedisTest, SYNC_STREAM_COMMANDS_XPENDING) {
    std::string key = test_key("xpending");
    std::string group = "test-group";
    std::string consumer = "test-consumer";

    // Create group
    redis.xgroup_create(key, group, "0", true);

    // Add entry
    std::vector<std::pair<std::string, std::string>> entries = {
            {"field1", "value1"}
    };
    auto id = redis.xadd(key, entries);

    // Test xpending without any message read (should be 0)
    EXPECT_EQ(redis.xpending(key, group), 0);

    // Read the message with the consumer to make it pending
    auto messages = redis.xreadgroup(key, group, consumer, ">");
    ASSERT_FALSE(messages.empty());
    ASSERT_FALSE(messages[key].empty());

    // Now test xpending after reading a message - should return count of pending messages
    EXPECT_EQ(redis.xpending(key, group), 1); 

    // Test xpending with consumer - should return count of pending messages for that consumer
    EXPECT_EQ(redis.xpending(key, group, consumer), 1);
}

// Test XREADGROUP command
TEST_F(RedisTest, SYNC_STREAM_COMMANDS_XREADGROUP) {
    std::string key = test_key("xreadgroup");
    std::string group = "test-group";
    std::string consumer = "test-consumer";

    // Create group
    EXPECT_TRUE(redis.xgroup_create(key, group, "0", true));

    // Add entries
    std::vector<std::pair<std::string, std::string>> entries = {
            {"field1", "value1"},
            {"field2", "value2"}
    };
    auto id1 = redis.xadd(key, entries);
    auto id2 = redis.xadd(key, entries);

    // Test xreadgroup with ">"
    auto unread_entries = redis.xreadgroup(key, group, consumer, ">");
    ASSERT_TRUE(!unread_entries.empty());
    ASSERT_EQ(unread_entries.size(), 1); // Now returns a map with 1 key (stream name)
    ASSERT_TRUE(unread_entries.find(key) != unread_entries.end());
    ASSERT_FALSE(unread_entries[key].empty());
    ASSERT_EQ(unread_entries[key][0].fields["field1"], "value1");
    ASSERT_EQ(unread_entries[key][0].fields["field2"], "value2");

    // Test xreadgroup with count limit
    auto entries_with_limit = redis.xreadgroup(key, group, consumer, "0", 1);
    ASSERT_TRUE(!entries_with_limit.empty());
    ASSERT_EQ(entries_with_limit.size(), 1);
    ASSERT_FALSE(entries_with_limit[key].empty());

    // Test with non-blocking mode
    auto no_entries = redis.xreadgroup(key, group, consumer, ">", 1, 0);
    EXPECT_FALSE(!no_entries.empty());
}

// Test XREADGROUP with multiple streams
TEST_F(RedisTest, SYNC_STREAM_COMMANDS_XREADGROUP_MULTI) {
    std::string key1 = test_key("xreadgroup_multi1");
    std::string key2 = test_key("xreadgroup_multi2");
    std::string group = "test-group";
    std::string consumer = "test-consumer";

    // Create groups for both streams
    EXPECT_TRUE(redis.xgroup_create(key1, group, "0", true));
    EXPECT_TRUE(redis.xgroup_create(key2, group, "0", true));

    // Add entries to both streams
    std::vector<std::pair<std::string, std::string>> entries1 = {
            {"stream", "one"},
            {"field",  "value1"}
    };

    std::vector<std::pair<std::string, std::string>> entries2 = {
            {"stream", "two"},
            {"field",  "value2"}
    };

    redis.xadd(key1, entries1);
    redis.xadd(key2, entries2);

    // Test reading from multiple streams using the multi-stream function
    std::vector<std::string> keys = {key1, key2};
    std::vector<std::string> ids = {">", ">"};

    auto result = redis.xreadgroup(keys, group, consumer, ids);

    // Verify results
    ASSERT_TRUE(!result.empty());
    ASSERT_EQ(result.size(), 2);
    ASSERT_TRUE(result.find(key1) != result.end());
    ASSERT_TRUE(result.find(key2) != result.end());

    // Check fields from each stream
    ASSERT_FALSE(result[key1].empty());
    ASSERT_FALSE(result[key2].empty());
    EXPECT_EQ(result[key1][0].fields["stream"], "one");
    EXPECT_EQ(result[key1][0].fields["field"], "value1");
    EXPECT_EQ(result[key2][0].fields["stream"], "two");
    EXPECT_EQ(result[key2][0].fields["field"], "value2");
}

// Test XREAD command
TEST_F(RedisTest, SYNC_STREAM_COMMANDS_XREAD) {
    std::string key = test_key("xread");

    // Add entries
    std::vector<std::pair<std::string, std::string>> entries = {
            {"field1", "value1"},
            {"field2", "value2"}
    };
    auto id1 = redis.xadd(key, entries);
    auto id2 = redis.xadd(key, entries);

    // Test xread with "0" to read all messages
    auto all_entries = redis.xread(key, "0");
    ASSERT_TRUE(!all_entries.empty());
    ASSERT_EQ(all_entries.size(), 1); // One stream
    ASSERT_TRUE(all_entries.find(key) != all_entries.end());
    ASSERT_FALSE(all_entries[key].empty());
    ASSERT_EQ(all_entries[key][0].fields["field1"], "value1");
    ASSERT_EQ(all_entries[key][0].fields["field2"], "value2");

    // Test xread with count limit
    auto entries_with_limit = redis.xread(key, "0", 1);
    ASSERT_TRUE(!entries_with_limit.empty());
    ASSERT_EQ(entries_with_limit.size(), 1);
    ASSERT_FALSE(entries_with_limit[key].empty());

    // Test with non-existing ID
    auto non_existing = redis.xread(key, std::to_string(id2.timestamp + 1000) + "-0", std::nullopt, 0);
    EXPECT_FALSE(!non_existing.empty());
}

// Test XREAD with multiple streams
TEST_F(RedisTest, SYNC_STREAM_COMMANDS_XREAD_MULTI) {
    std::string key1 = test_key("xread_multi1");
    std::string key2 = test_key("xread_multi2");

    // Add entries to both streams
    std::vector<std::pair<std::string, std::string>> entries1 = {
            {"stream", "one"},
            {"field",  "value1"}
    };

    std::vector<std::pair<std::string, std::string>> entries2 = {
            {"stream", "two"},
            {"field",  "value2"}
    };

    redis.xadd(key1, entries1);
    redis.xadd(key2, entries2);

    // Test reading from multiple streams
    std::vector<std::string> keys = {key1, key2};
    std::vector<std::string> ids = {"0", "0"};

    auto result = redis.xread(keys, ids);

    // Verify results
    ASSERT_TRUE(!result.empty());
    ASSERT_EQ(result.size(), 2);
    ASSERT_TRUE(result.find(key1) != result.end());
    ASSERT_TRUE(result.find(key2) != result.end());

    // Check fields from each stream
    ASSERT_FALSE(result[key1].empty());
    ASSERT_FALSE(result[key2].empty());
    EXPECT_EQ(result[key1][0].fields["stream"], "one");
    EXPECT_EQ(result[key1][0].fields["field"], "value1");
    EXPECT_EQ(result[key2][0].fields["stream"], "two");
    EXPECT_EQ(result[key2][0].fields["field"], "value2");
}

/*
 * ASYNCHRONOUS TESTS
 */

// Test async XADD command
TEST_F(RedisTest, ASYNC_STREAM_COMMANDS_XADD) {
    std::string key = test_key("async_xadd");
    std::vector<std::pair<std::string, std::string>> entries = {
            {"field1", "value1"}
    };
    bool xadd_completed = false;

    redis.xadd(
            [&](auto &&reply) {
                EXPECT_TRUE(reply.ok());
                auto id = reply.result();
                EXPECT_GT(id.timestamp, 0);
                EXPECT_GE(id.sequence, 0);
                xadd_completed = true;
            },
            key,
            entries
    );

    redis.await();
    EXPECT_TRUE(xadd_completed);
}

// Test async XLEN command
TEST_F(RedisTest, ASYNC_STREAM_COMMANDS_XLEN) {
    std::string key = test_key("async_xlen");
    std::vector<std::pair<std::string, std::string>> entries = {
            {"field1", "value1"}
    };
    bool xlen_completed = false;

    // Add entry
    redis.xadd(key, entries);

    redis.xlen(
            [&](auto &&reply) {
                EXPECT_TRUE(reply.ok());
                EXPECT_EQ(reply.result(), 1);
                xlen_completed = true;
            },
            key
    );

    redis.await();
    EXPECT_TRUE(xlen_completed);
}

// Test async XDEL command
TEST_F(RedisTest, ASYNC_STREAM_COMMANDS_XDEL) {
    std::string key = test_key("async_xdel");
    std::vector<std::pair<std::string, std::string>> entries = {
            {"field1", "value1"}
    };
    bool xdel_completed = false;

    // Add entry
    auto id = redis.xadd(key, entries);

    redis.xdel(
            [&](auto &&reply) {
                EXPECT_TRUE(reply.ok());
                EXPECT_EQ(reply.result(), 1);
                xdel_completed = true;
            },
            key,
            id
    );

    redis.await();
    EXPECT_TRUE(xdel_completed);
}

// Test async XGROUP commands
TEST_F(RedisTest, ASYNC_STREAM_COMMANDS_XGROUP) {
    std::string key = test_key("async_xgroup");
    std::string group = "test-group";
    bool group_commands_completed = false;
    int command_count = 0;

    // Setup callback to track completion
    auto completion_callback = [&command_count, &group_commands_completed](auto &&) {
        if (++command_count == 3) {
            group_commands_completed = true;
        }
    };

    // Create group
    redis.xgroup_create(
            [&](auto &&reply) {
                EXPECT_TRUE(reply.ok());
                completion_callback(reply);
            },
            key,
            group,
            "0",
            true
    );

    // Delete consumer
    redis.xgroup_delconsumer(
            [&](auto &&reply) {
                EXPECT_TRUE(reply.ok());
                EXPECT_EQ(reply.result(), 0);
                completion_callback(reply);
            },
            key,
            group,
            "test-consumer"
    );

    // Delete group
    redis.xgroup_destroy(
            [&](auto &&reply) {
                EXPECT_TRUE(reply.ok());
                EXPECT_GE(reply.result(), 0);
                completion_callback(reply);
            },
            key,
            group
    );

    redis.await();
    EXPECT_TRUE(group_commands_completed);
}

// Test async XACK command
TEST_F(RedisTest, ASYNC_STREAM_COMMANDS_XACK) {
    std::string key = test_key("async_xack");
    std::string group = "test-group";
    std::string consumer = "test-consumer";
    bool xack_completed = false;

    // Create group
    redis.xgroup_create(key, group, "0", true);

    // Add entry
    std::vector<std::pair<std::string, std::string>> entries = {
            {"field1", "value1"}
    };
    auto id = redis.xadd(key, entries);
    
    // Read the message with the consumer to make it pending
    auto messages = redis.xreadgroup(key, group, consumer, ">");
    ASSERT_FALSE(messages.empty());
    ASSERT_FALSE(messages[key].empty());

    redis.xack(
            [&](auto &&reply) {
                EXPECT_TRUE(reply.ok());
                EXPECT_EQ(reply.result(), 1);
                xack_completed = true;
            },
            key,
            group,
            id
    );

    redis.await();
    EXPECT_TRUE(xack_completed);
}

// Test async XTRIM command
TEST_F(RedisTest, ASYNC_STREAM_COMMANDS_XTRIM) {
    std::string key = test_key("async_xtrim");
    std::vector<std::pair<std::string, std::string>> entries = {
            {"field1", "value1"}
    };
    bool xtrim_completed = false;

    // Add entries
    for (int i = 0; i < 5; i++) {
        redis.xadd(key, entries);
    }

    redis.xtrim(
            [&](auto &&reply) {
                EXPECT_TRUE(reply.ok());
                EXPECT_EQ(reply.result(), 3);
                xtrim_completed = true;
            },
            key,
            2
    );

    redis.await();
    EXPECT_TRUE(xtrim_completed);
}

// Test command chaining
TEST_F(RedisTest, ASYNC_STREAM_COMMANDS_CHAINING) {
    std::string key = test_key("stream_chaining");
    std::vector<std::pair<std::string, std::string>> entries = {
            {"field1", "value1"}
    };
    bool all_commands_completed = false;
    int command_count = 0;

    // Setup callback to track completion
    auto completion_callback = [&command_count, &all_commands_completed](auto &&) {
        if (++command_count == 3) {
            all_commands_completed = true;
        }
    };

    // Chain multiple commands
    redis.xadd(
            [&](auto &&reply) {
                EXPECT_TRUE(reply.ok());
                // EXPECT_FALSE(reply.result());
                completion_callback(reply);
            },
            key,
            entries
    );

    redis.xlen(
            [&](auto &&reply) {
                EXPECT_TRUE(reply.ok());
                EXPECT_EQ(reply.result(), 1);
                completion_callback(reply);
            },
            key
    );

    redis.xtrim(
            [&](auto &&reply) {
                EXPECT_TRUE(reply.ok());
                EXPECT_EQ(reply.result(), 0);
                completion_callback(reply);
            },
            key,
            1
    );

    redis.await();
    EXPECT_TRUE(all_commands_completed);
}

// Test async XPENDING command
TEST_F(RedisTest, ASYNC_STREAM_COMMANDS_XPENDING) {
    std::string key = test_key("async_xpending");
    std::string group = "test-group";
    std::string consumer = "test-consumer";
    bool xpending_completed = false;

    // Create group
    redis.xgroup_create(key, group, "0", true);

    // Add entry
    std::vector<std::pair<std::string, std::string>> entries = {
            {"field1", "value1"}
    };
    auto id = redis.xadd(key, entries);
    
    // Read the message with the consumer to make it pending
    auto messages = redis.xreadgroup(key, group, consumer, ">");
    ASSERT_FALSE(messages.empty());
    ASSERT_FALSE(messages[key].empty());

    // Test xpending with consumer - should return count of pending messages for that consumer
    redis.xpending(
            [&](auto &&reply) {
                EXPECT_TRUE(reply.ok());
                EXPECT_EQ(reply.result(), 1);
                xpending_completed = true;
            },
            key,
            group,
            consumer
    );

    redis.await();
    EXPECT_TRUE(xpending_completed);
}

// Test async XREAD command
TEST_F(RedisTest, ASYNC_STREAM_COMMANDS_XREAD) {
    std::string key = test_key("async_xread");
    bool xread_completed = false;

    // Add entries
    std::vector<std::pair<std::string, std::string>> entries = {
            {"field1", "value1"},
            {"field2", "value2"}
    };
    redis.xadd(key, entries);

    // Test async xread
    redis.xread(
            [&](auto &&reply) {
                EXPECT_TRUE(reply.ok());
                ASSERT_TRUE(!reply.result().empty());
                auto &result = reply.result();
                ASSERT_EQ(result.size(), 1);
                ASSERT_TRUE(result.find(key) != result.end());
                ASSERT_FALSE(result[key].empty());
                EXPECT_EQ(result[key][0].fields["field1"], "value1");
                EXPECT_EQ(result[key][0].fields["field2"], "value2");
                xread_completed = true;
            },
            key,
            "0"
    );

    redis.await();
    EXPECT_TRUE(xread_completed);
}

// Test async XREAD with multiple streams
TEST_F(RedisTest, ASYNC_STREAM_COMMANDS_XREAD_MULTI) {
    std::string key1 = test_key("async_xread_multi1");
    std::string key2 = test_key("async_xread_multi2");
    bool xread_multi_completed = false;

    // Add entries to both streams
    std::vector<std::pair<std::string, std::string>> entries1 = {
            {"stream", "one"},
            {"field",  "value1"}
    };

    std::vector<std::pair<std::string, std::string>> entries2 = {
            {"stream", "two"},
            {"field",  "value2"}
    };

    redis.xadd(key1, entries1);
    redis.xadd(key2, entries2);

    // Test reading from multiple streams asynchronously
    std::vector<std::string> keys = {key1, key2};
    std::vector<std::string> ids = {"0", "0"};

    redis.xread(
            [&](auto &&reply) {
                EXPECT_TRUE(reply.ok());
                ASSERT_TRUE(!reply.result().empty());
                auto &result = reply.result();

                ASSERT_EQ(result.size(), 2);
                ASSERT_TRUE(result.find(key1) != result.end());
                ASSERT_TRUE(result.find(key2) != result.end());

                // Check fields from each stream
                ASSERT_FALSE(result[key1].empty());
                ASSERT_FALSE(result[key2].empty());
                EXPECT_EQ(result[key1][0].fields["stream"], "one");
                EXPECT_EQ(result[key1][0].fields["field"], "value1");
                EXPECT_EQ(result[key2][0].fields["stream"], "two");
                EXPECT_EQ(result[key2][0].fields["field"], "value2");

                xread_multi_completed = true;
            },
            keys, ids
    );

    redis.await();
    EXPECT_TRUE(xread_multi_completed);
}

// Test async XREADGROUP command
TEST_F(RedisTest, ASYNC_STREAM_COMMANDS_XREADGROUP) {
    std::string key = test_key("async_xreadgroup");
    std::string group = "test-group";
    std::string consumer = "test-consumer";
    bool xreadgroup_completed = false;

    // Create group
    redis.xgroup_create(key, group, "0", true);

    // Add entries
    std::vector<std::pair<std::string, std::string>> entries = {
            {"field1", "value1"},
            {"field2", "value2"}
    };
    redis.xadd(key, entries);
    redis.xadd(key, entries);

    // Test async xreadgroup
    redis.xreadgroup(
            [&](auto &&reply) {
                EXPECT_TRUE(reply.ok());
                ASSERT_TRUE(!reply.result().empty());
                auto &result = reply.result();
                ASSERT_EQ(result.size(), 1);
                ASSERT_TRUE(result.find(key) != result.end());
                ASSERT_FALSE(result[key].empty());
                EXPECT_EQ(result[key][0].fields["field1"], "value1");
                EXPECT_EQ(result[key][0].fields["field2"], "value2");
                xreadgroup_completed = true;
            },
            key,
            group,
            consumer,
            ">"
    );

    redis.await();
    EXPECT_TRUE(xreadgroup_completed);
}

// Test async XREADGROUP with multiple streams
TEST_F(RedisTest, ASYNC_STREAM_COMMANDS_XREADGROUP_MULTI) {
    std::string key1 = test_key("async_xreadgroup_multi1");
    std::string key2 = test_key("async_xreadgroup_multi2");
    std::string group = "test-group";
    std::string consumer = "test-consumer";
    bool xreadgroup_multi_completed = false;

    // Create groups for both streams
    redis.xgroup_create(key1, group, "0", true);
    redis.xgroup_create(key2, group, "0", true);

    // Add entries to both streams
    std::vector<std::pair<std::string, std::string>> entries1 = {
            {"stream", "one"},
            {"field",  "value1"}
    };

    std::vector<std::pair<std::string, std::string>> entries2 = {
            {"stream", "two"},
            {"field",  "value2"}
    };

    redis.xadd(key1, entries1);
    redis.xadd(key2, entries2);

    // Test reading from multiple streams using the multi-stream function
    std::vector<std::string> keys = {key1, key2};
    std::vector<std::string> ids = {">", ">"};

    redis.xreadgroup(
            [&](auto &&reply) {
                EXPECT_TRUE(reply.ok());
                ASSERT_TRUE(!reply.result().empty());
                auto &result = reply.result();

                ASSERT_EQ(result.size(), 2);
                ASSERT_TRUE(result.find(key1) != result.end());
                ASSERT_TRUE(result.find(key2) != result.end());

                // Check fields from each stream
                ASSERT_FALSE(result[key1].empty());
                ASSERT_FALSE(result[key2].empty());
                EXPECT_EQ(result[key1][0].fields["stream"], "one");
                EXPECT_EQ(result[key1][0].fields["field"], "value1");
                EXPECT_EQ(result[key2][0].fields["stream"], "two");
                EXPECT_EQ(result[key2][0].fields["field"], "value2");

                xreadgroup_multi_completed = true;
            },
            keys, group, consumer, ids
    );

    redis.await();
    EXPECT_TRUE(xreadgroup_multi_completed);
}

// Main function to run the tests
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
} 