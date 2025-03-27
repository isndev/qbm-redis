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

#include <stdexcept>
#include "reply.h"

namespace qb::redis {

/**
 * @brief Creates an error message for type mismatch errors
 * @param expect_type The expected Redis reply type
 * @param reply The actual Redis reply received
 * @return Formatted error message describing the mismatch
 */
std::string
ParseError::_err_info(const std::string &expect_type, const redisReply &reply) {
    return "expect " + expect_type + " reply, but got " + reply::type_to_string(reply.type) + " reply";
}

namespace reply {

/**
 * @brief Converts a Redis reply type code to a string representation
 * @param type Redis reply type code
 * @return String representation of the reply type
 */
std::string
type_to_string(int type) {
    switch (type) {
    case REDIS_REPLY_ERROR:
        return "ERROR";

    case REDIS_REPLY_NIL:
        return "NULL";

    case REDIS_REPLY_STRING:
        return "STRING";

    case REDIS_REPLY_STATUS:
        return "STATUS";

    case REDIS_REPLY_INTEGER:
        return "INTEGER";

    case REDIS_REPLY_ARRAY:
        return "ARRAY";

    default:
        return "UNKNOWN";
    }
}

/**
 * @brief Extracts a status string from a Redis reply
 * @param reply The Redis reply to extract status from
 * @return The status string contained in the reply
 * @throws ParseError if the reply is not a status reply
 * @throws ProtoError if the status string is null
 */
std::string
to_status(redisReply &reply) {
    if (!reply::is_status(reply)) {
        throw ParseError("STATUS", reply);
    }

    if (reply.str == nullptr) {
        throw ProtoError("A null status reply");
    }

    // Old version hiredis' *redisReply::len* is of type int.
    // So we CANNOT have something like: *return {reply.str, reply.len}*.
    return {reply.str, reply.len};
}

/**
 * @brief Parses a Redis reply into a string_view
 * @param tag Parse tag type (unused, for template specialization)
 * @param reply The Redis reply to parse
 * @return String view of the reply content
 * @throws ParseError if the reply is not a string or status reply
 * @throws ProtoError if the string is null
 */
std::string_view
parse(ParseTag<std::string_view>, redisReply &reply) {
#ifdef REDIS_PLUS_PLUS_RESP_VERSION_3
    if (!reply::is_string(reply) && !reply::is_status(reply) && !reply::is_verb(reply) && !reply::is_bignum(reply)) {
        throw ParseError("STRING or STATUS or VERB or BIGNUM", reply);
    }
#else
    if (reply::is_array(reply)) // pong in consumer
        return "PONG";
    if (!reply::is_string(reply) && !reply::is_status(reply)) {
        throw ParseError("STRING or STATUS", reply);
    }
#endif

    if (reply.str == nullptr) {
        throw ProtoError("A null string reply");
    }

    // Old version hiredis' *redisReply::len* is of type int.
    // So we CANNOT have something like: *return {reply.str, reply.len}*.
    return {reply.str, reply.len};
}

/**
 * @brief Parses a Redis reply into a std::string
 * @param tag Parse tag type (unused, for template specialization)
 * @param reply The Redis reply to parse
 * @return String copy of the reply content
 */
std::string
parse(ParseTag<std::string>, redisReply &reply) {
    auto str = parse(ParseTag<std::string_view>{}, reply);
    return {str.data(), str.size()};
}

/**
 * @brief Parses a Redis reply into a long long integer
 * @param tag Parse tag type (unused, for template specialization)
 * @param reply The Redis reply to parse
 * @return Integer value of the reply
 * @throws ParseError if the reply is not an integer reply
 */
long long
parse(ParseTag<long long>, redisReply &reply) {
    if (!reply::is_integer(reply)) {
        throw ParseError("INTEGER", reply);
    }

    return reply.integer;
}

/**
 * @brief Parses a Redis reply into a double
 * @param tag Parse tag type (unused, for template specialization)
 * @param reply The Redis reply to parse
 * @return Double value of the reply
 * @throws ParseError if the reply cannot be converted to a double
 */
double
parse(ParseTag<double>, redisReply &reply) {
#ifdef REDIS_PLUS_PLUS_RESP_VERSION_3
    if (is_double(reply)) {
        return reply.dval;
    } else {
        // Return by string reply.
#endif
        try {
            return std::stod(parse<std::string>(reply));
        } catch (const std::invalid_argument &) {
            throw ProtoError("not a double reply");
        } catch (const std::out_of_range &) {
            throw ProtoError("double reply out of range");
        }
#ifdef REDIS_PLUS_PLUS_RESP_VERSION_3
    }
#endif
}

/**
 * @brief Parses a Redis reply into a boolean
 * @param tag Parse tag type (unused, for template specialization)
 * @param reply The Redis reply to parse
 * @return Boolean value of the reply
 * @throws ParseError if the reply is not a boolean or integer reply
 * @throws ProtoError if the integer value is not 0 or 1
 */
bool
parse(ParseTag<bool>, redisReply &reply) {
#ifdef REDIS_PLUS_PLUS_RESP_VERSION_3
    long long ret = 0;
    if (is_bool(reply) || is_integer(reply)) {
        ret = reply.integer;
    } else {
        throw ProtoError("BOOL or INTEGER");
    }
#else
    auto ret = parse<long long>(reply);
#endif

    if (ret == 1) {
        return true;
    } else if (ret == 0) {
        return false;
    } else {
        throw ProtoError("Invalid bool reply: " + std::to_string(ret));
    }
}

/**
 * @brief Parses a Redis reply that doesn't return a value
 * @param tag Parse tag type (unused, for template specialization)
 * @param reply The Redis reply to parse
 * @throws ParseError if the reply is not a status reply
 * @throws ProtoError if the status is not "OK"
 */
void
parse(ParseTag<void>, redisReply &reply) {
    if (!reply::is_status(reply)) {
        throw ParseError("STATUS", reply);
    }

    if (reply.str == nullptr) {
        throw ProtoError("A null status reply");
    }

    static const std::string OK = "OK";

    // Old version hiredis' *redisReply::len* is of type int.
    // So we have to cast it to an unsigned int.
    if (static_cast<std::size_t>(reply.len) != OK.size() || OK.compare(0, OK.size(), reply.str, reply.len) != 0) {
        throw ProtoError("NOT ok status reply: " + reply::to_status(reply));
    }
}

/**
 * @brief Parses a Redis reply into a message structure
 * @param tag Parse tag type (unused, for template specialization)
 * @param reply The Redis reply to parse
 * @return Message structure containing channel and message content
 * @throws ProtoError if the reply structure doesn't match expected format
 */
message
parse(ParseTag<message>, redisReply &reply) {
    auto ptr = reply_ptr(&reply);
    if (reply.elements != 3) {
        throw ProtoError("Expect 3 sub replies");
    }

    assert(reply.element != nullptr);

    auto *channel_reply = reply.element[1];
    if (channel_reply == nullptr) {
        throw ProtoError("Null channel reply");
    }
    auto channel = reply::parse<std::string_view>(*channel_reply);

    auto *msg_reply = reply.element[2];
    if (msg_reply == nullptr) {
        throw ProtoError("Null message reply");
    }
    auto message = reply::parse<std::string_view>(*msg_reply);

    return {"", channel, message, std::move(ptr)};
}

/**
 * @brief Parses a Redis reply into a pattern message structure
 * @param tag Parse tag type (unused, for template specialization)
 * @param reply The Redis reply to parse
 * @return Pattern message structure containing pattern, channel and message content
 * @throws ProtoError if the reply structure doesn't match expected format
 */
pmessage
parse(ParseTag<pmessage>, redisReply &reply) {
    auto ptr = reply_ptr(&reply);
    if (reply.elements != 4) {
        throw ProtoError("Expect 4 sub replies");
    }

    assert(reply.element != nullptr);

    auto *pattern_reply = reply.element[1];
    if (pattern_reply == nullptr) {
        throw ProtoError("Null pattern reply");
    }
    auto pattern = reply::parse<std::string_view>(*pattern_reply);

    auto *channel_reply = reply.element[2];
    if (channel_reply == nullptr) {
        throw ProtoError("Null channel reply");
    }
    auto channel = reply::parse<std::string_view>(*channel_reply);

    auto *msg_reply = reply.element[3];
    if (msg_reply == nullptr) {
        throw ProtoError("Null message reply");
    }
    auto message = reply::parse<std::string_view>(*msg_reply);

    return {pattern, channel, message, std::move(ptr)};
}

/**
 * @brief Parses a Redis reply into a subscription structure
 * @param tag Parse tag type (unused, for template specialization)
 * @param reply The Redis reply to parse
 * @return Subscription structure containing channel and count information
 * @throws ProtoError if the reply structure doesn't match expected format
 */
subscription
parse(ParseTag<subscription>, redisReply &reply) {
    if (reply.elements != 3) {
        throw ProtoError("Expect 3 sub replies");
    }

    assert(reply.element != nullptr);

    auto *channel_reply = reply.element[1];
    if (channel_reply == nullptr) {
        throw ProtoError("Null channel reply");
    }
    auto channel = reply::parse<std::optional<std::string>>(*channel_reply);

    auto *num_reply = reply.element[2];
    if (num_reply == nullptr) {
        throw ProtoError("Null num reply");
    }
    auto num = reply::parse<long long>(*num_reply);

    return {std::move(channel), num};
}

/**
 * @brief Parses a SET command reply into a boolean
 * @param reply The Redis reply to parse
 * @return true if the SET command succeeded, false otherwise
 */
bool
parse_set_reply(redisReply &reply) {
    if (is_nil(reply)) {
        // Failed to set, and make it a FALSE reply.
        return false;
    }

    // Check if it's a "OK" status reply.
    reply::parse<void>(reply);

    // Make it a TRUE reply.
    return true;
}

/**
 * @brief Parses a Redis reply into a set structure
 * @param tag Parse tag type (unused, for template specialization)
 * @param reply The Redis reply to parse
 * @return Set structure containing the success status
 */
set
parse(ParseTag<set>, redisReply &reply) {
    return {parse_set_reply(reply)};
}

    inline std::vector<char>
    parse(ParseTag<std::vector<char>>, redisReply &reply) {
        if (!is_string(reply)) {
            throw ParseError("STRING", reply);
        }

        if (reply.len == 0 || reply.str == nullptr) {
            return {};
        }

        return std::vector<char>(reply.str, reply.str + reply.len);
    }

    inline std::chrono::milliseconds
    parse(ParseTag<std::chrono::milliseconds>, redisReply &reply) {
        if (!is_integer(reply)) {
            throw ParseError("INTEGER", reply);
        }

        return std::chrono::milliseconds(reply.integer);
    }

    inline std::chrono::seconds
    parse(ParseTag<std::chrono::seconds>, redisReply &reply) {
        if (!is_integer(reply)) {
            throw ParseError("INTEGER", reply);
        }

        return std::chrono::seconds(reply.integer);
    }

    inline geo_pos
    parse(ParseTag<geo_pos>, redisReply &reply) {
        if (!is_array(reply)) {
            throw ParseError("ARRAY", reply);
        }

        if (reply.elements != 2 || reply.element == nullptr) {
            throw ProtoError("Invalid GEO position reply");
        }

        auto *longitude_reply = reply.element[0];
        auto *latitude_reply = reply.element[1];

        if (longitude_reply == nullptr || latitude_reply == nullptr) {
            throw ProtoError("Null longitude or latitude reply");
        }

        return geo_pos{
                parse<double>(*longitude_reply),
                parse<double>(*latitude_reply)
        };
    }

    inline stream_id
    parse(ParseTag<stream_id>, redisReply &reply) {
        if (!is_string(reply)) {
            throw ParseError("STRING", reply);
        }

        if (reply.len == 0 || reply.str == nullptr) {
            return {};
        }

        std::string id_str(reply.str, reply.len);
        auto pos = id_str.find('-');

        if (pos == std::string::npos) {
            throw ProtoError("Invalid stream ID format: " + id_str);
        }

        stream_id id;
        try {
            id.timestamp = std::stoll(id_str.substr(0, pos));
            id.sequence = std::stoll(id_str.substr(pos + 1));
        } catch (const std::exception &) {
            throw ProtoError("Invalid stream ID: " + id_str);
        }

        return id;
    }

/**
 * @brief Parses a Redis reply into a stream_entry
 * @param tag Type tag for stream_entry
 * @param reply Redis reply to parse
 * @return Parsed stream_entry
 */
    inline stream_entry
    parse(ParseTag<stream_entry>, redisReply &reply) {
        if (!is_array(reply)) {
            throw ParseError("ARRAY", reply);
        }

        if (reply.elements != 2 || reply.element == nullptr) {
            throw ProtoError("Invalid stream entry reply");
        }

        auto *id_reply = reply.element[0];
        auto *fields_reply = reply.element[1];

        if (id_reply == nullptr || fields_reply == nullptr) {
            throw ProtoError("Null ID or fields reply");
        }

        stream_entry entry;
        entry.id = parse<stream_id>(*id_reply);
        entry.fields = parse<std::unordered_map<std::string, std::string>>(*fields_reply);

        return entry;
    }

namespace detail {

/**
 * @brief Checks if a Redis array reply is a flat array
 * 
 * A flat array is an array whose elements are not arrays themselves.
 * This is used for determining how to parse key-value pairs from Redis.
 * 
 * @param reply The Redis reply to check
 * @return true if the reply is a flat array, false otherwise
 */
bool
is_flat_array(redisReply &reply) {
#ifdef REDIS_PLUS_PLUS_RESP_VERSION_3
    assert(reply::is_array(reply) || reply::is_map(reply) || reply::is_set(reply));
#else
    assert(reply::is_array(reply));
#endif

    // Empty array reply.
    if (reply.element == nullptr || reply.elements == 0) {
        return false;
    }

    auto *sub_reply = reply.element[0];

    // Null element.
    if (sub_reply == nullptr) {
        return false;
    }

    return !reply::is_array(*sub_reply);
}

} // namespace detail

} // namespace reply

} // namespace qb::redis