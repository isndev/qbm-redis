# `qbm-redis`: Publish/Subscribe Commands

This document covers Redis commands related to the Publish/Subscribe messaging paradigm.

Reference: [Redis Pub/Sub Commands](https://redis.io/commands/?group=pubsub)

## Key Concepts

*   **Publishers:** Clients that send messages to named channels.
*   **Subscribers:** Clients that listen for messages on specific channels or patterns.
*   **Channels:** Named conduits for messages.
*   **Patterns:** Globs (like `news.*`) that allow subscribers to listen to multiple channels matching the pattern.
*   **Decoupling:** Publishers and subscribers are decoupled; publishers don't know who (if anyone) is listening.

## Client Roles

`qbm-redis` provides different client types for Pub/Sub:

*   **`qb::redis::tcp::client` (or `ssl::client`):** Used for **publishing** messages using the `publish` command.
*   **`qb::redis::tcp::cb_consumer` (or `ssl::cb_consumer`):** Used for **subscribing** to channels or patterns. This client enters a dedicated subscription mode and uses callbacks to deliver received messages.

**Important:** A regular client that issues `SUBSCRIBE` or `PSUBSCRIBE` will enter subscription mode and **cannot issue non-Pub/Sub commands** afterwards (except for `UNSUBSCRIBE`, `PUNSUBSCRIBE`, `PING`, `QUIT`). Use the dedicated `cb_consumer` for handling subscriptions.

## Common Types & Reply Types

*   **`qb::redis::Reply<long long>`:** For `PUBLISH` (returns number of clients that received the message).
*   **`qb::redis::message` Struct:** `{ std::string channel; std::string payload; }` (For regular channel messages).
*   **`qb::redis::pmessage` Struct:** `{ std::string pattern; std::string channel; std::string payload; }` (For pattern messages).
*   **`qb::redis::subscription` Struct:** `{ std::string channel_or_pattern; long long num_subscriptions; }` (For subscribe/unsubscribe confirmations).

## Commands

### `PUBLISH channel message`

Posts a message to the given channel.

*   **Interface:** `qb::redis::tcp::client`
*   **Sync:** `Reply<long long> publish(const std::string &channel, const std::string &message)`
*   **Async:** `void publish_async(const std::string &channel, const std::string &message, Callback<long long> cb)`

```cpp
// Publisher client
qb::redis::tcp::client publisher("tcp://127.0.0.1:6379");
auto reply = publisher.publish("news-channel", "Important update!");
if (reply) {
    qb::io::cout() << "Message published to " << reply.value() << " subscribers.\n";
}
```

## Subscription Handling (`cb_consumer`)

The `qb::redis::tcp::cb_consumer` manages the subscription state and delivers messages via callbacks.

### Setting Callbacks

Use the `on_message`, `on_pmessage`, `on_subscribe`, etc. methods to register your handlers *before* connecting or subscribing.

```cpp
#include <qbm/redis/redis.h>

std::atomic<int> msg_count = 0;

qb::redis::tcp::cb_consumer consumer("tcp://127.0.0.1:6379");

consumer.on_message([](qb::redis::message&& msg) {
    qb::io::cout() << "MSG Channel [" << msg.channel << "]: " << msg.payload << "\n";
    msg_count++;
});

consumer.on_pmessage([](qb::redis::pmessage&& msg) {
    qb::io::cout() << "PMSG Pattern[" << msg.pattern << "] Channel[" << msg.channel << "]: " << msg.payload << "\n";
    msg_count++;
});

consumer.on_subscribe([](qb::redis::subscription&& sub) {
    qb::io::cout() << "Subscribed to " << sub.channel_or_pattern 
                 << " (Total subs: " << sub.num_subscriptions << ")\n";
});

consumer.on_unsubscribe([](qb::redis::subscription&& sub) {
    qb::io::cout() << "Unsubscribed from " << sub.channel_or_pattern 
                 << " (Remaining subs: " << sub.num_subscriptions << ")\n";
});

consumer.on_error([](qb::redis::error&& err) {
    qb::io::cout() << "Consumer Error: " << err.what() << "\n";
});

consumer.on_disconnected([](int reason) {
    qb::io::cout() << "Consumer Disconnected. Reason: " << reason << "\n";
});
```

### Subscribing / Unsubscribing

Call these methods *after* setting callbacks and connecting.

*   **`SUBSCRIBE channel [channel ...]`**
    *   **Method:** `subscribe(const std::vector<std::string> &channels)`
*   **`PSUBSCRIBE pattern [pattern ...]`**
    *   **Method:** `psubscribe(const std::vector<std::string> &patterns)`
*   **`UNSUBSCRIBE [channel [channel ...]]`**
    *   **Method:** `unsubscribe(const std::vector<std::string> &channels = {})` (empty vector unsubscribes from all channels)
*   **`PUNSUBSCRIBE [pattern [pattern ...]]`**
    *   **Method:** `punsubscribe(const std::vector<std::string> &patterns = {})` (empty vector unsubscribes from all patterns)

### Running the Consumer

The consumer needs its event loop to run to receive messages. **This is typically done in a separate thread** or integrated into a QB actor's core.

```cpp
// Connect the consumer
if (!consumer.connect_sync()) { // Or use connect_async
    return 1; // Handle connection error
}

// Subscribe
consumer.subscribe({"news-channel", "alerts"});
consumer.psubscribe({"user.*.updates"});

// In a separate thread or main loop:
while (consumer.is_connected()) { // Or other loop condition
    consumer.consume(); // Process replies and messages, blocks briefly
    // Or use consume_nonblocking() and manage sleep/yield
}

// Or if integrated with qb-core/qb-io async loop
// Call consumer.start() to register with the listener
// The main loop (qb::io::async::run() or engine loop) will drive it.
```

**Note:** The `qb::redis::tcp::cb_consumer` uses the underlying `qb-io` async system. If used outside the `qb-core` engine, you must manually run the `qb::io::async::listener` event loop for callbacks to fire. 