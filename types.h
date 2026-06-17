/**
 * @file types.h
 * @brief Redis-specific types: enums, intervals, geo, stream, score, etc.
 */
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

#ifndef QBM_REDIS_TYPES_H
#define QBM_REDIS_TYPES_H

#include <string>
#include <string_view>
#include <tuple>
#include <map>
#include <vector>
#include <memory>
#include <unordered_map>
#include <optional>
#include <variant>
#include <chrono>

// Native C++20/23 parser (must be outside namespace)
#include "parser.h"

namespace qb::redis {

/** @brief Unique pointer to parser::Value for reply ownership */
using reply_ptr = std::unique_ptr<parser::Value>;

// ============================================================================
// Enums
// ============================================================================

/** @brief Key existence mode for SET NX/XX options */
enum class UpdateType { EXIST, NOT_EXIST, ALWAYS };
/** @brief Insert position for list commands */
enum class InsertPosition { BEFORE, AFTER };
/** @brief List end (left/right) for LPUSH, RPOP, etc. */
enum class ListPosition { LEFT, RIGHT };
/** @brief Interval bound type for range queries */
enum class BoundType { CLOSED, OPEN, LEFT_OPEN, RIGHT_OPEN };
/** @brief Aggregation for sorted set ZUNION/ZINTER */
enum class Aggregation { SUM, MIN, MAX };
/** @brief Bitwise operation for BITOP */
enum class BitOp { AND, OR, XOR, NOT };
/** @brief Distance unit for GEO commands */
enum class GeoUnit { M, KM, MI, FT };
/** @brief XTRIM strategy (MAXLEN or MINID) */
enum class XtrimStrategy { MAXLEN, MINID };

// ============================================================================
// Interval types
// ============================================================================

/** @brief Unbounded interval (-inf, +inf) for ZRANGEBYSCORE etc. */
template <typename T>
class UnboundedInterval;

template <typename T>
class BoundedInterval;

template <typename T>
class LeftBoundedInterval;

template <typename T>
class RightBoundedInterval;

template <>
class UnboundedInterval<double> {
public:
    [[nodiscard]] const std::string &lower() const;
    [[nodiscard]] const std::string &upper() const;
};

template <>
class BoundedInterval<double> {
public:
    BoundedInterval(double min, double max, BoundType type);
    
    [[nodiscard]] const std::string &lower() const { return _min; }
    [[nodiscard]] const std::string &upper() const { return _max; }

private:
    std::string _min;
    std::string _max;
};

template <>
class LeftBoundedInterval<double> {
public:
    LeftBoundedInterval(double min, BoundType type);
    
    [[nodiscard]] const std::string &lower() const { return _min; }
    [[nodiscard]] const std::string &upper() const;

private:
    std::string _min;
};

template <>
class RightBoundedInterval<double> {
public:
    RightBoundedInterval(double max, BoundType type);
    
    [[nodiscard]] const std::string &lower() const;
    [[nodiscard]] const std::string &upper() const { return _max; }

private:
    std::string _max;
};

template <>
class UnboundedInterval<std::string> {
public:
    [[nodiscard]] const std::string &lower() const;
    [[nodiscard]] const std::string &upper() const;
};

template <>
class BoundedInterval<std::string> {
public:
    BoundedInterval(const std::string &min, const std::string &max, BoundType type);
    
    [[nodiscard]] const std::string &lower() const { return _min; }
    [[nodiscard]] const std::string &upper() const { return _max; }

private:
    std::string _min;
    std::string _max;
};

template <>
class LeftBoundedInterval<std::string> {
public:
    LeftBoundedInterval(const std::string &min, BoundType type);
    
    [[nodiscard]] const std::string &lower() const { return _min; }
    [[nodiscard]] const std::string &upper() const;

private:
    std::string _min;
};

template <>
class RightBoundedInterval<std::string> {
public:
    RightBoundedInterval(const std::string &max, BoundType type);
    
    [[nodiscard]] const std::string &lower() const;
    [[nodiscard]] const std::string &upper() const { return _max; }

private:
    std::string _max;
};

using lex_interval = BoundedInterval<std::string>;
using score_interval = BoundedInterval<double>;

// ============================================================================
// Options structures
// ============================================================================

/** @brief LIMIT offset/count for range queries */
struct LimitOptions {
    long long offset = 0;
    long long count = -1;
};

// ============================================================================
// Data structures
// ============================================================================

/** @brief Geographic coordinates (longitude, latitude) */
struct geo_pos {
    double longitude{};
    double latitude{};
    
    bool operator==(const geo_pos &) const = default;
};

/** @brief GEO result: member name and distance */
struct geo_distance {
    std::string member;
    double distance{};
};

/** @brief Redis stream entry ID (timestamp-sequence) */
struct stream_id {
    long long timestamp{};
    long long sequence{};
    
    [[nodiscard]] std::string to_string() const {
        return std::to_string(timestamp) + "-" + std::to_string(sequence);
    }
    
    bool operator==(const stream_id &other) const = default;
    bool operator!=(const stream_id &other) const = default;
    
    bool operator<(const stream_id &other) const {
        return timestamp < other.timestamp ||
               (timestamp == other.timestamp && sequence < other.sequence);
    }
};

/** @brief Stream entry with ID and field-value map */
struct stream_entry {
    stream_id id;
    qb::unordered_map<std::string, std::string> fields;
};

using stream_entry_list = std::vector<stream_entry>;
using map_stream_entry_list = qb::unordered_map<std::string, stream_entry_list>;

/** @brief Sorted set score value */
struct score {
    double value{};
    
    bool operator==(const score &other) const = default;
    bool operator<(const score &other) const { return value < other.value; }
};

/** @brief Sorted set member with score */
struct score_member {
    double score{};
    std::string member;
    
    bool operator==(const score_member &other) const = default;
};

/** @brief FT.SEARCH result (key, fields, values) */
struct search_result {
    std::string key;
    std::vector<std::string> fields;
    std::vector<std::string> values;
};

/** @brief CLUSTER NODES entry */
struct cluster_node {
    std::string id;
    std::string ip;
    int port{};
    std::vector<std::string> flags;
    std::string master;
    long long ping_sent{};
    long long pong_received{};
    int epoch{};
    std::string link_state;
    std::vector<std::string> slots;
};

/** @brief INFO memory section parsed data */
struct memory_info {
    size_t used_memory{};
    size_t used_memory_peak{};
    size_t used_memory_lua{};
    size_t used_memory_scripts{};
    size_t number_of_keys{};
    size_t number_of_expires{};
    size_t number_of_connected_clients{};
    size_t number_of_slaves{};
    size_t number_of_replicas{};
    size_t number_of_commands_processed{};
    size_t total_connections_received{};
    size_t total_commands_processed{};
    size_t instantaneous_ops_per_sec{};
    size_t total_net_input_bytes{};
    size_t total_net_output_bytes{};
    size_t instantaneous_input_kbps{};
    size_t instantaneous_output_kbps{};
};

/**
 * @brief Result of a Redis MULTI/EXEC transaction.
 *
 * Individual replies are not individually owned here because `parser::Value`
 * is move-only.  Callers that need per-command results should access the raw
 * EXEC array via `Reply<pipeline_result>.raw()`.
 */
struct pipeline_result {
    size_t size{0};          ///< total number of queued commands executed
    size_t error_count{0};   ///< number of commands that returned an error
    bool all_succeeded{true};
};

/** @brief Redis JSON value (variant of null, bool, number, string, array, object) */
struct json_value {
    enum class Type { Null, Boolean, Number, String, Array, Object };

    Type type{Type::Null};
    std::variant<std::nullptr_t, bool, double, std::string, std::vector<json_value>,
                 qb::unordered_map<std::string, json_value>>
        data{nullptr};

    [[nodiscard]] bool is_null() const { return type == Type::Null; }
    [[nodiscard]] bool is_bool() const { return type == Type::Boolean; }
    [[nodiscard]] bool is_number() const { return type == Type::Number; }
    [[nodiscard]] bool is_string() const { return type == Type::String; }
    [[nodiscard]] bool is_array() const { return type == Type::Array; }
    [[nodiscard]] bool is_object() const { return type == Type::Object; }
};

/** @brief Pub/Sub message (channel, payload) */
struct message {
    std::string pattern;
    std::string channel;
    std::string payload;
    reply_ptr raw;
};

/** @brief Pattern-matched Pub/Sub message (adds pattern field) */
struct pmessage : public message {};

/** @brief Subscription confirmation (channel, count) */
struct subscription {
    std::optional<std::string> channel;
    long long num{};
};

/** @brief Redis status reply (e.g. "OK") */
struct status {
private:
    std::string _str;

public:
    status() = default;
    explicit status(std::string str) : _str(std::move(str)) {}
    
    [[nodiscard]] const std::string &str() const { return _str; }
    
    operator std::string() const { return _str; }
    
    [[nodiscard]] operator bool() const { return _str == "OK"; }
    [[nodiscard]] bool operator()() const { return static_cast<bool>(*this); }
    [[nodiscard]] bool ok() const { return _str == "OK"; }
    
    [[nodiscard]] bool operator==(const std::string &other) const { return _str == other; }
    [[nodiscard]] bool operator!=(const std::string &other) const { return _str != other; }
};

/** @brief SCAN result with cursor and items */
template <typename Out = std::vector<std::string>>
struct scan {
    std::size_t cursor;
    Out items;
};

/** @brief Redis error with message and raw reply */
struct error {
    std::string what;
    reply_ptr raw;
};

// ============================================================================
// Enum to string helpers
// ============================================================================

std::string to_string(BitOp op);
std::string to_string(UpdateType op);
std::string to_string(Aggregation op);
std::string to_string(GeoUnit op);
std::string to_string(InsertPosition pos);
std::string to_string(ListPosition pos);

} // namespace qb::redis

#endif // QBM_REDIS_TYPES_H
