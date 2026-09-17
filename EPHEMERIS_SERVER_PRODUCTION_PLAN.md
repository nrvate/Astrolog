# Ephemeris server: from built to production

`astrolog-ephd` is **built**: everything `EPHEMERIS_SERVER_PLAN.md` specified
is on `qt`, the Qt client casts from it bit-exactly, and five gates hold it.
It is not **deployable**: nothing about it assumes a network the operator
does not control. This document is the plan for closing that gap. It is
written to be resumed from, like the other two.

## Status

- **2026-09-17: planned, nothing started.** Phase 0 needs the maintainer's
  decisions (below); Phase 1, TLS, can start without them.

## Where the server stands (measured against the tree, 2026-09-17)

What a production service needs and the server already has:

- Concurrency without shared mutable state: one uWS event loop per thread,
  the kernel spreading accepts across them (`SO_REUSEPORT`), one Swiss
  Ephemeris context per loop (the thread-safe fork, ts.14).
- Bounded work and memory per request: WELCOME limits, `--max-cells`
  (S4), a result cache under `--cache-mb`, a backpressure ceiling on
  queued DATA chunks, 32-bit float windows.
- Hostile-input hardening: the robustness gate, `kObjIdMax`, a parser
  fuzzed under UBSan; the fork's G25 does the same for the library.
- Refusing a port another server holds; a fatal, logged bind failure.
- Clean failure: no silent Moshier fallback, ERROR codes with Swiss's text.

What it does not have, each checked in `ephsrv/eph_srv.cpp`:

| Gap | Where it shows |
|---|---|
| No TLS | `uWS::App`, never `SSLApp`, though uSockets is built `WITH_OPENSSL=1`; the client already accepts `wss://` |
| Binds every interface | `app->listen(port)`, no host; no `--bind` |
| No signal handling | SIGTERM kills mid-stream; no drain, no close frame; no certificate reload |
| No health, readiness or metrics | the only route is `ws("/*")`; state is in `--verbose` log lines |
| Unstructured logs | `Log()` printf lines; no connection id, no per-request record |
| No per-client limits | no cap on connections per address, no rate limit, no global connection cap; anyone who reaches the port gets full service |
| No authentication | none, and no field in HELLO to carry it |
| REQUEST before HELLO accepted | `kMsgRequest` is served whether or not a HELLO arrived |
| No version negotiation | the client refuses any `protoVersion` but its own (`qtdriver.cpp` ~7359); the server ignores the client's. Protocol 3 would strand every shipped client |
| Compute on the I/O loop | a 100000-cell request (~1 s) stalls every connection on its loop |
| Not packaged or deployed | not in any release artifact or `make install`; no unit, no image; the fork is a sibling checkout, not pinned |
| Client default is `localhost` | `FUrlEphSrv()`; a user with no local files sits in the connecting dialog |

## Phase 0 -- decisions (the maintainer's)

Each changes what later phases build. A recommendation is given for each.

1. **Who may use it.** Open to any Astrolog install, or keyed?
   *Recommend:* open by default with per-address limits (Phase 3), and
   optional tokens the operator can require. A keyed-only service means
   every user configures a key, and it stops being zero-config.
2. **Where it runs.** One VPS, several regions, or a managed container
   platform? This decides whether TLS certificates come from the server
   (Let's Encrypt on the host) or a load balancer, and whether real client
   addresses arrive directly or in a proxy header.
   *Recommend:* design for both. TLS in-process (Phase 1) and a trusted-proxy
   mode (Phase 3) cost little each.
3. **What data it serves.** The bundled `ephem/` (19 MB, main files, 29
   main-belt asteroids, 39 outer bodies) or a `/swe`-sized set (887,000
   files)? Every asteroid a chart names must exist server-side, or the
   cast fails with ERROR 5.
   *Recommend:* start with the bundle plus the main files' full 13,000-year
   range, and grow it on demand.
4. **The public address.** A hostname the client can bake in as the
   default, e.g. `wss://ephem.<domain>`. Nothing ships pointing at it
   until Phase 6.
5. **Privacy.** A REQUEST carries the chart's instants, and for
   topocentric positions its latitude, longitude and altitude -- a birth
   time and place. *Recommend:* never log request contents; say so in a
   short privacy note shipped with the client (About, README).

## Phase 1 -- TLS

The server:

- `uWS::SSLApp` when `--tls-cert FILE --tls-key FILE` are given (the cert
  file carrying the chain), plain `App` otherwise. One `Conn`/behavior
  template instantiated for both (`WebSocket<SSL, ...>` is a template
  parameter, so the handlers become templates or take a small shim).
- TLS 1.2 minimum, a modern cipher list, no compression; keys read once at
  startup, the process refusing to start on an unreadable or mismatched
  pair (logged by file name).
- **SIGHUP reloads the certificate** without dropping connections, for
  Let's Encrypt's 60-90 day renewals. uSockets builds its SSL_CTX at App
  construction; the reload path is to be measured, not assumed -- either
  swapping the context's certificate in place (OpenSSL allows
  `SSL_CTX_use_certificate_chain_file` on a live context; new handshakes
  pick it up) or a graceful re-exec (Phase 2's drain). Whichever is proven.
- `--tls-only`: refuse to start with no certificate unless `--bind` is a
  loopback address, so a public plain-text listener takes intent.

The client:

- `wss://` works today through `QWebSocket`; what is missing is how
  failures read. `sslErrors` goes into the connecting dialog's error list in
  words ("certificate expired", "name does not match"), never ignored.
- Qt's TLS backend differs per platform (OpenSSL on Linux, Schannel on
  Windows, Secure Transport on macOS for Qt 6.8): each release artifact
  must actually complete a `wss://` handshake. A suite check against a
  local TLS server with a test CA, run on all three platforms by the release
  workflow.

Gates:

- `tools/ephsrv-tls.sh`: a throwaway CA and certificates made with
  `openssl` in the scratch dir; the golden gate's comparison over `wss://`
  (bit-exact, same 88 columns); refusals for an expired, wrong-name and
  untrusted certificate; a reload under a live connection that must
  survive it; `testssl.sh` (if installed) for protocol and cipher floor.
- The bench over TLS, so handshake and encryption cost are numbers.

## Phase 2 -- operability

- `--bind ADDR` (default unchanged for compatibility of the gates; the
  production unit sets it explicitly).
- **Signals.** SIGTERM/SIGINT: close the listen sockets on every loop
  (through `Loop::defer`, since signals arrive on no particular thread),
  let in-flight streams finish up to `--drain-seconds`, then send close
  1001 "going away" and exit 0. The client's reconnect ladder already
  handles the close; a gate proves a cast in flight completes and a new
  connection lands on the replacement.
- **Health and metrics**, HTTP GETs on the same listener (uWS routes
  alongside `ws("/*")`), or on `--admin-bind` when the operator wants them
  off the public port:
  - `/healthz`: the process is serving (200).
  - `/readyz`: ephemeris discovered and every loop listening; 503 while
    draining, so a load balancer stops sending first.
  - `/metrics`, Prometheus text: connections open/total, HELLOs, requests
    and cells by result, ERROR counts by code, cache hits/misses/bytes,
    request latency histogram, bytes sent, backpressure stalls, TLS
    handshake failures. Counters per loop, summed on scrape, no locks on
    the hot path.
- **Logs**: one line per event as `key=value`, with a timestamp, level,
  loop, connection id and remote address; one record per request (cells,
  milliseconds, result, cache hit) at `--log-requests`. Never instants or
  coordinates (Phase 0.5). stderr, so journald or the container runtime
  owns rotation.
- **Startup checks**: `RLIMIT_NOFILE` against the connection cap (Phase
  3), logged; the ephemeris set's sentinel files logged at info, not only
  `--verbose`.

## Phase 3 -- abuse limits and access control

- HELLO required: a REQUEST before WELCOME is refused (ERROR 1) and the
  connection closed. The Qt client always sends HELLO first; the gates'
  clients must be checked.
- A handshake deadline: a connection that has not completed HELLO within
  `--hello-seconds` (default 10) is closed. uWS already bounds the HTTP
  upgrade at 10 s.
- `--max-conns` global and `--max-conns-per-ip`, counted per loop and
  summed through atomics; over the cap, the upgrade is refused with 503
  before a WebSocket exists.
- A token bucket per connection and per address on **cells per second**,
  not requests: cost is cells, which S4 already measures. Over budget, the
  request waits (bounded queue) or gets a new ERROR 6 "rate limited" with a
  retry-after the client's ladder honors. A client that asks for an
  animation window must still fit comfortably -- sized from the client's
  own window arithmetic, recorded as numbers.
- **Trusted proxies**: `--trust-proxy CIDR` takes the client address from
  `X-Forwarded-For`/`Forwarded` only when the peer is inside the CIDR, so
  per-address limits see real clients behind a load balancer and nobody
  else can forge an address.
- **Tokens** (if Phase 0.1 wants them): HELLO gains an optional token
  string (protocol version 3, see Phase 4); `--tokens FILE` lists accepted
  tokens with per-token limits; `--require-token` refuses HELLO without
  one (new ERROR 7). The client stores the token as a setting beside the
  address, written by the settings writer like `-bW`.
- The robustness gate gains a leg per limit, each proven to fail with the
  limit disabled.

## Phase 4 -- protocol compatibility policy (before any public default)

The one phase that is cheap now and impossible later: once a release
points clients at a public server, those binaries live forever.

- **The server supports a range**, `[kProtoMin, kProtoVersion]`, and
  answers WELCOME in the version the HELLO asked for, within the range;
  outside it, ERROR 1 with text the client shows ("this Astrolog is too
  old for the server; update"). The client's exact-match check becomes
  "the server's WELCOME version is one I speak".
- **New fields only at the end of a structure, gated by version**;
  capability bits (`caps`) for optional features, as `kCapFloat32` already
  is. The rule written into `ephproto.h`'s header.
- Protocol 3 is the first to use it: the token field (Phase 3) and a
  client identity (build, platform) in HELLO for deprecation decisions,
  logged in aggregate only.
- A gate: a protocol-2 client against a protocol-3 server, bit-exact, kept
  for as long as `kProtoMin` is 2.

## Phase 5 -- packaging and deployment

- **Pin the fork.** The server build takes the Swiss Ephemeris fork at a
  tag (ts.14 or later) -- a fetch-and-build script, or a submodule under
  `ephsrv/` -- so a server binary is reproducible from one commit of this
  repo. `/shares/swisseph` stays the developer override (`SWE_HOME`).
- **Tag the fork's releases** (ts.12-ts.14 are untagged today).
- **A container image**: multi-stage, the fork and the server built in a
  builder, a small runtime image with the binary, CA certificates, a
  non-root user, and the ephemeris set as a volume (or a data image,
  Phase 0.3). `HEALTHCHECK` on `/healthz`.
- **A systemd unit** for a plain host: `DynamicUser=`,
  `ProtectSystem=strict`, `NoNewPrivileges=`, `MemoryMax=`,
  `AmbientCapabilities=CAP_NET_BIND_SERVICE` for 443, `Restart=on-failure`,
  `ExecReload=` sending SIGHUP; certificate paths under `/etc/letsencrypt`
  with a renewal hook that reloads.
- **Release**: the image built and pushed (GHCR) on a server tag, and a
  Linux x86-64 and arm64 binary attached -- by the same rule as the rest
  of CI, since operators should not need this repo's toolchain. Its own
  tag family so a client release does not redeploy a server.
- The runbook (`EPHEMERIS_SERVER_PLAN.md` §11) rewritten for operators:
  install, certificates, limits, metrics to alert on, upgrading with a
  drain, rolling the ephemeris set.

## Phase 6 -- go-live

- Deploy per Phase 0.2, behind monitoring: alert on `/readyz`, error rate,
  p99 latency, connection count near the cap, certificate expiry.
- A load test at the expected scale from outside the host: N clients
  animating concurrently over `wss://`, with p50/p99 latency and CPU
  recorded, sizing `--threads` and the limits from it.
- **Then** the client's default address becomes the public `wss://` URL,
  with `localhost` still one setting away; the privacy note ships in the
  same release; `EPHEMERIS_CLIENT_PLAN.md`'s required-server dialog is now
  meaningful to a user with no local files.
- A status line in the dialog when the server says "update" (Phase 4).

## Phase 7 -- measured, only if the load test asks

- **Compute off the event loop.** Today a request computes on its loop's
  thread; a 100000-cell request holds that loop's other connections for
  about a second. A worker pool (contexts owned by workers, loops only
  parsing and streaming, results posted back with `Loop::defer`) removes
  the head-of-line stall at the cost of a queue and a copy. Do it when the
  load test's p99 says so, not before.
- **Shared cache** across loops (today each loop has its own share), if
  hit rates under real traffic show loops missing what a neighbour has.
- **Multi-region**: stateless per connection already, so it is DNS and
  deployment, not code.

## Order and size

| Phase | Blocked on | Rough size |
|---|---|---|
| 0 decisions | the maintainer | a conversation |
| 1 TLS | nothing | server + client + one gate; the largest code phase |
| 2 operability | nothing | signals, three routes, logs; one gate |
| 3 limits | 0.1 for tokens only | limits + robust-gate legs |
| 4 compatibility | before 6 | small code, one gate, a rule written down |
| 5 packaging | 0.2, 0.3 | scripts, image, unit, release job |
| 6 go-live | 0.2-0.5, phases 1-5 | deploy, load test, one client release |
| 7 | the load test | only if measured |

Phases 1 and 2 can run in parallel on separate branches; 3 and 4 touch the
protocol together and should land as one protocol-3 change. The release
that carries TLS in the client (qt.25 or later) can ship before go-live:
it changes nothing for a user until the default address does.
