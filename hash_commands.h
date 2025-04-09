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

#ifndef QBM_REDIS_HASH_COMMANDS_H
#define QBM_REDIS_HASH_COMMANDS_H
#include "reply.h"

namespace qb::redis {

/**
 * @class hash_commands
 * @brief Provides Redis hash command implementations.
 *
 * This class implements Redis hash operations, which provide a mapping of string
 * fields to string values. Each command has both synchronous and asynchronous versions.
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
     * @tparam Func Callback function type
     */
    template <typename Func>
    class scanner {
        Derived                            &_handler;
        std::string                         _key;
        std::string                         _pattern;
        Func                                _func;
        qb::redis::Reply<qb::redis::scan<>> _reply;

    public:
        /**
         * @brief Constructs a scanner for hash fields and values
         *
         * @param handler The Redis handler
         * @param key Key where the hash is stored
         * @param pattern Pattern to filter hash fields
         * @param func Callback function to process results
         */
        scanner(Derived &handler, std::string key, std::string pattern, Func &&func)
            : _handler(handler)
            , _key(std::move(key))
            , _pattern(std::move(pattern))
            , _func(std::forward<Func>(func)) {
            _handler.hscan(std::ref(*this), _key, 0, _pattern, 100);
        }

        /**
         * @brief Processes scan results and continues scanning if needed
         *
         * @param reply The scan operation reply
         */
        void
        operator()(qb::redis::Reply<qb::redis::scan<>> &&reply) {
            _reply.ok() = reply.ok();
            std::move(reply.result().items.begin(), reply.result().items.end(),
                      std::back_inserter(_reply.result().items));
            if (reply.ok() && reply.result().cursor)
                _handler.hscan(std::ref(*this), _key, reply.result().cursor, _pattern,
                               100);
            else {
                _func(std::move(_reply));
                delete this;
            }
        }
    };

    /**
     * @class multi_hvals
     * @brief Helper class for getting values from multiple hashes
     *
     * @tparam Func Callback function type
     */
    template <typename Func>
    class multi_hvals {
        const std::vector<std::string>  _keys;
        Func                            _func;
        Reply<std::vector<std::string>> _reply;

    public:
        /**
         * @brief Constructs a multi-hvals processor
         *
         * @param handler The Redis handler
         * @param keys Keys where the hashes are stored
         * @param func Callback function to process results
         */
        multi_hvals(Derived &handler, std::vector<std::string> keys, Func &&func)
            : _keys(std::move(keys))
            , _func(std::forward<Func>(func))
            , _reply{true} {
            if (!_keys.empty()) {
                for (auto it = _keys.begin(); it != std::end(_keys); ++it) {
                    handler.hvals(
                        [this, is_last = ((it + 1) == _keys.end())](auto &&reply) {
                            _reply.ok() &= reply.ok();
                            std::move(reply.result().begin(), reply.result().end(),
                                      std::back_inserter(_reply.result()));
                            if (is_last) {
                                this->_func(std::move(_reply));
                                delete this;
                            }
                        },
                        *it);
                }
            } else {
                this->_func(std::move(_reply));
                delete this;
            }
        }
    };

public:
    /**
     * @brief Deletes one or more hash fields
     *
     * @tparam Fields Variadic types for field names
     * @param key Key where the hash is stored
     * @param fields Fields to delete
     * @return Number of fields that were removed
     */
    template <typename... Fields>
    long long
    hdel(const std::string &key, Fields &&...fields) {
        return derived()
            .template command<long long>("HDEL", key, std::forward<Fields>(fields)...)
            .result();
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
     */
    template <typename Func, typename... Fields>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    hdel(Func &&func, const std::string &key, Fields &&...fields) {
        return derived().template command<long long>(
            std::forward<Func>(func), "HDEL", key, std::forward<Fields>(fields)...);
    }

    /**
     * @brief Determines if a hash field exists
     *
     * @param key Key where the hash is stored
     * @param field Field to check
     * @return true if the field exists, false otherwise
     */
    bool
    hexists(const std::string &key, const std::string &field) {
        return derived().template command<bool>("HEXISTS", key, field).result();
    }

    /**
     * @brief Asynchronous version of hexists
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the hash is stored
     * @param field Field to check
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    Derived &
    hexists(Func &&func, const std::string &key, const std::string &field) {
        return derived().template command<bool>(std::forward<Func>(func), "HEXISTS", key,
                                                field);
    }

    /**
     * @brief Gets the value of a hash field
     *
     * @param key Key where the hash is stored
     * @param field Field to get
     * @return Value of the field, or std::nullopt if the field does not exist
     */
    std::optional<std::string>
    hget(const std::string &key, const std::string &field) {
        return derived()
            .template command<std::optional<std::string>>("HGET", key, field)
            .result();
    }

    /**
     * @brief Asynchronous version of hget
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the hash is stored
     * @param field Field to get
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    Derived &
    hget(Func &&func, const std::string &key, const std::string &field) {
        return derived().template command<std::optional<std::string>>(
            std::forward<Func>(func), "HGET", key, field);
    }

    /**
     * @brief Gets all fields and values of a hash
     *
     * @param key Key where the hash is stored
     * @return Map of field-value pairs
     */
    qb::unordered_map<std::string, std::string>
    hgetall(const std::string &key) {
        return derived()
            .template command<qb::unordered_map<std::string, std::string>>("HGETALL",
                                                                           key)
            .result();
    }

    /**
     * @brief Asynchronous version of hgetall
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the hash is stored
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    Derived &
    hgetall(Func &&func, const std::string &key) {
        return derived().template command<qb::unordered_map<std::string, std::string>>(
            std::forward<Func>(func), "HGETALL", key);
    }

    /**
     * @brief Increments the integer value of a hash field by the given amount
     *
     * @param key Key where the hash is stored
     * @param field Field to increment
     * @param increment Increment amount
     * @return Value of the field after the increment
     */
    long long
    hincrby(const std::string &key, const std::string &field, long long increment) {
        return derived()
            .template command<long long>("HINCRBY", key, field, increment)
            .result();
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
     */
    template <typename Func>
    Derived &
    hincrby(Func &&func, const std::string &key, const std::string &field,
            long long increment) {
        return derived().template command<long long>(std::forward<Func>(func), "HINCRBY",
                                                     key, field, increment);
    }

    /**
     * @brief Increments the float value of a hash field by the given amount
     *
     * @param key Key where the hash is stored
     * @param field Field to increment
     * @param increment Increment amount
     * @return Value of the field after the increment
     */
    double
    hincrbyfloat(const std::string &key, const std::string &field, double increment) {
        return derived()
            .template command<double>("HINCRBYFLOAT", key, field, increment)
            .result();
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
     */
    template <typename Func>
    Derived &
    hincrbyfloat(Func &&func, const std::string &key, const std::string &field,
                 double increment) {
        return derived().template command<double>(std::forward<Func>(func),
                                                  "HINCRBYFLOAT", key, field, increment);
    }

    /**
     * @brief Gets all fields in a hash matching a pattern
     *
     * @param pattern Pattern to match fields against
     * @return Vector of matching field names
     */
    std::vector<std::string>
    hkeys(const std::string &pattern = "*") {
        return derived()
            .template command<std::vector<std::string>>("HKEYS", pattern)
            .result();
    }

    /**
     * @brief Asynchronous version of hkeys
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param pattern Pattern to match fields against
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<std::vector<std::string>> &&>,
                     Derived &>
    hkeys(Func &&func, const std::string &pattern = "*") {
        return derived().template command<std::vector<std::string>>(
            std::forward<Func>(func), "HKEYS", pattern);
    }

    /**
     * @brief Gets the number of fields in a hash
     *
     * @param key Key where the hash is stored
     * @return Number of fields in the hash
     */
    long long
    hlen(const std::string &key) {
        return derived().template command<long long>("HLEN", key).result();
    }

    /**
     * @brief Asynchronous version of hlen
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the hash is stored
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, Reply<long long> &&>, Derived &>
    hlen(Func &&func, const std::string &key) {
        return derived().template command<long long>(std::forward<Func>(func), "HLEN",
                                                     key);
    }

    /**
     * @brief Gets the values of all specified hash fields
     *
     * @tparam Fields Variadic types for field names
     * @param key Key where the hash is stored
     * @param fields Fields to get
     * @return Vector of field values (as optional strings)
     */
    template <typename... Fields>
    std::vector<std::optional<std::string>>
    hmget(const std::string &key, Fields &&...fields) {
        return derived()
            .template command<std::vector<std::optional<std::string>>>(
                "HMGET", key, std::forward<Fields>(fields)...)
            .result();
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
     */
    template <typename Func, typename... Fields>
    std::enable_if_t<
        std::is_invocable_v<Func, Reply<std::vector<std::optional<std::string>>> &&>,
        Derived &>
    hmget(Func &&func, const std::string &key, Fields &&...fields) {
        return derived().template command<std::vector<std::optional<std::string>>>(
            std::forward<Func>(func), "HMGET", key, std::forward<Fields>(fields)...);
    }

    /**
     * @brief Sets multiple hash fields to multiple values
     *
     * @tparam FieldValues Variadic types for field-value pairs
     * @param key Key where the hash is stored
     * @param field_values Field-value pairs to set
     * @return status object indicating success or failure
     */
    template <typename... FieldValues>
    status
    hmset(const std::string &key, FieldValues &&...field_values) {
        return derived()
            .template command<status>("HMSET", key,
                                      std::forward<FieldValues>(field_values)...)
            .result();
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
     */
    template <typename Func, typename... FieldValues>
    std::enable_if_t<std::is_invocable_v<Func, Reply<status> &&>, Derived &>
    hmset(Func &&func, const std::string &key, FieldValues &&...field_values) {
        return derived().template command<status>(
            std::forward<Func>(func), "HMSET", key,
            std::forward<FieldValues>(field_values)...);
    }

    /**
     * @brief Incrementally iterates hash fields and values
     *
     * @tparam Out Type of the output container
     * @param key Key where the hash is stored
     * @param cursor Cursor position to start iteration from
     * @param pattern Pattern to filter fields
     * @param count Hint for how many field-value pairs to return per call
     * @return Scan result containing next cursor and matching field-value pairs
     */
    template <typename Out = qb::unordered_map<std::string, std::string>>
    qb::redis::scan<Out>
    hscan(const std::string &key, long long cursor, const std::string &pattern = "*",
          long long count = 10) {
        if (key.empty()) {
            return {};
        }
        return derived()
            .template command<qb::redis::scan<Out>>("HSCAN", key, cursor, "MATCH",
                                                    pattern, "COUNT", count)
            .result();
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
     */
    template <typename Func, typename Out = qb::unordered_map<std::string, std::string>>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::redis::scan<Out>> &&>,
                     Derived &>
    hscan(Func &&func, const std::string &key, long long cursor,
          const std::string &pattern = "*", long long count = 10) {
        if (key.empty()) {
            return derived();
        }
        return derived().template command<qb::redis::scan<Out>>(
            std::forward<Func>(func), "HSCAN", key, cursor, "MATCH", pattern, "COUNT",
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
     */
    template <typename Func, typename Out = qb::unordered_map<std::string, std::string>>
    std::enable_if_t<std::is_invocable_v<Func, Reply<qb::redis::scan<Out>> &&>,
                     Derived &>
    hscan(Func &&func, const std::string &key, const std::string &pattern = "*") {
        new scanner<Func>(derived(), key, pattern, std::forward<Func>(func));
        return derived();
    }

    /**
     * @brief Sets the string value of a hash field
     *
     * @param key Key where the hash is stored
     * @param field Field to set
     * @param val Value to set
     * @return 1 if field is a new field and value was set, 0 if field already exists and
     * value was updated
     */
    long long
    hset(const std::string &key, const std::string &field, const std::string &val) {
        return derived().template command<long long>("HSET", key, field, val).result();
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
     */
    template <typename Func>
    Derived &
    hset(Func &&func, const std::string &key, const std::string &field,
         const std::string &val) {
        return derived().template command<long long>(std::forward<Func>(func), "HSET",
                                                     key, field, val);
    }

    /**
     * @brief Sets the string value of a hash field using a key-value pair
     *
     * @param key Key where the hash is stored
     * @param item Pair containing field name and value
     * @return true if the operation was successful
     */
    bool
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
     */
    template <typename Func>
    Derived &
    hset(Func &&func, const std::string &key,
         const std::pair<std::string, std::string> &item) {
        return hset(std::forward<Func>(func), key, item.first, item.second);
    }

    /**
     * @brief Sets the value of a hash field, only if the field does not exist
     *
     * @param key Key where the hash is stored
     * @param field Field to set
     * @param val Value to set
     * @return true if field was set, false if field already exists
     */
    bool
    hsetnx(const std::string &key, const std::string &field, const std::string &val) {
        return derived().template command<bool>("HSETNX", key, field, val).result();
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
     */
    template <typename Func>
    Derived &
    hsetnx(Func &&func, const std::string &key, const std::string &field,
           const std::string &val) {
        return derived().template command<bool>(std::forward<Func>(func), "HSETNX", key,
                                                field, val);
    }

    /**
     * @brief Sets the value of a hash field using a key-value pair, only if the field
     * does not exist
     *
     * @param key Key where the hash is stored
     * @param item Pair containing field name and value
     * @return true if field was set, false if field already exists
     */
    bool
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
     */
    template <typename Func>
    Derived &
    hsetnx(Func &&func, const std::string &key,
           const std::pair<std::string, std::string> &item) {
        return hsetnx(std::forward<Func>(func), key, item.first, item.second);
    }

    /**
     * @brief Gets the length of the value of a hash field
     *
     * @param key Key where the hash is stored
     * @param field Field to get length of
     * @return Length of the field value
     */
    long long
    hstrlen(const std::string &key, const std::string &field) {
        return derived().template command<long long>("HSTRLEN", key, field).result();
    }

    /**
     * @brief Asynchronous version of hstrlen
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the hash is stored
     * @param field Field to get length of
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    Derived &
    hstrlen(Func &&func, const std::string &key, const std::string &field) {
        return derived().template command<long long>(std::forward<Func>(func), "HSTRLEN",
                                                     key, field);
    }

    /**
     * @brief Gets all values in a hash
     *
     * @param key Key where the hash is stored
     * @return Vector of all values
     */
    std::vector<std::string>
    hvals(const std::string &key) {
        return derived()
            .template command<std::vector<std::string>>("HVALS", key)
            .result();
    }

    /**
     * @brief Asynchronous version of hvals
     *
     * @tparam Func Callback function type
     * @param func Callback function
     * @param key Key where the hash is stored
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    Derived &
    hvals(Func &&func, const std::string &key) {
        return derived().template command<std::vector<std::string>>(
            std::forward<Func>(func), "HVALS", key);
    }

    /**
     * @brief Gets all values from multiple hashes
     *
     * @tparam Func Callback function type
     * @param func Callback function to process all values from all hashes
     * @param keys Keys where the hashes are stored
     * @return Reference to the Redis handler for chaining
     */
    template <typename Func>
    Derived &
    hvals(Func &&func, std::vector<std::string> keys) {
        new multi_hvals<Func>(derived(), std::move(keys), std::forward<Func>(func));
        return derived();
    }
};

} // namespace qb::redis

#endif // QBM_REDIS_HASH_COMMANDS_H
