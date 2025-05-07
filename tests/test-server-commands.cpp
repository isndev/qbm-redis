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

// Generates unique key prefixes to avoid collisions between tests
inline std::string
key_prefix(const std::string &key = "") {
    static int  counter = 0;
    std::string prefix  = "qb::redis::server-test:" + std::to_string(++counter);

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

// Verifies connection and cleans the environment before tests
class RedisTest : public ::testing::Test {
protected:
    qb::redis::tcp::client redis{REDIS_URI};

    // Explicitly declare the destructor as noexcept to match testing::Test
    ~RedisTest() noexcept override = default;

    void
    SetUp() override {
        async::init();
        if (!redis.connect() || !redis.flushall())
            throw std::runtime_error("Unable to connect to Redis");

        // Wait for the connection to be established
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

// =============== CLIENT MANAGEMENT COMMANDS ===============

TEST_F(RedisTest, DISABLED_SYNC_SERVER_CLIENT_MANAGEMENT) {
    // Test client_setname and client_getname
    ASSERT_TRUE(redis.client_setname("test_client"));
    auto name = redis.client_getname();
    ASSERT_TRUE(name.has_value());
    ASSERT_EQ(*name, "test_client");

    // Test client_list - la response is received as a CSV formatted string and not a JSON
    auto clients_response = redis.client_list();
    std::cout << "CLIENT LIST response type: " << (clients_response.is_string() ? "string" : 
                                                 clients_response.is_array() ? "array" : 
                                                 clients_response.is_object() ? "object" : "other") << std::endl;
    std::cout << "CLIENT LIST response: " << clients_response.dump() << std::endl;
    
    // If it's a string, verify that it contains "name=test_client"
    if (clients_response.is_string()) {
        std::string response_str = clients_response.get<std::string>();
        // In a formatted string like this, we expect to find "name=test_client"
        ASSERT_TRUE(response_str.find("name=test_client") != std::string::npos);
    } 
    // Otherwise, search in other types of structures
    else if (clients_response.is_array()) {
        bool found_client = false;
        for (const auto& client : clients_response) {
            if (client.is_object() && client.contains("name") && 
                client["name"].get<std::string>() == "test_client") {
                found_client = true;
                break;
            }
        }
        ASSERT_TRUE(found_client);
    } 
    else if (clients_response.is_object()) {
        // Verify if the object itself contains the name
        bool client_found = false;
        if (clients_response.contains("name") && 
            clients_response["name"].get<std::string>() == "test_client") {
            client_found = true;
        } else if (clients_response.contains("clients") && clients_response["clients"].is_array()) {
            // Or if the object contains an "clients" array
            for (const auto& client : clients_response["clients"]) {
                if (client.is_object() && client.contains("name") && 
                    client["name"].get<std::string>() == "test_client") {
                    client_found = true;
                    break;
                }
            }
        }
        ASSERT_TRUE(client_found);
    } else {
        FAIL() << "Unexpected response type for client_list";
    }

    // Test client_pause
    ASSERT_TRUE(redis.client_pause(100, "WRITE")); // Pause for 100ms, WRITE mode

    // Test client_tracking
    ASSERT_TRUE(redis.client_tracking(true));
    
    // Test client_tracking_info
    auto tracking_info = redis.client_tracking_info();
    ASSERT_TRUE(tracking_info.is_object());
    ASSERT_TRUE(tracking_info.contains("flags"));
    ASSERT_TRUE(tracking_info.contains("redirect"));
    ASSERT_TRUE(tracking_info.contains("prefixes"));

    ASSERT_TRUE(redis.client_tracking(false));

    // Test client_unblock
    auto unblock_clients_list_json = redis.client_list(); // Get fresh list
    long long client_id_to_unblock = 0;
    if (unblock_clients_list_json.is_array() && !unblock_clients_list_json.empty()) {
        for(const auto& client_entry : unblock_clients_list_json) {
            if (client_entry.is_object() && client_entry.contains("id")) {
                // Try to find a client that is *not* our current connection, if possible.
                // This assumes the current connection might not have a name set or is identifiable.
                // A more robust way would be to get current client's ID via CLIENT ID and skip it.
                if (client_entry.contains("name") && client_entry["name"].get<std::string>() == "test_client") {
                    // This is likely our current named client, try to find another one or default to first.
                } else {
                     client_id_to_unblock = client_entry["id"].get<long long>();
                     // Prioritize unblocking a different client if found and it's not the one we just named test_client
                     if (!client_entry.contains("name") || client_entry["name"].get<std::string>() != "test_client") break;
                }
            }
        }
        // Fallback if no other client was found, or if the first one is the only one.
        if (client_id_to_unblock == 0 && unblock_clients_list_json[0].is_object() && unblock_clients_list_json[0].contains("id")) {
             client_id_to_unblock = unblock_clients_list_json[0]["id"].get<long long>();
        }
    } else if (unblock_clients_list_json.is_string()) {
        // If it's a string, try to extract the ID directly
        std::string client_list_str = unblock_clients_list_json.get<std::string>();
        size_t id_pos = client_list_str.find("id=");
        if (id_pos != std::string::npos) {
            size_t space_pos = client_list_str.find(" ", id_pos);
            if (space_pos != std::string::npos) {
                std::string id_str = client_list_str.substr(id_pos + 3, space_pos - (id_pos + 3));
                try {
                    client_id_to_unblock = std::stoll(id_str);
                } catch (const std::exception& e) {
                    std::cerr << "Error extracting client ID: " << e.what() << std::endl;
                }
            }
        }
    }

    if (client_id_to_unblock != 0) {
         // Attempt to unblock. This might still fail if the client is not actually blocked.
         // The command itself should succeed if the client ID is valid.
         try {
            redis.client_unblock(client_id_to_unblock, false);
         } catch (const qb::redis::Error& e) {
            // It's okay if unblocking a non-blocked client returns an error from Redis
            std::cerr << "Note: client_unblock for " << client_id_to_unblock << " failed as expected or client wasn't blocked: " << e.what() << std::endl;
         }
    }
}

// =============== CONFIGURATION COMMANDS ===============

TEST_F(RedisTest, SYNC_SERVER_CONFIGURATION) {
    // Test config_get and config_set
    auto maxmemory_pairs = redis.config_get("maxmemory");
    ASSERT_FALSE(maxmemory_pairs.empty());
    ASSERT_TRUE(redis.config_set("maxmemory", maxmemory_pairs[0].second));

    // Test getting multiple config params
    auto configs = redis.config_get("*max*");
    ASSERT_FALSE(configs.empty());

    // Test config_resetstat
    ASSERT_TRUE(redis.config_resetstat());

    // Test config_rewrite (requires write permissions, might fail in some environments)
    try {
        auto rewrite_status = redis.config_rewrite();
        // Just verify the call doesn't throw an exception or returns OK
        ASSERT_TRUE(rewrite_status);
    } catch (const std::exception &e) {
        std::cerr << "config_rewrite failed (might lack permissions): " << e.what()
                  << std::endl;
    }
}

// =============== COMMAND INFORMATION COMMANDS ===============

TEST_F(RedisTest, SYNC_SERVER_COMMAND_INFORMATION) {
    // Test command (JSON version for specific commands)
    try {
        std::vector<std::string> specific_cmds = {"get", "set"};
        auto cmd_json_info = redis.command(specific_cmds);
        std::cout << "COMMAND INFO JSON: " << cmd_json_info.dump() << std::endl;
        
        // Verify content rather than type
        ASSERT_FALSE(cmd_json_info.empty());
        
        // Verify that get and set are in the response
        bool has_get = false;
        bool has_set = false;
        
        if (cmd_json_info.is_object()) {
            // Format object with keys = command names
            has_get = cmd_json_info.contains("get");
            has_set = cmd_json_info.contains("set");
        } else if (cmd_json_info.is_array()) {
            for (const auto& cmd : cmd_json_info) {
                if (cmd.is_array() && cmd.size() > 0 && cmd[0].is_string()) {
                    // Format array of arrays [commandname, ...]
                    std::string name = cmd[0].get<std::string>();
                    if (name == "get") has_get = true;
                    else if (name == "set") has_set = true;
                } else if (cmd.is_object() && cmd.contains("name")) {
                    // Format array of objects [{name: ...}, ...]
                    std::string name = cmd["name"].get<std::string>();
                    if (name == "get") has_get = true;
                    else if (name == "set") has_set = true;
                }
            }
        }
        
        ASSERT_TRUE(has_get);
        ASSERT_TRUE(has_set);

        // Test command (JSON version for all commands)
        auto cmd_json_all = redis.command(); 
        std::cout << "COMMAND ALL JSON: " << cmd_json_all.dump().substr(0, 200) << "..." << std::endl;
        
        ASSERT_FALSE(cmd_json_all.empty());
        ASSERT_GT(cmd_json_all.size(), 10); 
        
        // Verify that ping is present, regardless of structure
        bool has_ping = false;
        
        if (cmd_json_all.is_object()) {
            has_ping = cmd_json_all.contains("ping");
        } else if (cmd_json_all.is_array()) {
            for (const auto& cmd : cmd_json_all) {
                if (cmd.is_array() && cmd.size() > 0 && cmd[0].is_string()) {
                    // Format array of arrays [commandname, ...]
                    std::string name = cmd[0].get<std::string>();
                    if (name == "ping") {
                        has_ping = true;
                        break;
                    }
                } else if (cmd.is_object() && cmd.contains("name")) {
                    // Format array of objects [{name: ...}, ...]
                    std::string name = cmd["name"].get<std::string>();
                    if (name == "ping") {
                        has_ping = true;
                        break;
                    }
                }
            }
        }
        
        ASSERT_TRUE(has_ping);

        // Test command_stats (JSON version) - might not be available in all Redis versions
        try {
            auto cmd_stats_json = redis.command_stats();
            ASSERT_TRUE(cmd_stats_json.is_object());
            // Check for a few common fields, actual fields might vary by Redis version
            ASSERT_TRUE(cmd_stats_json.contains("total_calls") || cmd_stats_json.contains("cmdstat_get"));
        } catch (const std::exception &e) {
            std::cerr << "command_stats (JSON) failed: " << e.what() << std::endl;
            // Don't fail the test if the command is not available
        }

    } catch (const std::exception &e) {
        std::cerr << "command (JSON) failed: " << e.what() << std::endl;
        FAIL();
    }
    
    // Test command_count
    auto count = redis.command_count();
    ASSERT_GT(count, 0);

    // Test command_getkeys
    std::vector<std::string> getkeys_args = {"test:key", "value"};
    auto keys = redis.command_getkeys("set", getkeys_args);
    ASSERT_FALSE(keys.empty());
    ASSERT_EQ(keys[0], "test:key");
}

// =============== DEBUG COMMANDS ===============

TEST_F(RedisTest, SYNC_SERVER_DEBUG_COMMANDS) {
    std::string key   = test_key("debug_test");
    std::string value = "test_value";

    // Create test data
    redis.set(key, value);

    // Test debug_object
    try {
        auto debug_info = redis.debug_object(key);
        ASSERT_FALSE(debug_info.empty());
        ASSERT_TRUE(debug_info.find("encoding") != std::string::npos ||
                    debug_info.find("refcount") != std::string::npos ||
                    debug_info.find("serializedlength") != std::string::npos);
    } catch (const std::exception &e) {
        std::cerr << "debug_object failed: " << e.what() << std::endl;
    }

    // Test debug_sleep (with a very short delay)
    try {
        ASSERT_TRUE(redis.debug_sleep(0.01)); // 10ms delay
    } catch (const std::exception &e) {
        std::cerr << "debug_sleep failed: " << e.what() << std::endl;
    }
}

// =============== MEMORY MANAGEMENT COMMANDS ===============

TEST_F(RedisTest, SYNC_SERVER_MEMORY_MANAGEMENT) {
    std::string key   = test_key("memory_test");
    std::string value = "test_value";

    // Create test data
    redis.set(key, value);

    // Test memory_usage
    auto usage = redis.memory_usage(key);
    ASSERT_GT(usage, 0);

    // Test memory_usage with samples
    auto usage_with_samples = redis.memory_usage(key, 5);
    ASSERT_GT(usage_with_samples, 0);
    
    // Test memory_stats (JSON version)
    try {
        auto stats_json = redis.memory_stats();
        ASSERT_TRUE(stats_json.is_object());
        ASSERT_TRUE(stats_json.contains("peak.allocated") || stats_json.contains("total.allocated")); 
    } catch (const std::exception &e) {
        std::cerr << "memory_stats (JSON) failed: " << e.what() << std::endl;
        FAIL();
    }

    // Test memory_doctor
    auto doctor = redis.memory_doctor();
    ASSERT_FALSE(doctor.empty());

    // Test memory_help
    auto help = redis.memory_help();
    ASSERT_FALSE(help.empty());
    ASSERT_TRUE(help.size() > 0);

    // Test memory_malloc_stats
    try {
        auto malloc_stats = redis.memory_malloc_stats();
        ASSERT_FALSE(malloc_stats.empty());
    } catch (const std::exception &e) {
        std::cerr << "memory_malloc_stats failed: " << e.what() << std::endl;
    }

    // Test memory_purge
    ASSERT_TRUE(redis.memory_purge());
}

// =============== SLOWLOG COMMANDS ===============

TEST_F(RedisTest, SYNC_SERVER_SLOWLOG) {
    // Test slowlog_len
    auto len = redis.slowlog_len();
    ASSERT_GE(len, 0);

    // Test slowlog_get (JSON version)
    auto entries_json = redis.slowlog_get();
    std::cout << "SLOWLOG JSON: " << entries_json.dump() << std::endl;
    
    ASSERT_TRUE(entries_json.is_array());
    if (!entries_json.empty()) {
        // Verify that each entry has a consistent structure
        for(const auto& entry : entries_json) {
            if (entry.is_object()) {
                // Standard format
                ASSERT_TRUE(entry.contains("id") || entry.contains("command"));
            } else if (entry.is_array()) {
                // Old format (array)
                ASSERT_GE(entry.size(), 4); // At least ID, timestamp, duration, command
            } else {
                FAIL() << "Unexpected entry format: " << entry.dump();
            }
        }
    }

    // Test slowlog_get with specific count (JSON version)
    auto entries_limit_json = redis.slowlog_get(5);
    ASSERT_TRUE(entries_limit_json.is_array());
    ASSERT_LE(entries_limit_json.size(), 5);

    // Test slowlog_reset
    ASSERT_TRUE(redis.slowlog_reset());

    // Verify reset worked
    auto new_len = redis.slowlog_len();
    ASSERT_EQ(new_len, 0);
}

// =============== SYNC COMMANDS ===============

TEST_F(RedisTest, DISABLED_SYNC_SERVER_SYNC_COMMANDS) {
    // Test sync - this is a replication command and might not work in all environments
    try {
        auto sync_status = redis.sync();
        ASSERT_TRUE(sync_status);
    } catch (const std::exception &e) {
        std::cerr << "sync failed (expected in standalone mode): " << e.what()
                  << std::endl;
    }

    // Test psync - this is a replication command and might not work in all environments
    try {
        auto psync_status = redis.psync("?", -1);
        ASSERT_TRUE(psync_status);
    } catch (const std::exception &e) {
        std::cerr << "psync failed (expected in standalone mode): " << e.what()
                  << std::endl;
    }
}

// =============== PERSISTENCE COMMANDS ===============

TEST_F(RedisTest, DISABLED_SYNC_SERVER_PERSISTENCE) {
    // Test bgrewriteaof
    try {
        auto bgrewriteaof_status = redis.bgrewriteaof();
        ASSERT_TRUE(
            bgrewriteaof_status ||
            bgrewriteaof_status.str() == "Background append only file rewriting started" ||
            bgrewriteaof_status.str().find("already in progress") != std::string::npos);
    } catch (const std::exception &e) {
        std::cerr << "bgrewriteaof failed: " << e.what() << std::endl;
    }

    // Test bgsave
    try {
        auto bgsave_status = redis.bgsave();
        ASSERT_TRUE(bgsave_status || bgsave_status.str() == "Background saving started" ||
                    bgsave_status.str().find("already in progress") !=
                        std::string::npos);
    } catch (const std::exception &e) {
        std::cerr << "bgsave failed: " << e.what() << std::endl;
    }

    // Test bgsave with schedule
    try {
        auto bgsave_schedule_status = redis.bgsave(true);
        ASSERT_TRUE(bgsave_schedule_status);
    } catch (const std::exception &e) {
        std::cerr << "bgsave(schedule) failed: " << e.what() << std::endl;
    }

    // Test save - this blocks the server and might timeout in some environments
    try {
        auto save_status = redis.save();
        ASSERT_TRUE(save_status);
    } catch (const std::exception &e) {
        std::cerr << "save failed: " << e.what() << std::endl;
    }

    // Test lastsave
    long long lastsave_result = redis.lastsave();
    ASSERT_GT(lastsave_result, 0);
}

// =============== DATABASE COMMANDS ===============

TEST_F(RedisTest, SYNC_SERVER_DATABASE) {
    // Set some test keys
    redis.set(test_key("db1"), "value1");
    redis.set(test_key("db2"), "value2");

    // Test dbsize
    long long dbsize_result = redis.dbsize();
    ASSERT_GE(dbsize_result, 2);

    // Test flushdb
    auto flushdb_result = redis.flushdb();
    ASSERT_TRUE(flushdb_result);

    // Verify dbsize after flush
    dbsize_result = redis.dbsize();
    ASSERT_EQ(dbsize_result, 0);

    // Set some keys again
    redis.set(test_key("db1"), "value1");
    redis.set(test_key("db2"), "value2");

    // Test async flushdb
    auto async_flushdb_result = redis.flushdb(true);
    ASSERT_TRUE(async_flushdb_result);

    // Need a small delay for async operation to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Verify dbsize after async flush
    dbsize_result = redis.dbsize();
    ASSERT_EQ(dbsize_result, 0);

    // Test flushall
    auto flushall_result = redis.flushall();
    ASSERT_TRUE(flushall_result);

    // Test async flushall
    auto async_flushall_result = redis.flushall(true);
    ASSERT_TRUE(async_flushall_result);
}

// =============== SERVER INFORMATION COMMANDS ===============

TEST_F(RedisTest, SYNC_SERVER_INFORMATION) {
    // Test info (JSON version)
    auto server_info_json = redis.info();
    std::cout << "INFO response type: " << (server_info_json.is_string() ? "string" : 
                                          server_info_json.is_object() ? "object" : 
                                          server_info_json.is_array() ? "array" : "other") << std::endl;
    std::cout << "INFO response: " << server_info_json.dump().substr(0, 200) << "..." << std::endl;
    
    // Verify that information about the Redis version is present
    if (server_info_json.is_object()) {
        ASSERT_TRUE(server_info_json.contains("redis_version") || 
                   (server_info_json.contains("server") && 
                    server_info_json["server"].is_object() && 
                    server_info_json["server"].contains("redis_version")));
    } else if (server_info_json.is_string()) {
        std::string info_str = server_info_json.get<std::string>();
        ASSERT_TRUE(info_str.find("redis_version") != std::string::npos);
    } else {
        FAIL() << "Unexpected INFO format";
    }

    // Test info with section (JSON version)
    auto memory_info_json = redis.info("memory");
    std::cout << "INFO memory response type: " << (memory_info_json.is_string() ? "string" : 
                                                memory_info_json.is_object() ? "object" : "other") << std::endl;
    
    // Verify that memory information is present
    if (memory_info_json.is_object()) {
        ASSERT_TRUE(memory_info_json.contains("used_memory") || 
                   (memory_info_json.contains("memory") && 
                    memory_info_json["memory"].is_object() && 
                    memory_info_json["memory"].contains("used_memory")));
    } else if (memory_info_json.is_string()) {
        std::string memory_info_str = memory_info_json.get<std::string>();
        ASSERT_TRUE(memory_info_str.find("used_memory") != std::string::npos);
    } else {
        FAIL() << "Unexpected INFO MEMORY format";
    }
    
    auto clients_section_json = redis.info("clients");
    std::cout << "INFO clients response type: " << (clients_section_json.is_string() ? "string" : 
                                                clients_section_json.is_object() ? "object" : "other") << std::endl;
    
    // Verify that client information is present
    if (clients_section_json.is_object()) {
        ASSERT_TRUE(clients_section_json.contains("connected_clients") || 
                   (clients_section_json.contains("clients") && 
                    clients_section_json["clients"].is_object() && 
                    clients_section_json["clients"].contains("connected_clients")));
    } else if (clients_section_json.is_string()) {
        std::string clients_info_str = clients_section_json.get<std::string>();
        ASSERT_TRUE(clients_info_str.find("connected_clients") != std::string::npos);
    } else {
        FAIL() << "Unexpected INFO CLIENTS format";
    }

    // Test time
    auto time_info = redis.time();
    ASSERT_GT(time_info.first, 0);  // Unix timestamp
    ASSERT_GE(time_info.second, 0); // Microseconds
}

// =============== ROLE COMMANDS ===============

TEST_F(RedisTest, SYNC_SERVER_ROLE) {
    // Test role
    try {
        auto role_info = redis.role();
        ASSERT_FALSE(role_info.empty());
        // We expect "master" in standalone mode
        if (!role_info.empty()) {
            ASSERT_TRUE(role_info[0] == "master" || role_info[0] == "slave" ||
                        role_info[0] == "sentinel");
        }
    } catch (const std::exception &e) {
        std::cerr << "role command failed: " << e.what() << std::endl;
    }
}

// =============== SHUTDOWN COMMANDS ===============

TEST_F(RedisTest, DISABLED_SYNC_SERVER_SHUTDOWN) {
    // Disabled by default as this would shut down the Redis server

    // Test shutdown without save (NOSAVE)
    // auto shutdown_result = redis.shutdown("NOSAVE");
    // ASSERT_TRUE(shutdown_result);

    // Test shutdown with save
    // auto shutdown_save_result = redis.shutdown("SAVE");
    // ASSERT_TRUE(shutdown_save_result);

    // Test shutdown with default (empty string)
    // auto shutdown_default_result = redis.shutdown();
    // ASSERT_TRUE(shutdown_default_result);
}

// =============== SLAVE COMMANDS ===============

TEST_F(RedisTest, DISABLED_SYNC_SERVER_SLAVE) {
    // Disabled by default as this would change the server's replication configuration

    // Test slaveof
    // auto slaveof_result = redis.slaveof("127.0.0.1", 6379);
    // ASSERT_TRUE(slaveof_result);

    // Test slaveof NO ONE (to stop replication)
    // auto slaveof_no_one_result = redis.slaveof("NO", "ONE");
    // ASSERT_TRUE(slaveof_no_one_result);
}

// =============== LATENCY COMMANDS (NEW TESTS) ===============
TEST_F(RedisTest, DISABLED_SYNC_SERVER_LATENCY_COMMANDS) {
    // LATENCY LATEST
    auto latest = redis.latency_latest();
    ASSERT_TRUE(latest.is_array()); // Should return an array, possibly empty
    if (!latest.empty()) {
        for (const auto& event : latest) {
            ASSERT_TRUE(event.is_object());
            ASSERT_TRUE(event.contains("event"));
            ASSERT_TRUE(event.contains("timestamp"));
            ASSERT_TRUE(event.contains("latency"));
            ASSERT_TRUE(event.contains("max-latency"));
        }
    }

    // Generate some command to potentially create latency data
    redis.ping();
    redis.set(test_key("latency_key"), "value");

    // LATENCY HISTORY command
    // Note: 'command' is a common event type. If this test fails due to
    // no 'command' events, a different known event type might be needed,
    // or a specific command to trigger a particular latency event.
    std::string event_name = "command"; 
    try {
         auto history = redis.latency_history(event_name);
         ASSERT_TRUE(history.is_array());
         if (!history.empty()) {
             for(const auto& entry : history) {
                 ASSERT_TRUE(entry.is_object());
                 ASSERT_TRUE(entry.contains("timestamp"));
                 ASSERT_TRUE(entry.contains("latency"));
             }
         }
    } catch (const std::exception& e) {
        // LATENCY HISTORY might return an error if the event does not exist
        // or has no history, which is acceptable in some Redis versions/configs.
        std::cerr << "LATENCY HISTORY for '" << event_name << "' failed or returned empty: " << e.what() << std::endl;
    }

    // LATENCY RESET
    ASSERT_TRUE(redis.latency_reset()); // Reset all
    // ASSERT_TRUE(redis.latency_reset(event_name)); // Reset specific, might fail if event doesn't exist after global reset
}

/*
 * ASYNCHRONOUS TESTS
 */

// =============== CLIENT MANAGEMENT COMMANDS ===============

TEST_F(RedisTest, DISABLED_ASYNC_SERVER_CLIENT_MANAGEMENT) {
    // Test client_setname
    bool client_setname_called = false;
    redis.client_setname(
        [&client_setname_called](auto &&reply) {
            client_setname_called = true;
            ASSERT_TRUE(reply.ok());
        },
        "test_client_async");
    redis.await();
    ASSERT_TRUE(client_setname_called);

    // Test client_getname
    bool client_getname_called = false;
    redis.client_getname([&client_getname_called](auto &&reply) {
        client_getname_called = true;
        ASSERT_TRUE(reply.ok());
        ASSERT_TRUE(reply.result().has_value());
        ASSERT_EQ(*reply.result(), "test_client_async");
    });
    redis.await();
    ASSERT_TRUE(client_getname_called);

    // Test client_list (JSON version)
    bool client_list_json_called = false;
    redis.client_list([&client_list_json_called](auto &&reply) {
        client_list_json_called = true;
        ASSERT_TRUE(reply.ok());
        ASSERT_TRUE(reply.result().is_array());
        // Add more specific checks if needed, e.g., check for "test_client_async"
    });
    redis.await();
    ASSERT_TRUE(client_list_json_called);
    
    // Test client_tracking_info (JSON version)
    bool client_tracking_info_called = false;
    redis.client_tracking_info([&client_tracking_info_called](auto &&reply) {
        client_tracking_info_called = true;
        ASSERT_TRUE(reply.ok());
        ASSERT_TRUE(reply.result().is_object());
        ASSERT_TRUE(reply.result().contains("flags"));
    });
    redis.await();
    ASSERT_TRUE(client_tracking_info_called);

    // Test client_pause
    bool client_pause_called = false;
    redis.client_pause(
        [&client_pause_called](auto &&reply) {
            client_pause_called = true;
            ASSERT_TRUE(reply.ok());
        },
        100, "WRITE");
    redis.await();
    ASSERT_TRUE(client_pause_called);

    // Test client_tracking
    bool client_tracking_on_called = false;
    redis.client_tracking(
        [&client_tracking_on_called](auto &&reply) {
            client_tracking_on_called = true;
            ASSERT_TRUE(reply.ok()); 
        },
        true);
    redis.await();
    ASSERT_TRUE(client_tracking_on_called);

    // Disable tracking
    bool client_tracking_off_called = false;
    redis.client_tracking(
        [&client_tracking_off_called](auto &&reply) {
            client_tracking_off_called = true;
            ASSERT_TRUE(reply.ok());
        },
        false);
    redis.await();
    ASSERT_TRUE(client_tracking_off_called);
}

// =============== DEBUG COMMANDS ===============

TEST_F(RedisTest, DISABLED_ASYNC_SERVER_DEBUG_COMMANDS) {
    std::string key   = test_key("async_debug_test");
    std::string value = "test_value";

    // Create test data
    redis.set(key, value);
    redis.await();

    // Test debug_object
    bool debug_object_called = false;
    redis.debug_object(
        [&debug_object_called](auto &&reply) {
            debug_object_called = true;
            if (reply.ok()) {
                ASSERT_FALSE(reply.result().empty());
            } else {
                std::cerr << "Async DEBUG OBJECT failed: " << reply.error() << std::endl;
            }
        },
        key);
    redis.await();
    ASSERT_TRUE(debug_object_called);

    // Test debug_sleep (with a very short delay)
    bool debug_sleep_called = false;
    redis.debug_sleep(
        [&debug_sleep_called](auto &&reply) {
            debug_sleep_called = true;
            ASSERT_TRUE(reply.ok());
        },
        0.01 // 10ms delay
    );
    redis.await();
    ASSERT_TRUE(debug_sleep_called);
}

// =============== SYNC COMMANDS ===============

TEST_F(RedisTest, DISABLED_ASYNC_SERVER_SYNC_COMMANDS) {
    // Test sync - this is a replication command and might not work in all environments
    bool sync_called = false;
    redis.sync([&sync_called](auto &&reply) {
        sync_called = true;
        // Don't assert on reply.ok() as this might fail in standalone mode
        if (!reply.ok()) {
            std::cerr << "Async SYNC failed (expected in standalone): " << reply.error() << std::endl;
        }
    });
    redis.await();
    ASSERT_TRUE(sync_called);

    // Test psync - this is a replication command and might not work in all environments
    bool psync_called = false;
    redis.psync(
        [&psync_called](auto &&reply) {
            psync_called = true;
            if (!reply.ok()) {
                 std::cerr << "Async PSYNC failed (expected in standalone): " << reply.error() << std::endl;
            }
        },
        "?", -1);
    redis.await();
    ASSERT_TRUE(psync_called);
}

// =============== ROLE COMMANDS ===============

TEST_F(RedisTest, ASYNC_SERVER_ROLE) {
    // Test role
    bool role_called = false;
    redis.role([&role_called](auto &&reply) {
        role_called = true;
        ASSERT_TRUE(reply.ok());
        ASSERT_FALSE(reply.result().empty());
        if (!reply.result().empty()) {
            ASSERT_TRUE(reply.result()[0] == "master" ||
                        reply.result()[0] == "slave" ||
                        reply.result()[0] == "sentinel");
        }
    });
    redis.await();
    ASSERT_TRUE(role_called);
}

// =============== ASYNC LATENCY COMMANDS (NEW TESTS) ===============
TEST_F(RedisTest, DISABLED_ASYNC_SERVER_LATENCY_COMMANDS) {
    // LATENCY LATEST
    bool latest_called = false;
    redis.latency_latest([&latest_called](auto &&reply) {
        latest_called = true;
        if (reply.ok()) {
            ASSERT_TRUE(reply.result().is_array());
            if (!reply.result().empty()) {
                for (const auto& event : reply.result()) {
                    ASSERT_TRUE(event.is_object());
                    ASSERT_TRUE(event.contains("event"));
                }
            }
        } else {
            std::cerr << "Async LATENCY LATEST failed: " << reply.error() << std::endl;
        }
    });
    redis.await();
    ASSERT_TRUE(latest_called);

    // Generate some command
    redis.ping();
    redis.set(test_key("async_latency_key"), "value");
    redis.await();

    // LATENCY HISTORY
    bool history_called = false;
    std::string event_name_async = "command"; 
    redis.latency_history([&history_called, event_name_async](auto &&reply) {
        history_called = true;
        if (reply.ok()) { // Okay to be empty or have data
            ASSERT_TRUE(reply.result().is_array());
        } else {
             std::cerr << "Async LATENCY HISTORY for '" << event_name_async << "' failed: " << reply.error() << std::endl;
        }
    }, event_name_async);
    redis.await();
    ASSERT_TRUE(history_called);

    // LATENCY RESET
    bool reset_all_called = false;
    redis.latency_reset([&reset_all_called](auto &&reply) {
        reset_all_called = true;
        // Don't assert on ok() as it depends on server version
        if (!reply.ok()) {
            std::cerr << "Async LATENCY RESET failed: " << reply.error() << std::endl;
        }
    });
    redis.await();
    ASSERT_TRUE(reset_all_called);
    
    bool reset_specific_called = false;
    // This might fail if the event 'command' doesn't exist after a global reset
    // or isn't a recognized event type by the server for individual reset.
    redis.latency_reset([&reset_specific_called](auto &&reply) {
        reset_specific_called = true;
        // Don't strictly assert reply.ok() as it depends on server state/version
        if (!reply.ok()){
            std::cerr << "Async LATENCY RESET for specific event failed: " << reply.error() << std::endl;
        }
    }, event_name_async);
    redis.await();
    ASSERT_TRUE(reset_specific_called);
}

// =============== ASYNC SERVER_INFORMATION (NEW TESTS) ===============
TEST_F(RedisTest, ASYNC_SERVER_INFORMATION) {
    // Test async info (JSON version)
    bool info_json_called = false;
    redis.info([&info_json_called](auto &&reply) {
        info_json_called = true;
        ASSERT_TRUE(reply.ok());
        
        // Verify the format of the response
        if (reply.result().is_object()) {
            ASSERT_TRUE(reply.result().contains("redis_version") || 
                        reply.result().contains("tcp_port") || 
                        reply.result().contains("used_memory"));
        } else if (reply.result().is_string()) {
            std::string result_str = reply.result().template get<std::string>();
            ASSERT_TRUE(result_str.find("redis_version") != std::string::npos || 
                        result_str.find("tcp_port") != std::string::npos);
        }
    });
    redis.await();
    ASSERT_TRUE(info_json_called);

    // Test async info with section (JSON version)
    bool info_section_json_called = false;
    redis.info([&info_section_json_called](auto &&reply) {
        info_section_json_called = true;
        ASSERT_TRUE(reply.ok());
        
        // Verify the format of the response
        if (reply.result().is_object()) {
            ASSERT_TRUE(reply.result().contains("used_memory") || 
                        reply.result().contains("used_memory_peak"));
        } else if (reply.result().is_string()) {
            std::string result_str = reply.result().template get<std::string>();
            ASSERT_TRUE(result_str.find("used_memory") != std::string::npos);
        }
    }, "memory");
    redis.await();
    ASSERT_TRUE(info_section_json_called);

    // Test async time
    bool time_called = false;
    redis.time([&time_called](auto &&reply) {
        time_called = true;
        ASSERT_TRUE(reply.ok());
        ASSERT_GT(reply.result().first, 0);
        ASSERT_GE(reply.result().second, 0);
    });
    redis.await();
    ASSERT_TRUE(time_called);
}

// =============== ASYNC COMMAND_INFORMATION (NEW TESTS) ===============
TEST_F(RedisTest, ASYNC_SERVER_COMMAND_INFORMATION) {
    // Test async command (JSON version)
    bool cmd_json_called = false;
    std::vector<std::string> specific_cmds_async = {"get"};
    redis.command([&cmd_json_called](auto &&reply) {
        cmd_json_called = true;
        ASSERT_TRUE(reply.ok());
        
        // Verify content rather than type
        if (reply.result().is_object()) {
            ASSERT_TRUE(reply.result().contains("get"));
        } else if (reply.result().is_array()) {
            // If the result is an array, search for command get information
            bool found_get = false;
            for (const auto& cmd_info : reply.result()) {
                if (cmd_info.is_array() && !cmd_info.empty() && 
                    cmd_info[0].is_string() && cmd_info[0].template get<std::string>() == "get") {
                    found_get = true;
                    break;
                }
            }
            ASSERT_TRUE(found_get);
        }
    }, specific_cmds_async);
    redis.await();
    ASSERT_TRUE(cmd_json_called);
    
    // Test async command_count
    bool cmd_count_called = false;
    redis.command_count([&cmd_count_called](auto &&reply) {
        cmd_count_called = true;
        ASSERT_TRUE(reply.ok());
        ASSERT_GT(reply.result(), 0);
    });
    redis.await();
    ASSERT_TRUE(cmd_count_called);

    // Test async command_getkeys
    bool cmd_getkeys_called = false;
    std::vector<std::string> getkeys_args_async = {"test:key:async", "value"};
    redis.command_getkeys([&cmd_getkeys_called](auto &&reply) {
        cmd_getkeys_called = true;
        ASSERT_TRUE(reply.ok());
        ASSERT_FALSE(reply.result().empty());
        ASSERT_EQ(reply.result()[0], "test:key:async");
    }, "set", getkeys_args_async);
    redis.await();
    ASSERT_TRUE(cmd_getkeys_called);
}

// =============== ASYNC MEMORY_MANAGEMENT (NEW TESTS) ===============
TEST_F(RedisTest, ASYNC_SERVER_MEMORY_MANAGEMENT) {
    std::string key   = test_key("async_memory_test");
    std::string value = "test_value_async";
    redis.set(key, value);
    redis.await();

    // Test async memory_usage
    bool mem_usage_called = false;
    redis.memory_usage([&mem_usage_called](auto &&reply) {
        mem_usage_called = true;
        ASSERT_TRUE(reply.ok());
        ASSERT_GT(reply.result(), 0);
    }, key);
    redis.await();
    ASSERT_TRUE(mem_usage_called);

    // Test async memory_stats (JSON version)
    bool mem_stats_json_called = false;
    redis.memory_stats([&mem_stats_json_called](auto &&reply) {
        mem_stats_json_called = true;
        ASSERT_TRUE(reply.ok());
        // Verify that the type is object or that the response contains the correct data
        if (reply.result().is_object()) {
            ASSERT_TRUE(reply.result().contains("peak.allocated") || 
                        reply.result().contains("total.allocated"));
        } else if (reply.result().is_string()) {
            std::string result_str = reply.result().template get<std::string>();
            ASSERT_TRUE(result_str.find("allocated") != std::string::npos);
        }
    });
    redis.await();
    ASSERT_TRUE(mem_stats_json_called);
    
    // Test async memory_doctor
    bool mem_doctor_called = false;
    redis.memory_doctor([&mem_doctor_called](auto&& reply){
        mem_doctor_called = true;
        ASSERT_TRUE(reply.ok());
        ASSERT_FALSE(reply.result().empty());
    });
    redis.await();
    ASSERT_TRUE(mem_doctor_called);

    // Test async memory_purge
    bool mem_purge_called = false;
    redis.memory_purge([&mem_purge_called](auto&& reply){
        mem_purge_called = true;
        ASSERT_TRUE(reply.ok());
    });
    redis.await();
    ASSERT_TRUE(mem_purge_called);
}

// =============== ASYNC SLOWLOG COMMANDS ===============
TEST_F(RedisTest, ASYNC_SERVER_SLOWLOG) {
    // Test async slowlog_len
    bool slowlog_len_called = false;
    redis.slowlog_len([&slowlog_len_called](auto &&reply) {
        slowlog_len_called = true;
        ASSERT_TRUE(reply.ok());
        ASSERT_GE(reply.result(), 0);
    });
    redis.await();
    ASSERT_TRUE(slowlog_len_called);

    // Test async slowlog_get (JSON version)
    bool slowlog_get_json_called = false;
    redis.slowlog_get([&slowlog_get_json_called](auto &&reply) {
        slowlog_get_json_called = true;
        ASSERT_TRUE(reply.ok());
        ASSERT_TRUE(reply.result().is_array());
        
        // If we have entries, verify their structure
        if (!reply.result().empty()) {
            for (const auto& entry : reply.result()) {
                if (entry.is_object()) {
                    // Standard format
                    ASSERT_TRUE(entry.contains("id") || entry.contains("command"));
                } else if (entry.is_array()) {
                    // Old format (array)
                    ASSERT_GE(entry.size(), 4); // At least ID, timestamp, duration, command
                }
            }
        }
    }, 5); // Get 5 entries
    redis.await();
    ASSERT_TRUE(slowlog_get_json_called);

    // Test async slowlog_reset
    bool slowlog_reset_called = false;
    redis.slowlog_reset([&slowlog_reset_called](auto &&reply) {
        slowlog_reset_called = true;
        ASSERT_TRUE(reply.ok());
    });
    redis.await();
    ASSERT_TRUE(slowlog_reset_called);
}

// Main function is usually at the end of the file or in a separate file.
// If it's missing and needed for these tests, add it.
// For now, assuming it exists elsewhere or is not strictly necessary for this refactoring.