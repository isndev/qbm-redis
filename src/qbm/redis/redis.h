/**
 * @file qbm/redis/redis.h
 * @brief Native Redis client, RESP protocol, connector, and Pub/Sub consumers
 *
 * This header assembles the public Redis client surface of the qbm-redis module on
 * top of the qb-io asynchronous runtime. It provides:
 *
 * - A native C++20/23 RESP2/RESP3 protocol codec (`qb::protocol::redis`) that feeds
 *   the streaming parser from `parser.h` and dispatches fully-parsed replies.
 * - A reusable CRTP TCP `connector` with connection lifecycle, optional TLS peer
 *   verification, coroutine-awaitable `connect()`, and exponential-backoff
 *   auto-reconnect (`RetryPolicy`).
 * - The full-featured `Redis` client, which inherits every `*_commands` mixin and
 *   exposes both callback and coroutine command APIs, pipelining, and an optional
 *   connection-health command deadline.
 * - Pub/Sub consumers: a callback consumer (`RedisCallbackConsumer`) and a
 *   coroutine `receive()` consumer (`RedisCoroConsumer`).
 *
 * All public types here are class templates parameterized on the qb-io transport
 * (e.g. `qb::io::transport::tcp` / `stcp`); the `qb::redis::tcp` aggregate provides
 * ready-made aliases.
 *
 * @warning The `Redis` client is not thread-safe; drive it from a single qb-io event
 *          loop / strand.
 *
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2026 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)
 * @ingroup Redis
 */
#ifndef QBM_REDIS_H
#define QBM_REDIS_H

#include <deque>
#include <functional>
#include <queue>
#include <random>
#include <utility>
#include <qb/io/async.h>
#include <qb/io/async/tcp/connector.h>
#ifdef QB_HAS_SSL
#include <qb/io/tcp/ssl/context.h> // qb::io::ssl::Context for make_connect_socket_() (rediss:// custom-CA / client-cert)
#endif
#include <qb/system/container/unordered_set.h>

// Native Redis Protocol Parser (C++20/23)
#include "parser.h"
#include "reply.h"

// Command traits
#include "commands/acl_commands.h"
#include "commands/bitmap_commands.h"
#include "commands/cluster_commands.h"
#include "commands/connection_commands.h"
#include "commands/function_commands.h"
#include "commands/geo_commands.h"
#include "commands/hash_commands.h"
#include "commands/hyperloglog_commands.h"
#include "commands/key_commands.h"
#include "commands/list_commands.h"
#include "commands/module_commands.h"
#include "commands/publish_commands.h"
#include "commands/scripting_commands.h"
#include "commands/server_commands.h"
#include "commands/set_commands.h"
#include "commands/sorted_set_commands.h"
#include "commands/stream_commands.h"
#include "commands/string_commands.h"
#include "commands/subscription_commands.h"
#include "commands/transaction_commands.h"

namespace qb::protocol {

/**
 * @class redis
 * @brief Native C++20/23 Redis protocol implementation
 *
 * Uses the world-class native Redis Protocol Parser with full RESP2/RESP3 support.
 * Zero-copy parsing where possible, streaming-capable for async I/O.
 * @tparam IO_ The underlying I/O connector type (CRTP)
 */
template <typename IO_>
class redis final : public qb::io::async::AProtocol<IO_> {
    constexpr IO_ &
    derived() {
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
              .protocol_version  = qb::redis::parser::ProtocolVersion::RESP3,
              .max_nesting_depth = 64,
              .max_bulk_size     = 512 * 1024 * 1024,
              .max_array_size    = 1'000'000
          }) {}

    ~redis() = default;

    std::size_t
    getMessageSize() noexcept final {
        const size_t current_size = this->_io.in().size();

        // Feed only the bytes that arrived since our last call so we never
        // double-feed the same bytes into the parser's internal buffer.
        if (current_size > _fed_bytes) {
            const auto new_data =
                std::span<const char>(this->_io.in().begin() + static_cast<std::ptrdiff_t>(_fed_bytes), current_size - _fed_bytes);

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

            // A fatal protocol error faults the parser: tear the connection down
            // instead of looping forever on a corrupt byte the parser cannot
            // advance past.
            if (qb__unlikely(_parser.has_error())) {
                this->not_ok();
                _pending_messages.clear();
                _fed_bytes = 0;
                return 0;
            }
        }

        // Return the number of bytes currently in _io.in() so the framework
        // knows how much to flush after onMessage().  We return non-zero only
        // when at least one complete value has been parsed and is waiting.
        return _pending_messages.empty() ? 0 : current_size;
    }

    void
    onMessage(std::size_t) noexcept final {
        if (!this->ok())
            return;

        // The framework is about to flush current_size bytes from _io.in().
        // Reset the offset so the next getMessageSize() feeds from 0.
        _fed_bytes = 0;

        // Dispatch every pending parsed reply to the owning IO handler.
        while (!_pending_messages.empty()) {
            auto reply_ptr = std::make_unique<qb::redis::parser::Value>(std::move(_pending_messages.front()));
            _pending_messages.pop_front();
            // noexcept boundary: an exception escaping here would std::terminate. The
            // per-handler dispatch already catches std::exception gracefully; this is a
            // backstop for anything it misses (e.g. a user callback that throws a
            // non-std type) so one bad reply cannot crash the process, and the remaining
            // pending replies still dispatch.
            try {
                this->_io.on(message{std::move(reply_ptr)});
            } catch (...) {
                QB_LOG_WARN("[qbm][redis] message handler threw a non-std exception; reply dropped");
            }
        }

        // Reclaim memory from the parser's internal buffer.
        _parser.compact();
    }

    void
    reset() noexcept final {
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
    int                                                       max_attempts = -1;
    qb::duration                                              initial_delay{std::chrono::milliseconds(100)};
    qb::duration                                              max_delay{std::chrono::seconds(30)};
    double                                                    multiplier      = 2.0;
    bool                                                      jitter          = true;
    qb::duration                                              connect_timeout = std::chrono::seconds(3);
    std::function<void(int attempt, qb::duration next_delay)> on_retry;

    /** @brief Set maximum number of retry attempts (-1 = unlimited) */
    RetryPolicy &
    with_max_attempts(int n) noexcept {
        max_attempts = n;
        return *this;
    }
    /** @brief Set initial delay between retries */
    RetryPolicy &
    with_initial_delay(qb::duration d) noexcept {
        initial_delay = d;
        return *this;
    }
    /** @brief Set maximum delay cap */
    RetryPolicy &
    with_max_delay(qb::duration d) noexcept {
        max_delay = d;
        return *this;
    }
    /** @brief Set exponential backoff multiplier */
    RetryPolicy &
    with_multiplier(double m) noexcept {
        multiplier = m;
        return *this;
    }
    /** @brief Enable/disable random jitter on delays */
    RetryPolicy &
    with_jitter(bool j) noexcept {
        jitter = j;
        return *this;
    }
    /** @brief Set connection timeout */
    RetryPolicy &
    with_connect_timeout(qb::duration t) noexcept {
        connect_timeout = t;
        return *this;
    }
    /** @brief Set callback invoked before each retry (attempt, next_delay) */
    RetryPolicy &
    with_on_retry(std::function<void(int, qb::duration)> cb) {
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
class connector : public qb::io::async::tcp::client<connector<QB_IO_, Derived>, QB_IO_, void> {
    friend struct has_method_on<connector<QB_IO_, Derived>, void, qb::io::async::event::disconnected>;
    friend class qb::io::async::io<connector<QB_IO_, Derived>>;
    friend class qb::protocol::redis<connector<QB_IO_, Derived>>;

    constexpr Derived &
    derived() {
        return static_cast<Derived &>(*this);
    }

public:
    using redis_protocol = qb::protocol::redis<connector<QB_IO_, Derived>>;

private:
    qb::io::uri                _uri;
    std::optional<RetryPolicy> _reconnect_policy;
    bool                       _is_reconnecting = false;
    bool                       _connected_flag  = false;
    bool                       _verify_peer     = true; /**< Verify server TLS cert for rediss:// (stcp transport). */
    std::string                _ssl_root_cert; /**< rediss://: private CA (PEM file/dir) added to the trust store. Empty = system store. */
    std::string                _ssl_cert;      /**< rediss://: client certificate (PEM) for mutual TLS. Empty = none. */
    std::string                _ssl_key;       /**< rediss://: client private key (PEM) for mutual TLS. Pairs with _ssl_cert. */
    std::shared_ptr<bool>      _alive{std::make_shared<bool>(true)};

    /**
     * @brief Build the transport socket for a new connection.
     * @details For a secure (`rediss://`) transport, mints it from a value-semantic `ssl::Context` carrying
     *          the verify mode + optional private CA (`_ssl_root_cert`) and client certificate
     *          (`_ssl_cert`/`_ssl_key`, mTLS); for a plain transport, a default socket. Handed to the
     *          existing-socket `connect()` overload so the Context — not a bare `verify` bool — governs
     *          verification (a broken Context fails CLOSED at connect, never a silent downgrade).
     */
    typename QB_IO_::transport_io_type
    make_connect_socket_() const {
        typename QB_IO_::transport_io_type sock;
#ifdef QB_HAS_SSL
        if constexpr (std::is_constructible_v<typename QB_IO_::transport_io_type, qb::io::ssl::Context>) {
            auto tls = qb::io::ssl::Context::client();
            if (!_verify_peer)
                tls.verify(qb::io::ssl::VerifyMode::none);
            if (!_ssl_root_cert.empty())
                tls.trust(_ssl_root_cert);
            if (!_ssl_cert.empty() && !_ssl_key.empty())
                tls.identity(_ssl_cert, _ssl_key);
            sock = typename QB_IO_::transport_io_type{std::move(tls)};
        }
#endif
        return sock;
    }

    void
    start_async() {
        this->clear_protocols(); // idempotent: drops any prior protocol, resets to the NoProtocol sentinel
        this->reset_io_state();
        this->template switch_protocol<redis_protocol>(*this);
        this->start();
        _connected_flag = true;
    }

    void
    on(typename redis_protocol::message msg) {
        derived().on(std::move(msg));
    }

    void
    on(qb::io::async::event::disconnected &&ev) {
        _connected_flag = false;
        QB_LOG_WARN("[qbm][redis] disconnected");
        derived().on(std::forward<qb::io::async::event::disconnected>(ev));

        if (_reconnect_policy && !_is_reconnecting) {
            _is_reconnecting = true;
            qb::io::async::coro_scheduler().spawn(_reconnect_task(*_reconnect_policy, _alive));
        }
    }

    qb::io::async::task<void>
    _reconnect_task(RetryPolicy policy, std::shared_ptr<bool> alive) {
        QB_LOG_INFO("[qbm][redis] auto-reconnect starting...");

        const bool ok = co_await connect_with_retry(policy);

        if (!*alive)
            co_return;

        _is_reconnecting = false;

        if (ok) {
            QB_LOG_INFO("[qbm][redis] auto-reconnect succeeded");
        } else {
            QB_LOG_WARN("[qbm][redis] auto-reconnect failed");
        }
    }

protected:
    connector() = default;
    ~connector() {
        *_alive = false;
    }

    explicit connector(qb::io::uri uri)
        : _uri{std::move(uri)} {}

    /**
     * @brief Liveness token shared with deferred work (timers, watchers).
     *
     * Captured by value before any suspension so a callback that fires after
     * the client has been destroyed can detect it and no-op instead of
     * touching freed memory. Set to `false` by ~connector().
     */
    [[nodiscard]] std::shared_ptr<bool>
    connector_alive() const noexcept {
        return _alive;
    }

public:
    /**
     * @struct connect_awaiter
     * @brief Coroutine awaiter for async connection (co_await connect())
     */
    struct connect_awaiter {
        connector              &_client;
        qb::duration            _timeout;
        bool                    _connected = false;
        std::coroutine_handle<> _handle;
        bool                    _ready = false;
        std::shared_ptr<bool>   _valid{std::make_shared<bool>(true)};

        explicit connect_awaiter(connector &client, qb::duration timeout = std::chrono::seconds(3))
            : _client(client)
            , _timeout(timeout) {}

        ~connect_awaiter() {
            if (_valid)
                *_valid = false;
        }

        bool
        await_ready() const noexcept {
            return _ready;
        }

        void
        await_suspend(std::coroutine_handle<> h) {
            _handle    = h;
            auto valid = _valid;
            // Capture the CLIENT's liveness too, not just the awaiter's. The
            // auto-reconnect task runs detached: it owns this awaiter on its coroutine
            // frame, so the connector can be destroyed while this connect is in flight
            // with the awaiter still alive (_valid stays true). Touching _client then
            // would be a use-after-free. If the client is gone we skip setup_connection
            // and still resume — connect_with_retry's own `!*alive` check then makes the
            // detached task exit cleanly instead of leaking.
            auto client_alive = _client.connector_alive();
            ::qb::io::async::tcp::connect<typename QB_IO_::transport_io_type>(
                _client.make_connect_socket_(), _client._uri,
                [this, valid, client_alive](auto &&raw_io) {
                    if (!*valid)
                        return;
                    if (*client_alive && raw_io.is_open()) {
                        _connected = _client.setup_connection(_client._uri, std::move(raw_io));
                    }
                    _ready = true;
                    if (_handle) {
                        ::qb::io::async::coro_scheduler().schedule_resume(_handle);
                    }
                },
                _timeout);
        }

        bool
        await_resume() const noexcept {
            return _connected;
        }
    };

    /** @brief Start async connection (co_awaitable) */
    connect_awaiter
    connect() {
        return connect_awaiter{*this};
    }
    connect_awaiter
    connect(qb::io::uri uri) {
        _uri = std::move(uri);
        return connect_awaiter{*this};
    }
    connect_awaiter
    connect(qb::duration timeout) {
        return connect_awaiter{*this, timeout};
    }
    connect_awaiter
    connect(qb::io::uri uri, qb::duration timeout) {
        _uri = std::move(uri);
        return connect_awaiter{*this, timeout};
    }

    void
    set_uri(qb::io::uri uri) noexcept {
        _uri = std::move(uri);
    }

    /**
     * @brief Coroutine connect with exponential-backoff retry.
     *
     * Repeatedly attempts to connect to the current URI until success or the
     * policy's attempt budget is exhausted, sleeping `RetryPolicy`-controlled
     * (optionally jittered) delays between attempts. Liveness-safe: if the client
     * is destroyed while suspended, the task exits cleanly without touching `*this`.
     *
     * @param policy Retry/backoff configuration (defaults: unlimited attempts).
     * @return A task yielding `true` on a successful connection, `false` otherwise.
     */
    qb::io::async::task<bool>
    connect_with_retry(RetryPolicy policy = RetryPolicy{}) {
        static thread_local std::mt19937 rng_{std::random_device{}()};

        // Capture the liveness flag by value BEFORE any suspension: when this
        // runs from the auto-reconnect task, the client may be destroyed while
        // we sleep between attempts. After each co_await we must re-check it
        // before touching *this again (the next connect_awaiter{*this}).
        auto alive = _alive;

        auto delay   = policy.initial_delay;
        int  attempt = 0;

        while (policy.max_attempts < 0 || attempt < policy.max_attempts) {
            ++attempt;

            if (co_await connect_awaiter{*this, policy.connect_timeout})
                co_return true;
            if (!*alive)
                co_return false; // *this may have been destroyed

            if (policy.max_attempts > 0 && attempt >= policy.max_attempts)
                break;

            // Backoff math is done in integer milliseconds (jitter + exponential
            // growth), then assigned back into the qb::duration delay variables.
            const auto   delay_ms    = std::chrono::duration_cast<std::chrono::milliseconds>(delay).count();
            qb::duration sleep_delay = delay;
            if (policy.jitter && delay_ms > 0) {
                const long long                          quarter = delay_ms / 4;
                std::uniform_int_distribution<long long> dist{-quarter, quarter};
                sleep_delay =
                    std::max(qb::duration{std::chrono::milliseconds{1}}, qb::duration{std::chrono::milliseconds{delay_ms + dist(rng_)}});
            }

            if (policy.on_retry) {
                policy.on_retry(attempt, sleep_delay);
            }

            co_await qb::io::async::sleep(sleep_delay);
            if (!*alive)
                co_return false; // destroyed during the backoff sleep

            const auto next_ms = static_cast<long long>(static_cast<double>(delay_ms) * policy.multiplier);
            delay              = std::min(qb::duration{std::chrono::milliseconds{next_ms}}, policy.max_delay);
        }

        co_return false;
    }

    qb::io::async::task<bool>
    connect_with_retry(qb::io::uri uri, RetryPolicy policy = RetryPolicy{}) {
        _uri = std::move(uri);
        co_return co_await connect_with_retry(std::move(policy));
    }

    template <std::invocable<bool> Func>
    void
    connect(Func &&func, qb::io::uri uri, qb::duration timeout = std::chrono::seconds(3)) {
        // qb::io::async::tcp::connect self-holds this completion lambda in a shared_ptr<connector>
        // that lives until the connect resolves or its deadline (default 3s) fires — a LATER
        // event-loop turn. The lambda captures the client by raw `this`, so a client destroyed while
        // the connect is in flight would have the deferred completion dereference freed memory
        // (setup_connection writes _connected_flag/_uri/this->transport() and starts the watchers).
        // Capture the client liveness token — ~connector sets *_alive=false — and no-op if it died,
        // exactly like the coroutine connect_awaiter above (that path was hardened in 4833dc7; this
        // sibling overload was missed).
        auto alive = connector_alive();
        qb::io::async::tcp::connect<typename QB_IO_::transport_io_type>(
            make_connect_socket_(), uri,
            [this, alive, uri, func = std::forward<Func>(func)](auto &&raw_io) {
                if (!*alive)
                    return;
                if (raw_io.is_open()) {
                    func(this->setup_connection(uri, std::forward<decltype(raw_io)>(raw_io)));
                } else {
                    func(false);
                }
            },
            timeout);
    }

    template <std::invocable<bool> Func>
    void
    connect(Func &&func, qb::duration timeout = std::chrono::seconds(3)) {
        connect(std::forward<Func>(func), _uri, timeout);
    }

    [[nodiscard]] qb::io::uri const &
    uri() const noexcept {
        return _uri;
    }

    /**
     * @brief Enable/disable TLS server certificate verification for rediss://.
     * @param value `true` (default) verifies chain + hostname; `false` disables it
     *              (trusted/self-signed endpoints only). Set before connect().
     */
    void
    set_verify_peer(bool value) noexcept {
        _verify_peer = value;
    }
    [[nodiscard]] bool
    verify_peer() const noexcept {
        return _verify_peer;
    }

    /**
     * @brief Trust a private CA (PEM file or directory) for `rediss://`, IN ADDITION to the system store,
     *        so `verify_peer(true)` can validate a server certificate issued by an internal CA (libpq-style
     *        `sslrootcert`). Set before connect(); empty (default) = system trust store only.
     */
    void
    set_ssl_root_cert(std::string ca_file_or_dir) {
        _ssl_root_cert = std::move(ca_file_or_dir);
    }

    /**
     * @brief Present a client certificate + private key (PEM) for mutual TLS on `rediss://` — both are
     *        required to take effect. Set before connect(); empty (default) = no client certificate.
     */
    void
    set_ssl_client_certificate(std::string cert_file, std::string key_file) {
        _ssl_cert = std::move(cert_file);
        _ssl_key  = std::move(key_file);
    }

    /**
     * @brief Adopt an already-opened transport socket and start the protocol.
     *
     * Installs the RESP protocol and begins async I/O on `raw_io`. No-ops (returns
     * `false`) if a connection is already active, guarding against double setup.
     *
     * @param uri    URI associated with this connection (stored).
     * @param raw_io An opened transport socket to take ownership of.
     * @return `true` if the connection was set up, `false` if already connected.
     */
    bool
    setup_connection(qb::io::uri uri, typename QB_IO_::transport_io_type &&raw_io) {
        if (_connected_flag)
            return false;
        _uri              = std::move(uri);
        this->transport() = std::move(raw_io);
        start_async();
        return true;
    }

    /**
     * @brief Enable automatic reconnection on unexpected disconnect.
     * @param policy Retry/backoff policy used by the detached reconnect task.
     */
    void
    enable_auto_reconnect(RetryPolicy policy = RetryPolicy{}) noexcept {
        _reconnect_policy = std::move(policy);
    }

    /** @brief Disable automatic reconnection (in-flight reconnects still finish). */
    void
    disable_auto_reconnect() noexcept {
        _reconnect_policy.reset();
    }

    [[nodiscard]] bool
    is_reconnecting() const noexcept {
        return _is_reconnecting;
    }
    [[nodiscard]] bool
    is_connected() const noexcept {
        return _connected_flag;
    }

    void
    disconnect() noexcept {
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
    Reply<T>                result_;
    std::coroutine_handle<> handle_;
    std::shared_ptr<bool>   valid_{std::make_shared<bool>(true)};
    Operation               operation_;

public:
    explicit redis_awaiter(Operation &&op)
        : operation_(std::forward<Operation>(op)) {}

    ~redis_awaiter() {
        if (valid_)
            *valid_ = false;
    }

    bool
    await_ready() const noexcept {
        return false;
    }

    void
    await_suspend(std::coroutine_handle<> h) {
        handle_    = h;
        auto valid = valid_;
        operation_([this, valid](Reply<T> &&reply) {
            if (!*valid)
                return;
            result_ = std::move(reply);
            qb::io::async::coro_scheduler().schedule_resume(handle_);
        });
    }

    [[nodiscard]] Reply<T>
    await_resume() {
        return std::move(result_);
    }
};

/** @brief Factory for redis_awaiter from a callback-based operation */
template <typename T, typename Func>
[[nodiscard]] auto
make_redis_awaiter(Func &&operation) {
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
    /**
     * @struct PendingReply
     * @brief One in-flight command awaiting its positional RESP reply.
     *
     * `blocking` marks commands that park server-side with their own timeout
     * (BLPOP, WAIT, …); the client-side command deadline is suspended while any
     * such command is in flight so it is never spuriously dropped.
     */
    struct PendingReply {
        std::unique_ptr<IReply> handler;
        bool                    blocking;
    };

    std::queue<PendingReply> _replies;

    // ---- Optional command deadline (opt-in via set_command_timeout) --------
    //
    // Implemented as a detached watcher coroutine that co_await sleeps for one
    // window then trips iff no reply landed meanwhile. A coroutine resumes in
    // the listener's coroutine-drain phase — after `_loop.run()`, outside libev
    // event dispatch — so the disconnect it issues on a stall is processed
    // through the normal deferred-dispose path. A generation token cancels a
    // still-sleeping watcher (its wake-up becomes a no-op) and a progress
    // counter distinguishes "no reply at all" from "replies flowing", so a
    // busy pipeline is never tripped.
    /// 0 disables the deadline (default — preserves park-forever semantics off).
    qb::duration _command_timeout{};
    /// Count of in-flight blocking commands; >0 suspends the deadline.
    int _inflight_blocking{0};
    /// True while a deadline watcher coroutine is live (avoids piling them up).
    bool _deadline_armed{false};
    /// Bumped to cancel/supersede a sleeping watcher without touching it.
    std::size_t _deadline_gen{0};
    /// Incremented on every reply; lets a woken watcher detect forward progress.
    std::size_t _reply_progress{0};
    /// Set when the deadline tripped, so on(disconnected) fails pending commands
    /// with a "command timed out" reason instead of the generic "disconnected".
    bool _deadline_tripped{false};

    /**
     * @brief Commands that legitimately block server-side (own timeout).
     *
     * The client deadline must not fire while one is pending, so they are
     * excluded from deadline accounting. XREAD/XREADGROUP block only with the
     * BLOCK option; treating them as blocking is conservative (less protection,
     * never a false drop).
     */
    [[nodiscard]] static bool
    is_blocking_command(std::string_view name) noexcept {
        static const qb::unordered_flat_set<std::string_view> blocking{"BLPOP",    "BRPOP",  "BLMOVE", "BLMPOP",  "BRPOPLPUSH", "BZPOPMIN",
                                                                       "BZPOPMAX", "BZMPOP", "WAIT",   "WAITAOF", "XREAD",      "XREADGROUP"};
        return blocking.find(name) != blocking.end();
    }

    /** @brief Arm the deadline watcher if one is warranted and not already live. */
    void
    arm_deadline() {
        if (_deadline_armed)
            return;
        if (_command_timeout <= qb::duration::zero() || _replies.empty() || _inflight_blocking > 0)
            return;
        _deadline_armed = true;
        qb::io::async::coro_scheduler().spawn(deadline_watch(++_deadline_gen, this->connector_alive()));
    }

    /**
     * @brief Detached watcher: sleeps one window, then trips iff no reply landed.
     *
     * Member coroutine with value parameters — both are copied into the frame,
     * and `alive` guards every touch of `*this` after the suspension. Exits
     * quietly when cancelled (generation bump), disabled, idle, or while a
     * blocking command holds the connection; re-arms while replies keep flowing.
     */
    qb::io::async::task<void>
    deadline_watch(std::size_t gen, std::shared_ptr<bool> alive) {
        const std::size_t snapshot = _reply_progress;
        co_await qb::io::async::sleep(_command_timeout);
        if (!*alive || gen != _deadline_gen)
            co_return; // client destroyed, or cancelled/superseded
        _deadline_armed = false;
        if (_command_timeout <= qb::duration::zero() || _replies.empty() || _inflight_blocking > 0)
            co_return; // nothing in flight to time out
        if (_reply_progress != snapshot) {
            arm_deadline(); // replies are flowing — re-arm rather than trip
            co_return;
        }
        on_command_deadline();
    }

    /** @brief Cancel a pending deadline watcher (its wake-up becomes a no-op). */
    void
    cancel_deadline() {
        ++_deadline_gen;
        _deadline_armed = false;
    }

    /**
     * @brief Deadline elapsed with no reply: the connection is unhealthy.
     *
     * A FIFO pipelined protocol cannot time out a single mid-queue command
     * without desynchronizing every later reply, so the correct action is to
     * drop the connection (auto-reconnect takes over if enabled). disconnect()
     * defers the teardown to the io watcher's own callback, where
     * on(disconnected) fails every pending command with the timeout reason —
     * we deliberately do NOT resolve awaiters here.
     */
    void
    on_command_deadline() {
        QB_LOG_WARN("[qbm][redis] command timeout (" << qb::detail::to_ev_seconds(_command_timeout)
                                                     << "s) exceeded with no reply; dropping connection");
        _deadline_tripped = true;
        this->disconnect();
    }

    template <typename... Args>
    void
    _command(Args &&...args) {
        this->ready_to_write();
        put_in_pipe(this->out(), std::forward<Args>(args)...);
    }

    void
    on(typename redis_protocol::message msg) {
        // RESP3 PUSH frames (client-side-caching invalidations, server pushes)
        // are out-of-band: they must NOT pop a command-reply handler, or the
        // reply/command FIFO desynchronizes permanently. The plain client does
        // not consume pushes, so discard them. (RedisConsumer overrides this to
        // route pub/sub messages.)
        if (msg.reply && reply::is_push(*msg.reply)) {
            return;
        }
        if (_replies.empty()) {
            QB_LOG_WARN("[qbm][redis] Received unsolicited reply with no pending command, discarding");
            return;
        }
        auto entry = std::move(_replies.front());
        _replies.pop();
        if (entry.blocking && _inflight_blocking > 0)
            --_inflight_blocking;
        // Record forward progress so a pending deadline watcher re-arms instead
        // of tripping, and arm a fresh one if more commands are still in flight.
        ++_reply_progress;
        arm_deadline();
        // Transfer ownership of reply to handler. Guard against a throwing
        // transform/user callback: this runs inside the libev read dispatch, so
        // an escaping exception would cross a noexcept boundary and terminate.
        try {
            (*entry.handler)(std::move(msg.reply));
        } catch (const std::exception &ex) {
            QB_LOG_WARN("[qbm][redis] reply handler error: " << ex.what());
        }
    }

    void
    on(qb::io::async::event::disconnected &&) {
        QB_LOG_WARN("[qbm][redis] disconnected by remote");
        _inflight_blocking = 0;
        cancel_deadline();
        // If the command deadline tripped, report it as a timeout rather than a
        // plain disconnect so callers can distinguish a slow/dead peer.
        const bool timed_out = _deadline_tripped;
        _deadline_tripped    = false;
        // Swap the queue out before failing: a failing callback may legitimately
        // re-issue a command (e.g. trigger a reconnect + retry), and that brand
        // new command must NOT be failed by this same drain loop.
        std::queue<PendingReply> pending;
        std::swap(pending, _replies);
        while (!pending.empty()) {
            auto entry = std::move(pending.front());
            pending.pop();
            try {
                if (timed_out)
                    entry.handler->fail("command timed out");
                else
                    (*entry.handler)(nullptr);
            } catch (const std::exception &ex) {
                QB_LOG_WARN("[qbm][redis] callback error: " << ex.what());
            } catch (...) {
                // on(disconnected) runs from dispose() under the libev C callback — a
                // non-std exception escaping here would terminate the process.
                QB_LOG_WARN("[qbm][redis] callback threw a non-std exception");
            }
        }
        transaction_commands<Redis<QB_IO_>>::reset_transaction_state();
    }

public:
    Redis() = default;
    explicit Redis(qb::io::uri uri)
        : connector<QB_IO_, Redis<QB_IO_>>(std::move(uri)) {}

    /**
     * @brief Send a raw command with a callback invoked on its reply (callback API).
     *
     * Registers the reply handler before the bytes are written so a synchronous
     * delivery cannot outrun handler registration, then enqueues the command on the
     * outbound pipe. Multiple calls pipeline naturally (one handler + one request
     * per call, FIFO). Drain with await() or your event loop.
     *
     * @tparam Ret  Expected decoded reply type for `Reply<Ret>`.
     * @param func  Callback invoked with `Reply<Ret>&&` on success, Redis error, or
     *              disconnect.
     * @param name  Redis command verb (used for blocking-command detection).
     * @param args  Command arguments, serialized in order.
     * @return *this, for fluent chaining.
     */
    template <typename Ret, typename Func, typename... Args>
    requires std::invocable<Func, Reply<Ret> &&>
    Redis &
    command(Func &&func, std::string const &name, Args &&...args) {
        // Register the reply handler before sending so a very fast/synchronous
        // delivery cannot run before the handler is queued (pipeline-safe).
        const bool blocking = is_blocking_command(name);
        _replies.push(PendingReply{std::make_unique<TReply<Func, Ret>>(std::forward<Func>(func)), blocking});
        if (blocking)
            ++_inflight_blocking;
        _command(name, std::forward<Args>(args)...);
        arm_deadline();
        return *this;
    }

    /**
     * @brief Send a raw command and await its reply as a coroutine (co_await API).
     *
     * @tparam Ret  Expected decoded reply type for `Reply<Ret>`.
     * @param name  Redis command verb.
     * @param args  Command arguments, serialized in order.
     * @return A redis_awaiter yielding `Reply<Ret>` when the command completes.
     */
    template <typename Ret, typename... Args>
    auto
    command(std::string const &name, Args &&...args) {
        return make_coro_command<Ret>([this, name, args_tuple = std::make_tuple(std::forward<Args>(args)...)](auto &&callback) mutable {
            std::apply(
                [this, &callback, &name](auto &&...a) { this->command<Ret>(std::move(callback), name, std::forward<decltype(a)>(a)...); },
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
    Redis &
    await() {
        while (!_replies.empty())
            qb::io::async::listener::current.run(EVRUN_NOWAIT);
        return *this;
    }

    /** @brief Number of commands sent awaiting a Redis reply (for pipeline debugging). */
    [[nodiscard]] std::size_t
    pending_reply_count() const noexcept {
        return _replies.size();
    }

    /**
     * @brief Set an optional per-connection command deadline (seconds).
     *
     * When >0, if no reply arrives for any in-flight, non-blocking command
     * within this window, the connection is dropped and every pending command
     * fails with "command timed out" (auto-reconnect resumes if enabled). This
     * is a connection-health watchdog, not a per-command timer: a FIFO
     * pipelined protocol cannot fail one mid-queue command without
     * desynchronizing later replies. Blocking commands (BLPOP, WAIT, XREAD, …)
     * suspend the deadline so their server-side timeout governs instead.
     *
     * @param timeout Command deadline as a `qb::duration`; zero (default) disables it.
     */
    void
    set_command_timeout(qb::duration timeout) noexcept {
        _command_timeout = timeout > qb::duration::zero() ? timeout : qb::duration::zero();
        if (_command_timeout <= qb::duration::zero())
            cancel_deadline();
        else
            arm_deadline();
    }

    /** @brief Current command deadline (`qb::duration::zero()` = disabled). */
    [[nodiscard]] qb::duration
    command_timeout() const noexcept {
        return _command_timeout;
    }

    template <typename T, typename Func>
    [[nodiscard]] auto
    make_coro_command(Func &&operation) {
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

    [[nodiscard]] Redis<QB_IO_> &
    client() noexcept {
        return _client;
    }
    [[nodiscard]] Redis<QB_IO_> const &
    client() const noexcept {
        return _client;
    }

    /** @brief Same as `client().pending_reply_count()`. */
    [[nodiscard]] std::size_t
    pending_reply_count() const noexcept {
        return _client.pending_reply_count();
    }

    template <typename Ret, typename Func, typename... Args>
    requires std::invocable<Func, Reply<Ret> &&>
    RedisPipeline &
    command(Func &&func, std::string const &name, Args &&...args) {
        _client.template command<Ret>(std::forward<Func>(func), name, std::forward<Args>(args)...);
        return *this;
    }

    /** @brief Drain pending replies (calls `client().await()`). */
    Redis<QB_IO_> &
    flush() {
        return _client.await();
    }
};

// ============================================================================
// Redis Consumer (Pub/Sub)
// ============================================================================

/**
 * @class RedisConsumer
 * @brief CRTP base for Redis Pub/Sub consumers
 *
 * Drives the RESP message loop for subscription-style connections: it tracks
 * predicted subscription state, reconciles the per-channel confirmation frames a
 * single (P)SUBSCRIBE/(P)UNSUBSCRIBE produces, and routes out-of-band MESSAGE /
 * PMESSAGE frames to the derived consumer. Concrete consumers
 * (`RedisCallbackConsumer`, `RedisCoroConsumer`) derive from this via CRTP and
 * implement the `on(message&&)` / `on(error&&)` / `on(disconnected&&)` sinks.
 *
 * @tparam QB_IO_   I/O transport type (e.g. qb::io::transport::tcp)
 * @tparam Derived  CRTP derived consumer type
 */
template <typename QB_IO_, typename Derived>
class RedisConsumer
    : public connector<QB_IO_, RedisConsumer<QB_IO_, Derived>>
    , public connection_commands<Derived>
    , public subscription_commands<Derived> {
    friend class connector<QB_IO_, RedisConsumer<QB_IO_, Derived>>;
    friend class connection_commands<Derived>;
    friend class subscription_commands<Derived>;

    constexpr Derived &
    derived() {
        return static_cast<Derived &>(*this);
    }

public:
    using redis_protocol = typename connector<QB_IO_, RedisConsumer<QB_IO_, Derived>>::redis_protocol;

private:
    enum class MsgType { SUBSCRIBE, UNSUBSCRIBE, PSUBSCRIBE, PUNSUBSCRIBE, MESSAGE, PMESSAGE, UNKNOWN };

    static MsgType
    msg_type(const std::string_view &type) {
        static const qb::unordered_flat_map<std::string_view, MsgType> str_to_enum{
            {"message", MsgType::MESSAGE},         {"pmessage", MsgType::PMESSAGE},     {"subscribe", MsgType::SUBSCRIBE},
            {"unsubscribe", MsgType::UNSUBSCRIBE}, {"psubscribe", MsgType::PSUBSCRIBE}, {"punsubscribe", MsgType::PUNSUBSCRIBE}
        };

        auto it = str_to_enum.find(type);
        return it != str_to_enum.end() ? it->second : MsgType::UNKNOWN;
    }

    /**
     * @struct PendingReply
     * @brief One pending command-reply slot plus its confirmation accounting.
     *
     * A single SUBSCRIBE/UNSUBSCRIBE/PSUBSCRIBE/PUNSUBSCRIBE command with N
     * channels makes Redis emit N separate confirmation frames, but the caller
     * registered exactly one handler. `remaining` counts how many confirmations
     * must still arrive before the handler resolves; intermediate confirmations
     * are absorbed without popping, so the reply/command FIFO never desyncs.
     * Regular commands (HELLO, AUTH, …) use `remaining == 1`.
     */
    struct PendingReply {
        std::unique_ptr<IReply> handler;
        int                     remaining;
        /// True for (P)SUBSCRIBE/(P)UNSUBSCRIBE handlers. Lets the dispatcher tell a
        /// subscription-confirmation handler apart from a regular command handler so a
        /// server that emits more confirmations than predicted cannot pop and
        /// cross-resolve an unrelated command at the head of the FIFO.
        bool is_subscription = false;
    };

    std::queue<PendingReply> _replies;

    /// Predicted active subscriptions, advanced synchronously at send time so the
    /// expected confirmation count of "unsubscribe-all" (UNSUBSCRIBE/PUNSUBSCRIBE
    /// with no argument) is exact even when pipelined behind pending subscribes.
    qb::unordered_flat_set<std::string> _pred_channels;
    qb::unordered_flat_set<std::string> _pred_patterns;

    template <typename... Args>
    void
    _command(Args &&...args) {
        this->ready_to_write();
        put_in_pipe(this->out(), std::forward<Args>(args)...);
    }

    template <typename Ret, typename Func, typename... Args>
    requires std::invocable<Func, Reply<Ret> &&>
    Derived &
    command(Func &&func, std::string const &name, Args &&...args) {
        _replies.push(PendingReply{std::make_unique<TReply<Func, Ret>>(std::forward<Func>(func)), 1});
        _command(name, std::forward<Args>(args)...);
        return derived();
    }

    /**
     * @brief Compute the number of confirmation frames Redis will emit and
     *        advance the predicted-subscription state accordingly.
     *
     * SUBSCRIBE/PSUBSCRIBE acknowledge once per channel argument (even repeats),
     * so the count is the argument count. UNSUBSCRIBE/PUNSUBSCRIBE with explicit
     * names acknowledge once per name; with no name they acknowledge once per
     * currently-subscribed channel/pattern (or once when none are subscribed).
     */
    int
    predict_confirmations(bool is_unsub, bool is_pattern, const std::vector<std::string> &names) {
        auto &set = is_pattern ? _pred_patterns : _pred_channels;
        if (!is_unsub) {
            for (auto const &n : names)
                set.insert(n);
            return static_cast<int>(names.size());
        }
        if (names.empty()) {
            const int n = std::max<int>(1, static_cast<int>(set.size()));
            set.clear();
            return n;
        }
        for (auto const &n : names)
            set.erase(n);
        return static_cast<int>(names.size());
    }

    /**
     * @brief Issue a (P)SUBSCRIBE/(P)UNSUBSCRIBE and register a single handler
     *        that resolves only after all of its confirmation frames arrive.
     *
     * @param func       Reply<subscription> callback.
     * @param cmd        Redis command verb ("SUBSCRIBE", "PUNSUBSCRIBE", …).
     * @param is_unsub   True for (P)UNSUBSCRIBE.
     * @param is_pattern True for the pattern variants (P*).
     * @param names      Channel/pattern arguments (empty ⇒ unsubscribe-all).
     */
    template <typename Func>
    requires std::invocable<Func, Reply<qb::redis::subscription> &&>
    Derived &
    pubsub_command(Func &&func, const char *cmd, bool is_unsub, bool is_pattern, const std::vector<std::string> &names) {
        const int expected = predict_confirmations(is_unsub, is_pattern, names);
        _replies.push(PendingReply{
            std::make_unique<TReply<Func, qb::redis::subscription>>(std::forward<Func>(func)), expected,
            /*is_subscription=*/true
        });
        if (names.empty())
            _command(cmd);
        else
            _command(cmd, names);
        return derived();
    }

    template <typename Ret, typename... Args>
    Reply<Ret>
    command(std::string const &name, Args &&...args) {
        Reply<Ret> value{};
        auto       func = [&value](auto &&reply) {
            value = std::forward<Reply<Ret>>(reply);
        };
        command<Ret>(func, name, std::forward<Args>(args)...).await();
        return value;
    }

    void
    on(typename redis_protocol::message msg) {
        try {
            if (!msg.reply) {
                throw ProtoError("Null reply received");
            }
            auto &raw = *msg.reply;

            // Regular command reply (HELLO map, etc.) - not pub/sub
            if (!reply::is_array_or_push(raw)) {
                if (!_replies.empty()) {
                    auto handler = std::move(_replies.front().handler);
                    _replies.pop();
                    try {
                        (*handler)(std::move(msg.reply));
                    } catch (const std::exception &e) {
                        QB_LOG_WARN("[qbm][redis] consumer error: " << e.what());
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
            if (!elem0)
                throw ProtoError("Invalid message format");
            auto type = msg_type(reply::parse<std::string_view>(*elem0));

            switch (type) {
                case MsgType::MESSAGE:
                    // Deliver out-of-band, contained in its own try/catch like every
                    // other handler dispatch below. A throwing user message callback (or
                    // a malformed MESSAGE frame) must NOT propagate to the outer catch,
                    // which would fail an unrelated pending command and desync the FIFO —
                    // a pub/sub message has no pending reply of its own.
                    try {
                        derived().on(parse<qb::redis::message>(raw));
                    } catch (const std::exception &e) {
                        QB_LOG_WARN("[qbm][redis] message handler error: " << e.what());
                    }
                    return;
                case MsgType::PMESSAGE:
                    try {
                        derived().on(parse<qb::redis::pmessage>(raw));
                    } catch (const std::exception &e) {
                        QB_LOG_WARN("[qbm][redis] message handler error: " << e.what());
                    }
                    return;
                case MsgType::SUBSCRIBE:
                case MsgType::UNSUBSCRIBE:
                case MsgType::PSUBSCRIBE:
                case MsgType::PUNSUBSCRIBE: {
                    // A subscription confirmation. One (P)SUBSCRIBE/(P)UNSUBSCRIBE
                    // over N channels yields N confirmations but only one handler:
                    // absorb the intermediate confirmations and resolve on the last
                    // so the reply/command FIFO stays aligned.
                    if (_replies.empty())
                        throw ProtoError("Subscription confirmation with no pending command");
                    auto &front = _replies.front();
                    if (!front.is_subscription) {
                        // A confirmation frame arrived but the head of the FIFO is a
                        // regular command — the server emitted more confirmations than we
                        // predicted. Drop this stray confirmation instead of popping and
                        // cross-resolving the unrelated command (which would silently
                        // desync the reply/command FIFO for the connection's lifetime).
                        QB_LOG_WARN("[qbm][redis] dropping unexpected subscription confirmation "
                                    "(FIFO head is a regular command)");
                        return;
                    }
                    if (front.remaining > 1) {
                        --front.remaining;
                        return; // keep the handler for the remaining confirmations
                    }
                    auto handler = std::move(front.handler);
                    _replies.pop();
                    try {
                        (*handler)(std::move(msg.reply));
                    } catch (const std::exception &e) {
                        QB_LOG_WARN("[qbm][redis] consumer error: " << e.what());
                    }
                    return;
                }
                default:
                    break;
            }

            if (!_replies.empty()) {
                auto handler = std::move(_replies.front().handler);
                _replies.pop();
                try {
                    (*handler)(std::move(msg.reply));
                } catch (const std::exception &e) {
                    QB_LOG_WARN("[qbm][redis] consumer error: " << e.what());
                }
            } else {
                throw ProtoError("Unknown message type");
            }
        } catch (std::exception &e) {
            if (!_replies.empty()) {
                auto handler = std::move(_replies.front().handler);
                _replies.pop();
                try {
                    (*handler)(nullptr); // Completes awaitable with error
                } catch (...) {
                }
            }
            on(qb::redis::error{e.what(), nullptr});
        }
    }

    void
    on(qb::io::async::event::disconnected &&e) {
        QB_LOG_WARN("[qbm][redis] consumer disconnected");
        // Predicted subscription state is meaningless across a reconnect.
        _pred_channels.clear();
        _pred_patterns.clear();
        while (!_replies.empty()) {
            auto handler = std::move(_replies.front().handler);
            _replies.pop();
            try {
                (*handler)(nullptr);
            } catch (const std::exception &ex) {
                QB_LOG_WARN("[qbm][redis] consumer callback error: " << ex.what());
            } catch (...) {
                // Runs from dispose() under the libev C callback: a non-std exception
                // escaping here would terminate the process.
                QB_LOG_WARN("[qbm][redis] consumer callback threw a non-std exception");
            }
        }
        if constexpr (has_method_on<Derived, void, qb::io::async::event::disconnected>::value)
            derived().on(std::forward<qb::io::async::event::disconnected>(e));
    }

    void
    on(qb::redis::error &&error) {
        QB_LOG_WARN("[qbm][redis] parse error: " << error.what);
        if constexpr (has_method_on<Derived, void, qb::redis::error>::value)
            derived().on(std::forward<qb::redis::error>(error));
    }

public:
    RedisConsumer() = default;
    explicit RedisConsumer(qb::io::uri uri)
        : connector<QB_IO_, RedisConsumer<QB_IO_, Derived>>(std::move(uri)) {}

    Derived &
    await() {
        while (!_replies.empty())
            qb::io::async::listener::current.run(EVRUN_NOWAIT);
        return derived();
    }

    /** @brief Number of subscription/command replies still awaited (debugging). */
    [[nodiscard]] std::size_t
    pending_reply_count() const noexcept {
        return _replies.size();
    }

    template <typename T, typename Func>
    [[nodiscard]] auto
    make_coro_command(Func &&operation) {
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
class RedisCallbackConsumer : public RedisConsumer<QB_IO_, RedisCallbackConsumer<QB_IO_>> {
    friend struct has_method_on<RedisCallbackConsumer<QB_IO_>, void, qb::redis::error>;
    friend struct has_method_on<RedisCallbackConsumer<QB_IO_>, void, qb::io::async::event::disconnected>;
    friend RedisConsumer<QB_IO_, RedisCallbackConsumer<QB_IO_>>;

    using cb_msg_t  = std::function<void(qb::redis::message &&)>;
    using cb_err_t  = std::function<void(qb::redis::error &&)>;
    using cb_disc_t = std::function<void(qb::io::async::event::disconnected &&)>;

    // Sinks default to a no-op and are NEVER left empty (the ctor and setters below fall back to the
    // no-op), so dispatch is an UNCONDITIONAL call — no per-event `if (cb)` branch. This matters for
    // `_on_message` under a busy subscription: the happy path is a single indirect call, nothing to
    // predict or skip. Contract (matches the documented setup order): configure the sinks BEFORE
    // subscribing, and never reassign a sink from within its own running handler — that would free
    // the callable whose operator() is on the stack (the modify-what-you're-executing hazard). The
    // setup-before-subscribe flow never does this, so no per-call copy/guard is needed to defend it.
    cb_msg_t _on_message = [](qb::redis::message &&) {
    };
    cb_err_t _on_error = [](qb::redis::error &&) {
    };
    cb_disc_t _on_disconnected = [](qb::io::async::event::disconnected &&) {
    };

    // Exception safety of the user callable is handled UPSTREAM: RedisConsumer::on wraps the
    // message/pmessage/disconnected dispatch in try/catch, and qb::protocol::redis::onMessage adds a
    // catch(...) backstop before the libev noexcept boundary — so a throwing handler is contained
    // and never terminates. These dispatchers therefore stay branch-free.
    void
    on(qb::redis::message &&msg) {
        _on_message(std::move(msg));
    }

    void
    on(qb::redis::error &&error) {
        _on_error(std::move(error));
    }

    void
    on(qb::io::async::event::disconnected &&ev) {
        _on_disconnected(std::move(ev));
    }

public:
    explicit RedisCallbackConsumer(qb::io::uri uri = {}, cb_msg_t &&on_message = cb_msg_t{}, cb_err_t &&on_error = cb_err_t{},
                                   cb_disc_t &&on_disconnected = cb_disc_t{})
        : RedisConsumer<QB_IO_, RedisCallbackConsumer<QB_IO_>>(std::move(uri)) {
        // Keep the no-op default unless a real callable was supplied — a passed-but-empty sink must
        // not leave the member empty (dispatch is branch-free and would throw bad_function_call).
        if (on_message)
            _on_message = std::move(on_message);
        if (on_error)
            _on_error = std::move(on_error);
        if (on_disconnected)
            _on_disconnected = std::move(on_disconnected);
    }

    RedisCallbackConsumer &
    on_message(cb_msg_t &&cb) {
        _on_message = cb ? std::move(cb) : cb_msg_t{[](qb::redis::message &&) {}};
        return *this;
    }

    RedisCallbackConsumer &
    on_error(cb_err_t &&cb) {
        _on_error = cb ? std::move(cb) : cb_err_t{[](qb::redis::error &&) {}};
        return *this;
    }

    RedisCallbackConsumer &
    on_disconnected(cb_disc_t &&cb) {
        _on_disconnected = cb ? std::move(cb) : cb_disc_t{[](qb::io::async::event::disconnected &&) {}};
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
class RedisCoroConsumer : public RedisConsumer<QB_IO_, RedisCoroConsumer<QB_IO_>> {
    friend RedisConsumer<QB_IO_, RedisCoroConsumer<QB_IO_>>;

    /// Default buffered capacity for co_await receive() (tune for burst tolerance).
    static constexpr size_t DEFAULT_MSG_CAPACITY = 8192;

    using message_drop_callback = std::function<void(qb::redis::message &&)>;

    qb::io::async::channel<qb::redis::message> _msg_channel{DEFAULT_MSG_CAPACITY};
    message_drop_callback                      _on_message_dropped;

    void
    enqueue_pubsub_message(qb::redis::message &&msg) {
        if (_msg_channel.try_send(std::move(msg))) {
            return;
        }
        if (_on_message_dropped) {
            try {
                _on_message_dropped(std::move(msg));
            } catch (const std::exception &ex) {
                QB_LOG_WARN("[qbm][redis] coro consumer on_message_dropped error: " << ex.what());
            }
        } else {
            QB_LOG_WARN("[qbm][redis] coro consumer: message dropped (buffer full)");
        }
    }

    void
    on(qb::redis::message &&msg) {
        enqueue_pubsub_message(std::move(msg));
    }

    void
    on(qb::redis::pmessage &&msg) {
        // pmessage inherits from message; store as message (pattern is in base)
        qb::redis::message m = std::move(msg);
        enqueue_pubsub_message(std::move(m));
    }

    void
    on(qb::io::async::event::disconnected &&) {
        _msg_channel.close();
        // Base already cleared _replies and invoked us; no need to re-enter
    }

public:
    RedisCoroConsumer() = default;

    explicit RedisCoroConsumer(qb::io::uri uri, size_t message_channel_capacity = DEFAULT_MSG_CAPACITY)
        : RedisConsumer<QB_IO_, RedisCoroConsumer<QB_IO_>>(std::move(uri))
        , _msg_channel(message_channel_capacity) {}

    /**
     * @brief Optional callback when the internal queue is full and a message is dropped.
     * @return *this
     */
    RedisCoroConsumer &
    on_message_dropped(message_drop_callback cb) {
        _on_message_dropped = std::move(cb);
        return *this;
    }

    /** @brief Configured capacity of the internal pub/sub message queue. */
    [[nodiscard]] size_t
    message_channel_capacity() const noexcept {
        return _msg_channel.capacity();
    }

    /**
     * @brief Receive the next pub/sub message (coroutine awaitable).
     * @return std::optional<message> - has value when a message arrived,
     *         nullopt when the channel is closed (disconnected).
     *
     * Direct coroutine member (NOT an immediately-invoked lambda `[this]{...}()`):
     * the lambda closure would be a temporary destroyed at the end of this call,
     * leaving the coroutine frame referencing freed memory for `this`
     * (dangling-closure UAF — ASan-blind stack corruption that crashes under some
     * compilers' frame layouts). The consumer object owns the coroutine's `this`.
     */
    [[nodiscard]] qb::io::async::task<std::optional<qb::redis::message>>
    receive() {
        co_return co_await _msg_channel.recv();
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
        using client   = detail::Redis<qb::io::transport::stcp>;
        using pipeline = detail::RedisPipeline<qb::io::transport::stcp>;

        template <typename Derived>
        using consumer = detail::RedisConsumer<qb::io::transport::stcp, Derived>;

        using cb_consumer = detail::RedisCallbackConsumer<qb::io::transport::stcp>;
        using co_consumer = detail::RedisCoroConsumer<qb::io::transport::stcp>;
    };
#endif
};

inline constexpr auto no_check = [](auto &&) {
};

} // namespace qb::redis

#endif // QBM_REDIS_H
