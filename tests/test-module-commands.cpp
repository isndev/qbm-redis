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
    std::string prefix  = "qb::redis::module-test:" + std::to_string(++counter);

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

// Test MODULE LIST command
TEST_F(RedisTest, SYNC_MODULE_COMMANDS_LIST) {
    try {
        auto modules = redis.module_list();
        
        // Check that the result is an array (even if empty)
        EXPECT_TRUE(modules.is_array());
        
        // If modules are loaded, each entry should have a "name" field
        if (!modules.empty()) {
            for (const auto& module : modules) {
                EXPECT_TRUE(module.contains("name"));
                EXPECT_TRUE(module["name"].is_string());
            }
        }
    } catch (const std::exception& e) {
        // This might happen if the Redis server doesn't support modules
        std::string error = e.what();
        EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                   error.find("module") != std::string::npos);
    }
}

// Test MODULE LOAD command (we can't actually load a module in tests,
// but we can check the command structure)
TEST_F(RedisTest, SYNC_MODULE_COMMANDS_LOAD) {
    try {
        // This should fail with "wrong number of arguments"
        // because we're not providing a real module path
        redis.module_load("");
        FAIL() << "Expected an exception because no module path was provided";
    } catch (const std::exception& e) {
        // This is expected - either "wrong number of arguments" or "unknown command"
        std::string error = e.what();
        EXPECT_TRUE(error.find("wrong number") != std::string::npos ||
                   error.find("unknown command") != std::string::npos ||
                   error.find("ERR") != std::string::npos);
    }
}

// Test MODULE UNLOAD command (we can't actually unload a module in tests,
// but we can check the command structure)
TEST_F(RedisTest, SYNC_MODULE_COMMANDS_UNLOAD) {
    try {
        // This should fail with "ERR module not loaded" or similar
        redis.module_unload("nonexistent_module");
        FAIL() << "Expected an exception for nonexistent module";
    } catch (const std::exception& e) {
        // This is expected - either "module not loaded" or "unknown command"
        std::string error = e.what();
        EXPECT_TRUE(error.find("module not loaded") != std::string::npos ||
                   error.find("unknown command") != std::string::npos ||
                   error.find("ERR") != std::string::npos);
    }
}

// Test MODULE HELP command
TEST_F(RedisTest, SYNC_MODULE_COMMANDS_HELP) {
    try {
        auto help = redis.module_help();
        
        // Check that the result is a vector of strings
        EXPECT_TRUE(!help.empty());
        
        // Each line of help should be a string
        for (const auto& line : help) {
            EXPECT_TRUE(!line.empty());
        }
    } catch (const std::exception& e) {
        // This might happen if the Redis server doesn't support module commands
        std::string error = e.what();
        EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                   error.find("module") != std::string::npos);
    }
}

/*
 * ASYNCHRONOUS TESTS
 */

// Test async MODULE LIST command
TEST_F(RedisTest, ASYNC_MODULE_COMMANDS_LIST) {
    bool list_completed = false;
    
    // Use direct command call to test for proper JSON response
    redis.command<qb::json>(
        [&](auto &&reply) {
            list_completed = true;
            
            if (reply.ok()) {
                auto modules = reply.result();
                
                // Check that the result is an array (even if empty)
                EXPECT_TRUE(modules.is_array());
                
                // If modules are loaded, each entry should have a "name" field
                if (!modules.empty()) {
                    for (const auto& module : modules) {
                        EXPECT_TRUE(module.contains("name"));
                        EXPECT_TRUE(module["name"].is_string());
                    }
                }
            } else {
                // This might happen if the Redis server doesn't support modules
                std::string error = std::string(reply.error());
                EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                           error.find("module") != std::string::npos);
            }
        }, "MODULE", "LIST");
    
    redis.await();
    EXPECT_TRUE(list_completed);
}

// Test async MODULE LOAD command
TEST_F(RedisTest, ASYNC_MODULE_COMMANDS_LOAD) {
    bool load_completed = false;
    
    // Use direct command call to test for proper status response
    redis.command<status>(
        [&](auto &&reply) {
            load_completed = true;
            
            // This should fail with "wrong number of arguments"
            // because we're not providing a real module path
            EXPECT_FALSE(reply.ok());
            
            std::string error = std::string(reply.error());
            EXPECT_TRUE(error.find("wrong number") != std::string::npos ||
                       error.find("unknown command") != std::string::npos ||
                       error.find("ERR") != std::string::npos);
        }, "MODULE", "LOAD", "");
    
    redis.await();
    EXPECT_TRUE(load_completed);
}

// Test async MODULE UNLOAD command
TEST_F(RedisTest, ASYNC_MODULE_COMMANDS_UNLOAD) {
    bool unload_completed = false;
    
    // Use direct command call to test for proper status response
    redis.command<status>(
        [&](auto &&reply) {
            unload_completed = true;
            
            // This should fail with "ERR module not loaded" or similar
            EXPECT_FALSE(reply.ok());
            
            std::string error = std::string(reply.error());
            EXPECT_TRUE(error.find("module not loaded") != std::string::npos ||
                       error.find("unknown command") != std::string::npos ||
                       error.find("ERR") != std::string::npos);
        }, "MODULE", "UNLOAD", "nonexistent_module");
    
    redis.await();
    EXPECT_TRUE(unload_completed);
}

// Test async MODULE HELP command
TEST_F(RedisTest, ASYNC_MODULE_COMMANDS_HELP) {
    bool help_completed = false;
    
    // Use direct command call to test for proper vector response
    redis.command<std::vector<std::string>>(
        [&](auto &&reply) {
            help_completed = true;
            
            if (reply.ok()) {
                auto help = reply.result();
                
                // Check that the result is a non-empty vector
                EXPECT_FALSE(help.empty());
                
                // Each line of help should be a string
                for (const auto& line : help) {
                    EXPECT_FALSE(line.empty());
                }
            } else {
                // This might happen if the Redis server doesn't support module commands
                std::string error = std::string(reply.error());
                EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                           error.find("module") != std::string::npos);
            }
        }, "MODULE", "HELP");
    
    redis.await();
    EXPECT_TRUE(help_completed);
}

// Main function to run the tests
int
main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}