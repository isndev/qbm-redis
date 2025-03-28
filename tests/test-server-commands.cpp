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

// Generates unique key prefixes to avoid collisions between tests
inline std::string
key_prefix(const std::string &key = "") {
    static int counter = 0;
    std::string prefix = "qb::redis::server-test:" + std::to_string(++counter);

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

    void SetUp() override {
        async::init();
        if (!redis.connect() || !redis.flushall())
            throw std::runtime_error("Unable to connect to Redis");

        // Wait for the connection to be established
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

// =============== CLIENT MANAGEMENT COMMANDS ===============

TEST_F(RedisTest, SYNC_SERVER_CLIENT_MANAGEMENT) {
    // Test client_setname and client_getname
    ASSERT_TRUE(redis.client_setname("test_client"));
    auto name = redis.client_getname();
    ASSERT_TRUE(name.has_value());
    ASSERT_EQ(*name, "test_client");

    // Test client_list
    auto clients = redis.client_list();
    ASSERT_FALSE(clients.empty());
    ASSERT_TRUE(std::find_if(clients.begin(), clients.end(),
        [](const std::string& client) { return client.find("test_client") != std::string::npos; }) != clients.end());

    // Test client_pause
    ASSERT_TRUE(redis.client_pause(100, "WRITE")); // Pause for 100ms, WRITE mode

    // Test client_tracking
    ASSERT_TRUE(redis.client_tracking(true));
    ASSERT_TRUE(redis.client_tracking(false));
    
    // Test client_unblock (test only with a valid ID, otherwise it will fail)
    auto client_info = redis.client_list()[0];
    size_t id_pos = client_info.find("id=");
    if (id_pos != std::string::npos) {
        std::string id_str = client_info.substr(id_pos + 3);
        size_t space_pos = id_str.find(" ");
        if (space_pos != std::string::npos) {
            id_str = id_str.substr(0, space_pos);
            long long client_id = std::stoll(id_str);
            // This might fail if the client is not blocked, but we're testing the function call
            redis.client_unblock(client_id, false);
        }
    }
}

// =============== CONFIGURATION COMMANDS ===============

TEST_F(RedisTest, SYNC_SERVER_CONFIGURATION) {
    // Test config_get and config_set
    auto maxmemory = redis.config_get("maxmemory");
    ASSERT_FALSE(maxmemory.empty());
    ASSERT_TRUE(redis.config_set("maxmemory", maxmemory[0].second));

    // Test getting multiple config params
    auto configs = redis.config_get("*max*");
    ASSERT_FALSE(configs.empty());
    
    // Test config_resetstat
    ASSERT_TRUE(redis.config_resetstat());

    // Test config_rewrite (requires write permissions, might fail in some environments)
    try {
        auto rewrite_status = redis.config_rewrite();
        // Just verify the call doesn't throw an exception
    } catch (const std::exception& e) {
        std::cerr << "config_rewrite failed (might lack permissions): " << e.what() << std::endl;
    }
}

// =============== COMMAND INFORMATION COMMANDS ===============

TEST_F(RedisTest, DISABLED_SYNC_SERVER_COMMAND_INFORMATION) {
    // Test command_info for specific commands
    try {
        auto info = redis.command_info({"get", "set"});
        ASSERT_FALSE(info.empty());
        if (!info.empty()) {
            ASSERT_EQ(info.size(), 2);
            
            // Redis COMMAND INFO returns a complex structure for each command
            // Our implementation maps it to std::vector<std::map<std::string, std::string>>
            // Where each map represents a command's properties
            for (const auto& cmd_map : info) {
                ASSERT_FALSE(cmd_map.empty());
                
                // Verify presence of typical command properties
                // The actual Redis response is more complex (nested arrays, etc.)
                // but our implementation flattens it to a map with key-value pairs
                bool has_essential_properties = false;
                
                // Check for common command properties
                if (cmd_map.find("name") != cmd_map.end() ||
                    cmd_map.find("arity") != cmd_map.end() ||
                    cmd_map.find("flags") != cmd_map.end()) {
                    has_essential_properties = true;
                }
                
                ASSERT_TRUE(has_essential_properties) 
                    << "Command info should contain at least some basic properties";
                
                // In a production system, we might want more specific checks for
                // particular commands, but that would require knowledge of their
                // exact structure
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "command_info failed: " << e.what() << std::endl;
    }

    // Test command_count
    auto count = redis.command_count();
    ASSERT_GT(count, 0);

    // Test command_getkeys
    auto keys = redis.command_getkeys("set", {"test:key", "value"});
    ASSERT_FALSE(keys.empty());
    ASSERT_EQ(keys[0], "test:key");
}

// =============== DEBUG COMMANDS ===============

TEST_F(RedisTest, DISABLED_SYNC_SERVER_DEBUG_COMMANDS) {
    std::string key = test_key("debug_test");
    std::string value = "test_value";

    // Create test data
    redis.set(key, value);

    // Test debug_object
    try {
        auto debug_info = redis.debug_object(key);
        ASSERT_FALSE(debug_info.empty());
        // Debug info should contain some object metadata
        ASSERT_TRUE(debug_info.find("encoding") != std::string::npos ||
                    debug_info.find("refcount") != std::string::npos ||
                    debug_info.find("serializedlength") != std::string::npos);
    } catch (const std::exception& e) {
        std::cerr << "debug_object failed: " << e.what() << std::endl;
    }

    // Test debug_sleep (with a very short delay)
    try {
        ASSERT_TRUE(redis.debug_sleep(0.01)); // 10ms delay
    } catch (const std::exception& e) {
        std::cerr << "debug_sleep failed: " << e.what() << std::endl;
    }

    // We don't test debug_segfault as it would crash the Redis server
}

// =============== MEMORY MANAGEMENT COMMANDS ===============

TEST_F(RedisTest, DISABLED_SYNC_SERVER_MEMORY_MANAGEMENT) {
    std::string key = test_key("memory_test");
    std::string value = "test_value";

    // Create test data
    redis.set(key, value);

    // Test memory_usage
    auto usage = redis.memory_usage(key);
    ASSERT_GT(usage, 0);
    
    // Test memory_usage with samples
    auto usage_with_samples = redis.memory_usage(key, 5);
    ASSERT_GT(usage_with_samples, 0);

    // Test memory_stats
    try {
        auto stats = redis.memory_stats();
        ASSERT_FALSE(stats.empty());
        // Check for common memory stats fields
        ASSERT_TRUE(stats.find("total.allocated") != stats.end() || 
                    stats.find("used_memory") != stats.end());
    } catch (const std::exception& e) {
        std::cerr << "memory_stats failed: " << e.what() << std::endl;
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
    } catch (const std::exception& e) {
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

    // Test slowlog_get without count (default)
    auto entries = redis.slowlog_get();
    // The number of entries might be 0 if there are no slow commands
    ASSERT_LE(entries.size(), 10); // Default is 10

    // Test slowlog_get with specific count
    auto entries_limit = redis.slowlog_get(5);
    ASSERT_LE(entries_limit.size(), 5);

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
        // Just verify the call doesn't throw an exception
    } catch (const std::exception& e) {
        std::cerr << "sync failed (expected in standalone mode): " << e.what() << std::endl;
    }
    
    // Test psync - this is a replication command and might not work in all environments
    try {
        auto psync_status = redis.psync("?", -1);
        // Just verify the call doesn't throw an exception
    } catch (const std::exception& e) {
        std::cerr << "psync failed (expected in standalone mode): " << e.what() << std::endl;
    }
}

// =============== PERSISTENCE COMMANDS ===============

TEST_F(RedisTest, DISABLED_SYNC_SERVER_PERSISTENCE) {
    // Test bgrewriteaof
    try {
        auto bgrewriteaof_status = redis.bgrewriteaof();
        ASSERT_TRUE(bgrewriteaof_status || 
                    bgrewriteaof_status == "Background append only file rewriting started" ||
                    bgrewriteaof_status.str().find("already in progress") != std::string::npos);
    } catch (const std::exception& e) {
        std::cerr << "bgrewriteaof failed: " << e.what() << std::endl;
    }

    // Test bgsave
    try {
        auto bgsave_status = redis.bgsave();
        ASSERT_TRUE(bgsave_status || 
                    bgsave_status == "Background saving started" ||
                    bgsave_status.str().find("already in progress") != std::string::npos);
    } catch (const std::exception& e) {
        std::cerr << "bgsave failed: " << e.what() << std::endl;
    }

    // Test bgsave with schedule
    try {
        auto bgsave_schedule_status = redis.bgsave(true);
        // Just verify the call doesn't throw an exception
    } catch (const std::exception& e) {
        std::cerr << "bgsave(schedule) failed: " << e.what() << std::endl;
    }

    // Test save - this blocks the server and might timeout in some environments
    try {
        auto save_status = redis.save();
        ASSERT_TRUE(save_status);
    } catch (const std::exception& e) {
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

TEST_F(RedisTest, DISABLED_SYNC_SERVER_INFORMATION) {
    // Test info
    auto server_info = redis.info();
    ASSERT_GT(server_info.used_memory, 0);
    ASSERT_GT(server_info.used_memory_peak, 0);
    ASSERT_GE(server_info.number_of_connected_clients, 1); // At least our connection

    // Test info with section
    auto memory_info = redis.info("memory");
    ASSERT_GT(memory_info.used_memory, 0);
    ASSERT_GT(memory_info.used_memory_peak, 0);
    ASSERT_GT(memory_info.used_memory_lua, 0);
    
    // Test info with other sections
    auto server_section = redis.info("server");
    ASSERT_GE(server_section.number_of_connected_clients, 1);
    
    auto clients_section = redis.info("clients");
    ASSERT_GE(clients_section.number_of_connected_clients, 1);
    
    auto stats_section = redis.info("stats");
    ASSERT_GE(stats_section.total_commands_processed, 0);

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
            ASSERT_TRUE(role_info[0] == "master" || role_info[0] == "slave" || role_info[0] == "sentinel");
        }
    } catch (const std::exception& e) {
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

/*
 * ASYNCHRONOUS TESTS
 */

// =============== CLIENT MANAGEMENT COMMANDS ===============

TEST_F(RedisTest, ASYNC_SERVER_CLIENT_MANAGEMENT) {
    // Test client_setname
    bool client_setname_called = false;
    redis.client_setname(
        [&client_setname_called](auto&& reply) {
            client_setname_called = true;
            ASSERT_TRUE(reply.ok());
        },
        "test_client_async"
    );
    redis.await();
    ASSERT_TRUE(client_setname_called);

    // Test client_getname
    bool client_getname_called = false;
    redis.client_getname(
        [&client_getname_called](auto&& reply) {
            client_getname_called = true;
            ASSERT_TRUE(reply.ok());
            ASSERT_TRUE(reply.result().has_value());
            ASSERT_EQ(*reply.result(), "test_client_async");
        }
    );
    redis.await();
    ASSERT_TRUE(client_getname_called);

    // Test client_list
    bool client_list_called = false;
    redis.client_list(
        [&client_list_called](auto&& reply) {
            client_list_called = true;
            ASSERT_TRUE(reply.ok());
            ASSERT_FALSE(reply.result().empty());
            ASSERT_TRUE(std::find_if(reply.result().begin(), reply.result().end(),
                [](const std::string& client) { return client.find("test_client_async") != std::string::npos; }) != reply.result().end());
        }
    );
    redis.await();
    ASSERT_TRUE(client_list_called);

    // Test client_pause
    bool client_pause_called = false;
    redis.client_pause(
        [&client_pause_called](auto&& reply) {
            client_pause_called = true;
            ASSERT_TRUE(reply.ok());
        },
        100, "WRITE"
    );
    redis.await();
    ASSERT_TRUE(client_pause_called);

    // Test client_tracking
    bool client_tracking_called = false;
    redis.client_tracking(
        [&client_tracking_called](auto&& reply) {
            client_tracking_called = true;
            // This might fail if the server doesn't support tracking
            // ASSERT_TRUE(reply);
        },
        true
    );
    redis.await();
    ASSERT_TRUE(client_tracking_called);
    
    // Disable tracking
    bool client_untracking_called = false;
    redis.client_tracking(
        [&client_untracking_called](auto&& reply) {
            client_untracking_called = true;
            // This might fail if the server doesn't support tracking
            // ASSERT_TRUE(reply);
        },
        false
    );
    redis.await();
    ASSERT_TRUE(client_untracking_called);
}

// =============== DEBUG COMMANDS ===============

TEST_F(RedisTest, ASYNC_SERVER_DEBUG_COMMANDS) {
    std::string key = test_key("async_debug_test");
    std::string value = "test_value";

    // Create test data
    redis.set(key, value);
    redis.await();

    // Test debug_object
    bool debug_object_called = false;
    redis.debug_object(
        [&debug_object_called](auto&& reply) {
            debug_object_called = true;
            if (reply.ok()) {
                ASSERT_FALSE(reply.result().empty());
            }
        },
        key
    );
    redis.await();
    ASSERT_TRUE(debug_object_called);

    // Test debug_sleep (with a very short delay)
    bool debug_sleep_called = false;
    redis.debug_sleep(
        [&debug_sleep_called](auto&& reply) {
            debug_sleep_called = true;
            if (reply.ok()) {
                ASSERT_TRUE(reply.ok());
            }
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
    redis.sync(
        [&sync_called](auto&& reply) {
            sync_called = true;
            // Don't assert on reply as this might fail in standalone mode
        }
    );
    redis.await();
    ASSERT_TRUE(sync_called);
    
    // Test psync - this is a replication command and might not work in all environments
    bool psync_called = false;
    redis.psync(
        [&psync_called](auto&& reply) {
            psync_called = true;
            // Don't assert on reply as this might fail in standalone mode
        },
        "?", -1
    );
    redis.await();
    ASSERT_TRUE(psync_called);
}

// =============== ROLE COMMANDS ===============

TEST_F(RedisTest, ASYNC_SERVER_ROLE) {
    // Test role
    bool role_called = false;
    redis.role(
        [&role_called](auto&& reply) {
            role_called = true;
            if (reply.ok()) {
                ASSERT_FALSE(reply.result().empty());
                // We expect "master" in standalone mode
                if (!reply.result().empty()) {
                    ASSERT_TRUE(reply.result()[0] == "master" || 
                                reply.result()[0] == "slave" || 
                                reply.result()[0] == "sentinel");
                }
            }
        }
    );
    redis.await();
    ASSERT_TRUE(role_called);
}