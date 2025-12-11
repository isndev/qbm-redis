#
# qb - C++ Actor Framework
# Copyright (c) 2011-2025 qb - isndev (cpp.actor). All rights reserved.
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
if(NOT QB_FOUND)
    qb_status_message("qb framework not found, skipping Redis module")
    return()
endif()

# Check if SSL support is available
if(NOT QB_HAS_SSL)
    qb_status_message("Redis module requires SSL support - skipping")
    return()
endif()

# Add the internal hiredis dependency
set(DISABLE_TESTS ON)
add_subdirectory(not-qb/hiredis)

# Register the Redis module using the new framework functions
qb_register_module(
    NAME redis
    VERSION 1.0.0
    DEPENDS qb-core hiredis
    SOURCES
        redis.cpp
        reply.cpp
    INCLUDES
        not-qb
    DEFINES
        QBM_REDIS_VERSION=1.0.0
)

target_include_directories(qbm-redis PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/not-qb)

# Add tests if enabled
if(QB_BUILD_TESTS)
    add_subdirectory(tests)
endif()