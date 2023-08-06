/*
 * qb - C++ Actor Framework
 * Copyright (C) 2011-2021 isndev (www.qbaf.io). All rights reserved.
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
#include "hyperlog_commands.h"
#include "geo_commands.h"
#include "scripting_commands.h"
#include "publish_commands.h"
#include "subscription_commands.h"
// !commands

namespace qb::protocol {

template <typename IO_>
class redis final : public qb::io::async::AProtocol<IO_> {
public:
    struct message {
        redisReply *reply;
    };

private:
    redisReader *reader_;

public:
    redis() = delete;
    explicit redis(IO_ &io) noexcept
        : qb::io::async::AProtocol<IO_>(io)
        , reader_(redisReaderCreate()) {}

    ~redis() {
        redisReaderFree(reader_);
    }

    std::size_t
    getMessageSize() noexcept final {
        if (qb__unlikely(redisReaderFeed(reader_, this->_io.in().begin(), this->_io.in().size()) != REDIS_OK)) {
            this->not_ok();
            return 0;
        }
        return this->_io.in().size();
    }

    void
    onMessage(std::size_t) noexcept final {
        if (!this->ok())
            return;

        message msg;
        while (redisReaderGetReply(reader_, reinterpret_cast<void **>(&msg.reply)) == REDIS_OK &&
               msg.reply != nullptr) {
            this->_io.on(msg);
        }

        reset();
    }

    void
    reset() noexcept final {}
};

} // namespace qb::protocol

namespace qb::redis {

namespace detail {
using namespace qb::io;

template <typename QB_IO_, typename Derived>
class connector : public qb::io::async::tcp::client<connector<QB_IO_, Derived>, QB_IO_, void> {
    friend class has_method_on<connector<QB_IO_, Derived>, void, qb::io::async::event::disconnected &&>;
    friend class qb::io::async::io<connector<QB_IO_, Derived>>;
    friend class qb::protocol::redis<connector<QB_IO_, Derived>>;

public:
    using redis_protocol = qb::protocol::redis<connector<QB_IO_, Derived>>;

private:
    qb::io::uri _uri;

    void
    start_async() {
        if (this->protocol())
            this->clear_protocols();

        this->template switch_protocol<redis_protocol>(*this);
        this->start();
    }

    void
    on(typename redis_protocol::message msg) {
        static_cast<Derived &>(*this).on(msg);
    }

    void
    on(qb::io::async::event::disconnected &&ev) {
        LOG_WARN("[qbm][redis] has been disconnected");
        static_cast<Derived &>(*this).on(std::forward<qb::io::async::event::disconnected>(ev));
    }

protected:
    connector() = default;
    explicit connector(qb::io::uri uri)
        : _uri{std::move(uri)} {}

public:
    bool
    connect() {
        if (!this->transport().connect(_uri)) {
            start_async();
            return true;
        }
        return false;
    }

    bool
    connect(qb::io::uri uri) {
        _uri = std::move(uri);
        return connect();
    }

    bool
    connect(qb::io::uri uri, typename QB_IO_::transport_io_type &&raw_io) {
        if (this->transport().is_open())
            return false;
        _uri = std::move(uri);
        this->transport() = std::move(raw_io);
        start_async();

        return true;
    }

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

    template <typename Func>
    std::enable_if_t<std::is_invocable_v<Func, bool>, void>
    connect(Func &&func, double timeout = 3) {
        connect(std::forward<Func>(func), _uri, timeout);
    }

    qb::io::uri const &
    uri() {
        return _uri;
    }
};

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
    , public hyperlog_commands<Redis<QB_IO_>>
    , public geo_commands<Redis<QB_IO_>>
    , public scripting_commands<Redis<QB_IO_>>
    , public publish_commands<Redis<QB_IO_>> {
    friend class connector<QB_IO_, Redis<QB_IO_>>;

public:
    using redis_protocol = typename connector<QB_IO_, Redis<QB_IO_>>::redis_protocol;
    using publish_commands<Redis<QB_IO_>>::publish;

private:
    std::queue<IReply *> _replies;

    template <typename... Args>
    void
    _command(Args &&...args) {
        this->ready_to_write();
        put_in_pipe(this->out(), std::forward<Args>(args)...);
    }

    void
    on(typename redis_protocol::message msg) {
        auto &reply = *_replies.front();
        reply(msg.reply);
        delete &reply;
        _replies.pop();
    }

    void
    on(qb::io::async::event::disconnected &&) {
        while (!_replies.empty()) {
            on(typename redis_protocol::message{nullptr});
        }
    }

public:
    Redis() = default;
    explicit Redis(qb::io::uri uri)
        : connector<QB_IO_, Redis<QB_IO_>>(std::move(uri)) {}

    template <typename Ret, typename Func, typename... Args>
    std::enable_if_t<std::is_invocable_v<Func, Reply<Ret> &&>, Redis &>
    command(Func &&func, std::string const &name, Args &&...args) {
        _command(name, std::forward<Args>(args)...);
        _replies.push(new TReply<Func, Ret>(std::forward<Func>(func)));
        return *this;
    }

    template <typename Ret, typename... Args>
    Reply<Ret>
    command(std::string const &name, Args &&...args) {
        Reply<Ret> value{};

        auto func = [&value](auto &&reply) {
            value = std::forward<Reply<Ret>>(reply);
        };

        command<Ret>(func, name, std::forward<Args>(args)...).await();

        return value;
    }

    Redis &
    await() {
        bool wait = true;
        this->ping([&wait](auto &&reply) {
            wait = false;
        });

        while (wait)
            qb::io::async::run(EVRUN_ONCE);
        return *this;
    }
};

template <typename QB_IO_, typename Derived>
class RedisConsumer
    : public connector<QB_IO_, RedisConsumer<QB_IO_, Derived>>
    , public connection_commands<Derived>
    , public subscription_commands<Derived> {
    friend class connector<QB_IO_, RedisConsumer<QB_IO_, Derived>>;
    friend class connection_commands<Derived>;
    friend class subscription_commands<Derived>;

public:
    using redis_protocol = typename connector<QB_IO_, RedisConsumer<QB_IO_, Derived>>::redis_protocol;

private:
    enum class MsgType { SUBSCRIBE, UNSUBSCRIBE, PSUBSCRIBE, PUNSUBSCRIBE, MESSAGE, PMESSAGE, UNKNOWN };
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

    qb::io::uri _uri;
    std::queue<IReply *> _replies;

    template <typename... Args>
    void
    _command(Args &&...args) {
        this->ready_to_write();
        put_in_pipe(this->out(), std::forward<Args>(args)...);
    }

    template <typename Ret, typename Func, typename... Args>
    std::enable_if_t<std::is_invocable_v<Func, Reply<Ret> &&>, Derived &>
    command(Func &&func, std::string const &name, Args &&...args) {
        _command(name, std::forward<Args>(args)...);
        _replies.push(new TReply<Func, Ret>(std::forward<Func>(func)));
        return static_cast<Derived &>(*this);
    }

    template <typename Ret, typename... Args>
    Reply<Ret>
    command(std::string const &name, Args &&...args) {
        Reply<Ret> value{};

        auto func = [&value](auto &&reply) {
            value = std::forward<Reply<Ret>>(reply);
        };

        command<Ret>(func, name, std::forward<Args>(args)...).await();

        return value;
    }

    void
    on(typename redis_protocol::message msg) {
        try {
            auto &raw = *msg.reply;
#ifdef REDIS_PLUS_PLUS_RESP_VERSION_3
            if (!(reply::is_array(*msg.reply) || reply::is_push(*msg.reply)) || msg.reply->elements < 1 ||
                msg.reply->element == nullptr) {
#else
            if (reply::is_array(*msg.reply) && msg.reply->elements > 0 && msg.reply->element) {
#endif
                auto type = msg_type(reply::parse<std::string_view>(*raw.element[0]));
                switch (type) {
                case MsgType::MESSAGE:
                    static_cast<Derived &>(*this).on(reply::parse<reply::message>(raw));
                    return;
                case MsgType::PMESSAGE:
                    static_cast<Derived &>(*this).on(reply::parse<reply::pmessage>(raw));
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
                    LOG_WARN("[qbm][redis] consumer failed to consume message -> " << e.what());
                }
                delete &reply;
                _replies.pop();
            } else
                throw ProtoError("unknown message type.");
        } catch (std::exception &e) {
            on(reply::error{e.what(), reply_ptr(msg.reply)});
        }
    }

    void
    on(qb::io::async::event::disconnected &&e) {
        LOG_WARN("[qbm][redis] has been disconnected by remote");
        while (!_replies.empty()) {
            on(typename redis_protocol::message{nullptr});
        }
        if constexpr (has_method_on<Derived, void, qb::io::async::event::disconnected &&>::value)
            static_cast<Derived &>(*this).on(std::forward<qb::io::async::event::disconnected>(e));
    }

    // default error if not implemented by Derived class
    void
    on(reply::error &&error) {
        LOG_WARN("[qbm][redis] failed to parse message : " << error.what);
        if constexpr (has_method_on<Derived, void, reply::error &&>::value)
            static_cast<Derived &>(*this).on(std::forward<reply::error>(error));
    }

public:
    RedisConsumer() = default;
    explicit RedisConsumer(qb::io::uri uri)
        : connector<QB_IO_, RedisConsumer<QB_IO_, Derived>>(std::move(uri)) {}

    Derived &
    await() {
        bool wait = true;
        this->ping([&wait](auto &&) {
            wait = false;
        });

        while (wait)
            qb::io::async::run(EVRUN_ONCE);
        return static_cast<Derived &>(*this);
    }
};

// todo: lambda deduction instead of std::function in c++20
template <typename QB_IO_>
class RedisCallbackConsumer : public RedisConsumer<QB_IO_, RedisCallbackConsumer<QB_IO_>> {
    friend class has_method_on<RedisCallbackConsumer<QB_IO_>, void, reply::error &&>;
    friend class has_method_on<RedisCallbackConsumer<QB_IO_>, void, qb::io::async::event::disconnected &&>;
    friend RedisConsumer<QB_IO_, RedisCallbackConsumer<QB_IO_>>;
    using cb_msg_t = std::function<void(reply::message &&)>;
    using cb_err_t = std::function<void(reply::error &&)>;
    using cb_disc_t = std::function<void(qb::io::async::event::disconnected &&)>;

    cb_msg_t _on_message;
    cb_err_t _on_error;
    cb_disc_t _on_disconnected;

    void
    on(reply::message &&msg) {
        _on_message(std::forward<reply::message>(msg));
    }

    void
    on(reply::error &&error) {
        _on_error(std::forward<reply::error>(error));
    }

    void
    on(qb::io::async::event::disconnected &&ev) {
        _on_disconnected(std::forward<qb::io::async::event::disconnected>(ev));
    }

public:
    explicit RedisCallbackConsumer(
        qb::io::uri uri = {}, cb_msg_t &&on_message = [](auto &&) {}, cb_err_t &&on_error = [](auto &&) {},
        cb_disc_t &&on_disconnected = [](auto &&) {})
        : RedisConsumer<QB_IO_, RedisCallbackConsumer<QB_IO_>>(uri)
        , _on_message(std::forward<cb_msg_t>(on_message))
        , _on_error(std::forward<cb_err_t>(on_error))
        , _on_disconnected(std::forward<cb_disc_t>(on_disconnected)) {}

    RedisCallbackConsumer &
    on_message(cb_msg_t &&cb) {
        _on_message = std::forward<cb_msg_t>(cb);
        return *this;
    }

    RedisCallbackConsumer &
    on_error(cb_err_t &&cb) {
        _on_error = std::forward<cb_err_t>(cb);
        return *this;
    }

    RedisCallbackConsumer &
    on_disconnected(cb_disc_t &&cb) {
        _on_disconnected = std::forward<cb_disc_t>(cb);
        return *this;
    }
};

} // namespace detail

template <typename QB_IO_>
using database = detail::Redis<QB_IO_>;

struct tcp {
    using client = detail::Redis<qb::io::transport::tcp>;
    template <typename Derived>
    using consumer = detail::RedisConsumer<qb::io::transport::tcp, Derived>;
    using cb_consumer = detail::RedisCallbackConsumer<qb::io::transport::tcp>;
#ifdef QB_IO_WITH_SSL
    struct ssl {
        using client = detail::Redis<qb::io::transport::stcp>;
        template <typename Derived>
        using consumer = detail::RedisConsumer<qb::io::transport::stcp, Derived>;
        using cb_consumer = detail::RedisCallbackConsumer<qb::io::transport::stcp>;
    };
#endif
};

const auto no_check = [](auto &&) {};

} // namespace qb::redis

#endif // QBM_REDIS_H
