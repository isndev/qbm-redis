# QB Redis Module - Roadmap

## Contexte Technique

Ce roadmap prend en compte l'évolution majeure du framework QB :

- **C++23** : Transition du C++17 vers le C++23 moderne
- **Coroutines** : Implémentation imminente des coroutines C++23 (`co_await`, `co_return`) en sous-jacent de `qb-io`
- **Impact** : Simplification drastique des patterns async actuels (callbacks → coroutines)

> **Note** : Certaines features seront implémentées en deux versions : une pour l'API actuelle callback-based, et une
> future version coroutine-optimized.

---

## 🎯 Features Priorisées

### P0 - Monitoring & Metrics (Critique pour Production)

**Objectif** : Observabilité complète du module Redis pour le monitoring en production.

**Métriques à implémenter** :

- **Latence** : p50, p95, p99 par type de commande
- **Throughput** : ops/sec global et par commande
- **Erreurs** : Taux d'erreur réseau, timeout, parsing
- **Connexions** : Nombre actif, en attente, échecs de reconnexion
- **Queue Depth** : Nombre de réponses en attente de traitement

**Implémentation Proposée** :

```cpp
// Fichier : metrics.h
#pragma once
#include <chrono>
#include <atomic>
#include <qb/system/container/ring_buffer.h>

namespace qb::redis {

/**
 * @brief Métriques pour une commande Redis individuelle
 */
struct CommandMetrics {
    std::string_view command_name;
    std::chrono::microseconds latency;
    bool success;
    std::size_t bytes_sent;
    std::size_t bytes_received;
};

/**
 * @brief Aggregateur de métriques haute performance
 * 
 * Thread-safe, lock-free pour la collecte
 */
class MetricsCollector {
public:
    void record(CommandMetrics&& metrics);
    
    // Statistiques calculées sur fenêtre glissante
    struct Stats {
        double ops_per_sec;
        double latency_p50_us;
        double latency_p99_us;
        double error_rate;
    };
    
    Stats get_stats(std::chrono::seconds window = std::chrono::seconds(60));
};

// Version Future (C++23 Coroutines)
// task<void> record_async(CommandMetrics metrics);

} // namespace qb::redis
```

**Intégration QB Actor** :

```cpp
// Event pour le monitoring
struct RedisMetricsEvent : qb::Event {
    std::string command;
    std::chrono::microseconds latency;
    bool success;
    std::size_t reply_size;
};

// Dans chaque commande (implémentation actuelle)
template <typename Ret, typename... Args>
Reply<Ret> command(std::string const& name, Args&&... args) {
    auto start = std::chrono::steady_clock::now();
    // ... exécution ...
    auto latency = std::chrono::steady_clock::now() - start;
    
    // Envoi vers le metrics actor
    if (metrics_actor_id_.is_valid()) {
        push<RedisMetricsEvent>(metrics_actor_id_, name, 
                              std::chrono::duration_cast<std::chrono::microseconds>(latency),
                              value.ok(), sizeof...(Args));
    }
    return value;
}
```

**Migration vers Coroutines (Future)** :

```cpp
// Avec C++23 coroutines, le monitoring devient trivial
template <typename Ret, typename... Args>
task<Reply<Ret>> command(std::string const& name, Args&&... args) {
    auto timer = metrics_.start_timer(name);
    
    auto reply = co_await send_command_async(name, std::forward<Args>(args)...);
    
    // Métriques automatiques via RAII
    timer.stop(reply.ok());  // Record latency + success
    
    co_return reply;
}
```

---

### P1 - Connection Pool

**Objectif** : Réutilisation efficace des connexions pour les scénarios high-throughput.

**Spécifications** :

- Pool configurable (min/max connexions)
- Health check automatique
- Reconnexion transparente
- Distribution round-robin ou least-loaded

**Implémentation Actuelle (Compatible C++17/23)** :

```cpp
// Fichier : pool.h
#pragma once
#include <queue>
#include <mutex>
#include <memory>
#include <condition_variable>

namespace qb::redis {

template <typename ClientType = tcp::client>
class ConnectionPool {
public:
    struct Config {
        std::size_t min_connections = 2;
        std::size_t max_connections = 10;
        std::chrono::seconds idle_timeout{300};
        std::chrono::seconds health_check_interval{30};
    };
    
    explicit ConnectionPool(qb::io::uri uri, Config config = {});
    
    // Acquisition bloquante (avec timeout)
    std::unique_ptr<ClientType> acquire(
        std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)
    );
    
    // Libération (retour au pool ou destruction si unhealthy)
    void release(std::unique_ptr<ClientType> conn);
    
    // Stats
    struct Stats {
        std::size_t active;
        std::size_t idle;
        std::size_t total_created;
        std::size_t total_destroyed;
    };
    Stats stats() const;
    
private:
    qb::io::uri uri_;
    Config config_;
    
    std::queue<std::unique_ptr<ClientType>> idle_;
    std::atomic<std::size_t> active_count_{0};
    std::mutex mutex_;
    std::condition_variable cv_;
};

} // namespace qb::redis
```

**Utilisation** :

```cpp
// Création du pool
auto pool = qb::redis::ConnectionPool(
    qb::io::uri{"tcp://localhost:6379"},
    {.min_connections = 2, .max_connections = 10}
);

// Usage avec RAII (retour automatique au pool)
{
    auto conn = pool.acquire();
    conn->hset("key", "field", "value");
    conn->hget("key", "field");
    // ~conn() → retour au pool automatiquement
}
```

**Version Future avec Coroutines** :

```cpp
// Acquisition async non-bloquante
task<std::unique_ptr<ClientType>> acquire_async();

// Usage
auto conn = co_await pool.acquire_async();
co_await conn->hset_async("key", "field", "value");
// Retour au pool automatique à la destruction
```

---

### P1 - Retry Policy

**Objectif** : Résilience automatique sur erreurs réseau temporaires.

**Types d'erreurs à gérer** :

- Timeout de connexion
- Déconnexion brutale (RST)
- Redis indisponible temporairement
- MOVED/ASK (Redis Cluster redirects)

**Implémentation** :

```cpp
// Fichier : retry.h
#pragma once
#include <functional>
#include <chrono>

namespace qb::redis {

enum class RetryStrategy {
    None,           // Pas de retry
    FixedDelay,     // Délai fixe entre tentatives
    ExponentialBackoff,  // Backoff exponentiel
    LinearBackoff    // Backoff linéaire
};

struct RetryPolicy {
    int max_attempts = 3;
    RetryStrategy strategy = RetryStrategy::ExponentialBackoff;
    std::chrono::milliseconds initial_delay{100};
    std::chrono::milliseconds max_delay{5000};
    
    // Callback personnalisable pour décider si on retry
    std::function<bool(const Error&)> should_retry;
};

// Helper pour calculer le délai
inline std::chrono::milliseconds calculate_delay(
    const RetryPolicy& policy, 
    int attempt
) {
    switch (policy.strategy) {
        case RetryStrategy::FixedDelay:
            return policy.initial_delay;
            
        case RetryStrategy::ExponentialBackoff:
            return std::min(
                policy.initial_delay * (1 << attempt),
                policy.max_delay
            );
            
        case RetryStrategy::LinearBackoff:
            return std::min(
                policy.initial_delay * attempt,
                policy.max_delay
            );
            
        default:
            return std::chrono::milliseconds{0};
    }
}

} // namespace qb::redis
```

**Intégration dans Redis Client** :

```cpp
class Redis {
public:
    void set_retry_policy(RetryPolicy policy) { retry_policy_ = std::move(policy); }
    
    template <typename Ret, typename... Args>
    Reply<Ret> command(std::string const& name, Args&&... args) {
        int attempt = 0;
        while (true) {
            try {
                return do_command<Ret>(name, std::forward<Args>(args)...);
            } catch (const NetworkError& e) {
                if (++attempt >= retry_policy_.max_attempts ||
                    !should_retry(e)) {
                    throw;
                }
                
                auto delay = calculate_delay(retry_policy_, attempt);
                std::this_thread::sleep_for(delay);
                
                // Reconnexion si nécessaire
                if (!is_connected()) {
                    reconnect();
                }
            }
        }
    }
    
private:
    RetryPolicy retry_policy_{.max_attempts = 1};  // Par défaut: pas de retry
};
```

**Version Coroutine (Future)** :

```cpp
task<Reply<Ret>> command_async(std::string name, Args... args) {
    int attempt = 0;
    while (true) {
        try {
            co_return co_await do_command_async<Ret>(name, args...);
        } catch (const NetworkError& e) {
            if (++attempt >= retry_policy_.max_attempts) throw;
            
            auto delay = calculate_delay(retry_policy_, attempt);
            co_await std::chrono::sleep_for(delay);  // Non-bloquant!
        }
    }
}
```

---

### P2 - Pipelining

**Objectif** : Envoi batch de commandes pour maximiser le throughput.

**Le Challenge** : Le parsing hiredis avec `redisReaderGetReply` retourne les réponses **une par une**, même en mode
pipeline. L'implémentation doit donc accumuler les réponses.

**Architecture Proposée** :

```cpp
// Fichier : pipeline.h
#pragma once
#include <vector>
#include <tuple>
#include <future>  // Future: remplacé par coroutines

namespace qb::redis {

/**
 * @brief Context de pipeline pour accumulation de commandes
 * 
 * Thread-local, non-thread-safe par design (comme hiredis)
 */
class Pipeline {
public:
    explicit Pipeline(Redis& client);
    
    // Ajout de commandes (lazy, pas d'envoi immédiat)
    template <typename... Args>
    Pipeline& add(std::string const& command, Args&&... args);
    
    // Exécution batch - envoie toutes les commandes d'un coup
    template <typename... Ret>
    std::tuple<Reply<Ret>...> execute();
    
    // Taille du batch
    std::size_t size() const { return buffer_.size(); }
    
    // Reset
    void clear();
    
private:
    Redis& client_;
    std::vector<std::string> buffer_;  // Commandes accumulées
    std::size_t expected_replies_ = 0;
};

} // namespace qb::redis
```

**Implémentation dans le Protocol Handler** :

```cpp
// Dans qb::protocol::redis
class redis : public qb::io::async::AProtocol<IO_> {
private:
    bool pipeline_mode_ = false;
    std::size_t expected_replies_ = 0;
    std::vector<redisReply*> pipeline_buffer_;
    
public:
    // Activer le mode pipeline
    void start_pipeline(std::size_t expected_replies) {
        pipeline_mode_ = true;
        expected_replies_ = expected_replies;
        pipeline_buffer_.clear();
        pipeline_buffer_.reserve(expected_replies);
    }
    
    // Récupérer le résultat batch
    std::vector<redisReply*> get_pipeline_results() {
        pipeline_mode_ = false;
        return std::move(pipeline_buffer_);
    }
    
protected:
    void onMessage(std::size_t) noexcept final {
        if (pipeline_mode_) {
            // Accumuler les réponses
            message msg;
            while (redisReaderGetReply(reader_, ...) == REDIS_OK && msg.reply) {
                pipeline_buffer_.push_back(msg.reply);
                
                // Quand on a tout reçu, notifier
                if (pipeline_buffer_.size() == expected_replies_) {
                    this->_io.on(pipeline_message{std::move(pipeline_buffer_)});
                    pipeline_mode_ = false;
                    break;
                }
            }
        } else {
            // Mode normal (existant)
            message msg;
            while (redisReaderGetReply(...) == REDIS_OK && msg.reply) {
                this->_io.on(msg);
            }
        }
    }
};
```

**Utilisation** :

```cpp
// Mode sync - batch de 100 SET
qb::redis::Pipeline pipe(redis);
for (int i = 0; i < 100; ++i) {
    pipe.add("SET", "key:" + std::to_string(i), "value" + std::to_string(i));
}
auto results = pipe.execute<std::string, std::string, ..., std::string>();  // 100 types

// Performance attendue :
// - Sans pipeline : 100 * 1ms = 100ms (round-trip RTT)
// - Avec pipeline : 1ms + overhead réseau = ~2-5ms
```

**Version Coroutine Optimisée** :

```cpp
// Le pipeline devient trivial avec coroutines
task<std::vector<Reply<std::string>>> batch_set(
    Redis& redis, 
    const std::vector<std::pair<std::string, std::string>>& kv_pairs
) {
    std::vector<task<Reply<std::string>>> tasks;
    tasks.reserve(kv_pairs.size());
    
    // Lancer toutes les commandes en parallèle
    for (auto& [k, v] : kv_pairs) {
        tasks.push_back(redis.set_async(k, v));
    }
    
    // Attendre toutes les réponses
    co_return co_await std::when_all(tasks.begin(), tasks.end());
}

// Usage
auto results = co_await batch_set(redis, data);
```

---

## 🚀 Migration vers C++23 Coroutines

### Changements Architecturaux Majeurs

**1. Simplification des Callbacks → Coroutines**

```cpp
// AVANT (Callback hell)
redis.hget([](auto&& reply) {
    if (reply.ok()) {
        redis.hset([](auto&& reply2) {
            // ...
        }, "other", "value");
    }
}, "key", "field");

// APRÈS (Coroutines - linéaire)
auto reply1 = co_await redis.hget_async("key", "field");
if (reply1.ok()) {
    auto reply2 = co_await redis.hset_async("other", "value");
}
```

**2. Nouvelle Architecture I/O**

```cpp
// qb-io avec coroutines
namespace qb::io::async {

task<std::size_t> read_async(buffer& buf);
task<std::size_t> write_async(const_buffer& buf);
task<void> connect_async(const uri& target);

} // namespace qb::io::async
```

**3. Impact sur le Protocol Handler**

```cpp
// Avant : callback-based
void onMessage(std::size_t size) noexcept final {
    // Parsing + dispatch
}

// Après : coroutine-aware
task<void> process_messages() {
    while (co_await has_data_async()) {
        auto msg = co_await read_message_async();
        co_await dispatch_async(msg);
    }
}
```

### Plan de Migration

| Phase | Description                            | Timeline   |
|-------|----------------------------------------|------------|
| **1** | qb-io coroutines foundation            | Q1 2025    |
| **2** | Redis protocol handler coroutinization | Q2 2025    |
| **3** | Nouvelle API `*_async()` parallèle     | Q2-Q3 2025 |
| **4** | Deprecation ancienne API callback      | Q4 2025    |
| **5** | Full coroutines (v3.0)                 | 2026       |

### Compatibilité Durante Transition

```cpp
// Support des deux paradigmes pendant 1-2 versions
class Redis {
public:
    // API Legacy (conservée)
    template <typename Func>
    Derived& hget(Func&& func, const std::string& key, const std::string& field);
    
    // API Future (coroutines)
    task<Reply<std::optional<std::string>>> hget_async(
        const std::string& key, 
        const std::string& field
    );
};
```

---

## 📋 Checklist Implémentation

### Monitoring & Metrics (P0)

- [ ] Structure `CommandMetrics`
- [ ] `MetricsCollector` lock-free
- [ ] Intégration dans toutes les commandes
- [ ] Tests de performance

### Connection Pool (P1)

- [ ] `ConnectionPool` template
- [ ] Health check mechanism
- [ ] Stats export
- [ ] Tests multi-thread

### Retry Policy (P1)

- [ ] Enum `RetryStrategy`
- [ ] `calculate_delay()` helpers
- [ ] Intégration dans `command()`
- [ ] Tests avec mocks réseau

### Pipelining (P2)

- [ ] `Pipeline` context class
- [ ] Mode pipeline dans Protocol Handler
- [ ] Accumulation réponses
- [ ] Tests performance (vs non-pipeline)

---

## 📋 Références

- **[TODO_COMMANDS.md](./TODO_COMMANDS.md)** — Liste des commandes Redis non encore implémentées comme méthodes dédiées.

---

## 🤝 Contribution

Ce roadmap est évolutif. Les contributions sont bienvenues :

- Proposer des designs via PR
- Signaler des priorités manquantes
- Partager des benchmarks

**Contact** : Maintainer QB Team

---

*Document version 1.0 - Janvier 2025*
*C++23 Coroutines Ready*
