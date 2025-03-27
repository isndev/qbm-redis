/*
 * qb - C++ Actor Framework
 * Copyright (C) 2011-2021 isndev (www.qbaf.io). All rights reserved.
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
#include <thread>
#include "../redis.h"

// Configuration Redis
#define REDIS_URI \
    { "tcp://localhost:6379" }

using namespace qb::io;
using namespace std::chrono;

// Génère des préfixes de clés uniques pour éviter les collisions entre tests
inline std::string
key_prefix(const std::string &key = "") {
    static int counter = 0;
    std::string prefix = "qb::redis::string-test:" + std::to_string(++counter);
    
    if (key.empty()) {
        return prefix;
    }
    
    return prefix + ":" + key;
}

// Génère une clé de test
inline std::string
test_key(const std::string &k) {
    return "{" + key_prefix() + "}::" + k;
}

// Vérifie la connexion et nettoie l'environnement avant les tests
class RedisTest : public ::testing::Test {
protected:
    qb::redis::tcp::client redis{REDIS_URI};
    
    void SetUp() override {
        async::init();
        if (!redis.connect() || !redis.flushall())
            throw std::runtime_error("Impossible de se connecter à Redis");
        
        // Attendre que la connexion soit établie
        redis.await();
        TearDown();
    }
    
    void TearDown() override {
        // Nettoyage après les tests
        redis.flushall();
        redis.await();
    }
};

/*
 * TESTS SYNCHRONES
 */

// Test des opérations SET et GET de base
TEST_F(RedisTest, SYNC_STRING_COMMANDS_SET_GET) {
    // Test SET simple
    std::string key = test_key("basic");
    std::string value = "hello world";
    
    EXPECT_TRUE(redis.set(key, value));
    
    // Test GET
    auto result = redis.get(key);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, value);
    
    // Test GETSET
    auto old_value = redis.getset(key, "new value");
    EXPECT_TRUE(old_value.has_value());
    EXPECT_EQ(*old_value, value);
    EXPECT_EQ(*redis.get(key), "new value");
    
    // Test DEL pour nettoyer
    EXPECT_EQ(redis.del(key), 1);
    EXPECT_FALSE(redis.get(key).has_value());
}

// Test des options SET (XX, NX, EX, PX)
TEST_F(RedisTest, SYNC_STRING_COMMANDS_SET_OPTIONS) {
    std::string key = test_key("options");
    std::string nx_key = test_key("nx");
    
    // Tester SET avec NX (only if not exists)
    EXPECT_TRUE(redis.set(nx_key, "initial", qb::redis::UpdateType::NOT_EXIST));
    EXPECT_FALSE(redis.set(nx_key, "updated", qb::redis::UpdateType::NOT_EXIST));
    EXPECT_EQ(*redis.get(nx_key), "initial");
    
    // Tester SETNX (équivalent à SET NX)
    EXPECT_FALSE(redis.setnx(nx_key, "setnx-value"));
    EXPECT_EQ(*redis.get(nx_key), "initial");
    
    // Tester SET avec XX (only if exists)
    EXPECT_FALSE(redis.set(key, "xx-value", qb::redis::UpdateType::EXIST));
    EXPECT_FALSE(redis.get(key).has_value());
    
    redis.set(key, "initial");
    EXPECT_TRUE(redis.set(key, "xx-value", qb::redis::UpdateType::EXIST));
    EXPECT_EQ(*redis.get(key), "xx-value");
    
    // Tester SET avec expiration (millisecondes)
    std::string pxkey = test_key("px");
    EXPECT_TRUE(redis.set(pxkey, "expire-soon", 500LL));
    EXPECT_TRUE(redis.get(pxkey).has_value());
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    EXPECT_FALSE(redis.get(pxkey).has_value());
    
    // Tester SET avec std::chrono::milliseconds
    EXPECT_TRUE(redis.set(pxkey, "chrono-expire", std::chrono::milliseconds(500)));
    EXPECT_TRUE(redis.get(pxkey).has_value());
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    EXPECT_FALSE(redis.get(pxkey).has_value());
    
    // Nettoyage
    redis.del(key, nx_key, pxkey);
}

// Test des commandes SETEX et PSETEX
TEST_F(RedisTest, SYNC_STRING_COMMANDS_SETEX) {
    std::string key = test_key("setex");
    std::string value = "expire-value";
    
    // Test SETEX (expiration en secondes)
    EXPECT_TRUE(redis.setex(key, 1, value));
    EXPECT_EQ(*redis.get(key), value);
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    EXPECT_FALSE(redis.get(key).has_value());
    
    // Test PSETEX (expiration en millisecondes)
    std::string pkey = test_key("psetex");
    EXPECT_TRUE(redis.psetex(pkey, 500, value));
    EXPECT_EQ(*redis.get(pkey), value);
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    EXPECT_FALSE(redis.get(pkey).has_value());
    
    // Test avec std::chrono
    EXPECT_TRUE(redis.setex(key, std::chrono::seconds(1), value));
    EXPECT_TRUE(redis.get(key).has_value());
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    EXPECT_FALSE(redis.get(key).has_value());
    
    EXPECT_TRUE(redis.psetex(pkey, std::chrono::milliseconds(500), value));
    EXPECT_TRUE(redis.get(pkey).has_value());
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    EXPECT_FALSE(redis.get(pkey).has_value());
    
    // Nettoyage
    redis.del(key, pkey);
}

// Test des opérations d'incrémentation/décrémentation
TEST_F(RedisTest, SYNC_STRING_COMMANDS_INCR_DECR) {
    std::string key = test_key("counter");
    
    // Incrémentation sur clé non existante
    EXPECT_EQ(redis.incr(key), 1);
    EXPECT_EQ(redis.incr(key), 2);
    
    // Incrémentation d'une valeur spécifique
    EXPECT_EQ(redis.incrby(key, 10), 12);
    
    // Décrémentation
    EXPECT_EQ(redis.decr(key), 11);
    EXPECT_EQ(redis.decrby(key, 5), 6);
    
    // Incrémentation à virgule flottante
    std::string float_key = test_key("float");
    EXPECT_EQ(redis.set(float_key, "10.5"), true);
    EXPECT_FLOAT_EQ(redis.incrbyfloat(float_key, 0.5), 11.0);
    EXPECT_FLOAT_EQ(redis.incrbyfloat(float_key, -1.5), 9.5);
    
    // Nettoyage
    redis.del(key, float_key);
}

// Test des opérations multi-clés
TEST_F(RedisTest, SYNC_STRING_COMMANDS_MULTI) {
    std::string key1 = test_key("multi1");
    std::string key2 = test_key("multi2");
    std::string key3 = test_key("multi3");
    std::string key4 = test_key("multi4");
    
    // Test MSET
    EXPECT_TRUE(redis.mset(key1, "val1", key2, "val2", key3, "val3"));
    
    // Test MGET
    auto values = redis.mget(key1, key2, key3, key4);
    EXPECT_EQ(values.size(), 4);
    EXPECT_TRUE(values[0].has_value());
    EXPECT_EQ(*values[0], "val1");
    EXPECT_TRUE(values[1].has_value());
    EXPECT_EQ(*values[1], "val2");
    EXPECT_TRUE(values[2].has_value());
    EXPECT_EQ(*values[2], "val3");
    EXPECT_FALSE(values[3].has_value());
    
    // Test MSETNX
    EXPECT_FALSE(redis.msetnx(key1, "new1", key4, "val4"));
    EXPECT_FALSE(redis.get(key4).has_value());
    EXPECT_EQ(*redis.get(key1), "val1");
    
    // Test MSETNX quand aucune clé n'existe
    std::string nx1 = test_key("nx1");
    std::string nx2 = test_key("nx2");
    EXPECT_TRUE(redis.msetnx(nx1, "nx-val1", nx2, "nx-val2"));
    EXPECT_EQ(*redis.get(nx1), "nx-val1");
    EXPECT_EQ(*redis.get(nx2), "nx-val2");
    
    // Nettoyage
    redis.del(key1, key2, key3, nx1, nx2);
}

// Test des opérations de manipulation de chaîne
TEST_F(RedisTest, SYNC_STRING_COMMANDS_STRING_OPS) {
    std::string key = test_key("string-ops");
    std::string value = "hello world";
    
    // Définir la valeur
    EXPECT_TRUE(redis.set(key, value));
    
    // Test STRLEN
    EXPECT_EQ(redis.strlen(key), value.length());
    
    // Test APPEND
    EXPECT_EQ(redis.append(key, "!"), value.length() + 1);
    EXPECT_EQ(*redis.get(key), value + "!");
    
    // Test GETRANGE
    EXPECT_EQ(redis.getrange(key, 0, 4), "hello");
    EXPECT_EQ(redis.getrange(key, 6, 10), "world");
    EXPECT_EQ(redis.getrange(key, -1, -1), "!");
    
    // Test SETRANGE
    EXPECT_EQ(redis.setrange(key, 6, "Redis"), value.length() + 1);
    EXPECT_EQ(*redis.get(key), "hello Redis!");
    
    // Nettoyage
    redis.del(key);
}

/*
 * TESTS ASYNCHRONES
 */

// Test de base SET/GET asynchrone
TEST_F(RedisTest, ASYNC_STRING_COMMANDS_SET_GET) {
    std::string key = test_key("async-basic");
    std::string value = "async-value";
    bool completed = false;
    
    // Utiliser SET de manière asynchrone
    redis.set([&](qb::redis::Reply<qb::redis::reply::set> &&reply) {
        EXPECT_TRUE(reply.ok);
        EXPECT_TRUE(reply.result());
        
        // Puis utiliser GET de manière asynchrone dans le callback
        redis.get([&](qb::redis::Reply<std::optional<std::string>> &&get_reply) {
            EXPECT_TRUE(get_reply.ok);
            EXPECT_TRUE(get_reply.result.has_value());
            EXPECT_EQ(*get_reply.result, value);
            completed = true;
        }, key);
    }, key, value);
    
    // Attendre que les opérations asynchrones soient terminées
    redis.await();
    EXPECT_TRUE(completed);
    
    // Nettoyage
    redis.del(key);
}

// Test des options SETEX asynchrones
TEST_F(RedisTest, ASYNC_STRING_COMMANDS_SETEX) {
    std::string key = test_key("async-setex");
    std::string value = "expire-value";
    bool completed = false;
    
    // Utiliser SETEX de manière asynchrone
    redis.setex([&](qb::redis::Reply<void> &&reply) {
        EXPECT_TRUE(reply.ok);
        
        // Vérifier que la clé existe
        redis.get([&](qb::redis::Reply<std::optional<std::string>> &&get_reply) {
            EXPECT_TRUE(get_reply.ok);
            EXPECT_TRUE(get_reply.result.has_value());
            EXPECT_EQ(*get_reply.result, value);
            
            // Attendre l'expiration
            std::this_thread::sleep_for(std::chrono::milliseconds(1100));
            
            // Vérifier que la clé a expiré
            redis.get([&](qb::redis::Reply<std::optional<std::string>> &&get_reply2) {
                EXPECT_TRUE(get_reply2.ok);
                EXPECT_FALSE(get_reply2.result.has_value());
                completed = true;
            }, key);
        }, key);
    }, key, 1, value);
    
    // Attendre que les opérations asynchrones soient terminées
    redis.await();
    EXPECT_TRUE(completed);
}

// Test d'incrémentation/décrémentation asynchrone
TEST_F(RedisTest, ASYNC_STRING_COMMANDS_INCR_DECR) {
    std::string key = test_key("async-counter");
    bool completed = false;
    
    // Initialiser la clé
    redis.set([&](qb::redis::Reply<qb::redis::reply::set> &&reply) {
        EXPECT_TRUE(reply.ok);
        
        // Incrémenter de manière asynchrone
        redis.incr([&](qb::redis::Reply<long long> &&incr_reply) {
            EXPECT_TRUE(incr_reply.ok);
            EXPECT_EQ(incr_reply.result, 6);
            
            // Incrémenter d'une valeur spécifique
            redis.incrby([&](qb::redis::Reply<long long> &&incrby_reply) {
                EXPECT_TRUE(incrby_reply.ok);
                EXPECT_EQ(incrby_reply.result, 16);
                
                // Décrémenter
                redis.decr([&](qb::redis::Reply<long long> &&decr_reply) {
                    EXPECT_TRUE(decr_reply.ok);
                    EXPECT_EQ(decr_reply.result, 15);
                    
                    // Décrémenter d'une valeur spécifique
                    redis.decrby([&](qb::redis::Reply<long long> &&decrby_reply) {
                        EXPECT_TRUE(decrby_reply.ok);
                        EXPECT_EQ(decrby_reply.result, 5);
                        completed = true;
                    }, key, 10);
                }, key);
            }, key, 10);
        }, key);
    }, key, "5");
    
    // Attendre que les opérations asynchrones soient terminées
    redis.await();
    EXPECT_TRUE(completed);
    
    // Nettoyage
    redis.del(key);
}

// Test multi-clés asynchrone
TEST_F(RedisTest, ASYNC_STRING_COMMANDS_MULTI) {
    std::string key1 = test_key("async-multi1");
    std::string key2 = test_key("async-multi2");
    std::string key3 = test_key("async-multi3");
    bool completed = false;
    
    // Définir plusieurs clés
    redis.mset([&](qb::redis::Reply<void> &&reply) {
        EXPECT_TRUE(reply.ok);
        
        // Récupérer plusieurs clés
        redis.mget([&](qb::redis::Reply<std::vector<std::optional<std::string>>> &&mget_reply) {
            EXPECT_TRUE(mget_reply.ok);
            EXPECT_EQ(mget_reply.result.size(), 3);
            EXPECT_TRUE(mget_reply.result[0].has_value());
            EXPECT_EQ(*mget_reply.result[0], "val1");
            EXPECT_TRUE(mget_reply.result[1].has_value());
            EXPECT_EQ(*mget_reply.result[1], "val2");
            EXPECT_TRUE(mget_reply.result[2].has_value());
            EXPECT_EQ(*mget_reply.result[2], "val3");
            completed = true;
        }, key1, key2, key3);
    }, key1, "val1", key2, "val2", key3, "val3");
    
    // Attendre que les opérations asynchrones soient terminées
    redis.await();
    EXPECT_TRUE(completed);
    
    // Nettoyage
    redis.del(key1, key2, key3);
}

// Test d'opérations de chaîne asynchrones
TEST_F(RedisTest, ASYNC_STRING_COMMANDS_STRING_OPS) {
    std::string key = test_key("async-string-ops");
    std::string value = "hello world";
    bool completed = false;
    
    // Définir la valeur
    redis.set([&](qb::redis::Reply<qb::redis::reply::set> &&reply) {
        EXPECT_TRUE(reply.ok);
        
        // Tester APPEND
        redis.append([&](qb::redis::Reply<long long> &&append_reply) {
            EXPECT_TRUE(append_reply.ok);
            EXPECT_EQ(append_reply.result, value.length() + 1);
            
            // Tester STRLEN
            redis.strlen([&](qb::redis::Reply<long long> &&strlen_reply) {
                EXPECT_TRUE(strlen_reply.ok);
                EXPECT_EQ(strlen_reply.result, value.length() + 1);
                
                // Tester GETRANGE
                redis.getrange([&](qb::redis::Reply<std::string> &&getrange_reply) {
                    EXPECT_TRUE(getrange_reply.ok);
                    EXPECT_EQ(getrange_reply.result, "hello");
                    
                    // Tester SETRANGE
                    redis.setrange([&](qb::redis::Reply<long long> &&setrange_reply) {
                        EXPECT_TRUE(setrange_reply.ok);
                        EXPECT_EQ(setrange_reply.result, value.length() + 1);
                        
                        // Vérifier le résultat final
                        redis.get([&](qb::redis::Reply<std::optional<std::string>> &&get_reply) {
                            EXPECT_TRUE(get_reply.ok);
                            EXPECT_TRUE(get_reply.result.has_value());
                            EXPECT_EQ(*get_reply.result, "hello Redis!");
                            completed = true;
                        }, key);
                    }, key, 6, "Redis");
                }, key, 0, 4);
            }, key);
        }, key, "!");
    }, key, value);
    
    // Attendre que les opérations asynchrones soient terminées
    redis.await();
    EXPECT_TRUE(completed);
    
    // Nettoyage
    redis.del(key);
}

// Test des données binaires
TEST_F(RedisTest, SYNC_STRING_COMMANDS_BINARY) {
    std::string key = test_key("binary");
    
    // Créer des données binaires
    std::vector<char> binary_data = {0, 1, 2, 3, 4, 5, static_cast<char>(0xFF), static_cast<char>(0xAA), 0x00};
    std::string binary_string(binary_data.begin(), binary_data.end());
    
    // Stocker des données binaires
    EXPECT_TRUE(redis.set(key, binary_string));
    
    // Récupérer et vérifier
    auto result = redis.get(key);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), binary_data.size());
    
    // Vérifier chaque octet
    for (size_t i = 0; i < binary_data.size(); ++i) {
        EXPECT_EQ(static_cast<unsigned char>((*result)[i]), 
                 static_cast<unsigned char>(binary_data[i]));
    }
    
    // Nettoyage
    redis.del(key);
}

// Test complexe de pipeline asynchrone
TEST_F(RedisTest, ASYNC_STRING_COMMANDS_PIPELINE) {
    std::string key1 = test_key("pipeline1");
    std::string key2 = test_key("pipeline2");
    std::string key3 = test_key("pipeline3");
    
    int callback_count = 0;
    auto check_callback = [&callback_count](auto &&reply) {
        EXPECT_TRUE(reply.ok);
        callback_count++;
    };
    
    // Créer un pipeline d'opérations
    redis.set(check_callback, key1, "pipeline-val1")
         .set(check_callback, key2, "pipeline-val2")
         .set(check_callback, key3, "pipeline-val3")
         .append(check_callback, key1, "-appended")
         .append(check_callback, key2, "-appended")
         .strlen(check_callback, key3)
         .mget([&callback_count](qb::redis::Reply<std::vector<std::optional<std::string>>> &&reply) {
             EXPECT_TRUE(reply.ok);
             EXPECT_EQ(reply.result.size(), 3);
             EXPECT_TRUE(reply.result[0].has_value());
             EXPECT_EQ(*reply.result[0], "pipeline-val1-appended");
             callback_count++;
         }, key1, key2, key3)
         .await();
    
    // Vérifier que tous les callbacks ont été appelés
    EXPECT_EQ(callback_count, 7);
    
    // Vérification supplémentaire
    EXPECT_EQ(*redis.get(key1), "pipeline-val1-appended");
    EXPECT_EQ(*redis.get(key2), "pipeline-val2-appended"); // Incrémenté en tant que chaîne
    EXPECT_EQ(*redis.get(key3), "pipeline-val3");
    
    // Nettoyage
    redis.del(key1, key2, key3);
} 