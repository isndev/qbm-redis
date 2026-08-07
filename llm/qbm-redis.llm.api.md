<!-- Verified-against: qbm-redis @ qb 3.0.0. Source of truth: the headers under qbm/redis/src/qbm/redis/. -->
# qbm-redis — Deterministic API Reference

Asynchronous Redis client for the **qb C++ Actor Framework**. Namespace: `qb::redis`.
Headers under `qbm/redis/`. RESP2/RESP3 protocol runs on `qb-io`; this module links transitively against `qb-io` / `qb-core`. C++20 by default (C++23-capable).

Integration: add the submodule, then `qb_load_modules(...)` + link `qbm::redis`.

```cpp
#include <qbm/redis/redis.h>
using namespace qb::redis;
```

Every signature in this file is confirmed against the headers. For prose/concepts see `llm/qbm-redis.llm.md` and `qbm/redis/README.md`. For referenced qb-core/qb-io types (`qb::io::uri`, `qb::io::async::task<T>`, `qb::duration`, `qb::unordered_map`, `qb::json`) see qb's own agent reference `qb.llm.api.md` in the isndev/qb repository — not restated here.

---

## Conventions

- **Two API styles per command.** Every command mixin exposes (1) a **coroutine** form `cmd(args...)` returning a `redis_awaiter` you `co_await` to get `Reply<T>`, and (2) a **callback** form `cmd(Func&& func, args...)` (callback first) that invokes `func(Reply<T>&&)` and returns `Derived&` for chaining. Signatures below show the coroutine form; the callback overload is implied unless noted "callback-only" / "coroutine-only".
- **`Reply<T>`** is the universal result wrapper: `ok()` / `operator bool` = no Redis error; `result()` / `value()` = parsed `T`; `raw()` = owning `parser::Value`; `error()` = owned error string.
- **`awaiter<Reply<T>>`** below is shorthand for the `redis_awaiter<T, ...>` returned by the coroutine form.
- **Empty-argument guard.** A required-variadic / required-collection command called with nothing to act on (`del`/`exists`/`touch`/`unlink` with no keys, `lpush`/`rpush(x)`, `sadd`/`srem`/`smismember`, `sdiff(store)`/`sinter(card/store)`/`sunion(store)`, `hdel`/`hmget`/`hmset`, `zmpop`/`bzmpop`, `lmpop`/`blmpop`, `geoadd`/`geohash`/`geopos`, `pfcount`/`pfmerge`, `cluster addslots`/`delslots`, `script exists`, …) does **not** send a frame: it resolves the callback / awaiter with a **failed `Reply`** (`ok()==false`, reason in `error()`) via `fail_client` (`reply.h:1284-1288`). **No exceptions**: the callback cursor forms `hscan`/`zscan` on an empty key and `lpos` on an empty key/element used to return unfired — parking the coroutine form forever — and now resolve the same way.

### TIME / UNITS — the boundary you must get right

| Surface | Type | Unit |
|---|---|---|
| `connect(...)` timeouts, `RetryPolicy` delays, `set_command_timeout` | **`qb::duration`** | std::chrono canonical |
| `debug_sleep(delay)` | **`qb::duration`** | converted to double-seconds on the wire |
| `EXPIRE` / `EXPIREAT` / `SETEX` / `expiretime` / `ttl` / `OBJECT IDLETIME` | seconds | **native Redis seconds** |
| `PEXPIRE` / `PEXPIREAT` / `PSETEX` / `pexpiretime` / `pttl` / `RESTORE` / `WAIT` / `MIGRATE` / `client_pause` | milliseconds | **native Redis milliseconds** |
| `GETEX` | `long long`→EX **seconds**, `std::chrono::milliseconds`→PX **ms** | asymmetric, see note |
| `BLPOP`/`BRPOP`/`BZPOPMAX`/`BZPOPMIN`/`BLMOVE`/`BLMPOP`/`BZMPOP`/`BRPOPLPUSH` block timeout | seconds | **native Redis seconds** |
| TTL **reply** values (`ttl`, `pttl`, `expiretime`, `pexpiretime`) | `long long` | plain integer, NOT a chrono type |

The Redis command argument units (EXPIRE seconds vs PEXPIRE milliseconds) are a **documented native boundary** — std::chrono-unit overloads (`std::chrono::seconds` for EXPIRE, `std::chrono::milliseconds` for PEXPIRE) exist purely as ergonomic wrappers that forward `.count()`. They are **NOT** `qb::duration`.

**NEVER write** (forbidden as usage anywhere): `qb::Timestamp`, `qb::Duration`, `qb::TimePoint`, `to_timestamp(`, `to_time_point(`.

---

## Client, transport, retry

### `qb::redis::detail::Redis<QB_IO_>` — the client
`redis.h:763` · `class template`. Full client inheriting `connector` + all `*_commands` mixins. Callback + coroutine command APIs, callback pipelining, `await()` drain, opt-in command-deadline watchdog. **Not thread-safe** (single I/O thread/strand).

```cpp
Redis<QB_IO_>();
explicit Redis<QB_IO_>(qb::io::uri uri);
```

| Method | Signature | Purpose / reply | Usage |
|---|---|---|---|
| `command` (callback) | `template<class Ret,class Func,class...A> requires invocable<Func,Reply<Ret>&&> Redis& command(Func&&, std::string const& name, A&&...)` | Low-level callback command; registers handler before sending bytes (pipeline-safe). Returns `*this`. | `c.command<long long>(cb,"INCR","k");` |
| `command` (coro) | `template<class Ret,class...A> auto command(std::string const& name, A&&...)` | co_awaitable; returns `redis_awaiter<Ret>`. | `auto r = co_await c.command<long long>("INCR","k");` |
| `await` | `Redis& await()` | Drain pending replies via non-blocking `run(EVRUN_NOWAIT)` poll. I/O thread only; for the callback API, not `co_await`. | `c.get(cb,"k"); c.await();` |
| `pending_reply_count` | `std::size_t pending_reply_count() const noexcept` | In-flight commands awaiting a reply (pipeline debugging). | `c.pending_reply_count();` |
| `set_command_timeout` | `void set_command_timeout(qb::duration timeout) noexcept` | Opt-in per-connection command deadline (**qb::duration**; `<=0`/zero disables = default). On timeout the connection drops and pending commands fail. A connection-health watchdog, not a per-command timer. | `c.set_command_timeout(5s);` |
| `command_timeout` | `qb::duration command_timeout() const noexcept` | Current deadline (`qb::duration::zero()` = disabled). | `auto t = c.command_timeout();` |
| `make_coro_command` | `template<class T,class Func> auto make_coro_command(Func&&)` | Builds a `redis_awaiter<T>` from a callback op (mixin hook). | internal |

Inherited connector methods: see **connector** below.

### `qb::redis::detail::connector<QB_IO_,Derived>` — connection base
`redis.h:290` · `class template` : `qb::io::async::tcp::client<...>`. Owns uri, `RetryPolicy`, connection-state flags, TLS verify-peer flag, liveness token. Handles connect, auto-reconnect, protocol switching.

| Method | Signature | Purpose / reply | Usage |
|---|---|---|---|
| `connect` (coro) | `connect_awaiter connect(); connect_awaiter connect(qb::io::uri); connect_awaiter connect(qb::duration timeout); connect_awaiter connect(qb::io::uri, qb::duration timeout)` | co_awaitable connect; `await_resume()` returns `bool`. **timeout is qb::duration** (default 3s). | `bool ok = co_await c.connect(uri, 3s);` |
| `connect` (callback) | `template<std::invocable<bool> Func> void connect(Func&&, qb::io::uri, qb::duration timeout=3s); void connect(Func&&, qb::duration timeout=3s)` | Callback connect; invokes `func(bool connected)`. **qb::duration** (default 3s). | `c.connect([](bool ok){...}, uri);` |
| `connect_with_retry` | `qb::io::async::task<bool> connect_with_retry(RetryPolicy policy={}); qb::io::async::task<bool> connect_with_retry(qb::io::uri, RetryPolicy policy={})` | Coroutine retrying connect per `RetryPolicy` (exp. backoff + jitter); re-checks liveness after each suspension. | `bool ok = co_await c.connect_with_retry(pol);` |
| `set_uri` | `void set_uri(qb::io::uri) noexcept` | Set endpoint URI for subsequent connects. | `c.set_uri(uri);` |
| `uri` | `qb::io::uri const& uri() const noexcept` | Current endpoint URI. | `auto& u = c.uri();` |
| `set_verify_peer` / `verify_peer` | `void set_verify_peer(bool) noexcept; bool verify_peer() const noexcept` | TLS server cert + hostname verification for `rediss://` (stcp). Default true; set before `connect()`. | `c.set_verify_peer(false);` |
| `set_ssl_root_cert` | `void set_ssl_root_cert(std::string ca_file_or_dir)` | `rediss://`: trust a private CA (PEM file/dir) IN ADDITION to the system store, so `verify_peer(true)` validates an internal cert. Set before `connect()`. | `c.set_ssl_root_cert("ca.pem");` |
| `set_ssl_client_certificate` | `void set_ssl_client_certificate(std::string cert, std::string key)` | `rediss://`: present a client certificate + key (PEM) for mutual TLS (both required). Set before `connect()`. | `c.set_ssl_client_certificate("cl.pem","cl.key");` |
| `setup_connection` | `bool setup_connection(qb::io::uri, typename QB_IO_::transport_io_type&&)` | Adopt a freshly-opened raw transport, switch to redis protocol, start I/O. False if already connected. | advanced |
| `enable_auto_reconnect` / `disable_auto_reconnect` | `void enable_auto_reconnect(RetryPolicy policy={}) noexcept; void disable_auto_reconnect() noexcept` | Arm/disarm auto-reconnect (detached coroutine runs `connect_with_retry`). | `c.enable_auto_reconnect(pol);` |
| `is_reconnecting` / `is_connected` | `bool is_reconnecting() const noexcept; bool is_connected() const noexcept` | Connection-state queries. | `if (c.is_connected())...` |
| `disconnect` | `void disconnect() noexcept` | Clear connected flag; defer transport teardown to the io watcher (deferred-dispose). | `c.disconnect();` |

`connect_awaiter` (`redis.h:410-413`): coroutine awaiter for async connect; default timeout **3s (qb::duration)**; `await_resume()` → `bool`.

### `qb::redis::RetryPolicy`
`redis.h:217` · `struct`. Exponential-backoff reconnection config. **All time fields are `qb::duration`.**

```cpp
struct RetryPolicy {
  int           max_attempts   = -1;        // -1 = unlimited
  qb::duration  initial_delay  = 100ms;
  qb::duration  max_delay      = 30s;
  double        multiplier     = 2.0;
  bool          jitter         = true;
  qb::duration  connect_timeout= 3s;
  std::function<void(int attempt, qb::duration next_delay)> on_retry;
};
```
Fluent setters (each returns `RetryPolicy&`): `with_max_attempts(int)`, `with_initial_delay(qb::duration)`, `with_max_delay(qb::duration)`, `with_multiplier(double)`, `with_jitter(bool)`, `with_connect_timeout(qb::duration)`, `with_on_retry(std::function<void(int,qb::duration)>)`.

```cpp
auto pol = RetryPolicy{}.with_max_attempts(5).with_initial_delay(200ms).with_max_delay(10s);
```

### Transport aliases — `qb::redis::tcp`
`redis.h:1694` · `struct`. Plaintext-TCP aliases the build always provides:

```cpp
qb::redis::tcp::client       // detail::Redis<qb::io::transport::tcp>
qb::redis::tcp::pipeline     // detail::RedisPipeline<...>
qb::redis::tcp::cb_consumer  // detail::RedisCallbackConsumer<...>
qb::redis::tcp::co_consumer  // detail::RedisCoroConsumer<...>
template<class D> tcp::consumer = detail::RedisConsumer<..., D>;
```
`qb::redis::tcp::ssl::{client,pipeline,cb_consumer,co_consumer}` (`redis.h:1706`) — TLS variants, compiled **ONLY** when `QB_HAS_SSL` is defined (OpenSSL found). The whole `ssl` struct is `#ifdef`-gated; on a TCP-only build these names do not exist.

`qb::redis::database<QB_IO_>` (`redis.h:1688`) · `alias template` = `detail::Redis<QB_IO_>`. Default-transport client alias.
`qb::redis::no_check` (`redis.h:1719`) · `inline constexpr auto no_check = [](auto&&){}` — no-op reply callback for fire-and-forget commands.

### Pipelining & consumers
- `qb::redis::detail::RedisPipeline<QB_IO_>` (`redis.h:1119`) — callback-pipelining wrapper around a `Redis&`: chains `command<Ret>(cb,name,args...)` and `flush()` (== `client().await()`). `flush()` is **unrelated** to FLUSHDB/FLUSHALL.
- `qb::redis::detail::RedisConsumer<QB_IO_,Derived>` (`redis.h:1175`) — pub/sub consumer base (CRTP); tracks `(P)SUBSCRIBE/(P)UNSUBSCRIBE` confirmation counts; routes message/pmessage out-of-band; `await()` / `pending_reply_count()` like `Redis`.
- `qb::redis::detail::RedisCallbackConsumer<QB_IO_>` (`redis.h:1494`) — set `on_message()` / `on_error()` / `on_disconnected()` (each returns `*this`) before subscribing. Ctor: `uri` + optional callbacks.
- `qb::redis::detail::RedisCoroConsumer<QB_IO_>` (`redis.h:1591`) — coroutine consumer; internal `qb::io::async::channel` buffers `DEFAULT_MSG_CAPACITY=8192`; `on_message_dropped()` reports overflow; `message_channel_capacity()` reports capacity.
  - `receive()` (`redis.h:1671`): `auto receive() -> qb::io::async::task<std::optional<qb::redis::message>>` — pull next message; suspends until one arrives, `nullopt` on channel close.

```cpp
auto m = co_await consumer.receive(); if (!m) co_return;  // channel closed
```

---

## Connection commands — `connection_commands<Derived>`
`connection_commands.h:33`. HELLO/AUTH/ECHO/PING/QUIT/SELECT/SWAPDB/RESET.

| Command | Signature (coro) | Purpose / reply | Usage |
|---|---|---|---|
| `hello` | `auto hello(int version=3)` | Switch RESP2/RESP3. Reply `qb::json` (server info map in RESP3). First command after connect to enable RESP3. | `co_await c.hello(3);` |
| `auth` | `auto auth(const std::string& password); auto auth(const std::string& user, const std::string& password)` | AUTH password or user+password. Reply `status`. | `co_await c.auth("pass");` |
| `echo` | `auto echo(const std::string& message)` | ECHO. Reply `std::string`. | `co_await c.echo("hi");` |
| `ping` | `auto ping(); auto ping(const std::string& message)` | PING (optional message). Reply `std::string`. | `co_await c.ping();` |
| `quit` | `auto quit()` | QUIT. Reply `status`. | `co_await c.quit();` |
| `select` | `auto select(long long index)` | SELECT logical DB index. Reply `status`. | `co_await c.select(1);` |
| `swapdb` | `auto swapdb(long long index1, long long index2)` | SWAPDB two databases. Reply `status`. | `co_await c.swapdb(0,1);` |
| `reset` | `auto reset()` | RESET connection state. Reply `status`. (Distinct from the protocol-level `redis<IO_>::reset()`.) | `co_await c.reset();` |

---

## String commands — `string_commands<Derived>`
`string_commands.h:49`.

| Command | Signature (coro) | Purpose / reply | Usage |
|---|---|---|---|
| `append` | `auto append(const std::string& key, const std::string& val)` | APPEND; new length. `Reply<long long>` | `co_await c.append("k","x");` |
| `decr` / `decrby` | `auto decr(const std::string& key); auto decrby(const std::string& key, long long decrement)` | DECR/DECRBY; new value. `Reply<long long>` | `co_await c.decrby("k",2);` |
| `get` | `auto get(const std::string& key)` | GET; `nullopt` if absent. `Reply<std::optional<std::string>>` | `auto v = co_await c.get("k");` |
| `getrange` / `substr` | `auto getrange(const std::string& key, long long start, long long end); auto substr(...)` | GETRANGE (inclusive 0-based); `substr` deprecated alias. `Reply<std::string>` | `co_await c.getrange("k",0,-1);` |
| `getset` | `auto getset(const std::string& key, const std::string& val)` | Atomic set + return prior. `Reply<std::optional<std::string>>` | `co_await c.getset("k","v");` |
| `incr` / `incrby` / `incrbyfloat` | `auto incr(...); auto incrby(..., long long); auto incrbyfloat(..., double)` | INCR/INCRBY → `long long`; INCRBYFLOAT → `double`. | `co_await c.incrby("k",5);` |
| `mget` | `auto mget(const std::vector<std::string>& keys)` | MGET; per-key `nullopt` for missing. `Reply<std::vector<std::optional<std::string>>>` | `co_await c.mget({"a","b"});` |
| `mset` / `msetnx` | `auto mset(const std::vector<std::pair<std::string,std::string>>&); auto msetnx(...)` | MSET → `status`; MSETNX → `bool` (all-or-nothing). | `co_await c.mset({{"a","1"}});` |
| `set` | `auto set(key, val, UpdateType=ALWAYS); auto set(key, val, long long ttl, UpdateType=ALWAYS); auto set(key, val, const std::chrono::milliseconds& ttl, UpdateType=ALWAYS)` | SET; `UpdateType` ALWAYS/EXIST=XX/NOT_EXIST=NX. **TTL overloads emit PX (MILLISECONDS).** `Reply<status>` | `co_await c.set("k","v");` |
| `setex` | `auto setex(key, long long ttl, val); auto setex(key, std::chrono::seconds const& ttl, val)` | SETEX — TTL in **SECONDS**. `Reply<status>` | `co_await c.setex("k",60,"v");` |
| `psetex` | `auto psetex(key, long long ttl, val); auto psetex(key, std::chrono::milliseconds const& ttl, val)` | PSETEX — TTL in **MILLISECONDS**. `Reply<status>` | `co_await c.psetex("k",60000,"v");` |
| `setnx` | `auto setnx(const std::string& key, const std::string& val)` | Set if absent. `Reply<bool>` | `co_await c.setnx("k","v");` |
| `setrange` | `auto setrange(const std::string& key, long long offset, const std::string& val)` | Overwrite at byte offset (zero-pads). `Reply<long long>` (new length) | `co_await c.setrange("k",5,"x");` |
| `strlen` | `auto strlen(const std::string& key)` | Byte length, 0 if absent. `Reply<long long>` | `co_await c.strlen("k");` |
| `getdel` | `auto getdel(const std::string& key)` | Atomic get-and-delete (6.2+). `Reply<std::optional<std::string>>` | `co_await c.getdel("k");` |
| `getex` | `auto getex(key, long long ttl); auto getex(key, std::chrono::milliseconds const& ttl)` | GETEX (6.2+). **UNIT ASYMMETRY**: `long long`→EX **SECONDS**; `std::chrono::milliseconds`→PX **ms**. `Reply<std::optional<std::string>>` | `co_await c.getex("k",60);` |
| `lcs` | `auto lcs(const std::string& key1, const std::string& key2)` | LCS — longest common subsequence (7.0+); plain form only (no LEN/IDX). `Reply<std::string>` | `co_await c.lcs("a","b");` |

> **EXPIRE-seconds / PEXPIRE-ms in strings:** `setex` = SECONDS, `psetex` = MILLISECONDS; `set(...,ttl)` emits PX (ms); `getex(long long)` emits EX (seconds) but `getex(chrono::milliseconds)` emits PX (ms).

---

## Key commands — `key_commands<Derived>`
`key_commands.h:37`.

| Command | Signature (coro) | Purpose / reply | Usage |
|---|---|---|---|
| `del` | `template<class...Keys> auto del(Keys&&...keys)` | DEL variadic (each key may be a string or container). `Reply<long long>` removed count | `co_await c.del("a","b");` |
| `dump` | `auto dump(const std::string& key)` | Serialized value; `nullopt` if absent. `Reply<std::optional<std::string>>` | `co_await c.dump("k");` |
| `exists` | `template<class...Keys> auto exists(Keys&&...keys)` | Number existing among args. `Reply<long long>` | `co_await c.exists("a","b");` |
| `expire` | `auto expire(key, long long timeout); auto expire(key, const std::chrono::seconds& timeout)` | EXPIRE — **SECONDS**. `Reply<bool>` | `co_await c.expire("k",60);` |
| `expireat` | `auto expireat(key, long long ts); auto expireat(key, const time_point<system_clock,seconds>& tp)` | EXPIREAT — absolute UNIX **SECONDS**. `Reply<bool>` | `co_await c.expireat("k",ts);` |
| `keys` | `auto keys(const std::string& pattern="*")` | KEYS glob (blocking server-side; prefer `scan`). `Reply<std::vector<std::string>>` | `co_await c.keys("user:*");` |
| `move` | `auto move(const std::string& key, long long destination_db)` | MOVE to another logical DB. `Reply<bool>` | `co_await c.move("k",1);` |
| `persist` | `auto persist(const std::string& key)` | Remove TTL. `Reply<bool>` | `co_await c.persist("k");` |
| `pexpire` | `auto pexpire(key, long long timeout); auto pexpire(key, const std::chrono::milliseconds& timeout)` | PEXPIRE — **MILLISECONDS**. `Reply<bool>` | `co_await c.pexpire("k",60000);` |
| `pexpireat` | `auto pexpireat(key, long long ts); auto pexpireat(key, const time_point<system_clock,milliseconds>& tp)` | PEXPIREAT — absolute UNIX **MILLISECONDS**. `Reply<bool>` | `co_await c.pexpireat("k",ts);` |
| `pttl` | `auto pttl(const std::string& key)` | Remaining TTL in **MILLISECONDS** as plain `long long` (not chrono). `Reply<long long>` | `co_await c.pttl("k");` |
| `randomkey` | `auto randomkey()` | Random key name or `nullopt`. `Reply<std::optional<std::string>>` | `co_await c.randomkey();` |
| `rename` | `auto rename(const std::string& key, const std::string& new_key)` | RENAME. `Reply<status>` | `co_await c.rename("a","b");` |
| `renamenx` | `auto renamenx(const std::string& key, const std::string& new_key)` | Rename if new absent. `Reply<bool>` | `co_await c.renamenx("a","b");` |
| `restore` | `auto restore(key, val, long long ttl, bool replace=false)` | RESTORE a DUMP value; **ttl in MILLISECONDS** (0=no expiry). `Reply<status>` | `co_await c.restore("k",blob,0);` |
| `scan` | `auto scan(long long cursor, const std::string& pattern="*", long long count=10)` | One MATCH/COUNT iteration → `scan<>{cursor,items}`. **Callback-only** `scan(Func&&, pattern)` auto-iterates whole keyspace then fires once. `Reply<qb::redis::scan<>>` | `auto s = co_await c.scan(0);` |
| `touch` | `template<class...Keys> auto touch(Keys&&...keys)` | Update last-access; count touched. `Reply<long long>` | `co_await c.touch("a","b");` |
| `ttl` | `auto ttl(const std::string& key)` | Remaining TTL in **SECONDS** as plain `long long`. `Reply<long long>` | `co_await c.ttl("k");` |
| `type` | `auto type(const std::string& key)` | Value type name ("string","list"...). `Reply<std::string>` | `co_await c.type("k");` |
| `unlink` | `template<class...Keys> auto unlink(Keys&&...keys)` | Non-blocking async delete; count removed. `Reply<long long>` | `co_await c.unlink("a");` |
| `wait` | `auto wait(long long num_slaves, long long timeout); auto wait(long long num_slaves, const std::chrono::milliseconds& ttl={0})` | WAIT replica acks; **timeout MILLISECONDS** (0=forever). `Reply<long long>` | `co_await c.wait(1,100);` |
| `copyKey` | `auto copyKey(source, destination, std::optional<long long> db=nullopt, bool replace=false)` | COPY (named to avoid `std::copy` clash). `Reply<bool>` | `co_await c.copyKey("a","b");` |
| `expiretime` | `auto expiretime(const std::string& key)` | Absolute expiry UNIX **SECONDS** as `long long`. `Reply<long long>` | `co_await c.expiretime("k");` |
| `pexpiretime` | `auto pexpiretime(const std::string& key)` | Absolute expiry UNIX **MILLISECONDS** as `long long`. `Reply<long long>` | `co_await c.pexpiretime("k");` |
| `migrate` | `auto migrate(host, int port, key, long long db, long long timeout, bool copy=false, bool replace=false, std::optional<std::string> auth=nullopt)` | MIGRATE a key; **timeout MILLISECONDS**. `Reply<status>` | `co_await c.migrate("h",6379,"k",0,1000);` |
| `objectEncoding` | `auto objectEncoding(const std::string& key)` | OBJECT ENCODING; `nullopt` if absent. `Reply<std::optional<std::string>>` | `co_await c.objectEncoding("k");` |
| `objectFreq` | `auto objectFreq(const std::string& key)` | OBJECT FREQ (LFU policy). `Reply<std::optional<long long>>` | `co_await c.objectFreq("k");` |
| `objectIdletime` | `auto objectIdletime(const std::string& key)` | OBJECT IDLETIME — idle **SECONDS**. `Reply<std::optional<long long>>` | `co_await c.objectIdletime("k");` |
| `objectRefcount` | `auto objectRefcount(const std::string& key)` | OBJECT REFCOUNT. `Reply<std::optional<long long>>` | `co_await c.objectRefcount("k");` |
| `sortKey` / `sortKeyStore` / `sortKeyRo` | `auto sortKey(key, const std::vector<std::string>& options={}); auto sortKeyStore(key, destination, options={}); auto sortKeyRo(key, options={})` | SORT family (named to avoid `std::sort` clash). `sortKey`→`vector<string>`; `sortKeyStore`→`long long` count; `sortKeyRo`→SORT_RO. options carry BY/LIMIT/GET/ASC\|DESC/ALPHA verbatim. | `co_await c.sortKey("mylist");` |
| `waitaof` | `auto waitaof(long long num_local, long long num_replicas, long long timeout)` | WAITAOF — AOF fsync local+replicas; **timeout MILLISECONDS**. `Reply<std::vector<long long>>` (the two fsync counts) | `co_await c.waitaof(1,0,1000);` |

> **EXPIRE-seconds / PEXPIRE-ms in keys:** `expire`/`expireat`/`expiretime` = SECONDS; `pexpire`/`pexpireat`/`pexpiretime`/`restore` ttl/`wait`/`migrate`/`waitaof` = MILLISECONDS. Reply TTLs (`ttl`,`pttl`,`expiretime`,`pexpiretime`) are plain `long long`.

---

## Bitmap commands — `bitmap_commands<Derived>`
`bitmap_commands.h:36`. Operate on strings as bit arrays.

| Command | Signature (coro) | Purpose / reply | Usage |
|---|---|---|---|
| `bitcount` | `auto bitcount(const std::string& key, long long start=0, long long end=-1)` | BITCOUNT over byte range (neg from end). `Reply<long long>` | `co_await c.bitcount("k");` |
| `bitfield` / `bitfieldRo` | `auto bitfield(key, const std::vector<std::string>& operations); auto bitfieldRo(key, operations)` | BITFIELD (rw, raw token ops) / BITFIELD_RO (GET-only). `Reply<std::vector<std::optional<long long>>>` | `co_await c.bitfield("k",{"GET","u8","0"});` |
| `bitop` | `auto bitop(const std::string& operation, const std::string& destkey, const std::vector<std::string>& keys)` | BITOP AND/OR/XOR/NOT (operation as **raw string**, not the `BitOp` enum). `Reply<long long>` (result length bytes) | `co_await c.bitop("AND","d",{"a","b"});` |
| `bitpos` | `auto bitpos(const std::string& key, bool bit, std::optional<long long> start=nullopt, std::optional<long long> end=nullopt)` | First position of bit 0/1; -1 if none. `start`/`end` are byte offsets emitted only when supplied (a lone `end` is never sent) — omitting `end` selects the open-ended clear-bit search into the string's zero-padding. `Reply<long long>` | `co_await c.bitpos("k",true);` |
| `getbit` / `setbit` | `auto getbit(key, long long offset); auto setbit(key, long long offset, bool value)` | GETBIT → bit; SETBIT returns the **original** bit. `Reply<long long>` | `co_await c.setbit("k",7,true);` |

---

## Hash commands — `hash_commands<Derived>`
`hash_commands.h:36`.

| Command | Signature (coro) | Purpose / reply | Usage |
|---|---|---|---|
| `hdel` | `template<class...Fields> auto hdel(const std::string& key, Fields&&...fields)` | Delete fields; count removed. `Reply<long long>` | `co_await c.hdel("h","f");` |
| `hexists` | `auto hexists(const std::string& key, const std::string& field)` | Field existence. `Reply<bool>` | `co_await c.hexists("h","f");` |
| `hget` | `auto hget(const std::string& key, const std::string& field)` | Single field; `nullopt` if absent. `Reply<std::optional<std::string>>` | `co_await c.hget("h","f");` |
| `hgetall` | `auto hgetall(const std::string& key)` | All pairs. `Reply<qb::unordered_map<std::string,std::string>>` | `co_await c.hgetall("h");` |
| `hincrby` / `hincrbyfloat` | `auto hincrby(key, field, long long increment)` (`hincrbyfloat`→`double`) | Increment field; new value. `Reply<long long>` / `Reply<double>` | `co_await c.hincrby("h","f",1);` |
| `hkeys` / `hvals` | `auto hkeys(const std::string& key)` (`hvals` similar) | Field names / values. `Reply<std::vector<std::string>>` | `co_await c.hkeys("h");` |
| `hlen` / `hstrlen` | `auto hlen(const std::string& key)` (`hstrlen(key,field)`) | Field count / one field's byte length. `Reply<long long>` | `co_await c.hlen("h");` |
| `hmget` | `template<class...Fields> auto hmget(const std::string& key, Fields&&...fields)` | Multiple values; per-field `nullopt`, positional. `Reply<std::vector<std::optional<std::string>>>` | `co_await c.hmget("h","a","b");` |
| `hmset` | `template<class...FieldValues> auto hmset(const std::string& key, FieldValues&&...field_values)` | Set multiple pairs (emits HMSET). `Reply<status>` | `co_await c.hmset("h","a","1","b","2");` |
| `hset` | `auto hset(key, field, val); auto hset(key, const std::pair<std::string,std::string>& item)` | Set one field; number of NEW fields. `Reply<long long>` | `co_await c.hset("h","f","v");` |
| `hsetnx` | `auto hsetnx(key, field, val); auto hsetnx(key, const std::pair<...>& item)` | Set field if absent. `Reply<bool>` | `co_await c.hsetnx("h","f","v");` |
| `hscan` | `template<class Out=qb::unordered_map<std::string,std::string>> auto hscan(key, long long cursor, pattern="*", long long count=10)` | Cursor scan, output container `Out`. Callback-only no-cursor overload auto-iterates. `Reply<qb::redis::scan<Out>>` | `co_await c.hscan("h",0);` |
| `hvals` (fan-out) | `template<class Func> Derived& hvals(Func&&, std::vector<std::string> keys)` | **Callback-only**: HVALS across many hashes (concatenated); `ok()` AND-folded. No coroutine form. | `c.hvals(cb,{"h1","h2"});` |

---

## List commands — `list_commands<Derived>`
`list_commands.h:36`.

| Command | Signature (coro) | Purpose / reply | Usage |
|---|---|---|---|
| `llen` | `auto llen(const std::string& key)` | Length. `Reply<long long>` | `co_await c.llen("l");` |
| `lpush` / `lpushx` / `rpush` / `rpushx` | `template<class...Args> auto lpush(const std::string& key, Args&&...args)` | Variadic push head/tail (`x`=only if exists). `Reply<long long>` (new length) | `co_await c.lpush("l","a","b");` |
| `lpop` / `rpop` | `auto lpop(key, long long count); auto lpop(key)` | Count form → `vector<string>`; one-arg → `optional<string>`. | `co_await c.lpop("l");` |
| `blpop` / `brpop` | `auto blpop(const std::vector<std::string>& keys, long long timeout=0); auto blpop(keys, const std::chrono::seconds& timeout)` | Blocking pop; **timeout SECONDS** (0=forever). `Reply<std::optional<std::pair<std::string,std::string>>>` | `co_await c.blpop({"l"},5);` |
| `lindex` | `auto lindex(const std::string& key, long long index)` | Element at index (-1=last). `Reply<std::optional<std::string>>` | `co_await c.lindex("l",0);` |
| `linsert` | `auto linsert(key, InsertPosition position, const std::string& pivot, const std::string& val)` | Insert before/after pivot (`InsertPosition` BEFORE/AFTER). `Reply<long long>` | `co_await c.linsert("l",InsertPosition::BEFORE,"p","v");` |
| `lrange` | `auto lrange(const std::string& key, long long start, long long stop)` | Range by index (inclusive, neg-from-end). `Reply<std::vector<std::string>>` | `co_await c.lrange("l",0,-1);` |
| `lrem` | `auto lrem(const std::string& key, long long count, const std::string& val)` | Remove first \|count\| occurrences. `Reply<long long>` | `co_await c.lrem("l",1,"v");` |
| `lset` / `ltrim` | `auto lset(key, long long index, val)` (`ltrim(key,start,stop)`) | Set element / trim. `Reply<status>` | `co_await c.lset("l",0,"v");` |
| `rpoplpush` | `auto rpoplpush(const std::string& source, const std::string& destination)` | Pop tail of source → head of dest. `Reply<std::optional<std::string>>` | `co_await c.rpoplpush("a","b");` |
| `lmove` / `blmove` | `auto lmove(source, destination, ListPosition wherefrom, ListPosition whereto)` (`blmove` adds `long long` SECONDS timeout) | Atomic move (`ListPosition` LEFT/RIGHT). `Reply<std::optional<std::string>>` | `co_await c.lmove("a","b",ListPosition::LEFT,ListPosition::RIGHT);` |
| `lmpop` / `blmpop` | `auto lmpop(const std::vector<std::string>& keys, ListPosition position, long long count=1)` (`blmpop` inserts `long long` SECONDS timeout before count) | Pop from first non-empty list. `Reply<std::optional<std::pair<std::string,std::vector<std::string>>>>` | `co_await c.lmpop({"a","b"},ListPosition::LEFT);` |
| `brpoplpush` | `auto brpoplpush(source, destination, long long timeout)` | Deprecated (use `blmove`); **timeout SECONDS**. `Reply<std::optional<std::string>>` | `co_await c.brpoplpush("a","b",5);` |
| `lpos` | `auto lpos(key, element, std::optional<long long> rank=nullopt, count=nullopt, maxlen=nullopt)` | Positions of element; empty vector if none. `Reply<std::vector<long long>>` | `co_await c.lpos("l","x");` |

---

## Set commands — `set_commands<Derived>`
`set_commands.h:35`.

| Command | Signature (coro) | Purpose / reply | Usage |
|---|---|---|---|
| `sadd` / `srem` | `template<class...Members> auto sadd(const std::string& key, Members&&...members)` | Add / remove members; count. `Reply<long long>` | `co_await c.sadd("s","a","b");` |
| `scard` | `auto scard(const std::string& key)` | Cardinality. `Reply<long long>` | `co_await c.scard("s");` |
| `sdiff` / `sinter` / `sunion` | `auto sdiff(const std::vector<std::string>& keys)` | Set diff/inter/union. `Reply<std::vector<std::string>>` | `co_await c.sdiff({"a","b"});` |
| `sdiffstore` / `sinterstore` / `sunionstore` | `auto sdiffstore(const std::string& destination, const std::vector<std::string>& keys)` | Store result; result size. `Reply<long long>` | `co_await c.sdiffstore("d",{"a","b"});` |
| `sintercard` | `auto sintercard(const std::vector<std::string>& keys, std::optional<long long> limit=nullopt)` | Intersection cardinality with LIMIT. `Reply<long long>` | `co_await c.sintercard({"a","b"});` |
| `sismember` | `auto sismember(const std::string& key, const std::string& member)` | Membership. `Reply<bool>` | `co_await c.sismember("s","m");` |
| `smismember` | `template<class...Members> auto smismember(const std::string& key, Members&&...members)` | Multi-membership. `Reply<std::vector<bool>>` | `co_await c.smismember("s","a","b");` |
| `smembers` | `auto smembers(const std::string& key)` | All members. `Reply<qb::unordered_set<std::string>>` | `co_await c.smembers("s");` |
| `smove` | `auto smove(const std::string& source, const std::string& destination, const std::string& member)` | Move member; true if moved. `Reply<bool>` | `co_await c.smove("a","b","m");` |
| `spop` / `srandmember` | `auto spop(key); auto spop(key, long long count)` | Pop (removes) / random (keeps); single→`optional`, count→`vector`. | `co_await c.spop("s");` |
| `sscan` | `auto sscan(key, long long cursor, pattern="*", long long count=10)` | Cursor scan → `scan<>`. Callback-only no-cursor overload auto-iterates. `Reply<scan<>>` | `co_await c.sscan("s",0);` |

---

## Sorted set commands — `sorted_set_commands<Derived>`
`sorted_set_commands.h:37`. Members are `qb::redis::score_member` (`{double score, std::string member}`).

| Command | Signature (coro) | Purpose / reply | Usage |
|---|---|---|---|
| `bzpopmax` / `bzpopmin` | `auto bzpopmax(const std::vector<std::string>& keys, long long timeout); auto bzpopmax(keys, const std::chrono::seconds& timeout={0})` | Blocking pop highest/lowest; **timeout SECONDS** (0=forever). `Reply<std::optional<std::tuple<std::string,std::string,double>>>` (key,member,score) | `co_await c.bzpopmax({"z"},5);` |
| `zadd` | `auto zadd(key, const std::vector<score_member>& members, UpdateType type=ALWAYS, bool changed=false)` | Add/update; `UpdateType`→NX/XX; `changed`→CH. `Reply<long long>` | `co_await c.zadd("z",{{1.0,"a"}});` |
| `zcard` | `auto zcard(const std::string& key)` | Member count. `Reply<long long>` | `co_await c.zcard("z");` |
| `zcount` / `zlexcount` | `template<class Interval> auto zcount(const std::string& key, const Interval& interval)` | Count in score / lex interval (`Interval` exposes `lower()`/`upper()`). `Reply<long long>` | `co_await c.zcount("z",score_interval{...});` |
| `zincrby` | `auto zincrby(const std::string& key, double increment, const std::string& member)` | Increment score; new score. `Reply<double>` | `co_await c.zincrby("z",2.0,"a");` |
| `zunionstore` / `zinterstore` | `auto zunionstore(destination, keys, const std::vector<double>& weights={}, Aggregation type=SUM)` | Store union/inter with weights + `Aggregation`. `Reply<long long>` | `co_await c.zunionstore("d",{"a","b"});` |
| `zinter` / `zinterWithScores` / `zdiff` / `zdiffWithScores` | `auto zinter(keys, weights={}, Aggregation=SUM)` | Non-storing; plain → `vector<string>`, WithScores → `vector<score_member>`. | `co_await c.zinter({"a","b"});` |
| `zdiffstore` | `auto zdiffstore(destination, keys)` | Store diff. `Reply<long long>` | `co_await c.zdiffstore("d",{"a","b"});` |
| `zintercard` | `auto zintercard(const std::vector<std::string>& keys, std::optional<long long> limit=nullopt)` | Intersection cardinality with LIMIT. `Reply<long long>` | `co_await c.zintercard({"a","b"});` |
| `zpopmax` / `zpopmin` | `auto zpopmax(const std::string& key, long long count=1)` | Pop highest/lowest. `Reply<std::vector<score_member>>` | `co_await c.zpopmax("z");` |
| `zrange` / `zrevrange` | `auto zrange(const std::string& key, long long start, long long stop)` | Index range (always WITHSCORES). `Reply<std::vector<score_member>>` | `co_await c.zrange("z",0,-1);` |
| `zrangebylex` / `zrevrangebylex` | `template<class Interval> auto zrangebylex(key, Interval const& interval, const LimitOptions& opts={})` | Lex range (no scores); `LimitOptions{offset,count}`. `Reply<std::vector<std::string>>` | `co_await c.zrangebylex("z",lex_interval{...});` |
| `zrangebyscore` / `zrevrangebyscore` | `template<class Interval> auto zrangebyscore(key, Interval const& interval, const LimitOptions& opts={})` | Score range, WITHSCORES. `Reply<std::vector<score_member>>` | `co_await c.zrangebyscore("z",score_interval{...});` |
| `zrangestore` | `auto zrangestore(dst, src, min, max, const std::vector<std::string>& options={})` | Store computed range (min/max/options raw strings). `Reply<long long>` | `co_await c.zrangestore("d","s","0","-1");` |
| `zrank` / `zrevrank` | `auto zrank(const std::string& key, const std::string& member)` | 0-based rank; `nullopt` if absent. `Reply<std::optional<long long>>` | `co_await c.zrank("z","a");` |
| `zrem` | `auto zrem(const std::string& key, const std::vector<std::string>& members)` | Remove members; count. `Reply<long long>` | `co_await c.zrem("z",{"a"});` |
| `zremrangebyrank` / `zremrangebylex` / `zremrangebyscore` | `auto zremrangebyrank(key, long long start, long long stop)` (lex/score take `Interval`) | Remove by range; count removed. `Reply<long long>` | `co_await c.zremrangebyrank("z",0,1);` |
| `zmpop` / `bzmpop` | `auto zmpop(const std::vector<std::string>& keys, const std::string& min_or_max, long long count=1)` (`bzmpop` inserts `long long` SECONDS timeout before min_or_max) | Pop from first non-empty zset (min_or_max raw "MIN"/"MAX"). `Reply<std::optional<std::pair<std::string,std::vector<score_member>>>>` | `co_await c.zmpop({"z"},"MIN");` |
| `zmscore` / `zscore` | `auto zmscore(const std::string& key, const std::vector<std::string>& members)` (`zscore` single) | Scores; per-member `nullopt`. `Reply<std::vector<std::optional<double>>>` / `Reply<std::optional<double>>` | `co_await c.zscore("z","a");` |
| `zrandmember` / `zrandmemberCount` / `zrandmemberWithScores` | `auto zrandmember(key); auto zrandmemberCount(key, long long count); auto zrandmemberWithScores(key, long long count)` | Random member: `optional`, vector, or `vector<score_member>`. (camelCase names.) | `co_await c.zrandmember("z");` |
| `zscan` | `auto zscan(key, long long cursor, pattern="*", long long count=10)` | Cursor scan → member→score map. Callback-only no-cursor overload auto-iterates. `Reply<scan<qb::unordered_map<std::string,double>>>` | `co_await c.zscan("z",0);` |

---

## Stream commands — `stream_commands<Derived>`
`stream_commands.h:35`. Stream block timeouts (`block`/`min_idle_time`) are raw `long long` **milliseconds**.

| Command | Signature (coro) | Purpose / reply | Usage |
|---|---|---|---|
| `xadd` | `auto xadd(key, const std::vector<std::pair<std::string,std::string>>& entries, const std::optional<std::string>& id=nullopt)` | Append entry; wire id defaults to `*` (auto). `Reply<stream_id>` | `co_await c.xadd("s",{{"f","v"}});` |
| `xlen` | `auto xlen(const std::string& key)` | Entry count. `Reply<long long>` | `co_await c.xlen("s");` |
| `xdel` | `template<class...Ids> auto xdel(const std::string& key, Ids&&...ids)` | Delete entries; count. `Reply<long long>` | `co_await c.xdel("s","1-0");` |
| `xgroup_create` | `auto xgroup_create(key, group, id, bool mkstream=false)` | XGROUP CREATE (+MKSTREAM). `Reply<status>` | `co_await c.xgroup_create("s","g","$",true);` |
| `xgroup_destroy` | `auto xgroup_destroy(const std::string& key, const std::string& group)` | XGROUP DESTROY. `Reply<long long>` | `co_await c.xgroup_destroy("s","g");` |
| `xgroup_delconsumer` | `auto xgroup_delconsumer(key, group, consumer)` | DELCONSUMER; pending-deleted count. `Reply<long long>` | `co_await c.xgroup_delconsumer("s","g","c");` |
| `xgroupSetid` | `auto xgroupSetid(key, group, id, std::optional<long long> entries_read=nullopt)` | XGROUP SETID (+ENTRIESREAD). `Reply<status>` (camelCase) | `co_await c.xgroupSetid("s","g","0");` |
| `xgroupCreateconsumer` | `auto xgroupCreateconsumer(key, group, consumer)` | CREATECONSUMER. `Reply<bool>` (camelCase) | `co_await c.xgroupCreateconsumer("s","g","c");` |
| `xack` | `template<class...Ids> auto xack(key, group, Ids&&...ids)` | Acked count. `Reply<long long>` | `co_await c.xack("s","g","1-0");` |
| `xtrim` | `auto xtrim(key, long long maxlen, bool approximate=false)` | XTRIM MAXLEN (`~`/`=`); deleted count. `Reply<long long>` | `co_await c.xtrim("s",1000);` |
| `xread` | `auto xread(key, id, std::optional<long long> count=nullopt, std::optional<long long> block=nullopt); auto xread(const std::vector<std::string>& keys, const std::vector<std::string>& ids, count=nullopt, block=nullopt)` | XREAD; multi-stream throws `std::invalid_argument` if sizes mismatch. **block = ms.** `Reply<qb::json>` | `co_await c.xread("s","0");` |
| `xreadgroup` | `auto xreadgroup(key, group, consumer, id, count=nullopt, block=nullopt)` (+ multi-stream `keys`/`ids` overload) | XREADGROUP. **block = ms.** `Reply<qb::json>` | `co_await c.xreadgroup("s","g","c",">");` |
| `xrange` / `xrevrange` | `auto xrange(key, start, end, std::optional<long long> count=nullopt)` (`xrevrange(key, end, start, ...)`) | Range forward / reverse (note arg order). `Reply<stream_entry_list>` | `co_await c.xrange("s","-","+");` |
| `xpending` | `auto xpending(key, group, start="-", end="+", long long count=10, const std::optional<std::string>& consumer=nullopt)` | XPENDING extended. `Reply<qb::json>` | `co_await c.xpending("s","g");` |
| `xclaim` | `auto xclaim(key, group, consumer, long long min_idle_time, const std::vector<std::string>& ids, const std::vector<std::string>& options={})` | XCLAIM; **min_idle_time = ms**; options verbatim. `Reply<stream_entry_list>` | `co_await c.xclaim("s","g","c",0,{"1-0"});` |
| `xautoclaim` | `auto xautoclaim(key, group, consumer, long long min_idle_time, start, count=nullopt, bool justid=false)` | XAUTOCLAIM; **min_idle_time = ms**; +JUSTID. `Reply<qb::json>` | `co_await c.xautoclaim("s","g","c",0,"0-0");` |
| `xinfo_stream` / `xinfo_groups` / `xinfo_consumers` / `xinfo_help` | `auto xinfo_stream(const std::string& key)` (etc.) | XINFO subcommands. `Reply<qb::json>` | `co_await c.xinfo_stream("s");` |

`parse_stream_id` (`stream_commands.h:85`) · `static stream_id parse_stream_id(const std::string&)` — parses `ts-seq`; returns `{0,0}` on parse error (swallows exception).

---

## Geo commands — `geo_commands<Derived>`
`geo_commands.h:34`. Distance unit is `qb::redis::GeoUnit` (M/KM/MI/FT), default M.

| Command | Signature (coro) | Purpose / reply | Usage |
|---|---|---|---|
| `geoadd` | `template<class...Members> auto geoadd(const std::string& key, Members&&...members)` | Add (lon,lat,name) triplets; count added. `Reply<long long>` | `co_await c.geoadd("g",13.0,52.0,"berlin");` |
| `geodist` | `auto geodist(key, member1, member2, GeoUnit unit=GeoUnit::M)` | Distance; `nullopt` if a member absent. `Reply<std::optional<double>>` | `co_await c.geodist("g","a","b",GeoUnit::KM);` |
| `geohash` | `template<class...Members> auto geohash(const std::string& key, Members&&...members)` | Geohash per member; `nullopt` if absent. `Reply<std::vector<std::optional<std::string>>>` | `co_await c.geohash("g","a");` |
| `geopos` | `template<class...Members> auto geopos(const std::string& key, Members&&...members)` | Coordinates per member. `Reply<std::vector<std::optional<geo_pos>>>` | `co_await c.geopos("g","a");` |
| `georadius` | `auto georadius(key, double longitude, double latitude, double radius, GeoUnit unit=GeoUnit::M, const std::vector<std::string>& options={})` | Radius search by lon/lat; options carry WITHCOORD/WITHDIST/COUNT/SORT raw. `Reply<std::vector<std::string>>` | `co_await c.georadius("g",13.0,52.0,100,GeoUnit::KM);` |
| `georadiusbymember` | `auto georadiusbymember(key, member, double radius, GeoUnit unit=GeoUnit::M, options={})` | Radius search around member. `Reply<std::vector<std::string>>` | `co_await c.georadiusbymember("g","berlin",100,GeoUnit::KM);` |
| `geosearch` | `auto geosearch(key, member, double radius, GeoUnit unit=GeoUnit::M, options={})` | GEOSEARCH; this binding only supports FROMMEMBER + BYRADIUS. `Reply<std::vector<std::string>>` | `co_await c.geosearch("g","berlin",100,GeoUnit::KM);` |

---

## HyperLogLog commands — `hyperloglog_commands<Derived>`
`hyperloglog_commands.h:39`.

| Command | Signature (coro) | Purpose / reply | Usage |
|---|---|---|---|
| `pfadd` | `template<class...Elements> auto pfadd(const std::string& key, Elements&&...elements)` | Add; true if cardinality estimate changed. `Reply<bool>` | `co_await c.pfadd("hll","a","b");` |
| `pfcount` | `template<class...Keys> auto pfcount(Keys&&...keys)` | Estimated cardinality over keys (no leading key param). `Reply<long long>` | `co_await c.pfcount("hll");` |
| `pfmerge` | `template<class...Keys> auto pfmerge(const std::string& destination, Keys&&...keys)` | Merge source keys into destination. `Reply<status>` | `co_await c.pfmerge("d","a","b");` |

---

## Publish/Subscribe

### Publish — `publish_commands<Derived>`
`publish_commands.h:37`.

| Command | Signature (coro) | Purpose / reply | Usage |
|---|---|---|---|
| `publish` | `auto publish(const std::string& channel, const std::string& message)` | PUBLISH; number of subscribers that received it. `Reply<long long>` | `co_await c.publish("ch","msg");` |

### Subscription — `subscription_commands<Derived>`
`subscription_commands.h:41`. Used by the **consumer** classes, not the data client. Every method has coro + callback forms; all yield `Reply<qb::redis::subscription>` (`{std::optional<std::string> channel, long long num}`).

| Command | Signature (coro) | Purpose / reply | Usage |
|---|---|---|---|
| `subscribe` | `auto subscribe(const std::string& channel); auto subscribe(const std::vector<std::string>& channels)` | SUBSCRIBE one/many channels. `Reply<subscription>` | `co_await consumer.subscribe("news");` |
| `unsubscribe` | `auto unsubscribe(const std::string& channel=""); auto unsubscribe(const std::vector<std::string>& channels)` | UNSUBSCRIBE (empty = all). `Reply<subscription>` | `co_await consumer.unsubscribe("news");` |
| `psubscribe` | `auto psubscribe(const std::string& pattern); auto psubscribe(const std::vector<std::string>& patterns)` | PSUBSCRIBE pattern(s). `Reply<subscription>` | `co_await consumer.psubscribe("news.*");` |
| `punsubscribe` | `auto punsubscribe(const std::string& pattern=""); auto punsubscribe(const std::vector<std::string>& patterns)` | PUNSUBSCRIBE (empty = all). `Reply<subscription>` | `co_await consumer.punsubscribe("news.*");` |

---

## Transaction commands — `transaction_commands<Derived>`
`transaction_commands.h:38`. MULTI/EXEC/DISCARD/WATCH/UNWATCH.

| Command | Signature (coro) | Purpose / reply | Usage |
|---|---|---|---|
| `multi` | `auto multi()` | MULTI — begin transaction. `Reply<status>` | `co_await c.multi();` |
| `exec` | `template<class Result> auto exec()` | EXEC — run queued commands; per-command results decoded as `Result`. `Reply<std::vector<Result>>` | `co_await c.exec<std::string>();` |
| `discard` | `auto discard()` | DISCARD — abort transaction. `Reply<status>` | `co_await c.discard();` |
| `watch` | `auto watch(const std::string& key); auto watch(const std::vector<std::string>& keys)` | WATCH key(s) for optimistic locking. `Reply<status>` | `co_await c.watch("k");` |
| `unwatch` | `auto unwatch()` | UNWATCH all. `Reply<status>` | `co_await c.unwatch();` |

> For raw per-command replies of a pipelined MULTI/EXEC, use `Reply<pipeline_result>` + `.raw()` (per-command replies are not stored on `pipeline_result`).

---

## Scripting commands — `scripting_commands<Derived>`
`scripting_commands.h:36`. Lua. `numkeys` is computed automatically from `keys.size()`.

| Command | Signature (coro) | Purpose / reply | Usage |
|---|---|---|---|
| `eval` | `template<class Ret> auto eval(const std::string& script, const std::vector<std::string>& keys={}, const std::vector<std::string>& args={})` | EVAL; caller chooses decoded `Ret`. `Reply<Ret>` | `co_await c.eval<long long>("return 1");` |
| `evalsha` | `template<class Ret> auto evalsha(const std::string& sha1, keys={}, args={})` | EVALSHA by SHA1. `Reply<Ret>` | `co_await c.evalsha<std::string>(sha);` |
| `evalRo` / `evalshaRo` | `template<class Ret> auto evalRo(script, keys={}, args={})` | Read-only variants (script may not write). `Reply<Ret>` | `co_await c.evalRo<long long>("return 1");` |
| `script_load` | `auto script_load(std::string const& script)` | SCRIPT LOAD; returns the SHA1. `Reply<std::string>` | `co_await c.script_load("return 1");` |
| `script_exists` | `template<class...Keys> auto script_exists(Keys&&...keys)` | SCRIPT EXISTS for SHA1 hashes. `Reply<std::vector<bool>>` | `co_await c.script_exists(sha);` |
| `script_flush` | `auto script_flush()` | SCRIPT FLUSH (clear cache). `Reply<status>` | `co_await c.script_flush();` |
| `script_kill` | `auto script_kill()` | SCRIPT KILL. `Reply<status>` | `co_await c.script_kill();` |
| `scriptDebug` | `auto scriptDebug(const std::string& mode)` | SCRIPT DEBUG ("YES"/"SYNC"/"NO"). `Reply<status>` (camelCase) | `co_await c.scriptDebug("NO");` |

---

## Server commands — `server_commands<Derived>`
`server_commands.h:41`.

| Command | Signature (coro) | Purpose / reply | Usage |
|---|---|---|---|
| `debug_sleep` | `auto debug_sleep(qb::duration delay)` | DEBUG SLEEP; **`qb::duration`** → double-seconds on the wire. `Reply<status>` | `co_await c.debug_sleep(500ms);` |
| `debug_object` | `auto debug_object(const std::string& key)` | DEBUG OBJECT. `Reply<std::string>` | `co_await c.debug_object("k");` |
| `debug_segfault` | `auto debug_segfault()` | DEBUG SEGFAULT (crashes server, testing). `Reply<status>` | `co_await c.debug_segfault();` |
| `client_id` | `auto client_id()` | CLIENT ID. `Reply<long long>` | `co_await c.client_id();` |
| `client_getname` / `client_setname` | `auto client_getname(); auto client_setname(const std::string& name)` | CLIENT GETNAME/SETNAME. `Reply<std::optional<std::string>>` / `Reply<status>` | `co_await c.client_setname("w1");` |
| `client_kill` | `auto client_kill(addr="", long long id=0, type="", bool skipme=true)` | CLIENT KILL (default/zero args omitted); always the FILTER form, whose reply is a COUNT. `Reply<long long>` = connections killed | `co_await c.client_kill("",42);` |
| `client_pause` / `client_unpause` | `auto client_pause(long long timeout, const std::string& mode="ALL"); auto client_unpause()` | CLIENT PAUSE; **timeout = raw long long milliseconds (native, NOT qb::duration)**. `Reply<status>` | `co_await c.client_pause(100);` |
| `client_unblock` | `auto client_unblock(long long client_id, bool error=false)` | CLIENT UNBLOCK (+ERROR). `Reply<status>` | `co_await c.client_unblock(42);` |
| `client_tracking` / `client_tracking_info` / `client_caching` / `client_getredir` / `client_no_evict` / `client_no_touch` / `client_reply` / `client_setinfo` / `client_info` / `client_list` | `auto client_tracking(bool enabled=true)` (etc.) | CLIENT subcommands; mostly `Reply<status>`. `client_info`→`std::string`, `client_list`/`client_tracking_info`→`qb::json`, `client_getredir`→`long long`. | `co_await c.client_list();` |
| `config_get` | `auto config_get(const std::string& parameter)` | CONFIG GET. `Reply<std::vector<std::pair<std::string,std::string>>>` | `co_await c.config_get("maxmemory");` |
| `config_set` / `config_resetstat` / `config_rewrite` | `auto config_set(parameter, value)` (etc.) | CONFIG SET/RESETSTAT/REWRITE. `Reply<status>` | `co_await c.config_set("maxmemory","0");` |
| `command_info` | `auto command_info(const std::vector<std::string>& command_names={})` | COMMAND INFO. `Reply<std::vector<std::map<std::string,std::string>>>` | `co_await c.command_info({"get"});` |
| `command_count` | `auto command_count()` | COMMAND COUNT. `Reply<long long>` | `co_await c.command_count();` |
| `command_getkeys` / `command_getkeysandflags` | `auto command_getkeys(command, const std::vector<std::string>& args)` | COMMAND GETKEYS / GETKEYSANDFLAGS. `Reply<std::vector<std::string>>` / `Reply<qb::json>` | `co_await c.command_getkeys("set",{"k","v"});` |
| `command` / `command_stats` / `command_docs` / `command_list` | `auto command(); auto command(const std::vector<std::string>& names)` | COMMAND family. `Reply<qb::json>`; `command_list`→`std::vector<std::string>`. | `co_await c.command();` |
| `info` | `auto info(const std::string& section="")` | INFO [section] parsed to JSON. `Reply<qb::json>` | `co_await c.info("memory");` |
| `dbsize` | `auto dbsize()` | DBSIZE. `Reply<long long>` | `co_await c.dbsize();` |
| `flushall` / `flushdb` | `auto flushall(bool async=false); auto flushdb(bool async=false)` | FLUSHALL/FLUSHDB [ASYNC]. `Reply<status>` | `co_await c.flushdb();` |
| `save` / `bgsave` / `bgrewriteaof` / `lastsave` | `auto save(); auto bgsave(bool schedule=false); auto bgrewriteaof(); auto lastsave()` | Persistence. `Reply<status>`; `lastsave`→`long long` (Unix ts as plain int). | `co_await c.bgsave();` |
| `time` | `auto time()` | TIME; parses via `from_chars` → never throws out of the libev callback. `Reply<std::pair<long long,long long>>` (Unix seconds, microseconds). Callback overload yields raw `Reply<std::vector<std::string>>`. | `auto t = co_await c.time();` |
| `memory_usage` | `auto memory_usage(const std::string& key, long long samples=0)` | MEMORY USAGE [SAMPLES n]; bytes. `Reply<long long>` | `co_await c.memory_usage("k");` |
| `memory_doctor` / `memory_help` / `memory_malloc_stats` / `memory_purge` / `memory_stats` | `auto memory_doctor()` (etc.) | MEMORY subcommands. `std::string` / `std::vector<std::string>` / `status` / `qb::json`. | `co_await c.memory_stats();` |
| `slowlog_get` / `slowlog_len` / `slowlog_reset` | `auto slowlog_get(long long count=10); auto slowlog_len(); auto slowlog_reset()` | SLOWLOG family. `qb::json` / `long long` / `status`. | `co_await c.slowlog_get();` |
| `latency_latest` / `latency_history` / `latency_reset` / `latency_doctor` / `latency_graph` / `latency_histogram` | `auto latency_latest()` (etc.) | LATENCY family. `qb::json` / `long long` (reset) / `std::string` (doctor,graph). | `co_await c.latency_latest();` |
| `role` | `auto role()` | ROLE. `Reply<qb::json>` | `co_await c.role();` |
| `shutdown` | `auto shutdown(const std::string& save_option="")` | SHUTDOWN [SAVE\|NOSAVE]. `Reply<status>` | `co_await c.shutdown("NOSAVE");` |
| `slaveof` | `auto slaveof(const std::string& host, long long port)` | SLAVEOF host port. `Reply<status>` | `co_await c.slaveof("h",6379);` |
| `failover` | `auto failover(const std::vector<std::string>& options={})` | FAILOVER. `Reply<status>` | `co_await c.failover();` |
| `sync` / `psync` | `auto sync(); auto psync(const std::string& replication_id, long long offset)` | Replication (raw). `Reply<status>` | `co_await c.sync();` |
| `monitor` | `template<class Func> Derived& monitor(Func&&)` | **Callback-only** streaming; `func(Reply<std::string>)` per monitored command. | `c.monitor(cb);` |

---

## Cluster commands — `cluster_commands<Derived>`
`cluster_commands.h:34`.

| Command | Signature (coro) | Purpose / reply | Usage |
|---|---|---|---|
| `cluster_info` / `cluster_nodes` / `cluster_slots` / `cluster_shards` / `cluster_links` / `cluster_replicas` / `cluster_slaves` | `auto cluster_info()` (etc.) | Topology introspection. `Reply<qb::json>`. (`cluster_slaves` deprecated; prefer `cluster_replicas`.) | `co_await c.cluster_info();` |
| `cluster_myid` / `cluster_myshardid` | `auto cluster_myid(); auto cluster_myshardid()` | This node's id / shard id. `Reply<std::string>` | `co_await c.cluster_myid();` |
| `cluster_meet` | `auto cluster_meet(const std::string& ip, int port)` | CLUSTER MEET. `Reply<status>` | `co_await c.cluster_meet("10.0.0.2",6379);` |
| `cluster_forget` / `cluster_replicate` | `auto cluster_forget(node_id); auto cluster_replicate(node_id)` | Remove / replicate node. `Reply<status>` | `co_await c.cluster_replicate(id);` |
| `cluster_reset` | `auto cluster_reset(const std::string& mode="SOFT")` | CLUSTER RESET HARD\|SOFT. `Reply<status>` | `co_await c.cluster_reset("HARD");` |
| `cluster_failover` | `auto cluster_failover(const std::string& option="")` | CLUSTER FAILOVER [FORCE\|TAKEOVER]. `Reply<status>` | `co_await c.cluster_failover("FORCE");` |
| `cluster_saveconfig` / `cluster_bumpepoch` / `cluster_flushslots` | `auto cluster_saveconfig()` (etc.) | Config ops. `Reply<status>` | `co_await c.cluster_saveconfig();` |
| `cluster_set_config_epoch` | `auto cluster_set_config_epoch(long long epoch)` | SET-CONFIG-EPOCH. `Reply<status>` | `co_await c.cluster_set_config_epoch(1);` |
| `cluster_keyslot` / `cluster_countkeysinslot` | `auto cluster_keyslot(key); auto cluster_countkeysinslot(int slot)` | Slot of key / key count in slot. `Reply<long long>` | `co_await c.cluster_keyslot("k");` |
| `cluster_getkeysinslot` | `auto cluster_getkeysinslot(int slot, int count)` | Keys in slot. `Reply<std::vector<std::string>>` | `co_await c.cluster_getkeysinslot(0,10);` |
| `cluster_count_failure_reports` | `auto cluster_count_failure_reports(const std::string& node_id)` | Failure-report count. `Reply<long long>` | `co_await c.cluster_count_failure_reports(id);` |
| `cluster_addslots` / `cluster_delslots` | `template<class...Slots> auto cluster_addslots(Slots&&...slots)` | Variadic slot numbers. `Reply<status>` | `co_await c.cluster_addslots(0,1,2);` |
| `cluster_addslotsrange` / `cluster_delslotsrange` | `auto cluster_addslotsrange(const std::vector<std::pair<int,int>>& ranges)` | Flatten (start,end) pairs. `Reply<status>` | `co_await c.cluster_addslotsrange({{0,100}});` |
| `cluster_setslot` | `auto cluster_setslot(int slot, const std::string& subcommand, const std::string& node_id="")` | SETSLOT MIGRATING\|IMPORTING\|NODE\|STABLE [node_id]. `Reply<status>` | `co_await c.cluster_setslot(0,"STABLE");` |
| `asking` / `readonly` / `readwrite` | `auto asking(); auto readonly(); auto readwrite()` | Per-connection redirection/replica-read mode. `Reply<status>` | `co_await c.readonly();` |

---

## ACL commands — `acl_commands<Derived>`
`acl_commands.h:38`.

| Command | Signature (coro) | Purpose / reply | Usage |
|---|---|---|---|
| `acl_list` / `acl_log` / `acl_getuser` / `acl_dryrun` | `auto acl_list(); auto acl_log(std::optional<long long> count=nullopt); auto acl_getuser(username); auto acl_dryrun(username, command, const std::vector<std::string>& args={})` | Introspection. `Reply<qb::json>` | `co_await c.acl_getuser("default");` |
| `acl_cat` / `acl_users` / `acl_help` | `auto acl_cat(const std::string& category=""); auto acl_users(); auto acl_help()` | Categories / users / help. `Reply<std::vector<std::string>>` | `co_await c.acl_users();` |
| `acl_whoami` / `acl_genpass` | `auto acl_whoami(); auto acl_genpass(std::optional<long long> bits=nullopt)` | Current user / generate password. `Reply<std::string>` | `co_await c.acl_whoami();` |
| `acl_setuser` | `template<class...Args> auto acl_setuser(const std::string& username, Args&&...rules)` | SETUSER; variadic rule tokens verbatim. `Reply<status>` | `co_await c.acl_setuser("u","on",">pw","~*","+@all");` |
| `acl_deluser` | `auto acl_deluser(const std::string& username)` | DELUSER; count deleted. `Reply<long long>` | `co_await c.acl_deluser("u");` |
| `acl_load` / `acl_save` | `auto acl_load(); auto acl_save()` | Reload / persist ACLs. `Reply<status>` | `co_await c.acl_save();` |

---

## Function commands — `function_commands<Derived>`
`function_commands.h:35`. Redis Functions. `numkeys` computed from `keys.size()`.

| Command | Signature (coro) | Purpose / reply | Usage |
|---|---|---|---|
| `function_list` / `function_stats` / `function_dump` | `auto function_list(const std::optional<std::string>& library=nullopt)` (etc.) | Introspection. `Reply<qb::json>` | `co_await c.function_list();` |
| `function_load` | `template<class...Args> auto function_load(const std::string& code, Args&&...options)` | LOAD [options...] code — **options emitted BEFORE code** (pass e.g. "REPLACE"). `Reply<status>` | `co_await c.function_load(code,"REPLACE");` |
| `function_delete` | `auto function_delete(const std::string& library)` | DELETE library. `Reply<status>` | `co_await c.function_delete("mylib");` |
| `function_flush` | `auto function_flush(const std::string& mode="SYNC")` | FLUSH ASYNC\|SYNC. `Reply<status>` | `co_await c.function_flush();` |
| `function_kill` | `auto function_kill()` | KILL. `Reply<status>` | `co_await c.function_kill();` |
| `function_restore` | `auto function_restore(const std::string& payload, const std::string& policy="APPEND")` | RESTORE [APPEND\|REPLACE\|FLUSH]. `Reply<status>` | `co_await c.function_restore(blob);` |
| `function_help` | `auto function_help()` | FUNCTION HELP. `Reply<std::vector<std::string>>` | `co_await c.function_help();` |
| `fcall` / `fcallRo` | `template<class Ret> auto fcall(const std::string& name, const std::vector<std::string>& keys={}, const std::vector<std::string>& args={})` | FCALL / FCALL_RO; numkeys auto. `Reply<Ret>` | `co_await c.fcall<long long>("f",{"k"});` |

---

## Module commands — `module_commands<Derived>`
`module_commands.h:34`.

| Command | Signature (coro) | Purpose / reply | Usage |
|---|---|---|---|
| `module_list` | `auto module_list()` | MODULE LIST. `Reply<qb::json>` | `co_await c.module_list();` |
| `module_load` | `template<class...Args> auto module_load(const std::string& path, Args&&...args)` | MODULE LOAD path [args...]. `Reply<status>` | `co_await c.module_load("/p.so");` |
| `module_unload` | `auto module_unload(const std::string& name)` | MODULE UNLOAD name. `Reply<status>` | `co_await c.module_unload("mod");` |
| `module_help` | `auto module_help()` | MODULE HELP. `Reply<std::vector<std::string>>` | `co_await c.module_help();` |

---

## Reply types — `reply.h`

### `qb::redis::Reply<T>`
`reply.h:1102` · `struct template`. Typed command result.

```cpp
template <typename T> struct Reply {
  bool        ok() const;  explicit operator bool() const;  // success (no Redis error)
  T&          result();    T& value();                      // parsed T
  reply_ptr&  raw();                                         // owning parser::Value
  std::string& error();                                     // owned error string (never dangling)
  template<class U> auto value_or(U&& default_value) const; // unwraps optional T
};
```

| Symbol | Header / kind | Purpose |
|---|---|---|
| `ReplyValue` | `reply.h:47` · type alias | `= parser::Value`; the native RESP reply value (variant over RESP2/RESP3 nodes). |
| `ReplyErrorType` | `reply.h:56` · enum class | `{ ERR, MOVED, ASK }` — generic vs cluster redirection. |
| `IReply` | `reply.h:1189` · abstract class | `virtual void operator()(std::unique_ptr<ReplyValue>)=0; virtual void fail(const std::string&)`. `operator()` **takes ownership** (nullptr = disconnect). |
| `TReply<Func,T>` | `reply.h:1212` · class template | Concrete handler: parses `T`, invokes `func(Reply<T>{...})`; nullptr→`{ok=false,error="disconnected"}`; thrown `qb::redis::Error`→`{ok=false}` with `e.what()` copied. |
| `is_optional_like<T>` | `reply.h:1096-1099` · trait | True for `std::optional<U>`; drives `value_or`. |
| `parse<T>` | `reply.h:305` (generic dispatcher) / `reply.h:411` (`ParseTag<T>` overloads) · function template | Convert `parser::Value` → `T` via `ParseTag<T>` dispatch; throws `ReplyParseError`/`ProtoError` on mismatch. |
| `ParseTag<T>` | `reply.h:193` · struct template | Empty tag for parse overload dispatch. |
| `to_status` / `parse_set_reply` / `parse_scan_reply` | `reply.h:665/677/588` · free fns | Extract `status`; `parse_set_reply` true iff "OK" (false, not throw, on nil); `parse_scan_reply` → new cursor (unsigned 64-bit). |
| `type predicates` | `reply.h:200-284` · noexcept free fns | `is_string/is_error/is_integer/is_nil/is_array/is_double/is_bool/is_map/is_set/is_push/is_bignum/is_status`. |
| `is_array_or_push` / `get_pubsub_element` / `get_pubsub_size` | `reply.h:252-270` · free fns | Uniform pub/sub accessors (Array in RESP2, Push in RESP3); element returns nullptr if OOB. |
| `type_to_string` | `reply.h:658` · free fn | RESP type name of a reply. |
| `put_in_pipe` / `to_redis_string` / `redis_count` | `reply.h:1080/805-893/704-757` · serialization | Serialize a command into a qb-io pipe; chrono `seconds`/`milliseconds` overloads emit `.count()` (native-unit command boundary). |
| `REDIS_MAX_STRING_SIZE` / `is_valid_redis_string_size` | `reply.h:786/795` | 512MB max serialized arg; exceeding throws `SecurityError`. |

### Error hierarchy (`reply.h`)
All derive from `qb::redis::Error : std::exception` (`reply.h:62`; `what()` valid for the exception's lifetime).

| Class | Header | Meaning |
|---|---|---|
| `ProtoError` | `reply.h:86` | Malformed/unexpected RESP structure. |
| `ConnectionError` | `reply.h:102` | TCP connection could not be established. |
| `AuthError` | `reply.h:118` | NOAUTH / WRONGPASS. |
| `CommandError` | `reply.h:134` | Redis returned `-ERR` for a command. |
| `TimeoutError` | `reply.h:150` | A connect/command exceeded its deadline. |
| `ReplyParseError` | `reply.h:166` (: `ProtoError`) | Wrong RESP type for the requested C++ type. |
| `SecurityError` | `reply.h:788` | Serialized argument exceeds `REDIS_MAX_STRING_SIZE`. |

### Server-side reply helpers (`server_reply.h`)
- `ServerReply<T>` (`server_reply.h:46`, +`void` specialization) — `{bool ok; T value; std::string error;}` with ref-qualified `result()`.
- `ValueExtractor` (`server_reply.h:100`) — non-throwing `std::optional`-based accessors over a **borrowed** `parser::Value` (must outlive the extractor): `as_string/as_string_view/as_integer/as_double/as_bool/as_array/as_map/as_set/is_null/is_error/raw`.
- `AsyncResult<T>` (`server_reply.h:305`, +`void`) — coroutine-friendly `qb::expected<T,std::string>` wrapper; `operator bool`, `operator->`.
- `extract_string` / `extract_integer` / `extract_string_array` / `extract_string_map` (`server_reply.h:269-293`), `extract_stream_id` / `extract_score_member` (`server_reply.h:396/414`) — return `qb::expected` (no exceptions).

---

## Domain & option types — `types.h`

| Type | Header / kind | Notes |
|---|---|---|
| `reply_ptr` | `types.h:41` · `= std::unique_ptr<parser::Value>` | Move-only owning reply pointer. |
| `status` | `types.h:475` · struct | Simple-string reply wrapper; `operator bool`/`ok()` true iff payload == "OK". |
| `stream_id` | `types.h:306` · struct | `{long long timestamp; long long sequence;}`; `to_string()` → "ts-seq". |
| `stream_entry` / `stream_entry_list` / `map_stream_entry_list` | `types.h:327-333` | `{stream_id id; qb::unordered_map<std::string,std::string> fields;}`; list; per-stream map. |
| `score` / `score_member` | `types.h:336/348` | `score{double value}`; `score_member{double score; std::string member}` (field order: score then member). |
| `geo_pos` / `geo_distance` | `types.h:292/300` | `geo_pos{double longitude; double latitude}`; `geo_distance{std::string member; double distance}`. |
| `subscription` | `types.h:464` · struct | `{std::optional<std::string> channel; long long num;}`. |
| `message` / `pmessage` | `types.h:454/462` | `message{pattern; channel; payload; reply_ptr raw}`; `pmessage : message`. |
| `scan<Out=std::vector<std::string>>` | `types.h:534` · struct template | `{std::size_t cursor; Out items;}` (cursor 0 = complete). |
| `json_value` | `types.h:411` · struct | Self-describing JSON-like variant (RedisJSON); RESP big numbers kept as strings. |
| `pipeline_result` | `types.h:404` · struct | `{size_t size; size_t error_count; bool all_succeeded;}` — MULTI/EXEC summary; per-command replies NOT stored (use `Reply<pipeline_result>.raw()`). |
| `memory_info` | `types.h:377` · struct | Parsed INFO-memory section (`size_t` fields, 0 fallback). |
| `cluster_node` | `types.h:363` · struct | One CLUSTER NODES line parsed field-by-field. |
| `search_result` | `types.h:356` · struct | FT.SEARCH per-document `{key; fields; values}`. |
| `error` | `types.h:540` · struct | `{std::string what; reply_ptr raw;}`. |
| `LimitOptions` | `types.h:282` · struct | `{long long offset=0; long long count=-1;}` (count -1 = unbounded). A **negative `offset`** on `zrangebylex`/`zrangebyscore` (and the `zrevrange*` variants) suppresses the `LIMIT` clause entirely (no triplet emitted); the default `{0,-1}` still sends `LIMIT 0 -1` (all). |

### Enums (`types.h:48-62`)
`UpdateType{EXIST,NOT_EXIST,ALWAYS}` (→XX/NX, ALWAYS=no flag) · `InsertPosition{BEFORE,AFTER}` · `ListPosition{LEFT,RIGHT}` · `BoundType{CLOSED,OPEN,LEFT_OPEN,RIGHT_OPEN}` · `Aggregation{SUM,MIN,MAX}` · `BitOp{AND,OR,XOR,NOT}` · `GeoUnit{M,KM,MI,FT}` (default M) · `XtrimStrategy{MAXLEN,MINID}`.
`to_string(...)` overloads (`types.h:550-560`) render `BitOp/UpdateType/Aggregation/GeoUnit/InsertPosition/ListPosition` to Redis keyword strings.

### Interval types (`types.h:65-275`)
`UnboundedInterval<T>` / `BoundedInterval<T>` / `LeftBoundedInterval<T>` / `RightBoundedInterval<T>`; aliases `lex_interval = BoundedInterval<std::string>`, `score_interval = BoundedInterval<double>`. Each exposes `lower()`/`upper()` returning the RESP-encoded bound string (consumed by `zcount`/`zrangebyscore`/`zrangebylex`).

---

## Protocol & parser (advanced)

- `qb::protocol::redis<IO_>` (`redis.h:80`) · `class template : qb::io::async::AProtocol<IO_>` — native RESP2/RESP3 protocol attached to a qb-io session (CRTP). RESP3, `max_nesting_depth=64`, `max_bulk_size=512MiB`, `max_array_size=1,000,000`. `message{std::unique_ptr<parser::Value> reply}` (`redis.h:94`) — one parsed reply (ownership moved into `on(message)`).
- `qb::redis::redis_awaiter<T,Operation>` (`redis.h:696`) — coroutine awaiter yielding `Reply<T>`; guards resume with a `shared_ptr<bool>` so a reply arriving after the awaiter is destroyed no-ops. `make_redis_awaiter<T>(Func&&)` (`redis.h:737`) builds one from a callback op.
- `qb::redis::parser` namespace (`src/qbm/redis/parser/types.h`): `Value : ValueBase` (`src/qbm/redis/parser/types.h:453`) — variant of all 15 RESP node types (`Null/SimpleString/SimpleError/Integer/BulkString/Array/Boolean/Double/BigNumber/BulkError/VerbatimString/Map/Attribute/Set/Push`) with `is_*`/`as_*`/`to_*` helpers; `ProtocolVersion{RESP2=2,RESP3=3}` (`types.h:43`); `ParseErrorCode` (`types.h:50`) / `ParseError` (`types.h:66`) / `ParseResult<T> = expected<T,ParseError>` (`types.h:119`); `type_id::*` first-byte prefix constants (`types.h:136`); `is_valid_type_prefix` / `is_resp3_type` / `is_aggregate_type`.
