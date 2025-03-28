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
#include <hiredis/hiredis.h>

namespace qb::redis {

/**
 * @struct ReplyDeleter
 * @brief Custom deleter for Redis reply objects
 */
struct ReplyDeleter {
    /**
     * @brief Deallocates a Redis reply object
     * @param reply Redis reply to delete
     */
    void
    operator()(redisReply *reply) const {
        freeReplyObject(reply);
    }
};

/**
 * @typedef reply_ptr
 * @brief Smart pointer for Redis reply objects
 */
using reply_ptr = std::unique_ptr<redisReply, ReplyDeleter>;

/**
 * @enum UpdateType
 * @brief Specifies update behavior for key-value operations
 * 
 * Used with SET-like operations to control when keys should be updated.
 */
enum class UpdateType { EXIST, NOT_EXIST, ALWAYS };

/**
 * @enum InsertPosition
 * @brief Specifies insertion position for list operations
 * 
 * Used with list operations to specify whether to insert before or after a pivot.
 */
enum class InsertPosition { BEFORE, AFTER };

/**
 * @enum ListPosition
 * @brief Specifies position in a list
 * 
 * Used with list operations to specify whether to insert before or after a pivot.
 */
enum class ListPosition { LEFT, RIGHT };

/**
 * @enum BoundType
 * @brief Specifies boundary type for interval operations
 * 
 * Controls whether interval boundaries are inclusive or exclusive.
 */
enum class BoundType { CLOSED, OPEN, LEFT_OPEN, RIGHT_OPEN };

/**
 * @class UnboundedInterval
 * @brief Represents an unbounded interval (-inf, +inf)
 * 
 * Used for range queries with no bounds in either direction.
 * 
 * @tparam T Type of the interval values
 */
template <typename T>
class UnboundedInterval;

/**
 * @class BoundedInterval
 * @brief Represents a bounded interval [min, max], (min, max), (min, max], or [min, max)
 * 
 * Used for range queries with both lower and upper bounds.
 * 
 * @tparam T Type of the interval values
 */
template <typename T>
class BoundedInterval;

/**
 * @class LeftBoundedInterval
 * @brief Represents a left-bounded interval [min, +inf) or (min, +inf)
 * 
 * Used for range queries with only a lower bound.
 * 
 * @tparam T Type of the interval values
 */
template <typename T>
class LeftBoundedInterval;

/**
 * @class RightBoundedInterval
 * @brief Represents a right-bounded interval (-inf, max] or (-inf, max)
 * 
 * Used for range queries with only an upper bound.
 * 
 * @tparam T Type of the interval values
 */
template <typename T>
class RightBoundedInterval;

/**
 * @class UnboundedInterval<double>
 * @brief Double specialization for unbounded interval
 */
template <>
class UnboundedInterval<double> {
public:
    [[nodiscard]] const std::string &lower() const;
    [[nodiscard]] const std::string &upper() const;
};

/**
 * @class BoundedInterval<double>
 * @brief Double specialization for bounded interval
 */
template <>
class BoundedInterval<double> {
public:
    BoundedInterval(double min, double max, BoundType type);

    [[nodiscard]] const std::string &
    lower() const {
        return _min;
    }

    [[nodiscard]] const std::string &
    upper() const {
        return _max;
    }

private:
    std::string _min;
    std::string _max;
};

/**
 * @class LeftBoundedInterval<double>
 * @brief Double specialization for left-bounded interval
 */
template <>
class LeftBoundedInterval<double> {
public:
    LeftBoundedInterval(double min, BoundType type);

    [[nodiscard]] const std::string &
    lower() const {
        return _min;
    }

    [[nodiscard]] const std::string &upper() const;

private:
    std::string _min;
};

template <>
class RightBoundedInterval<double> {
public:
    /**
     * @brief Constructs a right-bounded interval with a maximum value
     * 
     * @param max Maximum value for the interval
     * @param type Boundary type (open or closed)
     */
    RightBoundedInterval(double max, BoundType type);

    /**
     * @brief Gets the lower bound string representation
     * @return String representation of the lower bound (-inf)
     */
    [[nodiscard]] const std::string &lower() const;
    
    /**
     * @brief Gets the upper bound string representation
     * @return String representation of the upper bound
     */
    [[nodiscard]] const std::string &
    upper() const {
        return _max;
    }

private:
    std::string _max;
};

template <>
class UnboundedInterval<std::string> {
public:
    /**
     * @brief Gets the lower bound string representation
     * @return String representation of the lower bound (-inf)
     */
    [[nodiscard]] const std::string &lower() const;
    
    /**
     * @brief Gets the upper bound string representation
     * @return String representation of the upper bound (+inf)
     */
    [[nodiscard]] const std::string &upper() const;
};

template <>
class BoundedInterval<std::string> {
public:
    /**
     * @brief Constructs a bounded interval with minimum and maximum values
     * 
     * @param min Minimum value for the interval
     * @param max Maximum value for the interval
     * @param type Boundary type (open, closed, or partially open)
     */
    BoundedInterval(const std::string &min, const std::string &max, BoundType type);
    
    /**
     * @brief Gets the lower bound string representation
     * @return String representation of the lower bound
     */
    [[nodiscard]] const std::string &
    lower() const {
        return _min;
    }
    
    /**
     * @brief Gets the upper bound string representation
     * @return String representation of the upper bound
     */
    [[nodiscard]] const std::string &
    upper() const {
        return _max;
    }

private:
    std::string _min;
    std::string _max;
};

template <>
class LeftBoundedInterval<std::string> {
public:
    /**
     * @brief Constructs a left-bounded interval with a minimum value
     * 
     * @param min Minimum value for the interval
     * @param type Boundary type (open or closed)
     */
    LeftBoundedInterval(const std::string &min, BoundType type);
    
    /**
     * @brief Gets the lower bound string representation
     * @return String representation of the lower bound
     */
    [[nodiscard]] const std::string &
    lower() const {
        return _min;
    }
    
    /**
     * @brief Gets the upper bound string representation
     * @return String representation of the upper bound (+inf)
     */
    [[nodiscard]] const std::string &upper() const;

private:
    std::string _min;
};

template <>
class RightBoundedInterval<std::string> {
public:
    /**
     * @brief Constructs a right-bounded interval with a maximum value
     * 
     * @param max Maximum value for the interval
     * @param type Boundary type (open or closed)
     */
    RightBoundedInterval(const std::string &max, BoundType type);
    
    /**
     * @brief Gets the lower bound string representation
     * @return String representation of the lower bound (-inf)
     */
    [[nodiscard]] const std::string &lower() const;
    
    /**
     * @brief Gets the upper bound string representation
     * @return String representation of the upper bound
     */
    [[nodiscard]] const std::string &
    upper() const {
        return _max;
    }

private:
    std::string _max;
};

/**
 * @struct LimitOptions
 * @brief Options for limiting query results
 * 
 * Used with Redis commands that support limiting the number of returned items.
 */
struct LimitOptions {
    long long offset = 0;  ///< Starting offset for results
    long long count = -1;  ///< Number of items to return (-1 for unlimited)
};

/**
 * @enum Aggregation
 * @brief Aggregation operations for sorted set commands
 */
enum class Aggregation { SUM, MIN, MAX };

/**
 * @enum BitOp
 * @brief Bitwise operations for bitmap commands
 */
enum class BitOp { AND, OR, XOR, NOT };

/**
 * @enum GeoUnit
 * @brief Distance units for geospatial commands
 */
enum class GeoUnit { M, KM, MI, FT };

/**
 * @enum XtrimStrategy
 * @brief Trimming strategies for stream commands
 */
enum class XtrimStrategy { MAXLEN, MINID };

/**
 * @struct geo_pos
 * @brief Container for Redis GEO position information
 */
struct geo_pos {
    double longitude{};
    double latitude{};

    bool operator==(const geo_pos& other) const {
        return longitude == other.longitude && latitude == other.latitude;
    }

    bool operator!=(const geo_pos& other) const {
        return !(*this == other);
    }
};

/**
 * @struct geo_distance
 * @brief Container for Redis GEO distance information
 */
struct geo_distance {
    std::string member;
    double distance{};
};

/**
 * @struct stream_id
 * @brief Container for Redis Stream ID
 */
struct stream_id {
    long long timestamp{};
    long long sequence{};

    std::string to_string() const {
        return std::to_string(timestamp) + "-" + std::to_string(sequence);
    }

    bool operator==(const stream_id& other) const {
        return timestamp == other.timestamp && sequence == other.sequence;
    }

    bool operator!=(const stream_id& other) const {
        return !(*this == other);
    }

    bool operator<(const stream_id& other) const {
        return timestamp < other.timestamp ||
              (timestamp == other.timestamp && sequence < other.sequence);
    }
};

/**
 * @struct stream_entry
 * @brief Container for Redis Stream entry
 */
struct stream_entry {
    stream_id id;
    qb::unordered_map<std::string, std::string> fields;
};

using stream_entry_list = std::vector<stream_entry>;
using map_stream_entry_list = qb::unordered_map<std::string, stream_entry_list>;

/**
 * @struct score
 * @brief Container for a Redis sorted set score
 */
struct score {
    double value{};
    
    bool operator==(const score& other) const {
        return value == other.value;
    }
    
    bool operator<(const score& other) const {
        return value < other.value;
    }
};

/**
 * @struct score_member
 * @brief Container for a Redis sorted set member with its score
 */
struct score_member {
    double score{};
    std::string member;
    
    bool operator==(const score_member& other) const {
        return score == other.score && member == other.member;
    }
};

/**
 * @struct search_result
 * @brief Container for Redis search results
 */
struct search_result {
    std::string key;
    std::vector<std::string> fields;
    std::vector<std::string> values;
};

/**
 * @struct cluster_node
 * @brief Container for Redis cluster node information
 */
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

/**
 * @struct memory_info
 * @brief Container for Redis memory statistics
 */
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
 * @struct pipeline_result
 * @brief Container for Redis pipeline command results
 */
struct pipeline_result {
    std::vector<reply_ptr> replies;
    bool all_succeeded{true};
};

/**
 * @struct json_value
 * @brief Container for Redis JSON values
 */
struct json_value {
    enum class Type { Null, Boolean, Number, String, Array, Object };
    
    Type type{Type::Null};
    std::variant<
        std::nullptr_t,
        bool,
        double,
        std::string,
        std::vector<json_value>,
        qb::unordered_map<std::string, json_value>
    > data{nullptr};
    
    bool is_null() const { return type == Type::Null; }
    bool is_bool() const { return type == Type::Boolean; }
    bool is_number() const { return type == Type::Number; }
    bool is_string() const { return type == Type::String; }
    bool is_array() const { return type == Type::Array; }
    bool is_object() const { return type == Type::Object; }
};

/**
 * @struct message
 * @brief Container for Redis pub/sub message data
 */
struct message {
    std::string_view pattern;
    std::string_view channel;
    std::string_view message;
    reply_ptr raw;
};

/**
 * @struct pmessage
 * @brief Container for Redis pub/sub pattern message data
 */
struct pmessage : public message {};

/**
 * @struct subscription
 * @brief Container for Redis subscription information
 */
struct subscription {
    std::optional<std::string> channel;
    long long num{};
};

/**
 * @struct status
 * @brief Container for Redis status reply
 */
struct status {
private:
    std::string _str;

public:
    /**
     * @brief Default constructor
     */
    status() = default;
    
    /**
     * @brief Constructor from string
     * @param str The status string
     */
    explicit status(std::string str) : _str(std::move(str)) {}
    
    /**
     * @brief Get the status string
     * @return The status string
     */
    [[nodiscard]] const std::string& str() const {
        return _str;
    }
    
    /**
     * @brief Convert to string
     * @return The status string
     */
    operator std::string() const {
        return _str;
    }
    
    /**
     * @brief Convert to bool, checking if status is "OK"
     * @return true if status is "OK", false otherwise
     */
    operator bool() const {
        return _str == "OK";
    }
    bool
    operator()() const {
        return static_cast<bool>(*this);
    }

    /**
     * @brief Compare status with a string
     * @param other String to compare with
     * @return true if equal, false otherwise
     */
    bool operator==(const std::string& other) const {
        return _str == other;
    }
    
    /**
     * @brief Compare status with a string
     * @param other String to compare with
     * @return true if not equal, false otherwise
     */
    bool operator!=(const std::string& other) const {
        return _str != other;
    }
};

/**
 * @struct scan
 * @brief Container for Redis scan command results
 * @tparam Out Output container type for scan results
 */
template <typename Out = std::vector<std::string>>
struct scan {
    std::size_t cursor;
    Out items;
};

/**
 * @struct error
 * @brief Container for Redis error information
 */
struct error {
    std::string what;
    reply_ptr raw;
};

// Redis reply type checking functions
/**
 * @brief Checks if a Redis reply is an error
 * @param reply The Redis reply to check
 * @return true if the reply is an error, false otherwise
 */
inline bool
is_error(redisReply &reply) {
    return reply.type == REDIS_REPLY_ERROR;
}

/**
 * @brief Checks if a Redis reply is a nil value
 * @param reply The Redis reply to check
 * @return true if the reply is nil, false otherwise
 */
inline bool
is_nil(redisReply &reply) {
    return reply.type == REDIS_REPLY_NIL;
}

/**
 * @brief Checks if a Redis reply is a string
 * @param reply The Redis reply to check
 * @return true if the reply is a string, false otherwise
 */
inline bool
is_string(redisReply &reply) {
    return reply.type == REDIS_REPLY_STRING;
}

/**
 * @brief Checks if a Redis reply is a status
 * @param reply The Redis reply to check
 * @return true if the reply is a status, false otherwise
 */
inline bool
is_status(redisReply &reply) {
    return reply.type == REDIS_REPLY_STATUS;
}

/**
 * @brief Checks if a Redis reply is an integer
 * @param reply The Redis reply to check
 * @return true if the reply is an integer, false otherwise
 */
inline bool
is_integer(redisReply &reply) {
    return reply.type == REDIS_REPLY_INTEGER;
}

/**
 * @brief Checks if a Redis reply is an array
 * @param reply The Redis reply to check
 * @return true if the reply is an array, false otherwise
 */
inline bool
is_array(redisReply &reply) {
    return reply.type == REDIS_REPLY_ARRAY;
}

#ifdef REDIS_PLUS_PLUS_RESP_VERSION_3

/**
 * @brief Checks if a Redis reply is a double
 * @param reply The Redis reply to check
 * @return true if the reply is a double, false otherwise
 */
inline bool
is_double(redisReply &reply) {
    return reply.type == REDIS_REPLY_DOUBLE;
}

/**
 * @brief Checks if a Redis reply is a boolean
 * @param reply The Redis reply to check
 * @return true if the reply is a boolean, false otherwise
 */
inline bool
is_bool(redisReply &reply) {
    return reply.type == REDIS_REPLY_BOOL;
}

/**
 * @brief Checks if a Redis reply is a map
 * @param reply The Redis reply to check
 * @return true if the reply is a map, false otherwise
 */
inline bool
is_map(redisReply &reply) {
    return reply.type == REDIS_REPLY_MAP;
}

/**
 * @brief Checks if a Redis reply is a set
 * @param reply The Redis reply to check
 * @return true if the reply is a set, false otherwise
 */
inline bool
is_set(redisReply &reply) {
    return reply.type == REDIS_REPLY_SET;
}

/**
 * @brief Checks if a Redis reply is an attribute
 * @param reply The Redis reply to check
 * @return true if the reply is an attribute, false otherwise
 */
inline bool
is_attr(redisReply &reply) {
    return reply.type == REDIS_REPLY_ATTR;
}

/**
 * @brief Checks if a Redis reply is a push message
 * @param reply The Redis reply to check
 * @return true if the reply is a push message, false otherwise
 */
inline bool
is_push(redisReply &reply) {
    return reply.type == REDIS_REPLY_PUSH;
}

/**
 * @brief Checks if a Redis reply is a big number
 * @param reply The Redis reply to check
 * @return true if the reply is a big number, false otherwise
 */
inline bool
is_bignum(redisReply &reply) {
    return reply.type == REDIS_REPLY_BIGNUM;
}

/**
 * @brief Checks if a Redis reply is a verbatim string
 * @param reply The Redis reply to check
 * @return true if the reply is a verbatim string, false otherwise
 */
inline bool
is_verb(redisReply &reply) {
    return reply.type == REDIS_REPLY_VERB;
}

#endif

} // namespace qb::redis

namespace std {
/**
 * @brief Converts a BitOp enum to string
 * @param op The BitOp value to convert
 * @return String representation of the BitOp value
 */
std::string to_string(qb::redis::BitOp op);

/**
 * @brief Converts an UpdateType enum to string
 * @param op The UpdateType value to convert
 * @return String representation of the UpdateType value
 */
std::string to_string(qb::redis::UpdateType op);

/**
 * @brief Converts an Aggregation enum to string
 * @param op The Aggregation value to convert
 * @return String representation of the Aggregation value
 */
std::string to_string(qb::redis::Aggregation op);

/**
 * @brief Converts a GeoUnit enum to string
 * @param op The GeoUnit value to convert
 * @return String representation of the GeoUnit value
 */
std::string to_string(qb::redis::GeoUnit op);

/**
 * @brief Converts an InsertPosition enum to string
 * @param pos The InsertPosition value to convert
 * @return String representation of the InsertPosition value
 */
std::string to_string(qb::redis::InsertPosition pos);

/**
 * @brief Converts a ListPosition enum to string
 * @param pos The ListPosition value to convert
 * @return String representation of the ListPosition value
 */
std::string to_string(qb::redis::ListPosition pos);
} // namespace std

#endif // QBM_REDIS_TYPES_H
