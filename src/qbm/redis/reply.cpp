/**
 * @file qbm/redis/reply.cpp
 * @brief Out-of-line definitions for Redis reply parsing.
 *
 * Houses the non-template reply parsers (pub/sub messages, stream entries,
 * geo/score/cluster/memory/JSON conversions, ...) declared in reply.h, plus
 * the RESP type-name helper and the ReplyParseError diagnostic builder.
 *
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2026 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)
 * @ingroup Redis
 */
#include "reply.h"
#include <charconv>
#include <sstream>
#include <stdexcept>
#include <qb/system/parse.h>
#include "types.h"

namespace qb::redis {

ReplyErrorType
error_type(std::string_view const error) noexcept {
    const auto word = error.substr(0, error.find(' '));
    if (word == "MOVED")
        return ReplyErrorType::MOVED;
    if (word == "ASK")
        return ReplyErrorType::ASK;
    return ReplyErrorType::ERR;
}

std::optional<redirection>
parse_redirection(std::string_view const error) {
    const auto kind = error_type(error);
    if (kind == ReplyErrorType::ERR)
        return std::nullopt;
    // "<KIND> <slot> <host>:<port>" -- exactly three words; the host may be empty, `?`, or an unbracketed IPv6.
    const auto rest     = error.substr(error.find(' ') + 1);
    const auto gap      = rest.find(' ');
    const auto endpoint = gap == std::string_view::npos ? std::string_view{} : rest.substr(gap + 1);
    const auto colon    = endpoint.rfind(':');
    if (gap == std::string_view::npos || colon == std::string_view::npos || endpoint.find(' ') != std::string_view::npos)
        return std::nullopt;
    const auto slot = qb::to_number<std::uint16_t>(rest.substr(0, gap));
    const auto port = qb::to_number<std::uint16_t>(endpoint.substr(colon + 1));
    if (!slot || *slot > 16383 || !port || *port == 0)
        return std::nullopt;
    // Redis writes an IPv6 host unbracketed, split here at its last colon; a bracketed one is read inside its
    // brackets. A host made of colons only is an IPv6 address that lost its port (`::1`), not a host.
    auto host = endpoint.substr(0, colon);
    if (host.size() >= 2 && host.front() == '[' && host.back() == ']')
        host = host.substr(1, host.size() - 2);
    if (host.find_first_of("[]") != std::string_view::npos || (!host.empty() && host.find_first_not_of(':') == std::string_view::npos))
        return std::nullopt;
    return redirection{kind, *slot, std::string{host}, *port};
}

std::string
ReplyParseError::_err_info(const std::string &type, const ReplyValue &reply) {
    return "expect " + type + " reply, but got " + reply::type_to_string(reply) + " reply";
}

namespace reply {

const ReplyValue &
unwrap_attributes(const ReplyValue &reply) noexcept {
    const auto *value = &reply;
    while (value->is_attribute()) {
        const auto &attribute = value->as_attribute();
        if (!attribute.value)
            break;
        value = attribute.value.get();
    }
    return *value;
}

namespace {

// The value behind a decoration that reached an attribute-aware parser directly: nullptr when `reply` is not
// decorated, or when the decoration carries no value -- the parser then applies its own mismatch policy to the
// decoration, as before.
const ReplyValue *
decorated_value(const ReplyValue &reply) noexcept {
    if (!reply.is_attribute())
        return nullptr;
    const auto &value = unwrap_attributes(reply);
    return value.is_attribute() ? nullptr : &value;
}

// An aware parser re-enters itself on the value behind the decoration through this helper, never directly, so its
// own body is not recursive (a shape g++ 14 does not inline).
template <typename T>
[[nodiscard]] QB_NOINLINE QB_COLD T
reparse(const ReplyValue &value) {
    return parse(ParseTag<T>{}, value);
}

} // namespace

std::string_view
parse_string_view_mismatch(const ReplyValue &reply) {
    if (const auto *value = decorated_value(reply))
        return parse(ParseTag<std::string_view>{}, *value);
    throw ReplyParseError("STRING or STATUS", reply);
}

std::string
parse_string_mismatch(const ReplyValue &reply) {
    if (is_integer(reply))
        return std::to_string(reply.as_integer().value);
    // Commands like GEORADIUS with WITHDIST/WITHCOORD return nested arrays
    // [name, distance, ...] per element. Extract the first string sub-element.
    if (is_array(reply) && !reply.as_array().empty() && reply.as_array()[0]->is_string())
        return std::string(reply.as_array()[0]->as_string_view());
    if (const auto *value = decorated_value(reply))
        return parse(ParseTag<std::string>{}, *value);
    throw ReplyParseError("STRING or STATUS", reply);
}

long long
parse_integer_mismatch(const ReplyValue &reply) {
    if (const auto *value = decorated_value(reply))
        return parse(ParseTag<long long>{}, *value);
    throw ReplyParseError("INTEGER", reply);
}

double
parse_double_mismatch(const ReplyValue &reply) {
    if (const auto *value = decorated_value(reply))
        return parse(ParseTag<double>{}, *value);
    throw ProtoError("not a double reply");
}

// ============================================================================
// Type to string conversion
// ============================================================================

std::string
type_to_string(const parser::Value &value) {
    using namespace parser;
    if (value.is_simple_string())
        return "SIMPLE_STRING";
    if (value.is_simple_error())
        return "SIMPLE_ERROR";
    if (value.is_bulk_string())
        return "BULK_STRING";
    if (value.is_error())
        return "BULK_ERROR";
    if (value.is_integer())
        return "INTEGER";
    if (value.is_double())
        return "DOUBLE";
    if (value.is_boolean())
        return "BOOLEAN";
    if (value.is_null())
        return "NULL";
    if (value.is_array())
        return "ARRAY";
    if (value.is_map())
        return "MAP";
    if (value.is_set())
        return "SET";
    if (value.is_push())
        return "PUSH";
    if (value.is_attribute())
        return "ATTRIBUTE";
    if (value.is_string())
        return "VERBATIM_STRING";
    if (value.is_big_number())
        return "BIG_NUMBER";
    return "UNKNOWN";
}

// ============================================================================
// Complex type parsers
// ============================================================================

qb::redis::message
parse(ParseTag<qb::redis::message>, const ReplyValue &reply) {
    if (!is_array_or_push(reply) || get_pubsub_size(reply) < 3) {
        throw ProtoError("Expect array or push with 3 elements for message");
    }
    auto const *e1 = get_pubsub_element(reply, 1);
    auto const *e2 = get_pubsub_element(reply, 2);
    if (!e1 || !e2)
        throw ProtoError("Invalid message format");

    return qb::redis::message{
        "",                      // pattern (not applicable for MESSAGE)
        parse<std::string>(*e1), // channel
        parse<std::string>(*e2), // payload
        nullptr
    };
}

qb::redis::pmessage
parse(ParseTag<qb::redis::pmessage>, const ReplyValue &reply) {
    if (!is_array_or_push(reply) || get_pubsub_size(reply) < 4) {
        throw ProtoError("Expect array or push with 4 elements for pmessage");
    }
    auto const *e1 = get_pubsub_element(reply, 1);
    auto const *e2 = get_pubsub_element(reply, 2);
    auto const *e3 = get_pubsub_element(reply, 3);
    if (!e1 || !e2 || !e3)
        throw ProtoError("Invalid pmessage format");

    return qb::redis::pmessage{
        parse<std::string>(*e1), // pattern
        parse<std::string>(*e2), // channel
        parse<std::string>(*e3), // payload
        nullptr
    };
}

qb::redis::subscription
parse(ParseTag<qb::redis::subscription>, const ReplyValue &reply) {
    if (!is_array_or_push(reply) || get_pubsub_size(reply) < 3) {
        throw ProtoError("Expect array or push with 3 elements for subscription");
    }
    auto const *e1 = get_pubsub_element(reply, 1);
    auto const *e2 = get_pubsub_element(reply, 2);
    if (!e1 || !e2)
        throw ProtoError("Invalid subscription format");

    auto channel = parse<std::optional<std::string>>(*e1);
    auto num     = parse<long long>(*e2);
    return qb::redis::subscription{std::move(channel), num};
}

std::vector<char>
parse(ParseTag<std::vector<char>>, const ReplyValue &reply) {
    if (!is_string(reply)) [[unlikely]] {
        if (const auto *value = decorated_value(reply))
            return reparse<std::vector<char>>(*value);
        throw ReplyParseError("STRING", reply);
    }

    auto sv = reply.as_string_view();
    if (sv.empty()) {
        return {};
    }

    return std::vector<char>(sv.begin(), sv.end());
}

std::chrono::milliseconds
parse(ParseTag<std::chrono::milliseconds>, const ReplyValue &reply) {
    if (!is_integer(reply)) {
        throw ReplyParseError("INTEGER", reply);
    }
    return std::chrono::milliseconds(reply.as_integer().value);
}

std::chrono::seconds
parse(ParseTag<std::chrono::seconds>, const ReplyValue &reply) {
    if (!is_integer(reply)) {
        throw ReplyParseError("INTEGER", reply);
    }
    return std::chrono::seconds(reply.as_integer().value);
}

qb::redis::geo_pos
parse(ParseTag<qb::redis::geo_pos>, const ReplyValue &reply) {
    if (!is_array(reply) || reply.as_array().size() < 2) {
        throw ReplyParseError("ARRAY with 2 elements", reply);
    }

    return qb::redis::geo_pos{parse<double>(*reply.as_array()[0]), parse<double>(*reply.as_array()[1])};
}

qb::redis::stream_id
parse(ParseTag<qb::redis::stream_id>, const ReplyValue &reply) {
    if (!is_string(reply)) {
        throw ReplyParseError("STRING", reply);
    }

    auto sv = reply.as_string_view();
    if (sv.empty()) {
        return {};
    }

    auto pos = sv.find('-');
    if (pos == std::string_view::npos) {
        throw ProtoError("Invalid stream ID format");
    }

    qb::redis::stream_id id;
    auto                 ts  = qb::to_number<std::uint64_t>(sv.substr(0, pos));
    auto                 seq = qb::to_number<std::uint64_t>(sv.substr(pos + 1));
    if (!ts || !seq) {
        throw ProtoError("Invalid stream ID values");
    }
    id = qb::redis::stream_id::from_unsigned(*ts, *seq);
    // The public signed fields retain the unsigned components' bit patterns.

    return id;
}

qb::redis::stream_entry
parse(ParseTag<qb::redis::stream_entry>, const ReplyValue &reply) {
    if (!is_array(reply) || reply.as_array().size() < 2) [[unlikely]] {
        if (const auto *value = decorated_value(reply))
            return reparse<qb::redis::stream_entry>(*value);
        throw ProtoError("Invalid stream entry format");
    }

    qb::redis::stream_entry entry;
    entry.id     = parse<qb::redis::stream_id>(*reply.as_array()[0]);
    entry.fields = parse<qb::unordered_map<std::string, std::string>>(*reply.as_array()[1]);

    return entry;
}

stream_entry_list
parse(ParseTag<stream_entry_list>, const ReplyValue &reply) {
    if (!is_array(reply)) [[unlikely]] {
        if (const auto *value = decorated_value(reply))
            return reparse<stream_entry_list>(*value);
        throw ReplyParseError("ARRAY", reply);
    }

    stream_entry_list result;
    result.reserve(reply.as_array().size());

    for (const auto &elem : reply.as_array()) {
        result.push_back(parse<qb::redis::stream_entry>(*elem));
    }

    return result;
}

map_stream_entry_list
parse(ParseTag<map_stream_entry_list>, const ReplyValue &reply) {
    if (!is_array(reply)) [[unlikely]] {
        if (const auto *value = decorated_value(reply))
            return reparse<map_stream_entry_list>(*value);
        throw ReplyParseError("ARRAY", reply);
    }

    map_stream_entry_list result;

    for (const auto &elem : reply.as_array()) {
        if (!elem || !is_array(*elem) || elem->as_array().size() < 2) {
            throw ProtoError("Invalid stream map entry");
        }

        auto key     = parse<std::string>(*elem->as_array()[0]);
        auto entries = parse<stream_entry_list>(*elem->as_array()[1]);

        result.emplace(std::move(key), std::move(entries));
    }

    return result;
}

qb::redis::score
parse(ParseTag<qb::redis::score>, const ReplyValue &reply) {
    if (is_double(reply)) {
        return qb::redis::score{reply.as_double().value};
    }
    if (is_integer(reply)) {
        return qb::redis::score{static_cast<double>(reply.as_integer().value)};
    }
    if (is_string(reply) || is_status(reply)) {
        return qb::redis::score{parse<double>(reply)};
    }
    throw ReplyParseError("DOUBLE or INTEGER or STRING", reply);
}

qb::redis::score_member
parse(ParseTag<qb::redis::score_member>, const ReplyValue &reply) {
    if (!is_array(reply) || reply.as_array().size() < 2) [[unlikely]] {
        if (const auto *value = decorated_value(reply))
            return reparse<qb::redis::score_member>(*value);
        throw ProtoError("Invalid score-member format");
    }

    qb::redis::score_member sm;
    sm.member = parse<std::string>(*reply.as_array()[0]);
    sm.score  = parse<double>(*reply.as_array()[1]);

    return sm;
}

std::vector<qb::redis::score_member>
parse(ParseTag<std::vector<qb::redis::score_member>>, const ReplyValue &reply) {
    if (!is_array(reply)) [[unlikely]] {
        if (const auto *value = decorated_value(reply))
            return reparse<std::vector<qb::redis::score_member>>(*value);
        throw ReplyParseError("ARRAY", reply);
    }

    const auto                          &arr = reply.as_array();
    std::vector<qb::redis::score_member> result;

    // RESP3: array of [member, score] pairs: [[m1,s1], [m2,s2], ...]
    if (!arr.empty() && arr[0] && is_array(*arr[0]) && arr[0]->as_array().size() >= 2) {
        result.reserve(arr.size());
        for (const auto &elem : arr) {
            if (!elem)
                throw ProtoError("Null element in score-member array");
            result.push_back(parse<qb::redis::score_member>(*elem));
        }
        return result;
    }

    // RESP2: flat array [m1, s1, m2, s2, ...]
    if (arr.size() % 2 != 0) {
        throw ProtoError("Score-member array must have even number of elements");
    }
    result.reserve(arr.size() / 2);
    for (size_t i = 0; i < arr.size(); i += 2) {
        result.push_back(qb::redis::score_member{parse<double>(*arr[i + 1]), parse<std::string>(*arr[i])});
    }
    return result;
}

qb::redis::search_result
parse(ParseTag<qb::redis::search_result>, const ReplyValue &reply) {
    if (!is_array(reply)) {
        throw ReplyParseError("ARRAY", reply);
    }

    qb::redis::search_result result;

    if (reply.as_array().empty()) {
        return result;
    }

    // First element is the key
    result.key = parse<std::string>(*reply.as_array()[0]);

    // Remaining elements are field-value pairs
    for (size_t i = 1; i + 1 < reply.as_array().size(); i += 2) {
        result.fields.push_back(parse<std::string>(*reply.as_array()[i]));
        result.values.push_back(parse<std::string>(*reply.as_array()[i + 1]));
    }

    return result;
}

qb::redis::cluster_node
parse(ParseTag<qb::redis::cluster_node>, const ReplyValue &reply) {
    if (!is_string(reply)) {
        throw ReplyParseError("STRING", reply);
    }

    qb::redis::cluster_node node;
    std::istringstream      iss(std::string(reply.as_string_view()));
    std::string             token;

    // Node ID
    if (!(iss >> node.id)) {
        throw ProtoError("Failed to parse node ID");
    }

    // IP:port@cport
    if (!(iss >> token)) {
        throw ProtoError("Failed to parse node address");
    }

    size_t colon_pos = token.find(':');
    if (colon_pos == std::string::npos) {
        throw ProtoError("Invalid address format");
    }

    node.ip = token.substr(0, colon_pos);

    size_t      at_pos   = token.find('@', colon_pos);
    std::string port_str = (at_pos != std::string::npos) ? token.substr(colon_pos + 1, at_pos - colon_pos - 1) : token.substr(colon_pos + 1);

    auto port = qb::to_number<int>(port_str);
    if (!port) {
        throw ProtoError("Invalid port");
    }
    node.port = *port;

    // Flags
    if (!(iss >> token)) {
        throw ProtoError("Failed to parse flags");
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

    // Master, ping-sent, pong-recv, epoch, link-state
    if (!(iss >> node.master))
        throw ProtoError("Failed to parse master");
    if (!(iss >> node.ping_sent))
        throw ProtoError("Failed to parse ping-sent");
    if (!(iss >> node.pong_received))
        throw ProtoError("Failed to parse pong-recv");
    if (!(iss >> node.epoch))
        throw ProtoError("Failed to parse epoch");
    if (!(iss >> node.link_state))
        throw ProtoError("Failed to parse link-state");

    // Slots
    while (iss >> token) {
        node.slots.push_back(token);
    }

    return node;
}

qb::redis::memory_info
parse(ParseTag<qb::redis::memory_info>, const ReplyValue &reply) {
    if (!is_array(reply)) {
        throw ReplyParseError("ARRAY", reply);
    }

    qb::redis::memory_info                      info;
    qb::unordered_map<std::string, std::string> info_map;

    // Parse key-value pairs
    for (size_t i = 0; i + 1 < reply.as_array().size(); i += 2) {
        auto key      = parse<std::string>(*reply.as_array()[i]);
        auto val      = parse<std::string>(*reply.as_array()[i + 1]);
        info_map[key] = val;
    }

    // Extract fields
    auto get_size_t = [&info_map](const std::string &key) -> size_t {
        auto it = info_map.find(key);
        if (it == info_map.end())
            return 0;
        return static_cast<size_t>(qb::to_number_prefix<unsigned long long>(it->second).value_or(0));
    };

    info.used_memory                  = get_size_t("used_memory");
    info.used_memory_peak             = get_size_t("used_memory_peak");
    info.used_memory_lua              = get_size_t("used_memory_lua");
    info.used_memory_scripts          = get_size_t("used_memory_scripts");
    info.number_of_keys               = get_size_t("db0");
    info.number_of_expires            = get_size_t("expired_keys");
    info.number_of_connected_clients  = get_size_t("connected_clients");
    info.number_of_slaves             = get_size_t("connected_slaves");
    info.number_of_replicas           = get_size_t("connected_slaves");
    info.number_of_commands_processed = get_size_t("total_commands_processed");
    info.total_connections_received   = get_size_t("total_connections_received");
    info.total_commands_processed     = get_size_t("total_commands_processed");
    info.instantaneous_ops_per_sec    = get_size_t("instantaneous_ops_per_sec");
    info.total_net_input_bytes        = get_size_t("total_net_input_bytes");
    info.total_net_output_bytes       = get_size_t("total_net_output_bytes");
    info.instantaneous_input_kbps     = get_size_t("instantaneous_input_kbps");
    info.instantaneous_output_kbps    = get_size_t("instantaneous_output_kbps");

    return info;
}

qb::redis::pipeline_result
parse(ParseTag<qb::redis::pipeline_result>, const ReplyValue &reply) {
    if (!is_array(reply)) {
        throw ReplyParseError("ARRAY", reply);
    }

    qb::redis::pipeline_result result;
    result.size = reply.as_array().size();

    for (const auto &elem : reply.as_array()) {
        if (elem && elem->is_error()) {
            result.all_succeeded = false;
            ++result.error_count;
        }
    }

    // Individual replies are accessible through Reply<pipeline_result>.raw()
    // since Value is move-only and cannot be cloned out of the EXEC array.
    return result;
}

qb::redis::json_value
parse(ParseTag<qb::redis::json_value>, const ReplyValue &reply) {
    using Type = qb::redis::json_value::Type;

    // Error replies must propagate as CommandError so callers can inspect the message.
    if (is_error(reply)) {
        throw CommandError(reply.get_error_message());
    }

    if (is_nil(reply)) {
        return qb::redis::json_value{Type::Null, nullptr};
    }

    if (is_integer(reply)) {
        return qb::redis::json_value{Type::Number, static_cast<double>(reply.as_integer().value)};
    }

    if (is_double(reply)) {
        return qb::redis::json_value{Type::Number, reply.as_double().value};
    }

    if (is_bool(reply)) {
        return qb::redis::json_value{Type::Boolean, reply.as_boolean().value};
    }

    if (is_string(reply) || is_status(reply)) {
        return qb::redis::json_value{Type::String, std::string(reply.as_string_view())};
    }

    if (is_bignum(reply)) {
        return qb::redis::json_value{Type::String, reply.as_big_number().value};
    }

    if (is_array(reply)) {
        std::vector<qb::redis::json_value> arr;
        arr.reserve(reply.as_array().size());
        for (const auto &elem : reply.as_array()) {
            arr.push_back(parse<qb::redis::json_value>(*elem));
        }
        return qb::redis::json_value{Type::Array, std::move(arr)};
    }

    if (is_set(reply)) {
        std::vector<qb::redis::json_value> arr;
        arr.reserve(reply.as_set().size());
        for (const auto &elem : reply.as_set()) {
            arr.push_back(parse<qb::redis::json_value>(*elem));
        }
        return qb::redis::json_value{Type::Array, std::move(arr)};
    }

    if (is_push(reply)) {
        std::vector<qb::redis::json_value> arr;
        arr.reserve(reply.as_push().size());
        for (const auto &elem : reply.as_push().elements) {
            arr.push_back(parse<qb::redis::json_value>(*elem));
        }
        return qb::redis::json_value{Type::Array, std::move(arr)};
    }

    if (is_map(reply)) {
        qb::unordered_map<std::string, qb::redis::json_value> obj;
        for (const auto &entry : reply.as_map()) {
            auto key = parse<std::string>(*entry.first);
            obj[key] = parse<qb::redis::json_value>(*entry.second);
        }
        return qb::redis::json_value{Type::Object, std::move(obj)};
    }

    if (std::holds_alternative<parser::Attribute>(reply)) {
        const auto &attr = reply.as_attribute();
        if (attr.value) {
            return parse<qb::redis::json_value>(*attr.value);
        }
        return qb::redis::json_value{Type::Null, nullptr};
    }

    throw ProtoError("Unsupported type for JSON conversion");
}

qb::json
parse(ParseTag<qb::json>, const ReplyValue &reply) {
    // Error replies must propagate as CommandError so callers receive the actual message.
    if (is_error(reply)) {
        throw CommandError(reply.get_error_message());
    }

    if (is_nil(reply)) {
        return qb::json(nullptr);
    }

    if (is_integer(reply)) {
        return qb::json(reply.as_integer().value);
    }

    if (is_double(reply)) {
        return qb::json(reply.as_double().value);
    }

    if (is_bool(reply)) {
        return qb::json(reply.as_boolean().value);
    }

    if (is_bignum(reply)) {
        return qb::json(reply.as_big_number().value);
    }

    if (is_string(reply) || is_status(reply)) {
        auto sv = reply.as_string_view();

        // Try JSON parsing for structural content (e.g. RESP bulk string carrying JSON)
        if (sv.size() > 1 && ((sv.front() == '{' && sv.back() == '}') || (sv.front() == '[' && sv.back() == ']'))) {
            try {
                return qb::json::parse(sv);
            } catch (...) {
                // Not valid JSON - fall through to plain string
            }
        }

        return qb::json(std::string(sv));
    }

    if (is_array(reply)) {
        const auto &elems = reply.as_array();
        if (elems.empty()) {
            return qb::json::array();
        }

        // Heuristic: a flat even-sized array whose odd-indexed (key) elements are all
        // strings AND whose even-indexed (value) elements contain at least one non-string
        // is almost certainly a RESP2 flat-map reply (e.g. MEMORY STATS, XINFO STREAM).
        // Convert it to a JSON object for ergonomic access via operator[].
        // Arrays whose first key element is NOT a string (e.g. SLOWLOG GET, FUNCTION LIST,
        // COMMAND INFO) are left as-is.
        if (elems.size() % 2 == 0 && elems[0]->is_string()) {
            bool all_keys_strings     = true;
            bool has_non_string_value = false;
            for (size_t i = 0; i < elems.size(); i += 2) {
                if (!elems[i]->is_string()) {
                    all_keys_strings = false;
                    break;
                }
                if (i + 1 < elems.size() && !elems[i + 1]->is_string()) {
                    has_non_string_value = true;
                }
            }
            if (all_keys_strings && has_non_string_value) {
                qb::json obj = qb::json::object();
                for (size_t i = 0; i + 1 < elems.size(); i += 2) {
                    obj[parse<std::string>(*elems[i])] = parse<qb::json>(*elems[i + 1]);
                }
                return obj;
            }
        }

        qb::json arr = qb::json::array();
        for (const auto &elem : elems) {
            arr.push_back(parse<qb::json>(*elem));
        }
        return arr;
    }

    if (is_set(reply)) {
        qb::json arr = qb::json::array();
        for (const auto &elem : reply.as_set()) {
            arr.push_back(parse<qb::json>(*elem));
        }
        return arr;
    }

    if (is_push(reply)) {
        qb::json arr = qb::json::array();
        for (const auto &elem : reply.as_push().elements) {
            arr.push_back(parse<qb::json>(*elem));
        }
        return arr;
    }

    if (is_map(reply)) {
        qb::json obj = qb::json::object();
        for (const auto &entry : reply.as_map()) {
            auto key = parse<std::string>(*entry.first);
            obj[key] = parse<qb::json>(*entry.second);
        }
        return obj;
    }

    if (std::holds_alternative<parser::Attribute>(reply)) {
        const auto &attr = reply.as_attribute();
        if (attr.value) {
            return parse<qb::json>(*attr.value);
        }
        return qb::json(nullptr);
    }

    throw ProtoError("Unsupported type for JSON conversion");
}

namespace {

const ReplyValue &
command_reply_value(const ReplyValue &reply) {
    const auto *value = &reply;
    while (std::holds_alternative<parser::Attribute>(*value)) {
        const auto &attribute = value->as_attribute();
        if (!attribute.value)
            throw ProtoError("Missing command reply behind attribute");
        value = attribute.value.get();
    }
    if (value->is_error())
        throw CommandError(std::string(value->get_error_message()));
    return *value;
}

qb::json
role_value_to_json(const ReplyValue &reply) {
    const auto &value = command_reply_value(reply);
    if (is_array(value)) {
        qb::json result = qb::json::array();
        for (const auto &element : value.as_array())
            result.push_back(role_value_to_json(*element));
        return result;
    }
    // ROLE names are strings even when their bytes look like embedded JSON.
    if (is_string(value) || is_status(value))
        return qb::json(std::string(value.as_string_view()));
    return parse<qb::json>(value);
}

} // namespace

role_json_reply
parse(ParseTag<role_json_reply>, const ReplyValue &reply) {
    const auto &value = command_reply_value(reply);
    if (!is_array(value))
        throw ReplyParseError("ARRAY", value);
    return {role_value_to_json(value)};
}

command_stats_json_reply
parse(ParseTag<command_stats_json_reply>, const ReplyValue &reply) {
    auto     text   = parse<std::string_view>(command_reply_value(reply));
    qb::json result = qb::json::object();
    while (!text.empty()) {
        const auto newline = text.find('\n');
        auto       line    = text.substr(0, newline);
        if (newline == std::string_view::npos)
            text = {};
        else
            text.remove_prefix(newline + 1);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        if (line.empty() || line.front() == '#')
            continue;
        const auto separator = line.find(':');
        if (separator == std::string_view::npos || separator == 0)
            throw ProtoError("Invalid command statistics field");
        result[std::string(line.substr(0, separator))] = std::string(line.substr(separator + 1));
    }
    return {std::move(result)};
}

// ============================================================================
// COMMAND INFO (Huly QB-653)
// ============================================================================
//
// Each entry is a positional array -- name, arity, flags, first key, last key, step, then ACL
// categories (Redis 6.0), tips, key specifications and subcommands (7.0) -- or a nil for a name
// the server does not know. The positions become named fields with their native types: integers
// stay integers, strings stay strings (a flag or a tip is never read as JSON), a RESP3 set is an
// array, and a key specification -- a map in RESP3, the flat [key, value, ...] array in RESP2 --
// is an object in both. Positions a later server may append are left to raw().

namespace {

qb::json
info_value_json(const ReplyValue &reply) {
    const auto &value = command_reply_value(reply);
    if (is_nil(value))
        return nullptr;
    if (is_string(value) || is_status(value))
        return std::string(value.as_string_view());
    if (is_array(value) || is_set(value)) {
        qb::json list = qb::json::array();
        if (is_array(value)) {
            for (const auto &element : value.as_array())
                list.push_back(info_value_json(*element));
        } else {
            for (const auto &element : value.as_set())
                list.push_back(info_value_json(*element));
        }
        return list;
    }
    if (is_map(value)) {
        qb::json object = qb::json::object();
        for (const auto &entry : value.as_map())
            object[parse<std::string>(*entry.first)] = info_value_json(*entry.second);
        return object;
    }
    return parse<qb::json>(value); // integer, double, boolean, big number
}

// A list of entries -- a RESP array, or a RESP3 set -- each decoded by `element`.
template <typename Element>
qb::json
info_list_json(const ReplyValue &reply, Element &&element) {
    const auto &value = command_reply_value(reply);
    qb::json    list  = qb::json::array();
    if (is_array(value)) {
        for (const auto &item : value.as_array())
            list.push_back(element(*item));
    } else if (is_set(value)) {
        for (const auto &item : value.as_set())
            list.push_back(element(*item));
    } else {
        throw ReplyParseError("ARRAY or SET", value);
    }
    return list;
}

// A map -- RESP3 sends a map, RESP2 the flat [key, value, ...] array -- each value decoded by `field`.
template <typename Field>
qb::json
info_map_json(const ReplyValue &reply, Field &&field) {
    const auto &value  = command_reply_value(reply);
    qb::json    object = qb::json::object();
    if (is_map(value)) {
        for (const auto &entry : value.as_map()) {
            auto key    = parse<std::string>(*entry.first);
            object[key] = field(key, *entry.second);
        }
    } else if (is_array(value) && value.as_array().size() % 2 == 0) {
        const auto &flat = value.as_array();
        for (std::size_t i = 0; i < flat.size(); i += 2) {
            auto key    = parse<std::string>(*flat[i]);
            object[key] = field(key, *flat[i + 1]);
        }
    } else {
        throw ReplyParseError("MAP", value);
    }
    return object;
}

qb::json
info_field_json(const std::string &, const ReplyValue &value) {
    return info_value_json(value);
}

// begin_search / find_keys: {type, spec}, the spec a map of its own.
qb::json
key_search_json(const std::string &, const ReplyValue &value) {
    return info_map_json(value, [](const std::string &key, const ReplyValue &field) {
        return key == "spec" ? info_map_json(field, info_field_json) : info_value_json(field);
    });
}

qb::json
key_specification_json(const ReplyValue &value) {
    return info_map_json(value, [](const std::string &key, const ReplyValue &field) {
        return key == "begin_search" || key == "find_keys" ? key_search_json(key, field) : info_value_json(field);
    });
}

qb::json
command_doc_json(const ReplyValue &reply) {
    const auto &value = command_reply_value(reply);
    if (is_nil(value))
        return nullptr; // a name the server does not know
    if (!is_array(value))
        throw ReplyParseError("ARRAY", value);
    const auto &fields = value.as_array();
    if (fields.size() < 6)
        throw ProtoError("COMMAND INFO entry has fewer than 6 fields");
    qb::json doc     = qb::json::object();
    doc["name"]      = std::string(parse<std::string_view>(command_reply_value(*fields[0])));
    doc["arity"]     = parse<long long>(command_reply_value(*fields[1]));
    doc["flags"]     = info_value_json(*fields[2]);
    doc["first_key"] = parse<long long>(command_reply_value(*fields[3]));
    doc["last_key"]  = parse<long long>(command_reply_value(*fields[4]));
    doc["step"]      = parse<long long>(command_reply_value(*fields[5]));
    if (fields.size() > 6)
        doc["acl_categories"] = info_value_json(*fields[6]);
    if (fields.size() > 7)
        doc["tips"] = info_value_json(*fields[7]);
    if (fields.size() > 8)
        doc["key_specifications"] = info_list_json(*fields[8], key_specification_json);
    if (fields.size() > 9)
        doc["subcommands"] = info_list_json(*fields[9], command_doc_json);
    return doc;
}

} // namespace

command_info_json_reply
parse(ParseTag<command_info_json_reply>, const ReplyValue &reply) {
    return {info_list_json(reply, command_doc_json)};
}

} // namespace reply

} // namespace qb::redis
