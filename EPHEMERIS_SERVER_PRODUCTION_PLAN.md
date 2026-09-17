# Ephemeris server: from built to production

`astrolog-ephd` is **built**: everything `EPHEMERIS_SERVER_PLAN.md` specified
is on `qt`, the Qt client casts from it bit-exactly, and five gates hold it.
It is not **deployable**: nothing about it assumes a network the operator
does not control. This document is the plan for closing that gap, written
to be resumed from like the other two.

## Status

- **2026-09-17: planned, refined against the tree, nothing started.**
  Phase 0 needs the maintainer's decisions. Phase 1 (TLS) and Phase 2
  (operability) can start without them.
- Every claim about the current code below was checked in
  `ephsrv/eph_srv.cpp`, `qtdriver.cpp` or the vendored uWebSockets v20.80.0
  and uSockets on that date. Re-check before acting on one if the code has
  moved (CLAUDE.md, "Resolve findings the day they are raised").

## 1. Where the server stands

Already production-shaped:

- Concurrency without shared mutable state: one uWS event loop per thread,
  the kernel spreading accepts across them (`SO_REUSEPORT`), one Swiss
  Ephemeris context per loop (the thread-safe fork, ts.14).
- Bounded work and memory per request: WELCOME limits, `--max-cells` (S4),
  a result cache under `--cache-mb`, a backpressure ceiling on queued DATA
  chunks, f32 windows.
- Hostile input: the robustness gate, `kObjIdMax`, the parser fuzzed under
  UBSan in the review; the fork's G25 fuzzes the library the same way.
- Refusing a port another server holds; a fatal, logged bind failure.
- Clean failure: no silent Moshier fallback, ERROR codes with Swiss's text.
- No ambient credentials, so a web page opening a WebSocket to it gains
  nothing a direct client would not have (cross-site WebSocket hijacking
  needs cookies or similar to steal; keep it that way -- tokens go in HELLO,
  never in cookies).

The gaps:

| # | Gap | Where it shows |
|---|---|---|
| G1 | No TLS | `uWS::App`, never `SSLApp`, though uSockets is built `WITH_OPENSSL=1` |
| G2 | Binds every interface | `app->listen(port)` with no host; no `--bind` |
| G3 | No signal handling | SIGTERM kills mid-stream; no drain, no close frame, no certificate reload |
| G4 | No health, readiness or metrics | the only route is `ws("/*")` |
| G5 | Unstructured logs, and they leak | `Log()` printf lines with no connection id; the ERROR line (`eph_srv.cpp` ~595) prints Swiss's serr text, which carries the requested instant ("jd 2378490.500000 < lower limit ...") |
| G6 | No per-client limits | no cap on connections, per address or total; no rate limit |
| G7 | No authentication | none, and no field in HELLO to carry it |
| G8 | REQUEST before HELLO is served | `kMsgRequest` handled regardless |
| G9 | No version negotiation | the server ignores HELLO's `protoVersion`; the client treats any other WELCOME version as a refusal and **retries it on the ladder** for an hour (`qtdriver.cpp` ~7359) -- a permanent condition presented as a transient one |
| G10 | Compute on the I/O loop | a request computes on its loop's thread; at the bench's cold rate (647 ms per 30,000 cells, ~46k cells/s) a full 100,000-cell request holds every other connection on that loop for 1-2 s |
| G11 | Client TLS failures unexplained | no `sslErrors` handler; a bad certificate surfaces as the socket's generic error string |
| G12 | Client port default wrong for TLS | `FUrlEphSrv()` applies `kDefaultPort` (47190) to any URL without a port, `wss://` included; a public TLS service belongs on 443 |
| G13 | TLS runtime not guaranteed in packages | Qt 6 loads TLS as a plugin (`plugins/tls/`); `qt_windows_dist_audit.py` and the macOS packaging check platform plugins only. A Linux source build against Qt 5 needs OpenSSL at runtime, and nothing checks `QSslSocket::supportsSsl()` |
| G14 | The gates cannot speak TLS | `eph_wsclient` is a raw-socket client; the Python in the bench has no `websockets` module |
| G15 | Not packaged or deployed | no release artifact, no `make install`, no unit, no image; the fork is an unpinned sibling checkout (and ts.12-ts.14 are untagged) |
| G16 | Client default is `localhost` | `FUrlEphSrv()`; a user with no local files sits in the connecting dialog |

## 2. Phase 0 -- decisions (the maintainer's)

Each changes what later phases build. A recommendation for each.

1. **Who may use it.** Open to any Astrolog install, or keyed?
   *Recommend:* open, with per-address limits (Phase 3), and optional tokens
   an operator can require. Keyed-only ends zero configuration.
2. **Where it runs.** One VPS, several regions, or a managed platform
   behind a load balancer? This decides three things: who holds the
   certificate (the server, or the balancer), how real client addresses
   arrive (directly, `X-Forwarded-For`, or the PROXY protocol), and whether
   the server ever sees TLS at all.
   *Recommend:* build TLS in-process (Phase 1) and a trusted-proxy mode
   (Phase 3) both; each is small, and the choice of host stays reversible.
3. **What data it serves.** Measured: the bundled `ephem/` is 19 MB
   (1800-2400 main files, 29 main-belt asteroids, 39 outer bodies); the
   main files' full 13,000-year range from `/swe` (`sepl*`, `semo*`,
   `seas*`) is 105 MB; `/swe` whole is about 887,000 files. A chart naming
   an asteroid the server lacks fails that object with ERROR 5.
   *Recommend:* full-range main files plus the bundle's asteroids (~125 MB)
   to start; add asteroids on demand.
4. **The public address.** A hostname, e.g. `wss://ephem.<domain>` on 443.
   Nothing ships pointing at it until Phase 6.
5. **Privacy.** A REQUEST carries the chart's instants and, for topocentric
   positions, latitude, longitude and altitude: a birth time and place.
   *Recommend:* the server never logs request contents or Swiss error text
   (G5), keeps no per-request record beyond counts, and the client ships a
   short privacy note (About, README) in the release that makes the public
   server its default.

## 3. Phase 1 -- TLS

**Server.**

- `uWS::SSLApp` when `--tls-cert FILE` and `--tls-key FILE` are given (the
  certificate file carrying the chain), plain `App` otherwise. The socket
  type is a template parameter (`WebSocket<SSL, true, Conn>`), so the
  handlers in `SetupLoop()`, `OnMessage()`, `FlushStreams()` and the send
  helpers become templates over `SSL`; the plain path must stay
  byte-identical (the golden gate over `ws://` is the check).
- What uSockets already does, verified in `crypto/openssl.c`: TLS 1.2
  minimum (`SSL_CTX_set_min_proto_version`), TLS 1.3 allowed. What it does
  not: its default cipher list is applied only with DH parameters, so the
  server passes `ssl_ciphers` explicitly (ECDHE with AES-GCM or
  ChaCha20-Poly1305 for 1.2; 1.3's suites are OpenSSL's defaults).
- Startup refuses an unreadable, mismatched or expired pair, naming the
  file and the certificate's `notAfter`, and logs the expiry date on every
  start.
- **Reload on SIGHUP, in place.** Each loop owns its own `SSLApp`, so its
  own `SSL_CTX`, reachable through `app->getNativeHandle()`. The signal
  thread (Phase 2) posts to every loop with `Loop::defer`; each loop loads
  the new chain and key into its live context
  (`SSL_CTX_use_certificate_chain_file`, `SSL_CTX_use_PrivateKey_file`,
  `SSL_CTX_check_private_key`), and on any failure keeps the old pair and
  logs why. New handshakes get the new certificate; established
  connections are untouched. That OpenSSL behaves this way on a context
  with live connections is to be **proven by the gate**, not assumed; the
  fallback is Phase 2's drain plus a supervised restart.
- `--allow-plaintext-public`: without it, a server with no certificate
  refuses to start unless `--bind` is a loopback address, so a public
  plain-text listener takes intent. (Gates bind loopback, so they are
  unaffected.)
- SNI (`addServerName`) is available if one process ever serves several
  names; not needed for one hostname.

**Client** (`qtdriver.cpp`).

- `FUrlEphSrv()`: default port 443 for `wss://`, 47190 for `ws://` and a
  bare host (G12). The empty setting stays `localhost` until Phase 6.
- `QWebSocket::sslErrors` connected: each error in words in the connecting
  dialog and the About status line ("certificate expired on ...", "issued
  for X, not Y", "not trusted"), and never `ignoreSslErrors()`.
- When `QSslSocket::supportsSsl()` is false and the address is `wss://`,
  say so ("this build of Astrolog has no TLS support: install OpenSSL")
  rather than failing a handshake (G13).

**Packaging** (G13).

- `tools/qt_windows_dist_audit.py` requires a TLS backend plugin
  (`tls/qschannelbackend.dll`, or the OpenSSL one with its DLLs).
- The macOS package check requires `PlugIns/tls/`.
- The suite gains a group that completes a `wss://` handshake and a cast
  against a local TLS server with a test CA, so each release platform's
  run proves its own TLS stack. It skips, with a reason, where the server
  is not built -- which on the Windows and macOS runners means the group
  needs a TLS endpoint of its own; the cheapest honest one is a tiny
  `QSslServer`-based echo of WELCOME, which proves the TLS stack and not the
  server. Decide when building it.
- `tools/build-check.sh` adds `QSslSocket::supportsSsl()` to what each
  distribution's build must report.

**Gates** (G14).

- `eph_wsclient --tls [--ca FILE] [--sni NAME]`, linking the OpenSSL the
  server already links. Then `ephsrv-golden.sh` runs over `ws://` and
  `wss://` both, bit-exact, the same 88 columns.
- `tools/ephsrv-tls.sh`: a throwaway CA and certificates made with
  `openssl` in the scratch dir; refusals for an expired, wrong-name and
  untrusted certificate (client side) and a bad key pair (server start);
  a SIGHUP reload under a live, streaming connection that must survive
  while a new connection sees the new certificate; `testssl.sh`, when
  installed, for the protocol and cipher floor.
- The bench over TLS, so handshake and encryption cost are numbers in
  the server plan's §9 table.

## 4. Phase 2 -- operability

- `--bind ADDR` (IPv4 or IPv6; default all, as today, so the gates do not
  change).
- **Signals.** Handlers do nothing but hand off: a dedicated thread blocks
  in `sigwait()` on SIGTERM, SIGINT and SIGHUP (masked in every other
  thread before the loops start), then acts through `Loop::defer`, which
  is the one uWS call safe from another thread.
  - SIGTERM/SIGINT: each loop closes its listen socket
    (`us_listen_socket_close`) and stops accepting; `/readyz` turns 503;
    streams in flight finish, up to `--drain-seconds` (default 10); then
    every connection is closed with 1001 "going away" and the process
    exits 0 once the loops return. A second signal exits at once.
  - SIGHUP: certificate reload (Phase 1).
  - The client already reconnects on close; the gate proves a streaming
    window completes during a drain and a new connection lands on a
    replacement server.
- **Health and metrics**, HTTP GETs registered beside `ws("/*")`, or on
  `--admin-bind ADDR:PORT` (a second plain `App` on one loop) when the
  operator wants them off the public listener:
  - `/healthz`: 200 while the process runs.
  - `/readyz`: 200 once ephemeris discovery succeeded and every loop is
    listening; 503 while draining, so a balancer stops routing first.
  - `/metrics`, Prometheus text: connections open and total; HELLOs;
    requests and cells by outcome; ERRORs by code; cache hits, misses and
    bytes; request latency histogram; bytes sent; backpressure stalls; TLS
    handshake failures; build and fork version as labels. Counters per
    loop (no locks on the hot path), summed on scrape through `defer`.
- **Logs**: one `key=value` line per event with timestamp, level, loop,
  connection id and remote address; a per-request line (cells,
  milliseconds, outcome, cache hit) only with `--log-requests`. **Never**
  instants, coordinates, star names or Swiss's serr text: the ERROR line
  at ~595 logs the code and a fixed description, and the text goes only to
  the client (G5). stderr, so journald or the container runtime rotates.
- **Startup**: `RLIMIT_NOFILE` checked against `--max-conns` (Phase 3) and
  logged; the discovered ephemeris set's sentinel files logged at info.

## 5. Phase 3 -- limits and access control

- **HELLO first** (G8): a REQUEST before WELCOME is refused with ERROR 1
  and the connection closed. The Qt client and `eph_wsclient` already
  send HELLO first (checked); the robustness gate gains the refusal.
- **Handshake deadline**: no HELLO within `--hello-seconds` (default 10)
  closes the connection. uWS already bounds the HTTP upgrade at 10 s
  (`HTTP_IDLE_TIMEOUT_S`).
- **Connection caps**: `--max-conns` total and `--max-conns-per-ip`,
  counted in atomics shared by the loops, enforced in the WebSocket
  `upgrade` handler, which answers 503 before a WebSocket exists.
- **Rate limit on cells, not requests**, since cells are the cost S4
  already measures. A token bucket per address: burst of `--max-cells`
  (one full request), refilling at `--cells-per-sec`. Sizing, from numbers
  on record: a loop computes ~46k cells/s cold and serves hot windows at
  hundreds per second; the Qt client's largest routine window is
  64 objects x 1000 rows = 64,000 cells, asked once per 1000 animation
  frames; a still chart is a few hundred cells. A default near
  10,000 cells/s per address leaves an animating client far inside its
  budget and caps one address at a fifth of a loop's cold capacity. Over
  budget: a new ERROR 6 "rate limited" with a retry-after in the text,
  which the client treats as a failed window (asked again exactly), not a
  refusal. Final numbers come from the Phase 6 load test.
- **Real client addresses behind a proxy**, two mechanisms, both fenced
  by `--trust-proxy CIDR` (the peer's own address must be inside it):
  - HTTP balancers: `X-Forwarded-For` / `Forwarded`, read in the `upgrade`
    handler, rightmost untrusted hop.
  - TCP balancers: the PROXY protocol v2, which uWS supports only as a
    compile-time switch (`UWS_WITH_PROXY`) and then **accepts from any
    peer**. So: build with it, and in `upgrade` use
    `getProxiedRemoteAddress()` only when `getRemoteAddress()` is inside
    the trusted CIDR, else the peer address. What uWS does with a direct
    connection that sends no PROXY header under that build is to be
    measured before relying on it.
- **Tokens** (if 0.1 wants them): HELLO gains an optional token (protocol
  3, Phase 4); `--tokens FILE` lists accepted tokens with per-token
  budgets replacing the per-address ones; `--require-token` refuses HELLO
  without one (new ERROR 7). The client stores the token as a setting
  beside the address, through the settings writer like `-bW`, and the
  settings sweeps cover it.
- Every limit gets a robustness-gate leg, proven to fail with the limit
  switched off.

## 6. Phase 4 -- protocol compatibility (before any public default)

Cheap now, impossible later: once a release points clients at a public
server, those binaries stay in use for years.

- **The server speaks a range**, `[kProtoMin, kProtoVersion]`, reads the
  HELLO's `protoVersion` (ignored today, G9), and answers WELCOME in the
  highest version both speak. Below `kProtoMin`, ERROR 1 with text the
  client can show.
- **The client's mismatch becomes terminal** (G9): "this Astrolog is too
  old for the server at X; update" in the dialog and the About line, and
  no retry ladder for a condition time will not fix. The required-server
  dialog ends with it rather than cycling for an hour.
- **Layout rule**, written into `ephproto.h`'s header: new fields only at
  the end of a struct and only under a version check; optional behaviour
  behind `caps` bits, as `kCapFloat32` already is. Existing fields are
  never reinterpreted.
- **Protocol 3** is the first under the rule: the optional token (Phase 3),
  ERRORs 6 and 7, and a client identity in HELLO -- the Qt client sends
  `build` and `caps` as 0 today -- so deprecation can be decided from
  aggregate counts (never per-connection logs).
- A gate: a protocol-2 client (`eph_wsclient --proto 2`) against a
  protocol-3 server, bit-exact, for as long as `kProtoMin` is 2.

## 7. Phase 5 -- packaging and deployment

- **Pin the fork**: tag ts.14 (and tag every fork release after), and give
  the server build a fetch-and-build step for that tag, so a server binary
  is reproducible from one commit of this repo. `SWE_HOME` stays the
  developer override pointing at `/shares/swisseph`.
- **A container image**: multi-stage -- the fork and the server built in a
  builder; a small runtime image with the binary, CA certificates and a
  non-root user; the ephemeris set (0.3) as a volume or a separate data
  image; `HEALTHCHECK` on `/healthz`; `STOPSIGNAL SIGTERM` and a stop
  timeout above `--drain-seconds`.
- **A systemd unit** for a plain host: `DynamicUser=yes`,
  `ProtectSystem=strict`, `ProtectHome=yes`, `NoNewPrivileges=yes`,
  `MemoryMax=`, `LimitNOFILE=`, `AmbientCapabilities=CAP_NET_BIND_SERVICE`
  for 443, `Restart=on-failure`, `ExecReload=kill -HUP $MAINPID`, and a
  certbot deploy hook that reloads it.
- **Release**: on a server tag of its own family (so a client release
  never redeploys a server), the image pushed to GHCR and Linux x86-64 and
  arm64 binaries attached. Same rule as the rest of CI: operators should
  not need this repo's toolchain. The fork build inside it runs the fork's
  own `make check`.
- **Keeping the vendored network stack current**: uWebSockets and uSockets
  are pinned (v20.80.0); record where their security advisories are
  watched and how an update is proven (the five gates plus Phase 1's and
  3's). OpenSSL comes from the image's base and updates with it.
- The runbook (`EPHEMERIS_SERVER_PLAN.md` §11) rewritten for operators:
  install, certificates and renewal, limits, what to alert on, upgrading
  through a drain, changing the ephemeris set.

## 8. Phase 6 -- go-live

- Deploy per 0.2 with monitoring: alert on `/readyz`, ERROR rate, p99
  latency, connections near the cap, rate-limit refusals, certificate
  expiry under 14 days.
- **Load test** from outside the host over `wss://`: N clients animating
  (64 x 1000 windows, f32) plus M casting still charts, recording p50/p99
  latency, CPU and memory, and sizing `--threads`, `--max-conns` and
  `--cells-per-sec` from it. This is also Phase 7's measurement.
- **Then one client release** makes the public `wss://` URL the default
  address, with `localhost` a setting away, the privacy note (0.5), and
  the terminal "update" message (Phase 4). From that release on, the
  required-server dialog means something to a user with no local files.

## 9. Phase 7 -- only if measured

- **Compute off the event loop** (G10): workers own the contexts; loops
  parse, queue and stream; results post back with `Loop::defer`. Removes
  the 1-2 s head-of-line stall at the cost of a queue and a copy. Do it
  when the load test's p99 says so.
- **A cache shared across loops**, if hit rates under real traffic show
  loops missing what a neighbour computed.
- **Multi-region**: stateless per connection already, so DNS and
  deployment, not code.

## 10. Order and size

| Phase | Blocked on | Lands as |
|---|---|---|
| 0 decisions | the maintainer | a conversation |
| 1 TLS | nothing | server + client + `eph_wsclient --tls` + TLS gate; the largest code phase |
| 2 operability | nothing | signals, three routes, logs; a drain gate |
| 3 limits | 0.1 for tokens only | limits + robustness-gate legs |
| 4 compatibility | must precede 6 | small code, a rule, one gate |
| 5 packaging | 0.2, 0.3 | scripts, image, unit, release job |
| 6 go-live | 0.2-0.5, phases 1-5 | deploy, load test, one client release |
| 7 | the load test | only if measured |

Phases 1 and 2 are independent and can be separate branches. Phases 3 and
4 change the protocol together and should land as one protocol-3 branch.
A client release carrying Phase 1's client side (wss port, certificate
errors, TLS packaging) can ship before go-live: it changes nothing for a
user until the default address does.
