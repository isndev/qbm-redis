/**
 * @file redis.h
 * @brief Main Redis client, protocol, connector, and consumer types
 */
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

#ifndef QBM_REDIS_H
#define QBM_REDIS_H

#include <queue>
#include <deque>
#include <utility>
#include <random>
#include <functional>
#include <qb/io/async.h>
#include <qb/io/async/tcp/connector.h>

// Native Redis Protocol Parser (C++23)
#include "parser.h"
#include "reply.h"

// Command traits
#include "connection_commands.h"
#include "server_commands.h"
#include "key_commands.h"
#include "string_commands.h"
#include "list_commands.h"
#include "hash_commands.h"
#include "set_commands.h"
#include "sorted_set_commands.h"
#include "hyperloglog_commands.h"
#include "geo_commands.h"
#include "scripting_commands.h"
#include "stream_commands.h"
#include "publish_commands.h"
#include "subscription_commands.h"
#include "bitmap_commands.h"
#include "transaction_commands.h"
#include "cluster_commands.h"
#include "acl_commands.h"
#include "module_commands.h"
#include "function_commands.h"

namespace qb::protocol {

/**
 * @class redis
 * @brief Native C++23 Redis protocol implementation
 *
 * Uses the world-class native Redis Protocol Parser with full RESP2/RESP3 support.
 * Zero-copy parsing where possible, streaming-capable for async I/O.
 * @tparam IO_ The underlying I/O connector type (CRTP)
 */
template <typename IO_>
class redis final : public qb::io::async::AProtocol<IO_> {
    constexpr IO_ &derived() {
        return static_cast<IO_ &>(*this);
    }

public:
    /**
     * @struct message
     * @brief Container for Redis reply data with ownership transfer
     * 
     * Uses unique_ptr for clear ownership semantics. The handler takes ownership
     * of the reply via move semantics.
     */
    struct message {
        std::unique_ptr<qb::redis::parser::Value> reply;
    };

private:
    qb::redis::parser::RespParser _parser;
    /// Fully-parsed values waiting to be dispatched in onMessage().
    std::deque<qb::redis::parser::Value> _pending_messages;
    /// How many bytes from the current _io.in() window have already been
    /// fed to _parser.  Prevents double-feeding across successive
    /// getMessageSize() calls that arrive before onMessage() is invoked.
    size_t _fed_bytes{0};

public:
    redis() = delete;

    explicit redis(IO_ &io) noexcept
        : qb::io::async::AProtocol<IO_>(io)
        , _parser(qb::redis::parser::ParserConfig{
              .protocol_version = qb::redis::parser::ProtocolVersion::RESP3,
              .max_nesting_depth = 64,
              .max_bulk_size = 512 * 1024 * 1024,
              .max_array_size = 1'000'000
          }) {}

    ~redis() = default;

    std::size_t getMessageSize() noexcept final {
        const size_t current_size = this->_io.in().size();

        // Feed only the bytes that arrived since our last call so we never
        // double-feed the same bytes into the parser's internal buffer.
        if (current_size > _fed_bytes) {
            const auto new_data = std::span<const char>(
                this->_io.in().begin() + static_cast<std::ptrdiff_t>(_fed_bytes),
                current_size - _fed_bytes
            );

            if (qb__unlikely(!_parser.feed(new_data))) {
                this->not_ok();
                _parser.reset();
                _pending_messages.clear();
                _fed_bytes = 0;
                return 0;
            }

            _fed_bytes = current_size;

            // parse_all() compacts the buffer, then uses a non-destructive
            // ViewBuffer pass.  Incomplete aggregates leave no bytes consumed,
            // so they are retried intact when more data arrives.
            auto parsed = _parser.parse_all();
            for (auto &v : parsed)
                _pending_messages.push_back(std::move(v));
        }

        // Return the number of bytes currently in _io.in() so the framework
        // knows how much to flush after onMessage().  We return non-zero only
        // when at least one complete value has been parsed and is waiting.
        return _pending_messages.empty() ? 0 : current_size;
    }

    void onMessage(std::size_t) noexcept final {
        if (!this->ok()) return;

        // The framework is about to flush current_size bytes from _io.in().
        // Reset the offset so the next getMessageSize() feeds from 0.
        _fed_bytes = 0;

        // Dispatch every pending parsed reply to the owning IO handler.
        while (!_pending_messages.empty()) {
            auto reply_ptr = std::make_unique<qb::redis::parser::Value>(
                std::move(_pending_messages.front())
            );
            _pending_messages.pop_front();
            this->_io.on(message{std::move(reply_ptr)});
        }

        // Reclaim memory from the parser's internal buffer.
        _parser.compact();
    }

    void reset() noexcept final {
        _parser.reset();
        _pending_messages.clear();
        _fed_bytes = 0;
    }
};

} // namespace qb::protocol

namespace qb::redis {

// ============================================================================
// Retry Policy
// ============================================================================

/**
 * @struct RetryPolicy
 * @brief Configuration for connection retry behavior with exponential backoff
 *
 * Used by connect_with_retry() and enable_auto_reconnect() to control
 * reconnection attempts when the Redis connection is lost.
 */
struct RetryPolicy {
    int max_attempts = -1;
    std::chrono::milliseconds initial_delay{100};
    std::chrono::milliseconds max_delay{30'000};
    double multiplier = 2.0;
    bool jitter = true;
    double connect_timeout = 3.0;
    std::function<void(int attempt, std::chrono::milliseconds next_delay)> on_retry;

    /** @brief Set maximum number of retry attempts (-1 = unlimited) */
    RetryPolicy &with_max_attempts(int n) noexcept {
        max_attempts = n;
        return *this;
    }
    /** @brief Set initial delay between retries */
    RetryPolicy &with_initial_delay(std::chrono::milliseconds d) noexcept {
        initial_delay = d;
        return *this;
    }
    /** @brief Set maximum delay cap */
    RetryPolicy &with_max_delay(std::chrono::milliseconds d) noexcept {
        max_delay = d;
        return *this;
    }
    /** @brief Set exponential backoff multiplier */
    RetryPolicy &with_multiplier(double m) noexcept {
        multiplier = m;
        return *this;
    }
    /** @brief Enable/disable random jitter on delays */
    RetryPolicy &with_jitter(bool j) noexcept {
        jitter = j;
        return *this;
    }
    /** @brief Set connection timeout in seconds */
    RetryPolicy &with_connect_timeout(double t) noexcept {
        connect_timeout = t;
        return *this;
    }
    /** @brief Set callback invoked before each retry (attempt, next_delay) */
    RetryPolicy &with_on_retry(
        std::function<void(int, std::chrono::milliseconds)> cb) {
        on_retry = std::move(cb);
        return *this;
    }
};

// ============================================================================
// Connector base class
// ============================================================================

/**
 * @namespace detail
 * @brief Internal implementation details for Redis client and consumers
 */
namespace detail {
using namespace qb::io;

/**
 * @class connector
 * @brief Base TCP connector for Redis clients and consumers
 *
 * Handles connection lifecycle, auto-reconnect, and protocol switching.
 * @tparam QB_IO_ I/O transport type (e.g. qb::io::transport::tcp)
 * @tparam Derived CRTP derived class (Redis or RedisConsumer)
 */
template <typename QB_IO_, typename Derived>
class connector
    : public qb::io::async::tcp::client<connector<QB_IO_, Derived>, QB_IO_, void> {
    friend class has_method_on<connector<QB_IO_, Derived>, void,
                               qb::io::async::event::disconnected>;
    friend class qb::io::async::io<connector<QB_IO_, Derived>>;
    friend class qb::protocol::redis<connector<QB_IO_, Derived>>;
    
    constexpr Derived &derived() {
        return static_cast<Derived &>(*this);
    }

public:
    using redis_protocol = qb::protocol::redis<connector<QB_IO_, Derived>>;

private:
    qb::io::uri _uri;
    std::optional<RetryPolicy> _reconnect_policy;
    bool _is_reconnecting = false;
    bool _connected_flag = false;
    std::shared_ptr<bool> _alive{std::make_shared<bool>(true)};

    void start_async() {
        if (this->protocol()) this->clear_protocols();
        this->reset_io_state();
        this->template switch_protocol<redis_protocol>(*this);
        this->start();
        _connected_flag = true;
    }

    void on(typename redis_protocol::message msg) {
        derived().on(std::move(msg));
    }

    void on(qb::io::async::event::disconnected &&ev) {
        _connected_flag = false;
        LOG_WARN("[qbm][redis] disconnected");
        derived().on(std::forward<qb::io::async::event::disconnected>(ev));

        if (_reconnect_policy && !_is_reconnecting) {
            _is_reconnecting = true;
            qb::io::async::coro_scheduler().spawn(
                _reconnect_task(*_reconnect_policy, _alive));
        }
    }

    qb::io::async::task<void> _reconnect_task(RetryPolicy policy, std::shared_ptr<bool> alive) {
        LOG_INFO("[qbm][redis] auto-reconnect starting...");

        const bool ok = co_await connect_with_retry(policy);

        if (!*alive) co_return;

        _is_reconnecting = false;

        if (ok) {
            LOG_INFO("[qbm][redis] auto-reconnect succeeded");
        } else {
            LOG_WARN("[qbm][redis] auto-reconnect failed");
        }
    }

protected:
    connector() = default;
    ~connector() { *_alive = false; }

    explicit connector(qb::io::uri uri) : _uri{std::move(uri)} {}

public:
    /**
     * @struct connect_awaiter
     * @brief Coroutine awaiter for async connection (co_await connect())
     */
    struct connect_awaiter {
        connector& _client;
        double _timeout;
        bool _connected = false;
        std::coroutine_handle<> _handle;
        bool _ready = false;
        std::shared_ptr<bool> _valid{std::make_shared<bool>(true)};

        explicit connect_awaiter(connector &client, double timeout = 3.0)
            : _client(client), _timeout(timeout) {}

        ~connect_awaiter() { if (_valid) *_valid = false; }

        bool await_ready() const noexcept { return _ready; }

        void await_suspend(std::coroutine_handle<> h) {
            _handle = h;
            auto valid = _valid;
            ::qb::io::async::tcp::connect<typename QB_IO_::transport_io_type>(
                _client._uri,
                [this, valid](auto &&raw_io) {
                    if (!*valid) return;
                    if (raw_io.is_open()) {
                        _connected = _client.setup_connection(_client._uri, std::move(raw_io));
                    }
                    _ready = true;
                    if (_handle) {
                        ::qb::io::async::coro_scheduler().schedule_resume(_handle);
                    }
                },
                _timeout);
        }

        bool await_resume() const noexcept { return _connected; }
    };

    /** @brief Start async connection (co_awaitable) */
    connect_awaiter connect() { return connect_awaiter{*this}; }
    connect_awaiter connect(qb::io::uri uri) {
        _uri = std::move(uri);
        return connect_awaiter{*this};
    }
    connect_awaiter connect(double timeout_sec) {
        return connect_awaiter{*this, timeout_sec};
    }
    connect_awaiter connect(qb::io::uri uri, double timeout_sec) {
        _uri = std::move(uri);
        return connect_awaiter{*this, timeout_sec};
    }

    void set_uri(qb::io::uri uri) noexcept { _uri = std::move(uri); }

    qb::io::async::task<bool> connect_with_retry(RetryPolicy policy = RetryPolicy{}) {
        static thread_local std::mt19937 rng_{std::random_device{}()};

        auto delay = policy.initial_delay;
        int attempt = 0;

        while (policy.max_attempts < 0 || attempt < policy.max_attempts) {
            ++attempt;

            if (co_await connect_awaiter{*this, policy.connect_timeout})
                co_return true;

            if (policy.max_attempts > 0 && attempt >= policy.max_attempts)
                break;

            auto sleep_ms = delay;
            if (policy.jitter && delay.count() > 0) {
                const long long quarter = delay.count() / 4;
                std::uniform_int_distribution<long long> dist{-quarter, quarter};
                sleep_ms = std::max(std::chrono::milliseconds{1},
                                    std::chrono::milliseconds{delay.count() + dist(rng_)});
            }

            if (policy.on_retry) {
                policy.on_retry(attempt, sleep_ms);
            }

            co_await qb::io::async::sleep(sleep_ms);

            const auto next_ms = static_cast<long long>(
                static_cast<double>(delay.count()) * policy.multiplier);
            delay = std::min(std::chrono::milliseconds{next_ms}, policy.max_delay);
        }

        co_return false;
    }

    qb::io::async::task<bool> connect_with_retry(qb::io::uri uri, RetryPolicy policy = RetryPolicy{}) {
        _uri = std::move(uri);
        co_return co_await connect_with_retry(std::move(policy));
    }

    template <std::invocable<bool> Func>
    void connect(Func &&func, qb::io::uri uri, double timeout = 3) {
        qb::io::async::tcp::connect<typename QB_IO_::transport_io_type>(
            uri,
            [this, uri, func = std::forward<Func>(func)](auto &&raw_io) {
                if (raw_io.is_open()) {
                    func(this->setup_connection(uri, std::forward<decltype(raw_io)>(raw_io)));
                } else {
                    func(false);
                }
            },
            timeout);
    }

    template <std::invocable<bool> Func>
    void connect(Func &&func, double timeout = 3) {
        connect(std::forward<Func>(func), _uri, timeout);
    }

    [[nodiscard]] qb::io::uri const &uri() const noexcept { return _uri; }

    bool setup_connection(qb::io::uri uri, typename QB_IO_::transport_io_type &&raw_io) {
        if (_connected_flag) return false;
        _uri = std::move(uri);
        this->transport() = std::move(raw_io);
        start_async();
        return true;
    }

    void enable_auto_reconnect(RetryPolicy policy = RetryPolicy{}) noexcept {
        _reconnect_policy = std::move(policy);
    }

    void disable_auto_reconnect() noexcept { _reconnect_policy.reset(); }

    [[nodiscard]] bool is_reconnecting() const noexcept { return _is_reconnecting; }
    [[nodiscard]] bool is_connected() const noexcept { return _connected_flag; }

    void disconnect() noexcept {
        _connected_flag = false;
        qb::io::async::io<connector<QB_IO_, Derived>>::disconnect();
    }
};

// ============================================================================
// Redis awaiter
// ============================================================================

/**
 * @class redis_awaiter
 * @brief Coroutine awaiter for Redis command results
 *
 * Yields Reply<T> when the command completes. Use with co_await.
 * @tparam T Expected result type
 * @tparam Operation Callback-based operation that invokes the continuation
 */
template <typename T, typename Operation>
class redis_awaiter {
    Reply<T> result_;
    std::coroutine_handle<> handle_;
    std::shared_ptr<bool> valid_{std::make_shared<bool>(true)};
    Operation operation_;

public:
    explicit redis_awaiter(Operation &&op)
        : operation_(std::forward<Operation>(op)) {}

    ~redis_awaiter() { if (valid_) *valid_ = false; }

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> h) {
        handle_ = h;
        auto valid = valid_;
        operation_([this, valid](Reply<T> &&reply) {
            if (!*valid) return;
            result_ = std::move(reply);
            qb::io::async::coro_scheduler().schedule_resume(handle_);
        });
    }

    [[nodiscard]] Reply<T> await_resume() { return std::move(result_); }
};

/** @brief Factory for redis_awaiter from a callback-based operation */
template <typename T, typename Func>
[[nodiscard]] auto make_redis_awaiter(Func &&operation) {
    return redis_awaiter<T, std::remove_cvref_t<Func>>{std::forward<Func>(operation)};
}

// ============================================================================
// Main Redis client
// ============================================================================

/**
 * @class Redis
 * @brief Full-featured Redis client with all command groups
 *
 * Supports both callback-based and coroutine-based APIs.
 * Inherits from all *_commands mixins (connection, server, key, string, etc.).
 *
 * @warning Not thread-safe. Use from a single I/O thread / strand (one concurrent
 *          accessor at a time). The reply queue and outbound pipe are unsynchronized.
 *
 * @par Pipelining (callback API)
 * Issue multiple command(callback, ...) calls without awaiting between them; each
 * enqueues one handler and sends bytes in order. Drain with await() or your event
 * loop. See also RedisPipeline for a named wrapper.
 *
 * @tparam QB_IO_ I/O transport type (e.g. qb::io::transport::tcp)
 */
template <typename QB_IO_>
class Redis
    : public connector<QB_IO_, Redis<QB_IO_>>
    , public connection_commands<Redis<QB_IO_>>
    , public server_commands<Redis<QB_IO_>>
    , public key_commands<Redis<QB_IO_>>
    , public string_commands<Redis<QB_IO_>>
    , public list_commands<Redis<QB_IO_>>
    , public hash_commands<Redis<QB_IO_>>
    , public set_commands<Redis<QB_IO_>>
    , public sorted_set_commands<Redis<QB_IO_>>
    , public hyperloglog_commands<Redis<QB_IO_>>
    , public geo_commands<Redis<QB_IO_>>
    , public scripting_commands<Redis<QB_IO_>>
    , public publish_commands<Redis<QB_IO_>>
    , public stream_commands<Redis<QB_IO_>>
    , public bitmap_commands<Redis<QB_IO_>>
    , public transaction_commands<Redis<QB_IO_>>
    , public cluster_commands<Redis<QB_IO_>>
    , public acl_commands<Redis<QB_IO_>>
    , public module_commands<Redis<QB_IO_>>
    , public function_commands<Redis<QB_IO_>> {
    friend class connector<QB_IO_, Redis<QB_IO_>>;

public:
    using redis_protocol = typename connector<QB_IO_, Redis<QB_IO_>>::redis_protocol;
    using publish_commands<Redis<QB_IO_>>::publish;
    using server_commands<Redis<QB_IO_>>::command;

private:
    std::queue<std::unique_ptr<IReply>> _replies;

    template <typename... Args>
    void _command(Args &&...args) {
        this->ready_to_write();
        put_in_pipe(this->out(), std::forward<Args>(args)...);
    }

    void on(typename redis_protocol::message msg) {
        if (_replies.empty()) {
            LOG_WARN("[qbm][redis] Received unsolicited reply with no pending command, discarding");
            return;
        }
        auto handler = std::move(_replies.front());
        _replies.pop();
        // Transfer ownership of reply to handler
        (*handler)(std::move(msg.reply));
    }

    void on(qb::io::async::event::disconnected &&) {
        LOG_WARN("[qbm][redis] disconnected by remote");
        while (!_replies.empty()) {
            auto handler = std::move(_replies.front());
            _replies.pop();
            try {
                (*handler)(nullptr);
            } catch (const std::exception &ex) {
                LOG_WARN("[qbm][redis] callback error: " << ex.what());
            }
        }
        transaction_commands<Redis<QB_IO_>>::reset_transaction_state();
    }

public:
    Redis() = default;
    explicit Redis(qb::io::uri uri)
        : connector<QB_IO_, Redis<QB_IO_>>(std::move(uri)) {}

    template <typename Ret, typename Func, typename... Args>
        requires std::invocable<Func, Reply<Ret> &&>
    Redis &command(Func &&func, std::string const &name, Args &&...args) {
        // Register the reply handler before sending so a very fast/synchronous
        // delivery cannot run before the handler is queued (pipeline-safe).
        _replies.push(std::make_unique<TReply<Func, Ret>>(std::forward<Func>(func)));
        _command(name, std::forward<Args>(args)...);
        return *this;
    }

    template <typename Ret, typename... Args>
    auto command(std::string const &name, Args &&...args) {
        return make_coro_command<Ret>(
            [this, name, args_tuple = std::make_tuple(std::forward<Args>(args)...)](
                auto &&callback) mutable {
                std::apply(
                    [this, &callback, &name](auto &&...a) {
                        this->command<Ret>(std::move(callback), name,
                                          std::forward<decltype(a)>(a)...);
                    },
                    std::move(args_tuple));
            });
    }

    /**
     * @brief Drain all pending command replies on the current event loop.
     *
     * Runs @c qb::io::async::listener::current.run(EVRUN_NOWAIT) until the internal
     * reply queue is empty. This does **not** block the thread in the kernel; each
     * iteration is a non-blocking poll. The **caller** still runs synchronously until
     * every enqueued callback has been invoked (success, Redis error, or disconnect).
     *
     * @warning Call from the same thread / loop that drives Redis I/O. Do not
     *          confuse with coroutine @c co_await — this is explicit draining for
     *          the callback / pipeline API.
     *
     * @note Uses @c listener::current.run(EVRUN_NOWAIT), not @c async::run().
     *          Coroutine bodies may call @c await() (e.g. a second client in a
     *          test); @c async::run rejects that context to forbid nested blocking
     *          pumps, but a non-blocking loop here only needs libev + same semantics
     *          as before the guard existed.
     */
    Redis &await() {
        while (!_replies.empty())
            qb::io::async::listener::current.run(EVRUN_NOWAIT);
        return *this;
    }

    /** @brief Number of commands sent awaiting a Redis reply (for pipeline debugging). */
    [[nodiscard]] std::size_t pending_reply_count() const noexcept {
        return _replies.size();
    }

    template <typename T, typename Func>
    [[nodiscard]] auto make_coro_command(Func &&operation) {
        return make_redis_awaiter<T>(std::forward<Func>(operation));
    }
};

// ============================================================================
// Pipeline helper (callback API)
// ============================================================================

/**
 * @class RedisPipeline
 * @brief Optional helper for callback-style pipelining
 *
 * Pipelining is a property of the client: each `command(callback, ...)` (including
 * mixin methods `set()`, `get()`, …) pushes one handler and sends one request in
 * order; Redis returns replies in the same order. Drain with `await()` on the
 * client or `flush()` here.
 *
 * This wrapper only chains the low-level `command<Ret>(callback, name, args...)`.
 * For `set`/`get`/etc., use `client().set(...)` then `flush()`, or call `await()`
 * on the client.
 *
 * @note `flush()` runs the event loop until all pending replies are delivered; it
 *       is unrelated to the Redis command FLUSHDB/FLUSHALL.
 * @tparam QB_IO_ Same transport as Redis<QB_IO_>
 */
template <typename QB_IO_>
class RedisPipeline {
    Redis<QB_IO_> &_client;

public:
    explicit RedisPipeline(Redis<QB_IO_> &client) noexcept
        : _client(client) {}

    [[nodiscard]] Redis<QB_IO_> &client() noexcept { return _client; }
    [[nodiscard]] Redis<QB_IO_> const &client() const noexcept { return _client; }

    /** @brief Same as `client().pending_reply_count()`. */
    [[nodiscard]] std::size_t pending_reply_count() const noexcept {
        return _client.pending_reply_count();
    }

    template <typename Ret, typename Func, typename... Args>
        requires std::invocable<Func, Reply<Ret> &&>
    RedisPipeline &command(Func &&func, std::string const &name, Args &&...args) {
        _client.template command<Ret>(std::forward<Func>(func), name,
                                      std::forward<Args>(args)...);
        return *this;
    }

    /** @brief Drain pending replies (calls `client().await()`). */
    Redis<QB_IO_> &flush() { return _client.await(); }
};

// ============================================================================
// Redis Consumer (Pub/Sub)
// ============================================================================

template <typename QB_IO_, typename Derived>
class RedisConsumer
    : public connector<QB_IO_, RedisConsumer<QB_IO_, Derived>>
    , public connection_commands<Derived>
    , public subscription_commands<Derived> {
    friend class connector<QB_IO_, RedisConsumer<QB_IO_, Derived>>;
    friend class connection_commands<Derived>;
    friend class subscription_commands<Derived>;
    
    constexpr Derived &derived() {
        return static_cast<Derived &>(*this);
    }

public:
    using redis_protocol =
        typename connector<QB_IO_, RedisConsumer<QB_IO_, Derived>>::redis_protocol;

private:
    enum class MsgType { SUBSCRIBE, UNSUBSCRIBE, PSUBSCRIBE, PUNSUBSCRIBE, MESSAGE, PMESSAGE, UNKNOWN };

    static MsgType msg_type(const std::string_view &type) {
        static const qb::unordered_flat_map<std::string_view, MsgType> str_to_enum{
            {"message", MsgType::MESSAGE},
            {"pmessage", MsgType::PMESSAGE},
            {"subscribe", MsgType::SUBSCRIBE},
            {"unsubscribe", MsgType::UNSUBSCRIBE},
            {"psubscribe", MsgType::PSUBSCRIBE},
            {"punsubscribe", MsgType::PUNSUBSCRIBE}};

        auto it = str_to_enum.find(type);
        return it != str_to_enum.end() ? it->second : MsgType::UNKNOWN;
    }

    std::queue<std::unique_ptr<IReply>> _replies;

    template <typename... Args>
    void _command(Args &&...args) {
        this->ready_to_write();
        put_in_pipe(this->out(), std::forward<Args>(args)...);
    }

    template <typename Ret, typename Func, typename... Args>
        requires std::invocable<Func, Reply<Ret> &&>
    Derived &command(Func &&func, std::string const &name, Args &&...args) {
        _replies.push(std::make_unique<TReply<Func, Ret>>(std::forward<Func>(func)));
        _command(name, std::forward<Args>(args)...);
        return derived();
    }

    template <typename Ret, typename... Args>
    Reply<Ret> command(std::string const &name, Args &&...args) {
        Reply<Ret> value{};
        auto func = [&value](auto &&reply) { value = std::forward<Reply<Ret>>(reply); };
        command<Ret>(func, name, std::forward<Args>(args)...).await();
        return value;
    }

    void on(typename redis_protocol::message msg) {
        try {
            if (!msg.reply) {
                throw ProtoError("Null reply received");
            }
            auto &raw = *msg.reply;

            // Regular command reply (HELLO map, etc.) - not pub/sub
            if (!reply::is_array_or_push(raw)) {
                if (!_replies.empty()) {
                    auto handler = std::move(_replies.front());
                    _replies.pop();
                    try {
                        (*handler)(std::move(msg.reply));
                    } catch (const std::exception &e) {
                        LOG_WARN("[qbm][redis] consumer error: " << e.what());
                    }
                } else {
                    throw ProtoError("Unexpected non-pub/sub reply with no pending command");
                }
                return;
            }

            if (reply::get_pubsub_size(raw) == 0) {
                throw ProtoError("Invalid message format");
            }
            auto const *elem0 = reply::get_pubsub_element(raw, 0);
            if (!elem0) throw ProtoError("Invalid message format");
            auto type = msg_type(reply::parse<std::string_view>(*elem0));

            switch (type) {
                case MsgType::MESSAGE:
                    derived().on(parse<qb::redis::message>(raw));
                    return;
                case MsgType::PMESSAGE:
                    derived().on(parse<qb::redis::pmessage>(raw));
                    return;
                case MsgType::SUBSCRIBE:
                case MsgType::UNSUBSCRIBE:
                case MsgType::PSUBSCRIBE:
                case MsgType::PUNSUBSCRIBE:
                    // Subscription confirmations (not pub/sub messages) are command replies.
                    // Fall through to match with the pending handler in _replies.
                default:
                    break;
            }

            if (!_replies.empty()) {
                auto handler = std::move(_replies.front());
                _replies.pop();
                try {
                    (*handler)(std::move(msg.reply));
                } catch (const std::exception &e) {
                    LOG_WARN("[qbm][redis] consumer error: " << e.what());
                }
            } else {
                throw ProtoError("Unknown message type");
            }
        } catch (std::exception &e) {
            if (!_replies.empty()) {
                auto handler = std::move(_replies.front());
                _replies.pop();
                try {
                    (*handler)(nullptr);  // Completes awaitable with error
                } catch (...) {}
            }
            on(qb::redis::error{e.what(), nullptr});
        }
    }

    void on(qb::io::async::event::disconnected &&e) {
        LOG_WARN("[qbm][redis] consumer disconnected");
        while (!_replies.empty()) {
            auto handler = std::move(_replies.front());
            _replies.pop();
            try {
                (*handler)(nullptr);
            } catch (const std::exception &ex) {
                LOG_WARN("[qbm][redis] consumer callback error: " << ex.what());
            }
        }
        if constexpr (has_method_on<Derived, void, qb::io::async::event::disconnected>::value)
            derived().on(std::forward<qb::io::async::event::disconnected>(e));
    }

    void on(qb::redis::error &&error) {
        LOG_WARN("[qbm][redis] parse error: " << error.what);
        if constexpr (has_method_on<Derived, void, qb::redis::error>::value)
            derived().on(std::forward<qb::redis::error>(error));
    }

public:
    RedisConsumer() = default;
    explicit RedisConsumer(qb::io::uri uri)
        : connector<QB_IO_, RedisConsumer<QB_IO_, Derived>>(std::move(uri)) {}

    Derived &await() {
        while (!_replies.empty())
            qb::io::async::listener::current.run(EVRUN_NOWAIT);
        return derived();
    }

    template <typename T, typename Func>
    [[nodiscard]] auto make_coro_command(Func &&operation) {
        return make_redis_awaiter<T>(std::forward<Func>(operation));
    }
};

// ============================================================================
// Callback Consumer
// ============================================================================

/**
 * @class RedisCallbackConsumer
 * @brief Pub/Sub consumer with callback-based message handling
 *
 * Set on_message(), on_error(), on_disconnected() before subscribing.
 */
template <typename QB_IO_>
class RedisCallbackConsumer
    : public RedisConsumer<QB_IO_, RedisCallbackConsumer<QB_IO_>> {
    friend class has_method_on<RedisCallbackConsumer<QB_IO_>, void, qb::redis::error>;
    friend class has_method_on<RedisCallbackConsumer<QB_IO_>, void,
                               qb::io::async::event::disconnected>;
    friend RedisConsumer<QB_IO_, RedisCallbackConsumer<QB_IO_>>;
    
    using cb_msg_t = std::function<void(qb::redis::message &&)>;
    using cb_err_t = std::function<void(qb::redis::error &&)>;
    using cb_disc_t = std::function<void(qb::io::async::event::disconnected &&)>;

    cb_msg_t _on_message;
    cb_err_t _on_error;
    cb_disc_t _on_disconnected;

    void on(qb::redis::message &&msg) {
        if (_on_message) _on_message(std::forward<qb::redis::message>(msg));
    }

    void on(qb::redis::error &&error) {
        if (_on_error) _on_error(std::forward<qb::redis::error>(error));
    }

    void on(qb::io::async::event::disconnected &&ev) {
        if (_on_disconnected) _on_disconnected(std::forward<qb::io::async::event::disconnected>(ev));
    }

public:
    explicit RedisCallbackConsumer(
        qb::io::uri uri = {}, 
        cb_msg_t &&on_message = cb_msg_t{},
        cb_err_t &&on_error = cb_err_t{},
        cb_disc_t &&on_disconnected = cb_disc_t{})
        : RedisConsumer<QB_IO_, RedisCallbackConsumer<QB_IO_>>(std::move(uri))
        , _on_message(std::forward<cb_msg_t>(on_message))
        , _on_error(std::forward<cb_err_t>(on_error))
        , _on_disconnected(std::forward<cb_disc_t>(on_disconnected)) {}

    RedisCallbackConsumer &on_message(cb_msg_t &&cb) {
        _on_message = std::forward<cb_msg_t>(cb);
        return *this;
    }

    RedisCallbackConsumer &on_error(cb_err_t &&cb) {
        _on_error = std::forward<cb_err_t>(cb);
        return *this;
    }

    RedisCallbackConsumer &on_disconnected(cb_disc_t &&cb) {
        _on_disconnected = std::forward<cb_disc_t>(cb);
        return *this;
    }
};

// ============================================================================
// Coroutine Consumer (receive() API)
// ============================================================================

/**
 * @class RedisCoroConsumer
 * @brief Pub/Sub consumer with coroutine-based receive() API.
 *
 * Use receive() to pull messages sequentially: co_await consumer.receive()
 * suspends until a message arrives. Returns std::nullopt when the channel
 * is closed (e.g. disconnected).
 *
 * @code
 * co_await consumer.subscribe("mychan");
 * while (auto msg = co_await consumer.receive()) {
 *     process(*msg);
 * }
 * @endcode
 *
 * The internal message queue defaults to DEFAULT_MSG_CAPACITY slots.
 * Pass a larger capacity to the URI constructor if bursty pub/sub can
 * outpace your receive loop. Optional on_message_dropped() reports drops
 * when the buffer is full (otherwise a warning is logged).
 */
template <typename QB_IO_>
class RedisCoroConsumer
    : public RedisConsumer<QB_IO_, RedisCoroConsumer<QB_IO_>> {
    friend RedisConsumer<QB_IO_, RedisCoroConsumer<QB_IO_>>;

    /// Default buffered capacity for co_await receive() (tune for burst tolerance).
    static constexpr size_t DEFAULT_MSG_CAPACITY = 8192;

    using message_drop_callback = std::function<void(qb::redis::message &&)>;

    qb::io::async::channel<qb::redis::message> _msg_channel{DEFAULT_MSG_CAPACITY};
    message_drop_callback _on_message_dropped;

    void enqueue_pubsub_message(qb::redis::message &&msg) {
        if (_msg_channel.try_send(std::move(msg))) {
            return;
        }
        if (_on_message_dropped) {
            try {
                _on_message_dropped(std::move(msg));
            } catch (const std::exception &ex) {
                LOG_WARN("[qbm][redis] coro consumer on_message_dropped error: " << ex.what());
            }
        } else {
            LOG_WARN("[qbm][redis] coro consumer: message dropped (buffer full)");
        }
    }

    void on(qb::redis::message &&msg) {
        enqueue_pubsub_message(std::move(msg));
    }

    void on(qb::redis::pmessage &&msg) {
        // pmessage inherits from message; store as message (pattern is in base)
        qb::redis::message m = std::move(msg);
        enqueue_pubsub_message(std::move(m));
    }

    void on(qb::io::async::event::disconnected &&e) {
        _msg_channel.close();
        // Base already cleared _replies and invoked us; no need to re-enter
    }

public:
    RedisCoroConsumer() = default;

    explicit RedisCoroConsumer(qb::io::uri uri,
                               size_t message_channel_capacity = DEFAULT_MSG_CAPACITY)
        : RedisConsumer<QB_IO_, RedisCoroConsumer<QB_IO_>>(std::move(uri))
        , _msg_channel(message_channel_capacity) {}

    /**
     * @brief Optional callback when the internal queue is full and a message is dropped.
     * @return *this
     */
    RedisCoroConsumer &on_message_dropped(message_drop_callback cb) {
        _on_message_dropped = std::move(cb);
        return *this;
    }

    /** @brief Configured capacity of the internal pub/sub message queue. */
    [[nodiscard]] size_t message_channel_capacity() const noexcept {
        return _msg_channel.capacity();
    }

    /**
     * @brief Receive the next pub/sub message (coroutine awaitable).
     * @return std::optional<message> - has value when a message arrived,
     *         nullopt when the channel is closed (disconnected).
     */
    [[nodiscard]] auto receive() {
        return [this]() -> qb::io::async::task<std::optional<qb::redis::message>> {
            co_return co_await _msg_channel.recv();
        }();
    }

    ~RedisCoroConsumer() {
        _msg_channel.close();
    }
};

} // namespace detail

// ============================================================================
// Type aliases
// ============================================================================

/** @brief Alias for the main Redis client */
template <typename QB_IO_>
using database = detail::Redis<QB_IO_>;

/**
 * @struct tcp
 * @brief TCP transport type aliases for Redis client and consumers
 */
struct tcp {
    using client = detail::Redis<qb::io::transport::tcp>;
    /** @brief Callback pipelining helper; see detail::RedisPipeline */
    using pipeline = detail::RedisPipeline<qb::io::transport::tcp>;
    
    template <typename Derived>
    using consumer = detail::RedisConsumer<qb::io::transport::tcp, Derived>;
    
    using cb_consumer = detail::RedisCallbackConsumer<qb::io::transport::tcp>;
    using co_consumer = detail::RedisCoroConsumer<qb::io::transport::tcp>;

#ifdef QB_HAS_SSL
    struct ssl {
        using client = detail::Redis<qb::io::transport::stcp>;
        using pipeline = detail::RedisPipeline<qb::io::transport::stcp>;
        
        template <typename Derived>
        using consumer = detail::RedisConsumer<qb::io::transport::stcp, Derived>;
        
        using cb_consumer = detail::RedisCallbackConsumer<qb::io::transport::stcp>;
        using co_consumer = detail::RedisCoroConsumer<qb::io::transport::stcp>;
    };
#endif
};

inline constexpr auto no_check = [](auto &&) {};

} // namespace qb::redis

#endif // QBM_REDIS_H
