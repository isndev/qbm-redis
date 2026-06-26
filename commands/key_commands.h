/**
 * @file qbm/redis/commands/key_commands.h
 * @brief Redis key-space command mixin (CRTP).
 *
 * Provides the @ref qb::redis::key_commands mixin which implements the Redis
 * key-management command family (expiration, existence, renaming, scanning,
 * copy/move/migrate, OBJECT introspection, SORT, replication waits, ...). Each
 * command exposes both a coroutine-awaitable form and a callback-based
 * asynchronous form.
 *
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2026 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)
 * @ingroup Redis
 */
#ifndef QBM_REDIS_KEY_COMMANDS_H
#define QBM_REDIS_KEY_COMMANDS_H
#include <chrono>
#include "../reply.h"

namespace qb::redis {

/**
 * @class key_commands
 * @brief Provides Redis key management command implementations.
 *
 * This class implements Redis commands related to key management, including operations
 * for key expiration, existence checks, renaming, and scanning. Each command has both
 * synchronous and asynchronous versions.
 *
 * Key commands are fundamental for managing the Redis keyspace and working with key
 * lifetimes in the database.
 *
 * @tparam Derived The derived class type (CRTP pattern)
 */
template <typename Derived>
class key_commands {
private:
    constexpr Derived &
    derived() {
        return static_cast<Derived &>(*this);
    }

    /**
     * @class scanner
     * @brief Helper class for implementing incremental scanning of keys
     *
     * Uses shared_ptr for automatic memory management. Safe even if exceptions occur.
     *
     * @tparam Func Callback function type
     */
    template <typename Func>
    class scanner : public std::enable_shared_from_this<scanner<Func>> {
        Derived                            &_derived;
        std::string                         _pattern;
        Func                                _func;
        size_t                              _cursor{0};
        qb::redis::Reply<qb::redis::scan<>> _reply;
        bool                                _started{false};

    public:
        /**
         * @brief Constructs a scanner for keys matching a pattern
         *
         * @param derived The Redis handler
         * @param pattern Pattern to match keys against
         * @param func Callback function to process results
         */
        scanner(Derived &derived, std::string pattern, Func &&func)
            : _derived(derived)
            , _pattern(std::move(pattern))
            , _func(std::forward<Func>(func)) {}

        /**
         * @brief Start the scanning process
         */
        void
        start() {
            if (!_started) {
                _started  = true;
                auto self = this->shared_from_this();
                _derived.template command<qb::redis::scan<>>(
                    [self](qb::redis::Reply<qb::redis::scan<>> &&reply) { (*self)(std::forward<qb::redis::Reply<qb::redis::scan<>>>(reply)); },
                    "SCAN", _cursor, "MATCH", _pattern);
            }
        }

        /**
         * @brief Processes scan results and continues scanning if needed
         *
         * @param reply The scan operation reply
         */
        void
        operator()(qb::redis::Reply<qb::redis::scan<>> &&reply) {
            _reply.ok() = reply.ok();
            std::move(reply.result().items.begin(), reply.result().items.end(), std::back_inserter(_reply.result().items));
            if (reply.ok() && reply.result().cursor) {
                _cursor   = reply.result().cursor;
                auto self = this->shared_from_this();
                _derived.template command<qb::redis::scan<>>(
                    [self](qb::redis::Reply<qb::redis::scan<>> &&r) { (*self)(std::forward<qb::redis::Reply<qb::redis::scan<>>>(r)); }, "SCAN",
                    _cursor, "MATCH", _pattern);
            } else {
                try {
                    _func(std::move(_reply));
                } catch (std::exception const &e) {
                    LOG_WARN("[qbm][redis] key scanner callback failed: " << e.what());
                }
            }
        }

        /**
         * @brief Factory method to create and start a scanner safely
         */
        static void
        create_and_start(Derived &derived, std::string pattern, Func &&func) {
            auto ptr = std::make_shared<scanner>(derived, std::move(pattern), std::forward<Func>(func));
            ptr->start();
        }
    };

public:
    /**
     * @brief Delete the given keys (coroutine awaitable).
     * @param keys Keys, variadic(could be a string, or a container of keys)... .
     * @return redis_awaiter yielding Reply<long long>
     * @see https://redis.io/commands/del
     */
    template <typename... Keys>
    auto
    del(Keys &&...keys) {
        return derived().template make_coro_command<long long>([this, ... keys = std::forward<Keys>(keys)](auto &&callback) mutable {
            this->del(std::move(callback), std::forward<decltype(keys)>(keys)...);
        });
    }

    /**
     * @brief Asynchronous version of del
     * @see https://redis.io/commands/del
     */
    template <typename Func, typename... Keys>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    del(Func &&func, Keys &&...keys) {
        return derived().template command<long long>(std::forward<Func>(func), "DEL", std::forward<Keys>(keys)...);
    }

    /**
     * @brief Dump the serialized value of the key (coroutine awaitable).
     * @param key Key.
     * @return redis_awaiter yielding Reply<std::optional<std::string>>
     * @see https://redis.io/commands/dump
     */
    auto
    dump(const std::string &key) {
        return derived().template make_coro_command<std::optional<std::string>>(
            [this, key](auto &&callback) { this->dump(std::move(callback), key); });
    }

    /**
     * @brief Asynchronous version of dump
     * @see https://redis.io/commands/dump
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::string>> &&>, Derived &>
    dump(Func &&func, const std::string &key) {
        return derived().template command<std::optional<std::string>>(std::forward<Func>(func), "DUMP", key);
    }

    /**
     * @brief Check if the given keys exist (coroutine awaitable).
     * @param keys Keys, variadic(could be a string, or a container of keys)... .
     * @return redis_awaiter yielding Reply<long long>
     * @see https://redis.io/commands/exists
     */
    template <typename... Keys>
    auto
    exists(Keys &&...keys) {
        return derived().template make_coro_command<long long>([this, ... keys = std::forward<Keys>(keys)](auto &&callback) mutable {
            this->exists(std::move(callback), std::forward<Keys>(keys)...);
        });
    }

    /**
     * @brief Asynchronous version of exists
     * @see https://redis.io/commands/exists
     */
    template <typename Func, typename... Keys>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    exists(Func &&func, Keys &&...keys) {
        return derived().template command<long long>(std::forward<Func>(func), "EXISTS", std::forward<Keys>(keys)...);
    }

    /**
     * @brief Set the expiration time of a key to be `timeout` seconds (coroutine awaitable).
     * @param key Key.
     * @param timeout TTL in seconds.
     * @return redis_awaiter yielding Reply<bool>
     * @see https://redis.io/commands/expire
     */
    auto
    expire(const std::string &key, long long timeout) {
        return derived().template make_coro_command<bool>(
            [this, key, timeout](auto &&callback) { this->expire(std::move(callback), key, timeout); });
    }

    /**
     * @brief Asynchronous version of expire
     * @see https://redis.io/commands/expire
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    expire(Func &&func, const std::string &key, long long timeout) {
        return derived().template command<bool>(std::forward<Func>(func), "EXPIRE", key, timeout);
    }

    /**
     * @brief Set the expiration time of a key (chrono version, coroutine awaitable).
     * @param key Key.
     * @param timeout TTL in seconds.
     * @return redis_awaiter yielding Reply<bool>
     * @see https://redis.io/commands/expire
     */
    auto
    expire(const std::string &key, const std::chrono::seconds &timeout) {
        return expire(key, timeout.count());
    }

    /**
     * @brief Asynchronous version of expire (chrono overload)
     * @see https://redis.io/commands/expire
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    expire(Func &&func, const std::string &key, const std::chrono::seconds &timeout) {
        return expire(std::forward<Func>(func), key, timeout.count());
    }

    /**
     * @brief Set the expiration time of a key to be a UNIX timestamp (coroutine awaitable).
     * @param key Key.
     * @param timestamp UNIX timestamp.
     * @return redis_awaiter yielding Reply<bool>
     * @see https://redis.io/commands/expireat
     */
    auto
    expireat(const std::string &key, long long timestamp) {
        return derived().template make_coro_command<bool>(
            [this, key, timestamp](auto &&callback) { this->expireat(std::move(callback), key, timestamp); });
    }

    /**
     * @brief Asynchronous version of expireat
     * @see https://redis.io/commands/expireat
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    expireat(Func &&func, const std::string &key, long long timestamp) {
        return derived().template command<bool>(std::forward<Func>(func), "EXPIREAT", key, timestamp);
    }

    /**
     * @brief Set a timeout on key at a future time point (chrono version, coroutine awaitable).
     * @param key Key.
     * @param timestamp Time in seconds since UNIX epoch.
     * @return redis_awaiter yielding Reply<bool>
     * @see https://redis.io/commands/expireat
     */
    auto
    expireat(const std::string &key, const std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds> &tp) {
        return expireat(key, tp.time_since_epoch().count());
    }

    /**
     * @brief Asynchronous version of expireat (chrono overload)
     * @see https://redis.io/commands/expireat
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    expireat(Func &&func, const std::string &key, const std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds> &tp) {
        return expireat(std::forward<Func>(func), key, tp.time_since_epoch().count());
    }

    /**
     * @brief Get all keys matching the given pattern (coroutine awaitable).
     * @param pattern Pattern, supporting glob-style patterns.
     * @return redis_awaiter yielding Reply<std::vector<std::string>>
     * @see https://redis.io/commands/keys
     */
    auto
    keys(const std::string &pattern = "*") {
        return derived().template make_coro_command<std::vector<std::string>>(
            [this, pattern](auto &&callback) { this->keys(std::move(callback), pattern); });
    }

    /**
     * @brief Asynchronous version of keys
     * @see https://redis.io/commands/keys
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>, Derived &>
    keys(Func &&func, const std::string &pattern = "*") {
        return derived().template command<std::vector<std::string>>(std::forward<Func>(func), "KEYS", pattern);
    }

    /**
     * @brief Move the key from a database to another one (coroutine awaitable).
     * @param key Key.
     * @param destination_db Destination database ID.
     * @return redis_awaiter yielding Reply<bool>
     * @see https://redis.io/commands/move
     */
    auto
    move(const std::string &key, long long destination_db) {
        return derived().template make_coro_command<bool>(
            [this, key, destination_db](auto &&callback) { this->move(std::move(callback), key, destination_db); });
    }

    /**
     * @brief Asynchronous version of move
     * @see https://redis.io/commands/move
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    move(Func &&func, const std::string &key, long long destination_db) {
        return derived().template command<bool>(std::forward<Func>(func), "MOVE", key, destination_db);
    }

    /**
     * @brief Remove timeout on key (coroutine awaitable).
     * @param key Key.
     * @return redis_awaiter yielding Reply<bool>
     * @see https://redis.io/commands/persist
     */
    auto
    persist(const std::string &key) {
        return derived().template make_coro_command<bool>([this, key](auto &&callback) { this->persist(std::move(callback), key); });
    }

    /**
     * @brief Asynchronous version of persist
     * @see https://redis.io/commands/persist
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    persist(Func &&func, const std::string &key) {
        return derived().template command<bool>(std::forward<Func>(func), "PERSIST", key);
    }

    /**
     * @brief Set a timeout on key (coroutine awaitable).
     * @param key Key.
     * @param timeout Timeout in milliseconds.
     * @return redis_awaiter yielding Reply<bool>
     * @see https://redis.io/commands/pexpire
     */
    auto
    pexpire(const std::string &key, long long timeout) {
        return derived().template make_coro_command<bool>(
            [this, key, timeout](auto &&callback) { this->pexpire(std::move(callback), key, timeout); });
    }

    /**
     * @brief Asynchronous version of pexpire
     * @see https://redis.io/commands/pexpire
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    pexpire(Func &&func, const std::string &key, long long timeout) {
        return derived().template command<bool>(std::forward<Func>(func), "PEXPIRE", key, timeout);
    }

    /**
     * @brief Set a timeout on key (chrono version, coroutine awaitable).
     * @param key Key.
     * @param timeout Timeout in milliseconds.
     * @return redis_awaiter yielding Reply<bool>
     * @see https://redis.io/commands/pexpire
     */
    auto
    pexpire(const std::string &key, const std::chrono::milliseconds &timeout) {
        return pexpire(key, timeout.count());
    }

    /**
     * @brief Asynchronous version of pexpire (chrono overload)
     * @see https://redis.io/commands/pexpire
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    pexpire(Func &&func, const std::string &key, const std::chrono::milliseconds &timeout) {
        return pexpire(std::forward<Func>(func), key, timeout.count());
    }

    /**
     * @brief Set a timeout on key at a future time point (coroutine awaitable).
     * @param key Key.
     * @param timestamp Time in milliseconds since UNIX epoch.
     * @return redis_awaiter yielding Reply<bool>
     * @see https://redis.io/commands/pexpireat
     */
    auto
    pexpireat(const std::string &key, long long timestamp) {
        return derived().template make_coro_command<bool>(
            [this, key, timestamp](auto &&callback) { this->pexpireat(std::move(callback), key, timestamp); });
    }

    /**
     * @brief Asynchronous version of pexpireat
     * @see https://redis.io/commands/pexpireat
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    pexpireat(Func &&func, const std::string &key, long long timestamp) {
        return derived().template command<bool>(std::forward<Func>(func), "PEXPIREAT", key, timestamp);
    }

    /**
     * @brief Set a timeout on key at a future time point (chrono version, coroutine awaitable).
     * @param key Key.
     * @param timestamp Time in milliseconds since UNIX epoch.
     * @return redis_awaiter yielding Reply<bool>
     * @see https://redis.io/commands/pexpireat
     */
    auto
    pexpireat(const std::string &key, const std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds> &tp) {
        return pexpireat(key, tp.time_since_epoch().count());
    }

    /**
     * @brief Asynchronous version of pexpireat (chrono overload)
     * @see https://redis.io/commands/pexpireat
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    pexpireat(Func &&func, const std::string &key, const std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds> &tp) {
        return pexpireat(std::forward<Func>(func), key, tp.time_since_epoch().count());
    }

    /**
     * @brief Get the TTL of a key in milliseconds (coroutine awaitable).
     * @param key Key.
     * @return redis_awaiter yielding Reply<long long>
     * @see https://redis.io/commands/pttl
     */
    auto
    pttl(const std::string &key) {
        return derived().template make_coro_command<long long>([this, key](auto &&callback) { this->pttl(std::move(callback), key); });
    }

    /**
     * @brief Asynchronous version of pttl
     * @see https://redis.io/commands/pttl
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    pttl(Func &&func, const std::string &key) {
        return derived().template command<long long>(std::forward<Func>(func), "PTTL", key);
    }

    /**
     * @brief Get a random key from current database (coroutine awaitable).
     * @return redis_awaiter yielding Reply<std::optional<std::string>>
     * @see https://redis.io/commands/randomkey
     */
    auto
    randomkey() {
        return derived().template make_coro_command<std::optional<std::string>>(
            [this](auto &&callback) { this->randomkey(std::move(callback)); });
    }

    /**
     * @brief Asynchronous version of randomkey
     * @see https://redis.io/commands/randomkey
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::string>> &&>, Derived &>
    randomkey(Func &&func) {
        return derived().template command<std::optional<std::string>>(std::forward<Func>(func), "RANDOMKEY");
    }

    /**
     * @brief Renames a key (coroutine awaitable)
     * @param key Key name to rename
     * @param new_key New name for the key
     * @return redis_awaiter yielding Reply<status>
     * @see https://redis.io/commands/rename
     */
    auto
    rename(const std::string &key, const std::string &new_key) {
        return derived().template make_coro_command<status>(
            [this, key, new_key](auto &&callback) { this->rename(std::move(callback), key, new_key); });
    }

    /**
     * @brief Asynchronous version of rename
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key name to rename
     * @param new_key New name for the key
     * @return Reference to the Redis handler for chaining
     * @see https://redis.io/commands/rename
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    rename(Func &&func, const std::string &key, const std::string &new_key) {
        return derived().template command<status>(std::forward<Func>(func), "RENAME", key, new_key);
    }

    /**
     * @brief Rename `key` to `newkey` if `newkey` does not exist (coroutine awaitable).
     * @param key Key to be renamed.
     * @param new_key The new name of the key.
     * @return redis_awaiter yielding Reply<bool>
     * @see https://redis.io/commands/renamenx
     */
    auto
    renamenx(const std::string &key, const std::string &new_key) {
        return derived().template make_coro_command<bool>(
            [this, key, new_key](auto &&callback) { this->renamenx(std::move(callback), key, new_key); });
    }

    /**
     * @brief Asynchronous version of renamenx
     * @see https://redis.io/commands/renamenx
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    renamenx(Func &&func, const std::string &key, const std::string &new_key) {
        return derived().template command<bool>(std::forward<Func>(func), "RENAMENX", key, new_key);
    }

    /**
     * @brief Creates a key using the serialized value previously stored using DUMP (coroutine awaitable).
     * @param key Key name to restore
     * @param val Serialized value from DUMP
     * @param ttl Time-to-live in milliseconds
     * @param replace Whether to replace the key if it already exists
     * @return redis_awaiter yielding Reply<status>
     * @see https://redis.io/commands/restore
     */
    auto
    restore(const std::string &key, const std::string &val, long long ttl, bool replace = false) {
        return derived().template make_coro_command<status>(
            [this, key, val, ttl, replace](auto &&callback) { this->restore(std::move(callback), key, val, ttl, replace); });
    }

    /**
     * @brief Asynchronous version of restore
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key name to restore
     * @param val Serialized value from DUMP
     * @param ttl Time-to-live in milliseconds
     * @param replace Whether to replace the key if it already exists
     * @return Reference to the Redis handler for chaining
     * @see https://redis.io/commands/restore
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    restore(Func &&func, const std::string &key, const std::string &val, long long ttl, bool replace = false) {
        std::vector<std::string> opt;
        if (replace) {
            opt = {"REPLACE"};
        }
        return derived().template command<status>(std::forward<Func>(func), "RESTORE", key, ttl, val, opt);
    }

    /**
     * @brief Scan keys of the database matching the given pattern (coroutine awaitable).
     * @param cursor Cursor.
     * @param pattern Pattern of the keys to be scanned.
     * @param count A hint for how many keys to be scanned.
     * @return redis_awaiter yielding Reply<qb::redis::scan<>>
     * @see https://redis.io/commands/scan
     */
    auto
    scan(long long cursor, const std::string &pattern = "*", long long count = 10) {
        return derived().template make_coro_command<qb::redis::scan<>>(
            [this, cursor, pattern, count](auto &&callback) { this->scan(std::move(callback), cursor, pattern, count); });
    }

    /**
     * @brief Asynchronous version of scan (single iteration)
     * @see https://redis.io/commands/scan
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::redis::scan<>> &&>, Derived &>
    scan(Func &&func, long long cursor, const std::string &pattern = "*", long long count = 10) {
        return derived().template command<qb::redis::scan<>>(std::forward<Func>(func), "SCAN", cursor, "MATCH", pattern, "COUNT", count);
    }

    /**
     * @brief Asynchronous version of scan (iterates until cursor 0)
     * @see https://redis.io/commands/scan
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::redis::scan<>> &&>, Derived &>
    scan(Func &&func, const std::string &pattern = "*") {
        scanner<Func>::create_and_start(derived(), pattern, std::forward<Func>(func));
        return derived();
    }

    /**
     * @brief Update the last access time of the given key (coroutine awaitable).
     * @param keys Keys, variadic(could be a string, or a container of keys)... .
     * @return redis_awaiter yielding Reply<long long>
     * @see https://redis.io/commands/touch
     */
    template <typename... Keys>
    auto
    touch(Keys &&...keys) {
        return derived().template make_coro_command<long long>([this, ... keys = std::forward<Keys>(keys)](auto &&callback) mutable {
            this->touch(std::move(callback), std::forward<Keys>(keys)...);
        });
    }

    /**
     * @brief Asynchronous version of touch
     * @see https://redis.io/commands/touch
     */
    template <typename Func, typename... Keys>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    touch(Func &&func, Keys &&...keys) {
        return derived().template command<long long>(std::forward<Func>(func), "TOUCH", std::forward<Keys>(keys)...);
    }

    /**
     * @brief Get the remaining Time-To-Live of a key (coroutine awaitable).
     * @param key Key.
     * @return redis_awaiter yielding Reply<long long>
     * @see https://redis.io/commands/ttl
     */
    auto
    ttl(const std::string &key) {
        return derived().template make_coro_command<long long>([this, key](auto &&callback) { this->ttl(std::move(callback), key); });
    }

    /**
     * @brief Asynchronous version of ttl
     * @see https://redis.io/commands/ttl
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    ttl(Func &&func, const std::string &key) {
        return derived().template command<long long>(std::forward<Func>(func), "TTL", key);
    }

    /**
     * @brief Get the type of the value stored at key (coroutine awaitable).
     * @param key Key.
     * @return redis_awaiter yielding Reply<std::string>
     * @see https://redis.io/commands/type
     */
    auto
    type(const std::string &key) {
        return derived().template make_coro_command<std::string>([this, key](auto &&callback) { this->type(std::move(callback), key); });
    }

    /**
     * @brief Asynchronous version of type
     * @see https://redis.io/commands/type
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::string> &&>, Derived &>
    type(Func &&func, const std::string &key) {
        return derived().template command<std::string>(std::forward<Func>(func), "TYPE", key);
    }

    /**
     * @brief Remove the given key asynchronously, i.e. without blocking Redis (coroutine awaitable).
     * @param keys Keys, variadic(could be a string, or a container of keys)... .
     * @return redis_awaiter yielding Reply<long long>
     * @see https://redis.io/commands/unlink
     */
    template <typename... Keys>
    auto
    unlink(Keys &&...keys) {
        return derived().template make_coro_command<long long>([this, ... keys = std::forward<Keys>(keys)](auto &&callback) mutable {
            this->unlink(std::move(callback), std::forward<Keys>(keys)...);
        });
    }

    /**
     * @brief Asynchronous version of unlink
     * @see https://redis.io/commands/unlink
     */
    template <typename Func, typename... Keys>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    unlink(Func &&func, Keys &&...keys) {
        return derived().template command<long long>(std::forward<Func>(func), "UNLINK", std::forward<Keys>(keys)...);
    }

    /**
     * @brief Wait until previous write commands are successfully replicated (coroutine awaitable).
     * @param num_slaves Number of replicas.
     * @param timeout Timeout in milliseconds.
     * @return redis_awaiter yielding Reply<long long>
     * @see https://redis.io/commands/wait
     */
    auto
    wait(long long num_slaves, long long timeout) {
        return derived().template make_coro_command<long long>(
            [this, num_slaves, timeout](auto &&callback) { this->wait(std::move(callback), num_slaves, timeout); });
    }

    /**
     * @brief Asynchronous version of wait
     * @see https://redis.io/commands/wait
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    wait(Func &&func, long long num_slaves, long long timeout) {
        return derived().template command<long long>(std::forward<Func>(func), "WAIT", num_slaves, timeout);
    }

    /**
     * @brief Wait until previous write commands are successfully replicated to at
     *        least the specified number of replicas or the given timeout has been
     * reached.
     * @param numslaves Number of replicas.
     * @param timeout Timeout in milliseconds. If timeout is 0ms, wait forever.
     * @return Number of replicas that have been successfully replicated these write
     * commands.
     * @note The return value might be less than `numslaves`, because timeout has been
     * reached.
     * @see https://redis.io/commands/wait
     */
    auto
    wait(long long num_slaves, const std::chrono::milliseconds &ttl = std::chrono::milliseconds{0}) {
        return wait(num_slaves, ttl.count());
    }

    /**
     * @brief Asynchronous version of wait (chrono overload)
     * @see https://redis.io/commands/wait
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    wait(Func &&func, long long num_slaves, const std::chrono::milliseconds &ttl = std::chrono::milliseconds{0}) {
        return wait(std::forward<Func>(func), num_slaves, ttl.count());
    }

    // =============== Extended key commands ===============

    /**
     * @brief Copy a key to another key (coroutine awaitable).
     * @param source Source key.
     * @param destination Destination key.
     * @param db Optional destination database.
     * @param replace Whether to replace destination if it exists.
     * @return redis_awaiter yielding Reply<bool>
     * @see https://redis.io/commands/copy
     */
    auto
    copyKey(const std::string &source, const std::string &destination, std::optional<long long> db = std::nullopt, bool replace = false) {
        return derived().template make_coro_command<bool>([this, source, destination, db, replace](auto &&callback) {
            this->copyKey(std::move(callback), source, destination, db, replace);
        });
    }

    /**
     * @brief Asynchronous version of copyKey
     * @see https://redis.io/commands/copy
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    copyKey(Func &&func, const std::string &source, const std::string &destination, std::optional<long long> db = std::nullopt,
            bool replace = false) {
        std::vector<std::string> opt;
        if (db) {
            opt.push_back("DB");
            opt.push_back(std::to_string(*db));
        }
        if (replace) {
            opt.push_back("REPLACE");
        }
        return derived().template command<bool>(std::forward<Func>(func), "COPY", source, destination, opt);
    }

    /**
     * @brief Get the expiration Unix timestamp of a key in seconds (coroutine awaitable).
     * @param key Key.
     * @return redis_awaiter yielding Reply<long long>
     * @see https://redis.io/commands/expiretime
     */
    auto
    expiretime(const std::string &key) {
        return derived().template make_coro_command<long long>([this, key](auto &&callback) { this->expiretime(std::move(callback), key); });
    }

    /**
     * @brief Asynchronous version of expiretime
     * @see https://redis.io/commands/expiretime
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    expiretime(Func &&func, const std::string &key) {
        return derived().template command<long long>(std::forward<Func>(func), "EXPIRETIME", key);
    }

    /**
     * @brief Get the expiration Unix timestamp of a key in milliseconds (coroutine awaitable).
     * @param key Key.
     * @return redis_awaiter yielding Reply<long long>
     * @see https://redis.io/commands/pexpiretime
     */
    auto
    pexpiretime(const std::string &key) {
        return derived().template make_coro_command<long long>([this, key](auto &&callback) { this->pexpiretime(std::move(callback), key); });
    }

    /**
     * @brief Asynchronous version of pexpiretime
     * @see https://redis.io/commands/pexpiretime
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    pexpiretime(Func &&func, const std::string &key) {
        return derived().template command<long long>(std::forward<Func>(func), "PEXPIRETIME", key);
    }

    /**
     * @brief Atomically transfer a key from source to destination Redis instance (coroutine awaitable).
     * @param host Destination host.
     * @param port Destination port.
     * @param key Key to migrate.
     * @param db Destination database.
     * @param timeout Timeout in milliseconds.
     * @param copy Whether to copy instead of move.
     * @param replace Whether to replace key on destination.
     * @param auth Optional password for destination.
     * @return redis_awaiter yielding Reply<status>
     * @see https://redis.io/commands/migrate
     */
    auto
    migrate(const std::string &host, int port, const std::string &key, long long db, long long timeout, bool copy = false, bool replace = false,
            std::optional<std::string> auth = std::nullopt) {
        return derived().template make_coro_command<status>([this, host, port, key, db, timeout, copy, replace, auth](auto &&callback) {
            this->migrate(std::move(callback), host, port, key, db, timeout, copy, replace, auth);
        });
    }

    /**
     * @brief Asynchronous version of migrate
     * @see https://redis.io/commands/migrate
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    migrate(Func &&func, const std::string &host, int port, const std::string &key, long long db, long long timeout, bool copy = false,
            bool replace = false, std::optional<std::string> auth = std::nullopt) {
        std::vector<std::string> opt;
        if (copy)
            opt.push_back("COPY");
        if (replace)
            opt.push_back("REPLACE");
        if (auth) {
            opt.push_back("AUTH");
            opt.push_back(*auth);
        }
        return derived().template command<status>(std::forward<Func>(func), "MIGRATE", host, std::to_string(port), key, std::to_string(db),
                                                  std::to_string(timeout), opt);
    }

    /**
     * @brief Get the internal encoding of a key (coroutine awaitable).
     * @param key Key.
     * @return redis_awaiter yielding Reply<std::optional<std::string>>
     * @see https://redis.io/commands/object
     */
    auto
    objectEncoding(const std::string &key) {
        return derived().template make_coro_command<std::optional<std::string>>(
            [this, key](auto &&callback) { this->objectEncoding(std::move(callback), key); });
    }

    /**
     * @brief Asynchronous version of objectEncoding
     * @see https://redis.io/commands/object
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::string>> &&>, Derived &>
    objectEncoding(Func &&func, const std::string &key) {
        return derived().template command<std::optional<std::string>>(std::forward<Func>(func), "OBJECT", "ENCODING", key);
    }

    /**
     * @brief Get the logarithmic access frequency counter of a key (coroutine awaitable).
     * @param key Key.
     * @return redis_awaiter yielding Reply<std::optional<long long>>
     * @see https://redis.io/commands/object
     */
    auto
    objectFreq(const std::string &key) {
        return derived().template make_coro_command<std::optional<long long>>(
            [this, key](auto &&callback) { this->objectFreq(std::move(callback), key); });
    }

    /**
     * @brief Asynchronous version of objectFreq
     * @see https://redis.io/commands/object
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<long long>> &&>, Derived &>
    objectFreq(Func &&func, const std::string &key) {
        return derived().template command<std::optional<long long>>(std::forward<Func>(func), "OBJECT", "FREQ", key);
    }

    /**
     * @brief Get the idle time of a key in seconds (coroutine awaitable).
     * @param key Key.
     * @return redis_awaiter yielding Reply<std::optional<long long>>
     * @see https://redis.io/commands/object
     */
    auto
    objectIdletime(const std::string &key) {
        return derived().template make_coro_command<std::optional<long long>>(
            [this, key](auto &&callback) { this->objectIdletime(std::move(callback), key); });
    }

    /**
     * @brief Asynchronous version of objectIdletime
     * @see https://redis.io/commands/object
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<long long>> &&>, Derived &>
    objectIdletime(Func &&func, const std::string &key) {
        return derived().template command<std::optional<long long>>(std::forward<Func>(func), "OBJECT", "IDLETIME", key);
    }

    /**
     * @brief Get the reference count of a key (coroutine awaitable).
     * @param key Key.
     * @return redis_awaiter yielding Reply<std::optional<long long>>
     * @see https://redis.io/commands/object
     */
    auto
    objectRefcount(const std::string &key) {
        return derived().template make_coro_command<std::optional<long long>>(
            [this, key](auto &&callback) { this->objectRefcount(std::move(callback), key); });
    }

    /**
     * @brief Asynchronous version of objectRefcount
     * @see https://redis.io/commands/object
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<long long>> &&>, Derived &>
    objectRefcount(Func &&func, const std::string &key) {
        return derived().template command<std::optional<long long>>(std::forward<Func>(func), "OBJECT", "REFCOUNT", key);
    }

    /**
     * @brief Sort elements in a list, set, or sorted set (coroutine awaitable).
     * Basic overload: sort key, returns vector of strings.
     * @param key Key to sort.
     * @param options Optional: BY pattern, LIMIT offset count, GET pattern..., ASC|DESC, ALPHA.
     * @return redis_awaiter yielding Reply<std::vector<std::string>>
     * @see https://redis.io/commands/sort
     */
    auto
    sortKey(const std::string &key, const std::vector<std::string> &options = {}) {
        return derived().template make_coro_command<std::vector<std::string>>(
            [this, key, options](auto &&callback) { this->sortKey(std::move(callback), key, options); });
    }

    /**
     * @brief Asynchronous version of sortKey
     * @see https://redis.io/commands/sort
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>, Derived &>
    sortKey(Func &&func, const std::string &key, const std::vector<std::string> &options = {}) {
        std::vector<std::string> args = {key};
        args.insert(args.end(), options.begin(), options.end());
        return derived().template command<std::vector<std::string>>(std::forward<Func>(func), "SORT", args);
    }

    /**
     * @brief Sort elements and store result in destination (coroutine awaitable).
     * @param key Key to sort.
     * @param destination Destination key for STORE.
     * @param options Optional: BY pattern, LIMIT offset count, GET pattern..., ASC|DESC, ALPHA.
     * @return redis_awaiter yielding Reply<long long> (number of elements stored)
     * @see https://redis.io/commands/sort
     */
    auto
    sortKeyStore(const std::string &key, const std::string &destination, const std::vector<std::string> &options = {}) {
        return derived().template make_coro_command<long long>(
            [this, key, destination, options](auto &&callback) { this->sortKeyStore(std::move(callback), key, destination, options); });
    }

    /**
     * @brief Asynchronous version of sortKeyStore
     * @see https://redis.io/commands/sort
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    sortKeyStore(Func &&func, const std::string &key, const std::string &destination, const std::vector<std::string> &options = {}) {
        std::vector<std::string> args = {key};
        args.insert(args.end(), options.begin(), options.end());
        args.push_back("STORE");
        args.push_back(destination);
        return derived().template command<long long>(std::forward<Func>(func), "SORT", args);
    }

    /**
     * @brief Read-only variant of SORT (coroutine awaitable).
     * @param key Key to sort.
     * @param options Optional: BY pattern, LIMIT offset count, GET pattern..., ASC|DESC, ALPHA.
     * @return redis_awaiter yielding Reply<std::vector<std::string>>
     * @see https://redis.io/commands/sort_ro
     */
    auto
    sortKeyRo(const std::string &key, const std::vector<std::string> &options = {}) {
        return derived().template make_coro_command<std::vector<std::string>>(
            [this, key, options](auto &&callback) { this->sortKeyRo(std::move(callback), key, options); });
    }

    /**
     * @brief Asynchronous version of sortKeyRo
     * @see https://redis.io/commands/sort_ro
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>, Derived &>
    sortKeyRo(Func &&func, const std::string &key, const std::vector<std::string> &options = {}) {
        std::vector<std::string> args = {key};
        args.insert(args.end(), options.begin(), options.end());
        return derived().template command<std::vector<std::string>>(std::forward<Func>(func), "SORT_RO", args);
    }

    /**
     * @brief Block until the local server and the given number of replicas persist the
     *        AOF, returning the reached counts (coroutine awaitable).
     * @param num_local Number of local (this server) AOF fsyncs to wait for (0 or 1).
     * @param num_replicas Number of replicas that must persist to their AOF.
     * @param timeout Timeout in milliseconds (0 = block forever).
     * @return redis_awaiter yielding Reply<std::vector<long long>>
     * @note WAITAOF replies with a two-element integer array `[numlocal, numreplicas]`
     *       (NOT a single integer like WAIT) — the count of local fsyncs and the number
     *       of replicas that acknowledged the AOF. Confirmed against Redis 7.2+/8.x in
     *       both RESP2 and RESP3.
     * @see https://redis.io/commands/waitaof
     */
    auto
    waitaof(long long num_local, long long num_replicas, long long timeout) {
        return derived().template make_coro_command<std::vector<long long>>([this, num_local, num_replicas, timeout](auto &&callback) {
            this->waitaof(std::move(callback), num_local, num_replicas, timeout);
        });
    }

    /**
     * @brief Asynchronous version of waitaof
     * @see https://redis.io/commands/waitaof
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<long long>> &&>, Derived &>
    waitaof(Func &&func, long long num_local, long long num_replicas, long long timeout) {
        return derived().template command<std::vector<long long>>(std::forward<Func>(func), "WAITAOF", num_local, num_replicas, timeout);
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_KEY_COMMANDS_H
