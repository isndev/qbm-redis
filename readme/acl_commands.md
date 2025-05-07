# `qbm-redis`: ACL Commands

This document covers Redis Access Control List (ACL) commands, which are used to manage user permissions and control access to commands and data.

Reference: [Redis ACL Commands](https://redis.io/commands/?group=acl)

## Common Reply Types

*   `qb::json`: For commands returning structured data (e.g., `ACL LIST`, `ACL GETUSER`, `ACL LOG`).
*   `std::vector<std::string>`: For commands returning simple lists of strings (e.g., `ACL CAT` without arguments, `ACL USERS`, `ACL HELP`).
*   `std::string`: For commands returning a single string value (e.g., `ACL WHOAMI`, `ACL GENPASS`).
*   `qb::redis::status`: For commands returning a simple "OK" or status (e.g., `ACL SETUSER`, `ACL LOAD`, `ACL SAVE`).
*   `qb::redis::Reply<long long>`: For commands returning an integer (e.g., `ACL DELUSER`).

## Commands

All ACL commands are prefixed with `ACL`.

### `ACL CAT [categoryname]`

Lists all command categories or all commands within a category.

*   **Sync (List Categories):** `std::vector<std::string> acl_cat()`
*   **Sync (Commands in Category):** `std::vector<std::string> acl_cat(const std::string &category)`
    *   **Note:** For Redis 7+, when a category is specified, the server might return a JSON object. The C++ API currently returns `std::vector<std::string>` for this case. For direct JSON, use the generic `command<qb::json>()` method.
*   **Async (List Categories):** `Derived& acl_cat(Func &&func)`
*   **Async (Commands in Category):** `Derived& acl_cat(Func &&func, const std::string &category)`
*   **Reply (List Categories):** `Reply<std::vector<std::string>>`
*   **Reply (Commands in Category):** `Reply<std::vector<std::string>>` (or `Reply<qb::json>` if called via generic command)
*   **Example (from `test-acl-commands.cpp`):**
    ```cpp
    // List all categories
    auto categories = redis.acl_cat();
    EXPECT_FALSE(categories.empty());

    // List commands in 'string' category (using generic command for JSON)
    auto commands_reply = redis.command<qb::json>("ACL", "CAT", "string");
    auto commands = commands_reply.result();
    EXPECT_TRUE(commands.is_object());
    ```

### `ACL DELUSER username [username ...]`

Deletes ACL users and terminates their connections.

*   **Sync:** `long long acl_deluser(const std::string &username)` (single user)
    *   **Note:** For multiple users, use the generic `command<long long>()` method with multiple username arguments.
*   **Async:** `Derived& acl_deluser(Func &&func, const std::string &username)` (single user)
*   **Reply:** `Reply<long long>` (number of users deleted)
*   **Example (from `test-acl-commands.cpp`, adapted):**
    ```cpp
    // Setup: redis.acl_setuser("tempuser", "on", ">somepassword", "+@all");
    // long long deleted_count = redis.acl_deluser("tempuser");
    // EXPECT_EQ(deleted_count, 1);
    ```

### `ACL GENPASS [bits]`

Generates a pseudorandom, secure password.

*   **Sync:** `std::string acl_genpass(std::optional<long long> bits = std::nullopt)`
*   **Async:** `Derived& acl_genpass(Func &&func, std::optional<long long> bits = std::nullopt)`
*   **Reply:** `Reply<std::string>`
*   **Example (from `test-acl-commands.cpp`):**
    ```cpp
    auto password = redis.acl_genpass();
    EXPECT_FALSE(password.empty());
    EXPECT_GT(password.length(), 8);

    auto custom_password = redis.acl_genpass(128);
    EXPECT_FALSE(custom_password.empty());
    ```

### `ACL GETUSER username`

Gets details for a specific ACL user.

*   **Sync:** `qb::json acl_getuser(const std::string &username)`
*   **Async:** `Derived& acl_getuser(Func &&func, const std::string &username)`
*   **Reply:** `Reply<qb::json>`
*   **Example (from `test-acl-commands.cpp`):**
    ```cpp
    auto user_info = redis.acl_getuser("default");
    EXPECT_TRUE(user_info.is_object());
    EXPECT_TRUE(user_info.contains("flags"));
    ```

### `ACL HELP`

Shows the help information for ACL subcommands.

*   **Sync:** `std::vector<std::string> acl_help()`
*   **Async:** `Derived& acl_help(Func &&func)`
*   **Reply:** `Reply<std::vector<std::string>>`
*   **Example (from `test-acl-commands.cpp`):**
    ```cpp
    auto help_text = redis.acl_help();
    EXPECT_FALSE(help_text.empty());
    bool found_entry = false;
    for (const auto& line : help_text) {
        if (line.find("ACL") != std::string::npos) found_entry = true;
    }
    EXPECT_TRUE(found_entry);
    ```

### `ACL LIST`

Lists all ACL rules.

*   **Sync:** `qb::json acl_list()`
*   **Async:** `Derived& acl_list(Func &&func)`
*   **Reply:** `Reply<qb::json>`
*   **Example (from `test-acl-commands.cpp`):**
    ```cpp
    // The test uses a string-based check; direct JSON parsing would be:
    auto acl_rules_json = redis.acl_list();
    EXPECT_TRUE(acl_rules_json.is_array()); // Or is_object in some Redis versions
    EXPECT_FALSE(acl_rules_json.empty());
    ```

### `ACL LOAD`

Reloads ACL rules from the ACL file.

*   **Sync:** `status acl_load()`
*   **Async:** `Derived& acl_load(Func &&func)`
*   **Reply:** `Reply<status>`
*   **Example (from `test-acl-commands.cpp`, adapted - test checks for failure on non-existent file):**
    ```cpp
    // status load_status = redis.acl_load();
    // EXPECT_TRUE(load_status.ok()); // Or check for specific error if file doesn't exist
    ```

### `ACL LOG [count | RESET]`

Shows the ACL log.

*   **Sync:** `qb::json acl_log(std::optional<long long> count = std::nullopt)`
    *   **Note:** For `ACL LOG RESET`, use the generic `command<qb::json>()` method: `redis.command<qb::json>("ACL", "LOG", "RESET")`.
*   **Async:** `Derived& acl_log(Func &&func, std::optional<long long> count = std::nullopt)`
*   **Reply:** `Reply<qb::json>`
*   **Example (from `test-acl-commands.cpp`):**
    ```cpp
    auto logs = redis.acl_log();
    EXPECT_TRUE(logs.is_array());

    auto limited_logs = redis.acl_log(5);
    EXPECT_TRUE(limited_logs.is_array());
    if (!limited_logs.empty()) {
        EXPECT_LE(limited_logs.size(), 5);
    }
    ```

### `ACL SAVE`

Saves current ACL rules to the ACL file.

*   **Sync:** `status acl_save()`
*   **Async:** `Derived& acl_save(Func &&func)`
*   **Reply:** `Reply<status>`
*   **Example (from `test-acl-commands.cpp`, adapted):**
    ```cpp
    // status save_status = redis.acl_save();
    // EXPECT_TRUE(save_status.ok());
    ```

### `ACL SETUSER username [rule [rule ...]]`

Creates or modifies an ACL user.

*   **Sync:** `status acl_setuser(const std::string &username, Args&&... rules)` (Variadic template for rules)
*   **Async:** `Derived& acl_setuser(Func &&func, const std::string &username, Args&&... rules)`
*   **Reply:** `Reply<status>`
*   **Example (from `test-acl-commands.cpp`):**
    ```cpp
    // Create a temporary user
    status set_status = redis.acl_setuser("testuser", "on", ">somepassword", "allkeys", "+@all");
    EXPECT_TRUE(set_status);
    // ... then redis.acl_deluser("testuser");
    ```

### `ACL USERS`

Lists all ACL user usernames.

*   **Sync:** `std::vector<std::string> acl_users()`
*   **Async:** `Derived& acl_users(Func &&func)`
*   **Reply:** `Reply<std::vector<std::string>>`
*   **Example (from `test-acl-commands.cpp`):**
    ```cpp
    auto users = redis.acl_users();
    EXPECT_FALSE(users.empty());
    bool found_default = false;
    for (const auto& user : users) {
        if (user == "default") found_default = true;
    }
    EXPECT_TRUE(found_default);
    ```

### `ACL WHOAMI`

Returns the username of the current connection.

*   **Sync:** `std::string acl_whoami()`
*   **Async:** `Derived& acl_whoami(Func &&func)`
*   **Reply:** `Reply<std::string>`
*   **Example (from `test-acl-commands.cpp`):**
    ```cpp
    auto current_user = redis.acl_whoami();
    EXPECT_EQ(current_user, "default");
    ``` 