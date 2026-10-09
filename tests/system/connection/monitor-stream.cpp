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

/** @file monitor-stream.cpp
 * @brief MONITOR owns a persistent stream after +OK, ending once on disconnect (QB-651).
 * Real RESP frames cross the loopback socket and the parser.
 */

#include <gtest/gtest.h>
#include <chrono>
#include <stdexcept>
#include <string>
#include <vector>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <qb/io/tcp/listener.h>
#include <qb/io/tcp/socket.h>
#include <qbm/redis/redis.h>

using namespace std::chrono_literals;

namespace {

template <typename Pred>
bool
run_until(Pred &&pred, std::chrono::milliseconds timeout = 1000ms) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!pred() && std::chrono::steady_clock::now() < deadline)
        qb::io::async::run(EVRUN_NOWAIT);
    return pred();
}

} // namespace

TEST(RedisMonitorStream, LinesContinueAfterAdmissionAndEndOnceOnDisconnect) {
    qb::io::async::init();
    qb::io::tcp::listener listener;
    ASSERT_EQ(listener.listen_v4(0, "127.0.0.1"), qb::io::SocketStatus::Done);
    qb::redis::tcp::client client{qb::io::uri{"tcp://127.0.0.1:" + std::to_string(listener.local_endpoint().port())}};
    ASSERT_TRUE(qb::io::async::run_sync(client.connect()));
    qb::io::tcp::socket peer;
    ASSERT_EQ(listener.accept(peer), qb::io::SocketStatus::Done);

    std::vector<std::string> lines;
    int                      terminal = 0;
    client.monitor([&](qb::redis::Reply<std::string> &&r) {
        if (r.ok()) {
            lines.push_back(r.result());
            if (lines.size() == 2)
                throw std::runtime_error("callback failed on first line");
        } else
            ++terminal;
    });
    const std::string frames = "+OK\r\n+123 [0 peer] \"SET\" \"a\" \"1\"\r\n+124 [0 peer] \"GET\" \"a\"\r\n";
    ASSERT_EQ(peer.write(frames.data(), frames.size()), static_cast<int>(frames.size()));
    ASSERT_TRUE(run_until([&] { return lines.size() >= 3; })) << "MONITOR lost the stream after +OK";
    EXPECT_EQ(lines, (std::vector<std::string>{"OK", "123 [0 peer] \"SET\" \"a\" \"1\"", "124 [0 peer] \"GET\" \"a\""}));
    EXPECT_EQ(client.pending_reply_count(), 0u);

    client.disconnect();
    EXPECT_EQ(terminal, 1) << "a stream has one terminal callback when its connection ends";
}

TEST(RedisMonitorStream, FifoAdmissionResetAndOrdinaryRepliesRemainSeparate) {
    qb::io::async::init();
    qb::io::tcp::listener listener;
    ASSERT_EQ(listener.listen_v4(0, "127.0.0.1"), qb::io::SocketStatus::Done);
    qb::redis::tcp::client client{qb::io::uri{"tcp://127.0.0.1:" + std::to_string(listener.local_endpoint().port())}};
    ASSERT_TRUE(qb::io::async::run_sync(client.connect()));
    qb::io::tcp::socket peer;
    ASSERT_EQ(listener.accept(peer), qb::io::SocketStatus::Done);

    std::string ping_result;
    client.ping([&](qb::redis::Reply<std::string> &&r) { ping_result = r.value_or(""); });
    std::vector<std::string> lines;
    int                      terminal = 0;
    client.monitor([&](qb::redis::Reply<std::string> &&r) {
        if (r) {
            lines.push_back(r.result());
            if (r.result().find("\"SET\"") != std::string::npos)
                throw std::runtime_error("line callback failed before RESET");
        } else
            ++terminal;
    });
    const std::string admission = "+PONG\r\n+OK\r\n+125 [0 peer] \"SET\" \"a\" \"2\"\r\n";
    ASSERT_EQ(peer.write(admission.data(), admission.size()), static_cast<int>(admission.size()));
    ASSERT_TRUE(run_until([&] { return lines.size() == 2; }));
    EXPECT_EQ(ping_result, "PONG");
    EXPECT_EQ(lines, (std::vector<std::string>{"OK", "125 [0 peer] \"SET\" \"a\" \"2\""}));
    EXPECT_EQ(client.pending_reply_count(), 0u);

    int rejected = 0;
    client.get(
        [&](auto &&r) {
            ++rejected;
            EXPECT_FALSE(r.ok());
        },
        "a");
    client.monitor([&](auto &&r) {
        ++rejected;
        EXPECT_FALSE(r.ok());
    });
    EXPECT_EQ(rejected, 2);
    EXPECT_EQ(client.pending_reply_count(), 0u);

    std::string reset_result;
    client.reset([&](qb::redis::Reply<qb::redis::status> &&r) { reset_result = r ? std::string(r.result()) : r.error(); });
    EXPECT_EQ(client.pending_reply_count(), 1u);
    const std::string stopped = "+126 [0 peer] \"GET\" \"a\"\r\n+RESET\r\n";
    ASSERT_EQ(peer.write(stopped.data(), stopped.size()), static_cast<int>(stopped.size()));
    ASSERT_TRUE(run_until([&] { return !reset_result.empty(); }));
    EXPECT_EQ(lines.size(), 3u);
    EXPECT_EQ(reset_result, "RESET");
    EXPECT_EQ(terminal, 1);
    EXPECT_EQ(client.pending_reply_count(), 0u);

    client.ping([&](qb::redis::Reply<std::string> &&r) { ping_result = r.value_or(""); });
    const std::string pong = "+PONG\r\n";
    ASSERT_EQ(peer.write(pong.data(), pong.size()), static_cast<int>(pong.size()));
    ASSERT_TRUE(run_until([&] { return client.pending_reply_count() == 0; }));
    EXPECT_EQ(ping_result, "PONG");
    EXPECT_EQ(terminal, 1);
    client.disconnect();
}

TEST(RedisMonitorStream, AdmissionFailureAndQuitDoNotLeakCallbacks) {
    qb::io::async::init();
    qb::io::tcp::listener listener;
    ASSERT_EQ(listener.listen_v4(0, "127.0.0.1"), qb::io::SocketStatus::Done);
    qb::redis::tcp::client client{qb::io::uri{"tcp://127.0.0.1:" + std::to_string(listener.local_endpoint().port())}};
    ASSERT_TRUE(qb::io::async::run_sync(client.connect()));
    qb::io::tcp::socket peer;
    ASSERT_EQ(listener.accept(peer), qb::io::SocketStatus::Done);

    int admission_errors = 0;
    client.monitor([&](qb::redis::Reply<std::string> &&r) {
        EXPECT_FALSE(r.ok());
        ++admission_errors;
    });
    const std::string denied = "-NOPERM MONITOR denied\r\n";
    ASSERT_EQ(peer.write(denied.data(), denied.size()), static_cast<int>(denied.size()));
    ASSERT_TRUE(run_until([&] { return admission_errors == 1; }));
    EXPECT_EQ(client.pending_reply_count(), 0u);

    int stream_end = 0;
    client.monitor([&](qb::redis::Reply<std::string> &&r) {
        if (!r)
            ++stream_end;
    });
    const std::string admitted = "+OK\r\n";
    ASSERT_EQ(peer.write(admitted.data(), admitted.size()), static_cast<int>(admitted.size()));
    ASSERT_TRUE(run_until([&] { return client.pending_reply_count() == 0; }));

    std::string quit_result;
    client.quit([&](qb::redis::Reply<qb::redis::status> &&r) { quit_result = r ? std::string(r.result()) : r.error(); });
    const std::string quit_reply = "+OK\r\n";
    ASSERT_EQ(peer.write(quit_reply.data(), quit_reply.size()), static_cast<int>(quit_reply.size()));
    ASSERT_TRUE(run_until([&] { return !quit_result.empty(); }));
    EXPECT_EQ(quit_result, "OK");
    EXPECT_EQ(stream_end, 1);
    client.disconnect();
    EXPECT_EQ(admission_errors, 1);
    EXPECT_EQ(stream_end, 1);
}

TEST(RedisMonitorStream, DisconnectBeforeAdmissionDoesNotReplayOnReconnect) {
    qb::io::async::init();
    qb::io::tcp::listener listener;
    ASSERT_EQ(listener.listen_v4(0, "127.0.0.1"), qb::io::SocketStatus::Done);
    qb::redis::tcp::client client{qb::io::uri{"tcp://127.0.0.1:" + std::to_string(listener.local_endpoint().port())}};
    ASSERT_TRUE(qb::io::async::run_sync(client.connect()));
    qb::io::tcp::socket first;
    ASSERT_EQ(listener.accept(first), qb::io::SocketStatus::Done);

    int old_terminal = 0;
    client.monitor([&](qb::redis::Reply<std::string> &&r) {
        EXPECT_FALSE(r.ok());
        ++old_terminal;
    });
    EXPECT_EQ(client.pending_reply_count(), 1u);
    client.disconnect();
    EXPECT_EQ(old_terminal, 1);
    EXPECT_EQ(client.pending_reply_count(), 0u);

    ASSERT_TRUE(qb::io::async::run_sync(client.connect()));
    qb::io::tcp::socket second;
    ASSERT_EQ(listener.accept(second), qb::io::SocketStatus::Done);
    std::string ping_result;
    client.ping([&](qb::redis::Reply<std::string> &&r) { ping_result = r.value_or(""); });
    const std::string pong = "+PONG\r\n";
    ASSERT_EQ(second.write(pong.data(), pong.size()), static_cast<int>(pong.size()));
    ASSERT_TRUE(run_until([&] { return !ping_result.empty(); }));
    EXPECT_EQ(ping_result, "PONG");
    EXPECT_EQ(old_terminal, 1);

    int new_lines    = 0;
    int new_terminal = 0;
    client.monitor([&](qb::redis::Reply<std::string> &&r) {
        if (r)
            ++new_lines;
        else
            ++new_terminal;
    });
    const std::string frames = "+OK\r\n+123 [0 peer] \"GET\" \"new\"\r\n";
    ASSERT_EQ(second.write(frames.data(), frames.size()), static_cast<int>(frames.size()));
    ASSERT_TRUE(run_until([&] { return new_lines == 2; }));
    second.disconnect();
    ASSERT_TRUE(run_until([&] { return new_terminal == 1; }));
    EXPECT_EQ(old_terminal, 1);
    EXPECT_EQ(client.pending_reply_count(), 0u);
}

TEST(RedisMonitorStream, FailedResetKeepsStreamUntilDisconnect) {
    qb::io::async::init();
    qb::io::tcp::listener listener;
    ASSERT_EQ(listener.listen_v4(0, "127.0.0.1"), qb::io::SocketStatus::Done);
    qb::redis::tcp::client client{qb::io::uri{"tcp://127.0.0.1:" + std::to_string(listener.local_endpoint().port())}};
    ASSERT_TRUE(qb::io::async::run_sync(client.connect()));
    qb::io::tcp::socket peer;
    ASSERT_EQ(listener.accept(peer), qb::io::SocketStatus::Done);

    std::vector<std::string> lines;
    int                      terminal = 0;
    client.monitor([&](qb::redis::Reply<std::string> &&r) {
        if (r)
            lines.push_back(r.result());
        else
            ++terminal;
    });
    const std::string admission = "+OK\r\n";
    ASSERT_EQ(peer.write(admission.data(), admission.size()), static_cast<int>(admission.size()));
    ASSERT_TRUE(run_until([&] { return lines.size() == 1; }));

    int reset_errors = 0;
    client.reset([&](qb::redis::Reply<qb::redis::status> &&r) {
        if (!r)
            ++reset_errors;
    });
    const std::string reply = "-NOPERM RESET denied\r\n+127 [0 peer] \"SET\" \"x\" \"1\"\r\n";
    ASSERT_EQ(peer.write(reply.data(), reply.size()), static_cast<int>(reply.size()));
    ASSERT_TRUE(run_until([&] { return reset_errors == 1 && lines.size() == 2; }));
    EXPECT_EQ(terminal, 0);
    EXPECT_EQ(client.pending_reply_count(), 0u);
    client.disconnect();
    EXPECT_EQ(terminal, 1);
}

TEST(RedisMonitorStream, DisconnectInsideLineCallbackEndsCurrentSession) {
    qb::io::async::init();
    qb::io::tcp::listener listener;
    ASSERT_EQ(listener.listen_v4(0, "127.0.0.1"), qb::io::SocketStatus::Done);
    qb::redis::tcp::client client{qb::io::uri{"tcp://127.0.0.1:" + std::to_string(listener.local_endpoint().port())}};
    ASSERT_TRUE(qb::io::async::run_sync(client.connect()));
    qb::io::tcp::socket peer;
    ASSERT_EQ(listener.accept(peer), qb::io::SocketStatus::Done);

    std::vector<std::string> lines;
    int                      terminal = 0;
    client.monitor([&](qb::redis::Reply<std::string> &&r) {
        if (!r) {
            ++terminal;
            return;
        }
        lines.push_back(r.result());
        if (r.result() == "stop")
            client.disconnect();
    });
    const std::string frames = "+OK\r\n+stop\r\n+stale\r\n";
    ASSERT_EQ(peer.write(frames.data(), frames.size()), static_cast<int>(frames.size()));
    ASSERT_TRUE(run_until([&] { return terminal == 1; }));
    EXPECT_EQ(lines, (std::vector<std::string>{"OK", "stop"}));
    EXPECT_EQ(client.pending_reply_count(), 0u);
}
