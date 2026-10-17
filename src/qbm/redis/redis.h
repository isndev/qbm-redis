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

#include <cstdint>
#include <cstring>
#include <deque>
#include <exception>
#include <memory>
#include <optional>
#include <functional>
#include <queue>
#include <random>
#include <string>
#include <utility>
#include <vector>
#include <qb/io/async.h>
#include <qb/io/async/tcp/connector.h>
#include <qb/io/reconnect.h>
#ifdef QB_HAS_SSL
#include <qb/io/tcp/ssl/context.h> // qb::io::ssl::Context for make_connect_socket_() (rediss:// custom-CA / client-cert)
#endif
#include <qb/system/container/unordered_set.h>

// Native Redis Protocol Parser (C++20/23)
#include "parser.h"
#include "reply.h"
#include "stats.h"

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
 * @brief The reconnection backoff: qb-io's shared `qb::io::reconnect_policy` (`qb/io/reconnect.h`).
 *
 * Used by connect_with_retry() and enable_auto_reconnect(). The same type as `qb::http::RetryPolicy`:
 * exponential backoff from `initial_delay` by `multiplier` up to `max_delay`, jittered by a quarter
 * either way, `max_attempts` per run (-1 = unlimited), `on_retry(attempt, next_delay)` before each
 * wait. Its `connect_timeout` bounds each attempt's connect; zero (the default) means the 3 s a plain
 * `connect()` takes.
 */
using RetryPolicy = qb::io::reconnect_policy;

/**
 * @brief What a retrying view retries, and how (`redis.retrying(...)`, Huly QB-133).
 * @details A command sent through the view is retried only when it is safe and could help:
 *          - its verb is read-only (`command_traits_of(verb).read_only`), or an idempotent write and
 *            `idempotent_writes` is on -- never a non-idempotent write (INCR, APPEND, a list push or pop,
 *            XADD, EXPIRE, PUBLISH, EVAL, ...), never SET with NX, XX or GET, never ZADD with INCR, never a
 *            blocking command, never anything inside MULTI;
 *          - it failed without an answer ("disconnected", "command timed out") while the client can still
 *            carry it -- reconnecting, or ready -- or the server answered LOADING, BUSY, TRYAGAIN or
 *            MASTERDOWN. A terminal outcome is never retried: the client down (a run spent, a refused
 *            handshake, `disconnect()`), or idle.
 *          The attempts and the waits between them are `backoff`'s (the first attempt included); the last
 *          outcome reaches the callback or the awaiter unchanged, once.
 */
struct command_retry {
    /// The attempts (the first included) and the waits between them: 3 attempts, 50 ms then 100 ms.
    qb::io::reconnect_policy backoff = qb::io::reconnect_policy{}
                                           .with_max_attempts(3)
                                           .with_initial_delay(std::chrono::milliseconds{50})
                                           .with_max_delay(std::chrono::seconds{1});
    /// Retry the idempotent writes too (SET without NX/XX/GET, DEL, UNLINK, HSET, SADD, SREM, ZADD without
    /// INCR, EXPIREAT, PEXPIREAT, PERSIST, ...): off by default -- a replay leaves the same state, but its reply
    /// may differ from the first attempt's (a DEL that already deleted answers 0).
    bool idempotent_writes = false;
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
 * @class HandshakeClient
 * @brief The handshake hook's write handle: the client's command surface, writing on the link of ONE handshake.
 *
 * `set_handshake()` hands the hook a `handshake_client&`, never the client itself: while the client is
 * not ready, the client HOLDS every command (the hook's own would wait for the end of the hook that sent
 * them), and a handle that wrote whenever any coroutine asked would let other coroutines slip commands
 * past the gate while the hook is suspended. The handle writes straight to the wire only while the
 * handshake it was made for runs on its link; before or after that -- a leaked copy, an awaiter kept past
 * the hook -- every command through it fails with "handshake over". Defined after the clients, one
 * specialization each: the plain client's carries its whole command surface, a consumer's the connection
 * and subscription commands.
 * @tparam Owner the client or consumer class whose connector made it
 */
template <typename Owner>
class HandshakeClient;

/**
 * @class RetryingClient
 * @brief A view of a client whose eligible commands are retried (`redis.retrying(...)`, Huly QB-133).
 * @tparam Client the client class (`Redis<QB_IO_>`); defined after it.
 */
template <typename Client>
class RetryingClient;

/// The write path every `HandshakeClient` shares (see it below).
template <typename Owner, typename Self>
class handshake_writer;

/**
 * @class connector
 * @brief Base TCP connector for Redis clients and consumers: the reconnect contract (`qb/io/reconnect.h`).
 *
 * One state machine for the client and both consumers -- `idle`, `connecting`, `handshaking`, `ready`,
 * `reconnecting`, `down` (`qb::io::connection_state`):
 * - every connection, the first one included, runs the handshake before any user byte: HELLO 3 and AUTH
 *   (`with_resp3()`, `with_auth()`), SELECT (`with_select()`), the user's hook (`set_handshake()`), then
 *   the derived class's session replay (a consumer's subscriptions). Work submitted meanwhile is HELD by
 *   the derived class and leaves, in order, once the client is `ready`;
 * - a lost ready connection is recoverable only when a reconnection run is armed (`enable_auto_reconnect`);
 *   an explicit `disconnect()` never is: it is terminal, `down` with `down_reason::user`;
 * - a handshake the server refuses is terminal (`handshake_refused`); a transport failure during it, a
 *   transient server error (LOADING, BUSY, TRYAGAIN, MASTERDOWN) or an attempt past its deadline is only
 *   one more attempt;
 * - `down` fails held work once, with the reason; only an explicit `connect()` leaves it.
 *
 * @tparam QB_IO_ I/O transport type (e.g. qb::io::transport::tcp)
 * @tparam Derived CRTP derived class (Redis or RedisConsumer)
 */
template <typename QB_IO_, typename Derived>
class connector : public qb::io::async::tcp::client<connector<QB_IO_, Derived>, QB_IO_, void> {
    friend struct has_method_on<connector<QB_IO_, Derived>, void, qb::io::async::event::disconnected>;
    friend class qb::io::async::io<connector<QB_IO_, Derived>>;
    friend class qb::protocol::redis<connector<QB_IO_, Derived>>;
    friend class HandshakeClient<Derived>;
    template <typename, typename>
    friend class handshake_writer;

    constexpr Derived &
    derived() {
        return static_cast<Derived &>(*this);
    }

public:
    using redis_protocol = qb::protocol::redis<connector<QB_IO_, Derived>>;
    /// The handshake hook's write handle (see `set_handshake()`).
    using handshake_client = HandshakeClient<Derived>;
    /// The handshake hook: `true` lets the connection become ready, `false` refuses it for good.
    using handshake_hook = std::function<qb::io::async::task<bool>(handshake_client &)>;

private:
    /// How establish_() runs its attempts.
    enum class run_mode : std::uint8_t {
        single,   ///< a plain connect(): one attempt, and its failure is the link's cause
        run,      ///< connect_with_retry(): the policy's attempts, spent = retries_exhausted
        recovery, ///< the reconnection run of a lost ready connection: `reconnecting` between attempts
    };

    /// How one connection attempt -- the transport, then the handshake -- ended.
    enum class attempt_outcome : std::uint8_t {
        ready,            ///< the client is ready on the new link
        transport_failed, ///< no link, a lost link, a transient refusal or a deadline: another attempt may follow
        refused,          ///< the server or the hook refused the handshake: terminal
        cancelled,        ///< superseded (disconnect(), a new connect(), destruction): the new owner decides
    };

    /// Where a connect() reports: its awaiter's coroutine, or its callback.
    struct connect_completion {
        std::coroutine_handle<>   waiter;
        std::function<void(bool)> callback;
        bool                      ok = false;

        void
        settle(bool value) {
            ok = value;
            if (waiter) {
                auto h = std::exchange(waiter, {});
                ::qb::io::async::coro_scheduler().schedule_resume(h);
            } else if (callback) {
                auto cb = std::move(callback);
                cb(value);
            }
        }
    };

    qb::io::uri                _uri;
    std::optional<RetryPolicy> _reconnect_policy;
    qb::io::connection_state   _state         = qb::io::connection_state::idle;
    qb::io::down_reason        _down_reason   = qb::io::down_reason::user;
    qb::io::down_reason        _loss_cause    = qb::io::down_reason::peer; ///< why the last link went away
    bool                       _link_open     = false;                     ///< a link is installed and its teardown has not begun
    bool                       _cause_set     = false;                     ///< the client named _loss_cause before it dropped the link
    bool                       _session_ready = false;                     ///< the installed link reached ready: its loss fires on_disconnected
    bool                       _recovering    = false;                     ///< a reconnection run is in progress (is_reconnecting())
    bool                       _ever_ready    = false;                     ///< a connection reached ready once: a later one follows a gap
    bool                       _verify_peer   = true;                      /**< Verify server TLS cert for rediss:// (stcp transport). */
    /// Bumped by disconnect(), by every connect() that starts a lifecycle and by every terminal transition:
    /// the run, the attempt and the handshake that captured an older value were superseded and step aside.
    std::uint64_t _epoch           = 0;
    std::uint64_t _user_down_epoch = 0; ///< the epoch disconnect() left a ready link in: its teardown settles that down
    std::uint64_t _live_handshake  = 0; ///< the id of the handshake writing on the link (0: none)
    std::uint64_t _handshake_seq   = 0;
    std::string   _refusal;       ///< what refused the last handshake, for down_reason::handshake_refused's message
    std::string   _ssl_root_cert; /**< rediss://: private CA (PEM file/dir) added to the trust store. Empty = system store. */
    std::string   _ssl_cert;      /**< rediss://: client certificate (PEM) for mutual TLS. Empty = none. */
    std::string   _ssl_key;       /**< rediss://: client private key (PEM) for mutual TLS. Pairs with _ssl_cert. */
    // The handshake every connection runs, in this order: HELLO 3 and/or AUTH, SELECT, the hook, the replay.
    bool                       _hs_resp3 = false;
    std::optional<std::string> _hs_user;
    std::optional<std::string> _hs_password;
    std::optional<long long>   _hs_db;
    handshake_hook             _hs_hook;
    // Observability hooks: empty until set, read only on the cold transitions.
    std::function<void()>                    _on_connected;
    std::function<void(qb::io::down_reason)> _on_disconnected;
    std::function<void(qb::io::down_reason)> _on_down;
    qb::redis::stats                         _stats{}; ///< what this client or consumer has done (stats(), Huly QB-134)
    bool                  _lost_since_ready = false;   ///< a ready link was lost, not by disconnect(): the next ready is a reconnect
    std::shared_ptr<bool> _alive{std::make_shared<bool>(true)};

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

    /**
     * @brief Awaits one transport connect -- TCP, and the TLS handshake of a secure transport -- and hands the
     *        socket back, open or not. The socket is the frame's: a frame destroyed first closes it.
     */
    struct transport_awaiter {
        connector                                        &_client;
        qb::duration                                      _timeout;
        std::optional<typename QB_IO_::transport_io_type> _sock;
        std::coroutine_handle<>                           _handle;
        std::shared_ptr<bool>                             _valid{std::make_shared<bool>(true)};

        transport_awaiter(connector &client, qb::duration timeout)
            : _client(client)
            , _timeout(timeout) {}
        transport_awaiter(transport_awaiter const &)            = delete;
        transport_awaiter &operator=(transport_awaiter const &) = delete;
        ~transport_awaiter() {
            *_valid = false;
        }

        bool
        await_ready() const noexcept {
            return false;
        }

        void
        await_suspend(std::coroutine_handle<> h) {
            qb::io::async::track_suspension(h, "redis connect");
            _handle    = h;
            auto valid = _valid;
            ::qb::io::async::tcp::connect<typename QB_IO_::transport_io_type>(
                _client.make_connect_socket_(), _client._uri,
                [this, valid](auto &&raw_io) {
                    if (!*valid)
                        return; // the frame is gone; the socket closes with raw_io
                    _sock.emplace(std::forward<decltype(raw_io)>(raw_io));
                    ::qb::io::async::coro_scheduler().schedule_resume(_handle);
                },
                _timeout);
        }

        typename QB_IO_::transport_io_type
        await_resume() {
            return _sock ? std::move(*_sock) : typename QB_IO_::transport_io_type{};
        }
    };

    /// The cause a transport loss the client did not ask for maps to.
    [[nodiscard]] static qb::io::down_reason
    cause_of_(int reason) noexcept {
        using qb::io::async::event::disconnect_reason;
        switch (static_cast<disconnect_reason>(reason)) {
            case disconnect_reason::protocol_error:
            case disconnect_reason::message_too_large:
            case disconnect_reason::buffer_overflow:
                return qb::io::down_reason::protocol;
            default:
                return qb::io::down_reason::peer;
        }
    }

    /// A server error the handshake may retry: the server is starting, busy or failing over, not refusing.
    [[nodiscard]] static bool
    is_transient_error_(std::string_view error) noexcept {
        for (std::string_view code : {"LOADING", "BUSY", "TRYAGAIN", "MASTERDOWN"}) {
            if (error.size() >= code.size() && error.substr(0, code.size()) == code
                && (error.size() == code.size() || error[code.size()] == ' '))
                return true;
        }
        return false;
    }

    /// Install an opened socket as the client's link and start reading: the handshake comes next.
    void
    install_link_(typename QB_IO_::transport_io_type &&raw_io) {
        this->transport() = std::move(raw_io);
        this->clear_protocols(); // idempotent: drops any prior protocol, resets to the NoProtocol sentinel
        this->reset_io_state();
        // Nothing of an earlier link leaves on this one: work submitted while the client was not ready was
        // held apart (the derived class's held queue), never written here (Huly QB-202).
        this->in().reset();
        this->out().reset();
        this->template switch_protocol<redis_protocol>(*this);
        this->start();
        _link_open     = true;
        _cause_set     = false;
        _session_ready = false;
        _state         = qb::io::connection_state::handshaking;
    }

    /// Tear the link down now, naming the cause; on(disconnected) runs before this returns, or right after the
    /// reply handler that called it.
    void
    drop_link_(qb::io::down_reason cause) noexcept {
        if (!_link_open)
            return;
        _loss_cause = cause;
        _cause_set  = true;
        _link_open  = false;
        qb::io::async::io<connector<QB_IO_, Derived>>::disconnect_now();
    }

    template <typename Fn, typename... Args>
    static void
    notify_(Fn &hook, char const *what, Args... args) noexcept {
        if (!hook)
            return;
        try {
            hook(args...);
        } catch (const std::exception &ex) {
            QB_LOG_WARN("[qbm][redis] " << what << " hook error: " << ex.what());
        } catch (...) {
            QB_LOG_WARN("[qbm][redis] " << what << " hook threw a non-std exception");
        }
    }

    /// The handshake succeeded: release what was held, then tell the user.
    void
    enter_ready_() {
        _state          = qb::io::connection_state::ready;
        _session_ready  = true;
        _recovering     = false;
        _ever_ready     = true;
        _live_handshake = 0;
        if (_lost_since_ready) {
            _lost_since_ready = false;
            ++_stats.reconnects;
        }
        const auto epoch = _epoch;
        derived().on_ready_(); // held work leaves in order; a consumer then queues its pending gap marker
        if (epoch != _epoch || _state != qb::io::connection_state::ready)
            return; // a marker or a release callback disconnected
        notify_(_on_connected, "on_connected");
    }

    /// The message held work fails with when the client goes down for `reason`.
    [[nodiscard]] std::string
    down_text_(qb::io::down_reason reason) const {
        switch (reason) {
            case qb::io::down_reason::retries_exhausted:
                return "reconnection attempts exhausted";
            case qb::io::down_reason::handshake_refused:
                return "handshake refused: " + _refusal;
            default:
                return "disconnected";
        }
    }

    /// Terminal: fail what was held, end a consumer's stream, tell the user. Supersedes the current owner.
    void
    enter_down_(qb::io::down_reason reason) {
        _state           = qb::io::connection_state::down;
        _down_reason     = reason;
        _recovering      = false;
        const auto epoch = ++_epoch;
        settle_down_(reason, epoch);
    }

    void
    settle_down_(qb::io::down_reason reason, std::uint64_t epoch) {
        derived().on_down_(down_text_(reason)); // held work fails once; a consumer's stream drains then ends
        if (epoch != _epoch)
            return; // a failure callback connected again: the client did not stay down
        notify_(_on_down, "on_down", reason);
    }

    void
    on(typename redis_protocol::message msg) {
        derived().on(std::move(msg));
    }

    /**
     * @brief The link is gone: whatever was in flight on it fails, and the client decides what follows.
     * @details A handshake's link only fails the handshake's commands: the attempt that installed it decides.
     *          A ready link's loss fires on_disconnected with its cause, AFTER the in-flight work failed and
     *          BEFORE the reconnection decision -- a hook that disarms the policy prevents the run (QB-663);
     *          then a run starts when the cause is not `user` and a policy is armed, else the client is down.
     */
    void
    on(qb::io::async::event::disconnected &&ev) {
        const bool was_ready = _session_ready;
        _session_ready       = false;
        _link_open           = false;
        _live_handshake      = 0;
        if (!_cause_set)
            _loss_cause = cause_of_(ev.reason);
        _cause_set       = false;
        const auto cause = _loss_cause;
        // The dead connection's bytes go now, where its commands are failed (Huly QB-202): a command
        // serialized but never flushed would otherwise lead the next connection.
        this->in().reset();
        this->out().reset();
        QB_LOG_WARN("[qbm][redis] disconnected (" << qb::io::down_reason_name(cause) << ")");
        // A ready link's loss leaves `ready` BEFORE what was in flight fails: a failure callback that submits
        // again must find the client holding (a run will follow) or down (none will), never ready on a link
        // that is gone -- its bytes would be written to a dead pipe and its handler queued for no reply.
        if (was_ready && cause != qb::io::down_reason::user) {
            _lost_since_ready = true;
            if (_reconnect_policy) {
                _state      = qb::io::connection_state::reconnecting;
                _recovering = true;
            } else {
                _state       = qb::io::connection_state::down;
                _down_reason = cause;
            }
        }
        // Read before anything user code runs: a failure callback below, or the on_disconnected hook, that calls
        // disconnect() or connect() owns the client from then on, and this loss decides nothing after it.
        const auto epoch = _epoch;
        try {
            derived().on_link_lost_(was_ready);
        } catch (const std::exception &ex) {
            QB_LOG_WARN("[qbm][redis] disconnected handler error: " << ex.what());
        } catch (...) {
            QB_LOG_WARN("[qbm][redis] disconnected handler threw a non-std exception");
        }
        if (!was_ready)
            return;
        if (cause == qb::io::down_reason::user) {
            // disconnect() is already down: what remains is to say so, in order -- unless a connect() since
            // then started another lifecycle, whose held work is not this down's to fail.
            const bool current = _epoch == _user_down_epoch;
            notify_(_on_disconnected, "on_disconnected", cause);
            if (current && _epoch == _user_down_epoch)
                settle_down_(cause, _epoch);
            return;
        }
        // The state the client is heading for -- set above, readable from the hook; the hook may still change
        // the policy. The hook fires whatever a failure callback did: it reports this link's loss, and
        // on_connected / on_disconnected strictly alternate.
        notify_(_on_disconnected, "on_disconnected", cause);
        if (epoch != _epoch)
            return; // a failure callback or the hook disconnected or connected: that call owns the state now
        if (!_reconnect_policy) {
            enter_down_(cause);
            return;
        }
        _state      = qb::io::connection_state::reconnecting;
        _recovering = true;
        auto policy = *_reconnect_policy;
        ::qb::io::async::coro_scheduler().spawn(run_(std::move(policy), _alive, _epoch));
    }

    /// The reconnection run: the policy's attempts, until ready, refused, spent or superseded.
    qb::io::async::task<void>
    run_(RetryPolicy policy, std::shared_ptr<bool> alive, std::uint64_t epoch) {
        // A task first runs at the scheduler's next drain, not at spawn(): the client may be gone by then.
        if (!*alive || epoch != _epoch)
            co_return;
        QB_LOG_INFO("[qbm][redis] auto-reconnect starting...");
        const bool ok = co_await establish_(std::move(policy), run_mode::recovery, alive, epoch);
        if (!*alive)
            co_return;
        if (ok) {
            QB_LOG_INFO("[qbm][redis] auto-reconnect succeeded");
        } else {
            QB_LOG_WARN("[qbm][redis] auto-reconnect failed");
        }
    }

    [[nodiscard]] static qb::duration
    attempt_timeout_(RetryPolicy const &policy) noexcept {
        return policy.connect_timeout > qb::duration::zero() ? policy.connect_timeout : qb::duration{std::chrono::seconds(3)};
    }

    /**
     * @brief The one engine behind every connect form and the reconnection run: attempts until the client is
     *        ready, the handshake is refused, the policy is spent, or the lifecycle is superseded.
     * @param policy The attempts and their backoff (one attempt for a plain connect()).
     * @param mode A plain connect() (its failure is the link's cause), an explicit run (connect_with_retry),
     *             or the reconnection run (state `reconnecting` between attempts).
     */
    qb::io::async::task<bool>
    establish_(RetryPolicy policy, run_mode mode, std::shared_ptr<bool> alive, std::uint64_t epoch) {
        static thread_local std::mt19937 rng_{std::random_device{}()};
        const auto         between = mode == run_mode::recovery ? qb::io::connection_state::reconnecting : qb::io::connection_state::connecting;
        const qb::duration limit   = attempt_timeout_(policy);
        int                attempt = 0;
        while (policy.allows_attempt(attempt)) {
            if (!*alive || epoch != _epoch)
                co_return false;
            ++attempt;
            _state             = between;
            const auto outcome = co_await attempt_(limit, alive, epoch);
            if (!*alive || epoch != _epoch || outcome == attempt_outcome::cancelled)
                co_return false; // disconnect(), a new connect() or destruction owns the state now
            if (outcome == attempt_outcome::ready)
                co_return true;
            if (outcome == attempt_outcome::refused) {
                enter_down_(qb::io::down_reason::handshake_refused);
                co_return false;
            }
            _state = between; // the attempt's link is gone: the run waits, or ends
            if (!policy.allows_attempt(attempt))
                break;
            const qb::duration wait = policy.next_delay(attempt, &rng_);
            notify_(policy.on_retry, "on_retry", attempt, wait);
            if (!*alive || epoch != _epoch)
                co_return false; // on_retry disconnected
            co_await qb::io::async::sleep(wait);
        }
        if (!*alive || epoch != _epoch)
            co_return false;
        enter_down_(mode == run_mode::single ? _loss_cause : qb::io::down_reason::retries_exhausted);
        co_return false;
    }

    /// One attempt: the transport, then the handshake on the new link.
    qb::io::async::task<attempt_outcome>
    attempt_(qb::duration limit, std::shared_ptr<bool> alive, std::uint64_t epoch) {
        auto sock = co_await transport_awaiter{*this, limit};
        if (!*alive || epoch != _epoch)
            co_return attempt_outcome::cancelled; // the socket closes with this frame
        if (!sock.is_open()) {
            _loss_cause = qb::io::down_reason::peer;
            co_return attempt_outcome::transport_failed;
        }
        install_link_(std::move(sock));
        co_return co_await handshake_(limit, alive, epoch);
    }

    /// Whether the handshake has anything to do beyond the transport.
    [[nodiscard]] bool
    handshake_has_steps_() {
        return _hs_resp3 || _hs_password || _hs_db || _hs_hook || derived().has_session_to_replay_();
    }

    /// The handshake's deadline: an attempt that is still handshaking when it falls is a transport failure.
    qb::io::async::task<void>
    handshake_deadline_(std::uint64_t id, qb::duration limit, std::shared_ptr<bool> alive) {
        if (!*alive)
            co_return;
        co_await qb::io::async::sleep(limit);
        if (!*alive || _live_handshake != id)
            co_return;
        QB_LOG_WARN("[qbm][redis] handshake did not complete within " << qb::detail::to_ev_seconds(limit) << "s; dropping the link");
        drop_link_(qb::io::down_reason::peer);
    }

    /**
     * @brief The handshake on a fresh link: the protocol's, the user's hook, the derived class's replay.
     * @details Each step writes through a `handshake_client` that only this handshake can use. A reply that
     *          comes back as a server error refuses the handshake for good -- unless it is a transient one
     *          (LOADING, BUSY, TRYAGAIN, MASTERDOWN), which drops the link and lets the run try again. A link
     *          lost on the way, or past `limit`, is a transport failure whatever the step said.
     */
    qb::io::async::task<attempt_outcome>
    handshake_(qb::duration limit, std::shared_ptr<bool> alive, std::uint64_t epoch) {
        const std::uint64_t id = ++_handshake_seq;
        _live_handshake        = id;
        if (!handshake_has_steps_()) {
            enter_ready_();
            co_return attempt_outcome::ready;
        }
        ::qb::io::async::coro_scheduler().spawn(handshake_deadline_(id, limit, alive));
        handshake_client hs{derived(), alive, id};
        // Judges one step: nullopt when the handshake may go on, else how this attempt ends.
        auto judge = [&](bool ok, bool had_reply, std::string const &error) -> std::optional<attempt_outcome> {
            if (!*alive || epoch != _epoch)
                return attempt_outcome::cancelled;
            if (_live_handshake != id || !_link_open)
                return attempt_outcome::transport_failed; // the link went away under the step
            if (ok)
                return std::nullopt;
            if (had_reply && is_transient_error_(error)) {
                drop_link_(qb::io::down_reason::peer);
                return attempt_outcome::transport_failed;
            }
            _refusal        = error.empty() ? std::string{"refused"} : error;
            _live_handshake = 0;
            return attempt_outcome::refused;
        };
        if (_hs_resp3) {
            // HELLO 3 carries the credentials when there are some: one round trip, and the server answers
            // in RESP3 from the reply on.
            auto r = _hs_password ? co_await hs.template command<qb::json>("HELLO", "3", "AUTH", _hs_user.value_or("default"), *_hs_password)
                                  : co_await hs.template command<qb::json>("HELLO", "3");
            if (auto end = judge(r.ok(), r.raw() != nullptr, r.error()))
                co_return finish_attempt_(*end);
        } else if (_hs_password) {
            auto r = _hs_user ? co_await hs.auth(*_hs_user, *_hs_password) : co_await hs.auth(*_hs_password);
            if (auto end = judge(r.ok(), r.raw() != nullptr, r.error()))
                co_return finish_attempt_(*end);
        }
        if (_hs_db) {
            auto r = co_await hs.select(*_hs_db);
            if (auto end = judge(r.ok(), r.raw() != nullptr, r.error()))
                co_return finish_attempt_(*end);
        }
        if (_hs_hook) {
            // A copy: the hook may be replaced while it runs, and the closure it calls must outlive it.
            auto        hook    = _hs_hook;
            bool        verdict = false;
            std::string why;
            try {
                verdict = co_await hook(hs);
            } catch (const std::exception &ex) {
                why = ex.what();
            } catch (...) {
                why = "the handshake hook threw";
            }
            if (auto end = judge(verdict, false, why.empty() ? std::string{"the handshake hook refused the connection"} : why))
                co_return finish_attempt_(*end);
        }
        if (derived().has_session_to_replay_()) {
            const std::optional<std::string> error = co_await derived().replay_session_(hs);
            if (auto end = judge(!error, error.has_value(), error.value_or(std::string{})))
                co_return finish_attempt_(*end);
        }
        if (auto end = judge(true, false, {}))
            co_return finish_attempt_(*end);
        enter_ready_();
        co_return attempt_outcome::ready;
    }

    /// An attempt that ends before ready leaves no handshake live and, if it refused, no link either.
    attempt_outcome
    finish_attempt_(attempt_outcome outcome) {
        if (outcome == attempt_outcome::cancelled)
            return outcome;
        _live_handshake = 0;
        if (outcome == attempt_outcome::refused)
            drop_link_(qb::io::down_reason::peer);
        return outcome;
    }

    /// Start a lifecycle from idle or down, or say why not.
    [[nodiscard]] bool
    begin_lifecycle_() {
        if (_state != qb::io::connection_state::idle && _state != qb::io::connection_state::down)
            return false; // a connection, a handshake or a run is in progress: it owns the state
        ++_epoch;
        _refusal.clear();
        _state = qb::io::connection_state::connecting;
        derived().on_lifecycle_start_();
        return true;
    }

    qb::io::async::task<void>
    connect_flow_(RetryPolicy policy, std::shared_ptr<connect_completion> done, std::shared_ptr<bool> alive, std::uint64_t epoch) {
        const bool ok = co_await establish_(std::move(policy), run_mode::single, alive, epoch);
        done->settle(ok && *alive);
    }

    /// Start one connect() attempt for `done`; false when the client was busy (nothing started).
    bool
    begin_connect_(qb::duration timeout, std::shared_ptr<connect_completion> done) {
        if (!begin_lifecycle_())
            return false;
        auto policy = RetryPolicy{}.with_max_attempts(1).with_connect_timeout(timeout);
        ::qb::io::async::coro_scheduler().spawn(connect_flow_(std::move(policy), std::move(done), _alive, _epoch));
        return true;
    }

protected:
    connector() = default;
    ~connector() {
        *_alive = false;
    }

    // The derived class's part of the lifecycle. A derived class that needs one declares its own, which hides
    // the default; the connector calls them through derived().
    /// A connect() leaves idle or down: the session of a new lifecycle begins.
    void
    on_lifecycle_start_() {}
    /// Whether the handshake has a session to replay (a consumer's subscriptions).
    [[nodiscard]] bool
    has_session_to_replay_() const noexcept {
        return false;
    }
    /// Replay the session through the handshake's handle: nullopt once done, else the server's refusal.
    qb::io::async::task<std::optional<std::string>>
    replay_session_(handshake_client &) {
        co_return std::nullopt;
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

    /// Whether frames of the installed link are still dispatched (false once its teardown began).
    [[nodiscard]] bool
    link_open_() const noexcept {
        return _link_open;
    }

    /// Whether work flows: the derived classes hold or fail what is submitted otherwise.
    [[nodiscard]] bool
    ready_() const noexcept {
        return _state == qb::io::connection_state::ready;
    }

    /// Whether work submitted now waits for ready (a connection, a handshake or a run is in progress).
    [[nodiscard]] bool
    holding_() const noexcept {
        return _state == qb::io::connection_state::connecting || _state == qb::io::connection_state::handshaking
               || _state == qb::io::connection_state::reconnecting;
    }

    /// The failure of work submitted while the client cannot send it: idle, or down for its reason.
    [[nodiscard]] std::string
    refusal_text_() const {
        return _state == qb::io::connection_state::idle ? std::string{"not connected"} : down_text_(_down_reason);
    }

    /// Whether a connection reached ready before: the next one follows a gap in a consumer's stream.
    [[nodiscard]] bool
    ever_ready_() const noexcept {
        return _ever_ready;
    }

    /// Whether the handshake `id` is the one writing on the link now.
    [[nodiscard]] bool
    handshake_live_(std::uint64_t id) const noexcept {
        return id != 0 && _live_handshake == id && _link_open;
    }

    /// Drop the link as the client's own decision (the command watchdog): recoverable, cause `peer`.
    void
    drop_unresponsive_link_() noexcept {
        drop_link_(qb::io::down_reason::peer);
    }

    /**
     * @brief Fail a handler on the next loop pass, never inside the call that submitted it.
     * @details Work submitted while the client is idle or down cannot wait for anything: it fails at once,
     *          but from the loop -- a failure callback that submits again would otherwise recurse without
     *          bound. A client destroyed meanwhile discards the handler uninvoked, as it does its pending
     *          replies.
     */
    void
    fail_soon_(std::unique_ptr<IReply> handler, std::string reason) {
        ++_failing_soon;
        std::shared_ptr<IReply> shared{std::move(handler)};
        qb::io::async::defer([alive = _alive, shared, reason = std::move(reason), this]() {
            if (!*alive)
                return;
            --_failing_soon;
            try {
                shared->fail(reason);
            } catch (const std::exception &ex) {
                QB_LOG_WARN("[qbm][redis] callback error: " << ex.what());
            } catch (...) {
                QB_LOG_WARN("[qbm][redis] callback threw a non-std exception");
            }
        });
    }

    /// Failures fail_soon_() scheduled and the loop has not run yet (await() drains them too).
    std::size_t _failing_soon = 0;

    /// The counts, for the derived class's paths (Huly QB-134).
    [[nodiscard]] qb::redis::stats &
    stats_mut_() noexcept {
        return _stats;
    }

    /**
     * @brief Serialize one command into `pipe`: all of it, or nothing.
     * @details `put_in_pipe()` writes the RESP array header, then each argument, and an argument that
     *          cannot be written throws -- one over `REDIS_MAX_STRING_SIZE` throws `SecurityError` -- with
     *          the header and the arguments before it already in `pipe`. That partial frame used to stay:
     *          the next command went out behind it, and the server's replies no longer matched the reply
     *          FIFO (Huly QB-255). Here `pipe` is put back as it was, and the reason returned for the
     *          caller to fail the command with. Writing to a pipe sends nothing, so a handler queued after
     *          this call still precedes any reply.
     * @return Why the command could not be serialized; empty when it was.
     */
    template <typename... Args>
    [[nodiscard]] static std::optional<std::string>
    serialize_into_(qb::allocator::pipe<char> &pipe, Args &&...args) {
        const std::size_t before = pipe.size();
        try {
            put_in_pipe(pipe, std::forward<Args>(args)...);
        } catch (std::exception const &e) {
            pipe.free_back(pipe.size() - before);
            return std::string{e.what()};
        } catch (...) {
            pipe.free_back(pipe.size() - before);
            return std::string{"the command could not be serialized"};
        }
        return std::nullopt;
    }

    /// serialize_into_(out()), then arm the writer: the command leaves with the next write.
    template <typename... Args>
    [[nodiscard]] std::optional<std::string>
    try_put_command(Args &&...args) {
        if (auto error = serialize_into_(this->out(), std::forward<Args>(args)...))
            return error;
        this->ready_to_write();
        return std::nullopt;
    }

    /// Move the first `bytes` of `held` (a held command, serialized when it was submitted) onto the link.
    void
    put_held_bytes_(qb::allocator::pipe<char> &held, std::size_t bytes) {
        if (bytes == 0)
            return;
        std::memcpy(this->out().allocate_back(bytes), held.begin(), bytes);
        held.free_front(bytes);
    }

    /// Arm the writer: what was put on the link leaves with the next write.
    void
    arm_write_() {
        this->ready_to_write();
    }

    /// Whether `now` past `since` is past the command timeout `limit` (held work's deadline).
    [[nodiscard]] static qb::duration
    held_wait_(qb::mono_time since, qb::duration limit) noexcept {
        const auto due = since + std::chrono::duration_cast<qb::mono_time::duration>(limit);
        const auto now = qb::mono_now();
        return due > now ? std::chrono::duration_cast<qb::duration>(due - now) : qb::duration::zero();
    }

public:
    /**
     * @struct connect_awaiter
     * @brief Coroutine awaiter for async connection (`co_await connect()`): `true` once the client is ready.
     * @details Resolves when the connection's handshake is done -- `true`, the client `ready` -- or when the
     *          attempt failed, was refused, or was superseded by `disconnect()` -- `false`, the client `down`
     *          (or owned by the call that superseded it). Resolves `false` at once when a connection, a
     *          handshake or a reconnection run is already in progress. Neither copyable nor movable: an
     *          awaiter's completion is its own, and a copy would settle the original's (QB-1000).
     */
    struct connect_awaiter {
        connector                          &_client;
        qb::duration                        _timeout;
        std::shared_ptr<connect_completion> _done;

        explicit connect_awaiter(connector &client, qb::duration timeout = std::chrono::seconds(3))
            : _client(client)
            , _timeout(timeout) {}
        connect_awaiter(connect_awaiter const &)            = delete;
        connect_awaiter(connect_awaiter &&)                 = delete;
        connect_awaiter &operator=(connect_awaiter const &) = delete;
        connect_awaiter &operator=(connect_awaiter &&)      = delete;

        ~connect_awaiter() {
            if (_done)
                _done->waiter = {}; // a frame destroyed while connecting is never resumed
        }

        bool
        await_ready() const noexcept {
            return false;
        }

        bool
        await_suspend(std::coroutine_handle<> h) {
            qb::io::async::track_suspension(h, "redis connect");
            _done         = std::make_shared<connect_completion>();
            _done->waiter = h;
            if (_client.begin_connect_(_timeout, _done))
                return true;
            _done->waiter = {};
            return false; // busy: resolves false at once
        }

        bool
        await_resume() const noexcept {
            return _done && _done->ok;
        }
    };

    /** @brief Connect, then run the handshake (co_awaitable): `true` once the client is ready. */
    connect_awaiter
    connect() {
        return connect_awaiter{*this};
    }
    connect_awaiter
    connect(qb::io::uri uri) {
        if (_state == qb::io::connection_state::idle || _state == qb::io::connection_state::down)
            _uri = std::move(uri);
        return connect_awaiter{*this};
    }
    connect_awaiter
    connect(qb::duration timeout) {
        return connect_awaiter{*this, timeout};
    }
    connect_awaiter
    connect(qb::io::uri uri, qb::duration timeout) {
        if (_state == qb::io::connection_state::idle || _state == qb::io::connection_state::down)
            _uri = std::move(uri);
        return connect_awaiter{*this, timeout};
    }

    void
    set_uri(qb::io::uri uri) noexcept {
        _uri = std::move(uri);
    }

    /**
     * @brief Connect with exponential-backoff retry: the policy's attempts, each a transport connect and a
     *        handshake, until the client is ready.
     *
     * Attempt 1 is immediate; between attempts the run sleeps the policy's `next_delay()` (jittered), and
     * calls `on_retry(attempt, next_delay)` first. A handshake the server refuses ends the run at once,
     * `down` with `handshake_refused`; a spent policy ends it `down` with `retries_exhausted`. Resolves
     * `false` at once when a connection, a handshake or a run is already in progress. Liveness-safe: if the
     * client is destroyed while suspended, the task exits cleanly without touching `*this`.
     *
     * @param policy Retry/backoff configuration (defaults: unlimited attempts).
     * @return A task yielding `true` once the client is ready, `false` otherwise.
     */
    qb::io::async::task<bool>
    connect_with_retry(RetryPolicy policy = RetryPolicy{}) {
        if (!begin_lifecycle_())
            co_return false;
        co_return co_await establish_(std::move(policy), run_mode::run, _alive, _epoch);
    }

    qb::io::async::task<bool>
    connect_with_retry(qb::io::uri uri, RetryPolicy policy = RetryPolicy{}) {
        if (_state == qb::io::connection_state::idle || _state == qb::io::connection_state::down)
            _uri = std::move(uri);
        co_return co_await connect_with_retry(std::move(policy));
    }

    /**
     * @brief Callback connect: `func(true)` once the client is ready, `func(false)` when the attempt failed,
     *        was refused or superseded -- always from the loop, never inside this call.
     * @details A client destroyed meanwhile never calls `func` (the completion is the client's).
     */
    template <std::invocable<bool> Func>
    void
    connect(Func &&func, qb::io::uri uri, qb::duration timeout = std::chrono::seconds(3)) {
        if (_state == qb::io::connection_state::idle || _state == qb::io::connection_state::down)
            _uri = std::move(uri);
        auto done      = std::make_shared<connect_completion>();
        auto alive     = connector_alive();
        auto callable  = std::make_shared<std::decay_t<Func>>(std::forward<Func>(func));
        done->callback = [alive, callable](bool ok) {
            if (*alive)
                (*callable)(ok);
        };
        if (!begin_connect_(timeout, done)) {
            qb::io::async::defer([done]() { done->settle(false); });
        }
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
     * @brief Adopt an already-opened transport socket, then run the handshake on it.
     *
     * Installs the RESP protocol, begins async I/O on `raw_io` and starts the handshake: the client is
     * `ready` once it succeeds, `down` if it is refused or the link drops first. Refuses (returns `false`)
     * while a connection, a handshake or a run is in progress.
     *
     * @param uri    URI associated with this connection (stored).
     * @param raw_io An opened transport socket to take ownership of.
     * @return `true` if the socket was adopted, `false` if the client was busy.
     */
    bool
    setup_connection(qb::io::uri uri, typename QB_IO_::transport_io_type &&raw_io) {
        if (!begin_lifecycle_())
            return false;
        _uri = std::move(uri);
        install_link_(std::move(raw_io));
        ::qb::io::async::coro_scheduler().spawn(adopted_flow_(_alive, _epoch));
        return true;
    }

    /**
     * @brief Arm the reconnection run for a lost ready connection.
     * @details A loss -- the peer closed or reset the link, a frame the parser refused, the command
     *          watchdog -- starts a run of the policy's attempts, each one handshaken; an explicit
     *          `disconnect()` never does. Read when the loss happens: enabling it later arms the next loss.
     * @param policy Retry/backoff policy of the run.
     */
    void
    enable_auto_reconnect(RetryPolicy policy = RetryPolicy{}) noexcept {
        _reconnect_policy = std::move(policy);
    }

    /** @brief Disarm the reconnection run for the next loss (a run in progress still finishes). */
    void
    disable_auto_reconnect() noexcept {
        _reconnect_policy.reset();
    }

    /** @brief Whether a reconnection run is in progress: from the loss until it is ready or down. */
    [[nodiscard]] bool
    is_reconnecting() const noexcept {
        return _recovering;
    }

    /** @brief Whether the client is ready: the handshake ran on the current connection. */
    [[nodiscard]] bool
    is_connected() const noexcept {
        return _state == qb::io::connection_state::ready;
    }

    /** @brief Where the client stands (`qb::io::connection_state`). */
    [[nodiscard]] qb::io::connection_state
    state() const noexcept {
        return _state;
    }

    /** @brief Why the client is down; meaningful in `connection_state::down`. */
    [[nodiscard]] qb::io::down_reason
    down_reason() const noexcept {
        return _down_reason;
    }

    /**
     * @brief Install the user's handshake hook: it runs on every connection, the first one included, after
     *        the protocol's handshake (`with_resp3()`, `with_auth()`, `with_select()`) and before any held
     *        work leaves.
     * @details The hook writes through the `handshake_client&` it is given, never through the client, and
     *          returns `true` to let the connection become ready. `false`, or a throw, refuses it: the
     *          client goes `down` with `handshake_refused` and no run retries it. Its commands still fail
     *          as a lost link does when the link drops under it, and that is one more attempt. Set it before
     *          `connect()`; an empty function removes it.
     */
    auto &
    set_handshake(handshake_hook hook) {
        _hs_hook = std::move(hook);
        return derived().self_();
    }

    /** @brief Authenticate every connection with the `default` user's password (AUTH, or HELLO 3 AUTH). */
    auto &
    with_auth(std::string password) {
        _hs_user.reset();
        _hs_password = std::move(password);
        return derived().self_();
    }

    /** @brief Authenticate every connection as an ACL user (AUTH user password, or HELLO 3 AUTH). */
    auto &
    with_auth(std::string user, std::string password) {
        _hs_user     = std::move(user);
        _hs_password = std::move(password);
        return derived().self_();
    }

    /** @brief SELECT database `index` on every connection, after authentication. */
    auto &
    with_select(long long index) {
        _hs_db = index;
        return derived().self_();
    }

    /** @brief Speak RESP3 on every connection (HELLO 3, which then also carries the credentials); `false` keeps RESP2. */
    auto &
    with_resp3(bool enabled = true) {
        _hs_resp3 = enabled;
        return derived().self_();
    }

    /** @brief Called each time the client becomes ready: the first connection, and after every reconnection. */
    auto &
    on_connected(std::function<void()> hook) {
        _on_connected = std::move(hook);
        return derived().self_();
    }

    /**
     * @brief Called once for every ready connection lost, with its cause (`user`, `peer`, `protocol`).
     * @details After the work in flight on it failed, before the reconnection decision: `state()` reads
     *          `reconnecting` or `down` as the policy stands, and a hook that disarms the policy prevents
     *          the run. `on_connected` and `on_disconnected` strictly alternate.
     */
    auto &
    on_disconnected(std::function<void(qb::io::down_reason)> hook) {
        _on_disconnected = std::move(hook);
        return derived().self_();
    }

    /** @brief Called each time the client reaches `down`, with the reason, after the held work failed. */
    auto &
    on_down(std::function<void(qb::io::down_reason)> hook) {
        _on_down = std::move(hook);
        return derived().self_();
    }

    /**
     * @brief What this client or consumer has done so far, by value (Huly QB-134): monotonic counts since
     *        construction -- two snapshots give a rate, a reconnection resets nothing. See `qb::redis::stats`.
     */
    [[nodiscard]] qb::redis::stats
    stats() const noexcept {
        return _stats;
    }

    /**
     * @brief Disconnect for good, and complete the teardown before returning.
     * @details Terminal: `down` with `down_reason::user`, whatever the reconnection policy -- an explicit
     *          disconnect starts no run (the qbm-http clients' rule). What was in flight fails with
     *          "disconnected"; a connection, a handshake or a run in progress is abandoned and what it held
     *          fails too. The io base's `disconnect_now()` runs the teardown on the spot, without a loop pass
     *          (Huly QB-202) -- called from a reply handler, as soon as that handler returns. Only a new
     *          `connect()` leaves `down`.
     */
    void
    disconnect() noexcept {
        _lost_since_ready = false; // an explicit disconnect ends the session: the next ready is no reconnect
        switch (_state) {
            case qb::io::connection_state::idle:
            case qb::io::connection_state::down:
                return;
            case qb::io::connection_state::ready:
                _state           = qb::io::connection_state::down;
                _down_reason     = qb::io::down_reason::user;
                _user_down_epoch = ++_epoch;
                drop_link_(qb::io::down_reason::user); // on(disconnected) says so, then settles down
                return;
            default:
                ++_epoch; // the attempt or the run in progress steps aside at its next step
                drop_link_(qb::io::down_reason::user);
                try {
                    enter_down_(qb::io::down_reason::user);
                } catch (...) {
                    QB_LOG_WARN("[qbm][redis] disconnect: a failure callback threw");
                }
                return;
        }
    }

private:
    qb::io::async::task<void>
    adopted_flow_(std::shared_ptr<bool> alive, std::uint64_t epoch) {
        if (!*alive || epoch != _epoch)
            co_return;
        const auto outcome = co_await handshake_(qb::duration{std::chrono::seconds(3)}, alive, epoch);
        if (!*alive || epoch != _epoch)
            co_return;
        if (outcome == attempt_outcome::refused)
            enter_down_(qb::io::down_reason::handshake_refused);
        else if (outcome == attempt_outcome::transport_failed)
            enter_down_(_loss_cause);
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
        qb::io::async::track_suspension(h, "redis");
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
    friend class RetryingClient<Redis<QB_IO_>>;
    template <typename, typename>
    friend class handshake_writer;

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
        enum class Kind : std::uint8_t { command, monitor_admission, monitor_reset, monitor_quit };
        std::unique_ptr<IReply> handler;
        bool                    blocking;
        Kind                    kind{Kind::command};
        std::uint16_t           verb{kUnnamedVerb}; ///< the verb's id (verb_id_): its family and name at completion
        qb::mono_time           sent_at{};          ///< when it left, if latency stats were on then; zero otherwise
    };

    std::queue<PendingReply> _replies;

    /**
     * @struct HeldReply
     * @brief A command submitted while the client was not ready: serialized, waiting for the handshake.
     * @details Its bytes sit in `_held_out`, in submission order, and leave -- after the handshake's -- once
     *          the client is ready; its handler then joins `_replies`. It fails once, with the reason, when
     *          the client goes down, or with "command timed out" once it waited `set_command_timeout()`.
     */
    struct HeldReply {
        std::unique_ptr<IReply>     handler;
        bool                        blocking;
        typename PendingReply::Kind kind;
        std::size_t                 bytes; ///< its serialized length at the front of what `_held_out` holds for it
        qb::mono_time               since; ///< when it was submitted (the held deadline counts from here)
        std::uint16_t               verb;  ///< the verb's id (verb_id_)
    };

    std::deque<HeldReply>     _held;
    qb::allocator::pipe<char> _held_out;
    std::size_t               _held_gen{0};       ///< bumped to retire a held-deadline watcher
    bool                      _held_armed{false}; ///< a held-deadline watcher is live

    // Observability (Huly QB-134): the counts live in the connector (stats()); these are the client's own.
    /// Latency histograms, one per family, allocated as one block by set_latency_stats(true); null when off.
    std::unique_ptr<std::array<latency_histogram, command_families>> _latency;
    /// Told of every completed command; empty by default (one predictable branch per completion).
    std::function<void(command_info const &)> _on_command_complete;
    /// Verbs the command table does not know (module commands), interned on first use so a completion can
    /// name them; stable addresses, looked up by view.
    std::deque<std::string>                                 _other_verbs;
    qb::unordered_flat_map<std::string_view, std::uint16_t> _other_index;

    static constexpr std::uint16_t kUnnamedVerb   = 0xFFFF; ///< past kMaxOtherVerbs unknown verbs: family other, no name
    static constexpr std::size_t   kMaxOtherVerbs = 256;

    /// The verb's id: its row in the command table -- one lookup, the one blocking detection needed anyway.
    [[nodiscard]] std::uint16_t
    verb_id_(std::string_view const name) {
        const auto row = detail::command_index(name);
        if (row < detail::command_table.size()) [[likely]]
            return static_cast<std::uint16_t>(row);
        return other_verb_id_(name);
    }

    /// A verb the table does not know as spelled: in another letter case, or interned (a module command).
    QB_NOINLINE std::uint16_t
                other_verb_id_(std::string_view const name) {
        const auto row = detail::command_index_folded(name);
        if (row < detail::command_table.size())
            return static_cast<std::uint16_t>(row);
        if (const auto it = _other_index.find(name); it != _other_index.end())
            return it->second;
        if (_other_verbs.size() >= kMaxOtherVerbs)
            return kUnnamedVerb;
        const auto id = static_cast<std::uint16_t>(detail::command_table.size() + _other_verbs.size());
        _other_verbs.emplace_back(name);
        _other_index.emplace(std::string_view{_other_verbs.back()}, id);
        return id;
    }

    [[nodiscard]] static bool
    verb_blocks_(std::uint16_t const verb) noexcept {
        return verb < detail::command_table.size() && detail::command_table[verb].traits.blocking;
    }

    [[nodiscard]] static command_family
    verb_family_(std::uint16_t const verb) noexcept {
        return verb < detail::command_table.size() ? detail::command_table[verb].traits.family : command_family::other;
    }

    [[nodiscard]] std::string_view
    verb_name_(std::uint16_t const verb) const noexcept {
        if (verb < detail::command_table.size())
            return detail::command_table[verb].verb;
        const std::size_t other = static_cast<std::size_t>(verb) - detail::command_table.size();
        return other < _other_verbs.size() ? std::string_view{_other_verbs[other]} : std::string_view{};
    }

    /// When a command leaves: read only while latency stats are on.
    [[nodiscard]] qb::mono_time
    sent_stamp_() const noexcept {
        return _latency ? qb::mono_now() : qb::mono_time{};
    }

    /**
     * @brief One command completed: counted, and -- when latency stats are on or a hook is set -- timed and
     *        reported. Called once per command, before its callback.
     */
    void
    complete_(std::uint16_t const verb, qb::mono_time const sent_at, command_outcome const outcome, std::string_view const error) noexcept {
        const auto family = verb_family_(verb);
        auto      &counts = this->stats_mut_();
        ++counts.commands;
        ++counts.by_family[static_cast<std::size_t>(family)];
        if (outcome != command_outcome::ok) [[unlikely]] {
            ++counts.errors;
            if (outcome == command_outcome::failed)
                ++counts.failed;
        }
        if (_latency || _on_command_complete) [[unlikely]]
            observe_(verb, family, sent_at, outcome, error);
    }

    /// Out of line: the histograms and the hook. An exception escaping the hook is contained and logged.
    QB_NOINLINE void
    observe_(std::uint16_t const verb, command_family const family, qb::mono_time const sent_at, command_outcome const outcome,
             std::string_view const error) noexcept {
        qb::duration took{};
        if (_latency && sent_at != qb::mono_time{}) {
            took = std::chrono::duration_cast<qb::duration>(qb::mono_now() - sent_at);
            (*_latency)[static_cast<std::size_t>(family)].record(took);
        }
        if (!_on_command_complete)
            return;
        try {
            const command_info info{family, verb_name_(verb), took, outcome == command_outcome::ok, outcome, error};
            _on_command_complete(info);
        } catch (const std::exception &ex) {
            QB_LOG_WARN("[qbm][redis] on_command_complete hook error: " << ex.what());
        } catch (...) {
            QB_LOG_WARN("[qbm][redis] on_command_complete hook threw a non-std exception");
        }
    }

    /// A reply arrived for `entry`: ok, or the server's error reply -- its text read only for a hook.
    void
    complete_reply_(PendingReply const &entry, ReplyValue const *reply) noexcept {
        if (reply && reply::is_error(*reply)) [[unlikely]] {
            complete_error_reply_(entry, *reply);
            return;
        }
        complete_(entry.verb, entry.sent_at, command_outcome::ok, {});
    }

    QB_NOINLINE void
    complete_error_reply_(PendingReply const &entry, ReplyValue const &reply) noexcept {
        std::string text;
        if (_on_command_complete) {
            try {
                text = reply.get_error_message();
            } catch (...) {
            }
        }
        complete_(entry.verb, entry.sent_at, command_outcome::error, text);
    }

    struct MonitorSession {
        explicit MonitorSession(std::unique_ptr<IReply> callback)
            : handler(std::move(callback)) {}

        std::unique_ptr<IReply>                 handler;
        std::deque<std::unique_ptr<ReplyValue>> queued;
        std::optional<std::string>              terminal;
        bool                                    admitted{false};
        bool                                    stopping{false};
        bool                                    delivering{false};
        bool                                    ended{false};
    };

    std::shared_ptr<MonitorSession> _monitor;

    static void
    invoke_monitor(IReply &handler, std::unique_ptr<ReplyValue> reply) {
        try {
            handler(std::move(reply));
        } catch (const std::exception &ex) {
            QB_LOG_WARN("[qbm][redis] MONITOR callback error: " << ex.what());
        } catch (...) {
            QB_LOG_WARN("[qbm][redis] MONITOR callback threw a non-std exception");
        }
    }

    static void
    fail_monitor(IReply &handler, const std::string &reason) {
        try {
            handler.fail(reason);
        } catch (const std::exception &ex) {
            QB_LOG_WARN("[qbm][redis] MONITOR callback error: " << ex.what());
        } catch (...) {
            QB_LOG_WARN("[qbm][redis] MONITOR callback threw a non-std exception");
        }
    }

    void
    deliver_monitor(std::shared_ptr<MonitorSession> session, std::unique_ptr<ReplyValue> reply) {
        session->queued.push_back(std::move(reply));
        if (session->delivering)
            return;
        session->delivering = true;
        while (!session->queued.empty()) {
            auto next = std::move(session->queued.front());
            session->queued.pop_front();
            invoke_monitor(*session->handler, std::move(next));
            if (session->ended) {
                session->queued.clear();
                break;
            }
        }
        session->delivering = false;
        if (session->terminal) {
            auto reason = std::move(*session->terminal);
            session->terminal.reset();
            fail_monitor(*session->handler, reason);
        }
    }

    void
    end_monitor(const std::shared_ptr<MonitorSession> &session, const std::string &reason) {
        if (!session || session->ended)
            return;
        session->ended = true;
        session->queued.clear();
        if (session->delivering)
            session->terminal = reason;
        else
            fail_monitor(*session->handler, reason);
    }

    [[nodiscard]] static bool
    is_monitor_exit_reply(const ReplyValue &reply, PendingReply::Kind kind) {
        if (reply.is_error())
            return true;
        if (!reply.is_simple_string())
            return false;
        const auto text = reply.as_string_view();
        return (kind == PendingReply::Kind::monitor_reset && text == "RESET") || (kind == PendingReply::Kind::monitor_quit && text == "OK");
    }

    [[nodiscard]] static bool
    command_is(std::string_view actual, std::string_view expected) noexcept {
        if (actual.size() != expected.size())
            return false;
        for (std::size_t i = 0; i < actual.size(); ++i) {
            const char c = actual[i];
            if ((c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c) != expected[i])
                return false;
        }
        return true;
    }

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
        return command_traits_of(name).blocking; // the command table's flag (stats.h, Huly QB-134)
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
     * and `alive` guards every touch of `*this`: before the first one too, as the
     * task first runs at the scheduler's next drain and the client may have been
     * destroyed since arm_deadline() spawned it. Exits quietly when cancelled
     * (generation bump), disabled, idle, or while a blocking command holds the
     * connection; re-arms while replies keep flowing.
     */
    qb::io::async::task<void>
    deadline_watch(std::size_t gen, std::shared_ptr<bool> alive) {
        if (!*alive)
            co_return; // destroyed before this first ran (Huly QB-202)
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
     * completes the teardown before it returns: on(disconnected) has failed
     * every pending command with the timeout reason by then (_deadline_tripped
     * is read there) -- we deliberately do NOT resolve awaiters here.
     */
    void
    on_command_deadline() {
        QB_LOG_WARN("[qbm][redis] command timeout (" << qb::detail::to_ev_seconds(_command_timeout)
                                                     << "s) exceeded with no reply; dropping connection");
        _deadline_tripped = true;
        this->drop_unresponsive_link_(); // the client's own decision: recoverable, cause `peer`
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
        // disconnect_now() can defer teardown until this read dispatch returns.
        // Drop any remaining frames from that socket without consuming a queued
        // RESET/QUIT handler; the disconnect event will fail it exactly once.
        if (qb__unlikely(_monitor || !this->link_open_())) {
            if (!this->link_open_())
                return;
            if (_monitor->admitted) {
                if (!_replies.empty() && _replies.front().kind != PendingReply::Kind::command
                    && _replies.front().kind != PendingReply::Kind::monitor_admission && msg.reply
                    && is_monitor_exit_reply(*msg.reply, _replies.front().kind)) {
                    auto entry = std::move(_replies.front());
                    _replies.pop();
                    ++_reply_progress;
                    arm_deadline();
                    complete_reply_(entry, msg.reply.get());
                    auto session = std::move(_monitor);
                    if (msg.reply->is_error()) {
                        _monitor           = std::move(session);
                        _monitor->stopping = false;
                    } else {
                        end_monitor(session, "monitor stopped");
                    }
                    try {
                        (*entry.handler)(std::move(msg.reply));
                    } catch (const std::exception &ex) {
                        QB_LOG_WARN("[qbm][redis] reply handler error: " << ex.what());
                    } catch (...) {
                        QB_LOG_WARN("[qbm][redis] reply handler threw a non-std exception");
                    }
                    return;
                }
                deliver_monitor(_monitor, std::move(msg.reply));
                return;
            }
        }
        if (_replies.empty()) {
            QB_LOG_WARN("[qbm][redis] Received unsolicited reply with no pending command, discarding");
            return;
        }
        auto entry = std::move(_replies.front());
        _replies.pop();
        if (entry.kind == PendingReply::Kind::monitor_admission) {
            auto session = _monitor;
            ++_reply_progress;
            arm_deadline();
            complete_reply_(entry, msg.reply.get()); // MONITOR completes at its admission
            if (!session)
                return;
            if (msg.reply && msg.reply->is_simple_string() && msg.reply->as_string_view() == "OK") {
                session->admitted = true;
                deliver_monitor(session, std::move(msg.reply));
            } else {
                _monitor.reset();
                deliver_monitor(session, std::move(msg.reply));
                session->ended = true;
            }
            return;
        }
        if (entry.blocking && _inflight_blocking > 0)
            --_inflight_blocking;
        // Record forward progress so a pending deadline watcher re-arms instead
        // of tripping, and arm a fresh one if more commands are still in flight.
        ++_reply_progress;
        arm_deadline();
        complete_reply_(entry, msg.reply.get());
        // Transfer ownership of reply to handler. Guard against a throwing
        // transform/user callback: this runs inside the libev read dispatch, so
        // an escaping exception would cross a noexcept boundary and terminate.
        try {
            (*entry.handler)(std::move(msg.reply));
        } catch (const std::exception &ex) {
            QB_LOG_WARN("[qbm][redis] reply handler error: " << ex.what());
        }
    }

    /// The link is gone (the connector's on(disconnected)): what was in flight on it fails once.
    void
    on_link_lost_(bool /*was_ready*/) {
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
        auto session = std::move(_monitor);
        end_monitor(session, timed_out ? "command timed out" : "disconnected");
        const std::string_view lost = timed_out ? "command timed out" : "disconnected";
        while (!pending.empty()) {
            auto entry = std::move(pending.front());
            pending.pop();
            complete_(entry.verb, entry.sent_at, command_outcome::failed, lost);
            if (entry.kind == PendingReply::Kind::monitor_admission)
                continue; // the session was failed exactly once above
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

    /// Ready: the held work leaves, in submission order, behind the handshake's.
    void
    on_ready_() {
        if (_held.empty())
            return;
        ++_held_gen;
        _held_armed = false;
        while (!_held.empty()) {
            auto entry = std::move(_held.front());
            _held.pop_front();
            this->put_held_bytes_(_held_out, entry.bytes);
            if (entry.blocking)
                ++_inflight_blocking;
            _replies.push(PendingReply{std::move(entry.handler), entry.blocking, entry.kind, entry.verb, sent_stamp_()});
        }
        _held_out.reset();
        this->arm_write_();
        arm_deadline();
    }

    /// Fail one held entry: a held MONITOR ends its session, every other entry its handler.
    void
    fail_held_entry_(HeldReply &entry, const std::string &reason) {
        complete_(entry.verb, qb::mono_time{}, command_outcome::failed, reason);
        if (entry.kind == PendingReply::Kind::monitor_admission) {
            if (auto session = std::move(_monitor))
                end_monitor(session, reason);
            return;
        }
        try {
            entry.handler->fail(reason);
        } catch (const std::exception &ex) {
            QB_LOG_WARN("[qbm][redis] callback error: " << ex.what());
        } catch (...) {
            QB_LOG_WARN("[qbm][redis] callback threw a non-std exception");
        }
    }

    /// Down: what was held fails once, with the reason. A failure callback that submits again finds the
    /// client down and is failed on the next loop pass.
    void
    on_down_(const std::string &reason) {
        ++_held_gen;
        _held_armed = false;
        auto held   = std::move(_held);
        _held.clear();
        _held_out.reset();
        for (auto &entry : held)
            fail_held_entry_(entry, reason);
    }

    /// Arm the held-deadline watcher if a held command can time out and none is live.
    void
    arm_held_deadline_() {
        if (_held_armed || _held.empty() || _command_timeout <= qb::duration::zero())
            return;
        _held_armed = true;
        qb::io::async::coro_scheduler().spawn(held_watch_(++_held_gen, this->connector_alive()));
    }

    /**
     * @brief Detached watcher: held commands fail with "command timed out" once they waited the command
     *        timeout, oldest first, while the client is not ready.
     */
    qb::io::async::task<void>
    held_watch_(std::size_t gen, std::shared_ptr<bool> alive) {
        for (;;) {
            if (!*alive || gen != _held_gen)
                co_return; // destroyed, released or failed meanwhile
            if (_held.empty() || _command_timeout <= qb::duration::zero()) {
                _held_armed = false;
                co_return;
            }
            const qb::duration wait = this->held_wait_(_held.front().since, _command_timeout);
            if (wait > qb::duration::zero()) {
                co_await qb::io::async::sleep(wait);
                continue;
            }
            auto entry = std::move(_held.front());
            _held.pop_front();
            _held_out.free_front(entry.bytes);
            fail_held_entry_(entry, "command timed out");
        }
    }

    /**
     * @brief A command submitted while the client is not ready: held when a connection, a handshake or a
     *        run is in progress, failed on the next loop pass when the client is idle or down.
     */
    template <typename... Args>
    QB_NOINLINE void
    submit_unready_(std::unique_ptr<IReply> handler, typename PendingReply::Kind kind, std::string const &name, Args &&...args) {
        const std::uint16_t verb = verb_id_(name);
        if (!this->holding_()) {
            auto reason = this->refusal_text_();
            complete_(verb, qb::mono_time{}, command_outcome::failed, reason);
            this->fail_soon_(std::move(handler), std::move(reason));
            return;
        }
        const std::size_t before = _held_out.size();
        if (auto error = this->serialize_into_(_held_out, name, std::forward<Args>(args)...)) {
            complete_(verb, qb::mono_time{}, command_outcome::failed, *error);
            handler->fail(*error); // nothing held, nothing written (Huly QB-255)
            return;
        }
        ++this->stats_mut_().held;
        _held.push_back(HeldReply{std::move(handler), verb_blocks_(verb), kind, _held_out.size() - before, qb::mono_now(), verb});
        if (_monitor)
            _monitor->stopping = true;
        arm_held_deadline_();
    }

    /**
     * @brief The handshake's write path: straight to the link, the gate passed by construction.
     * @details Called by the handshake hook's `handshake_client` only while its handshake is live.
     */
    template <typename Ret, typename Func, typename... Args>
    void
    direct_command_(Func &&func, std::string const &name, Args &&...args) {
        auto                handler = std::make_unique<TReply<Func, Ret>>(std::forward<Func>(func));
        const std::uint16_t verb    = verb_id_(name);
        if (command_is(name, "MONITOR")) {
            complete_(verb, qb::mono_time{}, command_outcome::failed, "MONITOR cannot run in the handshake");
            handler->fail("MONITOR cannot run in the handshake");
            return;
        }
        if (auto error = this->try_put_command(name, std::forward<Args>(args)...)) {
            complete_(verb, qb::mono_time{}, command_outcome::failed, *error);
            handler->fail(*error);
            return;
        }
        const bool blocking = verb_blocks_(verb);
        _replies.push(PendingReply{std::move(handler), blocking, PendingReply::Kind::command, verb, sent_stamp_()});
        if (blocking)
            ++_inflight_blocking;
        arm_deadline();
    }

    Redis &
    self_() noexcept {
        return *this;
    }

    /// A retrying view re-issued a command (stats().retries).
    void
    count_retry_() noexcept {
        ++this->stats_mut_().retries;
    }

public:
    Redis() = default;
    explicit Redis(qb::io::uri uri)
        : connector<QB_IO_, Redis<QB_IO_>>(std::move(uri)) {}

    /**
     * @brief Send a raw command with a callback invoked on its reply (callback API).
     *
     * Serializes the command into the outbound pipe, then registers the reply handler
     * -- before any byte leaves: writing to the pipe sends nothing. Multiple calls
     * pipeline naturally (one handler + one request per call, FIFO). Drain with
     * await() or your event loop. A command that cannot be serialized (an argument
     * over `REDIS_MAX_STRING_SIZE`) leaves nothing behind -- no handler, no partial
     * frame -- and @p func receives a failed `Reply` before this returns, the reason
     * in `error()` (Huly QB-255).
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
        auto                handler = std::make_unique<TReply<Func, Ret>>(std::forward<Func>(func));
        const std::uint16_t verb    = verb_id_(name);
        if (command_is(name, "MONITOR")) [[unlikely]] {
            complete_(verb, qb::mono_time{}, command_outcome::failed, "use monitor(callback) for the MONITOR stream");
            handler->fail("use monitor(callback) for the MONITOR stream");
            return *this;
        }
        const bool resetting = command_is(name, "RESET");
        const bool quitting  = command_is(name, "QUIT");
        const bool exiting   = resetting || quitting;
        if (_monitor && (!exiting || _monitor->stopping)) {
            complete_(verb, qb::mono_time{}, command_outcome::failed, "MONITOR owns this connection; only RESET or QUIT may end it");
            handler->fail("MONITOR owns this connection; only RESET or QUIT may end it");
            return *this;
        }
        const auto kind =
            _monitor ? (resetting ? PendingReply::Kind::monitor_reset : PendingReply::Kind::monitor_quit) : PendingReply::Kind::command;
        if (!this->ready_()) [[unlikely]] {
            // Not ready: held behind the handshake, or failed when nothing would ever send it (R2, R4).
            submit_unready_(std::move(handler), kind, name, std::forward<Args>(args)...);
            return *this;
        }
        if (auto error = this->try_put_command(name, std::forward<Args>(args)...)) {
            complete_(verb, qb::mono_time{}, command_outcome::failed, *error);
            handler->fail(*error); // the client is consistent again: a callback may issue its next command
            return *this;
        }
        const bool blocking = verb_blocks_(verb);
        _replies.push(PendingReply{std::move(handler), blocking, kind, verb, sent_stamp_()});
        if (_monitor)
            _monitor->stopping = true;
        if (blocking)
            ++_inflight_blocking;
        arm_deadline();
        return *this;
    }

    /**
     * @brief Start a MONITOR stream on this connection.
     *
     * The callback receives the initial +OK, every subsequent monitor line, and
     * one failed reply when the stream ends. Use a dedicated connection. While it
     * is active, commands other than RESET and QUIT fail locally; RESET or QUIT
     * ends the stream at its own reply. A reconnect never restarts MONITOR.
     */
    template <typename Func>
    requires std::invocable<Func, Reply<std::string> &&>
    Redis &
    monitor(Func &&func) {
        auto                handler = std::make_unique<TReply<Func, std::string>>(std::forward<Func>(func));
        const std::uint16_t verb    = verb_id_("MONITOR");
        if (_monitor) {
            complete_(verb, qb::mono_time{}, command_outcome::failed, "MONITOR already active");
            handler->fail("MONITOR already active");
            return *this;
        }
        if (!this->ready_()) [[unlikely]] {
            if (!this->holding_()) {
                auto reason = this->refusal_text_();
                complete_(verb, qb::mono_time{}, command_outcome::failed, reason);
                this->fail_soon_(std::move(handler), std::move(reason));
                return *this;
            }
            const std::size_t before = _held_out.size();
            if (auto error = this->serialize_into_(_held_out, "MONITOR")) {
                complete_(verb, qb::mono_time{}, command_outcome::failed, *error);
                handler->fail(*error);
                return *this;
            }
            _monitor = std::make_shared<MonitorSession>(std::move(handler));
            ++this->stats_mut_().held;
            _held.push_back(HeldReply{nullptr, false, PendingReply::Kind::monitor_admission, _held_out.size() - before, qb::mono_now(), verb});
            arm_held_deadline_();
            return *this;
        }
        if (auto error = this->try_put_command("MONITOR")) {
            complete_(verb, qb::mono_time{}, command_outcome::failed, *error);
            handler->fail(*error);
            return *this;
        }
        _monitor = std::make_shared<MonitorSession>(std::move(handler));
        _replies.push(PendingReply{nullptr, false, PendingReply::Kind::monitor_admission, verb, sent_stamp_()});
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
     * Runs @c qb::io::async::listener::current.run(EVRUN_NOWAIT) until no command
     * waits for its reply: none sent and unanswered, none held while the client is not
     * ready, no failure still due from the loop. This does **not** block the thread in
     * the kernel; each iteration is a non-blocking poll. The **caller** still runs
     * synchronously until every enqueued callback has been invoked (success, Redis
     * error, or failure: the link lost, the client down, the command timeout).
     *
     * @warning Call from the same thread / loop that drives Redis I/O. Do not
     *          confuse with coroutine @c co_await — this is explicit draining for
     *          the callback / pipeline API.
     *
     * @note Uses @c listener::current.run(EVRUN_NOWAIT), not @c async::run(), so a
     *          coroutine body may call it. There the coroutines a pass makes ready wait
     *          for that body to yield (qb-io, Huly QB-253) -- the command deadline's
     *          watcher among them: from a coroutine, @c await() returns on the replies
     *          or a disconnect, never on @c set_command_timeout(). Nor does a command
     *          held there ever leave: the connection, its handshake and the reconnection
     *          run are coroutines too -- from a coroutine, @c co_await the command.
     */
    Redis &
    await() {
        while (!_replies.empty() || !_held.empty() || this->_failing_soon != 0)
            qb::io::async::listener::current.run(EVRUN_NOWAIT);
        return *this;
    }

    /**
     * @brief Number of commands awaiting their reply (for pipeline debugging): those sent on the link, and
     *        those held while the client is not ready.
     */
    [[nodiscard]] std::size_t
    pending_reply_count() const noexcept {
        return _replies.size() + _held.size();
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
        if (_command_timeout <= qb::duration::zero()) {
            cancel_deadline();
        } else {
            arm_deadline();
            ++_held_gen; // a held command's deadline counts from its submission, against the new timeout
            _held_armed = false;
            arm_held_deadline_();
        }
    }

    /** @brief Current command deadline (`qb::duration::zero()` = disabled). */
    [[nodiscard]] qb::duration
    command_timeout() const noexcept {
        return _command_timeout;
    }

    /**
     * @brief Turn the per-family latency histograms on or off (Huly QB-134).
     * @details On, every command is timed from its send to its completion and recorded in its family's
     *          histogram (`latency()`); the histograms are allocated then, as one block. Off -- the default --
     *          the client reads no clock for it and holds no histogram; turning it off drops what was recorded.
     *          Commands already in flight when it is turned on are not timed.
     */
    void
    set_latency_stats(bool const enabled) {
        if (!enabled) {
            _latency.reset();
            return;
        }
        if (!_latency)
            _latency = std::make_unique<std::array<latency_histogram, command_families>>();
    }

    /** @brief Whether the latency histograms are on. */
    [[nodiscard]] bool
    latency_stats() const noexcept {
        return _latency != nullptr;
    }

    /**
     * @brief The latency histogram of one family, by value: p50 / p95 / p99 are its `percentile(0.50)`,
     *        `percentile(0.95)`, `percentile(0.99)`. Empty while latency stats are off.
     */
    [[nodiscard]] latency_histogram
    latency(command_family const family) const noexcept {
        return _latency ? (*_latency)[static_cast<std::size_t>(family)] : latency_histogram{};
    }

    /**
     * @brief Be told of every completed command (Huly QB-134; qbm-pgsql's `on_query_complete`).
     * @details @p hook receives one `qb::redis::command_info` per command, on the client's thread, as the command
     *          completes and before its own callback or awaiter sees the result: a value, the server's error
     *          reply, or a failure without an answer -- refused locally, the link lost, a timeout, the client
     *          down. Keep it short; it must not drive this client (no command, no `disconnect()`). The views it
     *          receives are valid only during the call. An exception escaping it is contained and logged. An
     *          empty `std::function` -- the default -- removes it; without a hook a completion costs one
     *          predictable branch. To feed a metrics actor, push from it (`qb::Actor::push`): the event carries
     *          copies, never the views.
     * @return *this
     */
    Redis &
    on_command_complete(std::function<void(command_info const &)> hook) {
        _on_command_complete = std::move(hook);
        return *this;
    }

    /**
     * @brief A view of this client whose eligible commands are retried (Huly QB-133): the whole command
     *        surface (MULTI and the transaction commands excepted), coroutine and callback forms.
     * @details `co_await redis.retrying().get("k")`. What is retried, when, and how often: `command_retry`.
     *          An ineligible command passes through unchanged. Each command's callback or awaiter sees one
     *          outcome, the last; every re-issue counts in `stats().retries`, every attempt in
     *          `stats().commands`. Cheap to make (a pointer, a liveness token, the policy): make one where it
     *          is used. It must not outlive the client -- checked in debug; a command through a view whose
     *          client is gone fails, and a retry pending when the client goes is dropped, never called.
     */
    [[nodiscard]] RetryingClient<Redis<QB_IO_>>
    retrying(qb::redis::command_retry retry = {}) {
        return RetryingClient<Redis<QB_IO_>>{*this, this->connector_alive(), std::move(retry)};
    }

    template <typename T, typename Func>
    [[nodiscard]] auto
    make_coro_command(Func &&operation) {
        return make_redis_awaiter<T>(std::forward<Func>(operation));
    }

    /** @brief Stop a MONITOR stream before another frame from the current read batch is dispatched. */
    void
    disconnect() noexcept {
        connector<QB_IO_, Redis<QB_IO_>>::disconnect();
        // disconnect_now() defers teardown when called inside a reply callback. Close
        // the stream here so already-parsed frames cannot reach that old session.
        if (auto session = std::move(_monitor)) {
            try {
                end_monitor(session, "disconnected");
            } catch (const std::exception &ex) {
                QB_LOG_WARN("[qbm][redis] MONITOR teardown error: " << ex.what());
            } catch (...) {
                QB_LOG_WARN("[qbm][redis] MONITOR teardown threw a non-std exception");
            }
        }
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
    template <typename, typename>
    friend class handshake_writer;
    friend class HandshakeClient<RedisConsumer<QB_IO_, Derived>>;

    constexpr Derived &
    derived() {
        return static_cast<Derived &>(*this);
    }

public:
    using redis_protocol = typename connector<QB_IO_, RedisConsumer<QB_IO_, Derived>>::redis_protocol;

private:
    enum class MsgType { SUBSCRIBE, UNSUBSCRIBE, PSUBSCRIBE, PUNSUBSCRIBE, SSUBSCRIBE, SUNSUBSCRIBE, MESSAGE, PMESSAGE, SMESSAGE, UNKNOWN };

    static MsgType
    msg_type(const std::string_view &type) {
        static const qb::unordered_flat_map<std::string_view, MsgType> str_to_enum{
            {"message", MsgType::MESSAGE},           {"pmessage", MsgType::PMESSAGE},       {"smessage", MsgType::SMESSAGE},
            {"subscribe", MsgType::SUBSCRIBE},       {"unsubscribe", MsgType::UNSUBSCRIBE}, {"psubscribe", MsgType::PSUBSCRIBE},
            {"punsubscribe", MsgType::PUNSUBSCRIBE}, {"ssubscribe", MsgType::SSUBSCRIBE},   {"sunsubscribe", MsgType::SUNSUBSCRIBE}
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

    /**
     * @struct HeldCommand
     * @brief A command submitted while the consumer was not ready, waiting for the handshake.
     * @details A plain command is serialized at once (`bytes` at the front of `_held_out`); a
     *          (P)(UN)SUBSCRIBE keeps its arguments and is serialized when it leaves, because its
     *          confirmation count is predicted against the subscriptions of the connection it reaches.
     *          It waits until the consumer is ready, or fails once when the consumer goes down.
     */
    struct HeldCommand {
        std::unique_ptr<IReply>  handler;
        std::size_t              bytes    = 0;
        const char              *pubsub   = nullptr; ///< the verb of a held (P|S)(UN)SUBSCRIBE, else null
        bool                     is_unsub = false;
        subscription_kind        kind     = subscription_kind::channel;
        std::vector<std::string> names;
    };

    std::deque<HeldCommand>   _held;
    qb::allocator::pipe<char> _held_out;

    /// Predicted active subscriptions, advanced synchronously at send time so the
    /// expected confirmation count of "unsubscribe-all" (UNSUBSCRIBE/PUNSUBSCRIBE
    /// with no argument) is exact even when pipelined behind pending subscribes.
    qb::unordered_flat_set<std::string> _pred_channels;
    qb::unordered_flat_set<std::string> _pred_patterns;
    qb::unordered_flat_set<std::string> _pred_shards;
    bool                                _reset_pending = false;

    /// The subscriptions the SERVER confirmed: what a reconnection replays (R5). A confirmation frame adds or
    /// removes its name, a successful RESET clears both; a lost connection keeps them.
    qb::unordered_flat_set<std::string> _confirmed_channels;
    qb::unordered_flat_set<std::string> _confirmed_patterns;
    qb::unordered_flat_set<std::string> _confirmed_shards;

    std::function<void(qb::io::gap_info const &)> _on_gap;

protected:
    /// A hole is pending in the stream: the marker goes before the next item, or at the next ready (R5, R7).
    bool             _gap_pending = false;
    qb::io::gap_info _gap{};

    /// Coalesce one more loss into the pending gap: a reconnect among them names the gap, drops add up.
    void
    mark_gap_(qb::io::gap_reason reason, std::uint64_t lost) noexcept {
        if (!_gap_pending) {
            _gap         = qb::io::gap_info{reason, lost};
            _gap_pending = true;
            return;
        }
        if (reason == qb::io::gap_reason::reconnect)
            _gap.reason = qb::io::gap_reason::reconnect;
        _gap.lost += lost;
    }

    /// The marker of the pending gap: an empty channel, which a published message never has, and `kind` gap.
    [[nodiscard]] qb::redis::message
    gap_marker_() const {
        qb::redis::message marker;
        marker.kind = qb::redis::message_kind::gap;
        marker.gap  = _gap;
        return marker;
    }

    /// The marker of the pending gap was delivered or queued: the gap is closed, on_gap is told.
    void
    gap_flushed_() {
        const qb::io::gap_info gap = _gap;
        _gap_pending               = false;
        _gap                       = qb::io::gap_info{};
        ++this->stats_mut_().gaps;
        if (!_on_gap)
            return;
        try {
            _on_gap(gap);
        } catch (const std::exception &ex) {
            QB_LOG_WARN("[qbm][redis] on_gap hook error: " << ex.what());
        } catch (...) {
            QB_LOG_WARN("[qbm][redis] on_gap hook threw a non-std exception");
        }
    }

private:
    template <typename Ret, typename Func, typename... Args>
    requires std::invocable<Func, Reply<Ret> &&>
    Derived &
    command(Func &&func, std::string const &name, Args &&...args) {
        auto handler = std::make_unique<TReply<Func, Ret>>(std::forward<Func>(func));
        if (!this->ready_()) [[unlikely]] {
            hold_command_(std::move(handler), name, std::forward<Args>(args)...);
            return derived();
        }
        if (auto error = this->try_put_command(name, std::forward<Args>(args)...)) {
            handler->fail(*error); // nothing queued, nothing written (Huly QB-255)
            return derived();
        }
        _replies.push(PendingReply{std::move(handler), 1});
        return derived();
    }

    /// The handshake's write path for a plain command: straight to the link.
    template <typename Ret, typename Func, typename... Args>
    void
    direct_command_(Func &&func, std::string const &name, Args &&...args) {
        auto handler = std::make_unique<TReply<Func, Ret>>(std::forward<Func>(func));
        if (auto error = this->try_put_command(name, std::forward<Args>(args)...)) {
            handler->fail(*error);
            return;
        }
        _replies.push(PendingReply{std::move(handler), 1});
    }

    /// A plain command submitted while not ready: held, or failed on the next loop pass when idle or down.
    template <typename... Args>
    QB_NOINLINE void
    hold_command_(std::unique_ptr<IReply> handler, std::string const &name, Args &&...args) {
        if (!this->holding_()) {
            this->fail_soon_(std::move(handler), this->refusal_text_());
            return;
        }
        const std::size_t before = _held_out.size();
        if (auto error = this->serialize_into_(_held_out, name, std::forward<Args>(args)...)) {
            handler->fail(*error);
            return;
        }
        HeldCommand entry;
        entry.handler = std::move(handler);
        entry.bytes   = _held_out.size() - before;
        _held.push_back(std::move(entry));
    }

    // RESET is a reply-ordered barrier for Pub/Sub state. Do not infer its
    // outcome at send time: after an error the old subscriptions still exist.
    // Until its reply arrives, later Pub/Sub commands have no reliable count.
    template <typename Func>
    struct ResetCompletion {
        RedisConsumer *owner;
        Func           callback; // retains lvalue references and owns rvalue callbacks

        void
        operator()(Reply<status> &&reply) {
            owner->_reset_pending = false;
            if (reply.ok()) {
                owner->_pred_channels.clear();
                owner->_pred_patterns.clear();
                owner->_pred_shards.clear();
                owner->_confirmed_channels.clear(); // the server forgot them: nothing to replay
                owner->_confirmed_patterns.clear();
                owner->_confirmed_shards.clear();
            }
            callback(std::move(reply));
        }
    };

    template <typename Func>
    Derived &
    reset_session_command(Func &&func) {
        if (_reset_pending) {
            std::forward<Func>(func)(Reply<status>{false, {}, nullptr, "RESET already pending"});
            return derived();
        }
        auto completion = ResetCompletion<Func>{this, std::forward<Func>(func)};
        _reset_pending  = true;
        try {
            return command<status>(std::move(completion), "RESET");
        } catch (...) {
            _reset_pending = false;
            throw;
        }
    }

    /**
     * @brief Compute the number of confirmation frames Redis will emit and
     *        advance the predicted-subscription state accordingly.
     *
     * SUBSCRIBE/PSUBSCRIBE/SSUBSCRIBE acknowledge once per channel argument (even
     * repeats), so the count is the argument count. The UNSUBSCRIBE forms with
     * explicit names acknowledge once per name; with no name they acknowledge once
     * per currently-subscribed name of their kind (or once when none are). One path
     * for the three kinds (Huly QB-127).
     */
    int
    predict_confirmations(bool is_unsub, subscription_kind kind, const std::vector<std::string> &names) {
        auto &set = kind == subscription_kind::pattern ? _pred_patterns : kind == subscription_kind::shard ? _pred_shards : _pred_channels;
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
     * @param cmd      Redis command verb ("SUBSCRIBE", "PUNSUBSCRIBE", "SSUBSCRIBE", …).
     * @param is_unsub True for the UNSUBSCRIBE forms.
     * @param kind     Channel, pattern or shard channel: the set of names it manages.
     * @param names    Channel/pattern arguments (empty ⇒ unsubscribe-all).
     */
    template <typename Func>
    requires std::invocable<Func, Reply<qb::redis::subscription> &&>
    Derived &
    pubsub_command(Func &&func, const char *cmd, bool is_unsub, subscription_kind kind, const std::vector<std::string> &names) {
        auto handler = std::make_unique<TReply<Func, qb::redis::subscription>>(std::forward<Func>(func));
        if (_reset_pending) {
            handler->fail("RESET pending; await its reply before Pub/Sub commands");
            return derived();
        }
        if (!this->ready_()) [[unlikely]] {
            hold_pubsub_(std::move(handler), cmd, is_unsub, kind, names);
            return derived();
        }
        put_pubsub_(std::move(handler), cmd, is_unsub, kind, names);
        return derived();
    }

    /// Serialize a (P|S)(UN)SUBSCRIBE on the link and predict its confirmations.
    void
    put_pubsub_(std::unique_ptr<IReply> handler, const char *cmd, bool is_unsub, subscription_kind kind,
                const std::vector<std::string> &names) {
        // Serialized first, the prediction advanced only for a command that will reach the server (Huly QB-255).
        if (auto error = names.empty() ? this->try_put_command(cmd) : this->try_put_command(cmd, names)) {
            handler->fail(*error);
            return;
        }
        const int expected = predict_confirmations(is_unsub, kind, names);
        _replies.push(PendingReply{std::move(handler), expected, /*is_subscription=*/true});
    }

    /// The handshake's write path for a (P|S)(UN)SUBSCRIBE: straight to the link.
    template <typename Func>
    void
    direct_pubsub_command_(Func &&func, const char *cmd, bool is_unsub, subscription_kind kind, const std::vector<std::string> &names) {
        put_pubsub_(std::make_unique<TReply<Func, qb::redis::subscription>>(std::forward<Func>(func)), cmd, is_unsub, kind, names);
    }

    /// A (P|S)(UN)SUBSCRIBE submitted while not ready: held with its arguments, or failed soon when idle or down.
    QB_NOINLINE void
    hold_pubsub_(std::unique_ptr<IReply> handler, const char *cmd, bool is_unsub, subscription_kind kind,
                 const std::vector<std::string> &names) {
        if (!this->holding_()) {
            this->fail_soon_(std::move(handler), this->refusal_text_());
            return;
        }
        HeldCommand entry;
        entry.handler  = std::move(handler);
        entry.pubsub   = cmd;
        entry.is_unsub = is_unsub;
        entry.kind     = kind;
        entry.names    = names;
        _held.push_back(std::move(entry));
    }

    /// Ready: the held work leaves in submission order; a held (P|S)(UN)SUBSCRIBE is predicted now.
    void
    on_ready_() {
        if (!_held.empty()) {
            while (!_held.empty()) {
                auto entry = std::move(_held.front());
                _held.pop_front();
                if (entry.pubsub) {
                    put_pubsub_(std::move(entry.handler), entry.pubsub, entry.is_unsub, entry.kind, entry.names);
                } else {
                    this->put_held_bytes_(_held_out, entry.bytes);
                    _replies.push(PendingReply{std::move(entry.handler), 1});
                }
            }
            _held_out.reset();
            this->arm_write_();
        }
        derived().consumer_ready_();
    }

    static void
    fail_held_entry_(HeldCommand &entry, const std::string &reason) {
        try {
            entry.handler->fail(reason);
        } catch (const std::exception &ex) {
            QB_LOG_WARN("[qbm][redis] consumer callback error: " << ex.what());
        } catch (...) {
            QB_LOG_WARN("[qbm][redis] consumer callback threw a non-std exception");
        }
    }

    /// Down: what was held fails once, with the reason.
    void
    on_down_(const std::string &reason) {
        auto held = std::move(_held);
        _held.clear();
        _held_out.reset();
        for (auto &entry : held)
            fail_held_entry_(entry, reason);
        derived().consumer_down_();
    }

    Derived &
    self_() noexcept {
        return derived();
    }

    // The consumer's part of each transition; RedisCallbackConsumer and RedisCoroConsumer hide the ones they need.
    void
    consumer_ready_() {}
    void
    consumer_down_() {}
    void
    consumer_link_lost_(bool /*was_ready*/) {}
    void
    consumer_lifecycle_start_() {}

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
                    ++this->stats_mut_().messages_received;
                    try {
                        derived().on(parse<qb::redis::message>(raw));
                    } catch (const std::exception &e) {
                        QB_LOG_WARN("[qbm][redis] message handler error: " << e.what());
                    }
                    return;
                case MsgType::SMESSAGE:
                    // Sharded pub/sub (Redis 7+, Huly QB-127): the shape of a message, delivered as one.
                    ++this->stats_mut_().messages_received;
                    try {
                        derived().on(parse<qb::redis::message>(raw));
                    } catch (const std::exception &e) {
                        QB_LOG_WARN("[qbm][redis] message handler error: " << e.what());
                    }
                    return;
                case MsgType::PMESSAGE:
                    ++this->stats_mut_().messages_received;
                    try {
                        derived().on(parse<qb::redis::pmessage>(raw));
                    } catch (const std::exception &e) {
                        QB_LOG_WARN("[qbm][redis] message handler error: " << e.what());
                    }
                    return;
                case MsgType::SUBSCRIBE:
                case MsgType::UNSUBSCRIBE:
                case MsgType::PSUBSCRIBE:
                case MsgType::PUNSUBSCRIBE:
                case MsgType::SSUBSCRIBE:
                case MsgType::SUNSUBSCRIBE: {
                    confirm_(type, raw); // the server's word on what this consumer is subscribed to
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

            // A RESP3 PUSH frame of a kind this consumer does not know -- `invalidate` from
            // client-side caching (CLIENT TRACKING on this connection), whatever a later server
            // adds -- is OUT OF
            // BAND: it answers no command, so it must not pop the handler at the head of the
            // FIFO. Until 3.2 it did exactly that (Huly QB-141), the class of defect the
            // `is_subscription` guard above was written to kill on the confirmation side, and
            // one that desynchronises the reply/command FIFO for the connection's lifetime.
            // Dropped, and said so; the plain client discards every push the same way. A RESP2
            // ARRAY whose first element is not a pub/sub kind is a different thing and keeps
            // its path below: in subscriber mode that is the reply to a command -- `PING`
            // answers `["pong", ""]` -- and it belongs to the FIFO head.
            if (reply::is_push(raw)) {
                QB_LOG_WARN("[qbm][redis] dropping PUSH frame of unknown kind '" << reply::parse<std::string_view>(*elem0)
                                                                                 << "' (out of band: it resolves no pending command)");
                return;
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

    /// Record what a confirmation frame says: a name subscribed or unsubscribed, server-side.
    void
    confirm_(MsgType type, const ReplyValue &raw) {
        auto const *name = reply::get_pubsub_element(raw, 1);
        if (!name || name->is_null())
            return; // UNSUBSCRIBE from nothing: no name
        auto value = std::string{reply::parse<std::string_view>(*name)};
        switch (type) {
            case MsgType::SUBSCRIBE:
                _confirmed_channels.insert(std::move(value));
                break;
            case MsgType::UNSUBSCRIBE:
                _confirmed_channels.erase(value);
                break;
            case MsgType::PSUBSCRIBE:
                _confirmed_patterns.insert(std::move(value));
                break;
            case MsgType::PUNSUBSCRIBE:
                _confirmed_patterns.erase(value);
                break;
            case MsgType::SSUBSCRIBE:
                _confirmed_shards.insert(std::move(value));
                break;
            case MsgType::SUNSUBSCRIBE:
                _confirmed_shards.erase(value);
                break;
            default:
                break;
        }
    }

    /// The session a reconnection replays: the confirmed subscriptions (R5).
    [[nodiscard]] bool
    has_session_to_replay_() const noexcept {
        return !_confirmed_channels.empty() || !_confirmed_patterns.empty() || !_confirmed_shards.empty();
    }

    /**
     * @brief Replay the confirmed subscriptions through the handshake's handle (R5).
     * @details Copied before the first suspension: the confirmations of the replay land in the same sets.
     *          Messages for a channel confirmed early may arrive before the replay ends; they queue behind the
     *          gap marker the lost connection left pending.
     */
    qb::io::async::task<std::optional<std::string>>
    replay_session_(typename connector<QB_IO_, RedisConsumer<QB_IO_, Derived>>::handshake_client &hs) {
        std::vector<std::string> channels(_confirmed_channels.begin(), _confirmed_channels.end());
        std::vector<std::string> patterns(_confirmed_patterns.begin(), _confirmed_patterns.end());
        std::vector<std::string> shards(_confirmed_shards.begin(), _confirmed_shards.end());
        if (!channels.empty()) {
            auto r = co_await hs.subscribe(channels);
            if (!r)
                co_return r.error();
        }
        if (!patterns.empty()) {
            auto r = co_await hs.psubscribe(patterns);
            if (!r)
                co_return r.error();
        }
        // One SSUBSCRIBE per shard channel: on a cluster, names of several slots in one call are refused
        // (CROSSSLOT), and the consumer does not compute slots.
        for (auto const &shard : shards) {
            auto r = co_await hs.ssubscribe(shard);
            if (!r)
                co_return r.error();
        }
        co_return std::nullopt;
    }

    /// The link is gone (the connector's on(disconnected)): what was in flight on it fails once.
    void
    on_link_lost_(bool was_ready) {
        // Predicted subscription state is meaningless across a reconnect. A RESET in flight clears
        // _reset_pending through its own completion, failed below; a held one keeps it set.
        _pred_channels.clear();
        _pred_patterns.clear();
        _pred_shards.clear();
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
        // Whatever the server publishes until the next connection is ready is lost: a gap (R5). A link that
        // never carried a message -- the first connection's failed handshake -- leaves none.
        if (this->ever_ready_())
            mark_gap_(qb::io::gap_reason::reconnect, 0);
        derived().consumer_link_lost_(was_ready);
    }

    /// A connect() leaves idle or down: a consumer's stream reopens (R6).
    void
    on_lifecycle_start_() {
        derived().consumer_lifecycle_start_();
    }

    void
    on(qb::redis::error &&error) { // handler-access: this class's own on(message) calls it
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
        while (!_replies.empty() || !_held.empty() || this->_failing_soon != 0)
            qb::io::async::listener::current.run(EVRUN_NOWAIT);
        return derived();
    }

    /** @brief Number of subscription/command replies still awaited, sent or held (debugging). */
    [[nodiscard]] std::size_t
    pending_reply_count() const noexcept {
        return _replies.size() + _held.size();
    }

    /**
     * @brief Called each time a gap marker enters the stream, with what it carries (R5, R7).
     * @details A reconnection leaves a gap of unknown size (`gap_reason::reconnect`); a full queue one of
     *          `lost` dropped messages (`gap_reason::overflow`). Consecutive losses coalesce into one marker.
     */
    Derived &
    on_gap(std::function<void(qb::io::gap_info const &)> hook) {
        _on_gap = std::move(hook);
        return derived();
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
    friend RedisConsumer<QB_IO_, RedisCallbackConsumer<QB_IO_>>;

    using cb_msg_t  = std::function<void(qb::redis::message &&)>;
    using cb_err_t  = std::function<void(qb::redis::error &&)>;
    using cb_disc_t = std::function<void(qb::io::down_reason)>;

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

    // Exception safety of the user callable is handled UPSTREAM: RedisConsumer::on wraps the
    // message/pmessage dispatch in try/catch, and qb::protocol::redis::onMessage adds a
    // catch(...) backstop before the libev noexcept boundary — so a throwing handler is contained
    // and never terminates. These dispatchers therefore stay branch-free.
    void
    on(qb::redis::message &&msg) {
        if (this->_gap_pending) [[unlikely]]
            deliver_gap_();
        _on_message(std::move(msg));
    }

    /// The pending gap's marker, through on_message, before the next item (R5).
    QB_NOINLINE void
    deliver_gap_() {
        auto marker = this->gap_marker_();
        this->gap_flushed_();
        _on_message(std::move(marker));
    }

    /// Ready: the pending gap's marker goes out even when no message follows yet. This runs from the handshake,
    /// not from a dispatch that contains a throwing callback, so it contains its own.
    void
    consumer_ready_() {
        if (!this->_gap_pending)
            return;
        try {
            deliver_gap_();
        } catch (const std::exception &ex) {
            QB_LOG_WARN("[qbm][redis] cb_consumer on_message error (gap marker): " << ex.what());
        } catch (...) {
            QB_LOG_WARN("[qbm][redis] cb_consumer on_message threw a non-std exception (gap marker)");
        }
    }

    void
    on(qb::redis::error &&error) {
        _on_error(std::move(error));
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
            this->on_disconnected(std::move(on_disconnected)); // the connector's: once per ready connection lost
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
 *
 * A disconnect -- disconnect(), a peer drop -- closes the queue: receive() yields
 * what was already received, then std::nullopt for as long as the consumer stays
 * disconnected, so the loop above ends. After the next connect() (or an
 * auto-reconnect) receive() serves the new connection, starting with anything
 * received before the disconnect and not yet read. Subscriptions are not replayed:
 * subscribe again.
 */
template <typename QB_IO_>
class RedisCoroConsumer : public RedisConsumer<QB_IO_, RedisCoroConsumer<QB_IO_>> {
    friend RedisConsumer<QB_IO_, RedisCoroConsumer<QB_IO_>>;

    /// Default buffered capacity for co_await receive() (tune for burst tolerance).
    static constexpr size_t DEFAULT_MSG_CAPACITY = 8192;

    using message_drop_callback = std::function<void(qb::redis::message &&)>;
    using message_channel       = qb::io::async::channel<qb::redis::message>;

    size_t                           _msg_capacity = DEFAULT_MSG_CAPACITY;
    std::unique_ptr<message_channel> _msg_channel  = std::make_unique<message_channel>(DEFAULT_MSG_CAPACITY);
    message_drop_callback            _on_message_dropped;

    /**
     * @brief Queue the pending gap's marker, with room left for the item behind it (R5, R7).
     * @return Whether it was queued: a full queue keeps the gap pending, and the item is one more loss.
     */
    QB_NOINLINE bool
    queue_gap_() {
        auto &channel = *_msg_channel;
        if (channel.is_closed())
            return false;
        if (channel.capacity() >= 2 && channel.size() + 2 > channel.capacity())
            return false; // no room for the marker AND what follows it: keep coalescing
        if (!channel.try_send(this->gap_marker_()))
            return false;
        this->gap_flushed_();
        return true;
    }

    /// The queue was full: one more message lost, counted in the pending gap (R7).
    QB_NOINLINE void
    drop_message_(qb::redis::message &&msg) {
        this->mark_gap_(qb::io::gap_reason::overflow, 1);
        ++this->stats_mut_().messages_dropped;
        if (_on_message_dropped) {
            try {
                _on_message_dropped(std::move(msg));
            } catch (const std::exception &ex) {
                QB_LOG_WARN("[qbm][redis] coro consumer on_message_dropped error: " << ex.what());
            } catch (...) {
                QB_LOG_WARN("[qbm][redis] coro consumer on_message_dropped threw a non-std exception");
            }
        } else {
            QB_LOG_WARN("[qbm][redis] coro consumer: message dropped (buffer full)");
        }
    }

    void
    enqueue_pubsub_message(qb::redis::message &&msg) {
        if (this->_gap_pending) [[unlikely]] {
            if (!queue_gap_()) {
                drop_message_(std::move(msg));
                return;
            }
        }
        if (_msg_channel->try_send(std::move(msg))) [[likely]]
            return;
        drop_message_(std::move(msg));
    }

    /// Ready: the pending gap's marker joins the stream even when no message follows yet.
    void
    consumer_ready_() {
        if (this->_gap_pending)
            (void) queue_gap_();
    }

    /// Down: the stream ends -- receive() delivers what was queued, then std::nullopt (R6).
    void
    consumer_down_() {
        _msg_channel->close();
    }

    /**
     * @brief A connect() leaves idle or down: the stream of the new lifecycle.
     * @details A channel closes for good, and the consumer outlives its lifecycles: the queue `down` closed is
     *          replaced, carrying over in order what was received and not yet read. A receiver the close woke
     *          and that resumes after the swap sees the old channel gone and yields std::nullopt (the channel's
     *          own liveness guard); what it would have drained is in the new queue.
     */
    void
    consumer_lifecycle_start_() {
        if (!_msg_channel->is_closed())
            return;
        auto fresh = std::make_unique<message_channel>(_msg_capacity);
        while (auto unread = _msg_channel->try_recv())
            (void) fresh->try_send(std::move(*unread)); // never full: same capacity, at most as many
        _msg_channel = std::move(fresh);
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

public:
    RedisCoroConsumer() = default;

    explicit RedisCoroConsumer(qb::io::uri uri, size_t message_channel_capacity = DEFAULT_MSG_CAPACITY)
        : RedisConsumer<QB_IO_, RedisCoroConsumer<QB_IO_>>(std::move(uri))
        , _msg_capacity(message_channel_capacity)
        , _msg_channel(std::make_unique<message_channel>(message_channel_capacity)) {}

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
        return _msg_capacity;
    }

    /**
     * @brief Receive the next pub/sub message (coroutine awaitable).
     * @return std::optional<message> - has value when a message arrived, or
     *         a gap marker (`is_gap()`: a reconnection or a full queue left a
     *         hole there); nullopt only once the consumer is DOWN and what it
     *         queued is read -- `down_reason()` says why. A loss an armed run
     *         recovers from does not end it (Huly QB-126b).
     *
     * Direct coroutine member (NOT an immediately-invoked lambda `[this]{...}()`):
     * the lambda closure would be a temporary destroyed at the end of this call,
     * leaving the coroutine frame referencing freed memory for `this`
     * (dangling-closure UAF — ASan-blind stack corruption that crashes under some
     * compilers' frame layouts). The consumer object owns the coroutine's `this`.
     */
    [[nodiscard]] qb::io::async::task<std::optional<qb::redis::message>>
    receive() {
        co_return co_await _msg_channel->recv();
    }

    ~RedisCoroConsumer() {
        _msg_channel->close();
    }
};

// ============================================================================
// Handshake write handle
// ============================================================================

/**
 * @class handshake_writer
 * @brief What every `HandshakeClient` shares: the write path of one handshake, and nothing past it.
 * @details A command written while the handshake it was made for runs on its link goes straight to the wire,
 *          ahead of everything the client holds; any other time -- before, after, through a copy that
 *          outlived the hook -- it fails with "handshake over" and writes nothing. Copyable: a copy is the
 *          same handle.
 * @tparam Owner the client or consumer class whose connector made it
 * @tparam Self  the HandshakeClient specialization (CRTP, for the command mixins)
 */
template <typename Owner, typename Self>
class handshake_writer {
protected:
    Owner                *_owner;
    std::shared_ptr<bool> _alive;
    std::uint64_t         _id;

    constexpr Self &
    self() {
        return static_cast<Self &>(*this);
    }

public:
    handshake_writer(Owner &owner, std::shared_ptr<bool> alive, std::uint64_t id) noexcept
        : _owner(&owner)
        , _alive(std::move(alive))
        , _id(id) {}

    /** @brief Whether a command written now reaches the handshake's link. */
    [[nodiscard]] bool
    live() const noexcept {
        return *_alive && _owner->handshake_live_(_id);
    }

    /** @brief Send a raw command on the handshake's link (callback API); "handshake over" once it ended. */
    template <typename Ret, typename Func, typename... Args>
    requires std::invocable<Func, Reply<Ret> &&>
    Self &
    command(Func &&func, std::string const &name, Args &&...args) {
        if (!live()) {
            TReply<Func, Ret>{std::forward<Func>(func)}.fail("handshake over");
            return self();
        }
        _owner->template direct_command_<Ret>(std::forward<Func>(func), name, std::forward<Args>(args)...);
        return self();
    }

    /** @brief Send a raw command on the handshake's link and await its reply (co_await API). */
    template <typename Ret, typename... Args>
    auto
    command(std::string const &name, Args &&...args) {
        return make_redis_awaiter<Ret>([this, name, args_tuple = std::make_tuple(std::forward<Args>(args)...)](auto &&callback) mutable {
            std::apply([this, &callback,
                        &name](auto &&...a) { this->template command<Ret>(std::move(callback), name, std::forward<decltype(a)>(a)...); },
                       std::move(args_tuple));
        });
    }

    template <typename T, typename Func>
    [[nodiscard]] auto
    make_coro_command(Func &&operation) {
        return make_redis_awaiter<T>(std::forward<Func>(operation));
    }
};

/**
 * @brief The plain client's handshake handle: its whole command surface, on the handshake's link.
 */
template <typename QB_IO_>
class HandshakeClient<Redis<QB_IO_>>
    : public handshake_writer<Redis<QB_IO_>, HandshakeClient<Redis<QB_IO_>>>
    , public connection_commands<HandshakeClient<Redis<QB_IO_>>>
    , public server_commands<HandshakeClient<Redis<QB_IO_>>>
    , public key_commands<HandshakeClient<Redis<QB_IO_>>>
    , public string_commands<HandshakeClient<Redis<QB_IO_>>>
    , public list_commands<HandshakeClient<Redis<QB_IO_>>>
    , public hash_commands<HandshakeClient<Redis<QB_IO_>>>
    , public set_commands<HandshakeClient<Redis<QB_IO_>>>
    , public sorted_set_commands<HandshakeClient<Redis<QB_IO_>>>
    , public hyperloglog_commands<HandshakeClient<Redis<QB_IO_>>>
    , public geo_commands<HandshakeClient<Redis<QB_IO_>>>
    , public scripting_commands<HandshakeClient<Redis<QB_IO_>>>
    , public publish_commands<HandshakeClient<Redis<QB_IO_>>>
    , public stream_commands<HandshakeClient<Redis<QB_IO_>>>
    , public bitmap_commands<HandshakeClient<Redis<QB_IO_>>>
    , public transaction_commands<HandshakeClient<Redis<QB_IO_>>>
    , public cluster_commands<HandshakeClient<Redis<QB_IO_>>>
    , public acl_commands<HandshakeClient<Redis<QB_IO_>>>
    , public module_commands<HandshakeClient<Redis<QB_IO_>>>
    , public function_commands<HandshakeClient<Redis<QB_IO_>>> {
    using writer = handshake_writer<Redis<QB_IO_>, HandshakeClient<Redis<QB_IO_>>>;

public:
    using writer::command;
    using writer::writer;
    using publish_commands<HandshakeClient<Redis<QB_IO_>>>::publish;
    using server_commands<HandshakeClient<Redis<QB_IO_>>>::command;
};

/**
 * @brief A consumer's handshake handle: the connection and subscription commands, on the handshake's link.
 */
template <typename QB_IO_, typename Derived>
class HandshakeClient<RedisConsumer<QB_IO_, Derived>>
    : public handshake_writer<RedisConsumer<QB_IO_, Derived>, HandshakeClient<RedisConsumer<QB_IO_, Derived>>>
    , public connection_commands<HandshakeClient<RedisConsumer<QB_IO_, Derived>>>
    , public subscription_commands<HandshakeClient<RedisConsumer<QB_IO_, Derived>>> {
    using writer = handshake_writer<RedisConsumer<QB_IO_, Derived>, HandshakeClient<RedisConsumer<QB_IO_, Derived>>>;
    friend class subscription_commands<HandshakeClient<RedisConsumer<QB_IO_, Derived>>>;

    /// subscription_commands' entry point: a (P)(UN)SUBSCRIBE on the handshake's link.
    template <typename Func>
    requires std::invocable<Func, Reply<qb::redis::subscription> &&>
    HandshakeClient &
    pubsub_command(Func &&func, const char *cmd, bool is_unsub, subscription_kind kind, const std::vector<std::string> &names) {
        if (!this->live()) {
            TReply<Func, qb::redis::subscription>{std::forward<Func>(func)}.fail("handshake over");
            return *this;
        }
        this->_owner->direct_pubsub_command_(std::forward<Func>(func), cmd, is_unsub, kind, names);
        return *this;
    }

public:
    using writer::command;
    using writer::writer;
};

// ============================================================================
// Retrying view (Huly QB-133)
// ============================================================================

namespace retry_detail {

/// The arguments of a serialized command, its verb excluded: the RESP array `put_in_pipe` wrote, read back.
[[nodiscard]] inline bool
read_command_frame(std::string_view frame, std::vector<std::string> &parts) {
    auto line = [&frame](std::size_t &pos, char tag) -> std::optional<long long> {
        if (pos >= frame.size() || frame[pos] != tag)
            return std::nullopt;
        const auto end = frame.find("\r\n", pos);
        if (end == std::string_view::npos)
            return std::nullopt;
        long long n = 0;
        for (std::size_t i = pos + 1; i < end; ++i) {
            if (frame[i] < '0' || frame[i] > '9')
                return std::nullopt;
            n = n * 10 + (frame[i] - '0');
        }
        pos = end + 2;
        return n;
    };
    std::size_t pos   = 0;
    const auto  count = line(pos, '*');
    if (!count || *count < 1)
        return false;
    for (long long i = 0; i < *count; ++i) {
        const auto len = line(pos, '$');
        if (!len || pos + static_cast<std::size_t>(*len) + 2 > frame.size())
            return false;
        if (i > 0) // the verb is the view's own `name`
            parts.emplace_back(frame.substr(pos, static_cast<std::size_t>(*len)));
        pos += static_cast<std::size_t>(*len) + 2;
    }
    return pos == frame.size();
}

[[nodiscard]] inline bool
equals_folded(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto ca = (a[i] >= 'a' && a[i] <= 'z') ? static_cast<char>(a[i] - 'a' + 'A') : a[i];
        if (ca != b[i])
            return false;
    }
    return true;
}

/**
 * @brief Whether an idempotent write stays one with these arguments: SET with NX, XX or GET does not (its reply
 *        would depend on the first attempt), nor ZADD with INCR (it accumulates).
 */
[[nodiscard]] inline bool
idempotent_form(std::string_view verb, std::vector<std::string> const &parts) noexcept {
    if (equals_folded(verb, "SET")) {
        for (std::size_t i = 2; i < parts.size(); ++i) // after the key and the value
            if (equals_folded(parts[i], "NX") || equals_folded(parts[i], "XX") || equals_folded(parts[i], "GET"))
                return false;
        return true;
    }
    if (equals_folded(verb, "ZADD")) {
        for (std::size_t i = 1; i < parts.size(); ++i) // a member spelled INCR counts too: the view errs on no retry
            if (equals_folded(parts[i], "INCR"))
                return false;
        return true;
    }
    return true;
}

/// A server error worth another attempt: the first word of LOADING, BUSY, TRYAGAIN or MASTERDOWN.
[[nodiscard]] inline bool
transient_server_error(std::string_view error) noexcept {
    const auto word = error.substr(0, error.find(' '));
    return word == "LOADING" || word == "BUSY" || word == "TRYAGAIN" || word == "MASTERDOWN";
}

} // namespace retry_detail

/**
 * @brief The plain client's retrying view: its command surface, each eligible command retried
 *        (`Redis::retrying()`, `qb::redis::command_retry`).
 * @details The transaction commands are left out: MULTI through a view would leave the client unaware of the
 *          transaction it then runs. A command the view retries is serialized once, when it is submitted, and
 *          every attempt sends the same arguments; an ineligible one passes through to the client unchanged.
 */
template <typename QB_IO_>
class RetryingClient<Redis<QB_IO_>>
    : public connection_commands<RetryingClient<Redis<QB_IO_>>>
    , public server_commands<RetryingClient<Redis<QB_IO_>>>
    , public key_commands<RetryingClient<Redis<QB_IO_>>>
    , public string_commands<RetryingClient<Redis<QB_IO_>>>
    , public list_commands<RetryingClient<Redis<QB_IO_>>>
    , public hash_commands<RetryingClient<Redis<QB_IO_>>>
    , public set_commands<RetryingClient<Redis<QB_IO_>>>
    , public sorted_set_commands<RetryingClient<Redis<QB_IO_>>>
    , public hyperloglog_commands<RetryingClient<Redis<QB_IO_>>>
    , public geo_commands<RetryingClient<Redis<QB_IO_>>>
    , public scripting_commands<RetryingClient<Redis<QB_IO_>>>
    , public publish_commands<RetryingClient<Redis<QB_IO_>>>
    , public stream_commands<RetryingClient<Redis<QB_IO_>>>
    , public bitmap_commands<RetryingClient<Redis<QB_IO_>>>
    , public cluster_commands<RetryingClient<Redis<QB_IO_>>>
    , public acl_commands<RetryingClient<Redis<QB_IO_>>>
    , public module_commands<RetryingClient<Redis<QB_IO_>>>
    , public function_commands<RetryingClient<Redis<QB_IO_>>> {
    using Client = Redis<QB_IO_>;

    Client                  *_client;
    std::shared_ptr<bool>    _alive; ///< the client's liveness token: a pending retry never touches a client gone
    qb::redis::command_retry _retry;

    /// One command being retried: the arguments it was serialized with, the attempts made, its callback.
    template <typename Ret, typename Func>
    struct attempt_state : std::enable_shared_from_this<attempt_state<Ret, Func>> {
        Client                  *client;
        std::shared_ptr<bool>    alive;
        qb::redis::command_retry retry;
        std::string              name;
        std::vector<std::string> parts;
        Func                     callback;
        int                      attempts = 0;

        attempt_state(Client *c, std::shared_ptr<bool> a, qb::redis::command_retry r, std::string n, std::vector<std::string> p, Func f)
            : client(c)
            , alive(std::move(a))
            , retry(std::move(r))
            , name(std::move(n))
            , parts(std::move(p))
            , callback(std::move(f)) {}

        void
        issue() {
            ++attempts;
            auto self = this->shared_from_this();
            client->template command<Ret>([self](Reply<Ret> &&reply) { self->settle(std::move(reply)); }, name, parts);
        }

        /// Whether `reply`'s failure is worth another attempt: no answer while the client can still carry the
        /// command, or a transient server error. A terminal client (down, idle) never is.
        [[nodiscard]] bool
        retryable(Reply<Ret> const &reply) const {
            const auto state = client->state();
            if (state == qb::io::connection_state::down || state == qb::io::connection_state::idle)
                return false;
            if (reply.raw() == nullptr)
                return reply.error() == "disconnected" || reply.error() == "command timed out";
            return retry_detail::transient_server_error(reply.error());
        }

        void
        settle(Reply<Ret> &&reply) {
            if (reply.ok() || !retry.backoff.allows_attempt(attempts) || !retryable(reply)) {
                callback(std::move(reply)); // the last outcome, once
                return;
            }
            static thread_local std::mt19937 rng{std::random_device{}()};
            const qb::duration               wait = retry.backoff.next_delay(attempts, &rng);
            client->count_retry_();
            if (retry.backoff.on_retry) {
                try {
                    retry.backoff.on_retry(attempts, wait);
                } catch (...) {
                    QB_LOG_WARN("[qbm][redis] command_retry on_retry threw");
                }
            }
            auto again = [self = this->shared_from_this()] {
                if (*self->alive) // the client is gone: the retry is dropped, never called
                    self->issue();
            };
            if (wait > qb::duration::zero())
                qb::io::async::callback(std::move(again), wait);
            else
                qb::io::async::defer(std::move(again)); // never inside the failing callback
        }
    };

    /// Whether `name` may be retried, and its arguments then (serialized once); false passes it through.
    template <typename... Args>
    [[nodiscard]] bool
    eligible_(std::string const &name, std::vector<std::string> &parts, Args const &...args) const {
        const auto traits = command_traits_of(name);
        if (traits.blocking || _client->is_in_multi())
            return false;
        if (!traits.read_only && !(traits.idempotent && _retry.idempotent_writes))
            return false;
        qb::allocator::pipe<char> frame;
        try {
            put_in_pipe(frame, name, args...);
        } catch (...) {
            return false; // the client refuses it the same way, and says why
        }
        if (!retry_detail::read_command_frame(std::string_view{frame.begin(), frame.size()}, parts))
            return false;
        return traits.read_only || retry_detail::idempotent_form(name, parts);
    }

public:
    RetryingClient(Client &client, std::shared_ptr<bool> alive, qb::redis::command_retry retry)
        : _client(&client)
        , _alive(std::move(alive))
        , _retry(std::move(retry)) {}

    using publish_commands<RetryingClient<Redis<QB_IO_>>>::publish;
    using server_commands<RetryingClient<Redis<QB_IO_>>>::command;

    /** @brief The policy this view retries with. */
    [[nodiscard]] qb::redis::command_retry const &
    policy() const noexcept {
        return _retry;
    }

    /**
     * @brief Send a raw command through the view (callback API): retried when eligible, else passed through.
     * @details @p func is called once, with the last outcome.
     */
    template <typename Ret, typename Func, typename... Args>
    requires std::invocable<Func, Reply<Ret> &&>
    RetryingClient &
    command(Func &&func, std::string const &name, Args &&...args) {
        assert(*_alive && "a retrying view must not outlive its client");
        if (!*_alive) [[unlikely]] {
            TReply<Func, Ret>{std::forward<Func>(func)}.fail("the client is gone");
            return *this;
        }
        std::vector<std::string> parts;
        if (!eligible_(name, parts, args...)) {
            _client->template command<Ret>(std::forward<Func>(func), name, std::forward<Args>(args)...);
            return *this;
        }
        using State = attempt_state<Ret, std::decay_t<Func>>;
        std::make_shared<State>(_client, _alive, _retry, name, std::move(parts), std::decay_t<Func>(std::forward<Func>(func)))->issue();
        return *this;
    }

    /** @brief Send a raw command through the view and await its last outcome (co_await API). */
    template <typename Ret, typename... Args>
    auto
    command(std::string const &name, Args &&...args) {
        return make_coro_command<Ret>([this, name, args_tuple = std::make_tuple(std::forward<Args>(args)...)](auto &&callback) mutable {
            std::apply(
                [this, &callback, &name](auto &&...a) { this->command<Ret>(std::move(callback), name, std::forward<decltype(a)>(a)...); },
                std::move(args_tuple));
        });
    }

    template <typename T, typename Func>
    [[nodiscard]] auto
    make_coro_command(Func &&operation) {
        return make_redis_awaiter<T>(std::forward<Func>(operation));
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
