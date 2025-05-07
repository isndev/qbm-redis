# `qbm-redis`: Subscription Commands

This document covers Redis commands related to the Publish/Subscribe messaging paradigm, specifically focusing on how clients subscribe to and unsubscribe from channels and patterns.

Reference: [Redis Pub/Sub Commands](https://redis.io/commands/?group=pubsub)

## Key Concepts for Subscriptions

*   **Subscribers:** Clients that listen for messages.
*   **Channels:** Named conduits for messages. Subscribers listen to specific channels.
*   **Patterns:** Globs (e.g., `news.*`) that allow subscribers to listen to multiple channels matching the pattern.
*   **Subscription Mode:** When a client issues `SUBSCRIBE` or `PSUBSCRIBE`, it enters a special mode where it can only execute subscription-related commands (`UNSUBSCRIBE`, `PUNSUBSCRIBE`, `PING`, `QUIT`).

## `qb::redis::tcp::cb_consumer`

For handling subscriptions effectively in an asynchronous environment, `qbm-redis` provides the `qb::redis::tcp::cb_consumer` (and its SSL variant `qb::redis::tcp::ssl::cb_consumer`). This specialized client handles the subscription mode and delivers incoming messages and subscription confirmations via user-provided callbacks.

**It is highly recommended to use `cb_consumer` for all subscription needs.**

### Setting Callbacks on `cb_consumer`

Before connecting or subscribing, you register callbacks for various events:

*   `on_message(Func &&callback)`: For messages received on exact channels.
    *   Callback signature: `void(qb::redis::message&& msg)`
    *   `msg` fields: `channel`, `payload` (message content).
*   `on_pmessage(Func &&callback)`: For messages received on pattern subscriptions.
    *   Callback signature: `void(qb::redis::pmessage&& msg)`
    *   `msg` fields: `pattern`, `channel` (actual channel message was sent to), `payload`.
*   `on_subscribe(Func &&callback)`: Confirmation for `SUBSCRIBE` and `PSUBSCRIBE`.
    *   Callback signature: `void(qb::redis::subscription&& sub)`
    *   `sub` fields: `channel_or_pattern`, `num_subscriptions` (current total active subscriptions for this client).
*   `on_unsubscribe(Func &&callback)`: Confirmation for `UNSUBSCRIBE` and `PUNSUBSCRIBE`.
    *   Callback signature: `void(qb::redis::subscription&& sub)`
    *   `sub` fields: `channel_or_pattern`, `num_subscriptions` (remaining active subscriptions).
*   `on_error(Func &&callback)`: For errors encountered by the consumer.
    *   Callback signature: `void(qb::redis::error&& err)`
*   `on_disconnected(Func &&callback)`: When the consumer disconnects.
    *   Callback signature: `void(int reason_code)`

**Example (`test-publish-commands.cpp`):**
```cpp
qb::redis::tcp::cb_consumer consumer{REDIS_URI, [&](auto &&msg) {
    // This is a simplified constructor callback for messages only.
    // For more control, use the on_message, on_pmessage, etc. methods.
    EXPECT_EQ(msg.message, TEST_MESSAGE);
    // ... process message ...
}};

// More detailed setup:
qb::redis::tcp::cb_consumer detailed_consumer;
detailed_consumer.on_message([](qb::redis::message&& msg){ /* ... */ });
detailed_consumer.on_pmessage([](qb::redis::pmessage&& msg){ /* ... */ });
detailed_consumer.on_subscribe([](qb::redis::subscription&& sub){ /* ... */ });
// ... and so on for other event types.
```

## Subscription Commands (via `cb_consumer`)

These commands are methods of the `qb::redis::tcp::cb_consumer` class.

### `SUBSCRIBE channel [channel ...]`

Subscribes the client to the specified channels.

*   **Method (single channel):** `Derived& subscribe(Func &&func, const std::string &channel)`
*   **Method (multiple channels):** `Derived& subscribe(Func &&func, const std::vector<std::string> &channels)`
*   **Sync (within consumer context, typically for initial setup):** `qb::redis::subscription subscribe(const std::string &channel)` or `qb::redis::subscription subscribe(const std::vector<std::string> &channels)`
    *   **Note:** The synchronous versions on `cb_consumer` still operate within its async nature. They will send the command and wait for the subscription confirmation from Redis *before* returning. The main purpose is to simplify initial subscription setup.
*   **Reply (via async callback `on_subscribe` or sync return):** `qb::redis::subscription`
*   **Example (from `test-subscription-commands.cpp`):**
    ```cpp
    // Async subscription
    bool subscribe_status = false;
    consumer.subscribe(
        [&subscribe_status](auto &&reply) {
            EXPECT_TRUE(reply.ok());
            EXPECT_TRUE(reply.result().channel.has_value());
            EXPECT_EQ(*reply.result().channel, TEST_CHANNEL);
            subscribe_status = true;
        },
        TEST_CHANNEL);
    // ... wait for callback ...
    ```

### `UNSUBSCRIBE [channel [channel ...]]`

Unsubscribes the client from the given channels, or from all channels if no channel is given.

*   **Method (single/all):** `Derived& unsubscribe(Func &&func, const std::string &channel = "")`
*   **Method (multiple channels):** `Derived& unsubscribe(Func &&func, const std::vector<std::string> &channels)`
*   **Sync (within consumer context):** `qb::redis::subscription unsubscribe(const std::string &channel = "")` or `qb::redis::subscription unsubscribe(const std::vector<std::string> &channels)`
*   **Reply (via async callback `on_unsubscribe` or sync return):** `qb::redis::subscription`
*   **Example (from `test-subscription-commands.cpp`):**
    ```cpp
    // Assuming consumer is subscribed to TEST_CHANNEL
    bool unsubscribe_complete = false;
    consumer.unsubscribe(
        [&](auto &&reply) {
            ASSERT_TRUE(reply.ok());
            // ... check reply.result() ...
            unsubscribe_complete = true;
        },
        TEST_CHANNEL);
    // ... wait for callback ...
    ```

### `PSUBSCRIBE pattern [pattern ...]`

Subscribes the client to the given patterns.

*   **Method (single pattern):** `Derived& psubscribe(Func &&func, const std::string &pattern)`
*   **Method (multiple patterns):** `Derived& psubscribe(Func &&func, const std::vector<std::string> &patterns)`
*   **Sync (within consumer context):** `qb::redis::subscription psubscribe(const std::string &pattern)` or `qb::redis::subscription psubscribe(const std::vector<std::string> &patterns)`
*   **Reply (via async callback `on_subscribe` or sync return):** `qb::redis::subscription`
*   **Example (from `test-subscription-commands.cpp`):**
    ```cpp
    bool psubscribe_status = false;
    consumer.psubscribe(
        [&psubscribe_status](auto &&reply) {
            EXPECT_TRUE(reply.ok());
            // ... check reply.result() ...
            psubscribe_status = true;
        },
        TEST_PATTERN);
    // ... wait for callback ...
    ```

### `PUNSUBSCRIBE [pattern [pattern ...]]`

Unsubscribes the client from the given patterns, or from all patterns if no pattern is given.

*   **Method (single/all):** `Derived& punsubscribe(Func &&func, const std::string &pattern = "")`
*   **Method (multiple patterns):** `Derived& punsubscribe(Func &&func, const std::vector<std::string> &patterns)`
*   **Sync (within consumer context):** `qb::redis::subscription punsubscribe(const std::string &pattern = "")` or `qb::redis::subscription punsubscribe(const std::vector<std::string> &patterns)`
*   **Reply (via async callback `on_unsubscribe` or sync return):** `qb::redis::subscription`
*   **Example (from `test-subscription-commands.cpp`):**
    ```cpp
    // Assuming consumer is subscribed to TEST_PATTERN
    bool punsubscribe_complete = false;
    consumer.punsubscribe(
        [&](auto &&reply) {
            ASSERT_TRUE(reply.ok());
            // ... check reply.result() ...
            punsubscribe_complete = true;
        },
        TEST_PATTERN);
    // ... wait for callback ...
    ```

## Running the Consumer

The `cb_consumer` needs its event loop to run to process network events and trigger callbacks. If you are using `qb-core` and the consumer is part of an actor, the actor's event loop will drive the consumer once it's started (e.g., by calling `consumer.start()` if it's a `qb::io::component`).

If used standalone (e.g., in tests or a simple main), you typically need to run the `qb::io::async::run()` loop:

```cpp
// After connecting and subscribing the consumer:
publisher.publish("some_channel", "message");

// Allow time for message processing in an async context
for (int i = 0; i < 10 && !message_received_flag; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    qb::io::async::run(EVRUN_NOWAIT); // Process pending I/O events
}
EXPECT_TRUE(message_received_flag);
```

Refer to `test-subscription-commands.cpp` and `test-publish-commands.cpp` for more detailed examples of using the `cb_consumer`. 