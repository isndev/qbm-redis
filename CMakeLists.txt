#
# qb - C++ Actor Framework
# Copyright (C) 2011-2021 isndev (www.qbaf.io). All rights reserved.
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

add_subdirectory(not-qb/hiredis)

qb_register_module(
        NAME redis
        VERSION 1.0.0
        DEPENDENCIES hiredis
        SOURCES
        redis.cpp
        reply.cpp
)

target_include_directories(qbm-redis PUBLIC not-qb)

#target_include_directories(qbm-http PRIVATE not-qb/llhttp/include)

if (QB_BUILD_TEST)
add_subdirectory(tests)
endif()