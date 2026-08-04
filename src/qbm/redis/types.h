/**
 * @file qbm/redis/types.h
 * @brief Redis value-domain types: enums, range intervals, geo, stream, score and
 *
 *        reply structures.
 *
 * Defines the strongly-typed C++ vocabulary used across the qbm-redis command
 * mixins: command option enums (@ref qb::redis::UpdateType, @ref qb::redis::BitOp,
 * ...), the range-interval family (bounded/left-bounded/right-bounded/unbounded
 * over @c double scores and lexicographic @c std::string bounds), and the parsed
 * reply structures (@ref qb::redis::stream_entry, @ref qb::redis::score_member,
 * @ref qb::redis::status, @ref qb::redis::message, ...). The non-template
 * out-of-line definitions for the interval specializations and the enum
 * @c to_string overloads live in @c redis.cpp.
 *
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2026 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)
 * @ingroup Redis
 */
#ifndef QBM_REDIS_TYPES_H
#define QBM_REDIS_TYPES_H

#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <variant>
#include <vector>
#include <qb/system/container/unordered_map.h> // qb::unordered_map — the map replies below use it

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

/**
 * @brief Fully unbounded interval `(-inf, +inf)`.
 *
 * Used by score/lexicographic range commands (e.g. ZRANGEBYSCORE,
 * ZRANGEBYLEX) to select every element. Only the @c double and
 * @c std::string specializations are provided.
 */
template <typename T>
class UnboundedInterval;

/**
 * @brief Interval bounded on both ends, `[min, max]` / `(min, max)`.
 *
 * The @ref BoundType passed at construction selects whether each endpoint is
 * inclusive or exclusive. Only the @c double and @c std::string
 * specializations are provided.
 */
template <typename T>
class BoundedInterval;

/**
 * @brief Interval bounded on the lower end only, `[min, +inf)`.
 *
 * Only the @c double and @c std::string specializations are provided.
 */
template <typename T>
class LeftBoundedInterval;

/**
 * @brief Interval bounded on the upper end only, `(-inf, max]`.
 *
 * Only the @c double and @c std::string specializations are provided.
 */
template <typename T>
class RightBoundedInterval;

/** @brief Unbounded score interval `(-inf, +inf)` rendered as `-inf` / `+inf`. */
template <>
class UnboundedInterval<double> {
public:
    /** @return Redis-encoded lower bound (`-inf`). */
    [[nodiscard]] const std::string &lower() const;
    /** @return Redis-encoded upper bound (`+inf`). */
    [[nodiscard]] const std::string &upper() const;
};

/** @brief Score interval bounded on both ends, with per-endpoint inclusivity. */
template <>
class BoundedInterval<double> {
public:
    /**
     * @brief Build a closed/open score interval.
     * @param min  Lower bound value.
     * @param max  Upper bound value.
     * @param type Endpoint inclusivity (see @ref BoundType).
     */
    BoundedInterval(double min, double max, BoundType type);

    /** @return Redis-encoded lower bound. */
    [[nodiscard]] const std::string &
    lower() const {
        return _min;
    }
    /** @return Redis-encoded upper bound. */
    [[nodiscard]] const std::string &
    upper() const {
        return _max;
    }

private:
    std::string _min;
    std::string _max;
};

/** @brief Score interval bounded on the lower end, upper end open at `+inf`. */
template <>
class LeftBoundedInterval<double> {
public:
    /**
     * @brief Build a lower-bounded score interval.
     * @param min  Lower bound value.
     * @param type Lower-endpoint inclusivity (see @ref BoundType).
     */
    LeftBoundedInterval(double min, BoundType type);

    /** @return Redis-encoded lower bound. */
    [[nodiscard]] const std::string &
    lower() const {
        return _min;
    }
    /** @return Redis-encoded upper bound (`+inf`). */
    [[nodiscard]] const std::string &upper() const;

private:
    std::string _min;
};

/** @brief Score interval bounded on the upper end, lower end open at `-inf`. */
template <>
class RightBoundedInterval<double> {
public:
    /**
     * @brief Build an upper-bounded score interval.
     * @param max  Upper bound value.
     * @param type Upper-endpoint inclusivity (see @ref BoundType).
     */
    RightBoundedInterval(double max, BoundType type);

    /** @return Redis-encoded lower bound (`-inf`). */
    [[nodiscard]] const std::string &lower() const;
    /** @return Redis-encoded upper bound. */
    [[nodiscard]] const std::string &
    upper() const {
        return _max;
    }

private:
    std::string _max;
};

/** @brief Unbounded lexicographic interval rendered as `-` / `+`. */
template <>
class UnboundedInterval<std::string> {
public:
    /** @return Redis-encoded lower bound (`-`). */
    [[nodiscard]] const std::string &lower() const;
    /** @return Redis-encoded upper bound (`+`). */
    [[nodiscard]] const std::string &upper() const;
};

/** @brief Lexicographic interval bounded on both ends, with endpoint inclusivity. */
template <>
class BoundedInterval<std::string> {
public:
    /**
     * @brief Build a closed/open lexicographic interval.
     * @param min  Lower bound member.
     * @param max  Upper bound member.
     * @param type Endpoint inclusivity (see @ref BoundType).
     */
    BoundedInterval(const std::string &min, const std::string &max, BoundType type);

    /** @return Redis-encoded lower bound. */
    [[nodiscard]] const std::string &
    lower() const {
        return _min;
    }
    /** @return Redis-encoded upper bound. */
    [[nodiscard]] const std::string &
    upper() const {
        return _max;
    }

private:
    std::string _min;
    std::string _max;
};

/** @brief Lexicographic interval bounded on the lower end, upper end open at `+`. */
template <>
class LeftBoundedInterval<std::string> {
public:
    /**
     * @brief Build a lower-bounded lexicographic interval.
     * @param min  Lower bound member.
     * @param type Lower-endpoint inclusivity (see @ref BoundType).
     */
    LeftBoundedInterval(const std::string &min, BoundType type);

    /** @return Redis-encoded lower bound. */
    [[nodiscard]] const std::string &
    lower() const {
        return _min;
    }
    /** @return Redis-encoded upper bound (`+`). */
    [[nodiscard]] const std::string &upper() const;

private:
    std::string _min;
};

/** @brief Lexicographic interval bounded on the upper end, lower end open at `-`. */
template <>
class RightBoundedInterval<std::string> {
public:
    /**
     * @brief Build an upper-bounded lexicographic interval.
     * @param max  Upper bound member.
     * @param type Upper-endpoint inclusivity (see @ref BoundType).
     */
    RightBoundedInterval(const std::string &max, BoundType type);

    /** @return Redis-encoded lower bound (`-`). */
    [[nodiscard]] const std::string &lower() const;
    /** @return Redis-encoded upper bound. */
    [[nodiscard]] const std::string &
    upper() const {
        return _max;
    }

private:
    std::string _max;
};

/** @brief Convenience alias: lexicographic range `[min, max]` over members. */
using lex_interval = BoundedInterval<std::string>;
/** @brief Convenience alias: score range `[min, max]` over doubles. */
using score_interval = BoundedInterval<double>;

// ============================================================================
// Options structures
// ============================================================================

/** @brief LIMIT clause for range queries: starting offset and element count. */
struct LimitOptions {
    long long offset = 0;  ///< Number of leading elements to skip.
    long long count  = -1; ///< Maximum elements to return (-1 means unlimited).
};

// ============================================================================
// Data structures
// ============================================================================

/** @brief Geographic coordinate pair. */
struct geo_pos {
    double longitude{}; ///< Longitude in degrees.
    double latitude{};  ///< Latitude in degrees.

    bool operator==(const geo_pos &) const = default;
};

/** @brief GEO result: member name paired with its distance to the query origin. */
struct geo_distance {
    std::string member;     ///< Member name.
    double      distance{}; ///< Distance in the requested @ref GeoUnit.
};

/** @brief Redis stream entry ID in `timestamp-sequence` form. */
struct stream_id {
    long long timestamp{}; ///< Millisecond timestamp component.
    long long sequence{};  ///< Sequence component within the timestamp.

    /** @return The ID rendered as `"<timestamp>-<sequence>"`. */
    [[nodiscard]] std::string
    to_string() const {
        return std::to_string(timestamp) + "-" + std::to_string(sequence);
    }

    bool operator==(const stream_id &other) const = default;
    bool operator!=(const stream_id &other) const = default;

    /** @return @c true if this ID orders strictly before @p other. */
    bool
    operator<(const stream_id &other) const {
        return timestamp < other.timestamp || (timestamp == other.timestamp && sequence < other.sequence);
    }
};

/** @brief A single stream entry: its ID and the field-value pairs it carries. */
struct stream_entry {
    stream_id                                   id;     ///< Entry ID.
    qb::unordered_map<std::string, std::string> fields; ///< Field-value payload.
};

using stream_entry_list     = std::vector<stream_entry>;
using map_stream_entry_list = qb::unordered_map<std::string, stream_entry_list>;

/** @brief Sorted-set score value wrapper, ordered by its numeric value. */
struct score {
    double value{}; ///< Score value.

    bool operator==(const score &other) const = default;
    /** @return @c true if this score is strictly less than @p other. */
    bool
    operator<(const score &other) const {
        return value < other.value;
    }
};

/** @brief Sorted-set entry: a member name with its associated score. */
struct score_member {
    double      score{}; ///< Score associated with the member.
    std::string member;  ///< Member name.

    bool operator==(const score_member &other) const = default;
};

/** @brief A single FT.SEARCH hit: document key with parallel field/value lists. */
struct search_result {
    std::string              key;    ///< Document key.
    std::vector<std::string> fields; ///< Returned field names.
    std::vector<std::string> values; ///< Field values, positionally aligned with @ref fields.
};

/** @brief A single CLUSTER NODES entry describing one cluster node. */
struct cluster_node {
    std::string              id;              ///< 40-character node ID.
    std::string              ip;              ///< Node IP address.
    int                      port{};          ///< Client port.
    std::vector<std::string> flags;           ///< Node flags (master, slave, myself, ...).
    std::string              master;          ///< Master node ID (empty for masters).
    long long                ping_sent{};     ///< Timestamp of the last ping sent.
    long long                pong_received{}; ///< Timestamp of the last pong received.
    int                      epoch{};         ///< Node configuration epoch.
    std::string              link_state;      ///< Link state (connected/disconnected).
    std::vector<std::string> slots;           ///< Hash-slot ranges served by the node.
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
    size_t size{0};        ///< total number of queued commands executed
    size_t error_count{0}; ///< number of commands that returned an error
    bool   all_succeeded{true};
};

/** @brief Redis JSON value: a tagged variant of null, bool, number, string, array, object. */
struct json_value {
    /** @brief Discriminator for the active alternative held in @ref data. */
    enum class Type { Null, Boolean, Number, String, Array, Object };

    Type type{Type::Null}; ///< Active alternative tag.
    std::variant<std::nullptr_t, bool, double, std::string, std::vector<json_value>, qb::unordered_map<std::string, json_value>> data{
        nullptr
    }; ///< Held value.

    /** @return @c true if the value is JSON null. */
    [[nodiscard]] bool
    is_null() const {
        return type == Type::Null;
    }
    /** @return @c true if the value is a boolean. */
    [[nodiscard]] bool
    is_bool() const {
        return type == Type::Boolean;
    }
    /** @return @c true if the value is a number. */
    [[nodiscard]] bool
    is_number() const {
        return type == Type::Number;
    }
    /** @return @c true if the value is a string. */
    [[nodiscard]] bool
    is_string() const {
        return type == Type::String;
    }
    /** @return @c true if the value is an array. */
    [[nodiscard]] bool
    is_array() const {
        return type == Type::Array;
    }
    /** @return @c true if the value is an object. */
    [[nodiscard]] bool
    is_object() const {
        return type == Type::Object;
    }
};

/** @brief Pub/Sub message delivered on a subscribed channel. */
struct message {
    std::string pattern; ///< Matching pattern (empty for plain SUBSCRIBE).
    std::string channel; ///< Channel the message was published to.
    std::string payload; ///< Message payload.
    reply_ptr   raw;     ///< Owning handle to the raw reply value.
};

/** @brief Pattern-matched Pub/Sub message (PSUBSCRIBE); reuses @ref message. */
struct pmessage : public message {};

/** @brief (Un)subscription confirmation: affected channel and remaining count. */
struct subscription {
    std::optional<std::string> channel; ///< Channel name (unset for some replies).
    long long                  num{};   ///< Number of channels still subscribed.
};

/**
 * @brief Redis simple-status reply (e.g. @c "OK").
 *
 * Convertible to @c std::string and contextually to @c bool, where truthiness
 * means the status equals @c "OK".
 */
struct status {
private:
    std::string _str;

public:
    /** @brief Construct an empty status. */
    status() = default;
    /**
     * @brief Construct from a status string.
     * @param str Raw status text returned by the server.
     */
    explicit status(std::string str)
        : _str(std::move(str)) {}

    /** @return The raw status text. */
    [[nodiscard]] const std::string &
    str() const {
        return _str;
    }

    /** @return The raw status text by value. */
    operator std::string() const {
        return _str;
    }

    /** @return @c true if the status equals @c "OK". */
    [[nodiscard]]
    operator bool() const {
        return _str == "OK";
    }
    /** @return @c true if the status equals @c "OK". */
    [[nodiscard]] bool
    operator()() const {
        return static_cast<bool>(*this);
    }
    /** @return @c true if the status equals @c "OK". */
    [[nodiscard]] bool
    ok() const {
        return _str == "OK";
    }

    /** @return @c true if the status text equals @p other. */
    [[nodiscard]] bool
    operator==(const std::string &other) const {
        return _str == other;
    }
    /** @return @c true if the status text differs from @p other. */
    [[nodiscard]] bool
    operator!=(const std::string &other) const {
        return _str != other;
    }
};

/**
 * @brief Result of a cursor-based SCAN family command.
 * @tparam Out Container type holding the returned items
 *             (defaults to @c std::vector<std::string>).
 */
template <typename Out = std::vector<std::string>>
struct scan {
    std::size_t cursor; ///< Cursor to pass to the next SCAN call (0 when complete).
    Out         items;  ///< Items returned by this scan step.
};

/** @brief Redis error reply: human-readable message plus the raw reply node. */
struct error {
    std::string what; ///< Error message text.
    reply_ptr   raw;  ///< Owning handle to the raw reply value.
};

// ============================================================================
// Enum to string helpers
// ============================================================================

/** @brief Render a @ref BitOp as its BITOP operation keyword. */
std::string to_string(BitOp op);
/** @brief Render an @ref UpdateType as its SET NX/XX modifier (empty for ALWAYS). */
std::string to_string(UpdateType op);
/** @brief Render an @ref Aggregation as its ZUNIONSTORE/ZINTERSTORE keyword. */
std::string to_string(Aggregation op);
/** @brief Render a @ref GeoUnit as its GEO distance-unit token. */
std::string to_string(GeoUnit op);
/** @brief Render an @ref InsertPosition as the LINSERT BEFORE/AFTER token. */
std::string to_string(InsertPosition pos);
/** @brief Render a @ref ListPosition as the LEFT/RIGHT token. */
std::string to_string(ListPosition pos);

} // namespace qb::redis

#endif // QBM_REDIS_TYPES_H
