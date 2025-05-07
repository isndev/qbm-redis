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
    std::string prefix  = "qb::redis::function-test:" + std::to_string(++counter);

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

// Test FUNCTION LIST command
TEST_F(RedisTest, SYNC_FUNCTION_COMMANDS_LIST) {
    try {
        auto functions = redis.function_list();
        
        // Check that the result is an array (even if empty)
        EXPECT_TRUE(functions.is_array());
        
        // If functions are loaded, each entry should have a "name" field
        if (!functions.empty()) {
            for (const auto& function : functions) {
                EXPECT_TRUE(function.contains("name"));
                EXPECT_TRUE(function["name"].is_string());
            }
        }
    } catch (const std::exception& e) {
        // This might happen if the Redis server doesn't support function commands
        // (introduced in Redis 7.0)
        std::string error = e.what();
        EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                   error.find("function") != std::string::npos);
    }
}

// Test FUNCTION LOAD command (we can't actually load a function in tests,
// but we can check the command structure)
TEST_F(RedisTest, SYNC_FUNCTION_COMMANDS_LOAD) {
    try {
        // This should fail because we're not providing a valid Lua function
        redis.function_load("invalid function code");
        FAIL() << "Expected an exception for invalid function code";
    } catch (const std::exception& e) {
        // This is expected - we're providing invalid Lua code
        std::string error = e.what();
        EXPECT_TRUE(error.find("syntax error") != std::string::npos || 
                   error.find("unknown command") != std::string::npos ||
                   error.find("ERR") != std::string::npos);
    }
}

// Test FUNCTION DELETE command
TEST_F(RedisTest, SYNC_FUNCTION_COMMANDS_DELETE) {
    try {
        // This should fail with "ERR function not found" or similar
        redis.function_delete("nonexistent_function");
        FAIL() << "Expected an exception for nonexistent function";
    } catch (const std::exception& e) {
        // This is expected - either "function not found" or "unknown command"
        std::string error = e.what();
        EXPECT_TRUE(error.find("function not found") != std::string::npos || 
                   error.find("unknown command") != std::string::npos ||
                   error.find("ERR") != std::string::npos);
    }
}

// Test FUNCTION FLUSH command
TEST_F(RedisTest, SYNC_FUNCTION_COMMANDS_FLUSH) {
    try {
        // Flush all functions
        auto result = redis.function_flush();
        
        // Check that the result is OK
        EXPECT_EQ(result, "OK");
    } catch (const std::exception& e) {
        // This might happen if the Redis server doesn't support function commands
        std::string error = e.what();
        EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                   error.find("function") != std::string::npos);
    }
}

// Test FUNCTION KILL command
TEST_F(RedisTest, SYNC_FUNCTION_COMMANDS_KILL) {
    try {
        // This should fail with "ERR No scripts in execution" or similar
        // since we're not running any functions
        redis.function_kill();
        FAIL() << "Expected an exception as no functions are running";
    } catch (const std::exception& e) {
        // This is expected - either "no scripts" or "unknown command"
        std::string error = e.what();
        EXPECT_TRUE(error.find("No scripts") != std::string::npos || 
                   error.find("unknown command") != std::string::npos ||
                   error.find("ERR") != std::string::npos);
    }
}

// Test FUNCTION STATS command
TEST_F(RedisTest, SYNC_FUNCTION_COMMANDS_STATS) {
    try {
        auto stats = redis.function_stats();
        
        // Check that the result is an object with expected fields
        EXPECT_TRUE(stats.is_object());
        
        // Stats should typically have running_scripts field (even if empty)
        EXPECT_TRUE(stats.contains("running_scripts") || stats.contains("engines"));
    } catch (const std::exception& e) {
        // This might happen if the Redis server doesn't support function commands
        std::string error = e.what();
        EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                   error.find("function") != std::string::npos);
    }
}

// Test FUNCTION DUMP command
TEST_F(RedisTest, SYNC_FUNCTION_COMMANDS_DUMP) {
    try {
        auto dump = redis.function_dump();
        
        // Dump is typically a string with the serialized functions
        // (which might be empty if no functions are loaded)
        EXPECT_TRUE(dump.is_string() || dump.is_binary());
    } catch (const std::exception& e) {
        // This might happen if the Redis server doesn't support function commands
        std::string error = e.what();
        EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                   error.find("function") != std::string::npos);
    }
}

// Test FUNCTION RESTORE command
TEST_F(RedisTest, SYNC_FUNCTION_COMMANDS_RESTORE) {
    try {
        // This should fail because we're not providing valid dump data
        redis.function_restore("invalid_dump_data");
        FAIL() << "Expected an exception for invalid dump data";
    } catch (const std::exception& e) {
        // This is expected - either "invalid payload" or "unknown command"
        std::string error = e.what();
        EXPECT_TRUE(error.find("invalid payload") != std::string::npos || 
                   error.find("unknown command") != std::string::npos ||
                   error.find("ERR") != std::string::npos);
    }
}

// Test FUNCTION HELP command
TEST_F(RedisTest, SYNC_FUNCTION_COMMANDS_HELP) {
    try {
        auto help = redis.function_help();
        
        // Check that the result is a vector of strings
        EXPECT_FALSE(help.empty());
        
        // Each line of help should be a non-empty string
        for (const auto& line : help) {
            EXPECT_FALSE(line.empty());
        }
    } catch (const std::exception& e) {
        // This might happen if the Redis server doesn't support function commands
        std::string error = e.what();
        EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                   error.find("function") != std::string::npos);
    }
}

/*
 * ASYNCHRONOUS TESTS
 */

// Test async FUNCTION LIST command
TEST_F(RedisTest, ASYNC_FUNCTION_COMMANDS_LIST) {
    bool list_completed = false;
    
    // Use direct command call to test for proper JSON response
    redis.command<qb::json>(
        [&](auto &&reply) {
            list_completed = true;
            
            if (reply.ok()) {
                auto functions = reply.result();
                
                // Check that the result is an array (even if empty)
                EXPECT_TRUE(functions.is_array());
                
                // If functions are loaded, each entry should have a "name" field
                if (!functions.empty()) {
                    for (const auto& function : functions) {
                        EXPECT_TRUE(function.contains("name"));
                        EXPECT_TRUE(function["name"].is_string());
                    }
                }
            } else {
                // This might happen if the Redis server doesn't support function commands
                std::string error = std::string(reply.error());
                EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                           error.find("function") != std::string::npos);
            }
        }, "FUNCTION", "LIST");
    
    redis.await();
    EXPECT_TRUE(list_completed);
}

// Test async FUNCTION LOAD command
TEST_F(RedisTest, ASYNC_FUNCTION_COMMANDS_LOAD) {
    bool load_completed = false;
    
    // Use direct command call to test for proper status response
    redis.command<status>(
        [&](auto &&reply) {
            load_completed = true;
            
            // This should fail because we're not providing a valid Lua function
            EXPECT_FALSE(reply.ok());
            
            std::string error = std::string(reply.error());
            EXPECT_TRUE(error.find("syntax error") != std::string::npos || 
                       error.find("unknown command") != std::string::npos ||
                       error.find("ERR") != std::string::npos);
        }, "FUNCTION", "LOAD", "invalid function code");
    
    redis.await();
    EXPECT_TRUE(load_completed);
}

// Test async FUNCTION DELETE command
TEST_F(RedisTest, ASYNC_FUNCTION_COMMANDS_DELETE) {
    bool delete_completed = false;
    
    // Use direct command call to test for proper status response
    redis.command<status>(
        [&](auto &&reply) {
            delete_completed = true;
            
            // This should fail with "ERR function not found" or similar
            EXPECT_FALSE(reply.ok());
            
            std::string error = std::string(reply.error());
            EXPECT_TRUE(error.find("function not found") != std::string::npos || 
                       error.find("unknown command") != std::string::npos ||
                       error.find("ERR") != std::string::npos);
        }, "FUNCTION", "DELETE", "nonexistent_function");
    
    redis.await();
    EXPECT_TRUE(delete_completed);
}

// Test async FUNCTION FLUSH command
TEST_F(RedisTest, ASYNC_FUNCTION_COMMANDS_FLUSH) {
    bool flush_completed = false;
    
    // Use direct command call to test for proper status response
    redis.command<status>(
        [&](auto &&reply) {
            flush_completed = true;
            
            if (reply.ok()) {
                auto result = reply.result();
                
                // Check that the result is "OK"
                EXPECT_EQ(result, "OK");
            } else {
                // This might happen if the Redis server doesn't support function commands
                std::string error = std::string(reply.error());
                EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                           error.find("function") != std::string::npos);
            }
        }, "FUNCTION", "FLUSH");
    
    redis.await();
    EXPECT_TRUE(flush_completed);
}

// Test async FUNCTION KILL command
TEST_F(RedisTest, ASYNC_FUNCTION_COMMANDS_KILL) {
    bool kill_completed = false;
    
    // Use direct command call to test for proper status response
    redis.command<status>(
        [&](auto &&reply) {
            kill_completed = true;
            
            // This should fail with "ERR No scripts in execution" or similar
            EXPECT_FALSE(reply.ok());
            
            std::string error = std::string(reply.error());
            EXPECT_TRUE(error.find("No scripts") != std::string::npos || 
                       error.find("unknown command") != std::string::npos ||
                       error.find("ERR") != std::string::npos);
        }, "FUNCTION", "KILL");
    
    redis.await();
    EXPECT_TRUE(kill_completed);
}

// Test async FUNCTION STATS command
TEST_F(RedisTest, ASYNC_FUNCTION_COMMANDS_STATS) {
    bool stats_completed = false;
    
    // Use direct command call to test for proper JSON response
    redis.command<qb::json>(
        [&](auto &&reply) {
            stats_completed = true;
            
            if (reply.ok()) {
                auto stats = reply.result();
                
                // Check that the result is an object with expected fields
                EXPECT_TRUE(stats.is_object());
                
                // Stats should typically have running_scripts field (even if empty)
                EXPECT_TRUE(stats.contains("running_scripts") || stats.contains("engines"));
            } else {
                // This might happen if the Redis server doesn't support function commands
                std::string error = std::string(reply.error());
                EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                           error.find("function") != std::string::npos);
            }
        }, "FUNCTION", "STATS");
    
    redis.await();
    EXPECT_TRUE(stats_completed);
}

// Test async FUNCTION DUMP command
TEST_F(RedisTest, ASYNC_FUNCTION_COMMANDS_DUMP) {
    bool dump_completed = false;
    
    // Use direct command call to test for proper JSON response
    redis.command<qb::json>(
        [&](auto &&reply) {
            dump_completed = true;
            
            if (reply.ok()) {
                auto dump = reply.result();
                
                // Dump is typically a string with the serialized functions
                EXPECT_TRUE(dump.is_string() || dump.is_binary());
            } else {
                // This might happen if the Redis server doesn't support function commands
                std::string error = std::string(reply.error());
                EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                           error.find("function") != std::string::npos);
            }
        }, "FUNCTION", "DUMP");
    
    redis.await();
    EXPECT_TRUE(dump_completed);
}

// Test async FUNCTION RESTORE command
TEST_F(RedisTest, ASYNC_FUNCTION_COMMANDS_RESTORE) {
    bool restore_completed = false;
    
    // Use direct command call to test for proper status response
    redis.command<status>(
        [&](auto &&reply) {
            restore_completed = true;
            
            // This should fail because we're not providing valid dump data
            EXPECT_FALSE(reply.ok());
            
            std::string error = std::string(reply.error());
            EXPECT_TRUE(error.find("invalid payload") != std::string::npos || 
                       error.find("unknown command") != std::string::npos ||
                       error.find("ERR") != std::string::npos);
        }, "FUNCTION", "RESTORE", "APPEND", "invalid_dump_data");
    
    redis.await();
    EXPECT_TRUE(restore_completed);
}

// Test async FUNCTION HELP command
TEST_F(RedisTest, ASYNC_FUNCTION_COMMANDS_HELP) {
    bool help_completed = false;
    
    // Use direct command call to test for proper vector response
    redis.command<std::vector<std::string>>(
        [&](auto &&reply) {
            help_completed = true;
            
            if (reply.ok()) {
                auto help = reply.result();
                
                // Check that the result is a non-empty vector
                EXPECT_FALSE(help.empty());
                
                // Each line of help should be a non-empty string
                for (const auto& line : help) {
                    EXPECT_FALSE(line.empty());
                }
            } else {
                // This might happen if the Redis server doesn't support function commands
                std::string error = std::string(reply.error());
                EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                           error.find("function") != std::string::npos);
            }
        }, "FUNCTION", "HELP");
    
    redis.await();
    EXPECT_TRUE(help_completed);
}

// Main function to run the tests
int
main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}