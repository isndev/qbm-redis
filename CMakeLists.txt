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

project(qbm-redis VERSION 3.0.0)

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
        VERSION ${PROJECT_VERSION}
        DEPENDS qb-core
        SOURCES
        redis.cpp
        reply.cpp
        server_reply.cpp
        DEFINES
        QBM_REDIS_VERSION="${PROJECT_VERSION}"
        QBM_REDIS_USE_NATIVE_PARSER=1
)

# No CONFIG_DEPENDENCIES / INSTALL_CMAKE_FILES: this module's only consume-time dependency is
# qb::core, which the generated qbm-redisConfig.cmake already resolves with find_dependency(qb).
# There is no hiredis here -- QBM_REDIS_USE_NATIVE_PARSER=1 selects the in-tree RESP parser.

# No INCLUDES argument on purpose. It used to pass ${CMAKE_CURRENT_SOURCE_DIR}, which
# qb_register_module turns into a PUBLIC include directory holding a raw absolute source
# path with no $<BUILD_INTERFACE:> guard -- and that makes install(TARGETS qbm-redis EXPORT ...)
# a hard generate error ("INTERFACE_INCLUDE_DIRECTORIES property contains path ... which is
# prefixed in the source directory"). It was also unused: this module's only bare-name angle
# includes are system headers (<future>, <new>, <malloc.h>), never its own. Consumers reach
# redis by the module prefix -- <redis/redis.h> -- through the qbm/ root that
# qb_register_module adds for every module.

# Add tests if enabled
if (QB_BUILD_TESTS)
    add_subdirectory(tests)
endif ()
