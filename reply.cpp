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

std::string
ParseError::_err_info(const std::string &expect_type, const redisReply &reply) {
    return "expect " + expect_type + " reply, but got " + reply::type_to_string(reply.type) + " reply";
}

namespace reply {

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

std::string
parse(ParseTag<std::string>, redisReply &reply) {
    auto str = parse(ParseTag<std::string_view>{}, reply);
    return {str.data(), str.size()};
}

long long
parse(ParseTag<long long>, redisReply &reply) {
    if (!reply::is_integer(reply)) {
        throw ParseError("INTEGER", reply);
    }

    return reply.integer;
}

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

set
parse(ParseTag<set>, redisReply &reply) {
    return {parse_set_reply(reply)};
}

namespace detail {

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