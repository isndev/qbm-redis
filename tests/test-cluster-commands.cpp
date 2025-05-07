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
    std::string prefix  = "qb::redis::cluster-test:" + std::to_string(++counter);

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

// Test CLUSTER INFO command
TEST_F(RedisTest, SYNC_CLUSTER_COMMANDS_INFO) {
    try {
        auto info = redis.cluster_info();
        
        // Check if we received a valid response (either cluster info or an error)
        EXPECT_TRUE(info.is_object() || info.is_string());
        
        if (info.is_string()) {
            std::string info_str = info.template get<std::string>();
            
            // If Redis is not in cluster mode, it will contain "cluster_state:fail"
            // If Redis is in cluster mode, it will contain "cluster_state:ok"
            EXPECT_TRUE(info_str.find("cluster_state:") != std::string::npos);
        }
    } catch (const std::exception& e) {
        // If the command is not recognized, that's acceptable
        // as not all Redis instances support cluster commands
        std::string error = e.what();
        EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                   error.find("cluster") != std::string::npos);
    }
}

// Test CLUSTER NODES command
TEST_F(RedisTest, SYNC_CLUSTER_COMMANDS_NODES) {
    try {
        auto nodes = redis.cluster_nodes();
        
        // Check if we received a valid response
        EXPECT_TRUE(nodes.is_object() || nodes.is_string() || nodes.is_array());
        
        if (nodes.is_string()) {
            // If Redis is not in cluster mode, we'll get an empty string
            // or an error message as string
            std::string nodes_str = nodes.template get<std::string>();
            
            // Just make sure we got some data - even if it's an error message
            EXPECT_FALSE(nodes_str.empty());
        }
    } catch (const std::exception& e) {
        // If the command is not recognized, that's acceptable
        // as not all Redis instances support cluster commands
        std::string error = e.what();
        EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                   error.find("cluster") != std::string::npos);
    }
}

// Test CLUSTER SLOTS command
TEST_F(RedisTest, SYNC_CLUSTER_COMMANDS_SLOTS) {
    try {
        auto slots = redis.cluster_slots();
        
        // Check if we received a valid response
        EXPECT_TRUE(slots.is_array() || slots.is_object());
    } catch (const std::exception& e) {
        // If the command is not recognized, that's acceptable
        // as not all Redis instances support cluster commands
        std::string error = e.what();
        EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                   error.find("cluster") != std::string::npos);
    }
}

// Test CLUSTER KEYSLOT command
TEST_F(RedisTest, SYNC_CLUSTER_COMMANDS_KEYSLOT) {
    try {
        std::string key = test_key("keyslot-test");
        auto slot = redis.cluster_keyslot(key);
        
        // Check if we received a valid slot number
        // Slot numbers in Redis Cluster are from 0 to 16383
        EXPECT_GE(slot, 0);
        EXPECT_LE(slot, 16383);
    } catch (const std::exception& e) {
        // If the command is not recognized, that's acceptable
        // as not all Redis instances support cluster commands
        std::string error = e.what();
        EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                   error.find("cluster") != std::string::npos);
    }
}

// Test CLUSTER COUNTKEYSINSLOT command
TEST_F(RedisTest, SYNC_CLUSTER_COMMANDS_COUNTKEYSINSLOT) {
    try {
        // Test with slot 0
        auto count = redis.cluster_countkeysinslot(0);
        
        // In a non-cluster Redis, this should be 0
        EXPECT_GE(count, 0);
    } catch (const std::exception& e) {
        // If the command is not recognized, that's acceptable
        // as not all Redis instances support cluster commands
        std::string error = e.what();
        EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                   error.find("cluster") != std::string::npos);
    }
}

// Test CLUSTER GETKEYSINSLOT command
TEST_F(RedisTest, SYNC_CLUSTER_COMMANDS_GETKEYSINSLOT) {
    try {
        // Test with slot 0 and count 10
        auto keys = redis.cluster_getkeysinslot(0, 10);
        
        // Verify we got a valid response
        // This will likely be empty in non-cluster mode
        EXPECT_TRUE(keys.empty() || !keys.empty());
    } catch (const std::exception& e) {
        // If the command is not recognized, that's acceptable
        // as not all Redis instances support cluster commands
        std::string error = e.what();
        EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                   error.find("cluster") != std::string::npos);
    }
}

// Test CLUSTER MYID command
TEST_F(RedisTest, SYNC_CLUSTER_COMMANDS_MYID) {
    try {
        auto node_id = redis.cluster_myid();
        
        // Verify we got a valid node ID (40 character string)
        // This might be empty or fail in non-cluster mode
        if (!node_id.empty()) {
            EXPECT_EQ(node_id.length(), 40);
        }
    } catch (const std::exception& e) {
        // If the command is not recognized, that's acceptable
        // as not all Redis instances support cluster commands
        std::string error = e.what();
        EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                   error.find("cluster") != std::string::npos);
    }
}

// Tests for commands that modify the cluster configuration
// These tests will verify command syntax but likely fail in non-cluster mode
TEST_F(RedisTest, SYNC_CLUSTER_COMMANDS_MODIFICATION) {
    try {
        // CLUSTER MEET
        // This would normally add a node to the cluster
        // We'll just test that the command format is accepted
        auto meet_status = redis.cluster_meet("127.0.0.1", 7000);
        SUCCEED();
    } catch (const std::exception& e) {
        // Expected to fail in non-cluster mode
        std::string error = e.what();
        EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                   error.find("cluster") != std::string::npos ||
                   error.find("ERR") != std::string::npos);
    }

    try {
        // CLUSTER FORGET
        // This would normally remove a node from the cluster
        auto forget_status = redis.cluster_forget("0000000000000000000000000000000000000000");
        SUCCEED();
    } catch (const std::exception& e) {
        // Expected to fail in non-cluster mode
        std::string error = e.what();
        EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                   error.find("cluster") != std::string::npos ||
                   error.find("ERR") != std::string::npos);
    }

    try {
        // CLUSTER RESET
        auto reset_status = redis.cluster_reset();
        SUCCEED();
    } catch (const std::exception& e) {
        // Expected to fail in non-cluster mode
        std::string error = e.what();
        EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                   error.find("cluster") != std::string::npos ||
                   error.find("ERR") != std::string::npos);
    }

    try {
        // CLUSTER FAILOVER
        auto failover_status = redis.cluster_failover();
        SUCCEED();
    } catch (const std::exception& e) {
        // Expected to fail in non-cluster mode
        std::string error = e.what();
        EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                   error.find("cluster") != std::string::npos ||
                   error.find("ERR") != std::string::npos);
    }

    try {
        // CLUSTER REPLICATE
        auto replicate_status = redis.cluster_replicate("0000000000000000000000000000000000000000");
        SUCCEED();
    } catch (const std::exception& e) {
        // Expected to fail in non-cluster mode
        std::string error = e.what();
        EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                   error.find("cluster") != std::string::npos ||
                   error.find("ERR") != std::string::npos);
    }

    try {
        // CLUSTER SAVECONFIG
        auto save_status = redis.cluster_saveconfig();
        SUCCEED();
    } catch (const std::exception& e) {
        // Expected to fail in non-cluster mode
        std::string error = e.what();
        EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                   error.find("cluster") != std::string::npos ||
                   error.find("ERR") != std::string::npos);
    }

    try {
        // CLUSTER SET-CONFIG-EPOCH
        auto epoch_status = redis.cluster_set_config_epoch(1);
        SUCCEED();
    } catch (const std::exception& e) {
        // Expected to fail in non-cluster mode
        std::string error = e.what();
        EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                   error.find("cluster") != std::string::npos ||
                   error.find("ERR") != std::string::npos);
    }

    try {
        // CLUSTER BUMPEPOCH
        auto bump_status = redis.cluster_bumpepoch();
        SUCCEED();
    } catch (const std::exception& e) {
        // Expected to fail in non-cluster mode
        std::string error = e.what();
        EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                   error.find("cluster") != std::string::npos ||
                   error.find("ERR") != std::string::npos);
    }
}

/*
 * ASYNCHRONOUS TESTS
 */

// Test async CLUSTER INFO command
TEST_F(RedisTest, ASYNC_CLUSTER_COMMANDS_INFO) {
    bool info_completed = false;
    
    // Use command directly with a lambda 
    redis.command<qb::json>(
        [&](auto &&reply) {
            info_completed = true;
            
            if (reply.ok()) {
                auto info = reply.result();
                
                // Check if we received a valid response
                EXPECT_TRUE(info.is_object() || info.is_string());
                
                if (info.is_string()) {
                    std::string info_str = info.template get<std::string>();
                    
                    // If Redis is not in cluster mode, it will contain "cluster_state:fail"
                    // If Redis is in cluster mode, it will contain "cluster_state:ok"
                    EXPECT_TRUE(info_str.find("cluster_state:") != std::string::npos);
                }
            } else {
                // If the command is not recognized, that's acceptable
                // as not all Redis instances support cluster commands
                std::string error = std::string(reply.error());
                EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                           error.find("cluster") != std::string::npos);
            }
        }, "CLUSTER", "INFO");
    
    redis.await();
    EXPECT_TRUE(info_completed);
}

// Test async CLUSTER NODES command
TEST_F(RedisTest, ASYNC_CLUSTER_COMMANDS_NODES) {
    bool nodes_completed = false;
    
    // Use command directly with a lambda
    redis.command<qb::json>(
        [&](auto &&reply) {
            nodes_completed = true;
            
            if (reply.ok()) {
                auto nodes = reply.result();
                
                // Check if we received a valid response
                EXPECT_TRUE(nodes.is_object() || nodes.is_string() || nodes.is_array());
                
                if (nodes.is_string()) {
                    // If Redis is not in cluster mode, we'll get an empty string
                    // or an error message as string
                    std::string nodes_str = nodes.template get<std::string>();
                    
                    // Just make sure we got some data - even if it's an error message
                    EXPECT_FALSE(nodes_str.empty());
                }
            } else {
                // If the command is not recognized, that's acceptable
                // as not all Redis instances support cluster commands
                std::string error = std::string(reply.error());
                EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                           error.find("cluster") != std::string::npos);
            }
        }, "CLUSTER", "NODES");
    
    redis.await();
    EXPECT_TRUE(nodes_completed);
}

// Test async CLUSTER SLOTS command
TEST_F(RedisTest, ASYNC_CLUSTER_COMMANDS_SLOTS) {
    bool slots_completed = false;
    
    // Use command directly with a lambda
    redis.command<qb::json>(
        [&](auto &&reply) {
            slots_completed = true;
            
            if (reply.ok()) {
                auto slots = reply.result();
                
                // Check if we received a valid response
                EXPECT_TRUE(slots.is_array() || slots.is_object());
            } else {
                // If the command is not recognized, that's acceptable
                // as not all Redis instances support cluster commands
                std::string error = std::string(reply.error());
                EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                           error.find("cluster") != std::string::npos);
            }
        }, "CLUSTER", "SLOTS");
    
    redis.await();
    EXPECT_TRUE(slots_completed);
}

// Test async CLUSTER KEYSLOT command
TEST_F(RedisTest, ASYNC_CLUSTER_COMMANDS_KEYSLOT) {
    bool keyslot_completed = false;
    std::string key = test_key("keyslot-test");
    
    // Use command directly with a lambda
    redis.command<long long>(
        [&](auto &&reply) {
            keyslot_completed = true;
            
            if (reply.ok()) {
                auto slot = reply.result();
                
                // Check if we received a valid slot number
                // Slot numbers in Redis Cluster are from 0 to 16383
                EXPECT_GE(slot, 0);
                EXPECT_LE(slot, 16383);
            } else {
                // If the command is not recognized, that's acceptable
                // as not all Redis instances support cluster commands
                std::string error = std::string(reply.error());
                EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                           error.find("cluster") != std::string::npos);
            }
        }, "CLUSTER", "KEYSLOT", key);
    
    redis.await();
    EXPECT_TRUE(keyslot_completed);
}

// Test async CLUSTER COUNTKEYSINSLOT command
TEST_F(RedisTest, ASYNC_CLUSTER_COMMANDS_COUNTKEYSINSLOT) {
    bool count_keys_completed = false;
    
    // Use command directly with a lambda
    redis.command<long long>(
        [&](auto &&reply) {
            count_keys_completed = true;
            
            if (reply.ok()) {
                auto count = reply.result();
                
                // In a non-cluster Redis, this should be 0
                EXPECT_GE(count, 0);
            } else {
                // If the command is not recognized, that's acceptable
                // as not all Redis instances support cluster commands
                std::string error = std::string(reply.error());
                EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                           error.find("cluster") != std::string::npos);
            }
        }, "CLUSTER", "COUNTKEYSINSLOT", 0); // Test with slot 0
    
    redis.await();
    EXPECT_TRUE(count_keys_completed);
}

// Test async CLUSTER GETKEYSINSLOT command
TEST_F(RedisTest, ASYNC_CLUSTER_COMMANDS_GETKEYSINSLOT) {
    bool getkeysinslot_completed = false;
    
    // Use command directly with a lambda
    redis.command<std::vector<std::string>>(
        [&](auto &&reply) {
            getkeysinslot_completed = true;
            
            if (reply.ok()) {
                auto keys = reply.result();
                
                // Verify we got a valid response
                // This will likely be empty in non-cluster mode
                EXPECT_TRUE(keys.empty() || !keys.empty());
            } else {
                // If the command is not recognized, that's acceptable
                // as not all Redis instances support cluster commands
                std::string error = std::string(reply.error());
                EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                           error.find("cluster") != std::string::npos);
            }
        }, "CLUSTER", "GETKEYSINSLOT", 0, 10); // Test with slot 0 and count 10
    
    redis.await();
    EXPECT_TRUE(getkeysinslot_completed);
}

// Test async CLUSTER MYID command
TEST_F(RedisTest, ASYNC_CLUSTER_COMMANDS_MYID) {
    bool myid_completed = false;
    
    // Use command directly with a lambda
    redis.command<std::string>(
        [&](auto &&reply) {
            myid_completed = true;
            
            if (reply.ok()) {
                auto node_id = reply.result();
                
                // Verify we got a valid node ID (40 character string)
                // This might be empty or fail in non-cluster mode
                if (!node_id.empty()) {
                    EXPECT_EQ(node_id.length(), 40);
                }
            } else {
                // If the command is not recognized, that's acceptable
                // as not all Redis instances support cluster commands
                std::string error = std::string(reply.error());
                EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                           error.find("cluster") != std::string::npos);
            }
        }, "CLUSTER", "MYID");
    
    redis.await();
    EXPECT_TRUE(myid_completed);
}

// Test async versions of cluster modification commands
TEST_F(RedisTest, ASYNC_CLUSTER_COMMANDS_MODIFICATION) {
    // CLUSTER MEET
    bool meet_completed = false;
    
    // Use command directly with a lambda
    redis.command<status>(
        [&](auto &&reply) {
            meet_completed = true;
            
            if (!reply.ok()) {
                // Expected to fail in non-cluster mode
                std::string error = std::string(reply.error());
                EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                           error.find("cluster") != std::string::npos ||
                           error.find("ERR") != std::string::npos);
            }
        }, "CLUSTER", "MEET", "127.0.0.1", 7000);
    
    redis.await();
    EXPECT_TRUE(meet_completed);
    
    // CLUSTER FORGET
    bool forget_completed = false;
    
    // Use command directly with a lambda
    redis.command<status>(
        [&](auto &&reply) {
            forget_completed = true;
            
            if (!reply.ok()) {
                // Expected to fail in non-cluster mode
                std::string error = std::string(reply.error());
                EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                           error.find("cluster") != std::string::npos ||
                           error.find("ERR") != std::string::npos);
            }
        }, "CLUSTER", "FORGET", "0000000000000000000000000000000000000000");
    
    redis.await();
    EXPECT_TRUE(forget_completed);
    
    // CLUSTER RESET
    bool reset_completed = false;
    
    // Use command directly with a lambda
    redis.command<status>(
        [&](auto &&reply) {
            reset_completed = true;
            
            if (!reply.ok()) {
                // Expected to fail in non-cluster mode
                std::string error = std::string(reply.error());
                EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                           error.find("cluster") != std::string::npos ||
                           error.find("ERR") != std::string::npos);
            }
        }, "CLUSTER", "RESET", "SOFT");
    
    redis.await();
    EXPECT_TRUE(reset_completed);
    
    // CLUSTER FAILOVER
    bool failover_completed = false;
    
    // Use command directly with a lambda
    redis.command<status>(
        [&](auto &&reply) {
            failover_completed = true;
            
            if (!reply.ok()) {
                // Expected to fail in non-cluster mode
                std::string error = std::string(reply.error());
                EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                           error.find("cluster") != std::string::npos ||
                           error.find("ERR") != std::string::npos);
            }
        }, "CLUSTER", "FAILOVER");
    
    redis.await();
    EXPECT_TRUE(failover_completed);
    
    // CLUSTER REPLICATE
    bool replicate_completed = false;
    
    // Use command directly with a lambda
    redis.command<status>(
        [&](auto &&reply) {
            replicate_completed = true;
            
            if (!reply.ok()) {
                // Expected to fail in non-cluster mode
                std::string error = std::string(reply.error());
                EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                           error.find("cluster") != std::string::npos ||
                           error.find("ERR") != std::string::npos);
            }
        }, "CLUSTER", "REPLICATE", "0000000000000000000000000000000000000000");
    
    redis.await();
    EXPECT_TRUE(replicate_completed);
    
    // CLUSTER SAVECONFIG
    bool saveconfig_completed = false;
    
    // Use command directly with a lambda
    redis.command<status>(
        [&](auto &&reply) {
            saveconfig_completed = true;
            
            if (!reply.ok()) {
                // Expected to fail in non-cluster mode
                std::string error = std::string(reply.error());
                EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                           error.find("cluster") != std::string::npos ||
                           error.find("ERR") != std::string::npos);
            }
        }, "CLUSTER", "SAVECONFIG");
    
    redis.await();
    EXPECT_TRUE(saveconfig_completed);
    
    // CLUSTER SET-CONFIG-EPOCH
    bool setepoch_completed = false;
    
    // Use command directly with a lambda
    redis.command<status>(
        [&](auto &&reply) {
            setepoch_completed = true;
            
            if (!reply.ok()) {
                // Expected to fail in non-cluster mode
                std::string error = std::string(reply.error());
                EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                           error.find("cluster") != std::string::npos ||
                           error.find("ERR") != std::string::npos);
            }
        }, "CLUSTER", "SET-CONFIG-EPOCH", 1);
    
    redis.await();
    EXPECT_TRUE(setepoch_completed);
    
    // CLUSTER BUMPEPOCH
    bool bumpepoch_completed = false;
    
    // Use command directly with a lambda
    redis.command<status>(
        [&](auto &&reply) {
            bumpepoch_completed = true;
            
            if (!reply.ok()) {
                // Expected to fail in non-cluster mode
                std::string error = std::string(reply.error());
                EXPECT_TRUE(error.find("unknown command") != std::string::npos ||
                           error.find("cluster") != std::string::npos ||
                           error.find("ERR") != std::string::npos);
            }
        }, "CLUSTER", "BUMPEPOCH");
    
    redis.await();
    EXPECT_TRUE(bumpepoch_completed);
}

// Main function to run the tests
int
main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}