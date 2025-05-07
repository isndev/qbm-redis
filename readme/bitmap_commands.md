# `qbm-redis`: Bitmap Commands

This document covers Redis commands operating on Bitmaps, which are essentially manipulations of String values at the bit level.

Reference: [Redis Bitmap Commands](https://redis.io/commands/?group=bitmap)

## Common Reply Types

*   `qb::redis::Reply<long long>`: For `BITCOUNT`, `BITPOS`, `BITFIELD` (individual operations within BITFIELD might return `std::optional<long long>`).
*   `qb::redis::Reply<bool>`: For `GETBIT` (returns the bit value as bool), `SETBIT` (returns the original bit value as bool).
*   **Note:** The C++ API often wraps the direct Redis integer reply (0 or 1 for bits) into a `bool` for clarity.

## Commands

### `BITCOUNT key [start end [BYTE|BIT]]`

Counts the number of set bits (population count) in a string.

*   **Sync:** `long long bitcount(const std::string &key, long long start = 0, long long end = -1)`
*   **Async:** `Derived& bitcount(Func &&func, const std::string &key, long long start = 0, long long end = -1)`
*   **Reply:** `Reply<long long>`
*   **Note:** The `BYTE|BIT` option (Redis 7+) is not currently exposed directly; the command defaults to byte-wise range if `start`/`end` are provided.
*   **Example (from `test-bitmap-commands.cpp`):**
    ```cpp
    std::string key = test_key("bitcount");
    redis.set(key, std::string("\xFF\x00\xFF", 3)); // Represents 11111111 00000000 11111111
    EXPECT_EQ(redis.bitcount(key), 16);
    EXPECT_EQ(redis.bitcount(key, 0, 0), 8); // Count bits in the first byte
    EXPECT_EQ(redis.bitcount(key, 1, 1), 0); // Count bits in the second byte
    ```

### `BITFIELD key [GET type offset] [SET type offset value] [INCRBY type offset increment] [OVERFLOW WRAP|SAT|FAIL]`

Performs arbitrary bitfield integer operations on a string value.

*   **Sync:** `std::vector<std::optional<long long>> bitfield(const std::string &key, const std::vector<std::string> &operations)`
*   **Async:** `Derived& bitfield(Func &&func, const std::string &key, const std::vector<std::string> &operations)`
*   **Reply:** `Reply<std::vector<std::optional<long long>>>` (Each element corresponds to an operation, `std::nullopt` for OVERFLOW FAIL or if the operation does not return a value).
*   **Note:** The `operations` vector should contain strings representing the subcommands (e.g., `"GET u4 0"`, `"SET i8 100 5"`).
*   **Example (from `test-bitmap-commands.cpp`):**
    ```cpp
    std::string key = test_key("bitfield");
    std::vector<std::string> operations = {
        "SET", "u4", "#0", "10",    // Set 4-bit unsigned int at offset #0 (0th bit) to 10 (1010b)
        "GET", "u4", "#0"         // Get 4-bit unsigned int at offset #0
    };
    auto results = redis.bitfield(key, operations);
    // results[0] is the old value at offset #0 before SET (likely 0 if key was new)
    // results[1] is the value 10 fetched by GET
    if (results.size() == 2 && results[1].has_value()) {
        EXPECT_EQ(results[1].value(), 10);
    }
    ```

### `BITOP operation destkey key [key ...]`

Performs bitwise operations between strings.

*   **Sync:** `long long bitop(const std::string &operation, const std::string &destkey, const std::vector<std::string> &keys)`
*   **Async:** `Derived& bitop(Func &&func, const std::string &operation, const std::string &destkey, const std::vector<std::string> &keys)`
*   **Reply:** `Reply<long long>` (length of the string stored at `destkey`).
*   **`operation`:** Can be `"AND"`, `"OR"`, `"XOR"`, `"NOT"`. Note `NOT` only takes one source key.
*   **Example (from `test-bitmap-commands.cpp`):**
    ```cpp
    std::string key1 = test_key("bitop1");
    std::string key2 = test_key("bitop2");
    std::string destkey = test_key("bitop_dest");

    redis.set(key1, "\x0F"); // 00001111
    redis.set(key2, "\xF0"); // 11110000

    EXPECT_EQ(redis.bitop("AND", destkey, {key1, key2}), 1); // Result length
    auto result = redis.get(destkey);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, std::string("\x00", 1)); // 00000000
    ```

### `BITPOS key bit [start [end [BYTE|BIT]]]`

Finds the first bit set to 0 or 1 in a string.

*   **Sync:** `long long bitpos(const std::string &key, bool bit, long long start = 0, long long end = -1)`
*   **Async:** `Derived& bitpos(Func &&func, const std::string &key, bool bit, long long start = 0, long long end = -1)`
*   **Reply:** `Reply<long long>` (position of the first bit, or -1 if not found).
*   **Note:** The `BYTE|BIT` option (Redis 7+) is not currently exposed directly.
*   **Example (from `test-bitmap-commands.cpp`):**
    ```cpp
    std::string key = test_key("bitpos");
    redis.set(key, std::string("\x00\xFF", 2)); // 00000000 11111111

    EXPECT_EQ(redis.bitpos(key, true), 8);  // First set bit (1) is at index 8 (start of second byte)
    EXPECT_EQ(redis.bitpos(key, false), 0); // First clear bit (0) is at index 0
    ```

### `GETBIT key offset`

Returns the bit value at `offset` in the string value stored at `key`.

*   **Sync:** `bool getbit(const std::string &key, long long offset)`
*   **Async:** `Derived& getbit(Func &&func, const std::string &key, long long offset)`
*   **Reply:** `Reply<long long>` (where 0 or 1 is converted to `bool` by the sync wrapper)
*   **Example (from `test-bitmap-commands.cpp`):**
    ```cpp
    std::string key = test_key("getbit_setbit");
    redis.setbit(key, 7, true); // Set 8th bit (index 7) to 1
    EXPECT_TRUE(redis.getbit(key, 7));
    EXPECT_FALSE(redis.getbit(key, 0));
    ```

### `SETBIT key offset value`

Sets or clears the bit at `offset` in the string value stored at `key`. Returns the original bit value at `offset`.

*   **Sync:** `bool setbit(const std::string &key, long long offset, bool value)`
*   **Async:** `Derived& setbit(Func &&func, const std::string &key, long long offset, bool value)`
*   **Reply:** `Reply<long long>` (where 0 or 1 is converted to `bool` by the sync wrapper)
*   **Example (from `test-bitmap-commands.cpp`):**
    ```cpp
    std::string key = test_key("getbit_setbit");
    // Set 8th bit (index 7) to 1, returns original value (0 if new key)
    EXPECT_FALSE(redis.setbit(key, 7, true)); 
    // Set 8th bit to 0, returns original value (1)
    EXPECT_TRUE(redis.setbit(key, 7, false)); 
    ``` 