# `qbm-redis`: Function Commands

This document covers Redis commands for managing and executing server-side Lua scripts, known as Functions (introduced in Redis 7.0).

Reference: [Redis Function Commands](https://redis.io/commands/?group=scripting) (Note: Function commands are a newer alternative to some older script commands, but both exist).

## Common Reply Types

*   `qb::json`: For commands returning structured data (e.g., `FUNCTION LIST`, `FUNCTION STATS`, `FUNCTION DUMP`).
*   `qb::redis::status`: For commands returning a simple "OK" or status (e.g., `FUNCTION LOAD`, `FUNCTION DELETE`, `FUNCTION FLUSH`, `FUNCTION KILL`, `FUNCTION RESTORE`).
*   `std::vector<std::string>`: For `FUNCTION HELP`.

## Commands

All Function commands are prefixed with `FUNCTION`.

### `FUNCTION DELETE libraryname`

Deletes a library and all its functions.

*   **Sync:** `status function_delete(const std::string &libraryname)`
*   **Async:** `Derived& function_delete(Func &&func, const std::string &libraryname)`
*   **Reply:** `Reply<status>`
*   **Example (from `test-function-commands.cpp`):**
    ```cpp
    // This will likely fail if the library doesn't exist, which is expected in the test.
    try {
        redis.function_delete("nonexistent_library");
        FAIL() << "Expected an exception for nonexistent library";
    } catch (const std::exception& e) {
        std::string error = e.what();
        EXPECT_TRUE(error.find("ERR Library not found") != std::string::npos || 
                    error.find("unknown command") != std::string::npos);
    }
    ```

### `FUNCTION DUMP`

Returns a serialized payload representing all functions.

*   **Sync:** `qb::json function_dump()` (The C++ API returns `qb::json`, but Redis returns a bulk string. The `qb::json` will contain this string.)
*   **Async:** `Derived& function_dump(Func &&func)`
*   **Reply:** `Reply<qb::json>`
*   **Example (from `test-function-commands.cpp`):**
    ```cpp
    auto dump_data = redis.function_dump();
    // Dump is typically a string (binary for Redis) with the serialized functions.
    // The test checks if it's a string or binary within the JSON wrapper.
    EXPECT_TRUE(dump_data.is_string() || dump_data.is_binary()); 
    ```

### `FUNCTION FLUSH [ASYNC|SYNC]`

Deletes all libraries and functions.

*   **Sync:** `status function_flush(const std::string &mode = "SYNC")` (`mode` can be "ASYNC" or "SYNC")
*   **Async:** `Derived& function_flush(Func &&func, const std::string &mode = "SYNC")`
*   **Reply:** `Reply<status>`
*   **Example (from `test-function-commands.cpp`):**
    ```cpp
    auto result = redis.function_flush(); // Defaults to SYNC
    EXPECT_EQ(result, "OK");
    ```

### `FUNCTION HELP`

Returns help information about `FUNCTION` subcommands.

*   **Sync:** `std::vector<std::string> function_help()`
*   **Async:** `Derived& function_help(Func &&func)`
*   **Reply:** `Reply<std::vector<std::string>>`
*   **Example (from `test-function-commands.cpp`):**
    ```cpp
    auto help_lines = redis.function_help();
    EXPECT_FALSE(help_lines.empty());
    // Each line should be a non-empty string
    for (const auto& line : help_lines) {
        EXPECT_FALSE(line.empty());
    }
    ```

### `FUNCTION KILL`

Kills a function that is currently executing.

*   **Sync:** `status function_kill()`
*   **Async:** `Derived& function_kill(Func &&func)`
*   **Reply:** `Reply<status>`
*   **Example (from `test-function-commands.cpp`):
    ```cpp
    // This typically fails if no script is running, which is the test scenario.
    try {
        redis.function_kill();
        FAIL() << "Expected an exception as no functions are running";
    } catch (const std::exception& e) {
        std::string error = e.what();
        EXPECT_TRUE(error.find("ERR No scripts in execution") != std::string::npos || 
                    error.find("unknown command") != std::string::npos);
    }
    ```

### `FUNCTION LIST [LIBRARYNAME libraryname_pattern] [WITHCODE]`

Returns a list of all functions, optionally filtered by library name pattern and with or without code.

*   **Sync:** `qb::json function_list(const std::optional<std::string> &libraryname_pattern = std::nullopt)`
    *   **Note:** `WITHCODE` is not explicitly exposed as a parameter. To use `WITHCODE`, construct the command manually using the generic `command<qb::json>()` method: `redis.command<qb::json>("FUNCTION", "LIST", "WITHCODE");` or `redis.command<qb::json>("FUNCTION", "LIST", "LIBRARYNAME", mypattern, "WITHCODE");`
*   **Async:** `Derived& function_list(Func &&func, const std::optional<std::string> &libraryname_pattern = std::nullopt)`
*   **Reply:** `Reply<qb::json>`
*   **Example (from `test-function-commands.cpp`):**
    ```cpp
    auto functions = redis.function_list();
    EXPECT_TRUE(functions.is_array()); // Functions are returned as a JSON array of objects
    // Example with library name filter:
    // auto specific_functions = redis.function_list("mylib_*");
    ```

### `FUNCTION LOAD [REPLACE] function_code`

Loads a library into Redis.

*   **Sync:** `status function_load(const std::string &function_code, Args&&... options)` (options like "REPLACE")
*   **Async:** `Derived& function_load(Func &&func, const std::string &function_code, Args&&... options)`
*   **Reply:** `Reply<status>`
*   **Example (from `test-function-commands.cpp`):
    ```cpp
    // This test expects failure with invalid code.
    try {
        redis.function_load("invalid lua code#!@");
        FAIL() << "Expected an exception for invalid function code";
    } catch (const std::exception& e) {
        std::string error = e.what();
        EXPECT_TRUE(error.find("ERR Error compiling Lua script") != std::string::npos || 
                    error.find("unknown command") != std::string::npos);
    }
    // Example of a valid load:
    // status load_reply = redis.function_load("redis.register_function('myfunc', function(keys, args) return args[1] end)");
    // EXPECT_TRUE(load_reply);
    ```

### `FUNCTION RESTORE serialized_payload [FLUSH|APPEND|REPLACE]`

Restores functions from a payload created by `FUNCTION DUMP`.

*   **Sync:** `status function_restore(const std::string &payload, const std::string &policy = "APPEND")` (`policy` can be "FLUSH", "APPEND", "REPLACE")
*   **Async:** `Derived& function_restore(Func &&func, const std::string &payload, const std::string &policy = "APPEND")`
*   **Reply:** `Reply<status>`
*   **Example (from `test-function-commands.cpp`):
    ```cpp
    // This test expects failure with invalid dump data.
    try {
        redis.function_restore("invalid_dump_data");
        FAIL() << "Expected an exception for invalid dump data";
    } catch (const std::exception& e) {
        std::string error = e.what();
        EXPECT_TRUE(error.find("ERR DUMP payload version or checksum are wrong") != std::string::npos || 
                    error.find("unknown command") != std::string::npos);
    }
    ```

### `FUNCTION STATS`

Returns information about the function runtime.

*   **Sync:** `qb::json function_stats()`
*   **Async:** `Derived& function_stats(Func &&func)`
*   **Reply:** `Reply<qb::json>`
*   **Example (from `test-function-commands.cpp`):**
    ```cpp
    auto stats = redis.function_stats();
    EXPECT_TRUE(stats.is_object());
    // Stats should contain information about running scripts and engines
    EXPECT_TRUE(stats.contains("running_script") || stats.contains("engines"));
    ``` 