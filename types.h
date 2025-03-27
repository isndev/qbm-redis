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

#ifndef QBM_REDIS_TYPES_H
#define QBM_REDIS_TYPES_H
#include <string>
#include <tuple>
#include <map>

namespace qb::redis {

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
} // namespace std

#endif // QBM_REDIS_TYPES_H
