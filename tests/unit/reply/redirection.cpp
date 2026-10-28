/*
 * qb - C++ Actor Framework
 * Copyright (C) 2011-2026 isndev (cpp.actor). All rights reserved.
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

/**
 * @file redirection.cpp
 * @brief `qb::redis::error_type` and `qb::redis::parse_redirection` (Huly QB-129): what a cluster redirection says.
 *
 * Pure: no socket. The formats are those a Redis 8 cluster sends (measured on a local three-node cluster:
 * `MOVED 15495 127.0.0.1:30003`, `ASK 15495 127.0.0.1:30002`), the host forms its source allows (an empty one, `?`, an
 * unbracketed IPv6), and the malformed ones a client must not follow.
 */

#include <gtest/gtest.h>
#include <qbm/redis/redis.h>

using qb::redis::parse_redirection;
using qb::redis::ReplyErrorType;

TEST(RedisRedirection, TheErrorTypeIsTheFirstWord) {
    EXPECT_EQ(qb::redis::error_type("MOVED 3999 127.0.0.1:6381"), ReplyErrorType::MOVED);
    EXPECT_EQ(qb::redis::error_type("ASK 3999 127.0.0.1:6381"), ReplyErrorType::ASK);
    EXPECT_EQ(qb::redis::error_type("ERR unknown command"), ReplyErrorType::ERR);
    EXPECT_EQ(qb::redis::error_type("MOVEDX 1 h:1"), ReplyErrorType::ERR) << "the whole first word";
    EXPECT_EQ(qb::redis::error_type("ASKING"), ReplyErrorType::ERR);
    EXPECT_EQ(qb::redis::error_type(""), ReplyErrorType::ERR);
}

TEST(RedisRedirection, MovedAndAskAsAClusterSendsThem) {
    const auto moved = parse_redirection("MOVED 15495 127.0.0.1:30003");
    ASSERT_TRUE(moved);
    EXPECT_EQ(moved->kind, ReplyErrorType::MOVED);
    EXPECT_EQ(moved->slot, 15495);
    EXPECT_EQ(moved->host, "127.0.0.1");
    EXPECT_EQ(moved->port, 30003);
    const auto ask = parse_redirection("ASK 0 redis-2.cluster.local:6379");
    ASSERT_TRUE(ask);
    EXPECT_EQ(ask->kind, ReplyErrorType::ASK);
    EXPECT_EQ(ask->slot, 0);
    EXPECT_EQ(ask->host, "redis-2.cluster.local");
    EXPECT_EQ(ask->port, 6379);
}

TEST(RedisRedirection, TheHostFormsAreKeptAsSent) {
    const auto v6 = parse_redirection("MOVED 16383 ::1:7000");
    ASSERT_TRUE(v6);
    EXPECT_EQ(v6->slot, 16383);
    EXPECT_EQ(v6->host, "::1") << "the port is after the LAST colon";
    EXPECT_EQ(v6->port, 7000);
    const auto empty = parse_redirection("MOVED 1 :7001");
    ASSERT_TRUE(empty);
    EXPECT_EQ(empty->host, "") << "the answering node's host is meant";
    const auto unknown = parse_redirection("ASK 1 ?:7002");
    ASSERT_TRUE(unknown);
    EXPECT_EQ(unknown->host, "?") << "unknown endpoint: parsed, and not followed by the view";
}

TEST(RedisRedirection, AMalformedOneIsNoRedirection) {
    for (const char *bad :
         {"MOVED", "MOVED 3999", "MOVED 3999 127.0.0.1", "MOVED 16384 127.0.0.1:1", "MOVED -1 127.0.0.1:1", "MOVED x 127.0.0.1:1",
          "MOVED 1 127.0.0.1:0", "MOVED 1 127.0.0.1:65536", "MOVED 1 127.0.0.1:port", "MOVED 1 127.0.0.1:1 extra", "MOVED 1 h:1 x:2",
          "ERR 1 h:1", "MOVED  1 h:1"})
        EXPECT_FALSE(parse_redirection(bad)) << bad;
}
