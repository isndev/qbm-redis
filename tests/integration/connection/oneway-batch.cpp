/**
 * @file qbm/redis/tests/integration/connection/oneway-batch.cpp
 * @brief One-way batches against the server under test, RESP2 and RESP3 (Huly QB-902).
 *
 * `redis.send_oneway(std::move(batch))` writes `CLIENT REPLY OFF`, the batch and `CLIENT REPLY ON` in one piece and
 * completes at the ON's `+OK`. Proved against a real server:
 *   - the batch runs -- a thousand INCRs and the commands around them -- and every typed command pipelined before and
 *     after it gets its own reply, once; a command of the batch the server rejects (a wrong type, a wrong arity)
 *     fails silently and desynchronizes nothing;
 *   - the witness: the same commands sent the usual way leave the same data;
 *   - a batch submitted between a MULTI and its EXEC is refused, and accepted after it;
 *   - after a lost link, the reconnected connection answers again, and a new batch runs;
 *   - RESP3 with CLIENT TRACKING on: the invalidation the batch provokes, pushed while replies are off, is not taken
 *     for a reply.
 * The client's side, against a scripted server -- a server that does not honour OFF, the timeout, held work --, is
 * `system/connection/oneway-batch.cpp`.
 *
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2026 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)
 * @ingroup Redis
 */

#include <gtest/gtest.h>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <qb/io/tcp/socket.h>
#include "shared/redis_integration_fixture.h"

using namespace std::chrono_literals;

namespace oneway_batch_integration_test {

template <typename Pred>
bool
run_until(Pred &&pred, std::chrono::milliseconds timeout = 5000ms) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!pred() && std::chrono::steady_clock::now() < deadline)
        qb::io::async::run(EVRUN_NOWAIT);
    return pred();
}

/// What the server sends one raw connection for `commands` within `wait`: the server's side, no client in between.
std::string
raw_exchange(std::string const &commands, std::chrono::milliseconds wait) {
    const qb::io::uri   uri{qb::redis::test::redis_test_uri()};
    qb::io::tcp::socket sock;
    if (sock.connect_v4(std::string{uri.host()}, uri.u_port()) != 0)
        return {};
    if (sock.write(commands.data(), commands.size()) != static_cast<int>(commands.size()))
        return {};
    sock.set_nonblocking(true);
    std::string got;
    char        buf[4096];
    for (const auto until = std::chrono::steady_clock::now() + wait; std::chrono::steady_clock::now() < until;) {
        const int n = sock.read(buf, sizeof(buf));
        if (n > 0)
            got.append(buf, static_cast<std::size_t>(n));
        else
            std::this_thread::sleep_for(5ms);
    }
    return got;
}

std::string
resp(std::initializer_list<std::string_view> words) {
    std::string out = "*" + std::to_string(words.size()) + "\r\n";
    for (auto const w : words)
        out += "$" + std::to_string(w.size()) + "\r\n" + std::string{w} + "\r\n";
    return out;
}

/// CLIENT KILL from another connection: a loss the client did not ask for, which an armed reconnection recovers from.
qb::io::async::task<bool>
kill_from_outside(long long id) {
    qb::redis::tcp::client executioner{qb::io::uri{qb::redis::test::redis_test_uri()}};
    if (!co_await executioner.connect())
        co_return false;
    auto killed = co_await executioner.client_kill("", id);
    co_return killed.ok() && killed.result() == 1;
}

struct Seen {
    std::vector<std::string> outcomes;

    auto
    oneway(std::string what) {
        return [this, what = std::move(what)](qb::redis::Reply<qb::redis::oneway_result> &&r) {
            outcomes.push_back(what + (r.ok() ? " ok:" + std::to_string(r.result().commands) : " error:" + r.error()));
        };
    }
    auto
    get(std::string what) {
        return [this, what = std::move(what)](qb::redis::Reply<std::optional<std::string>> &&r) {
            outcomes.push_back(what + (r.ok() ? " ok:" + r.result().value_or("<nil>") : " error:" + r.error()));
        };
    }
    auto
    status(std::string what) {
        return [this, what = std::move(what)](qb::redis::Reply<qb::redis::status> &&r) {
            outcomes.push_back(what + (r.ok() ? std::string{" ok"} : " error:" + r.error()));
        };
    }
    auto
    integer(std::string what) {
        return [this, what = std::move(what)](qb::redis::Reply<long long> &&r) {
            outcomes.push_back(what + (r.ok() ? " ok:" + std::to_string(r.result()) : " error:" + r.error()));
        };
    }
};

class OnewayBatchIntegration : public ProtocolModesTestBase {
protected:
    void
    SetUp() override {
        ProtocolModesTestBase::SetUp();
        if (IsSkipped())
            return;
        if (GetParam() == ProtocolMode::RESP3) {
            const auto hello = qb::io::async::run_sync(redis.hello(3));
            ASSERT_TRUE(hello.ok()) << hello.error();
        }
    }
};

/// RESP3 on its own connection: CLIENT TRACKING pushes on the connection that reads the key.
class OnewayBatchResp3 : public RedisIntegrationTest {
protected:
    void
    SetUp() override {
        RedisIntegrationTest::SetUp();
        if (IsSkipped())
            return;
        const auto hello = qb::io::async::run_sync(redis.hello(3));
        ASSERT_TRUE(hello.ok()) << hello.error();
    }
};

} // namespace oneway_batch_integration_test

using namespace oneway_batch_integration_test;

TEST_P(OnewayBatchIntegration, TheBatchRunsAndEveryReplyAroundItIsAttributedOnce) {
    const auto key = protocol_key("oneway:run");
    Seen       seen;

    redis.set(seen.status("set before"), key + ":before", "x");
    redis.get(seen.get("get before"), key + ":before");
    qb::redis::oneway_batch batch;
    for (int i = 0; i < 1000; ++i)
        ASSERT_FALSE(batch.add("INCR", key + ":n"));
    ASSERT_FALSE(batch.add("SET", key + ":s", "v"));
    ASSERT_FALSE(batch.add("INCR", key + ":s"));     // the server rejects it: not an integer
    ASSERT_FALSE(batch.add("SET", key + ":lonely")); // the server rejects it: wrong arity
    ASSERT_FALSE(batch.add("RPUSH", key + ":l", "a", "b"));
    redis.send_oneway(std::move(batch), seen.oneway("batch"));
    redis.get(seen.get("get n"), key + ":n");
    redis.get(seen.get("get s"), key + ":s");
    redis.llen(seen.integer("llen"), key + ":l");
    redis.await();

    EXPECT_EQ(seen.outcomes,
              (std::vector<std::string>{"set before ok", "get before ok:x", "batch ok:1004", "get n ok:1000", "get s ok:v", "llen ok:2"}));
    EXPECT_EQ(redis.stats().oneway, 1004u);
    EXPECT_EQ(redis.pending_reply_count(), 0u);

    // Replies are on again: a typed command right after a batch completes on its own reply.
    const auto pong = qb::io::async::run_sync(redis.ping());
    EXPECT_TRUE(pong.ok()) << pong.error();
}

TEST_P(OnewayBatchIntegration, TheSameCommandsSentTheUsualWayLeaveTheSameData) {
    const auto typed  = protocol_key("oneway:witness:typed");
    const auto oneway = protocol_key("oneway:witness:oneway");
    Seen       seen;

    qb::redis::oneway_batch batch;
    for (int i = 0; i < 50; ++i) {
        redis.incr(seen.integer("incr"), typed + ":n");
        redis.rpush(seen.integer("rpush"), typed + ":l", std::to_string(i));
        ASSERT_FALSE(batch.add("INCR", oneway + ":n"));
        ASSERT_FALSE(batch.add("RPUSH", oneway + ":l", std::to_string(i)));
    }
    redis.hset(seen.integer("hset"), typed + ":h", "f", "1");
    ASSERT_FALSE(batch.add("HSET", oneway + ":h", "f", "1"));
    redis.send_oneway(std::move(batch), seen.oneway("batch"));
    redis.await();
    ASSERT_EQ(seen.outcomes.back(), "batch ok:101");

    for (auto const *suffix : {":n"}) {
        const auto a = qb::io::async::run_sync(redis.get(typed + suffix));
        const auto b = qb::io::async::run_sync(redis.get(oneway + suffix));
        ASSERT_TRUE(a.ok() && b.ok());
        EXPECT_EQ(a.result(), b.result());
        EXPECT_EQ(b.result().value_or(""), "50");
    }
    const auto la = qb::io::async::run_sync(redis.lrange(typed + ":l", 0, -1));
    const auto lb = qb::io::async::run_sync(redis.lrange(oneway + ":l", 0, -1));
    ASSERT_TRUE(la.ok() && lb.ok());
    EXPECT_EQ(la.result(), lb.result());
    EXPECT_EQ(lb.result().size(), 50u);
    const auto ha = qb::io::async::run_sync(redis.hget(typed + ":h", "f"));
    const auto hb = qb::io::async::run_sync(redis.hget(oneway + ":h", "f"));
    ASSERT_TRUE(ha.ok() && hb.ok());
    EXPECT_EQ(ha.result(), hb.result());
}

TEST_P(OnewayBatchIntegration, ABatchBetweenAMultiAndItsExecIsRefusedAndAcceptedAfterIt) {
    const auto key = protocol_key("oneway:multi");
    Seen       seen;

    qb::redis::oneway_batch inside;
    ASSERT_FALSE(inside.add("SET", key + ":inside", "no"));
    qb::redis::oneway_batch after;
    ASSERT_FALSE(after.add("SET", key + ":after", "yes"));

    redis.multi(seen.status("multi"));
    redis.send_oneway(std::move(inside), seen.oneway("inside"));
    redis.set(seen.status("set queued"), key + ":queued", "q");
    redis.exec<qb::redis::status>([&](qb::redis::Reply<std::vector<qb::redis::status>> &&r) {
        seen.outcomes.push_back(r.ok() ? "exec ok:" + std::to_string(r.result().size()) : "exec error:" + r.error());
    });
    redis.send_oneway(std::move(after), seen.oneway("after"));
    redis.await();

    ASSERT_EQ(seen.outcomes.size(), 5u);
    EXPECT_EQ(seen.outcomes[0].rfind("inside error:a MULTI is open", 0), 0u) << seen.outcomes[0];
    EXPECT_EQ(seen.outcomes[1], "multi ok");
    EXPECT_EQ(seen.outcomes[2].rfind("set queued", 0), 0u) << "a command inside MULTI is answered QUEUED: " << seen.outcomes[2];
    EXPECT_EQ(seen.outcomes[3], "exec ok:1");
    EXPECT_EQ(seen.outcomes[4], "after ok:1");
    EXPECT_FALSE(qb::io::async::run_sync(redis.get(key + ":inside")).result().has_value()) << "the refused batch never ran";
    EXPECT_EQ(qb::io::async::run_sync(redis.get(key + ":after")).result().value_or(""), "yes");
    EXPECT_EQ(qb::io::async::run_sync(redis.get(key + ":queued")).result().value_or(""), "q");
}

TEST_P(OnewayBatchIntegration, AfterALostLinkTheReconnectedConnectionAnswersAndANewBatchRuns) {
    const auto key = protocol_key("oneway:reconnect");
    redis.with_resp3(GetParam() == ProtocolMode::RESP3); // the reconnection's handshake applies it
    redis.enable_auto_reconnect(
        qb::redis::RetryPolicy{}.with_initial_delay(50ms).with_max_delay(200ms).with_connect_timeout(2s).with_jitter(false));

    qb::redis::oneway_batch first;
    ASSERT_FALSE(first.add("SET", key + ":first", "1"));
    const auto done = qb::io::async::run_sync(redis.send_oneway(std::move(first)));
    ASSERT_TRUE(done.ok()) << done.error();

    const auto id = qb::io::async::run_sync(redis.client_id());
    ASSERT_TRUE(id.ok()) << id.error();
    const auto before = redis.stats();
    ASSERT_TRUE(qb::io::async::run_sync(kill_from_outside(id.result())));
    ASSERT_TRUE(run_until([&] { return redis.stats().reconnects == before.reconnects + 1 && redis.is_connected(); }))
        << "the client did not reconnect";

    const auto value = qb::io::async::run_sync(redis.get(key + ":first"));
    ASSERT_TRUE(value.ok()) << value.error();
    EXPECT_EQ(value.result().value_or(""), "1") << "the reconnected connection answers";
    qb::redis::oneway_batch second;
    ASSERT_FALSE(second.add("SET", key + ":second", "2"));
    const auto again = qb::io::async::run_sync(redis.send_oneway(std::move(second)));
    ASSERT_TRUE(again.ok()) << again.error();
    EXPECT_EQ(qb::io::async::run_sync(redis.get(key + ":second")).result().value_or(""), "2");
}

TEST_F(OnewayBatchResp3, AnInvalidationPushedWhileRepliesAreOffIsNotTakenForAReply) {
    const auto key = std::string{"oneway:tracking:pid"} + std::to_string(qb::redis::test::detail::process_id_for_keys());
    ASSERT_TRUE(qb::io::async::run_sync(redis.set(key, "old")).ok());
    const auto tracking = qb::io::async::run_sync(redis.client_tracking(true));
    ASSERT_TRUE(tracking.ok()) << tracking.error();
    ASSERT_EQ(qb::io::async::run_sync(redis.get(key)).result().value_or(""), "old"); // now tracked

    // The positive control, on a raw connection: this server does push the invalidation while replies are off.
    const auto raw = raw_exchange(resp({"HELLO", "3"}) + resp({"CLIENT", "TRACKING", "ON"}) + resp({"GET", key})
                                      + resp({"CLIENT", "REPLY", "OFF"}) + resp({"SET", key, "raw"}) + resp({"CLIENT", "REPLY", "ON"}),
                                  300ms);
    ASSERT_NE(raw.find("invalidate"), std::string::npos) << "no invalidation pushed under CLIENT REPLY OFF: the case below would prove "
                                                            "nothing; bytes: "
                                                         << raw;
    ASSERT_TRUE(qb::io::async::run_sync(redis.set(key, "old")).ok());
    ASSERT_EQ(qb::io::async::run_sync(redis.get(key)).result().value_or(""), "old"); // tracked again on `redis`

    Seen                    seen;
    qb::redis::oneway_batch batch;
    ASSERT_FALSE(batch.add("SET", key, "new")); // invalidates the tracked key: pushed while replies are off
    redis.send_oneway(std::move(batch), seen.oneway("batch"));
    redis.get(seen.get("get"), key);
    redis.await();
    EXPECT_EQ(seen.outcomes, (std::vector<std::string>{"batch ok:1", "get ok:new"}));
}

INSTANTIATE_PROTOCOL_MODES(OnewayBatchIntegration);
