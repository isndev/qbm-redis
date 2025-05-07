# `qbm-redis`: Module Commands

This document covers Redis commands for managing Redis Modules.

Reference: [Redis Module Commands](https://redis.io/commands/?group=module)

## Common Reply Types

*   `qb::json`: For `MODULE LIST`.
*   `qb::redis::status`: For `MODULE LOAD`, `MODULE UNLOAD`.
*   `std::vector<std::string>`: For `MODULE HELP`.

## Commands

All Module commands are prefixed with `MODULE`.

### `MODULE HELP`

Shows help information about the module commands.

*   **Sync:** `std::vector<std::string> module_help()`
*   **Async:** `Derived& module_help(Func &&func)`
*   **Reply:** `Reply<std::vector<std::string>>`
*   **Example (from `test-module-commands.cpp`):**
    ```cpp
    auto help_lines = redis.module_help();
    EXPECT_FALSE(help_lines.empty());
    // Each line should be a non-empty string
    for (const auto& line : help_lines) {
        EXPECT_FALSE(line.empty());
    }
    ```

### `MODULE LIST`

Lists all loaded modules.

*   **Sync:** `qb::json module_list()`
*   **Async:** `Derived& module_list(Func &&func)`
*   **Reply:** `Reply<qb::json>` (Returns an array of objects, each describing a module)
*   **Example (from `test-module-commands.cpp`):**
    ```cpp
    auto modules = redis.module_list();
    EXPECT_TRUE(modules.is_array());
    if (!modules.empty()) {
        for (const auto& module_info : modules) {
            EXPECT_TRUE(module_info.is_object());
            EXPECT_TRUE(module_info.contains("name"));
            EXPECT_TRUE(module_info.contains("ver")); // Version
        }
    }
    ```

### `MODULE LOAD path [arg [arg ...]]`

Loads a module from the given path, optionally passing arguments to it.

*   **Sync:** `status module_load(const std::string &path, Args&&... args)` (Variadic template for args)
*   **Async:** `Derived& module_load(Func &&func, const std::string &path, Args&&... args)`
*   **Reply:** `Reply<status>`
*   **Example (from `test-module-commands.cpp`, adapted for expected failure as loading arbitrary modules in tests is unsafe/impractical):**
    ```cpp
    try {
        // Attempting to load a non-existent or invalid module path
        redis.module_load("/path/to/nonexistent/module.so");
        FAIL() << "Expected an exception for invalid module path";
    } catch (const std::exception& e) {
        std::string error = e.what();
        // Expect errors like "ERR Error loading module", "wrong number of arguments", or "unknown command"
        EXPECT_TRUE(error.find("ERR") != std::string::npos || 
                    error.find("wrong number") != std::string::npos ||
                    error.find("unknown command") != std::string::npos);
    }
    ```

### `MODULE UNLOAD name`

Unloads a module by its name.

*   **Sync:** `status module_unload(const std::string &name)`
*   **Async:** `Derived& module_unload(Func &&func, const std::string &name)`
*   **Reply:** `Reply<status>`
*   **Example (from `test-module-commands.cpp`, adapted for expected failure):**
    ```cpp
    try {
        redis.module_unload("nonexistent_module");
        FAIL() << "Expected an exception for nonexistent module";
    } catch (const std::exception& e) {
        std::string error = e.what();
        // Expect errors like "ERR Module <name> not loaded" or "unknown command"
        EXPECT_TRUE(error.find("module not loaded") != std::string::npos || 
                    error.find("unknown command") != std::string::npos ||
                    error.find("ERR") != std::string::npos);
    }
    ``` 