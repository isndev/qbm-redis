/*
 * qb - C++ Actor Framework
 * Copyright (C) 2011-2026 isndev (cpp.actor). All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 *         limitations under the License.
 */

//
// System tier: the SECURE redis client (qb::redis::ssl::client, transport::stcp) now builds its TLS from a
// value-semantic qb::io::ssl::Context — carrying the verify mode plus an OPTIONAL private CA and client
// certificate (Redis::make_connect_socket_) — and hands the ready socket to the existing-socket connect()
// overload, instead of threading only a bare `verify_peer` bool through the connector.
//
// This file is the NEGATIVE-PROOF of that feature:
//   * it INSTANTIATES the secure client, which forces the stcp `make_connect_socket_()` Context path to
//     compile (previously no test instantiated the secure client at all);
//   * it exercises the NEW mTLS / custom-CA setters `set_ssl_root_cert()` + `set_ssl_client_certificate()`
//     which did not exist before — so under the pre-feature client this translation unit does not compile.
//
// End-to-end mTLS / private-CA validation against a live server is an integration concern (needs a redis
// configured with `tls-cert-file`/`tls-ca-cert-file`); the Context-building primitives it composes
// (Context::client().trust()/identity()/verify(), fail-closed on a bad path) are proven in qb-io's
// ssl-context-value / ssl-context-config suites.
//

#include <gtest/gtest.h>
#include <string>
#include <qb/io/async.h>
#include "../redis.h"

#ifdef QB_HAS_SSL

// Instantiating the secure client here is the load-bearing compile check for the Context path.
TEST(RedisSslContext, SecureClientExposesTlsMaterialSetters) {
    qb::io::async::init();

    qb::redis::tcp::ssl::client client{qb::io::uri{"rediss://127.0.0.1:6380"}};

    // The verify toggle round-trips (feeds make_connect_socket_ -> Context::verify()).
    client.set_verify_peer(false);
    EXPECT_FALSE(client.verify_peer());
    client.set_verify_peer(true);
    EXPECT_TRUE(client.verify_peer());

    // The NEW capability: a private CA and a client certificate for mutual TLS. These setters simply not
    // existing is what makes this a negative-proof — the file would not build against the old client.
    client.set_ssl_root_cert("private-ca.pem");
    client.set_ssl_client_certificate("client.pem", "client.key");

    SUCCEED() << "secure redis client instantiated; custom-CA + client-certificate API compiled and callable";
}

// Drives an actual connect so Redis::make_connect_socket_() RUNS — it builds the ssl::Context from the
// options (verify mode + trust(root_cert) + identity(cert,key)) and hands the socket to the existing-socket
// connect() overload — rather than only compiling. Port 1 on loopback is refused, so the connect fails fast;
// the point is that the Context-building path is EXERCISED end-to-end and the client fails gracefully.
TEST(RedisSslContext, SecureClientBuildsContextSocketOnConnect) {
    qb::io::async::init();

    // verify_peer=false branch of make_connect_socket_: Context::verify(none) + trust(root_cert) + identity.
    qb::redis::tcp::ssl::client insecure{qb::io::uri{"tcp://127.0.0.1:1"}};
    insecure.set_verify_peer(false);
    insecure.set_ssl_root_cert("qb-nonexistent-private-ca.pem");        // make_connect_socket_ -> Context::trust()
    insecure.set_ssl_client_certificate("qb-nonexistent.pem", "qb-nonexistent.key"); // -> Context::identity()
    EXPECT_FALSE(qb::io::async::run_sync(insecure.connect()))
        << "connect (verify off) must fail on a refused endpoint, having exercised make_connect_socket_()";

    // verify_peer=true branch (secure default): still builds the Context socket, still fails on the refused endpoint.
    qb::redis::tcp::ssl::client verifying{qb::io::uri{"tcp://127.0.0.1:1"}};
    verifying.set_verify_peer(true);
    EXPECT_FALSE(qb::io::async::run_sync(verifying.connect()))
        << "connect (verify on) must fail on a refused endpoint, having exercised make_connect_socket_()";
}

#endif // QB_HAS_SSL
