# Redis benchmark instruments

`resp-parse` and `reply-codec` exercise the decoder and typed codec. `client-roundtrip` adds a complete
client path through the real `qb::redis::tcp::client` callback GET API, TCP loopback, RESP decoder and reply FIFO.
It uses an ephemeral IPv4 loopback listener and an in-process nonblocking peer on the same thread. No Redis
daemon, server thread, authentication, TLS, retry policy or external database is involved.

## Work and roster

Target/binary: `qbm-redis-bench-client-roundtrip`. One Google Benchmark iteration issues **16 pipelined GETs per
persistent connection** and waits for all callbacks and peer writes. Four cells use `sessions` = 1 or 4 and
`response_bytes` = 32 or 256. The public callback return type is `Reply<std::optional<std::string>>`; every
response is a non-nil RESP bulk string. Keys and payloads contain a fresh batch epoch, connection identity and
command ordinal, so an earlier or reordered result cannot satisfy the exact-payload check.

The fake peer is a fixed workload responder, not a Redis server implementation: it checks the independently
spelled RESP2 GET array and complete key bytes before allowing that response to leave. It does not use qbm's
serializer, parser or reply converter to decide what should have been sent. Partial reads and writes retain
cursors. Unexpected command bytes, early close, transport/decode errors and a two-second setup/batch deadline
produce `SkipWithError`, not a valid measurement. All sockets are RAII-owned; clients are destroyed while their
callback slots exist, then the benchmark-owned thread-local listener is cleared. There is no service to stop
or server thread to join.

## What is timed

Connection setup, literal request/response preparation and result-slot reset are outside timing. The measured
region contains command admission/serialization, one callback handler per GET, the real async loop and kernel
loopback, peer reads/request matching/response writes, and callback result retention. Deadline clock checks and
fixture progress checks are included. The peer's response strings are prepared in advance, but its socket work
and request validation are measured. This is explicitly **client + loopback + fixture**, not pure client CPU.

Every batch is validated while timing is paused: exact request bytes consumed, all 16 commands and responses
per connection, exact one callback for each slot, `ok()` and non-nil payload, byte-exact payload identity,
checksum and zero `pending_reply_count()`. A complete preflight and fresh-identity terminal batch are also
excluded from timing. The same exchange driver runs in preflight and measured batches.

`UseRealTime()` reports wall-clock **nanoseconds per batch**, not an individual round-trip latency percentile.
`items_per_second` counts completed GETs; `bytes_per_second` counts response VALUE payload bytes, excluding
RESP framing and request bytes. The scalar invariants `commands_per_batch`, `value_bytes_per_batch` and
`connections` describe the validated work (not rates); collection must match them to each cell before using
its timing. A write is bounded to 16 × (256 + 8 RESP framing) = 4224 bytes per peer; the native int length
conversion is guarded by that bound, and even the four-peer total is statically below INT_MAX.
No baseline or performance claim is created by adding this instrument.

## `oneway-batch`: the same SETs with and without their replies (Huly QB-902)

Target/binary: `qbm-redis-bench-oneway-batch`, the same harness shape as `client-roundtrip`: one persistent
connection, an in-process nonblocking peer on the same thread, no Redis daemon. One iteration sends N SETs
(`commands` = 16 or 256, a 32-byte value) and waits for their completion. `oneway = 0` issues N typed
`set(callback, key, value)`; the peer answers N `+OK`. `oneway = 1` adds the N SETs to a `qb::redis::oneway_batch`
and calls `send_oneway`; the peer answers the single `+OK` of the batch's `CLIENT REPLY ON`. The serialization of the
commands is inside the timed region in both modes. The peer matches every byte against independently spelled RESP
(`CLIENT REPLY OFF`, each SET, `CLIENT REPLY ON`) and sends a reply only once the request it answers is matched,
everything unlocked in one send in both modes. Outside timing, after every batch: the exact wire bytes, one callback
per SET or one per batch and each `ok()`, an empty reply FIFO, and the `stats().oneway` delta.
`allocations_per_command` counts the process's global `operator new` calls (this binary replaces it) during one more
batch run after the measured loop, outside timing, divided by N. `items_per_second` counts SETs. It is client +
loopback + fixture, like `client-roundtrip`, and no baseline or performance claim is created by adding it.

## Build and smoke

From the qb-dev superproject, using an isolated build directory:

```sh
cmake --preset benchmarks
cmake --build --preset benchmarks --target qbm-redis-bench-client-roundtrip
build/presets/benchmarks/bin/benchmarks/qbm-redis-bench-client-roundtrip \
  --benchmark_filter=BM_RedisClient_GetBatch \
  --benchmark_min_time=1x --benchmark_repetitions=1 \
  --benchmark_out=redis-client-smoke.json --benchmark_out_format=json
```

Require exactly four nonempty cells, each with one iteration, no `error_occurred`, the expected label and the
four distinct dimension pairs. Google Benchmark may exit zero after `SkipWithError`; inspect the JSON too.
A zero-duration one-iteration smoke is a correctness run, never a performance result. Keep an external process
watchdog (for example the integrator's 30-second runner); the in-process deadline cannot recover a hung library
callback or event-loop dispatch.

For ASan+UBSan, TSan and coverage, enable `QB_BUILD_BENCHMARKS=ON` explicitly in the corresponding isolated
preset build, then build this target and run the same four-cell smoke. These are benchmark cases, not ctest
registrations. Real collection uses a quiet admitted host, separate sequential processes, A/A, physical binary
copies, seeded order and distributions under the maintainer's performance protocol. macOS/no-pin or loaded
host observations remain diagnostic. Windows/MSVC collection and calibration are a separate prerequisite;
this instrument does not supply them.
