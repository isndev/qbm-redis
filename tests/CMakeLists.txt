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
# -----------------------------------------------------------------------------
# qbm-redis tiered test suite (unit / system / integration / benchmark).
#   unit/        pure RESP/reply/type logic, daemon-free, parallel-safe (no lock)
#   system/      in-process loopback socket, no external daemon (reconnect UAF)
#   integration/ needs live redis:6379 — REQUIRES live (skip-not-fail), shares the
#                qb_redis_integration RESOURCE_LOCK (serialized FLUSHALL against one DB)
#   benchmark/   google-benchmark; RESP parse + reply codec (daemon-free)
# Includes resolve "../redis.h" / "../../shared/*.h" via INCLUDES=tests dir.
# -----------------------------------------------------------------------------

function(qredis_test TIER NAME RELPATH)
    cmake_parse_arguments(T "" "" "REQUIRES;EXTRA_LABELS" ${ARGN})
    qb_register_module_test(
            MODULE_NAME redis TIER ${TIER} TEST_NAME ${NAME}
            SOURCES ${RELPATH}
            INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
            LABELS module:qbm-redis tier:${TIER} ${T_EXTRA_LABELS}
            REQUIRES ${T_REQUIRES})
endfunction()

function(qredis_itest NAME RELPATH)
    cmake_parse_arguments(T "" "" "REQUIRES;EXTRA_LABELS" ${ARGN})
    qb_register_module_test(
            MODULE_NAME redis TIER integration TEST_NAME ${NAME}
            SOURCES ${RELPATH}
            INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
            LABELS module:qbm-redis tier:integration coroutine ${T_EXTRA_LABELS}
            REQUIRES live ${T_REQUIRES}
            RESOURCE_LOCK qb_redis_integration)
endfunction()

function(qredis_bench NAME RELPATH)
    qb_register_module_benchmark(
            MODULE_NAME redis BENCH_NAME ${NAME}
            SOURCES ${RELPATH}
            INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
            LABELS module:qbm-redis bench parser)
endfunction()

# UNIT — pure logic, parallel-safe, no lock
qredis_test(unit resp-codec unit/parser/resp-codec.cpp EXTRA_LABELS parser)
qredis_test(unit resp-aggregate-reserve unit/parser/resp-aggregate-reserve.cpp EXTRA_LABELS parser)
qredis_test(unit resp-buffer unit/parser/resp-buffer.cpp EXTRA_LABELS parser)
qredis_test(unit resp-types unit/parser/resp-types.cpp EXTRA_LABELS parser)
qredis_test(unit reply-parse unit/reply/reply-parse.cpp EXTRA_LABELS reply)
qredis_test(unit reply-serialize unit/reply/reply-serialize.cpp EXTRA_LABELS reply)
qredis_test(unit reply-wrapper unit/reply/reply-wrapper.cpp EXTRA_LABELS reply)
qredis_test(unit reply-extractors unit/reply/reply-extractors.cpp EXTRA_LABELS reply)
qredis_test(unit reply-json-decode unit/reply/reply-json-decode.cpp EXTRA_LABELS reply)
qredis_test(unit interval-formatting unit/types/interval-formatting.cpp EXTRA_LABELS types)
qredis_test(unit value-domain-types unit/types/value-domain-types.cpp EXTRA_LABELS types)
qredis_test(unit transaction-args-validation unit/commands/transaction-args-validation.cpp EXTRA_LABELS commands)
qredis_test(unit empty-arg-guards unit/commands/empty-arg-guards.cpp EXTRA_LABELS commands)

# SYSTEM — loopback, no daemon
qredis_test(system reconnect-lifetime-uaf system/connection/reconnect-lifetime-uaf.cpp EXTRA_LABELS coroutine)
qredis_test(system ssl-context system/connection/redis-ssl-context.cpp REQUIRES ssl EXTRA_LABELS ssl)

# INTEGRATION — live redis:6379 (REQUIRES live, RESOURCE_LOCK)
qredis_itest(connection-commands integration/connection/connection-commands.cpp)
qredis_itest(pipeline integration/connection/pipeline.cpp)
qredis_itest(reconnect-resilience integration/connection/reconnect-resilience.cpp EXTRA_LABELS slow)
qredis_itest(string-commands integration/string/string-commands.cpp)
qredis_itest(key-commands integration/key/key-commands.cpp)
qredis_itest(bitmap-commands integration/bitmap/bitmap-commands.cpp)
qredis_itest(list-commands integration/list/list-commands.cpp)
qredis_itest(hash-commands integration/hash/hash-commands.cpp)
qredis_itest(set-commands integration/set/set-commands.cpp)
qredis_itest(sorted-set-commands integration/sorted-set/sorted-set-commands.cpp)
qredis_itest(hyperloglog-commands integration/hyperloglog/hyperloglog-commands.cpp)
qredis_itest(geo-commands integration/geo/geo-commands.cpp)
qredis_itest(stream-commands integration/stream/stream-commands.cpp)
qredis_itest(transaction-multi-exec integration/transaction/transaction-multi-exec.cpp)
qredis_itest(scripting-commands integration/scripting/scripting-commands.cpp)
qredis_itest(eval-json integration/scripting/eval-json.cpp)
qredis_itest(pubsub-fanout integration/pubsub/pubsub-fanout.cpp)
qredis_itest(pubsub-subscribe integration/pubsub/pubsub-subscribe.cpp)
qredis_itest(pubsub-coconsumer-receive integration/pubsub/pubsub-coconsumer-receive.cpp)
qredis_itest(resilience integration/resilience/coro-cancel-and-connection-loss.cpp)
qredis_itest(pubsub-pipeline-desync integration/pubsub/pubsub-pipeline-desync.cpp)
qredis_itest(server-admin-commands integration/server/server-admin-commands.cpp)
qredis_itest(server-introspection integration/server/server-introspection.cpp)
qredis_itest(acl-commands integration/admin/acl-commands.cpp)
qredis_itest(cluster-commands integration/admin/cluster-commands.cpp)
qredis_itest(function-commands integration/admin/function-commands.cpp)
qredis_itest(module-commands integration/admin/module-commands.cpp)

# BENCHMARK — RESP parse + reply codec (daemon-free)
qredis_bench(resp-parse benchmark/parser/resp-parse.bench.cpp)
qredis_bench(reply-codec benchmark/parser/reply-codec.bench.cpp)
