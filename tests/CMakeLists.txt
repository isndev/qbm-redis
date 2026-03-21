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

include(CTest)

# Define test files and their corresponding test names
set(REDIS_TESTS
        connection-commands
        server-commands
        key-commands
        string-commands
        bitmap-commands
        list-commands
        hash-commands
        transaction-commands
        set-commands
        sorted-set-commands
        hyperloglog-commands
        geo-commands
        scripting-commands
        subscription-commands
        publish-commands
        stream-commands
        acl-commands
        function-commands
        module-commands
        cluster-commands
        json-parse
        reconnect
        parser
        pipeline
        server-api
)

# Register each test
foreach (TEST ${REDIS_TESTS})
    qb_register_module_test(
        MODULE_NAME redis
        TEST_NAME ${TEST}
        SOURCES test-${TEST}.cpp
    )
endforeach ()

# Integration tests share tcp://localhost:6379 and call FLUSHALL in the fixture.
# Without a lock, parallel ctest (-j N) runs multiple binaries at once and they
# destroy each other's data (false failures, occasional crashes).
set(_qbm_redis_ctest_names "")
foreach (TEST ${REDIS_TESTS})
    list(APPEND _qbm_redis_ctest_names "qbm-redis-test-${TEST}")
endforeach ()
set_tests_properties(${_qbm_redis_ctest_names} PROPERTIES RESOURCE_LOCK qb_redis_integration)

# Suppress nodiscard warnings for cleanup/setup co_await calls in tests
foreach (TEST ${REDIS_TESTS})
    set(test_target "qbm-redis-test-${TEST}")
    if(TARGET ${test_target})
        target_compile_options(${test_target} PRIVATE
            $<$<CXX_COMPILER_ID:Clang>:-Wno-unused-result>
            $<$<CXX_COMPILER_ID:GNU>:-Wno-unused-result>
        )
    endif()
endforeach ()