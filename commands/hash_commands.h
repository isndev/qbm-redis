/**
 * @file qbm/redis/commands/hash_commands.h
 * @brief Redis hash command mixin for the qb Redis module.
 *
 * Provides the @ref qb::redis::hash_commands CRTP mixin implementing the Redis
 * hash command family (HSET, HGET, HGETALL, HSCAN, ...). Every command is
 * exposed both as a coroutine-awaitable form and as a callback-based async form
 * that returns a reference to the derived handler for chaining.
 *
 *            See accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0
 *
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2026 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)
 * @ingroup Redis
 */
#ifndef QBM_REDIS_HASH_COMMANDS_H
#define QBM_REDIS_HASH_COMMANDS_H
#include <type_traits>
#include "../reply.h"

namespace qb::redis {

/**
 * @class hash_commands
 * @brief Provides Redis hash command implementations.
 *
 * This class implements Redis hash operations, which provide a mapping of string
 * fields to string values. Commands return awaiters for coroutine-first async I/O.
 *
 * Redis hashes are particularly useful for representing objects with multiple fields
 * and for efficient field-based operations.
 *
 * @tparam Derived The derived class type (CRTP pattern)
 */
template <typename Derived>
class hash_commands {
private:
    constexpr Derived &
    derived() {
        return static_cast<Derived &>(*this);
    }

    /**
     * @class scanner
     * @brief Helper class for implementing incremental scanning of hashes
     *
     * Uses shared_ptr for automatic memory management. Safe even if exceptions occur.
     *
     * @tparam Func Decayed callback type; never a reference. The scanner owns its callback:
     *              it keeps itself alive across the cursor round-trips and therefore outlives
     *              the hscan() call that built it, so anything it merely referred to would be
     *              long gone by the time the callback fires. See the hscan() call site.
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
         * @brief Constructs a scanner for hash fields and values
         *
         * @param handler The Redis handler
         * @param key Key where the hash is stored
         * @param pattern Pattern to filter hash fields
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
                _started = true;
                // Capture shared_ptr to keep alive during async operations
                auto self = this->shared_from_this();
                _handler.hscan([self](auto &&reply) { (*self)(std::forward<decltype(reply)>(reply)); }, _key, 0, _pattern, 100);
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
                // Continue scanning - capture shared_ptr to keep alive
                auto self = this->shared_from_this();
                _handler.hscan([self](auto &&reply) { (*self)(std::forward<decltype(reply)>(reply)); }, _key, reply.result().cursor, _pattern,
                               100);
            } else {
                try {
                    _func(std::move(_reply));
                } catch (std::exception const &e) {
                    LOG_WARN("[qbm][redis] scanner callback failed: " << e.what());
                }
                // Automatically destroyed when shared_ptr reference count reaches 0
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

    /**
     * @class multi_hvals
     * @brief Helper class for getting values from multiple hashes
     *
     * Uses shared_ptr for automatic memory management. Safe even if exceptions occur.
     *
     * @tparam Func Decayed callback type; never a reference. Same ownership rule as scanner:
     *              the helper stays alive until the last hvals reply lands, well after the
     *              hvals() call that built it returned. See the hvals() call site.
     */
    template <typename Func>
    class multi_hvals : public std::enable_shared_from_this<multi_hvals<Func>> {
        const std::vector<std::string>  _keys;
        Func                            _func;
        Reply<std::vector<std::string>> _reply;
        std::atomic<size_t>             _pending{0};

    public:
        /**
         * @brief Constructs a multi-hvals processor
         *
         * @param handler The Redis handler
         * @param keys Keys where the hashes are stored
         * @param func Callback function to process results
         */
        multi_hvals(std::vector<std::string> keys, Func func)
            : _keys(std::move(keys))
            , _func(std::move(func))
            , _reply{true}
            , _pending(_keys.empty() ? 1 : _keys.size()) {}

        /**
         * @brief Start the processing
         */
        void
        start(Derived &handler) {
            if (_keys.empty()) {
                complete();
                return;
            }

            for (auto it = _keys.begin(); it != std::end(_keys); ++it) {
                // Capture shared_ptr to keep alive during async operations
                auto self = this->shared_from_this();
                handler.hvals([self](auto &&reply) { self->handle_reply(std::forward<decltype(reply)>(reply)); }, *it);
            }
        }

        /**
         * @brief Handle a single reply
         */
        void
        handle_reply(Reply<std::vector<std::string>> &&reply) {
            _reply.ok() &= reply.ok();
            std::move(reply.result().begin(), reply.result().end(), std::back_inserter(_reply.result()));

            if (--_pending == 0) {
                complete();
            }
        }

        /**
         * @brief Complete the operation and call the callback
         */
        void
        complete() {
            try {
                _func(std::move(_reply));
            } catch (std::exception const &e) {
                LOG_WARN("[qbm][redis] multi_hvals callback failed: " << e.what());
            }
            // Automatically destroyed when shared_ptr reference count reaches 0
        }

        /**
         * @brief Factory method to create and start a multi_hvals safely
         */
        static void
        create_and_start(Derived &handler, std::vector<std::string> keys, Func func) {
            auto ptr = std::make_shared<multi_hvals>(std::move(keys), std::move(func));
            ptr->start(handler);
        }
    };

public:
    /**
     * @brief Deletes one or more hash fields (coroutine awaitable)
     *
     * @tparam Fields Variadic types for field names
     * @param key Key where the hash is stored
     * @param fields Fields to delete
     * @return redis_awaiter yielding Reply<long long>
     * @see https://redis.io/commands/hdel
     */
    template <typename... Fields>
    auto
    hdel(const std::string &key, Fields &&...fields) {
        return derived().template make_coro_command<long long>([this, key, ... fields = std::forward<Fields>(fields)](auto &&callback) mutable {
            this->hdel(std::move(callback), key, std::forward<decltype(fields)>(fields)...);
        });
    }

    /**
     * @brief Asynchronous version of hdel
     *
     * @tparam Func Callback function type
     * @tparam Fields Variadic types for field names
     * @param func Callback function
     * @param key Key where the hash is stored
     * @param fields Fields to delete
     * @return Reference to the Redis handler for chaining
     * @see https://redis.io/commands/hdel
     */
    template <typename Func, typename... Fields>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    hdel(Func &&func, const std::string &key, Fields &&...fields) {
        // A zero-field pack would emit `HDEL key`, which Redis rejects. Reject it client-side and
        // resolve the callback/awaiter via fail_client (never a silent no-op / coroutine hang).
        if (key.empty() || sizeof...(fields) == 0) {
            fail_client<long long>(std::forward<Func>(func), "HDEL requires at least one field");
            return derived();
        }
        return derived().template command<long long>(std::forward<Func>(func), "HDEL", key, std::forward<Fields>(fields)...);
    }

    /**
     * @brief Determines if a hash field exists (coroutine awaitable)
     *
     * @param key Key where the hash is stored
     * @param field Field to check
     * @return redis_awaiter yielding Reply<bool>
     * @see https://redis.io/commands/hexists
     */
    auto
    hexists(const std::string &key, const std::string &field) {
        return derived().template make_coro_command<bool>(
            [this, key, field](auto &&callback) { this->hexists(std::move(callback), key, field); });
    }

    /**
     * @brief Asynchronous version of hexists
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the hash is stored
     * @param field Field to check
     * @return Reference to the Redis handler for chaining
     * @see https://redis.io/commands/hexists
     */
    template <typename Func>
    Derived &
    hexists(Func &&func, const std::string &key, const std::string &field) {
        return derived().template command<bool>(std::forward<Func>(func), "HEXISTS", key, field);
    }

    /**
     * @brief Gets the value of a hash field (coroutine awaitable)
     *
     * @param key Key where the hash is stored
     * @param field Field to get
     * @return redis_awaiter yielding Reply<std::optional<std::string>>
     * @see https://redis.io/commands/hget
     */
    auto
    hget(const std::string &key, const std::string &field) {
        return derived().template make_coro_command<std::optional<std::string>>(
            [this, key, field](auto &&callback) { this->hget(std::move(callback), key, field); });
    }

    /**
     * @brief Asynchronous version of hget
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the hash is stored
     * @param field Field to get
     * @return Reference to the Redis handler for chaining
     * @see https://redis.io/commands/hget
     */
    template <typename Func>
    Derived &
    hget(Func &&func, const std::string &key, const std::string &field) {
        return derived().template command<std::optional<std::string>>(std::forward<Func>(func), "HGET", key, field);
    }

    /**
     * @brief Gets all fields and values of a hash (coroutine awaitable)
     *
     * @param key Key where the hash is stored
     * @return redis_awaiter yielding Reply<qb::unordered_map<std::string, std::string>>
     * @see https://redis.io/commands/hgetall
     */
    auto
    hgetall(const std::string &key) {
        return derived().template make_coro_command<qb::unordered_map<std::string, std::string>>(
            [this, key](auto &&callback) { this->hgetall(std::move(callback), key); });
    }

    /**
     * @brief Asynchronous version of hgetall
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the hash is stored
     * @return Reference to the Redis handler for chaining
     * @see https://redis.io/commands/hgetall
     */
    template <typename Func>
    Derived &
    hgetall(Func &&func, const std::string &key) {
        return derived().template command<qb::unordered_map<std::string, std::string>>(std::forward<Func>(func), "HGETALL", key);
    }

    /**
     * @brief Increments the integer value of a hash field (coroutine awaitable)
     *
     * @param key Key where the hash is stored
     * @param field Field to increment
     * @param increment Increment amount
     * @return redis_awaiter yielding Reply<long long>
     * @see https://redis.io/commands/hincrby
     */
    auto
    hincrby(const std::string &key, const std::string &field, long long increment) {
        return derived().template make_coro_command<long long>(
            [this, key, field, increment](auto &&callback) { this->hincrby(std::move(callback), key, field, increment); });
    }

    /**
     * @brief Asynchronous version of hincrby
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the hash is stored
     * @param field Field to increment
     * @param increment Increment amount
     * @return Reference to the Redis handler for chaining
     * @see https://redis.io/commands/hincrby
     */
    template <typename Func>
    Derived &
    hincrby(Func &&func, const std::string &key, const std::string &field, long long increment) {
        return derived().template command<long long>(std::forward<Func>(func), "HINCRBY", key, field, increment);
    }

    /**
     * @brief Increments the float value of a hash field (coroutine awaitable)
     *
     * @param key Key where the hash is stored
     * @param field Field to increment
     * @param increment Increment amount
     * @return redis_awaiter yielding Reply<double>
     * @see https://redis.io/commands/hincrbyfloat
     */
    auto
    hincrbyfloat(const std::string &key, const std::string &field, double increment) {
        return derived().template make_coro_command<double>(
            [this, key, field, increment](auto &&callback) { this->hincrbyfloat(std::move(callback), key, field, increment); });
    }

    /**
     * @brief Asynchronous version of hincrbyfloat
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the hash is stored
     * @param field Field to increment
     * @param increment Increment amount
     * @return Reference to the Redis handler for chaining
     * @see https://redis.io/commands/hincrbyfloat
     */
    template <typename Func>
    Derived &
    hincrbyfloat(Func &&func, const std::string &key, const std::string &field, double increment) {
        return derived().template command<double>(std::forward<Func>(func), "HINCRBYFLOAT", key, field, increment);
    }

    /**
     * @brief Gets all field names in a hash (coroutine awaitable)
     *
     * @param key Key where the hash is stored
     * @return redis_awaiter yielding Reply<std::vector<std::string>>
     * @see https://redis.io/commands/hkeys
     */
    auto
    hkeys(const std::string &key) {
        return derived().template make_coro_command<std::vector<std::string>>(
            [this, key](auto &&callback) { this->hkeys(std::move(callback), key); });
    }

    /**
     * @brief Asynchronous version of hkeys
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the hash is stored
     * @return Reference to the Redis handler for chaining
     * @see https://redis.io/commands/hkeys
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>, Derived &>
    hkeys(Func &&func, const std::string &key) {
        return derived().template command<std::vector<std::string>>(std::forward<Func>(func), "HKEYS", key);
    }

    /**
     * @brief Gets the number of fields in a hash (coroutine awaitable)
     *
     * @param key Key where the hash is stored
     * @return redis_awaiter yielding Reply<long long>
     * @see https://redis.io/commands/hlen
     */
    auto
    hlen(const std::string &key) {
        return derived().template make_coro_command<long long>([this, key](auto &&callback) { this->hlen(std::move(callback), key); });
    }

    /**
     * @brief Asynchronous version of hlen
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the hash is stored
     * @return Reference to the Redis handler for chaining
     * @see https://redis.io/commands/hlen
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    hlen(Func &&func, const std::string &key) {
        return derived().template command<long long>(std::forward<Func>(func), "HLEN", key);
    }

    /**
     * @brief Gets the values of all specified hash fields (coroutine awaitable)
     *
     * @tparam Fields Variadic types for field names
     * @param key Key where the hash is stored
     * @param fields Fields to get
     * @return redis_awaiter yielding Reply<std::vector<std::optional<std::string>>>
     * @see https://redis.io/commands/hmget
     */
    template <typename... Fields>
    auto
    hmget(const std::string &key, Fields &&...fields) {
        return derived().template make_coro_command<std::vector<std::optional<std::string>>>(
            [this, key, ... fields = std::forward<Fields>(fields)](auto &&callback) mutable {
                this->hmget(std::move(callback), key, std::forward<decltype(fields)>(fields)...);
            });
    }

    /**
     * @brief Asynchronous version of hmget
     *
     * @tparam Func Callback function type
     * @tparam Fields Variadic types for field names
     * @param func Callback function
     * @param key Key where the hash is stored
     * @param fields Fields to get
     * @return Reference to the Redis handler for chaining
     * @see https://redis.io/commands/hmget
     */
    template <typename Func, typename... Fields>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::optional<std::string>>> &&>, Derived &>
    hmget(Func &&func, const std::string &key, Fields &&...fields) {
        // Empty field pack → `HMGET key`, which Redis rejects. Reject client-side (resolve callback).
        if (key.empty() || sizeof...(fields) == 0) {
            fail_client<std::vector<std::optional<std::string>>>(std::forward<Func>(func), "HMGET requires at least one field");
            return derived();
        }
        return derived().template command<std::vector<std::optional<std::string>>>(std::forward<Func>(func), "HMGET", key,
                                                                                   std::forward<Fields>(fields)...);
    }

    /**
     * @brief Sets multiple hash fields to multiple values (coroutine awaitable)
     *
     * @tparam FieldValues Variadic types for field-value pairs
     * @param key Key where the hash is stored
     * @param field_values Field-value pairs to set
     * @return redis_awaiter yielding Reply<status>
     * @see https://redis.io/commands/hset
     */
    template <typename... FieldValues>
    auto
    hmset(const std::string &key, FieldValues &&...field_values) {
        return derived().template make_coro_command<status>(
            [this, key, ... field_values = std::forward<FieldValues>(field_values)](auto &&callback) mutable {
                this->hmset(std::move(callback), key, std::forward<decltype(field_values)>(field_values)...);
            });
    }

    /**
     * @brief Asynchronous version of hmset
     *
     * @tparam Func Callback function type
     * @tparam FieldValues Variadic types for field-value pairs
     * @param func Callback function
     * @param key Key where the hash is stored
     * @param field_values Field-value pairs to set
     * @return Reference to the Redis handler for chaining
     * @see https://redis.io/commands/hset
     */
    template <typename Func, typename... FieldValues>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    hmset(Func &&func, const std::string &key, FieldValues &&...field_values) {
        // Empty pack → `HMSET key`, which Redis rejects. Reject client-side (resolve callback). (An
        // odd count is left to the server to reject with a clear error rather than dropped here.)
        if (key.empty() || sizeof...(field_values) == 0) {
            fail_client<status>(std::forward<Func>(func), "HMSET requires at least one field-value pair");
            return derived();
        }
        return derived().template command<status>(std::forward<Func>(func), "HMSET", key, std::forward<FieldValues>(field_values)...);
    }

    /**
     * @brief Incrementally iterates hash fields and values (coroutine awaitable)
     *
     * @tparam Out Type of the output container
     * @param key Key where the hash is stored
     * @param cursor Cursor position to start iteration from
     * @param pattern Pattern to filter fields
     * @param count Hint for how many field-value pairs to return per call
     * @return redis_awaiter yielding Reply<qb::redis::scan<Out>>
     * @see https://redis.io/commands/hscan
     */
    template <typename Out = qb::unordered_map<std::string, std::string>>
    auto
    hscan(const std::string &key, long long cursor, const std::string &pattern = "*", long long count = 10) {
        return derived().template make_coro_command<qb::redis::scan<Out>>(
            [this, key, cursor, pattern, count](auto &&callback) { this->hscan(std::move(callback), key, cursor, pattern, count); });
    }

    /**
     * @brief Asynchronous version of hscan
     *
     * @tparam Func Callback function type
     * @tparam Out Type of the output container
     * @param func Callback function
     * @param key Key where the hash is stored
     * @param cursor Cursor position to start iteration from
     * @param pattern Pattern to filter fields
     * @param count Hint for how many field-value pairs to return per call
     * @return Reference to the Redis handler for chaining
     * @see https://redis.io/commands/hscan
     */
    template <typename Func, typename Out = qb::unordered_map<std::string, std::string>>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::redis::scan<Out>> &&>, Derived &>
    hscan(Func &&func, const std::string &key, long long cursor, const std::string &pattern = "*", long long count = 10) {
        // Never a silent no-op: a bare `return` leaves the callback unfired, and on the coroutine
        // form the awaiter parks forever waiting for a reply that was never sent (a hang).
        // Resolve it the way every other argument guard in this module does.
        if (key.empty()) {
            fail_client<qb::redis::scan<Out>>(std::forward<Func>(func), "HSCAN requires a non-empty key");
            return derived();
        }
        return derived().template command<qb::redis::scan<Out>>(std::forward<Func>(func), "HSCAN", key, cursor, "MATCH", pattern, "COUNT",
                                                                count);
    }

    /**
     * @brief Automatically iterates through all hash fields and values matching a
     * pattern
     *
     * This version manages cursor iteration internally, collecting all results
     * and calling the callback once with the complete result set.
     *
     * @tparam Func Callback function type
     * @param func Callback function to process complete results
     * @param key Key where the hash is stored
     * @param pattern Pattern to filter fields
     * @return Reference to the Redis handler for chaining
     * @see https://redis.io/commands/hscan
     */
    template <typename Func, typename Out = qb::unordered_map<std::string, std::string>>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::redis::scan<Out>> &&>, Derived &>
    hscan(Func &&func, const std::string &key, const std::string &pattern = "*") {
        // decay_t, not Func: for an lvalue callback Func deduces to `Cb&`, and the member declared
        // `Func _func` in scanner<Cb&> is then a *reference* to the caller's functor. The scanner
        // outlives this call (it drives the cursor across async round-trips), so that reference
        // dangles before it is ever invoked. Decaying makes _func an owned copy.
        scanner<std::decay_t<Func>>::create_and_start(derived(), key, pattern, std::forward<Func>(func));
        return derived();
    }

    /**
     * @brief Sets the string value of a hash field (coroutine awaitable)
     *
     * @param key Key where the hash is stored
     * @param field Field to set
     * @param val Value to set
     * @return redis_awaiter yielding Reply<long long>
     * @see https://redis.io/commands/hset
     */
    auto
    hset(const std::string &key, const std::string &field, const std::string &val) {
        return derived().template make_coro_command<long long>(
            [this, key, field, val](auto &&callback) { this->hset(std::move(callback), key, field, val); });
    }

    /**
     * @brief Asynchronous version of hset
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the hash is stored
     * @param field Field to set
     * @param val Value to set
     * @return Reference to the Redis handler for chaining
     * @see https://redis.io/commands/hset
     */
    template <typename Func>
    Derived &
    hset(Func &&func, const std::string &key, const std::string &field, const std::string &val) {
        return derived().template command<long long>(std::forward<Func>(func), "HSET", key, field, val);
    }

    /**
     * @brief Sets the string value of a hash field using a key-value pair (coroutine awaitable)
     *
     * @param key Key where the hash is stored
     * @param item Pair containing field name and value
     * @return redis_awaiter yielding Reply<long long>
     * @see https://redis.io/commands/hset
     */
    auto
    hset(const std::string &key, const std::pair<std::string, std::string> &item) {
        return hset(key, item.first, item.second);
    }

    /**
     * @brief Asynchronous version of hset with key-value pair
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the hash is stored
     * @param item Pair containing field name and value
     * @return Reference to the Redis handler for chaining
     * @see https://redis.io/commands/hset
     */
    template <typename Func>
    Derived &
    hset(Func &&func, const std::string &key, const std::pair<std::string, std::string> &item) {
        return hset(std::forward<Func>(func), key, item.first, item.second);
    }

    /**
     * @brief Sets the value of a hash field, only if the field does not exist (coroutine awaitable)
     *
     * @param key Key where the hash is stored
     * @param field Field to set
     * @param val Value to set
     * @return redis_awaiter yielding Reply<bool>
     * @see https://redis.io/commands/hsetnx
     */
    auto
    hsetnx(const std::string &key, const std::string &field, const std::string &val) {
        return derived().template make_coro_command<bool>(
            [this, key, field, val](auto &&callback) { this->hsetnx(std::move(callback), key, field, val); });
    }

    /**
     * @brief Asynchronous version of hsetnx
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the hash is stored
     * @param field Field to set
     * @param val Value to set
     * @return Reference to the Redis handler for chaining
     * @see https://redis.io/commands/hsetnx
     */
    template <typename Func>
    Derived &
    hsetnx(Func &&func, const std::string &key, const std::string &field, const std::string &val) {
        return derived().template command<bool>(std::forward<Func>(func), "HSETNX", key, field, val);
    }

    /**
     * @brief Sets the value of a hash field using a key-value pair, only if the field does not exist (coroutine awaitable)
     *
     * @param key Key where the hash is stored
     * @param item Pair containing field name and value
     * @return redis_awaiter yielding Reply<bool>
     * @see https://redis.io/commands/hsetnx
     */
    auto
    hsetnx(const std::string &key, const std::pair<std::string, std::string> &item) {
        return hsetnx(key, item.first, item.second);
    }

    /**
     * @brief Asynchronous version of hsetnx with key-value pair
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the hash is stored
     * @param item Pair containing field name and value
     * @return Reference to the Redis handler for chaining
     * @see https://redis.io/commands/hsetnx
     */
    template <typename Func>
    Derived &
    hsetnx(Func &&func, const std::string &key, const std::pair<std::string, std::string> &item) {
        return hsetnx(std::forward<Func>(func), key, item.first, item.second);
    }

    /**
     * @brief Gets the length of the value of a hash field (coroutine awaitable)
     *
     * @param key Key where the hash is stored
     * @param field Field to get length of
     * @return redis_awaiter yielding Reply<long long>
     * @see https://redis.io/commands/hstrlen
     */
    auto
    hstrlen(const std::string &key, const std::string &field) {
        return derived().template make_coro_command<long long>(
            [this, key, field](auto &&callback) { this->hstrlen(std::move(callback), key, field); });
    }

    /**
     * @brief Asynchronous version of hstrlen
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the hash is stored
     * @param field Field to get length of
     * @return Reference to the Redis handler for chaining
     * @see https://redis.io/commands/hstrlen
     */
    template <typename Func>
    Derived &
    hstrlen(Func &&func, const std::string &key, const std::string &field) {
        return derived().template command<long long>(std::forward<Func>(func), "HSTRLEN", key, field);
    }

    /**
     * @brief Gets all values in a hash (coroutine awaitable)
     *
     * @param key Key where the hash is stored
     * @return redis_awaiter yielding Reply<std::vector<std::string>>
     * @see https://redis.io/commands/hvals
     */
    auto
    hvals(const std::string &key) {
        return derived().template make_coro_command<std::vector<std::string>>(
            [this, key](auto &&callback) { this->hvals(std::move(callback), key); });
    }

    /**
     * @brief Asynchronous version of hvals
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the hash is stored
     * @return Reference to the Redis handler for chaining
     * @see https://redis.io/commands/hvals
     */
    template <typename Func>
    Derived &
    hvals(Func &&func, const std::string &key) {
        return derived().template command<std::vector<std::string>>(std::forward<Func>(func), "HVALS", key);
    }

    /**
     * @brief Gets all values from multiple hashes
     *
     * @tparam Func Callback function type
     * @param func Callback function to process all values from all hashes
     * @param keys Keys where the hashes are stored
     * @return Reference to the Redis handler for chaining
     * @see https://redis.io/commands/hvals
     */
    template <typename Func>
    Derived &
    hvals(Func &&func, std::vector<std::string> keys) {
        // decay_t, not Func: see hscan above. An lvalue callback would make `Func _func` a
        // reference into the caller's frame, and this helper outlives the call by design.
        multi_hvals<std::decay_t<Func>>::create_and_start(derived(), std::move(keys), std::forward<Func>(func));
        return derived();
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_HASH_COMMANDS_H
