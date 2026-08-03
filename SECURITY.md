<!-- Verified-against: qbm-redis @ qb 3.0.0 (C++20 default, C++23 supported) -->

# Security policy

qbm-redis is a module of the qb actor framework. Vulnerability reporting and disclosure follow the
framework's process — see the qb [SECURITY policy](https://github.com/isndev/qb/blob/main/SECURITY.md). **Do not report security issues
through public GitHub issues, pull requests, or discussions.**

## Supported versions

Security fixes target the module version that ships with the supported qb framework release (the `2.6.x`
line). See the framework policy for details.

## Module attack surface

qbm-redis parses untrusted bytes from a Redis server connection. When reporting, identify the affected
component:

- RESP reply decoding (the parser, type transforms, the `SCAN` cursor).
- Length and bounds handling in the read buffer (`ViewBuffer`).
- The pub/sub message dispatch path and the command/reply correlation FIFO.
- The auto-reconnect connect path and connection lifecycle.

The module fails closed on hostile input: handler exceptions are contained at the `noexcept` `onMessage`
boundary, a throwing pub/sub callback cannot desynchronize the reply FIFO, `ViewBuffer` length bounds are
overflow-safe, a corrupt RESP terminator faults the connection, and numeric parsing rejects trailing
garbage. Report any case where these protections can be bypassed, or where a malicious server response
reaches an unsafe path.

Connect only to a trusted Redis server over a trusted network or TLS, and treat the server as part of your
trust boundary.
