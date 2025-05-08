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
#define REDIS_URI {"tcp://localhost:6379"}

using namespace qb::io;
using namespace std::chrono;
using namespace qb::redis;

// Generates unique key prefixes to avoid collisions between tests
inline std::string
key_prefix(const std::string &key = "") {
    static int  counter = 0;
    std::string prefix  = "qb::redis::stream-test:" + std::to_string(++counter);

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

// Verifies connection and cleans environment before tests
class RedisTest : public ::testing::Test {
protected:
    qb::redis::tcp::client redis{REDIS_URI};

    // Explicitly declare a virtual destructor as noexcept to match the base class
    ~RedisTest() noexcept override = default;

    void
    SetUp() override {
        async::init();
        if (!redis.connect() || !redis.flushall())
            throw std::runtime_error("Unable to connect to Redis");

        // Wait for connection to be established
        redis.await();
        TearDown();
    }

    void
    TearDown() override {
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
    std::string                                      key     = test_key("xadd");
    std::vector<std::pair<std::string, std::string>> entries = {{"field1", "value1"},
                                                                {"field2", "value2"}};

    // Test basic xadd
    auto id = redis.xadd(key, entries);
    EXPECT_GT(id.timestamp, 0);
    EXPECT_GE(id.sequence, 0);

    // Test xadd with specific ID
    std::string specific_id_str =
        std::to_string(
            std::chrono::high_resolution_clock::now().time_since_epoch().count()) +
        "-1";
    qb::redis::stream_id expected_id{};
    auto                 pos = specific_id_str.find('-');
    expected_id.timestamp    = std::stoll(specific_id_str.substr(0, pos));
    expected_id.sequence     = std::stoll(specific_id_str.substr(pos + 1));

    auto result_id = redis.xadd(key, entries, specific_id_str);
    EXPECT_EQ(result_id.timestamp, expected_id.timestamp);
    EXPECT_EQ(result_id.sequence, expected_id.sequence);
}

// Test XLEN command
TEST_F(RedisTest, SYNC_STREAM_COMMANDS_XLEN) {
    std::string                                      key     = test_key("xlen");
    std::vector<std::pair<std::string, std::string>> entries = {{"field1", "value1"}};

    // Add entries
    redis.xadd(key, entries);
    redis.xadd(key, entries);

    // Test xlen
    EXPECT_EQ(redis.xlen(key), 2);
}

// Test XDEL command
TEST_F(RedisTest, SYNC_STREAM_COMMANDS_XDEL) {
    std::string                                      key     = test_key("xdel");
    std::vector<std::pair<std::string, std::string>> entries = {{"field1", "value1"}};

    // Add entries
    auto id1 = redis.xadd(key, entries);
    (void)id1;
    redis.xadd(key, entries);

    // Test xdel
    EXPECT_EQ(redis.xdel(key, id1), 1);
    EXPECT_EQ(redis.xlen(key), 1);
}

// Test XGROUP commands
TEST_F(RedisTest, SYNC_STREAM_COMMANDS_XGROUP) {
    std::string key      = test_key("xgroup");
    std::string group    = "test-group";
    std::string consumer = "test-consumer";

    // Create group
    EXPECT_TRUE(redis.xgroup_create(key, group, "0", true));

    // Add an entry to the stream
    std::vector<std::pair<std::string, std::string>> entries = {
        {"field1", "value1"}, {"field2", "value2"}, {"field3", "value3"}};
    redis.xadd(key, entries);
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
    std::string key      = test_key("xack");
    std::string group    = "test-group";
    std::string consumer = "test-consumer";

    // Create group
    redis.xgroup_create(key, group, "0", true);

    // Add entry
    std::vector<std::pair<std::string, std::string>> entries = {{"field1", "value1"}};
    auto                                             id      = redis.xadd(key, entries);
    (void)id;

    // Read the message with the consumer to make it pending
    auto messages = redis.xreadgroup(key, group, consumer, ">");
    
    // Vérifier que nous avons bien reçu des messages
    ASSERT_TRUE(messages.is_array());
    ASSERT_FALSE(messages.empty());
    
    // Vérifier que notre stream est présent dans la réponse
    bool found_stream = false;
    for (const auto& stream_obj : messages) {
        for (auto it = stream_obj.begin(); it != stream_obj.end(); ++it) {
            std::string stream_key = it.key();
            if (stream_key.find(key) != std::string::npos) {
                found_stream = true;
                ASSERT_FALSE(it.value().empty());
            }
        }
    }
    ASSERT_TRUE(found_stream);

    // Now the message can be acknowledged
    EXPECT_EQ(redis.xack(key, group, id), 1);
}

// Test XTRIM command
TEST_F(RedisTest, SYNC_STREAM_COMMANDS_XTRIM) {
    std::string                                      key     = test_key("xtrim");
    std::vector<std::pair<std::string, std::string>> entries = {{"field1", "value1"}};

    // Add entries
    for (int i = 0; i < 5; i++) {
        redis.xadd(key, entries);
    }

    // Test xtrim
    EXPECT_EQ(redis.xtrim(key, 2), 3);
    EXPECT_EQ(redis.xlen(key), 2);
}

// Test XPENDING_DETAILED command
TEST_F(RedisTest, SYNC_STREAM_COMMANDS_XPENDING) {
    std::string key = test_key("xpending");
    std::string group = "test-group";
    std::string consumer = "test-consumer";

    // Create group and add messages
    redis.xgroup_create(key, group, "0", true);
    
    std::vector<std::pair<std::string, std::string>> entries = {
        {"field1", "value1"}, {"field2", "value2"}
    };
    
    auto id1 = redis.xadd(key, entries);
    (void)id1;
    auto id2 = redis.xadd(key, entries);
    (void)id2;
    
    // Read messages to make them pending
    redis.xreadgroup(key, group, consumer, ">");
    
    // Get detailed pending information
    auto pending_info = redis.xpending(key, group);
    std::cout << "XPENDING: " << pending_info.dump(2) << std::endl;
    
    // Verify that we have a valid response
    ASSERT_FALSE(pending_info.is_null());
    ASSERT_TRUE(pending_info.is_array());
    ASSERT_FALSE(pending_info.empty());
    
    // Process as array format
    for (const auto& message : pending_info) {
        ASSERT_TRUE(message.is_array());
        ASSERT_GE(message.size(), 4);
        
        // Format: [id, consumer, idle_time, delivery_count]
        // L'ID est au format numérique (timestamp), pas une chaîne
        EXPECT_TRUE(message[0].is_number());
        EXPECT_TRUE(message[1].is_string());
        EXPECT_TRUE(message[2].is_number());
        EXPECT_TRUE(message[3].is_number());
        
        std::string msg_consumer = message[1].template get<std::string>();
        EXPECT_EQ(msg_consumer, consumer);
        
        // Vérifier le nombre de livraisons (doit être 1 pour la première livraison)
        EXPECT_EQ(message[3].template get<int64_t>(), 1);
    }
    
    // Test with consumer filter
    auto consumer_pending = redis.xpending(key, group, "-", "+", 10, consumer);
    std::cout << "XPENDING with filter: " << consumer_pending.dump(2) << std::endl;
    
    ASSERT_FALSE(consumer_pending.is_null());
    ASSERT_TRUE(consumer_pending.is_array());
    
    // Si la réponse n'est pas vide, vérifier le format
    if (!consumer_pending.empty()) {
        for (const auto& message : consumer_pending) {
            ASSERT_TRUE(message.is_array());
            ASSERT_GE(message.size(), 4);
            EXPECT_TRUE(message[1].is_string());
            
            std::string msg_consumer = message[1].template get<std::string>();
            EXPECT_EQ(msg_consumer, consumer);
        }
    }
}

// Test XREADGROUP command
TEST_F(RedisTest, SYNC_STREAM_COMMANDS_XREADGROUP) {
    std::string key      = test_key("xreadgroup");
    std::string group    = "test-group";
    std::string consumer = "test-consumer";

    // Create group
    EXPECT_TRUE(redis.xgroup_create(key, group, "0", true));

    // Add entries
    std::vector<std::pair<std::string, std::string>> entries = {{"field1", "value1"},
                                                                {"field2", "value2"}};
    redis.xadd(key, entries);
    redis.xadd(key, entries);

    // Test xreadgroup with ">"
    auto unread_entries = redis.xreadgroup(key, group, consumer, ">");
    std::cout << "XREADGROUP JSON: " << unread_entries.dump() << std::endl;
    
    // La structure retournée est un tableau d'objets où chaque objet a pour clé le nom du stream
    ASSERT_TRUE(unread_entries.is_array());
    ASSERT_GE(unread_entries.size(), 1);
    
    // Trouver notre clé dans le tableau d'objets
    bool found_stream = false;
    for (const auto& stream_obj : unread_entries) {
        // Parcourir chaque objet stream dans le tableau
        for (auto it = stream_obj.begin(); it != stream_obj.end(); ++it) {
            std::string stream_key = it.key();
            
            // Vérifier si c'est notre stream
            if (stream_key.find(key) != std::string::npos) {
                found_stream = true;
                
                // Vérifier la structure des messages
                ASSERT_TRUE(it.value().is_array());
                ASSERT_FALSE(it.value().empty());
                
                // Chaque message est un objet avec l'ID comme clé et les champs comme valeurs
                for (const auto& message_obj : it.value()) {
                    for (auto msg_it = message_obj.begin(); msg_it != message_obj.end(); ++msg_it) {
                        // msg_it.key() est l'ID du message
                        // msg_it.value() est l'objet contenant les champs
                        ASSERT_TRUE(msg_it.value().is_object());
                        ASSERT_TRUE(msg_it.value().contains("field1"));
                        ASSERT_TRUE(msg_it.value().contains("field2"));
                        EXPECT_EQ(msg_it.value()["field1"], "value1");
                        EXPECT_EQ(msg_it.value()["field2"], "value2");
                    }
                }
            }
        }
    }
    
    ASSERT_TRUE(found_stream);

    // Test xreadgroup with count limit
    auto entries_with_limit = redis.xreadgroup(key, group, consumer, "0", 1);
    ASSERT_TRUE(entries_with_limit.is_array());

    // Test with non-blocking mode - use a small timeout rather than 0
    auto no_entries = redis.xreadgroup(key, group, consumer, ">", 1, 100);
    // Pour une requête sans résultat, ça peut être soit un tableau vide soit une valeur nulle
    EXPECT_TRUE(no_entries.is_null() || no_entries.empty());
}

// Test XREADGROUP with multiple streams
TEST_F(RedisTest, SYNC_STREAM_COMMANDS_XREADGROUP_MULTI) {
    std::string key1     = test_key("xreadgroup_multi1");
    std::string key2     = test_key("xreadgroup_multi2");
    std::string group    = "test-group";
    std::string consumer = "test-consumer";

    // Create groups for both streams
    EXPECT_TRUE(redis.xgroup_create(key1, group, "0", true));
    EXPECT_TRUE(redis.xgroup_create(key2, group, "0", true));

    // Add entries to both streams
    std::vector<std::pair<std::string, std::string>> entries1 = {{"stream", "one"},
                                                                 {"field", "value1"}};

    std::vector<std::pair<std::string, std::string>> entries2 = {{"stream", "two"},
                                                                 {"field", "value2"}};

    redis.xadd(key1, entries1);
    redis.xadd(key2, entries2);

    // Test reading from multiple streams using the multi-stream function
    std::vector<std::string> keys = {key1, key2};
    std::vector<std::string> ids  = {">", ">"};

    auto result = redis.xreadgroup(keys, group, consumer, ids);
    std::cout << "XREADGROUP_MULTI JSON: " << result.dump() << std::endl;

    // La structure retournée est un tableau d'objets où chaque objet a pour clé le nom du stream
    ASSERT_TRUE(result.is_array());
    ASSERT_GE(result.size(), 1);
    
    // Vérifier que les deux streams sont présents
    bool found_stream1 = false;
    bool found_stream2 = false;
    
    for (const auto& stream_obj : result) {
        // Parcourir chaque objet stream dans le tableau
        for (auto it = stream_obj.begin(); it != stream_obj.end(); ++it) {
            std::string stream_key = it.key();
            
            // Vérifier si c'est stream1
            if (stream_key.find(key1) != std::string::npos) {
                found_stream1 = true;
                
                // Vérifier les messages du stream1
                ASSERT_TRUE(it.value().is_array());
                ASSERT_FALSE(it.value().empty());
                
                // Vérifier les champs du premier message
                for (const auto& message_obj : it.value()) {
                    for (auto msg_it = message_obj.begin(); msg_it != message_obj.end(); ++msg_it) {
                        ASSERT_TRUE(msg_it.value().is_object());
                        EXPECT_EQ(msg_it.value()["stream"], "one");
                        EXPECT_EQ(msg_it.value()["field"], "value1");
                    }
                }
            }
            
            // Vérifier si c'est stream2
            if (stream_key.find(key2) != std::string::npos) {
                found_stream2 = true;
                
                // Vérifier les messages du stream2
                ASSERT_TRUE(it.value().is_array());
                ASSERT_FALSE(it.value().empty());
                
                // Vérifier les champs du premier message
                for (const auto& message_obj : it.value()) {
                    for (auto msg_it = message_obj.begin(); msg_it != message_obj.end(); ++msg_it) {
                        ASSERT_TRUE(msg_it.value().is_object());
                        EXPECT_EQ(msg_it.value()["stream"], "two");
                        EXPECT_EQ(msg_it.value()["field"], "value2");
                    }
                }
            }
        }
    }
    
    // Vérifier que les deux streams ont été trouvés
    ASSERT_TRUE(found_stream1);
    ASSERT_TRUE(found_stream2);
}

// Test XREAD command
TEST_F(RedisTest, SYNC_STREAM_COMMANDS_XREAD) {
    std::string key = test_key("xread");

    // Add entries
    std::vector<std::pair<std::string, std::string>> entries = {{"field1", "value1"},
                                                                {"field2", "value2"}};
    redis.xadd(key, entries);
    auto id2 = redis.xadd(key, entries);
    (void)id2;

    // Test xread with "0" to read all messages
    auto all_entries = redis.xread(key, "0");
    std::cout << "XREAD JSON: " << all_entries.dump() << std::endl;
    
    // La structure retournée est un tableau d'objets où chaque objet a pour clé le nom du stream
    ASSERT_TRUE(all_entries.is_array());
    ASSERT_FALSE(all_entries.empty());
    
    // Trouver notre clé dans le tableau d'objets
    bool found_stream = false;
    for (const auto& stream_obj : all_entries) {
        // Parcourir chaque objet stream dans le tableau
        for (auto it = stream_obj.begin(); it != stream_obj.end(); ++it) {
            std::string stream_key = it.key();
            
            // Vérifier si c'est notre stream
            if (stream_key.find(key) != std::string::npos) {
                found_stream = true;
                
                // Vérifier les messages du stream
                ASSERT_TRUE(it.value().is_array());
                ASSERT_FALSE(it.value().empty());
                
                // Vérifier les champs du premier message
                for (const auto& message_obj : it.value()) {
                    for (auto msg_it = message_obj.begin(); msg_it != message_obj.end(); ++msg_it) {
                        ASSERT_TRUE(msg_it.value().is_object());
                        ASSERT_TRUE(msg_it.value().contains("field1"));
                        ASSERT_TRUE(msg_it.value().contains("field2"));
                        EXPECT_EQ(msg_it.value()["field1"], "value1");
                        EXPECT_EQ(msg_it.value()["field2"], "value2");
                    }
                }
            }
        }
    }
    
    ASSERT_TRUE(found_stream);

    // Test xread with count limit
    auto entries_with_limit = redis.xread(key, "0", 1);
    ASSERT_TRUE(entries_with_limit.is_array());
    ASSERT_FALSE(entries_with_limit.empty());

    // Test with non-existing ID
    auto non_existing =
        redis.xread(key, std::to_string(id2.timestamp + 1000) + "-0", std::nullopt, 100);
    // Pour une requête sans résultat, ça peut être soit un tableau vide soit une valeur nulle
    EXPECT_TRUE(non_existing.is_null() || non_existing.empty());
}

// Test XREAD with multiple streams
TEST_F(RedisTest, SYNC_STREAM_COMMANDS_XREAD_MULTI) {
    std::string key1 = test_key("xread_multi1");
    std::string key2 = test_key("xread_multi2");

    // Add entries to both streams
    std::vector<std::pair<std::string, std::string>> entries1 = {{"stream", "one"},
                                                                 {"field", "value1"}};

    std::vector<std::pair<std::string, std::string>> entries2 = {{"stream", "two"},
                                                                 {"field", "value2"}};

    redis.xadd(key1, entries1);
    redis.xadd(key2, entries2);

    // Test reading from multiple streams
    std::vector<std::string> keys = {key1, key2};
    std::vector<std::string> ids  = {"0", "0"};

    auto result = redis.xread(keys, ids);
    std::cout << "XREAD_MULTI JSON: " << result.dump() << std::endl;

    // La structure retournée est un tableau d'objets où chaque objet a pour clé le nom du stream
    ASSERT_TRUE(result.is_array());
    ASSERT_FALSE(result.empty());
    
    // Vérifier que les deux streams sont présents
    bool found_stream1 = false;
    bool found_stream2 = false;
    
    for (const auto& stream_obj : result) {
        // Parcourir chaque objet stream dans le tableau
        for (auto it = stream_obj.begin(); it != stream_obj.end(); ++it) {
            std::string stream_key = it.key();
            
            // Vérifier si c'est stream1
            if (stream_key.find(key1) != std::string::npos) {
                found_stream1 = true;
                
                // Vérifier les messages du stream1
                ASSERT_TRUE(it.value().is_array());
                ASSERT_FALSE(it.value().empty());
                
                // Vérifier les champs du premier message
                for (const auto& message_obj : it.value()) {
                    for (auto msg_it = message_obj.begin(); msg_it != message_obj.end(); ++msg_it) {
                        ASSERT_TRUE(msg_it.value().is_object());
                        EXPECT_EQ(msg_it.value()["stream"], "one");
                        EXPECT_EQ(msg_it.value()["field"], "value1");
                    }
                }
            }
            
            // Vérifier si c'est stream2
            if (stream_key.find(key2) != std::string::npos) {
                found_stream2 = true;
                
                // Vérifier les messages du stream2
                ASSERT_TRUE(it.value().is_array());
                ASSERT_FALSE(it.value().empty());
                
                // Vérifier les champs du premier message
                for (const auto& message_obj : it.value()) {
                    for (auto msg_it = message_obj.begin(); msg_it != message_obj.end(); ++msg_it) {
                        ASSERT_TRUE(msg_it.value().is_object());
                        EXPECT_EQ(msg_it.value()["stream"], "two");
                        EXPECT_EQ(msg_it.value()["field"], "value2");
                    }
                }
            }
        }
    }
    
    // Vérifier que les deux streams ont été trouvés
    ASSERT_TRUE(found_stream1);
    ASSERT_TRUE(found_stream2);
}

// Test XINFO_STREAM command
TEST_F(RedisTest, SYNC_STREAM_COMMANDS_XINFO_STREAM) {
    std::string key = test_key("xinfo_stream");

    // Add some entries to the stream
    std::vector<std::pair<std::string, std::string>> entries = {
        {"field1", "value1"}, {"field2", "value2"}
    };
    redis.xadd(key, entries);
    redis.xadd(key, entries);

    // Get stream info
    auto info = redis.xinfo_stream(key);
    
    // Verify the structure of the returned JSON
    ASSERT_TRUE(!info.empty());
    EXPECT_EQ(info["length"].template get<int64_t>(), 2);
    EXPECT_TRUE(info.contains("first-entry"));
    EXPECT_TRUE(info.contains("last-entry"));
    EXPECT_TRUE(info.contains("last-generated-id"));
}

// Test XINFO_GROUPS command
TEST_F(RedisTest, SYNC_STREAM_COMMANDS_XINFO_GROUPS) {
    std::string key = test_key("xinfo_groups");
    std::string group1 = "test-group1";
    std::string group2 = "test-group2";

    // Create the stream
    std::vector<std::pair<std::string, std::string>> entries = {{"field1", "value1"}};
    redis.xadd(key, entries);

    // Create consumer groups
    redis.xgroup_create(key, group1, "0");
    redis.xgroup_create(key, group2, "0");

    // Get groups info
    auto groups_info = redis.xinfo_groups(key);
    
    // Verify the structure
    ASSERT_TRUE(groups_info.is_array());
    ASSERT_EQ(groups_info.size(), 2);
    
    // Check each group info
    for (const auto& group : groups_info) {
        EXPECT_TRUE(group.contains("name"));
        EXPECT_TRUE(group.contains("consumers"));
        EXPECT_TRUE(group.contains("pending"));
        EXPECT_TRUE(group.contains("last-delivered-id"));
        
        // Verify one of the groups is in our list
        std::string name = group["name"].template get<std::string>();
        EXPECT_TRUE(name == group1 || name == group2);
    }
}

// Test XINFO_CONSUMERS command
TEST_F(RedisTest, SYNC_STREAM_COMMANDS_XINFO_CONSUMERS) {
    std::string key = test_key("xinfo_consumers");
    std::string group = "test-group";
    std::string consumer1 = "test-consumer1";
    std::string consumer2 = "test-consumer2";

    // Create the stream and consumer group
    std::vector<std::pair<std::string, std::string>> entries = {{"field1", "value1"}};
    redis.xadd(key, entries);
    redis.xadd(key, entries);
    redis.xgroup_create(key, group, "0");

    // Create consumers by reading messages
    redis.xreadgroup(key, group, consumer1, ">");
    redis.xreadgroup(key, group, consumer2, ">");

    // Get consumers info
    auto consumers_info = redis.xinfo_consumers(key, group);
    
    // Verify the structure
    ASSERT_TRUE(consumers_info.is_array());
    ASSERT_EQ(consumers_info.size(), 2);
    
    // Check each consumer info
    for (const auto& consumer : consumers_info) {
        EXPECT_TRUE(consumer.contains("name"));
        EXPECT_TRUE(consumer.contains("pending"));
        
        // Verify one of the consumers is in our list
        std::string name = consumer["name"].template get<std::string>();
        EXPECT_TRUE(name == consumer1 || name == consumer2);
    }
}

// Test XINFO_HELP command
TEST_F(RedisTest, SYNC_STREAM_COMMANDS_XINFO_HELP) {
    // Get XINFO command help
    auto help_info = redis.xinfo_help();
    
    // Verify the structure
    ASSERT_TRUE(help_info.is_array());
    ASSERT_FALSE(help_info.empty());
    
    // Help output should contain strings explaining XINFO usage
    bool has_help_text = false;
    for (const auto& line : help_info) {
        if (line.is_string() && line.template get<std::string>().find("XINFO") != std::string::npos) {
            has_help_text = true;
            break;
        }
    }
    EXPECT_TRUE(has_help_text);
}

/*
 * ASYNCHRONOUS TESTS
 */

// Test async XADD command
TEST_F(RedisTest, ASYNC_STREAM_COMMANDS_XADD) {
    std::string                                      key     = test_key("async_xadd");
    std::vector<std::pair<std::string, std::string>> entries = {{"field1", "value1"}};
    bool                                             xadd_completed = false;

    redis.xadd(
        [&](auto &&reply) {
            EXPECT_TRUE(reply.ok());
            auto id = reply.result();
            EXPECT_GT(id.timestamp, 0);
            EXPECT_GE(id.sequence, 0);
            xadd_completed = true;
        },
        key, entries);

    redis.await();
    EXPECT_TRUE(xadd_completed);
}

// Test async XLEN command
TEST_F(RedisTest, ASYNC_STREAM_COMMANDS_XLEN) {
    std::string                                      key     = test_key("async_xlen");
    std::vector<std::pair<std::string, std::string>> entries = {{"field1", "value1"}};
    bool                                             xlen_completed = false;

    // Add entry
    redis.xadd(key, entries);

    redis.xlen(
        [&](auto &&reply) {
            EXPECT_TRUE(reply.ok());
            EXPECT_EQ(reply.result(), 1);
            xlen_completed = true;
        },
        key);

    redis.await();
    EXPECT_TRUE(xlen_completed);
}

// Test async XDEL command
TEST_F(RedisTest, ASYNC_STREAM_COMMANDS_XDEL) {
    std::string                                      key     = test_key("async_xdel");
    std::vector<std::pair<std::string, std::string>> entries = {{"field1", "value1"}};
    bool                                             xdel_completed = false;

    // Add entry
    auto id = redis.xadd(key, entries);
    (void)id;

    redis.xdel(
        [&](auto &&reply) {
            EXPECT_TRUE(reply.ok());
            EXPECT_EQ(reply.result(), 1);
            xdel_completed = true;
        },
        key, id);

    redis.await();
    EXPECT_TRUE(xdel_completed);
}

// Test async XGROUP commands
TEST_F(RedisTest, ASYNC_STREAM_COMMANDS_XGROUP) {
    std::string key                      = test_key("async_xgroup");
    std::string group                    = "test-group";
    bool        group_commands_completed = false;
    int         command_count            = 0;

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
        key, group, "0", true);

    // Delete consumer
    redis.xgroup_delconsumer(
        [&](auto &&reply) {
            EXPECT_TRUE(reply.ok());
            EXPECT_EQ(reply.result(), 0);
            completion_callback(reply);
        },
        key, group, "test-consumer");

    // Delete group
    redis.xgroup_destroy(
        [&](auto &&reply) {
            EXPECT_TRUE(reply.ok());
            EXPECT_GE(reply.result(), 0);
            completion_callback(reply);
        },
        key, group);

    redis.await();
    EXPECT_TRUE(group_commands_completed);
}

// Test async XACK command
TEST_F(RedisTest, ASYNC_STREAM_COMMANDS_XACK) {
    std::string key            = test_key("async_xack");
    std::string group          = "test-group";
    std::string consumer       = "test-consumer";
    bool        xack_completed = false;

    // Create group
    redis.xgroup_create(key, group, "0", true);

    // Add entry
    std::vector<std::pair<std::string, std::string>> entries = {{"field1", "value1"}};
    auto                                             id      = redis.xadd(key, entries);
    (void)id;

    // Read the message with the consumer to make it pending
    auto messages = redis.xreadgroup(key, group, consumer, ">");
    
    // Vérifier que nous avons bien reçu des messages
    ASSERT_TRUE(messages.is_array());
    ASSERT_FALSE(messages.empty());
    
    // Vérifier que notre stream est présent dans la réponse
    bool found_stream = false;
    for (const auto& stream_obj : messages) {
        for (auto it = stream_obj.begin(); it != stream_obj.end(); ++it) {
            std::string stream_key = it.key();
            if (stream_key.find(key) != std::string::npos) {
                found_stream = true;
                ASSERT_FALSE(it.value().empty());
            }
        }
    }
    ASSERT_TRUE(found_stream);

    redis.xack(
        [&](auto &&reply) {
            EXPECT_TRUE(reply.ok());
            EXPECT_EQ(reply.result(), 1);
            xack_completed = true;
        },
        key, group, id);

    redis.await();
    EXPECT_TRUE(xack_completed);
}

// Test async XTRIM command
TEST_F(RedisTest, ASYNC_STREAM_COMMANDS_XTRIM) {
    std::string                                      key     = test_key("async_xtrim");
    std::vector<std::pair<std::string, std::string>> entries = {{"field1", "value1"}};
    bool                                             xtrim_completed = false;

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
        key, 2);

    redis.await();
    EXPECT_TRUE(xtrim_completed);
}

// Test command chaining
TEST_F(RedisTest, ASYNC_STREAM_COMMANDS_CHAINING) {
    std::string                                      key = test_key("stream_chaining");
    std::vector<std::pair<std::string, std::string>> entries = {{"field1", "value1"}};
    bool                                             all_commands_completed = false;
    int                                              command_count          = 0;

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
        key, entries);

    redis.xlen(
        [&](auto &&reply) {
            EXPECT_TRUE(reply.ok());
            EXPECT_EQ(reply.result(), 1);
            completion_callback(reply);
        },
        key);

    redis.xtrim(
        [&](auto &&reply) {
            EXPECT_TRUE(reply.ok());
            EXPECT_EQ(reply.result(), 0);
            completion_callback(reply);
        },
        key, 1);

    redis.await();
    EXPECT_TRUE(all_commands_completed);
}

// Test async XREAD command
TEST_F(RedisTest, ASYNC_STREAM_COMMANDS_XREAD) {
    std::string key             = test_key("async_xread");
    bool        xread_completed = false;

    // Add entries
    std::vector<std::pair<std::string, std::string>> entries = {{"field1", "value1"},
                                                                {"field2", "value2"}};
    redis.xadd(key, entries);

    // Test async xread
    redis.xread(
        [&](auto &&reply) {
            EXPECT_TRUE(reply.ok());
            auto &result = reply.result();
            
            std::cout << "ASYNC_XREAD JSON: " << result.dump() << std::endl;
            
            // La structure retournée est un tableau d'objets où chaque objet a pour clé le nom du stream
            ASSERT_TRUE(result.is_array());
            ASSERT_FALSE(result.empty());
            
            // Trouver notre clé dans le tableau d'objets
            bool found_stream = false;
            for (const auto& stream_obj : result) {
                // Parcourir chaque objet stream dans le tableau
                for (auto it = stream_obj.begin(); it != stream_obj.end(); ++it) {
                    std::string stream_key = it.key();
                    
                    // Vérifier si c'est notre stream
                    if (stream_key.find(key) != std::string::npos) {
                        found_stream = true;
                        
                        // Vérifier les messages du stream
                        ASSERT_TRUE(it.value().is_array());
                        ASSERT_FALSE(it.value().empty());
                        
                        // Vérifier les champs du premier message
                        for (const auto& message_obj : it.value()) {
                            for (auto msg_it = message_obj.begin(); msg_it != message_obj.end(); ++msg_it) {
                                ASSERT_TRUE(msg_it.value().is_object());
                                ASSERT_TRUE(msg_it.value().contains("field1"));
                                ASSERT_TRUE(msg_it.value().contains("field2"));
                                EXPECT_EQ(msg_it.value()["field1"], "value1");
                                EXPECT_EQ(msg_it.value()["field2"], "value2");
                            }
                        }
                    }
                }
            }
            
            ASSERT_TRUE(found_stream);
            xread_completed = true;
        },
        key, "0");

    redis.await();
    EXPECT_TRUE(xread_completed);
}

// Test async XREAD with multiple streams
TEST_F(RedisTest, ASYNC_STREAM_COMMANDS_XREAD_MULTI) {
    std::string key1                  = test_key("async_xread_multi1");
    std::string key2                  = test_key("async_xread_multi2");
    bool        xread_multi_completed = false;

    // Add entries to both streams
    std::vector<std::pair<std::string, std::string>> entries1 = {{"stream", "one"},
                                                                 {"field", "value1"}};

    std::vector<std::pair<std::string, std::string>> entries2 = {{"stream", "two"},
                                                                 {"field", "value2"}};

    redis.xadd(key1, entries1);
    redis.xadd(key2, entries2);

    // Test reading from multiple streams
    std::vector<std::string> keys = {key1, key2};
    std::vector<std::string> ids  = {"0", "0"};

    redis.xread(
        [&](auto &&reply) {
            EXPECT_TRUE(reply.ok());
            auto &result = reply.result();

            std::cout << "ASYNC_XREAD_MULTI JSON: " << result.dump() << std::endl;

            // La structure retournée est un tableau d'objets où chaque objet a pour clé le nom du stream
            ASSERT_TRUE(result.is_array());
            ASSERT_FALSE(result.empty());
            
            // Vérifier que les deux streams sont présents
            bool found_stream1 = false;
            bool found_stream2 = false;
            
            for (const auto& stream_obj : result) {
                // Parcourir chaque objet stream dans le tableau
                for (auto it = stream_obj.begin(); it != stream_obj.end(); ++it) {
                    std::string stream_key = it.key();
                    
                    // Vérifier si c'est stream1
                    if (stream_key.find(key1) != std::string::npos) {
                        found_stream1 = true;
                        
                        // Vérifier les messages du stream1
                        ASSERT_TRUE(it.value().is_array());
                        ASSERT_FALSE(it.value().empty());
                        
                        // Vérifier les champs du premier message
                        for (const auto& message_obj : it.value()) {
                            for (auto msg_it = message_obj.begin(); msg_it != message_obj.end(); ++msg_it) {
                                ASSERT_TRUE(msg_it.value().is_object());
                                EXPECT_EQ(msg_it.value()["stream"], "one");
                                EXPECT_EQ(msg_it.value()["field"], "value1");
                            }
                        }
                    }
                    
                    // Vérifier si c'est stream2
                    if (stream_key.find(key2) != std::string::npos) {
                        found_stream2 = true;
                        
                        // Vérifier les messages du stream2
                        ASSERT_TRUE(it.value().is_array());
                        ASSERT_FALSE(it.value().empty());
                        
                        // Vérifier les champs du premier message
                        for (const auto& message_obj : it.value()) {
                            for (auto msg_it = message_obj.begin(); msg_it != message_obj.end(); ++msg_it) {
                                ASSERT_TRUE(msg_it.value().is_object());
                                EXPECT_EQ(msg_it.value()["stream"], "two");
                                EXPECT_EQ(msg_it.value()["field"], "value2");
                            }
                        }
                    }
                }
            }
            
            // Vérifier que les deux streams ont été trouvés
            ASSERT_TRUE(found_stream1);
            ASSERT_TRUE(found_stream2);
            xread_multi_completed = true;
        },
        keys, ids);

    redis.await();
    EXPECT_TRUE(xread_multi_completed);
}

// Test async XREADGROUP command
TEST_F(RedisTest, ASYNC_STREAM_COMMANDS_XREADGROUP) {
    std::string key                  = test_key("async_xreadgroup");
    std::string group                = "test-group";
    std::string consumer             = "test-consumer";
    bool        xreadgroup_completed = false;

    // Create group
    redis.xgroup_create(key, group, "0", true);

    // Add entries
    std::vector<std::pair<std::string, std::string>> entries = {{"field1", "value1"},
                                                                {"field2", "value2"}};
    redis.xadd(key, entries);
    redis.xadd(key, entries);

    // Test async xreadgroup
    redis.xreadgroup(
        [&](auto &&reply) {
            EXPECT_TRUE(reply.ok());
            auto &result = reply.result();
            
            std::cout << "ASYNC_XREADGROUP JSON: " << result.dump() << std::endl;
            
            // La structure retournée est un tableau d'objets où chaque objet a pour clé le nom du stream
            ASSERT_TRUE(result.is_array());
            ASSERT_FALSE(result.empty());
            
            // Trouver notre clé dans le tableau d'objets
            bool found_stream = false;
            for (const auto& stream_obj : result) {
                // Parcourir chaque objet stream dans le tableau
                for (auto it = stream_obj.begin(); it != stream_obj.end(); ++it) {
                    std::string stream_key = it.key();
                    
                    // Vérifier si c'est notre stream
                    if (stream_key.find(key) != std::string::npos) {
                        found_stream = true;
                        
                        // Vérifier les messages du stream
                        ASSERT_TRUE(it.value().is_array());
                        ASSERT_FALSE(it.value().empty());
                        
                        // Vérifier les champs du premier message
                        for (const auto& message_obj : it.value()) {
                            for (auto msg_it = message_obj.begin(); msg_it != message_obj.end(); ++msg_it) {
                                ASSERT_TRUE(msg_it.value().is_object());
                                ASSERT_TRUE(msg_it.value().contains("field1"));
                                ASSERT_TRUE(msg_it.value().contains("field2"));
                                EXPECT_EQ(msg_it.value()["field1"], "value1");
                                EXPECT_EQ(msg_it.value()["field2"], "value2");
                            }
                        }
                    }
                }
            }
            
            ASSERT_TRUE(found_stream);
            xreadgroup_completed = true;
        },
        key, group, consumer, ">");

    redis.await();
    EXPECT_TRUE(xreadgroup_completed);
}

// Test async XREADGROUP with multiple streams
TEST_F(RedisTest, ASYNC_STREAM_COMMANDS_XREADGROUP_MULTI) {
    std::string key1     = test_key("async_xreadgroup_multi1");
    std::string key2     = test_key("async_xreadgroup_multi2");
    std::string group    = "test-group";
    std::string consumer = "test-consumer";
    bool        xreadgroup_completed = false;

    // Create groups for both streams
    redis.xgroup_create(key1, group, "0", true);
    redis.xgroup_create(key2, group, "0", true);

    // Add entries to both streams
    std::vector<std::pair<std::string, std::string>> entries1 = {{"stream", "one"},
                                                                 {"field", "value1"}};

    std::vector<std::pair<std::string, std::string>> entries2 = {{"stream", "two"},
                                                                 {"field", "value2"}};

    redis.xadd(key1, entries1);
    redis.xadd(key2, entries2);

    // Test reading from multiple streams
    std::vector<std::string> keys = {key1, key2};
    std::vector<std::string> ids  = {">", ">"};

    redis.xreadgroup(
        [&](auto &&reply) {
            EXPECT_TRUE(reply.ok());
            auto result = reply.result();
            
            std::cout << "ASYNC_XREADGROUP_MULTI JSON: " << result.dump() << std::endl;

            // La structure retournée est un tableau d'objets où chaque objet a pour clé le nom du stream
            ASSERT_TRUE(result.is_array());
            ASSERT_FALSE(result.empty());
            
            // Vérifier que les deux streams sont présents
            bool found_stream1 = false;
            bool found_stream2 = false;
            
            for (const auto& stream_obj : result) {
                // Parcourir chaque objet stream dans le tableau
                for (auto it = stream_obj.begin(); it != stream_obj.end(); ++it) {
                    std::string stream_key = it.key();
                    
                    // Vérifier si c'est stream1
                    if (stream_key.find(key1) != std::string::npos) {
                        found_stream1 = true;
                        
                        // Vérifier les messages du stream1
                        ASSERT_TRUE(it.value().is_array());
                        ASSERT_FALSE(it.value().empty());
                        
                        // Vérifier les champs du premier message
                        for (const auto& message_obj : it.value()) {
                            for (auto msg_it = message_obj.begin(); msg_it != message_obj.end(); ++msg_it) {
                                ASSERT_TRUE(msg_it.value().is_object());
                                EXPECT_EQ(msg_it.value()["stream"], "one");
                                EXPECT_EQ(msg_it.value()["field"], "value1");
                            }
                        }
                    }
                    
                    // Vérifier si c'est stream2
                    if (stream_key.find(key2) != std::string::npos) {
                        found_stream2 = true;
                        
                        // Vérifier les messages du stream2
                        ASSERT_TRUE(it.value().is_array());
                        ASSERT_FALSE(it.value().empty());
                        
                        // Vérifier les champs du premier message
                        for (const auto& message_obj : it.value()) {
                            for (auto msg_it = message_obj.begin(); msg_it != message_obj.end(); ++msg_it) {
                                ASSERT_TRUE(msg_it.value().is_object());
                                EXPECT_EQ(msg_it.value()["stream"], "two");
                                EXPECT_EQ(msg_it.value()["field"], "value2");
                            }
                        }
                    }
                }
            }
            
            // Vérifier que les deux streams ont été trouvés
            ASSERT_TRUE(found_stream1);
            ASSERT_TRUE(found_stream2);
            xreadgroup_completed = true;
        },
        keys, group, consumer, ids);

    redis.await();
    EXPECT_TRUE(xreadgroup_completed);
}

// Test async XINFO_STREAM command
TEST_F(RedisTest, ASYNC_STREAM_COMMANDS_XINFO_STREAM) {
    std::string key = test_key("async_xinfo_stream");
    bool xinfo_stream_completed = false;

    // Add some entries to the stream
    std::vector<std::pair<std::string, std::string>> entries = {
        {"field1", "value1"}, {"field2", "value2"}
    };
    redis.xadd(key, entries);
    redis.xadd(key, entries);

    // Get stream info asynchronously
    redis.xinfo_stream(
        [&](auto &&reply) {
            EXPECT_TRUE(reply.ok());
            auto info = reply.result();
            
            // Verify the structure of the returned JSON
            ASSERT_TRUE(!info.empty());
            EXPECT_EQ(info["length"].template get<int64_t>(), 2);
            EXPECT_TRUE(info.contains("first-entry"));
            EXPECT_TRUE(info.contains("last-entry"));
            EXPECT_TRUE(info.contains("last-generated-id"));
            
            xinfo_stream_completed = true;
        },
        key);

    redis.await();
    EXPECT_TRUE(xinfo_stream_completed);
}

// Test async XINFO_GROUPS command
TEST_F(RedisTest, ASYNC_STREAM_COMMANDS_XINFO_GROUPS) {
    std::string key = test_key("async_xinfo_groups");
    std::string group1 = "test-group1";
    std::string group2 = "test-group2";
    bool xinfo_groups_completed = false;

    // Create the stream
    std::vector<std::pair<std::string, std::string>> entries = {{"field1", "value1"}};
    redis.xadd(key, entries);

    // Create consumer groups
    redis.xgroup_create(key, group1, "0");
    redis.xgroup_create(key, group2, "0");

    // Get groups info asynchronously
    redis.xinfo_groups(
        [&](auto &&reply) {
            EXPECT_TRUE(reply.ok());
            auto groups_info = reply.result();
            
            // Verify the structure
            ASSERT_TRUE(groups_info.is_array());
            ASSERT_EQ(groups_info.size(), 2);
            
            // Check each group info
            for (const auto& group : groups_info) {
                EXPECT_TRUE(group.contains("name"));
                EXPECT_TRUE(group.contains("consumers"));
                EXPECT_TRUE(group.contains("pending"));
                EXPECT_TRUE(group.contains("last-delivered-id"));
                
                // Verify one of the groups is in our list
                std::string name = group["name"].template get<std::string>();
                EXPECT_TRUE(name == group1 || name == group2);
            }
            
            xinfo_groups_completed = true;
        },
        key);

    redis.await();
    EXPECT_TRUE(xinfo_groups_completed);
}

// Test async XINFO_CONSUMERS command
TEST_F(RedisTest, ASYNC_STREAM_COMMANDS_XINFO_CONSUMERS) {
    std::string key = test_key("async_xinfo_consumers");
    std::string group = "test-group";
    std::string consumer1 = "test-consumer1";
    std::string consumer2 = "test-consumer2";
    bool xinfo_consumers_completed = false;

    // Create the stream and consumer group
    std::vector<std::pair<std::string, std::string>> entries = {{"field1", "value1"}};
    redis.xadd(key, entries);
    redis.xadd(key, entries);
    redis.xgroup_create(key, group, "0");

    // Create consumers by reading messages
    redis.xreadgroup(key, group, consumer1, ">");
    redis.xreadgroup(key, group, consumer2, ">");

    // Get consumers info asynchronously
    redis.xinfo_consumers(
        [&](auto &&reply) {
            EXPECT_TRUE(reply.ok());
            auto consumers_info = reply.result();
            
            // Verify the structure
            ASSERT_TRUE(consumers_info.is_array());
            ASSERT_EQ(consumers_info.size(), 2);
            
            // Check each consumer info
            for (const auto& consumer : consumers_info) {
                EXPECT_TRUE(consumer.contains("name"));
                EXPECT_TRUE(consumer.contains("pending"));
                
                // Verify one of the consumers is in our list
                std::string name = consumer["name"].template get<std::string>();
                EXPECT_TRUE(name == consumer1 || name == consumer2);
            }
            
            xinfo_consumers_completed = true;
        },
        key, group);

    redis.await();
    EXPECT_TRUE(xinfo_consumers_completed);
}

// Test async XINFO_HELP command
TEST_F(RedisTest, ASYNC_STREAM_COMMANDS_XINFO_HELP) {
    bool xinfo_help_completed = false;

    // Get XINFO command help asynchronously
    redis.xinfo_help(
        [&](auto &&reply) {
            EXPECT_TRUE(reply.ok());
            auto help_info = reply.result();
            
            // Verify the structure
            ASSERT_TRUE(help_info.is_array());
            ASSERT_FALSE(help_info.empty());
            
            // Help output should contain strings explaining XINFO usage
            bool has_help_text = false;
            for (const auto& line : help_info) {
                if (line.is_string() && line.template get<std::string>().find("XINFO") != std::string::npos) {
                    has_help_text = true;
                    break;
                }
            }
            EXPECT_TRUE(has_help_text);
            
            xinfo_help_completed = true;
        });

    redis.await();
    EXPECT_TRUE(xinfo_help_completed);
}

// Test async XPENDING command
TEST_F(RedisTest, ASYNC_STREAM_COMMANDS_XPENDING) {
    std::string key = test_key("async_xpending");
    std::string group = "test-group";
    std::string consumer = "test-consumer";
    bool xpending_completed = false;

    // Create group and add messages
    redis.xgroup_create(key, group, "0", true);
    
    std::vector<std::pair<std::string, std::string>> entries = {
        {"field1", "value1"}, {"field2", "value2"}
    };
    
    auto id1 = redis.xadd(key, entries);
    (void)id1;
    auto id2 = redis.xadd(key, entries);
    (void)id2;
    
    // Read messages to make them pending
    redis.xreadgroup(key, group, consumer, ">");
    
    // Get detailed pending information asynchronously
    redis.xpending(
        [&](auto &&reply) {
            EXPECT_TRUE(reply.ok());
            auto pending_info = reply.result();
            
            // Verify the structure
            ASSERT_TRUE(pending_info.is_array());
            ASSERT_EQ(pending_info.size(), 2);
            
            // Check details of pending messages
            for (const auto& message : pending_info) {
                ASSERT_TRUE(message.is_array());
                ASSERT_GE(message.size(), 4);
                
                // Message format should be [id, consumer, idle_time, delivery_count]
                EXPECT_TRUE(message[0].is_number());
                EXPECT_TRUE(message[1].is_string());
                EXPECT_TRUE(message[2].is_number());
                EXPECT_TRUE(message[3].is_number());
                
                std::string msg_consumer = message[1].template get<std::string>();
                EXPECT_EQ(msg_consumer, consumer);
                
                // Delivery count should be 1 (first delivery)
                EXPECT_EQ(message[3].template get<int64_t>(), 1);
            }
            
            xpending_completed = true;
        },
        key, group);

    redis.await();
    EXPECT_TRUE(xpending_completed);
}

// Main function to run the tests
int
main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}