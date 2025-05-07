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
#include <iostream>

// Redis Configuration
#define REDIS_URI {"tcp://localhost:6379"}

using namespace qb::io;
using namespace std::chrono;
using namespace qb::redis;

// Generates unique key prefixes to avoid collisions between tests
inline std::string
key_prefix(const std::string &key = "") {
    static int  counter = 0;
    std::string prefix  = "qb::redis::acl-test:" + std::to_string(++counter);

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

// Test ACL CAT command (list categories)
TEST_F(RedisTest, SYNC_ACL_COMMANDS_CAT) {
    try {
        // Test basic ACL CAT - expecting a vector<string>
        auto categories = redis.acl_cat();
        EXPECT_FALSE(categories.empty());
        
        // Verify common categories are present
        bool found_string_category = false;
        bool found_key_category = false;
        
        for (const auto& cat : categories) {
            if (cat == "string")
                found_string_category = true;
            if (cat == "keyspace")
                found_key_category = true;
        }
        
        EXPECT_TRUE(found_string_category);
        EXPECT_TRUE(found_key_category);
        
        // Test ACL CAT with specific category
        // Note: For specific category, using command directly to get JSON
        // since the API returns vector<string> but Redis 7+ returns an object for this
        auto commands_reply = redis.command<qb::json>("ACL", "CAT", "string");
        auto commands = commands_reply.result();
        
        // For ACL CAT with a category, Redis 7+ returns an object where each key is a command
        // and the value might be a related command or alias
        EXPECT_TRUE(commands.is_object());
        EXPECT_FALSE(commands.empty());
        
        // Look for commands we know exist in the Redis string category
        bool found_string_command = false;
        for (auto it = commands.begin(); it != commands.end(); ++it) {
            if (it.key() == "incr" || it.key() == "decr" || it.key() == "getex" || 
                it.key() == "getrange" || it.key() == "strlen") {
                found_string_command = true;
                break;
            }
        }
        
        EXPECT_TRUE(found_string_command);
    } catch (const std::exception& e) {
        // This might happen if the Redis server doesn't support ACL commands
        std::string error = e.what();
        EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                   error.find("ACL") != std::string::npos);
    }
}

// Test ACL GETUSER command
TEST_F(RedisTest, SYNC_ACL_COMMANDS_GETUSER) {
    try {
        // Test getting the default user - returns JSON object
        auto user_info = redis.acl_getuser("default");
        EXPECT_TRUE(user_info.is_object());
        
        // Verify the default user has flags
        EXPECT_TRUE(user_info.contains("flags"));
        auto flags = user_info["flags"];
        EXPECT_TRUE(flags.is_array());
        
        // Check if the default user is active (on)
        bool is_active = false;
        for (const auto& flag : flags) {
            if (flag.template get<std::string>() == "on")
                is_active = true;
        }
        
        EXPECT_TRUE(is_active);
    } catch (const std::exception& e) {
        // This might happen if the Redis server doesn't support ACL commands
        std::string error = e.what();
        EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                   error.find("ACL") != std::string::npos);
    }
}

// Test ACL LIST command
TEST_F(RedisTest, SYNC_ACL_COMMANDS_LIST) {
    try {
        // Get the list of ACL rules - returns JSON array
        auto acl_rules = redis.acl_list();
        EXPECT_TRUE(acl_rules.is_array());
        EXPECT_FALSE(acl_rules.empty());
        
        // Verify that the default user is listed
        bool found_default_user = false;
        
        for (const auto& rule : acl_rules) {
            std::string rule_str = rule.template get<std::string>();
            if (rule_str.find("user default") != std::string::npos)
                found_default_user = true;
        }
        
        EXPECT_TRUE(found_default_user);
    } catch (const std::exception& e) {
        // This might happen if the Redis server doesn't support ACL commands
        std::string error = e.what();
        EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                   error.find("ACL") != std::string::npos);
    }
}

// Test ACL LOG command
TEST_F(RedisTest, SYNC_ACL_COMMANDS_LOG) {
    try {
        // Get ACL logs without count limitation - returns JSON array
        auto logs = redis.acl_log();
        EXPECT_TRUE(logs.is_array());
        
        // Get ACL logs with count limitation
        auto limited_logs = redis.acl_log(5);
        EXPECT_TRUE(limited_logs.is_array());
        
        // If there are logs, the limited logs should have at most 5 entries
        if (!limited_logs.empty()) {
            EXPECT_LE(limited_logs.size(), 5);
        }
    } catch (const std::exception& e) {
        // This might happen if the Redis server doesn't support ACL commands
        std::string error = e.what();
        EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                   error.find("ACL") != std::string::npos);
    }
}

// Test ACL USERS command
TEST_F(RedisTest, SYNC_ACL_COMMANDS_USERS) {
    try {
        // Get the list of users - returns vector<string>
        auto users = redis.acl_users();
        EXPECT_FALSE(users.empty());
        
        // Verify that the default user exists
        bool found_default_user = false;
        
        for (const auto& user_str : users) {
            if (user_str == "default")
                found_default_user = true;
        }
        
        EXPECT_TRUE(found_default_user);
    } catch (const std::exception& e) {
        // This might happen if the Redis server doesn't support ACL commands
        std::string error = e.what();
        EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                   error.find("ACL") != std::string::npos);
    }
}

// Test ACL WHOAMI command
TEST_F(RedisTest, SYNC_ACL_COMMANDS_WHOAMI) {
    try {
        // Get the current user - returns string
        auto current_user = redis.acl_whoami();
        
        // Verify that we're using the default user
        EXPECT_EQ(current_user, "default");
    } catch (const std::exception& e) {
        // This might happen if the Redis server doesn't support ACL commands
        std::string error = e.what();
        EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                   error.find("ACL") != std::string::npos);
    }
}

// Test ACL HELP command
TEST_F(RedisTest, SYNC_ACL_COMMANDS_HELP) {
    try {
        // Get ACL help - returns vector<string>
        auto help = redis.acl_help();
        EXPECT_FALSE(help.empty());
        
        // Verify that the help contains useful information
        bool found_help_entry = false;
        
        for (const auto& line_str : help) {
            if (line_str.find("ACL") != std::string::npos)
                found_help_entry = true;
        }
        
        EXPECT_TRUE(found_help_entry);
    } catch (const std::exception& e) {
        // This might happen if the Redis server doesn't support ACL commands
        std::string error = e.what();
        EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                   error.find("ACL") != std::string::npos);
    }
}

// Test ACL GENPASS command
TEST_F(RedisTest, SYNC_ACL_COMMANDS_GENPASS) {
    try {
        // Generate a password - returns string
        auto password = redis.acl_genpass();
        EXPECT_FALSE(password.empty());
        EXPECT_GT(password.length(), 8); // Should be long enough to be secure
        
        // Test with custom bits
        auto custom_password = redis.acl_genpass(128);
        EXPECT_FALSE(custom_password.empty());
    } catch (const std::exception& e) {
        // This might happen if the Redis server doesn't support ACL commands
        std::string error = e.what();
        EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                   error.find("ACL") != std::string::npos);
    }
}

// Note: We're not testing SETUSER and DELUSER directly as they modify the ACL
// which could interfere with other tests or the server's configuration

/*
 * ASYNCHRONOUS TESTS
 */

// Test async ACL CAT command
TEST_F(RedisTest, ASYNC_ACL_COMMANDS_CAT) {
    bool cat_completed = false;

    redis.acl_cat(
        [&](auto &&reply) {
            EXPECT_TRUE(reply.ok());
            auto categories = reply.result();
            
            EXPECT_FALSE(categories.empty());
            
            // Verify common categories are present
            bool found_string_category = false;
            bool found_key_category = false;
            
            for (const auto& cat : categories) {
                if (cat == "string")
                    found_string_category = true;
                if (cat == "keyspace")
                    found_key_category = true;
            }
            
            EXPECT_TRUE(found_string_category);
            EXPECT_TRUE(found_key_category);
            
            cat_completed = true;
        });

    redis.await();
    EXPECT_TRUE(cat_completed);
    
    // Test async ACL CAT with specific category
    bool cat_string_completed = false;
    
    // Note: For specific category, using command directly to get JSON
    // since the API returns vector<string> but Redis 7+ returns an object for this
    redis.command<qb::json>(
        [&](auto &&reply) {
            EXPECT_TRUE(reply.ok());
            auto commands = reply.result();
            
            // For ACL CAT with a category, Redis 7+ returns an object where each key is a command
            // and the value might be a related command or alias
            EXPECT_TRUE(commands.is_object());
            EXPECT_FALSE(commands.empty());
            
            // Look for commands we know exist in the Redis string category
            bool found_string_command = false;
            for (auto it = commands.begin(); it != commands.end(); ++it) {
                if (it.key() == "incr" || it.key() == "decr" || it.key() == "getex" || 
                    it.key() == "getrange" || it.key() == "strlen") {
                    found_string_command = true;
                    break;
                }
            }
            
            EXPECT_TRUE(found_string_command);
            
            cat_string_completed = true;
        }, "ACL", "CAT", "string");

    redis.await();
    EXPECT_TRUE(cat_string_completed);
}

// Test async ACL GETUSER command
TEST_F(RedisTest, ASYNC_ACL_COMMANDS_GETUSER) {
    bool getuser_completed = false;

    redis.acl_getuser(
        [&](auto &&reply) {
            EXPECT_TRUE(reply.ok());
            auto user_info = reply.result();
            
            EXPECT_TRUE(user_info.is_object());
            
            // Verify the default user has flags
            EXPECT_TRUE(user_info.contains("flags"));
            auto flags = user_info["flags"];
            EXPECT_TRUE(flags.is_array());
            
            // Check if the default user is active (on)
            bool is_active = false;
            for (const auto& flag : flags) {
                if (flag.template get<std::string>() == "on")
                    is_active = true;
            }
            
            EXPECT_TRUE(is_active);
            
            getuser_completed = true;
        }, "default");

    redis.await();
    EXPECT_TRUE(getuser_completed);
}

// Test async ACL LOG command
TEST_F(RedisTest, ASYNC_ACL_COMMANDS_LOG) {
    bool log_completed = false;

    redis.acl_log(
        [&](auto &&reply) {
            EXPECT_TRUE(reply.ok());
            auto logs = reply.result();
            
            EXPECT_TRUE(logs.is_array());
            
            log_completed = true;
        });

    redis.await();
    EXPECT_TRUE(log_completed);
    
    // Test with count parameter
    bool log_limited_completed = false;
    
    redis.acl_log(
        [&](auto &&reply) {
            EXPECT_TRUE(reply.ok());
            auto limited_logs = reply.result();
            
            EXPECT_TRUE(limited_logs.is_array());
            
            // If there are logs, the limited logs should have at most 5 entries
            if (!limited_logs.empty()) {
                EXPECT_LE(limited_logs.size(), 5);
            }
            
            log_limited_completed = true;
        }, 5);

    redis.await();
    EXPECT_TRUE(log_limited_completed);
}

// Test async ACL LIST command
TEST_F(RedisTest, ASYNC_ACL_COMMANDS_LIST) {
    bool list_completed = false;

    redis.acl_list(
        [&](auto &&reply) {
            EXPECT_TRUE(reply.ok());
            auto acl_rules = reply.result();
            
            EXPECT_TRUE(acl_rules.is_array());
            EXPECT_FALSE(acl_rules.empty());
            
            // Verify that the default user is listed
            bool found_default_user = false;
            
            for (const auto& rule : acl_rules) {
                std::string rule_str = rule.template get<std::string>();
                if (rule_str.find("user default") != std::string::npos)
                    found_default_user = true;
            }
            
            EXPECT_TRUE(found_default_user);
            
            list_completed = true;
        });

    redis.await();
    EXPECT_TRUE(list_completed);
}

// Test async ACL USERS command
TEST_F(RedisTest, ASYNC_ACL_COMMANDS_USERS) {
    bool users_completed = false;

    redis.acl_users(
        [&](auto &&reply) {
            EXPECT_TRUE(reply.ok());
            auto users = reply.result();
            
            EXPECT_FALSE(users.empty());
            
            // Verify that the default user exists
            bool found_default_user = false;
            
            for (const auto& user_str : users) {
                if (user_str == "default")
                    found_default_user = true;
            }
            
            EXPECT_TRUE(found_default_user);
            
            users_completed = true;
        });

    redis.await();
    EXPECT_TRUE(users_completed);
}

// Test async ACL WHOAMI command
TEST_F(RedisTest, ASYNC_ACL_COMMANDS_WHOAMI) {
    bool whoami_completed = false;

    redis.acl_whoami(
        [&](auto &&reply) {
            EXPECT_TRUE(reply.ok());
            auto current_user = reply.result();
            
            // Verify that we're using the default user
            EXPECT_EQ(current_user, "default");
            
            whoami_completed = true;
        });

    redis.await();
    EXPECT_TRUE(whoami_completed);
}

// Test async ACL HELP command
TEST_F(RedisTest, ASYNC_ACL_COMMANDS_HELP) {
    bool help_completed = false;

    redis.acl_help(
        [&](auto &&reply) {
            EXPECT_TRUE(reply.ok());
            auto help = reply.result();
            
            EXPECT_FALSE(help.empty());
            
            // Verify that the help contains useful information
            bool found_help_entry = false;
            
            for (const auto& line_str : help) {
                if (line_str.find("ACL") != std::string::npos)
                    found_help_entry = true;
            }
            
            EXPECT_TRUE(found_help_entry);
            
            help_completed = true;
        });

    redis.await();
    EXPECT_TRUE(help_completed);
}

// Test async ACL GENPASS command
TEST_F(RedisTest, ASYNC_ACL_COMMANDS_GENPASS) {
    bool genpass_completed = false;

    redis.acl_genpass(
        [&](auto &&reply) {
            EXPECT_TRUE(reply.ok());
            auto password = reply.result();
            
            EXPECT_FALSE(password.empty());
            EXPECT_GT(password.length(), 8); // Should be long enough to be secure
            
            genpass_completed = true;
        });

    redis.await();
    EXPECT_TRUE(genpass_completed);
    
    // Test with custom bits
    bool genpass_custom_completed = false;
    
    redis.acl_genpass(
        [&](auto &&reply) {
            EXPECT_TRUE(reply.ok());
            auto password = reply.result();
            
            EXPECT_FALSE(password.empty());
            
            genpass_custom_completed = true;
        }, 128);

    redis.await();
    EXPECT_TRUE(genpass_custom_completed);
}

// Main function to run the tests
int
main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}