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

#ifndef QBM_REDIS_KEY_COMMANDS_H
#define QBM_REDIS_KEY_COMMANDS_H
#include <chrono>
#include "reply.h"

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
    /**
     * @class scanner
     * @brief Helper class for implementing incremental scanning of keys
     * 
     * @tparam Func Callback function type
     */
    template <typename Func>
    class scanner {
        Derived &_handler;
        std::string _pattern;
        Func _func;
        qb::redis::Reply<qb::redis::reply::scan<>> _reply;

    public:
        /**
         * @brief Constructs a scanner for keys matching a pattern
         * 
         * @param handler The Redis handler
         * @param pattern Pattern to match keys against
         * @param func Callback function to process results
         */
        scanner(Derived &handler, std::string pattern, Func &&func)
            : _handler(handler)
            , _pattern(std::move(pattern))
            , _func(std::forward<Func>(func)) {
            _handler.scan(std::ref(*this), 0, _pattern, 100);
        }

        /**
         * @brief Processes scan results and continues scanning if needed
         * 
         * @param reply The scan operation reply
         */
        void
        operator()(qb::redis::Reply<qb::redis::reply::scan<>> &&reply) {
            _reply.ok = reply.ok;
            std::move(reply.result.items.begin(), reply.result.items.end(), std::back_inserter(_reply.result.items));
            if (reply.ok && reply.result.cursor)
                _handler.scan(std::ref(*this), reply.result.cursor, _pattern, 100);
            else {
                _func(std::move(_reply));
                delete this;
            }
        }
    };

public:
    /**
     * @brief Delete the given keys.
     * @param keys Keys, variadic(could be a string, or a container of keys)... .
     * @return Number of keys that have been deleted.
     * @see https://redis.io/commands/del
     */
    template <typename... Keys>
    long long
    del(Keys &&...keys) {
        return static_cast<Derived &>(*this).template command<long long>("DEL", std::forward<Keys>(keys)...).result;
    }
    template <typename Func, typename... Keys>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    del(Func &&func, Keys &&...keys) {
        return static_cast<Derived &>(*this)
            .template command<long long>(std::forward<Func>(func), "DEL", std::forward<Keys>(keys)...);
    }

    /**
     * @brief Dump the serialized value of the key.
     * @param key Key.
     * @return The serialized value.
     * @note The returned value can only be processed by `RESTORE` command.
     * @see `Redis::restore`
     * @see https://redis.io/commands/dump
     */
    std::optional<std::string>
    dump(const std::string &key) {
        return static_cast<Derived &>(*this).template command<std::optional<std::string>>("DUMP", key).result;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::string>> &&>, Derived &>
    dump(Func &&func, const std::string &key) {
        return static_cast<Derived &>(*this).template command<std::optional<std::string>>(
            std::forward<Func>(func),
            "DUMP",
            key);
    }

    /**
     * @brief Check if the given keys exist.
     * @param keys Keys, variadic(could be a string, or a container of keys)... .
     * @return Number of keys that exist in the database.
     * @see https://redis.io/commands/exists
     */
    template <typename... Keys>
    long long
    exists(Keys &&...keys) {
        return static_cast<Derived &>(*this).template command<long long>("EXISTS", std::forward<Keys>(keys)...).result;
    }
    template <typename Func, typename... Keys>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    exists(Func &&func, Keys &&...keys) {
        return static_cast<Derived &>(*this)
            .template command<long long>(std::forward<Func>(func), "EXISTS", std::forward<Keys>(keys)...);
    }

    /**
     * @brief Set the expiration time of a key to be `timeout` seconds.
     * @param key Key.
     * @param timeout TTL in seconds.
     * @return Whether the timeout has been set.
     * @retval true If the timeout has been set.
     * @retval false If the key does not exist.
     * @see https://redis.io/commands/expire
     */
    bool
    expire(const std::string &key, long long timeout) {
        return static_cast<Derived &>(*this).template command<bool>("EXPIRE", key, timeout).ok;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    expire(Func &&func, const std::string &key, long long timeout) {
        return static_cast<Derived &>(*this).template command<bool>(std::forward<Func>(func), "EXPIRE", key, timeout);
    }

    /**
     * @brief Set the expiration time of a key to be `timeout` seconds.
     * @param key Key.
     * @param timeout TTL in seconds.
     * @return Whether the timeout has been set.
     * @retval true If the timeout has been set.
     * @retval false If the key does not exist.
     * @see https://redis.io/commands/expire
     */
    bool
    expire(const std::string &key, const std::chrono::seconds &timeout) {
        return expire(key, timeout.count());
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    expire(Func &&func, const std::string &key, const std::chrono::seconds &timeout) {
        return expire(std::forward<Func>(func), key, timeout.count());
    }

    /**
     * @brief Set the expiration time of a key to be a UNIX timestamp.
     * @param key Key.
     * @param timestamp UNIX timestamp.
     * @return Whether the timeout has been set.
     * @retval true If the timeout has been set.
     * @retval false If the key does not exist.
     * @see https://redis.io/commands/expireat
     */
    bool
    expireat(const std::string &key, long long timestamp) {
        return static_cast<Derived &>(*this).template command<bool>("EXPIREAT", key, timestamp).ok;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    expireat(Func &&func, const std::string &key, long long timestamp) {
        return static_cast<Derived &>(*this).template command<bool>(
            std::forward<Func>(func),
            "EXPIREAT",
            key,
            timestamp);
    }

    /**
     * @brief Set a timeout on key, i.e. expire the key at a future time point.
     * @param key Key.
     * @param timestamp Time in seconds since UNIX epoch.
     * @return Whether timeout has been set.
     * @retval true If timeout has been set.
     * @retval false If key does not exist.
     * @see https://redis.io/commands/expireat
     */
    bool
    expireat(
        const std::string &key, const std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds> &tp) {
        return expireat(key, tp.time_since_epoch().count());
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    expireat(
        Func &&func, const std::string &key,
        const std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds> &tp) {
        return expireat(std::forward<Func>(func), key, tp.time_since_epoch().count());
    }

    /**
     * @brief Get all keys matching the given pattern.
     * @param pattern Pattern, supporting glob-style patterns.
     * @return All keys matching the given pattern.
     * @note This command might block Redis server when used with large datasets, use with caution.
     * @see `Redis::scan` for a non-blocking alternative
     * @see https://redis.io/commands/keys
     */
    std::vector<std::string>
    keys(const std::string &pattern = "*") {
        return static_cast<Derived &>(*this).template command<std::vector<std::string>>("KEYS", pattern).result;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>, Derived &>
    keys(Func &&func, const std::string &pattern = "*") {
        return static_cast<Derived &>(*this).template command<std::vector<std::string>>(
            std::forward<Func>(func),
            "KEYS",
            pattern);
    }

    /**
     * @brief Move the key from a database to another one.
     * @param key Key.
     * @param destination_db Destination database ID.
     * @return Whether we successfully moved the key.
     * @retval true If the key has been moved.
     * @retval false If the key was not moved, because of some errors, e.g. key does not exist.
     * @see https://redis.io/commands/move
     */
    bool
    move(const std::string &key, long long destination_db) {
        return static_cast<Derived &>(*this).template command<bool>("MOVE", key, destination_db).ok;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    move(Func &&func, const std::string &key, long long destination_db) {
        return static_cast<Derived &>(*this).template command<bool>(
            std::forward<Func>(func),
            "MOVE",
            key,
            destination_db);
    }

    /**
     * @brief Remove timeout on key.
     * @param key Key.
     * @return Whether timeout has been removed.
     * @retval true If timeout has been removed.
     * @retval false If key does not exist, or does not have an associated timeout.
     * @see https://redis.io/commands/persist
     */
    bool
    persist(const std::string &key) {
        return static_cast<Derived &>(*this).template command<bool>("PERSIST", key).ok;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    persist(Func &&func, const std::string &key) {
        return static_cast<Derived &>(*this).template command<bool>(std::forward<Func>(func), "PERSIST", key);
    }

    /**
     * @brief Set a timeout on key.
     * @param key Key.
     * @param timeout Timeout in milliseconds.
     * @return Whether timeout has been set.
     * @retval true If timeout has been set.
     * @retval false If key does not exist.
     * @see https://redis.io/commands/pexpire
     */
    bool
    pexpire(const std::string &key, long long timeout) {
        return static_cast<Derived &>(*this).template command<bool>("PEXPIRE", key, timeout).ok;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    pexpire(Func &&func, const std::string &key, long long timeout) {
        return static_cast<Derived &>(*this).template command<bool>(std::forward<Func>(func), "PEXPIRE", key, timeout);
    }

    /**
     * @brief Set a timeout on key.
     * @param key Key.
     * @param timeout Timeout in milliseconds.
     * @return Whether timeout has been set.
     * @retval true If timeout has been set.
     * @retval false If key does not exist.
     * @see https://redis.io/commands/pexpire
     */
    bool
    pexpire(const std::string &key, const std::chrono::milliseconds &timeout) {
        return pexpire(key, timeout.count());
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    pexpire(Func &&func, const std::string &key, const std::chrono::milliseconds &timeout) {
        return pexpire(std::forward<Func>(func), key, timeout.count());
    }

    /**
     * @brief Set a timeout on key, i.e. expire the key at a future time point.
     * @param key Key.
     * @param timestamp Time in milliseconds since UNIX epoch.
     * @return Whether timeout has been set.
     * @retval true If timeout has been set.
     * @retval false If key does not exist.
     * @see https://redis.io/commands/pexpireat
     */
    bool
    pexpireat(const std::string &key, long long timestamp) {
        return static_cast<Derived &>(*this).template command<bool>("PEXPIREAT", key, timestamp).ok;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    pexpireat(Func &&func, const std::string &key, long long timestamp) {
        return static_cast<Derived &>(*this)
            .template command<bool>(std::forward<Func>(func), "PEXPIREAT", key, timestamp);
    }

    /**
     * @brief Set a timeout on key, i.e. expire the key at a future time point.
     * @param key Key.
     * @param timestamp Time in milliseconds since UNIX epoch.
     * @return Whether timeout has been set.
     * @retval true If timeout has been set.
     * @retval false If key does not exist.
     * @see https://redis.io/commands/pexpireat
     */
    bool
    pexpireat(
        const std::string &key,
        const std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds> &tp) {
        return pexpireat(key, tp.time_since_epoch().count());
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    pexpireat(
        Func &&func, const std::string &key,
        const std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds> &tp) {
        return pexpireat(std::forward<Func>(func), key, tp.time_since_epoch().count());
    }

    /**
     * @brief Get the TTL of a key in milliseconds.
     * @param key Key.
     * @return TTL of the key in milliseconds.
     * @see https://redis.io/commands/pttl
     */
    long long
    pttl(const std::string &key) {
        return static_cast<Derived &>(*this).template command<long long>("PTTL", key).result;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    pttl(Func &&func, const std::string &key) {
        return static_cast<Derived &>(*this).template command<long long>(std::forward<Func>(func), "PTTL", key);
    }

    /**
     * @brief Get a random key from current database.
     * @return A random key.
     * @note If the database is empty, `randomkey` returns `OptionalString{}` (`std::nullopt`).
     * @see https://redis.io/commands/randomkey
     */
    std::optional<std::string>
    randomkey() {
        return static_cast<Derived &>(*this).template command<std::optional<std::string>>("RANDOMKEY").result;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::string>> &&>, Derived &>
    randomkey(Func &&func) {
        return static_cast<Derived &>(*this).template command<std::optional<std::string>>(
            std::forward<Func>(func),
            "RANDOMKEY");
    }

    /**
     * @brief Rename `key` to `newkey`.
     * @param key Key to be renamed.
     * @param newkey The new name of the key.
     * @see https://redis.io/commands/rename
     */
    bool
    rename(const std::string &key, const std::string &new_key) {
        return static_cast<Derived &>(*this).template command<void>("RENAME", key, new_key).ok;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    rename(Func &&func, const std::string &key, const std::string &new_key) {
        return static_cast<Derived &>(*this).template command<void>(std::forward<Func>(func), "RENAME", key, new_key);
    }

    /**
     * @brief Rename `key` to `newkey` if `newkey` does not exist.
     * @param key Key to be renamed.
     * @param newkey The new name of the key.
     * @return Whether key has been renamed.
     * @retval true If key has been renamed.
     * @retval false If newkey already exists.
     * @see https://redis.io/commands/renamenx
     */
    bool
    renamenx(const std::string &key, const std::string &new_key) {
        return static_cast<Derived &>(*this).template command<bool>("RENAMENX", key, new_key).ok;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    renamenx(Func &&func, const std::string &key, const std::string &new_key) {
        return static_cast<Derived &>(*this).template command<bool>(std::forward<Func>(func), "RENAMENX", key, new_key);
    }

    /**
     * @brief Create a key with the value obtained by `Redis::dump`.
     * @param key Key.
     * @param val Value obtained by `Redis::dump`.
     * @param ttl Timeout of the created key in milliseconds. If `ttl` is 0, set no timeout.
     * @param replace Whether to overwrite an existing key.
     *                If `replace` is `false` and key already exists, throw an exception.
     * @see https://redis.io/commands/restore
     */
    bool
    restore(const std::string &key, const std::string &val, long long ttl, bool replace = false) {
        std::optional<std::string> opt;
        if (replace)
            opt = "REPLACE";
        return static_cast<Derived &>(*this).template command<void>("RESTORE", key, ttl, val, opt).ok;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    restore(Func &&func, const std::string &key, const std::string &val, long long ttl, bool replace = false) {
        std::optional<std::string> opt;
        if (replace)
            opt = "REPLACE";
        return static_cast<Derived &>(*this)
            .template command<void>(std::forward<Func>(func), "RESTORE", key, ttl, val, opt);
    }

    /**
     * @brief Create a key with the value obtained by `Redis::dump`.
     * @param key Key.
     * @param val Value obtained by `Redis::dump`.
     * @param ttl Timeout of the created key in milliseconds. If `ttl` is 0, set no timeout.
     * @param replace Whether to overwrite an existing key.
     *                If `replace` is `false` and key already exists, throw an exception.
     * @see https://redis.io/commands/restore
     */
    bool
    restore(
        const std::string &key, const std::string &val,
        const std::chrono::milliseconds &ttl = std::chrono::milliseconds{0}, bool replace = false) {
        return restore(key, val, ttl.count(), replace);
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    restore(
        Func &&func, const std::string &key, const std::string &val,
        const std::chrono::milliseconds &ttl = std::chrono::milliseconds{0}, bool replace = false) {
        return restore(std::forward<Func>(func), key, val, ttl.count(), replace);
    }

    /**
     * @brief Scan keys of the database matching the given pattern.
     *
     * Example:
     * @code{.cpp}
     * auto cursor = 0LL;
     * std::vector<std::string> keys;
     * while (true) {
     *     auto scan = redis.scan(cursor, "pattern:*", 10);
     *     std::move(scan.items.begin(), scan.items.end(), std::back_inserter(keys));
     *     cursor = scan.cursor;
     *     if (cursor == 0) {
     *         break;
     *     }
     * }
     * @endcode
     * @param cursor Cursor.
     * @param pattern Pattern of the keys to be scanned.
     * @param count A hint for how many keys to be scanned.
     * @return scan.items the key(s) found.
     * @return scan.cursor the cursor to be used for the next scan operation.
     * @see https://redis.io/commands/scan
     */
    reply::scan<>
    scan(long long cursor, const std::string &pattern = "*", long long count = 10) {
        return static_cast<Derived &>(*this)
            .template command<reply::scan<>>("SCAN", cursor, "MATCH", pattern, "COUNT", count)
            .result;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<reply::scan<>> &&>, Derived &>
    scan(Func &&func, long long cursor, const std::string &pattern = "*", long long count = 10) {
        return static_cast<Derived &>(*this).template command<reply::scan<>>(
            std::forward<Func>(func),
            "SCAN",
            cursor,
            "MATCH",
            pattern,
            "COUNT",
            count);
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<reply::scan<>> &&>, Derived &>
    scan(Func &&func, const std::string &pattern = "*") {
        new scanner<Func>{static_cast<Derived &>(*this), pattern, std::forward<Func>(func)};
        return static_cast<Derived &>(*this);
    }

    /**
     * @brief Update the last access time of the given key.
     * @param keys Keys, variadic(could be a string, or a container of keys)... .
     * @return Whether last access time of the key has been updated.
     * @retval 1 If key exists, and last access time has been updated.
     * @retval 0 If key does not exist.
     * @see https://redis.io/commands/touch
     */
    template <typename... Keys>
    long long
    touch(Keys &&...keys) {
        return static_cast<Derived &>(*this).template command<long long>("TOUCH", std::forward<Keys>(keys)...).result;
    }
    template <typename Func, typename... Keys>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    touch(Func &&func, Keys &&...keys) {
        return static_cast<Derived &>(*this).template command<long long>(
            std::forward<Func>(func),
            "TOUCH",
            std::forward<Keys>(keys)...);
    }

    /**
     * @brief Get the remaining Time-To-Live of a key.
     * @param key Key.
     * @return TTL in seconds.
     * @retval TTL If the key has a timeout.
     * @retval -1 If the key exists but does not have a timeout.
     * @retval -2 If the key does not exist.
     * @note In Redis 2.6 or older, `ttl` returns -1 if the key does not exist,
     *       or if the key exists but does not have a timeout.
     * @see https://redis.io/commands/ttl
     */
    long long
    ttl(const std::string &key) {
        return static_cast<Derived &>(*this).template command<long long>("TTL", key).result;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    ttl(Func &&func, const std::string &key) {
        return static_cast<Derived &>(*this).template command<long long>(std::forward<Func>(func), "TTL", key);
    }

    /**
     * @brief Get the type of the value stored at key.
     * @param key Key.
     * @return The type of the value.
     * @see https://redis.io/commands/type
     */
    std::string
    type(const std::string &key) {
        return static_cast<Derived &>(*this).template command<std::string>("TYPE", key).result;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::string> &&>, Derived &>
    type(Func &&func, const std::string &key) {
        return static_cast<Derived &>(*this).template command<std::string>(std::forward<Func>(func), "TYPE", key);
    }

    /**
     * @brief Remove the given key asynchronously, i.e. without blocking Redis.
     * @param keys Keys, variadic(could be a string, or a container of keys)... .
     * @return Number of keys that have been removed.
     * @see https://redis.io/commands/unlink
     */
    template <typename... Keys>
    long long
    unlink(Keys &&...keys) {
        return static_cast<Derived &>(*this).template command<long long>("UNLINK", std::forward<Keys>(keys)...).result;
    }
    template <typename Func, typename... Keys>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    unlink(Func &&func, Keys &&...keys) {
        return static_cast<Derived &>(*this).template command<long long>(
            std::forward<Func>(func),
            "UNLINK",
            std::forward<Keys>(keys)...);
    }

    /**
     * @brief Wait until previous write commands are successfully replicated to at
     *        least the specified number of replicas or the given timeout has been reached.
     * @param numslaves Number of replicas.
     * @param timeout Timeout in milliseconds. If timeout is 0ms, wait forever.
     * @return Number of replicas that have been successfully replicated these write commands.
     * @note The return value might be less than `numslaves`, because timeout has been reached.
     * @see https://redis.io/commands/wait
     */
    long long
    wait(long long num_slaves, long long timeout) {
        return static_cast<Derived &>(*this).template command<long long>("WAIT", num_slaves, timeout).result;
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    wait(Func &&func, long long num_slaves, long long timeout) {
        return static_cast<Derived &>(*this)
            .template command<long long>(std::forward<Func>(func), "WAIT", num_slaves, timeout);
    }

    /**
     * @brief Wait until previous write commands are successfully replicated to at
     *        least the specified number of replicas or the given timeout has been reached.
     * @param numslaves Number of replicas.
     * @param timeout Timeout in milliseconds. If timeout is 0ms, wait forever.
     * @return Number of replicas that have been successfully replicated these write commands.
     * @note The return value might be less than `numslaves`, because timeout has been reached.
     * @see https://redis.io/commands/wait
     */
    long long
    wait(long long num_slaves, const std::chrono::milliseconds &ttl = std::chrono::milliseconds{0}) {
        return wait(num_slaves, ttl.count());
    }
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    wait(Func &&func, long long num_slaves, const std::chrono::milliseconds &ttl = std::chrono::milliseconds{0}) {
        return wait(std::forward<Func>(func), num_slaves, ttl.count());
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_KEY_COMMANDS_H
