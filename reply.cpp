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

#include <stdexcept>
#include <sstream>
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
 * @return The status structure containing the status string
 * @throws ParseError if the reply is not a status reply
 * @throws ProtoError if the status string is null
 */
status
to_status(redisReply &reply) {
    if (!qb::redis::is_status(reply)) {
        throw ParseError("STATUS", reply);
    }

    if (reply.str == nullptr) {
        throw ProtoError("A null status reply");
    }

    // Create status from string representation
    return status(std::string(reply.str, reply.len));
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
    if (!qb::redis::is_string(reply) && !qb::redis::is_status(reply) && !qb::redis::is_verb(reply) && !qb::redis::is_bignum(reply)) {
        throw ParseError("STRING or STATUS or VERB or BIGNUM", reply);
    }
#else
    if (qb::redis::is_array(reply)) // pong in consumer
        return "PONG";
    if (!qb::redis::is_string(reply) && !qb::redis::is_status(reply)) {
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
    if (is_integer(reply))
        return std::to_string(reply.integer);

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
    if (!qb::redis::is_integer(reply)) {
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
    if (qb::redis::is_double(reply)) {
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
    if (is_nil(reply))
        return false;
#ifdef REDIS_PLUS_PLUS_RESP_VERSION_3
    long long ret = 0;
    if (qb::redis::is_bool(reply) || qb::redis::is_integer(reply)) {
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
    auto channel = qb::redis::parse<std::string_view>(*channel_reply);

    auto *msg_reply = reply.element[2];
    if (msg_reply == nullptr) {
        throw ProtoError("Null message reply");
    }
    auto message = qb::redis::parse<std::string_view>(*msg_reply);

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
    auto pattern = qb::redis::parse<std::string_view>(*pattern_reply);

    auto *channel_reply = reply.element[2];
    if (channel_reply == nullptr) {
        throw ProtoError("Null channel reply");
    }
    auto channel = qb::redis::parse<std::string_view>(*channel_reply);

    auto *msg_reply = reply.element[3];
    if (msg_reply == nullptr) {
        throw ProtoError("Null message reply");
    }
    auto message = qb::redis::parse<std::string_view>(*msg_reply);

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
    auto channel = qb::redis::parse<std::optional<std::string>>(*channel_reply);

    auto *num_reply = reply.element[2];
    if (num_reply == nullptr) {
        throw ProtoError("Null num reply");
    }
    auto num = qb::redis::parse<long long>(*num_reply);

    return {std::move(channel), num};
}

/**
 * @brief Parses a Redis reply into a status structure
 * @param tag Parse tag type (unused, for template specialization)
 * @param reply The Redis reply to parse
 * @return Status structure containing the status string
 */
status
parse(ParseTag<status>, redisReply &reply) {
    if (!qb::redis::is_status(reply)) {
        throw ParseError("STATUS", reply);
    }

    if (reply.str == nullptr) {
        throw ProtoError("A null status reply");
    }

    // Create status from string representation
    return status(std::string(reply.str, reply.len));
}

    inline std::vector<char>
    parse(ParseTag<std::vector<char>>, redisReply &reply) {
        if (!qb::redis::is_string(reply)) {
            throw ParseError("STRING", reply);
        }

        if (reply.len == 0 || reply.str == nullptr) {
            return {};
        }

        return std::vector<char>(reply.str, reply.str + reply.len);
    }

    inline std::chrono::milliseconds
    parse(ParseTag<std::chrono::milliseconds>, redisReply &reply) {
        if (!qb::redis::is_integer(reply)) {
            throw ParseError("INTEGER", reply);
        }

        return std::chrono::milliseconds(reply.integer);
    }

    inline std::chrono::seconds
    parse(ParseTag<std::chrono::seconds>, redisReply &reply) {
        if (!qb::redis::is_integer(reply)) {
            throw ParseError("INTEGER", reply);
        }

        return std::chrono::seconds(reply.integer);
    }

/**
 * @brief Parses a Redis reply into a geo_pos
 * @param tag Parse tag type (unused, for template specialization)
 * @param reply The Redis reply to parse
 * @return GeoPos value containing longitude and latitude
 * @throws ParseError if the reply is not an array with 2 elements
 */
    qb::redis::geo_pos
    parse(ParseTag<qb::redis::geo_pos>, redisReply &reply) {
        if (!qb::redis::is_array(reply)) {
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

        return qb::redis::geo_pos{
                parse<double>(*longitude_reply),
                parse<double>(*latitude_reply)
        };
    }

    qb::redis::stream_id
    parse(ParseTag<qb::redis::stream_id>, redisReply &reply) {
        if (!qb::redis::is_string(reply)) {
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

        qb::redis::stream_id id;
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
    qb::redis::stream_entry
    parse(ParseTag<qb::redis::stream_entry>, redisReply &reply) {
        if (!qb::redis::is_array(reply)) {
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

        qb::redis::stream_entry entry;
        entry.id = parse<qb::redis::stream_id>(*id_reply);
        entry.fields = parse<qb::unordered_map<std::string, std::string>>(*fields_reply);

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
    assert(qb::redis::is_array(reply) || qb::redis::is_map(reply) || qb::redis::is_set(reply));
#else
    assert(qb::redis::is_array(reply));
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

    return !qb::redis::is_array(*sub_reply);
}

} // namespace detail

/**
 * @brief Parses a Redis reply into a score
 * @param tag Parse tag type (unused, for template specialization)
 * @param reply The Redis reply to parse
 * @return Score value of the reply
 * @throws ParseError if the reply is not a double or integer reply
 */
qb::redis::score
parse(ParseTag<qb::redis::score>, redisReply &reply) {
#ifdef REDIS_PLUS_PLUS_RESP_VERSION_3
    if (!qb::redis::is_double(reply) && !qb::redis::is_integer(reply) && !qb::redis::is_string(reply)) {
        throw ParseError("DOUBLE or INTEGER or STRING", reply);
    }
#else
    if (!qb::redis::is_integer(reply) && !qb::redis::is_string(reply)) {
        throw ParseError("INTEGER or STRING", reply);
    }
#endif

    return qb::redis::score{parse<double>(reply)};
}

/**
 * @brief Parses a Redis reply into a score_member
 * @param tag Parse tag type (unused, for template specialization)
 * @param reply The Redis reply to parse
 * @return ScoreMember value of the reply
 * @throws ParseError if the reply is not an array with 2 elements
 */
qb::redis::score_member
parse(ParseTag<qb::redis::score_member>, redisReply &reply) {
    if (!qb::redis::is_array(reply)) {
        throw ParseError("ARRAY", reply);
    }

    if (reply.elements != 2 || reply.element == nullptr) {
        throw ProtoError("Invalid score-member reply, expect array with 2 elements");
    }

    auto *member_reply = reply.element[0];
    auto *score_reply = reply.element[1];

    if (score_reply == nullptr || member_reply == nullptr) {
        throw ProtoError("Null score or member reply");
    }

    qb::redis::score_member sm;
    sm.score = parse<double>(*score_reply);
    sm.member = parse<std::string>(*member_reply);

    return sm;
}

/**
 * @brief Parses a Redis reply into a search_result
 * @param tag Parse tag type (unused, for template specialization)
 * @param reply The Redis reply to parse
 * @return SearchResult value of the reply
 * @throws ParseError if the reply is not an array
 */
qb::redis::search_result
parse(ParseTag<qb::redis::search_result>, redisReply &reply) {
    if (!qb::redis::is_array(reply)) {
        throw ParseError("ARRAY", reply);
    }

    qb::redis::search_result result;

    if (reply.elements == 0 || reply.element == nullptr) {
        return result;
    }

    // First element is the key
    auto *key_reply = reply.element[0];
    if (key_reply == nullptr) {
        throw ProtoError("Null key reply in search result");
    }
    result.key = parse<std::string>(*key_reply);

    // Remaining elements are field-value pairs
    for (size_t i = 1; i < reply.elements; i += 2) {
        if (i + 1 >= reply.elements) {
            break; // Avoid out of bounds if we have an odd number of elements
        }

        auto *field_reply = reply.element[i];
        auto *value_reply = reply.element[i + 1];

        if (field_reply == nullptr || value_reply == nullptr) {
            throw ProtoError("Null field or value reply in search result");
        }

        result.fields.push_back(parse<std::string>(*field_reply));
        result.values.push_back(parse<std::string>(*value_reply));
    }

    return result;
}

/**
 * @brief Parses a Redis reply into a cluster_node
 * @param tag Parse tag type (unused, for template specialization)
 * @param reply The Redis reply to parse
 * @return ClusterNode value of the reply
 * @throws ParseError if the reply format is invalid
 */
qb::redis::cluster_node
parse(ParseTag<qb::redis::cluster_node>, redisReply &reply) {
    if (!qb::redis::is_string(reply)) {
        throw ParseError("STRING", reply);
    }

    qb::redis::cluster_node node;
    std::string node_info = parse<std::string>(reply);
    
    // Parse node info string
    // Format: <id> <ip:port@cport> <flags> <master> <ping-sent> <pong-recv> <epoch> <link-state> <slot> <slot> ... <slot>
    std::istringstream iss(node_info);
    std::string token;
    
    // Node ID
    if (!(iss >> node.id)) {
        throw ProtoError("Failed to parse node ID from: " + node_info);
    }
    
    // IP:port@cport
    if (!(iss >> token)) {
        throw ProtoError("Failed to parse node address from: " + node_info);
    }
    
    size_t colon_pos = token.find(':');
    size_t at_pos = token.find('@');
    
    if (colon_pos == std::string::npos) {
        throw ProtoError("Invalid address format (missing colon): " + token);
    }
    
    node.ip = token.substr(0, colon_pos);
    
    std::string port_str;
    if (at_pos != std::string::npos) {
        port_str = token.substr(colon_pos + 1, at_pos - colon_pos - 1);
    } else {
        port_str = token.substr(colon_pos + 1);
    }
    
    try {
        node.port = std::stoi(port_str);
    } catch (const std::exception &) {
        throw ProtoError("Invalid port: " + port_str);
    }
    
    // Flags
    if (!(iss >> token)) {
        throw ProtoError("Failed to parse node flags from: " + node_info);
    }
    
    size_t start = 0;
    size_t comma_pos;
    do {
        comma_pos = token.find(',', start);
        if (comma_pos == std::string::npos) {
            node.flags.push_back(token.substr(start));
            break;
        }
        
        node.flags.push_back(token.substr(start, comma_pos - start));
        start = comma_pos + 1;
    } while (true);
    
    // Master
    if (!(iss >> node.master)) {
        throw ProtoError("Failed to parse node master from: " + node_info);
    }
    
    // Ping sent
    if (!(iss >> node.ping_sent)) {
        throw ProtoError("Failed to parse ping sent from: " + node_info);
    }
    
    // Pong received
    if (!(iss >> node.pong_received)) {
        throw ProtoError("Failed to parse pong received from: " + node_info);
    }
    
    // Epoch
    if (!(iss >> node.epoch)) {
        throw ProtoError("Failed to parse epoch from: " + node_info);
    }
    
    // Link state
    if (!(iss >> node.link_state)) {
        throw ProtoError("Failed to parse link state from: " + node_info);
    }
    
    // Slots
    while (iss >> token) {
        node.slots.push_back(token);
    }
    
    return node;
}

/**
 * @brief Parses a Redis reply into a memory_info
 * @param tag Parse tag type (unused, for template specialization)
 * @param reply The Redis reply to parse
 * @return MemoryInfo value of the reply
 * @throws ParseError if the reply is not an array or the format is invalid
 */
qb::redis::memory_info
parse(ParseTag<qb::redis::memory_info>, redisReply &reply) {
    if (!qb::redis::is_array(reply)) {
        throw ParseError("ARRAY", reply);
    }
    
    qb::redis::memory_info info;
    
    // INFO reply is typically an array of string pairs
    if (reply.elements == 0 || reply.element == nullptr) {
        return info;
    }
    
    qb::unordered_map<std::string, std::string> info_map;
    
    // Parse all key-value pairs
    for (size_t i = 0; i < reply.elements; i += 2) {
        if (i + 1 >= reply.elements) {
            break;
        }
        
        auto *key_reply = reply.element[i];
        auto *val_reply = reply.element[i + 1];
        
        if (key_reply == nullptr || val_reply == nullptr) {
            continue;
        }
        
        std::string key = parse<std::string>(*key_reply);
        std::string val = parse<std::string>(*val_reply);
        
        info_map[key] = val;
    }
    
    // Extract memory info from the map
    auto get_size_t = [&info_map](const std::string& key) -> size_t {
        auto it = info_map.find(key);
        if (it == info_map.end()) return 0;
        
        try {
            return std::stoull(it->second);
        } catch (const std::exception&) {
            return 0;
        }
    };
    
    info.used_memory = get_size_t("used_memory");
    info.used_memory_peak = get_size_t("used_memory_peak");
    info.used_memory_lua = get_size_t("used_memory_lua");
    info.used_memory_scripts = get_size_t("used_memory_scripts");
    info.number_of_keys = get_size_t("db0");  // This is a simplification
    info.number_of_expires = get_size_t("expired_keys");
    info.number_of_connected_clients = get_size_t("connected_clients");
    info.number_of_slaves = get_size_t("connected_slaves");
    info.number_of_replicas = get_size_t("connected_slaves");  // Same as slaves
    info.number_of_commands_processed = get_size_t("total_commands_processed");
    info.total_connections_received = get_size_t("total_connections_received");
    info.total_commands_processed = get_size_t("total_commands_processed");
    info.instantaneous_ops_per_sec = get_size_t("instantaneous_ops_per_sec");
    info.total_net_input_bytes = get_size_t("total_net_input_bytes");
    info.total_net_output_bytes = get_size_t("total_net_output_bytes");
    info.instantaneous_input_kbps = get_size_t("instantaneous_input_kbps");
    info.instantaneous_output_kbps = get_size_t("instantaneous_output_kbps");
    
    return info;
}

/**
 * @brief Parses a Redis reply into a pipeline_result
 * @param tag Parse tag type (unused, for template specialization)
 * @param reply The Redis reply to parse
 * @return PipelineResult value of the reply
 * @throws ParseError if the reply is not an array
 */
qb::redis::pipeline_result
parse(ParseTag<qb::redis::pipeline_result>, redisReply &reply) {
    if (!qb::redis::is_array(reply)) {
        throw ParseError("ARRAY", reply);
    }
    
    qb::redis::pipeline_result result;
    
    if (reply.elements == 0 || reply.element == nullptr) {
        return result;
    }
    
    result.replies.reserve(reply.elements);
    
    for (size_t i = 0; i < reply.elements; ++i) {
        auto *sub_reply = reply.element[i];
        if (sub_reply == nullptr) {
            result.all_succeeded = false;
            continue;
        }
        
        if (qb::redis::is_error(*sub_reply)) {
            result.all_succeeded = false;
        }
        
        result.replies.push_back(reply_ptr(sub_reply));
    }
    
    return result;
}

/**
 * @brief Parses a Redis reply into a json_value
 * @param tag Parse tag type (unused, for template specialization)
 * @param reply The Redis reply to parse
 * @return JsonValue value of the reply
 */
qb::redis::json_value
parse(ParseTag<qb::redis::json_value>, redisReply &reply) {
    qb::redis::json_value value;
    
    if (qb::redis::is_nil(reply)) {
        value.type = qb::redis::json_value::Type::Null;
        value.data = nullptr;
    }
    else if (qb::redis::is_integer(reply) 
#ifdef REDIS_PLUS_PLUS_RESP_VERSION_3
             || qb::redis::is_double(reply)
#endif
    ) {
        value.type = qb::redis::json_value::Type::Number;
        value.data = parse<double>(reply);
    }
    else if (qb::redis::is_string(reply) || qb::redis::is_status(reply)) {
        std::string str = parse<std::string>(reply);
        
        // Check if the string is a boolean
        if (str == "true") {
            value.type = qb::redis::json_value::Type::Boolean;
            value.data = true;
        } 
        else if (str == "false") {
            value.type = qb::redis::json_value::Type::Boolean;
            value.data = false;
        }
        // Check if the string is null
        else if (str == "null") {
            value.type = qb::redis::json_value::Type::Null;
            value.data = nullptr;
        }
        else {
            value.type = qb::redis::json_value::Type::String;
            value.data = std::move(str);
        }
    }
    else if (qb::redis::is_array(reply)) {
        // If array has 0 or odd number of elements, treat as array
        // If array has even number of elements and each odd index is a string, treat as object
        bool is_object = reply.elements > 0 && (reply.elements % 2 == 0);
        
        if (is_object) {
            // Check if all odd indices are strings
            for (size_t i = 0; i < reply.elements; i += 2) {
                auto *key_reply = reply.element[i];
                if (key_reply == nullptr || !qb::redis::is_string(*key_reply)) {
                    is_object = false;
                    break;
                }
            }
        }
        
        if (is_object) {
            value.type = qb::redis::json_value::Type::Object;
            qb::unordered_map<std::string, qb::redis::json_value> object;
            
            for (size_t i = 0; i < reply.elements; i += 2) {
                auto *key_reply = reply.element[i];
                auto *val_reply = reply.element[i + 1];
                
                if (key_reply == nullptr || val_reply == nullptr) {
                    continue;
                }
                
                std::string key = parse<std::string>(*key_reply);
                qb::redis::json_value val = parse<qb::redis::json_value>(*val_reply);
                
                object[key] = std::move(val);
            }
            
            value.data = std::move(object);
        } 
        else {
            value.type = qb::redis::json_value::Type::Array;
            std::vector<qb::redis::json_value> array;
            array.reserve(reply.elements);
            
            for (size_t i = 0; i < reply.elements; ++i) {
                auto *element_reply = reply.element[i];
                if (element_reply == nullptr) {
                    array.push_back(qb::redis::json_value{}); // null
                } else {
                    array.push_back(parse<qb::redis::json_value>(*element_reply));
                }
            }
            
            value.data = std::move(array);
        }
    }
#ifdef REDIS_PLUS_PLUS_RESP_VERSION_3
    else if (qb::redis::is_bool(reply)) {
        value.type = qb::redis::json_value::Type::Boolean;
        value.data = reply.integer != 0;
    }
    else if (qb::redis::is_map(reply)) {
        value.type = qb::redis::json_value::Type::Object;
        qb::unordered_map<std::string, qb::redis::json_value> object;
        
        for (size_t i = 0; i < reply.elements; i += 2) {
            auto *key_reply = reply.element[i];
            auto *val_reply = reply.element[i + 1];
            
            if (key_reply == nullptr || val_reply == nullptr) {
                continue;
            }
            
            std::string key = parse<std::string>(*key_reply);
            qb::redis::json_value val = parse<qb::redis::json_value>(*val_reply);
            
            object[key] = std::move(val);
        }
        
        value.data = std::move(object);
    }
    else if (qb::redis::is_set(reply)) {
        value.type = qb::redis::json_value::Type::Array;
        std::vector<qb::redis::json_value> array;
        array.reserve(reply.elements);
        
        for (size_t i = 0; i < reply.elements; ++i) {
            auto *element_reply = reply.element[i];
            if (element_reply == nullptr) {
                array.push_back(qb::redis::json_value{}); // null
            } else {
                array.push_back(parse<qb::redis::json_value>(*element_reply));
            }
        }
        
        value.data = std::move(array);
    }
#endif
    
    return value;
}

/**
 * @brief Parses a Redis reply into a vector of score_member
 * @param tag Parse tag type (unused, for template specialization)
 * @param reply The Redis reply to parse
 * @return Vector of score_member values
 * @throws ParseError if the reply is not an array
 */
std::vector<qb::redis::score_member>
parse(ParseTag<std::vector<qb::redis::score_member>>, redisReply &reply) {
    if (!qb::redis::is_array(reply)) {
        throw ParseError("ARRAY", reply);
    }

    if (reply.element == nullptr) {
        throw ProtoError("Null array reply");
    }

    if (reply.elements % 2) {
            throw ProtoError("Invalid array length for string-double pairs");
    }

    std::vector<qb::redis::score_member> result;
    result.reserve(reply.elements / 2);

    auto copy_reply = reply;
    for (size_t i = 0; i < reply.elements; i += 2) {
        copy_reply.elements = 2;
        copy_reply.element = reply.element + i;

        result.push_back(parse<qb::redis::score_member>(copy_reply));
    }

    return result;
}

/**
 * @brief Parses a Redis reply into a vector of string-double pairs
 * @param tag Parse tag type (unused, for template specialization)
 * @param reply The Redis reply to parse
 * @return Vector of string-double pairs
 * @throws ParseError if the reply is not an array
 */
std::vector<std::pair<std::string, double>>
parse(ParseTag<std::vector<std::pair<std::string, double>>>, redisReply &reply) {
    if (!qb::redis::is_array(reply)) {
        throw ParseError("ARRAY", reply);
    }

    if (reply.element == nullptr) {
        throw ProtoError("Null array reply");
    }

    std::vector<std::pair<std::string, double>> result;
    result.reserve(reply.elements / 2); // Each pair is represented by two elements

    for (size_t i = 0; i < reply.elements; i += 2) {
        if (i + 1 >= reply.elements) {
            throw ProtoError("Invalid array length for string-double pairs");
        }

        auto *member_reply = reply.element[i];
        auto *score_reply = reply.element[i + 1];

        if (member_reply == nullptr || score_reply == nullptr) {
            throw ProtoError("Null array element");
        }

        result.emplace_back(
            parse<std::string>(*member_reply),
            parse<double>(*score_reply)
        );
    }

    return result;
}

/**
 * @brief Parses a Redis reply into an optional vector of stream entries
 * 
 * This function handles parsing a Redis reply into an optional vector of stream entries.
 * Returns std::nullopt if the reply is nil, and a vector of stream entries otherwise.
 * 
 * @param tag Type tag for optional vector of stream entries
 * @param reply Redis reply to parse
 * @return Optional vector of stream entries or std::nullopt if nil
 */
    stream_entry_list
    parse(ParseTag<stream_entry_list>, redisReply &reply) {
        if (!qb::redis::is_array(reply)) {
            throw ParseError("ARRAY", reply);
        }

        stream_entry_list result;
        // Empty array
        if (reply.elements && reply.element) {
            result.reserve(reply.elements);

            for (size_t i = 0; i < reply.elements; ++i) {
                if (reply.element[i] == nullptr) {
                    throw ProtoError("Null stream entry in array");
                }
                result.push_back(parse<qb::redis::stream_entry>(*reply.element[i]));
            }
        }
        return result;
    }

    /**
     * @brief Parses a Redis reply into an optional unordered map of string to stream entry
     * 
     * This function handles parsing a Redis reply into an optional unordered map where
     * keys are strings and values are stream entries. Returns std::nullopt if the reply
     * is nil, and a map otherwise. This is typically used for commands like XREAD or
     * XREADGROUP that return data from multiple streams.
     * 
     * @param tag Type tag for optional unordered map of string to stream entry
     * @param reply Redis reply to parse
     * @return Optional unordered map or std::nullopt if nil
     */
    map_stream_entry_list
    parse(ParseTag<map_stream_entry_list>, redisReply &reply) {
        if (!qb::redis::is_array(reply)) {
            throw ParseError("ARRAY", reply);
        }
        // Empty array
        if (qb::redis::is_nil(reply) || !reply.elements|| !reply.element ) {
            return {};
        }

        map_stream_entry_list result;
        for (size_t i = 0; i < reply.elements; ++i) {
            auto &sub_reply= *reply.element[i];

            for (size_t j = 0; j < sub_reply.elements; j += 2) {
                if (!qb::redis::is_array(sub_reply)) {
                    throw ParseError("SUB_ARRAY", sub_reply);
                }
                if (j + 1 >= sub_reply.elements) {
                    throw ProtoError("Invalid array length for stream key-entry pairs");
                }

                auto *key_reply = sub_reply.element[j];
                auto *entry_reply = sub_reply.element[j + 1];

                if (key_reply == nullptr || entry_reply == nullptr) {
                    throw ProtoError("Null key or entry in stream reply");
                }

                auto key = parse<std::string>(*key_reply);
                auto entry = parse<stream_entry_list>(*entry_reply);

                result.emplace(std::move(key), std::move(entry));
            }
        }

        return result;
    }

} // namespace reply

} // namespace qb::redis