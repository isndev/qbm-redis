/**
 * @file qbm/redis/tests/benchmark/client/client-roundtrip.bench.cpp
 * @brief Real GET command-to-reply batches over bounded, daemon-free loopback sockets.
 *
 * Measures the callback client (command serialization/admission, pending-reply FIFO,
 * transport, RESP parsing and typed dispatch) together with the event loop, loopback
 * kernel work and the in-process peer. This is not isolated client CPU cost or
 * per-command latency: one iteration completes 16 pipelined GETs per connection.
 *
 * The peer consumes and matches each complete request against an independently
 * spelled RESP fixture before making its response bytes eligible for sending.
 * Responses carry batch, connection and command identities. Exact payloads, per-slot
 * callback counts, wire byte counts, checksums and an empty reply FIFO are checked
 * outside timing after every batch. A fresh-identity preflight and terminal batch
 * run outside the measured loop as well. No Redis process or server thread is used.
 *
 * @copyright Copyright (c) 2011-2026 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *     http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software distributed
 * under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations under the License.
 * @ingroup Redis
 */

#include <benchmark/benchmark.h>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <qb/io/async.h>
#include <qb/io/system/sys__socket.h>
#include <qbm/redis/redis.h>

namespace redis_client_roundtrip_bench {

using Clock                            = std::chrono::steady_clock;
using Socket                           = qb::io::socket;
using Client                           = qb::redis::tcp::client;
constexpr std::size_t kCommands        = 16;
constexpr std::size_t kMaxConnections  = 4;
constexpr std::size_t kMaxPayloadBytes = 256;
// "$256\r\n" + payload + "\r\n": at most 264 bytes per response, 4224 per peer.
constexpr std::size_t kMaxPeerReplyBytes = kCommands * (kMaxPayloadBytes + 8);
static_assert(kMaxConnections * kMaxPeerReplyBytes <= static_cast<std::size_t>(std::numeric_limits<int>::max()));
constexpr auto kDeadline = std::chrono::seconds{2};

struct LoopScope {
    LoopScope() {
        qb::io::async::init();
    }
    ~LoopScope() {
        qb::io::async::listener::current.clear();
    }
};

std::string
hex(std::uint64_t value, std::size_t digits) {
    constexpr char alphabet[] = "0123456789abcdef";
    std::string    out(digits, '0');
    for (std::size_t i = digits; i != 0; --i) {
        out[i - 1] = alphabet[value & 15u];
        value >>= 4u;
    }
    return out;
}

std::uint64_t
checksum(std::uint64_t value, std::string_view bytes) {
    for (const char byte : bytes) {
        value ^= static_cast<unsigned char>(byte);
        value *= UINT64_C(1099511628211); // Unsigned wrap is intentional.
    }
    return value;
}

struct Expected {
    std::string key;
    std::string command;
    std::string payload;
    std::size_t response_end = 0;
};

struct Observed {
    std::size_t                calls = 0;
    bool                       ok    = false;
    std::optional<std::string> value;
};

struct Session {
    Socket                          peer;
    std::array<Expected, kCommands> expected;
    std::array<Observed, kCommands> observed;
    std::string                     command_wire;
    std::string                     response_wire;
    std::string                     received;
    std::size_t                     consumed      = 0;
    std::size_t                     commands_seen = 0;
    std::size_t                     bytes_sent    = 0;
    std::size_t                     replies_sent  = 0;
    std::size_t                     callbacks     = 0;
    bool                            connect_done  = false;
    bool                            connected     = false;
    std::unique_ptr<Client>         client;

    // Callbacks capture this stable, heap-owned Session. Dispose the client while
    // its result slots still exist, including on an early failure with replies pending.
    ~Session() {
        client.reset();
    }
};

class Harness {
    Socket                                _listener;
    std::vector<std::unique_ptr<Session>> _sessions;
    std::size_t                           _payload_bytes;
    std::string                           _error;

    void
    fail(const char *message) {
        if (_error.empty())
            _error = message;
    }

    void
    receive_commands(Session &session) {
        std::array<char, 4096> bytes;
        const int              n = session.peer.recv(bytes.data(), static_cast<int>(bytes.size()), 0);
        if (n > 0) {
            const auto count = static_cast<std::size_t>(n);
            if (count > session.command_wire.size() - session.received.size()) {
                fail("peer received excess command bytes");
                return;
            }
            session.received.append(bytes.data(), count);
        } else if (n == 0) {
            fail("client closed before the batch completed");
            return;
        } else if (!Socket::not_recv_error(Socket::get_last_errno())) {
            fail("peer recv failed");
            return;
        }

        while (session.commands_seen < kCommands) {
            const auto &command = session.expected[session.commands_seen].command;
            if (session.received.size() - session.consumed < command.size())
                break;
            if (session.received.compare(session.consumed, command.size(), command) != 0) {
                fail("peer command identity or RESP bytes differ from the fixture");
                return;
            }
            session.consumed += command.size();
            ++session.commands_seen;
        }
    }

    void
    send_responses(Session &session) {
        // Only requests completely consumed and verified above unlock response bytes.
        const auto ready = session.commands_seen == 0 ? 0 : session.expected[session.commands_seen - 1].response_end;
        if (session.bytes_sent == ready)
            return;
        const auto remaining = ready - session.bytes_sent;
        if (remaining > kMaxPeerReplyBytes) {
            fail("response write exceeds the fixture byte bound");
            return;
        }
        // The checked fixture bound above proves this narrowing for the native socket API.
        const int bytes_to_send = static_cast<int>(remaining);
        const int n             = session.peer.send(session.response_wire.data() + session.bytes_sent, bytes_to_send);
        if (n > 0) {
            session.bytes_sent += static_cast<std::size_t>(n);
            while (session.replies_sent < kCommands && session.bytes_sent >= session.expected[session.replies_sent].response_end)
                ++session.replies_sent;
        } else if (n == 0 || !Socket::not_send_error(Socket::get_last_errno())) {
            fail("peer send failed");
        }
    }

    bool
    finished() const {
        for (const auto &session : _sessions) {
            if (session->callbacks != kCommands || session->commands_seen != kCommands || session->replies_sent != kCommands)
                return false;
        }
        return true;
    }

public:
    Harness(std::size_t connections, std::size_t payload_bytes)
        : _payload_bytes(payload_bytes) {
        if (connections == 0 || connections > kMaxConnections || payload_bytes < 32 || payload_bytes > kMaxPayloadBytes)
            throw std::runtime_error("unsupported fixture dimensions");
        if (!_listener.open(AF_INET, SOCK_STREAM, 0) || _listener.set_nonblocking(true) != 0)
            throw std::runtime_error("cannot open nonblocking loopback listener");
        sockaddr_in address{};
        address.sin_family      = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port        = 0;
        if (::bind(_listener.native_handle(), reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0
            || ::listen(_listener.native_handle(), 4) != 0)
            throw std::runtime_error("cannot bind/listen on ephemeral loopback port");
        socklen_t length = sizeof(address);
        if (::getsockname(_listener.native_handle(), reinterpret_cast<sockaddr *>(&address), &length) != 0)
            throw std::runtime_error("cannot discover loopback port");
        const qb::io::uri uri{"tcp://127.0.0.1:" + std::to_string(ntohs(address.sin_port))};
        _sessions.reserve(connections);
        for (std::size_t i = 0; i < connections; ++i) {
            _sessions.push_back(std::make_unique<Session>());
            auto &session  = *_sessions.back();
            session.client = std::make_unique<Client>(uri);
            session.client->connect(
                [&session](bool connected) {
                    session.connected    = connected;
                    session.connect_done = true;
                },
                kDeadline);
            const auto deadline = Clock::now() + kDeadline;
            while (!session.connect_done || !session.peer.is_open()) {
                if (!session.peer.is_open()) {
                    const auto accepted = ::accept(_listener.native_handle(), nullptr, nullptr);
                    if (accepted != qb::io::inet::invalid_socket) {
                        session.peer = accepted; // Adopts descriptor and the platform SIGPIPE protection.
                        if (session.peer.set_nonblocking(true) != 0)
                            throw std::runtime_error("cannot make accepted peer nonblocking");
                    } else if (!Socket::not_recv_error(Socket::get_last_errno())) {
                        throw std::runtime_error("loopback accept failed");
                    }
                }
                qb::io::async::run(EVRUN_NOWAIT);
                if (session.connect_done && !session.connected)
                    throw std::runtime_error("Redis client connect failed");
                if (Clock::now() >= deadline)
                    throw std::runtime_error("loopback connect/accept deadline expired");
            }
        }
        _listener.close(); // No listening service remains during measurement.
    }

    // Dispose callbacks before _error and the slots they can still report into.
    ~Harness() {
        _sessions.clear();
    }

    const std::string &
    error() const {
        return _error;
    }

    void
    prepare(std::uint64_t epoch) {
        _error.clear();
        for (std::size_t id = 0; id < _sessions.size(); ++id) {
            auto &session = *_sessions[id];
            session.command_wire.clear();
            session.response_wire.clear();
            session.received.clear();
            session.consumed = session.commands_seen = session.bytes_sent = session.replies_sent = session.callbacks = 0;
            for (std::size_t slot = 0; slot < kCommands; ++slot) {
                auto &expected = session.expected[slot];
                expected.key   = "qb:" + hex(epoch, 16) + ":" + hex(id, 2) + ":" + hex(slot, 2);
                // RESP is spelled here without qbm's serializer or decoder as an oracle.
                expected.command = "*2\r\n$3\r\nGET\r\n$" + std::to_string(expected.key.size()) + "\r\n" + expected.key + "\r\n";
                expected.payload = expected.key;
                expected.payload.resize(_payload_bytes, static_cast<char>('a' + slot));
                session.command_wire += expected.command;
                session.response_wire += "$" + std::to_string(expected.payload.size()) + "\r\n" + expected.payload + "\r\n";
                expected.response_end  = session.response_wire.size();
                session.observed[slot] = Observed{};
            }
            session.received.reserve(session.command_wire.size());
        }
    }

    bool
    exchange() {
        const auto deadline = Clock::now() + kDeadline;
        for (auto &owned : _sessions) {
            auto &session = *owned;
            for (std::size_t slot = 0; slot < kCommands; ++slot) {
                session.client->get(
                    [this, &session, slot](qb::redis::Reply<std::optional<std::string>> &&reply) {
                        auto &observed = session.observed[slot];
                        ++observed.calls;
                        ++session.callbacks;
                        observed.ok    = reply.ok();
                        observed.value = std::move(reply.value());
                        if (observed.calls != 1)
                            fail("duplicate client callback");
                        else if (!observed.ok)
                            fail("GET callback returned a Redis/transport/decode error");
                    },
                    session.expected[slot].key);
            }
        }
        while (_error.empty() && !finished()) {
            qb::io::async::run(EVRUN_NOWAIT);
            for (auto &owned : _sessions) {
                auto &session = *owned;
                receive_commands(session);
                if (!_error.empty())
                    return false;
                send_responses(session);
                if (!_error.empty())
                    return false;
            }
            if (Clock::now() >= deadline) {
                fail("batch deadline expired before every command and reply completed");
                return false;
            }
        }
        return _error.empty();
    }

    bool
    verify() {
        if (!finished()) {
            fail("batch terminated without the complete command/reply roster");
            return false;
        }
        std::uint64_t expected_hash = UINT64_C(14695981039346656037);
        std::uint64_t observed_hash = expected_hash;
        for (const auto &owned : _sessions) {
            const auto &session = *owned;
            if (session.received != session.command_wire || session.consumed != session.command_wire.size()
                || session.bytes_sent != session.response_wire.size() || session.client->pending_reply_count() != 0) {
                fail("wire byte counts or pending reply FIFO differ from the completed roster");
                return false;
            }
            for (std::size_t slot = 0; slot < kCommands; ++slot) {
                const auto &expected = session.expected[slot];
                const auto &observed = session.observed[slot];
                if (observed.calls != 1 || !observed.ok || !observed.value || *observed.value != expected.payload) {
                    fail("GET reply identity, payload or callback count mismatch");
                    return false;
                }
                expected_hash = checksum(expected_hash, expected.payload);
                observed_hash = checksum(observed_hash, *observed.value);
            }
        }
        if (observed_hash != expected_hash) {
            fail("GET batch checksum mismatch");
            return false;
        }
        benchmark::DoNotOptimize(observed_hash);
        return true;
    }
};

void
BM_RedisClient_GetBatch(benchmark::State &state) {
    bool paused = false;
    try {
        LoopScope  loop;
        const auto sessions       = static_cast<std::size_t>(state.range(0));
        const auto response_bytes = static_cast<std::size_t>(state.range(1));
        Harness    harness(sessions, response_bytes);
        harness.prepare(0);
        if (!harness.exchange() || !harness.verify()) {
            state.SkipWithError(harness.error());
            return;
        }
        std::uint64_t epoch = 1;
        for (auto _ : state) {
            state.PauseTiming();
            paused = true;
            harness.prepare(epoch++);
            state.ResumeTiming();
            paused               = false;
            const bool completed = harness.exchange();
            state.PauseTiming();
            paused           = true;
            const bool valid = completed && harness.verify();
            state.ResumeTiming();
            paused = false;
            if (!valid) {
                state.SkipWithError(harness.error());
                return;
            }
        }
        // Fresh identities settle the last measured batch before teardown; not counted.
        harness.prepare(epoch);
        if (!harness.exchange() || !harness.verify()) {
            state.SkipWithError(harness.error());
            return;
        }
        const auto commands = static_cast<std::int64_t>(sessions * kCommands);
        const auto bytes    = commands * static_cast<std::int64_t>(response_bytes);
        if (state.iterations() > std::numeric_limits<std::int64_t>::max() / bytes) {
            state.SkipWithError("benchmark counters would overflow");
            return;
        }
        state.counters["commands_per_batch"]    = static_cast<double>(commands);
        state.counters["value_bytes_per_batch"] = static_cast<double>(bytes);
        state.counters["connections"]           = static_cast<double>(sessions);
        state.SetItemsProcessed(state.iterations() * commands);
        state.SetBytesProcessed(state.iterations() * bytes); // Received VALUE payload bytes, not wire bytes.
        state.SetLabel("GET callbacks; client+loopback+fixture; 16 commands/session/batch; persistent connections");
    } catch (const std::exception &error) {
        if (paused)
            state.ResumeTiming();
        state.SkipWithError(error.what());
    }
}

} // namespace redis_client_roundtrip_bench

BENCHMARK(redis_client_roundtrip_bench::BM_RedisClient_GetBatch)
    ->Args({1, 32})
    ->Args({1, 256})
    ->Args({4, 32})
    ->Args({4, 256})
    ->ArgNames({"sessions", "response_bytes"})
    ->UseRealTime()
    ->Unit(benchmark::kNanosecond);

BENCHMARK_MAIN();
