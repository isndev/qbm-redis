/**
 * @file qbm/redis/tests/benchmark/client/oneway-batch.bench.cpp
 * @brief N SETs sent the usual way against the same N SETs in one one-way batch, over a daemon-free loopback socket
 *        (Huly QB-902).
 *
 * One iteration sends N SETs on one persistent connection and waits for their completion. `oneway = 0` issues N
 * typed `set(callback, key, value)` and the peer answers N `+OK`; `oneway = 1` adds the N SETs to a
 * `qb::redis::oneway_batch` and sends it with `send_oneway`, and the peer answers the one `+OK` of its
 * `CLIENT REPLY ON`. Both include the serialization of the commands, the client, the event loop, the loopback kernel
 * work and the in-process peer: this is client + loopback + fixture, not isolated client CPU and not a latency.
 *
 * The peer matches every received byte against an independently spelled RESP fixture (qbm's serializer is not the
 * oracle) and sends its replies only once the requests they answer are consumed. After every batch, outside timing:
 * the exact wire bytes, one callback per command (or one for the batch) and each one `ok()`, an empty reply FIFO,
 * and for a batch the `stats().oneway` delta. `allocations_per_command` counts the global `operator new` calls of one
 * more batch run after the measured loop, outside timing, divided by N. No Redis process or server thread is used.
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
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <qb/io/async.h>
#include <qb/io/system/sys__socket.h>
#include <qbm/redis/redis.h>

namespace redis_oneway_batch_bench {

/// Every global allocation of the process: read around one batch outside timing.
std::atomic<std::uint64_t> allocations{0};

} // namespace redis_oneway_batch_bench

void *
operator new(std::size_t size) {
    redis_oneway_batch_bench::allocations.fetch_add(1, std::memory_order_relaxed);
    if (void *p = std::malloc(size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc{};
}
void *
operator new[](std::size_t size) {
    return ::operator new(size);
}
void
operator delete(void *p) noexcept {
    std::free(p);
}
void
operator delete[](void *p) noexcept {
    std::free(p);
}
void
operator delete(void *p, std::size_t) noexcept {
    std::free(p);
}
void
operator delete[](void *p, std::size_t) noexcept {
    std::free(p);
}

namespace redis_oneway_batch_bench {

using Clock                          = std::chrono::steady_clock;
using Socket                         = qb::io::socket;
using Client                         = qb::redis::tcp::client;
constexpr std::size_t      kValue    = 32;
constexpr auto             kDeadline = std::chrono::seconds{2};
constexpr std::string_view kOff      = "*3\r\n$6\r\nCLIENT\r\n$5\r\nREPLY\r\n$3\r\nOFF\r\n";
constexpr std::string_view kOn       = "*3\r\n$6\r\nCLIENT\r\n$5\r\nREPLY\r\n$2\r\nON\r\n";
constexpr std::string_view kOk       = "+OK\r\n";

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

class Harness {
    Socket                   _listener;
    Socket                   _peer;
    std::unique_ptr<Client>  _client;
    bool                     _oneway;
    std::size_t              _commands;
    std::vector<std::string> _keys;
    std::string              _value;
    std::vector<std::size_t> _request_end; ///< where each expected request ends in `_wire` (typed: one per SET)
    std::string              _wire;        ///< the exact bytes the client must write for one batch
    std::string              _received;
    std::string              _replies_wire;   ///< every `+OK` of one batch, sent as their requests are matched
    std::size_t              _requests   = 0; ///< requests consumed and matched
    std::size_t              _bytes_sent = 0; ///< of `_replies_wire`
    std::size_t              _callbacks  = 0;
    std::size_t              _failures   = 0;
    std::string              _error;

    void
    fail(const char *message) {
        if (_error.empty())
            _error = message;
    }

    [[nodiscard]] std::size_t
    replies_due() const noexcept {
        return _oneway ? 1 : _commands;
    }

    void
    pump_peer() {
        std::array<char, 8192> bytes;
        for (;;) {
            const int n = _peer.recv(bytes.data(), static_cast<int>(bytes.size()), 0);
            if (n > 0) {
                const auto count = static_cast<std::size_t>(n);
                if (count > _wire.size() - _received.size()) {
                    fail("peer received excess command bytes");
                    return;
                }
                _received.append(bytes.data(), count);
                continue;
            }
            if (n == 0) {
                fail("client closed before the batch completed");
                return;
            }
            if (!Socket::not_recv_error(Socket::get_last_errno()))
                fail("peer recv failed");
            break;
        }
        while (_requests < _request_end.size() && _received.size() >= _request_end[_requests]) {
            const auto begin = _requests == 0 ? 0 : _request_end[_requests - 1];
            if (_received.compare(begin, _request_end[_requests] - begin, _wire, begin, _request_end[_requests] - begin) != 0) {
                fail("peer request bytes differ from the fixture");
                return;
            }
            ++_requests;
        }
        // A reply leaves only once the request it answers was matched; what is unlocked leaves in one send, in both
        // modes, so the peer's cost does not depend on how many replies there are to write.
        const std::size_t unlocked = kOk.size() * (_oneway ? (_requests == _request_end.size() ? 1 : 0) : _requests);
        if (_bytes_sent == unlocked)
            return;
        const int n = _peer.send(_replies_wire.data() + _bytes_sent, static_cast<int>(unlocked - _bytes_sent));
        if (n > 0)
            _bytes_sent += static_cast<std::size_t>(n);
        else if (n == 0 || !Socket::not_send_error(Socket::get_last_errno()))
            fail("peer send failed");
    }

public:
    Harness(bool oneway, std::size_t commands)
        : _oneway(oneway)
        , _commands(commands)
        , _value(kValue, 'v') {
        if (!_listener.open(AF_INET, SOCK_STREAM, 0) || _listener.set_nonblocking(true) != 0)
            throw std::runtime_error("cannot open nonblocking loopback listener");
        sockaddr_in address{};
        address.sin_family      = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port        = 0;
        if (::bind(_listener.native_handle(), reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0
            || ::listen(_listener.native_handle(), 1) != 0)
            throw std::runtime_error("cannot bind/listen on ephemeral loopback port");
        socklen_t length = sizeof(address);
        if (::getsockname(_listener.native_handle(), reinterpret_cast<sockaddr *>(&address), &length) != 0)
            throw std::runtime_error("cannot discover loopback port");
        _client           = std::make_unique<Client>(qb::io::uri{"tcp://127.0.0.1:" + std::to_string(ntohs(address.sin_port))});
        bool connect_done = false;
        bool connected    = false;
        _client->connect(
            [&](bool ok) {
                connected    = ok;
                connect_done = true;
            },
            kDeadline);
        const auto deadline = Clock::now() + kDeadline;
        while (!connect_done || !_peer.is_open()) {
            if (!_peer.is_open()) {
                const auto accepted = ::accept(_listener.native_handle(), nullptr, nullptr);
                if (accepted != qb::io::inet::invalid_socket) {
                    _peer = accepted;
                    if (_peer.set_nonblocking(true) != 0)
                        throw std::runtime_error("cannot make accepted peer nonblocking");
                } else if (!Socket::not_recv_error(Socket::get_last_errno())) {
                    throw std::runtime_error("loopback accept failed");
                }
            }
            qb::io::async::run(EVRUN_NOWAIT);
            if (connect_done && !connected)
                throw std::runtime_error("Redis client connect failed");
            if (Clock::now() >= deadline)
                throw std::runtime_error("loopback connect/accept deadline expired");
        }
        _listener.close();
        _keys.resize(_commands);
    }

    ~Harness() {
        _client.reset(); // the callbacks report into this object
    }

    [[nodiscard]] std::string const &
    error() const noexcept {
        return _error;
    }

    void
    prepare(std::uint64_t epoch) {
        _error.clear();
        _wire.clear();
        _request_end.clear();
        _received.clear();
        _requests = _bytes_sent = _callbacks = _failures = 0;
        if (_oneway)
            _wire += kOff;
        for (std::size_t i = 0; i < _commands; ++i) {
            _keys[i] = "qb:" + hex(epoch, 16) + ":" + hex(i, 4);
            // RESP spelled here, without qbm's serializer as the oracle.
            _wire += "*3\r\n$3\r\nSET\r\n$" + std::to_string(_keys[i].size()) + "\r\n" + _keys[i] + "\r\n$" + std::to_string(kValue) + "\r\n"
                     + _value + "\r\n";
            if (!_oneway)
                _request_end.push_back(_wire.size());
        }
        if (_oneway) {
            _wire += kOn;
            _request_end.push_back(_wire.size());
        }
        _received.reserve(_wire.size());
        _replies_wire.clear();
        for (std::size_t i = 0; i < replies_due(); ++i)
            _replies_wire += kOk;
    }

    bool
    exchange() {
        if (_oneway) {
            qb::redis::oneway_batch batch;
            for (auto const &key : _keys)
                if (batch.add("SET", key, _value)) {
                    fail("a SET was refused by the batch");
                    return false;
                }
            _client->send_oneway(std::move(batch), [this](qb::redis::Reply<qb::redis::oneway_result> &&reply) {
                ++_callbacks;
                if (!reply.ok() || reply.result().commands != _commands)
                    ++_failures;
            });
        } else {
            for (auto const &key : _keys)
                _client->set(
                    [this](qb::redis::Reply<qb::redis::status> &&reply) {
                        ++_callbacks;
                        if (!reply.ok())
                            ++_failures;
                    },
                    key, _value);
        }
        const auto deadline = Clock::now() + kDeadline;
        while (_error.empty() && _callbacks < replies_due()) {
            qb::io::async::run(EVRUN_NOWAIT);
            pump_peer();
            if (Clock::now() >= deadline) {
                fail("batch deadline expired before every command and reply completed");
                return false;
            }
        }
        return _error.empty();
    }

    bool
    verify(std::uint64_t oneway_before) {
        if (_callbacks != replies_due() || _failures != 0) {
            fail("a callback failed, or the callbacks are not one per command (one for a batch)");
            return false;
        }
        if (_received != _wire || _requests != _request_end.size() || _bytes_sent != _replies_wire.size()
            || _client->pending_reply_count() != 0) {
            fail("wire bytes, replies or the pending reply FIFO differ from the completed roster");
            return false;
        }
        if (_client->stats().oneway != oneway_before + (_oneway ? _commands : 0)) {
            fail("stats().oneway does not count the batch's commands");
            return false;
        }
        return true;
    }

    [[nodiscard]] std::uint64_t
    oneway_count() const noexcept {
        return _client->stats().oneway;
    }
};

void
BM_RedisClient_SetBatch(benchmark::State &state) {
    bool paused = false;
    try {
        LoopScope  loop;
        const bool oneway   = state.range(0) != 0;
        const auto commands = static_cast<std::size_t>(state.range(1));
        Harness    harness(oneway, commands);
        harness.prepare(0);
        if (auto before = harness.oneway_count(); !harness.exchange() || !harness.verify(before)) {
            state.SkipWithError(harness.error());
            return;
        }
        std::uint64_t epoch = 1;
        for (auto _ : state) {
            state.PauseTiming();
            paused = true;
            harness.prepare(epoch++);
            const auto before = harness.oneway_count();
            state.ResumeTiming();
            paused               = false;
            const bool completed = harness.exchange();
            state.PauseTiming();
            paused           = true;
            const bool valid = completed && harness.verify(before);
            state.ResumeTiming();
            paused = false;
            if (!valid) {
                state.SkipWithError(harness.error());
                return;
            }
        }
        // One more batch outside timing: its allocations, then its validation.
        harness.prepare(epoch);
        const auto before      = harness.oneway_count();
        const auto allocs_from = allocations.load(std::memory_order_relaxed);
        const bool completed   = harness.exchange();
        const auto allocs      = allocations.load(std::memory_order_relaxed) - allocs_from;
        if (!completed || !harness.verify(before)) {
            state.SkipWithError(harness.error());
            return;
        }
        const auto items = static_cast<std::int64_t>(commands);
        if (state.iterations() > std::numeric_limits<std::int64_t>::max() / items) {
            state.SkipWithError("benchmark counters would overflow");
            return;
        }
        state.counters["commands_per_batch"]      = static_cast<double>(commands);
        state.counters["allocations_per_command"] = static_cast<double>(allocs) / static_cast<double>(commands);
        state.SetItemsProcessed(state.iterations() * items);
        state.SetLabel(oneway ? "one-way batch: OFF + N SET + ON, one +OK; client+loopback+fixture"
                              : "typed SET callbacks: N +OK; client+loopback+fixture");
    } catch (const std::exception &error) {
        if (paused)
            state.ResumeTiming();
        state.SkipWithError(error.what());
    }
}

} // namespace redis_oneway_batch_bench

BENCHMARK(redis_oneway_batch_bench::BM_RedisClient_SetBatch)
    ->Args({0, 16})
    ->Args({1, 16})
    ->Args({0, 256})
    ->Args({1, 256})
    ->ArgNames({"oneway", "commands"})
    ->UseRealTime()
    ->Unit(benchmark::kNanosecond);

BENCHMARK_MAIN();
