# `qbm-redis`: Scripting Commands

This document covers Redis commands for executing Lua scripts on the server.

Reference: [Redis Scripting Commands](https://redis.io/commands/?group=scripting)

## Key Concepts

*   **Atomic Execution:** Scripts run atomically on the Redis server.
*   **Reduced Network Latency:** Complex operations can be performed server-side, reducing round trips.
*   **Caching:** Scripts can be loaded into Redis and executed by their SHA1 hash (`EVALSHA`), saving bandwidth.

## Reply Types

*   The reply type for `EVAL` and `EVALSHA` depends entirely on what the Lua script returns. The client uses `qb::redis::Reply<qb::redis::json_value>` to represent this potentially complex return type. `qb::redis::json_value` is a `std::variant` capable of holding various types (string, number, boolean, array, map, null) mirroring JSON types.
*   `SCRIPT EXISTS`: `Reply<std::vector<bool>>`.
*   `SCRIPT LOAD`: `Reply<std::string>` (returns the SHA1 hash).
*   `SCRIPT FLUSH`, `SCRIPT KILL`: `qb::redis::status`.

## Commands

### `EVAL script numkeys key [key ...] arg [arg ...]`

Executes a Lua script server-side.

*   **Sync:** `Reply<json_value> eval(const std::string &script, const std::vector<std::string> &keys = {}, const std::vector<std::string> &args = {})`
*   **Async:** `void eval_async(const std::string &script, Callback<json_value> cb, const std::vector<std::string> &keys = {}, const std::vector<std::string> &args = {})`
*   **Note:** `numkeys` is calculated automatically from the `keys` vector size.

```cpp
// Sync EVAL
std::string script = "return {KEYS[1], ARGV[1], tonumber(ARGV[2])}";
auto reply = redis.eval(script, {"mykey"}, {"hello", "123"});

if (reply) {
    qb::redis::json_value result = reply.value();
    // result will likely be an array variant containing other json_value variants
    // Use std::get or std::visit to access underlying data.
    if (std::holds_alternative<qb::redis::json_array>(result.data)) {
        const auto& arr = std::get<qb::redis::json_array>(result.data);
        // ... process array elements ...
    }
} else {
    // Handle script error
}
```

### `EVALSHA sha1 numkeys key [key ...] arg [arg ...]`

Executes a script previously loaded into the script cache using its SHA1 hash.

*   **Sync:** `Reply<json_value> evalsha(const std::string &sha1, const std::vector<std::string> &keys = {}, const std::vector<std::string> &args = {})`
*   **Async:** `void evalsha_async(const std::string &sha1, Callback<json_value> cb, const std::vector<std::string> &keys = {}, const std::vector<std::string> &args = {})`

### `SCRIPT EXISTS script [script ...]`

Checks the existence of scripts in the script cache.

*   **Sync:** `Reply<std::vector<bool>> script_exists(const std::vector<std::string> &sha1s)`
*   **Async:** `void script_exists_async(const std::vector<std::string> &sha1s, Callback<std::vector<bool>> cb)`

### `SCRIPT FLUSH [ASYNC|SYNC]`

Removes all scripts from the script cache.

*   **Sync:** `status script_flush()`
*   **Async:** `void script_flush_async(Callback<status> cb)`
*   **Note:** `ASYNC`/`SYNC` modifier not directly exposed.

### `SCRIPT KILL`

Kills the currently executing Lua script (if it hasn't performed write operations).

*   **Sync:** `status script_kill()`
*   **Async:** `void script_kill_async(Callback<status> cb)`

### `SCRIPT LOAD script`

Loads the given Lua script into the script cache without executing it. Returns the SHA1 hash.

*   **Sync:** `Reply<std::string> script_load(std::string const &script)`
*   **Async:** `void script_load_async(std::string const &script, Callback<std::string> cb)` 