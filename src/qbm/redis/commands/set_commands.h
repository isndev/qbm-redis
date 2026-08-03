/**
 * @file qbm/redis/commands/set_commands.h
 * @brief Redis set command mixin (SADD, SMEMBERS, SINTER, SSCAN, ...).
 *
 * Provides the CRTP command mixin implementing the Redis set command family.
 * Every command is exposed in two forms: a coroutine-awaitable overload and a
 * callback-based asynchronous overload, both backed by the derived connection.
 *
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2026 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)
 * @ingroup Redis
 */
#ifndef QBM_REDIS_SET_COMMANDS_H
#define QBM_REDIS_SET_COMMANDS_H

#include <type_traits>
#include <qb/system/container/unordered_set.h> // for qb::unordered_set (previously picked up transitively via listener.h)
#include "../reply.h"

namespace qb::redis {

/**
 * @class set_commands
 * @brief Provides Redis set command implementations.
 *
 * This class implements Redis set operations, which provide an unordered collection
 * of unique strings. Commands return awaiters for coroutine-first async I/O.
 *
 * Redis sets are particularly useful for expressing relations between objects and
 * for quickly checking membership of elements.
 *
 * @tparam Derived The derived class type (CRTP pattern)
 */
template <typename Derived>
class set_commands {
private:
    constexpr Derived &
    derived() {
        return static_cast<Derived &>(*this);
    }

    /**
     * @class scanner
     * @brief Helper class for implementing incremental scanning of sets
     *
     * Uses shared_ptr for automatic memory management. Safe even if exceptions occur.
     *
     * @tparam Func Decayed callback type; never a reference. The scanner owns its callback:
     *              it keeps itself alive across the cursor round-trips and therefore outlives
     *              the sscan() call that built it, so anything it merely referred to would be
     *              long gone by the time the callback fires. See the sscan() call site.
     */
    template <typename Func>
    class scanner : public std::enable_shared_from_this<scanner<Func>> {
        Derived                            &_handler;
        std::string                         _key;
        std::string                         _pattern;
        Func                                _func;
        qb::redis::Reply<qb::redis::scan<>> _reply;
        bool                                _started{false};

    public:
        /**
         * @brief Constructs a scanner for set elements
         *
         * @param handler The Redis handler
         * @param key Key where the set is stored
         * @param pattern Pattern to filter set members
         * @param func Callback function to process results
         */
        scanner(Derived &handler, std::string key, std::string pattern, Func func)
            : _handler(handler)
            , _key(std::move(key))
            , _pattern(std::move(pattern))
            , _func(std::move(func)) {}

        /**
         * @brief Start the scanning process
         */
        void
        start() {
            if (!_started) {
                _started  = true;
                auto self = this->shared_from_this();
                _handler.sscan([self](auto &&reply) { (*self)(std::forward<decltype(reply)>(reply)); }, _key, 0, _pattern, 100);
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
                auto self = this->shared_from_this();
                _handler.sscan([self](auto &&reply) { (*self)(std::forward<decltype(reply)>(reply)); }, _key, reply.result().cursor, _pattern,
                               100);
            } else {
                try {
                    _func(std::move(_reply));
                } catch (std::exception const &e) {
                    LOG_WARN("[qbm][redis] set scanner callback failed: " << e.what());
                }
            }
        }

        /**
         * @brief Factory method to create and start a scanner safely
         */
        static void
        create_and_start(Derived &handler, std::string key, std::string pattern, Func func) {
            auto ptr = std::make_shared<scanner>(handler, std::move(key), std::move(pattern), std::move(func));
            ptr->start();
        }
    };

public:
    // =============== Basic Set Operations ===============

    /**
     * @brief Adds members to a set (coroutine awaitable)
     *
     * @tparam Members Variadic types for set members
     * @param key Key where the set is stored
     * @param members Members to add to the set
     * @return redis_awaiter yielding Reply<long long>
     * @see https://redis.io/commands/sadd
     */
    template <typename... Members>
    auto
    sadd(const std::string &key, Members &&...members) {
        return derived().template make_coro_command<long long>(
            [this, key, ... members = std::forward<Members>(members)](auto &&callback) mutable {
                this->sadd(std::move(callback), key, std::forward<decltype(members)>(members)...);
            });
    }

    /**
     * @brief Asynchronous version of sadd
     *
     * @tparam Func Callback function type
     * @tparam Members Variadic types for set members
     * @param func Callback function
     * @param key Key where the set is stored
     * @param members Members to add to the set
     * @return Reference to the derived class
     * @see https://redis.io/commands/sadd
     */
    template <typename Func, typename... Members>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    sadd(Func &&func, const std::string &key, Members &&...members) {
        if (key.empty() || sizeof...(members) == 0) {
            fail_client<long long>(std::forward<Func>(func), "SADD requires at least one member");
            return derived();
        }
        return derived().template command<long long>(std::forward<Func>(func), "SADD", key, std::forward<Members>(members)...);
    }

    /**
     * @brief Gets the number of members in a set (coroutine awaitable)
     *
     * @param key Key where the set is stored
     * @return redis_awaiter yielding Reply<long long>
     * @see https://redis.io/commands/scard
     */
    auto
    scard(const std::string &key) {
        return derived().template make_coro_command<long long>([this, key](auto &&callback) { this->scard(std::move(callback), key); });
    }

    /**
     * @brief Asynchronous version of scard
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the set is stored
     * @return Reference to the derived class
     * @see https://redis.io/commands/scard
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    scard(Func &&func, const std::string &key) {
        // Never a silent no-op: a bare `return` leaves the callback unfired, and on the coroutine
        // form the awaiter parks forever waiting for a reply that was never sent (a hang).
        if (key.empty()) {
            fail_client<long long>(std::forward<Func>(func), "SCARD requires a non-empty key");
            return derived();
        }
        return derived().template command<long long>(std::forward<Func>(func), "SCARD", key);
    }

    // =============== Set Operations ===============

    /**
     * @brief Subtracts multiple sets
     *
     * @param keys Keys where the sets are stored
     * @return Members of the resulting set (difference between first set and all others)
     * @note Time complexity: O(N) where N is the total number of elements in all sets
     * @see https://redis.io/commands/sdiff
     */
    auto
    sdiff(const std::vector<std::string> &keys) {
        return derived().template make_coro_command<std::vector<std::string>>(
            [this, keys](auto &&callback) { this->sdiff(std::move(callback), keys); });
    }

    /**
     * @brief Asynchronous version of sdiff
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param keys Keys where the sets are stored
     * @return Reference to the derived class
     * @see https://redis.io/commands/sdiff
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>, Derived &>
    sdiff(Func &&func, const std::vector<std::string> &keys) {
        if (keys.size() == 0) {
            fail_client<std::vector<std::string>>(std::forward<Func>(func), "SDIFF requires at least one key");
            return derived();
        }

        return derived().template command<std::vector<std::string>>(std::forward<Func>(func), "SDIFF", keys);
    }

    /**
     * @brief Store the difference of sets in a destination key (coroutine awaitable)
     *
     * @param destination Destination key for the result
     * @param keys Source keys where the sets are stored
     * @return redis_awaiter yielding Reply<long long> (number of elements in result)
     * @see https://redis.io/commands/sdiffstore
     */
    auto
    sdiffstore(const std::string &destination, const std::vector<std::string> &keys) {
        return derived().template make_coro_command<long long>(
            [this, destination, keys](auto &&callback) { this->sdiffstore(std::move(callback), destination, keys); });
    }

    /**
     * @brief Asynchronous version of sdiffstore
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param destination Destination key where the resulting set will be stored
     * @param keys Source keys where the sets are stored
     * @return Reference to the derived class
     * @see https://redis.io/commands/sdiffstore
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    sdiffstore(Func &&func, const std::string &destination, const std::vector<std::string> &keys) {
        if (destination.empty() || keys.size() == 0) {
            fail_client<long long>(std::forward<Func>(func), "SDIFFSTORE requires at least one key");
            return derived();
        }

        return derived().template command<long long>(std::forward<Func>(func), "SDIFFSTORE", destination, keys);
    }

    /**
     * @brief Returns the intersection of multiple sets (coroutine awaitable)
     *
     * @param keys Keys where the sets are stored
     * @return redis_awaiter yielding Reply<std::vector<std::string>>
     * @note Time complexity: O(N*M) where N is the cardinality of the smallest
     *       set and M is the number of sets
     * @see https://redis.io/commands/sinter
     */
    auto
    sinter(const std::vector<std::string> &keys) {
        return derived().template make_coro_command<std::vector<std::string>>(
            [this, keys](auto &&callback) { this->sinter(std::move(callback), keys); });
    }

    /**
     * @brief Asynchronous version of sinter
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param keys Keys where the sets are stored
     * @return Reference to the derived class
     * @see https://redis.io/commands/sinter
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>, Derived &>
    sinter(Func &&func, const std::vector<std::string> &keys) {
        if (keys.size() == 0) {
            fail_client<std::vector<std::string>>(std::forward<Func>(func), "SINTER requires at least one key");
            return derived();
        }

        return derived().template command<std::vector<std::string>>(std::forward<Func>(func), "SINTER", keys);
    }

    /**
     * @brief Returns the cardinality of the intersection of sets (coroutine awaitable)
     *
     * @param keys Keys where the sets are stored
     * @param limit Optional maximum number of elements to count
     * @return redis_awaiter yielding Reply<long long>
     * @see https://redis.io/commands/sintercard
     */
    auto
    sintercard(const std::vector<std::string> &keys, std::optional<long long> limit = std::nullopt) {
        return derived().template make_coro_command<long long>(
            [this, keys, limit](auto &&callback) mutable { this->sintercard(std::move(callback), keys, std::move(limit)); });
    }

    /**
     * @brief Asynchronous version of sintercard
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param keys Keys where the sets are stored
     * @param limit Maximum number of elements to count (optional)
     * @return Reference to the derived class
     * @see https://redis.io/commands/sintercard
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    sintercard(Func &&func, const std::vector<std::string> &keys, std::optional<long long> limit = std::nullopt) {
        if (keys.size() == 0) {
            fail_client<long long>(std::forward<Func>(func), "SINTERCARD requires at least one key");
            return derived();
        }
        std::vector<std::string> args;
        args.reserve(2);
        if (limit) {
            args.push_back("LIMIT");
            args.push_back(std::to_string(*limit));
        }
        return derived().template command<long long>(std::forward<Func>(func), "SINTERCARD", keys.size(), keys, args);
    }

    /**
     * @brief Store the intersection of sets in a destination key (coroutine awaitable)
     *
     * @param destination Destination key for the result
     * @param keys Source keys where the sets are stored
     * @return redis_awaiter yielding Reply<long long> (number of elements in result)
     * @see https://redis.io/commands/sinterstore
     */
    auto
    sinterstore(const std::string &destination, const std::vector<std::string> &keys) {
        return derived().template make_coro_command<long long>(
            [this, destination, keys](auto &&callback) { this->sinterstore(std::move(callback), destination, keys); });
    }

    /**
     * @brief Asynchronous version of sinterstore
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param destination Destination key where the resulting set will be stored
     * @param keys Source keys where the sets are stored
     * @return Reference to the derived class
     * @see https://redis.io/commands/sinterstore
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    sinterstore(Func &&func, const std::string &destination, const std::vector<std::string> &keys) {
        if (destination.empty() || keys.size() == 0) {
            fail_client<long long>(std::forward<Func>(func), "SINTERSTORE requires at least one key");
            return derived();
        }
        return derived().template command<long long>(std::forward<Func>(func), "SINTERSTORE", destination, keys);
    }

    /**
     * @brief Determines if a member is in a set
     *
     * @param key Key where the set is stored
     * @param member Member to check
     * @return true if the member exists in the set, false otherwise
     * @note Time complexity: O(1)
     * @see https://redis.io/commands/sismember
     */
    auto
    sismember(const std::string &key, const std::string &member) {
        return derived().template make_coro_command<bool>(
            [this, key, member](auto &&callback) { this->sismember(std::move(callback), key, member); });
    }

    /**
     * @brief Asynchronous version of sismember
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the set is stored
     * @param member Member to check
     * @return Reference to the derived class
     * @see https://redis.io/commands/sismember
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    sismember(Func &&func, const std::string &key, const std::string &member) {
        // Never a silent no-op: a bare `return` leaves the callback unfired, and on the coroutine
        // form the awaiter parks forever waiting for a reply that was never sent (a hang).
        if (key.empty() || member.empty()) {
            fail_client<bool>(std::forward<Func>(func), "SISMEMBER requires a non-empty key and member");
            return derived();
        }
        return derived().template command<bool>(std::forward<Func>(func), "SISMEMBER", key, member);
    }

    /**
     * @brief Determines if multiple members are in a set
     *
     * @tparam Members Variadic types for members to check
     * @param key Key where the set is stored
     * @param members Members to check
     * @return Vector of boolean values indicating membership
     * @note Time complexity: O(N) where N is the number of members to check
     * @see https://redis.io/commands/smismember
     */
    template <typename... Members>
    auto
    smismember(const std::string &key, Members &&...members) {
        return derived().template make_coro_command<std::vector<bool>>(
            [this, key, ... members = std::forward<Members>(members)](auto &&callback) mutable {
                this->smismember(std::move(callback), key, std::forward<decltype(members)>(members)...);
            });
    }

    /**
     * @brief Asynchronous version of smismember
     *
     * @tparam Func Callback function type
     * @tparam Members Variadic types for members to check
     * @param func Callback function
     * @param key Key where the set is stored
     * @param members Members to check
     * @return Reference to the derived class
     * @see https://redis.io/commands/smismember
     */
    template <typename Func, typename... Members>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<bool>> &&>, Derived &>
    smismember(Func &&func, const std::string &key, Members &&...members) {
        if (key.empty() || sizeof...(members) == 0) {
            fail_client<std::vector<bool>>(std::forward<Func>(func), "SMISMEMBER requires at least one member");
            return derived();
        }
        return derived().template command<std::vector<bool>>(std::forward<Func>(func), "SMISMEMBER", key, std::forward<Members>(members)...);
    }

    /**
     * @brief Returns all members of a set (coroutine awaitable)
     *
     * @param key Key where the set is stored
     * @return redis_awaiter yielding Reply<qb::unordered_set<std::string>>
     * @see https://redis.io/commands/smembers
     */
    auto
    smembers(const std::string &key) {
        return derived().template make_coro_command<qb::unordered_set<std::string>>(
            [this, key](auto &&callback) { this->smembers(std::move(callback), key); });
    }

    /**
     * @brief Asynchronous version of smembers
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the set is stored
     * @return Reference to the derived class
     * @see https://redis.io/commands/smembers
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::unordered_set<std::string>> &&>, Derived &>
    smembers(Func &&func, const std::string &key) {
        // Never a silent no-op: a bare `return` leaves the callback unfired, and on the coroutine
        // form the awaiter parks forever waiting for a reply that was never sent (a hang).
        if (key.empty()) {
            fail_client<qb::unordered_set<std::string>>(std::forward<Func>(func), "SMEMBERS requires a non-empty key");
            return derived();
        }
        return derived().template command<qb::unordered_set<std::string>>(std::forward<Func>(func), "SMEMBERS", key);
    }

    /**
     * @brief Moves a member from one set to another (coroutine awaitable)
     *
     * @param source Source key where the set is stored
     * @param destination Destination key where the set is stored
     * @param member Member to move
     * @return redis_awaiter yielding Reply<bool> (true if member was moved)
     * @see https://redis.io/commands/smove
     */
    auto
    smove(const std::string &source, const std::string &destination, const std::string &member) {
        return derived().template make_coro_command<bool>(
            [this, source, destination, member](auto &&callback) { this->smove(std::move(callback), source, destination, member); });
    }

    /**
     * @brief Asynchronous version of smove
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param source Source key where the set is stored
     * @param destination Destination key where the set is stored
     * @param member Member to move
     * @return Reference to the derived class
     * @see https://redis.io/commands/smove
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<bool> &&>, Derived &>
    smove(Func &&func, const std::string &source, const std::string &destination, const std::string &member) {
        // Never a silent no-op: a bare `return` leaves the callback unfired, and on the coroutine
        // form the awaiter parks forever waiting for a reply that was never sent (a hang).
        if (source.empty() || destination.empty() || member.empty()) {
            fail_client<bool>(std::forward<Func>(func), "SMOVE requires a non-empty source, destination and member");
            return derived();
        }
        return derived().template command<bool>(std::forward<Func>(func), "SMOVE", source, destination, member);
    }

    /**
     * @brief Removes and returns a random member from a set (coroutine awaitable)
     *
     * @param key Key where the set is stored
     * @return redis_awaiter yielding Reply<std::optional<std::string>>
     * @see https://redis.io/commands/spop
     */
    auto
    spop(const std::string &key) {
        return derived().template make_coro_command<std::optional<std::string>>(
            [this, key](auto &&callback) { this->spop(std::move(callback), key); });
    }

    /**
     * @brief Asynchronous version of spop
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the set is stored
     * @return Reference to the derived class
     * @see https://redis.io/commands/spop
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::string>> &&>, Derived &>
    spop(Func &&func, const std::string &key) {
        // Never a silent no-op: a bare `return` leaves the callback unfired, and on the coroutine
        // form the awaiter parks forever waiting for a reply that was never sent (a hang).
        if (key.empty()) {
            fail_client<std::optional<std::string>>(std::forward<Func>(func), "SPOP requires a non-empty key");
            return derived();
        }
        return derived().template command<std::optional<std::string>>(std::forward<Func>(func), "SPOP", key);
    }

    /**
     * @brief Removes and returns multiple random members from a set (coroutine awaitable)
     *
     * @param key Key where the set is stored
     * @param count Number of members to pop
     * @return redis_awaiter yielding Reply<std::vector<std::string>>
     * @see https://redis.io/commands/spop
     */
    auto
    spop(const std::string &key, long long count) {
        return derived().template make_coro_command<std::vector<std::string>>(
            [this, key, count](auto &&callback) { this->spop(std::move(callback), key, count); });
    }

    /**
     * @brief Asynchronous version of spop for multiple members
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the set is stored
     * @param count Number of members to pop
     * @return Reference to the derived class
     * @see https://redis.io/commands/spop
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>, Derived &>
    spop(Func &&func, const std::string &key, long long count) {
        // Never a silent no-op: a bare `return` leaves the callback unfired, and on the coroutine
        // form the awaiter parks forever waiting for a reply that was never sent (a hang).
        if (key.empty() || count < 1) {
            fail_client<std::vector<std::string>>(std::forward<Func>(func), "SPOP requires a non-empty key and a count >= 1");
            return derived();
        }
        return derived().template command<std::vector<std::string>>(std::forward<Func>(func), "SPOP", key, count);
    }

    /**
     * @brief Gets a random member from a set
     *
     * @param key Key where the set is stored
     * @return A random member, or std::nullopt if the set is empty
     * @note Time complexity: O(1)
     * @see https://redis.io/commands/srandmember
     */
    auto
    srandmember(const std::string &key) {
        return derived().template make_coro_command<std::optional<std::string>>(
            [this, key](auto &&callback) { this->srandmember(std::move(callback), key); });
    }

    /**
     * @brief Asynchronous version of srandmember
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the set is stored
     * @return Reference to the derived class
     * @see https://redis.io/commands/srandmember
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::optional<std::string>> &&>, Derived &>
    srandmember(Func &&func, const std::string &key) {
        // Never a silent no-op: a bare `return` leaves the callback unfired, and on the coroutine
        // form the awaiter parks forever waiting for a reply that was never sent (a hang).
        if (key.empty()) {
            fail_client<std::optional<std::string>>(std::forward<Func>(func), "SRANDMEMBER requires a non-empty key");
            return derived();
        }
        return derived().template command<std::optional<std::string>>(std::forward<Func>(func), "SRANDMEMBER", key);
    }

    /**
     * @brief Gets multiple random members from a set
     *
     * @param key Key where the set is stored
     * @param count Number of members to return
     * @return Vector of random members
     * @note Time complexity: O(N) where N is the absolute value of count
     * @see https://redis.io/commands/srandmember
     */
    auto
    srandmember(const std::string &key, long long count) {
        return derived().template make_coro_command<std::vector<std::string>>(
            [this, key, count](auto &&callback) { this->srandmember(std::move(callback), key, count); });
    }

    /**
     * @brief Asynchronous version of srandmember for multiple members
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the set is stored
     * @param count Number of members to return
     * @return Reference to the derived class
     * @see https://redis.io/commands/srandmember
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>, Derived &>
    srandmember(Func &&func, const std::string &key, long long count) {
        // Never a silent no-op: a bare `return` leaves the callback unfired, and on the coroutine
        // form the awaiter parks forever waiting for a reply that was never sent (a hang).
        if (key.empty()) {
            fail_client<std::vector<std::string>>(std::forward<Func>(func), "SRANDMEMBER requires a non-empty key");
            return derived();
        }
        return derived().template command<std::vector<std::string>>(std::forward<Func>(func), "SRANDMEMBER", key, count);
    }

    /**
     * @brief Removes members from a set (coroutine awaitable)
     *
     * @tparam Members Variadic types for members to remove
     * @param key Key where the set is stored
     * @param members Members to remove
     * @return redis_awaiter yielding Reply<long long> (number of members removed)
     * @see https://redis.io/commands/srem
     */
    template <typename... Members>
    auto
    srem(const std::string &key, Members &&...members) {
        return derived().template make_coro_command<long long>(
            [this, key, ... members = std::forward<Members>(members)](auto &&callback) mutable {
                this->srem(std::move(callback), key, std::forward<decltype(members)>(members)...);
            });
    }

    /**
     * @brief Asynchronous version of srem
     *
     * @tparam Func Callback function type
     * @tparam Members Variadic types for members to remove
     * @param func Callback function
     * @param key Key where the set is stored
     * @param members Members to remove
     * @return Reference to the derived class
     * @see https://redis.io/commands/srem
     */
    template <typename Func, typename... Members>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    srem(Func &&func, const std::string &key, Members &&...members) {
        if (key.empty() || sizeof...(members) == 0) {
            fail_client<long long>(std::forward<Func>(func), "SREM requires at least one member");
            return derived();
        }
        return derived().template command<long long>(std::forward<Func>(func), "SREM", key, std::forward<Members>(members)...);
    }

    // =============== Set Scanning Operations ===============

    /**
     * @brief Incrementally iterates over set members (coroutine awaitable)
     *
     * @param key Key where the set is stored
     * @param cursor Cursor position to start iteration from (0 to start)
     * @param pattern Glob pattern to filter members (default "*")
     * @param count Hint for how many elements to return per call (default 10)
     * @return redis_awaiter yielding Reply<scan<>>
     * @see https://redis.io/commands/sscan
     */
    auto
    sscan(const std::string &key, long long cursor, const std::string &pattern = "*", long long count = 10) {
        return derived().template make_coro_command<scan<>>(
            [this, key, cursor, pattern, count](auto &&callback) { this->sscan(std::move(callback), key, cursor, pattern, count); });
    }

    /**
     * @brief Asynchronous version of sscan
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the set is stored
     * @param cursor Cursor position to start iteration from
     * @param pattern Pattern to filter members
     * @param count Hint for how many elements to return per call
     * @return Reference to the derived class
     * @see https://redis.io/commands/sscan
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<scan<>> &&>, Derived &>
    sscan(Func &&func, const std::string &key, long long cursor, const std::string &pattern = "*", long long count = 10) {
        // Never a silent no-op: a bare `return` leaves the callback unfired, and on the coroutine
        // form the awaiter parks forever waiting for a reply that was never sent (a hang).
        if (key.empty()) {
            fail_client<scan<>>(std::forward<Func>(func), "SSCAN requires a non-empty key");
            return derived();
        }
        return derived().template command<scan<>>(std::forward<Func>(func), "SSCAN", key, cursor, "MATCH", pattern, "COUNT", count);
    }

    /**
     * @brief Automatically iterates through all set elements matching a pattern
     *
     * This version manages cursor iteration internally, collecting all results
     * and calling the callback once with the complete result set.
     *
     * @tparam Func Callback function type
     * @param func Callback function to process complete results
     * @param key Key where the set is stored
     * @param pattern Pattern to filter members
     * @return Reference to the derived class
     * @see https://redis.io/commands/sscan
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<scan<>> &&>, Derived &>
    sscan(Func &&func, const std::string &key, const std::string &pattern = "*") {
        // Never a silent no-op: a bare `return` leaves the callback unfired, and on the coroutine
        // form the awaiter parks forever waiting for a reply that was never sent (a hang).
        if (key.empty()) {
            fail_client<scan<>>(std::forward<Func>(func), "SSCAN requires a non-empty key");
            return derived();
        }
        // decay_t, not Func: for an lvalue callback Func deduces to `Cb&`, and the member declared
        // `Func _func` in scanner<Cb&> is then a *reference* to the caller's functor. The scanner
        // outlives this call (it drives the cursor across async round-trips), so that reference
        // dangles before it is ever invoked. Decaying makes _func an owned copy.
        scanner<std::decay_t<Func>>::create_and_start(derived(), key, pattern, std::forward<Func>(func));
        return derived();
    }

    // =============== Set Operations ===============

    /**
     * @brief Returns the union of multiple sets (coroutine awaitable)
     *
     * @param keys Keys where the sets are stored
     * @return redis_awaiter yielding Reply<std::vector<std::string>>
     * @note Time complexity: O(N) where N is the total number of elements in all sets
     * @see https://redis.io/commands/sunion
     */
    auto
    sunion(const std::vector<std::string> &keys) {
        return derived().template make_coro_command<std::vector<std::string>>(
            [this, keys](auto &&callback) { this->sunion(std::move(callback), keys); });
    }

    /**
     * @brief Asynchronous version of sunion
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param keys Keys where the sets are stored
     * @return Reference to the derived class
     * @see https://redis.io/commands/sunion
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>, Derived &>
    sunion(Func &&func, const std::vector<std::string> &keys) {
        if (keys.size() == 0) {
            fail_client<std::vector<std::string>>(std::forward<Func>(func), "SUNION requires at least one key");
            return derived();
        }
        std::vector<std::string> args;
        args.reserve(keys.size());
        args.insert(args.end(), keys.begin(), keys.end());
        return derived().template command<std::vector<std::string>>(std::forward<Func>(func), "SUNION", args);
    }

    /**
     * @brief Adds multiple sets and stores the result in a key
     *
     * @param destination Destination key where the resulting set will be stored
     * @param keys Source keys where the sets are stored
     * @return Number of members in the resulting set
     * @note Time complexity: O(N) where N is the total number of elements in all sets
     * @see https://redis.io/commands/sunionstore
     */
    auto
    sunionstore(const std::string &destination, const std::vector<std::string> &keys) {
        return derived().template make_coro_command<long long>(
            [this, destination, keys](auto &&callback) { this->sunionstore(std::move(callback), destination, keys); });
    }

    /**
     * @brief Asynchronous version of sunionstore
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param destination Destination key where the resulting set will be stored
     * @param keys Source keys where the sets are stored
     * @return Reference to the derived class
     * @see https://redis.io/commands/sunionstore
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    sunionstore(Func &&func, const std::string &destination, const std::vector<std::string> &keys) {
        if (destination.empty() || keys.size() == 0) {
            fail_client<long long>(std::forward<Func>(func), "SUNIONSTORE requires at least one key");
            return derived();
        }
        return derived().template command<long long>(std::forward<Func>(func), "SUNIONSTORE", destination, keys);
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_SET_COMMANDS_H
