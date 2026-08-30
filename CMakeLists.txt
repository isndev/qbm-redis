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

cmake_minimum_required(VERSION 3.24)

project(qbm-redis VERSION 3.1.0)

# This module is NOT standalone-configurable, and it says so here rather than dying on whichever
# qb_* command it happens to reach first.
#
# qbm modules are built as part of the qb-dev superproject, which include()s qb's cmake/ helpers
# before add_subdirectory()ing them: this file calls qb_status_message(), qb_register_module() and
# qb_add_test(), and an INSTALLED qb ships none of those (they are development-time helpers, not
# package content -- lib/cmake/qb/ carries only qbConfig, qbConfigVersion, qbTargets and the
# Find modules). Pointing CMAKE_PREFIX_PATH at an installed qb therefore does not help, which is
# exactly the mistake this guard is here to name: without it the first symptom is
#     CMake Error at CMakeLists.txt:22 (qb_status_message):
#       Unknown CMake command "qb_status_message".
# which reads like a missing include, not like "wrong entry point".
#
# To build this module: configure the qb-dev superproject root. To get an INSTALLABLE one:
#     cmake --preset package && cmake --build --preset package
#     cmake --install build/presets/package --prefix <prefix>
# then consume it with find_package(qbm-redis CONFIG REQUIRED) -> qbm::redis.
if(NOT COMMAND qb_status_message)
    message(FATAL_ERROR
        "[qbm-redis] this module cannot be configured on its own.\n"
        "     It is built from the qb-dev superproject, which loads qb's CMake helpers "
        "(qb_status_message / qb_register_module / qb_add_test) first; an installed qb does not "
        "ship them, so CMAKE_PREFIX_PATH will not fix this.\n"
        "     Configure the superproject root instead, or use its `package` preset to produce an "
        "installable qbm-redis and consume that with find_package(qbm-redis CONFIG REQUIRED).")
endif()

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
        src/qbm/redis/redis.cpp
        src/qbm/redis/reply.cpp
        src/qbm/redis/server_reply.cpp
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
# redis by the module prefix -- <qbm/redis/redis.h> -- through this module's own src/, which
# qb_register_module adds as the include root and mirrors verbatim onto <includedir>.

# Add tests if enabled
if (QB_BUILD_TESTS)
    add_subdirectory(tests)
endif ()
