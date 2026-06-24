#
# qb - C++ Actor Framework
# Copyright (c) 2011-2026 qb - isndev (cpp.actor). All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#         http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
#         limitations under the License.
#

project(qbm-redis)

# Check if qb framework is available
if (NOT QB_FOUND)
    qb_status_message("qb framework not found, skipping Redis module")
    return()
endif ()

# TCP client builds without SSL; qb::redis::tcp::ssl::* requires QB_HAS_SSL
if (NOT QB_HAS_SSL)
    qb_status_message("Redis: building TCP-only (QB_HAS_SSL off — TLS aliases unavailable)")
endif ()

# Register the Redis module using the new framework functions
# Note: Uses the native parser instead of hiredis.
qb_register_module(
        NAME redis
        VERSION 2.0.0
        DEPENDS qb-core
        SOURCES
        redis.cpp
        reply.cpp
        server_reply.cpp
        INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}
        DEFINES
        QBM_REDIS_VERSION="2.0.0"
        QBM_REDIS_USE_NATIVE_PARSER=1
)

# Note: the include directory is already added PUBLIC via the INCLUDES argument of
# qb_register_module above; no separate target_include_directories() needed.

# Add tests if enabled
if (QB_BUILD_TESTS)
    add_subdirectory(tests)
endif ()
