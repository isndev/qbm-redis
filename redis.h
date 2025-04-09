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
#include <utility>
#include <qb/io/async.h>
#include <qb/io/async/tcp/connector.h>
// commands trait
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
// !commands

namespace qb::protocol {

/**
 * @class redis
 * @brief Redis protocol implementation for the QB I/O system.
 *
 * Implements the Redis protocol for the QB I/O async system, handling
 * the parsing of Redis replies and message passing.
 *
 * @tparam IO_ The I/O type used for communication
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
     * @brief Container for Redis reply data
     */
    struct message {
        redisReply *reply;
    };

private:
    redisReader *reader_;

public:
    redis() = delete;

    /**
     * @brief Constructs a Redis protocol handler
     * @param io The I/O object to use for communication
     */
    explicit redis(IO_ &io) noexcept
        : qb::io::async::AProtocol<IO_>(io)
        , reader_(redisReaderCreate()) {}

    /**
     * @brief Destructor that cleans up Redis reader resources
     */
    ~redis() {
        redisReaderFree(reader_);
    }

    /**
     * @brief Gets the size of the incoming Redis message
     * @return Size of the message in bytes
     */
    std::size_t
    getMessageSize() noexcept final {
        if (qb__unlikely(redisReaderFeed(reader_, this->_io.in().begin(),
                                         this->_io.in().size()) != REDIS_OK)) {
            this->not_ok();
            return 0;
        }
        return this->_io.in().size();
    }

    /**
     * @brief Processes an incoming Redis message
     * @param size Size of the message (unused)
     */
    void
    onMessage(std::size_t) noexcept final {
        if (!this->ok())
            return;

        message msg;
        while (redisReaderGetReply(reader_, reinterpret_cast<void **>(&msg.reply)) ==
                   REDIS_OK &&
               msg.reply != nullptr) {
            this->_io.on(msg);
        }

        reset();
    }

    /**
     * @brief Resets the protocol state
     */
    void
    reset() noexcept final {}
};

} // namespace qb::protocol

namespace qb::redis {

namespace detail {
using namespace qb::io;

/**
 * @class connector
 * @brief Base class for Redis client connections
 *
 * Provides connection functionality for Redis clients, handling
 * the connection lifecycle and protocol switching.
 *
 * @tparam QB_IO_ The QB I/O type to use
 * @tparam Derived The derived class (CRTP pattern)
 */
template <typename QB_IO_, typename Derived>
class connector
    : public qb::io::async::tcp::client<connector<QB_IO_, Derived>, QB_IO_, void> {
    friend class has_method_on<connector<QB_IO_, Derived>, void,
                               qb::io::async::event::disconnected>;
    friend class qb::io::async::io<connector<QB_IO_, Derived>>;
    friend class qb::protocol::redis<connector<QB_IO_, Derived>>;
    constexpr Derived &
    derived() {
        return static_cast<Derived &>(*this);
    }

public:
    using redis_protocol = qb::protocol::redis<connector<QB_IO_, Derived>>;

private:
    qb::io::uri _uri;

    /**
     * @brief Starts the async communication
     */
    void
    start_async() {
        if (this->protocol())
            this->clear_protocols();

        this->template switch_protocol<redis_protocol>(*this);
        this->start();
    }

    /**
     * @brief Handles Redis protocol messages
     * @param msg The Redis message to handle
     */
    void
    on(typename redis_protocol::message msg) {
        derived().on(msg);
    }

    /**
     * @brief Handles disconnection events
     * @param ev The disconnection event
     */
    void
    on(qb::io::async::event::disconnected &&ev) {
        LOG_WARN("[qbm][redis] has been disconnected");
        derived().on(std::forward<qb::io::async::event::disconnected>(ev));
    }

protected:
    connector() = default;

    /**
     * @brief Constructs a connector with the specified URI
     * @param uri The Redis server URI
     */
    explicit connector(qb::io::uri uri)
        : _uri{std::move(uri)} {}

public:
    /**
     * @brief Connects to the Redis server using the stored URI
     * @return true on success, false on failure
     */
    bool
    connect() {
        if (!this->transport().connect(_uri)) {
            start_async();
            return true;
        }
        return false;
    }

    /**
     * @brief Connects to the Redis server using the specified URI
     * @param uri The Redis server URI
     * @return true on success, false on failure
     */
    bool
    connect(qb::io::uri uri) {
        _uri = std::move(uri);
        return connect();
    }

    /**
     * @brief Connects to the Redis server using the specified URI and I/O
     * @param uri The Redis server URI
     * @param raw_io The transport I/O object
     * @return true on success, false on failure
     */
    bool
    connect(qb::io::uri uri, typename QB_IO_::transport_io_type &&raw_io) {
        if (this->transport().is_open())
            return false;
        _uri              = std::move(uri);
        this->transport() = std::move(raw_io);
        start_async();

        return true;
    }

    /**
     * @brief Asynchronously connects to the Redis server
     * @tparam Func Callback function type
     * @param func Callback function to call on completion
     * @param uri The Redis server URI
     * @param timeout Connection timeout in seconds
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, bool>, void>
    connect(Func &&func, qb::io::uri uri, double timeout = 3) {
        qb::io::async::tcp::connect<typename QB_IO_::transport_io_type>(
            uri,
            [this, uri, func = std::forward<Func>(func)](auto &&raw_io) {
                if (raw_io.is_open()) {
                    func(this->connect(uri, std::forward<decltype(raw_io)>(raw_io)));
                } else
                    func(false);
            },
            timeout);
    }

    /**
     * @brief Asynchronously connects to the Redis server using the stored URI
     * @tparam Func Callback function type
     * @param func Callback function to call on completion
     * @param timeout Connection timeout in seconds
     */
    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, bool>, void>
    connect(Func &&func, double timeout = 3) {
        connect(std::forward<Func>(func), _uri, timeout);
    }

    /**
     * @brief Gets the current Redis server URI
     * @return The current URI
     */
    qb::io::uri const &
    uri() {
        return _uri;
    }
};

/**
 * @class Redis
 * @brief Main Redis client implementation
 *
 * Implements a Redis client with support for all Redis commands.
 * This class inherits from all command trait classes to provide
 * the complete Redis API.
 *
 * @tparam QB_IO_ The QB I/O type to use
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
    , public transaction_commands<Redis<QB_IO_>> {
    friend class connector<QB_IO_, Redis<QB_IO_>>;

public:
    using redis_protocol = typename connector<QB_IO_, Redis<QB_IO_>>::redis_protocol;
    using publish_commands<Redis<QB_IO_>>::publish;

private:
    std::queue<IReply *> _replies;

    /**
     * @brief Internal method to send a command to Redis
     * @tparam Args Command arguments types
     * @param args Command arguments
     */
    template <typename... Args>
    void
    _command(Args &&...args) {
        this->ready_to_write();
        put_in_pipe(this->out(), std::forward<Args>(args)...);
    }

    /**
     * @brief Handles Redis protocol messages
     * @param msg The Redis message to handle
     */
    void
    on(typename redis_protocol::message msg) {
        auto &reply = *_replies.front();
        reply(msg.reply);
        delete &reply;
        _replies.pop();
    }

    /**
     * @brief Handles disconnection events
     * @param Unused disconnection event
     */
    void
    on(qb::io::async::event::disconnected &&) {
        while (!_replies.empty()) {
            on(typename redis_protocol::message{nullptr});
        }
    }

public:
    /**
     * @brief Default constructor
     */
    Redis() = default;

    /**
     * @brief Constructs a Redis client with the specified URI
     * @param uri The Redis server URI
     */
    explicit Redis(qb::io::uri uri)
        : connector<QB_IO_, Redis<QB_IO_>>(std::move(uri)) {}

    /**
     * @brief Sends a command to Redis asynchronously
     *
     * @tparam Ret Return type of the command
     * @tparam Func Callback function type
     * @tparam Args Command argument types
     * @param func Callback function to call with the result
     * @param name Command name
     * @param args Command arguments
     * @return Reference to this Redis client for chaining
     */
    template <typename Ret, typename Func, typename... Args>
    std::enable_if_t<std::is_invocable_v<Func, Reply<Ret> &&>, Redis &>
    command(Func &&func, std::string const &name, Args &&...args) {
        _command(name, std::forward<Args>(args)...);
        _replies.push(new TReply<Func, Ret>(std::forward<Func>(func)));
        return *this;
    }

    /**
     * @brief Sends a command to Redis synchronously
     *
     * @tparam Ret Return type of the command
     * @tparam Args Command argument types
     * @param name Command name
     * @param args Command arguments
     * @return Reply containing the command result
     */
    template <typename Ret, typename... Args>
    Reply<Ret>
    command(std::string const &name, Args &&...args) {
        Reply<Ret> value{};

        auto func = [&value](auto &&reply) { value = std::forward<Reply<Ret>>(reply); };

        command<Ret>(func, name, std::forward<Args>(args)...);
        await();

        if (!value.ok())
            throw std::runtime_error(std::string(value.error()));

        return value;
    }

    /**
     * @brief Waits for the completion of a command
     *
     * This method sends a PING command and waits until a response is received,
     * ensuring that all previous commands have been processed.
     *
     * @return Reference to this Redis client for chaining
     */
    Redis &
    await() {
        //        bool wait = true;
        //        this->ping([&wait](auto &&) {
        //            wait = false;
        //        });
        do {
            qb::io::async::run(EVRUN_NOWAIT);
        } while (!_replies.empty());
        return *this;
    }
};

/**
 * @class RedisConsumer
 * @brief Redis client specialized for subscription operations
 *
 * This class implements a Redis client optimized for Pub/Sub operations,
 * providing support for subscribing to channels and handling incoming
 * messages asynchronously.
 *
 * @tparam QB_IO_ The I/O type to use
 * @tparam Derived The derived class type (CRTP pattern)
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
    using redis_protocol =
        typename connector<QB_IO_, RedisConsumer<QB_IO_, Derived>>::redis_protocol;

private:
    /**
     * @enum MsgType
     * @brief Types of messages that can be received in Pub/Sub mode
     */
    enum class MsgType {
        SUBSCRIBE,
        UNSUBSCRIBE,
        PSUBSCRIBE,
        PUNSUBSCRIBE,
        MESSAGE,
        PMESSAGE,
        UNKNOWN
    };

    /**
     * @brief Determines the type of a Pub/Sub message
     * @param type String representation of the message type
     * @return The corresponding MsgType enum value
     */
    static MsgType
    msg_type(const std::string_view &type) {
        static const qb::unordered_flat_map<std::string_view, MsgType> str_to_enum{
            {"message", MsgType::MESSAGE},
            {"pmessage", MsgType::PMESSAGE},
            {"subscribe", MsgType::SUBSCRIBE},
            {"unsubscribe", MsgType::UNSUBSCRIBE},
            {"psubscribe", MsgType::PSUBSCRIBE},
            {"punsubscribe", MsgType::PUNSUBSCRIBE}};

        auto const it = str_to_enum.find(type);
        return qb::likely(it != std::cend(str_to_enum)) ? it->second : MsgType::UNKNOWN;
    }

    std::queue<IReply *> _replies;

    /**
     * @brief Internal method to send a command to Redis
     * @tparam Args Command argument types
     * @param args Command arguments
     */
    template <typename... Args>
    void
    _command(Args &&...args) {
        this->ready_to_write();
        put_in_pipe(this->out(), std::forward<Args>(args)...);
    }

    /**
     * @brief Sends a command to Redis asynchronously
     *
     * @tparam Ret Return type of the command
     * @tparam Func Callback function type
     * @tparam Args Command argument types
     * @param func Callback function to call with the result
     * @param name Command name
     * @param args Command arguments
     * @return Reference to the derived class for chaining
     */
    template <typename Ret, typename Func, typename... Args>
    std::enable_if_t<std::is_invocable_v<Func, Reply<Ret> &&>, Derived &>
    command(Func &&func, std::string const &name, Args &&...args) {
        _command(name, std::forward<Args>(args)...);
        _replies.push(new TReply<Func, Ret>(std::forward<Func>(func)));
        return derived();
    }

    /**
     * @brief Sends a command to Redis synchronously
     *
     * @tparam Ret Return type of the command
     * @tparam Args Command argument types
     * @param name Command name
     * @param args Command arguments
     * @return Reply containing the command result
     */
    template <typename Ret, typename... Args>
    Reply<Ret>
    command(std::string const &name, Args &&...args) {
        Reply<Ret> value{};

        auto func = [&value](auto &&reply) { value = std::forward<Reply<Ret>>(reply); };

        command<Ret>(func, name, std::forward<Args>(args)...).await();

        return value;
    }

    /**
     * @brief Handles Redis protocol messages
     * @param msg The Redis message to handle
     */
    void
    on(typename redis_protocol::message msg) {
        try {
            auto &raw = *msg.reply;
#ifdef REDIS_PLUS_PLUS_RESP_VERSION_3
            if (!(qb::redis::is_array(*msg.reply) || qb::redis::is_push(*msg.reply)) ||
                msg.reply->elements < 1 || msg.reply->element == nullptr) {
#else
            if (qb::redis::is_array(*msg.reply) && msg.reply->elements > 0 &&
                msg.reply->element) {
#endif
                auto type =
                    msg_type(qb::redis::parse<std::string_view>(*raw.element[0]));
                switch (type) {
                    case MsgType::MESSAGE:
                        derived().on(qb::redis::parse<qb::redis::message>(raw));
                        return;
                    case MsgType::PMESSAGE:
                        derived().on(qb::redis::parse<qb::redis::pmessage>(raw));
                        return;
                    case MsgType::SUBSCRIBE:
                    case MsgType::UNSUBSCRIBE:
                    case MsgType::PSUBSCRIBE:
                    case MsgType::PUNSUBSCRIBE:
                    default:
                        break;
                }
            }
            if (!_replies.empty()) {
                auto &reply = *_replies.front();
                try {
                    reply(&raw);
                } catch (std::exception const &e) {
                    LOG_WARN("[qbm][redis] consumer failed to consume message -> "
                             << e.what());
                }
                delete &reply;
                _replies.pop();
            } else
                throw ProtoError("unknown message type.");
        } catch (std::exception &e) {
            on(qb::redis::error{e.what(), reply_ptr(msg.reply)});
        }
    }

    /**
     * @brief Handles disconnection events
     * @param e The disconnection event
     */
    void
    on(qb::io::async::event::disconnected &&e) {
        LOG_WARN("[qbm][redis] has been disconnected by remote");
        while (!_replies.empty()) {
            on(typename redis_protocol::message{nullptr});
        }
        if constexpr (has_method_on<Derived, void,
                                    qb::io::async::event::disconnected>::value)
            derived().on(std::forward<qb::io::async::event::disconnected>(e));
    }

    /**
     * @brief Default error handler
     * @param error The error that occurred
     */
    void
    on(qb::redis::error &&error) {
        LOG_WARN("[qbm][redis] failed to parse message : " << error.what);
        if constexpr (has_method_on<Derived, void, qb::redis::error>::value)
            derived().on(std::forward<qb::redis::error>(error));
    }

public:
    /**
     * @brief Default constructor
     */
    RedisConsumer() = default;

    /**
     * @brief Constructs a RedisConsumer with the specified URI
     * @param uri The Redis server URI
     */
    explicit RedisConsumer(qb::io::uri uri)
        : connector<QB_IO_, RedisConsumer<QB_IO_, Derived>>(std::move(uri)) {}

    /**
     * @brief Waits for pending operations to complete
     *
     * This method sends a PING command and waits until a response is received,
     * processing any messages that arrive in the meantime.
     *
     * @return Reference to the derived class for chaining
     */
    Derived &
    await() {
        //        bool wait = true;
        //        this->ping([&wait](auto &&) {
        //            wait = false;
        //        });
        //        while (wait)
        //            qb::io::async::run(EVRUN_NOWAIT);

        do {
            qb::io::async::run(EVRUN_NOWAIT);
        } while (!_replies.empty());

        return derived();
    }
};

// todo: lambda deduction instead of std::function in c++20
/**
 * @class RedisCallbackConsumer
 * @brief Redis consumer with callback-based message handling
 *
 * This class extends RedisConsumer to provide a simpler callback-based interface
 * for handling Redis Pub/Sub messages, errors, and disconnection events.
 * Instead of subclassing, users can provide callback functions to handle
 * different event types.
 *
 * @tparam QB_IO_ The I/O type to use
 */
template <typename QB_IO_>
class RedisCallbackConsumer
    : public RedisConsumer<QB_IO_, RedisCallbackConsumer<QB_IO_>> {
    friend class has_method_on<RedisCallbackConsumer<QB_IO_>, void, qb::redis::error>;
    friend class has_method_on<RedisCallbackConsumer<QB_IO_>, void,
                               qb::io::async::event::disconnected>;
    friend RedisConsumer<QB_IO_, RedisCallbackConsumer<QB_IO_>>;
    using cb_msg_t  = std::function<void(qb::redis::message &&)>;
    using cb_err_t  = std::function<void(qb::redis::error &&)>;
    using cb_disc_t = std::function<void(qb::io::async::event::disconnected &&)>;

    cb_msg_t  _on_message;
    cb_err_t  _on_error;
    cb_disc_t _on_disconnected;

    /**
     * @brief Handler for Redis Pub/Sub messages
     * @param msg The received message
     */
    void
    on(qb::redis::message &&msg) {
        _on_message(std::forward<qb::redis::message>(msg));
    }

    /**
     * @brief Handler for Redis error responses
     * @param error The error that occurred
     */
    void
    on(qb::redis::error &&error) {
        _on_error(std::forward<qb::redis::error>(error));
    }

    /**
     * @brief Handler for disconnection events
     * @param ev The disconnection event
     */
    void
    on(qb::io::async::event::disconnected &&ev) {
        _on_disconnected(std::forward<qb::io::async::event::disconnected>(ev));
    }

public:
    /**
     * @brief Constructs a RedisCallbackConsumer with optional callbacks
     *
     * @param uri The Redis server URI
     * @param on_message Callback for handling messages
     * @param on_error Callback for handling errors
     * @param on_disconnected Callback for handling disconnection events
     */
    explicit RedisCallbackConsumer(
        qb::io::uri uri = {}, cb_msg_t &&on_message = [](auto &&) {},
        cb_err_t  &&on_error        = [](auto &&) {},
        cb_disc_t &&on_disconnected = [](auto &&) {})
        : RedisConsumer<QB_IO_, RedisCallbackConsumer<QB_IO_>>(uri)
        , _on_message(std::forward<cb_msg_t>(on_message))
        , _on_error(std::forward<cb_err_t>(on_error))
        , _on_disconnected(std::forward<cb_disc_t>(on_disconnected)) {}

    /**
     * @brief Sets the callback for handling messages
     *
     * @param cb The message callback function
     * @return Reference to this consumer for chaining
     */
    RedisCallbackConsumer &
    on_message(cb_msg_t &&cb) {
        _on_message = std::forward<cb_msg_t>(cb);
        return *this;
    }

    /**
     * @brief Sets the callback for handling errors
     *
     * @param cb The error callback function
     * @return Reference to this consumer for chaining
     */
    RedisCallbackConsumer &
    on_error(cb_err_t &&cb) {
        _on_error = std::forward<cb_err_t>(cb);
        return *this;
    }

    /**
     * @brief Sets the callback for handling disconnection events
     *
     * @param cb The disconnection callback function
     * @return Reference to this consumer for chaining
     */
    RedisCallbackConsumer &
    on_disconnected(cb_disc_t &&cb) {
        _on_disconnected = std::forward<cb_disc_t>(cb);
        return *this;
    }
};

} // namespace detail

/**
 * @brief Alias for Redis database client with custom I/O type
 *
 * @tparam QB_IO_ The I/O type to use for the Redis client
 */
template <typename QB_IO_>
using database = detail::Redis<QB_IO_>;

/**
 * @struct tcp
 * @brief Namespace providing TCP-based Redis client and consumer types
 *
 * This namespace contains typedefs for common Redis client and consumer
 * types that use TCP as the transport layer.
 */
struct tcp {
    /**
     * @brief TCP-based Redis client
     */
    using client = detail::Redis<qb::io::transport::tcp>;

    /**
     * @brief TCP-based Redis consumer template
     *
     * @tparam Derived The derived class type for CRTP
     */
    template <typename Derived>
    using consumer = detail::RedisConsumer<qb::io::transport::tcp, Derived>;

    /**
     * @brief TCP-based Redis callback consumer
     */
    using cb_consumer = detail::RedisCallbackConsumer<qb::io::transport::tcp>;
#ifdef QB_IO_WITH_SSL
    /**
     * @struct ssl
     * @brief Namespace providing SSL/TLS-secured Redis client and consumer types
     *
     * This namespace contains typedefs for Redis client and consumer types
     * that use SSL/TLS-secured TCP as the transport layer.
     */
    struct ssl {
        /**
         * @brief SSL-secured Redis client
         */
        using client = detail::Redis<qb::io::transport::stcp>;

        /**
         * @brief SSL-secured Redis consumer template
         *
         * @tparam Derived The derived class type for CRTP
         */
        template <typename Derived>
        using consumer = detail::RedisConsumer<qb::io::transport::stcp, Derived>;

        /**
         * @brief SSL-secured Redis callback consumer
         */
        using cb_consumer = detail::RedisCallbackConsumer<qb::io::transport::stcp>;
    };
#endif
};

/**
 * @brief No-operation callback function
 *
 * A convenience function that does nothing, useful for optional callback parameters
 * when no action is needed.
 */
const auto no_check = [](auto &&) {};

} // namespace qb::redis

#endif // QBM_REDIS_H
