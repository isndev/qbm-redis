/*
 * qb - C++ Actor Framework
 * Copyright (C) 2011-2026 isndev (cpp.actor). All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * See the License for the specific terms.
 */

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <unistd.h>
#include <gtest/gtest.h>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <qbm/redis/redis.h>

namespace redis_unix_socket_test {

using namespace std::chrono_literals;

class UnixSocketConnection : public ::testing::TestWithParam<int> {
protected:
    std::string                             path;
    std::unique_ptr<qb::redis::tcp::client> client;

    void
    SetUp() override {
        const auto *configured = std::getenv("REDIS_UNIX_SOCKET");
        if (!configured || !*configured)
            GTEST_SKIP() << "QBM_INTEGRATION_SKIP_DAEMON_UNREACHABLE: REDIS_UNIX_SOCKET is unset";
        path = configured;
        ASSERT_TRUE(std::filesystem::path(path).is_absolute());
        ASSERT_TRUE(std::filesystem::is_socket(path)) << "Configured Redis Unix socket is absent: " << path;
        qb::io::async::init();
        client = std::make_unique<qb::redis::tcp::client>(qb::io::uri{"unix://" + path});
        client->set_command_timeout(3s);
        ASSERT_TRUE(qb::io::async::run_sync(client->connect(3s))) << path;
        if (GetParam() == 3) {
            const auto reply = qb::io::async::run_sync(client->hello(3));
            ASSERT_TRUE(reply) << reply.error();
        }
    }
};

TEST_P(UnixSocketConnection, ExchangesCommandsAndBinaryValues) {
    const auto pong = qb::io::async::run_sync(client->ping());
    ASSERT_TRUE(pong) << pong.error();
    EXPECT_EQ(pong.result(), "PONG");

    const std::string key = "qb:unix-socket:" + std::to_string(getpid()) + ":" + std::to_string(GetParam());
    const std::string value{"value\0over-uds", 14};
    const auto        stored = qb::io::async::run_sync(client->set(key, value));
    ASSERT_TRUE(stored) << stored.error();
    const auto fetched = qb::io::async::run_sync(client->get(key));
    ASSERT_TRUE(fetched) << fetched.error();
    ASSERT_TRUE(fetched.result().has_value());
    EXPECT_EQ(*fetched.result(), value);
    const auto removed = qb::io::async::run_sync(client->del(key));
    ASSERT_TRUE(removed) << removed.error();
    EXPECT_EQ(removed.result(), 1);

    client->disconnect();
    EXPECT_FALSE(client->is_connected());
}

TEST_P(UnixSocketConnection, MissingSocketFailsWithoutAConnectedClient) {
    const auto missing_path = path + ".missing";
    ASSERT_FALSE(std::filesystem::exists(missing_path));
    qb::redis::tcp::client missing{qb::io::uri{"unix://" + missing_path}};
    EXPECT_FALSE(qb::io::async::run_sync(missing.connect(3s)));
    EXPECT_FALSE(missing.is_connected());
}

INSTANTIATE_TEST_SUITE_P(Resp2AndResp3, UnixSocketConnection, ::testing::Values(2, 3));

} // namespace redis_unix_socket_test
