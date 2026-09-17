# Running astrolog-ephd

`astrolog-ephd` is the Astrolog ephemeris server: Swiss Ephemeris positions
over a WebSocket, for Astrolog's "Ephemeris Server" backend. This directory
is how to run it somewhere other than a developer's machine. The design is
`EPHEMERIS_SERVER_PLAN.md`, the road to production
`EPHEMERIS_SERVER_PRODUCTION_PLAN.md`; its §11 runbook lists every option.

| File | What it is |
|---|---|
| `SWISSEPH_PIN` | the Swiss Ephemeris fork commit the server is built against |
| `Dockerfile` | a container image, built by `tools/ephsrv-image.sh` |
| `astrolog-ephd.service` | a hardened systemd unit for a plain host |
| `certbot-deploy-hook` | copies a renewed certificate to the server and reloads it |

## Build

The server links the thread-safe Swiss Ephemeris fork, a separate
repository. Fetch and build the pinned commit, then the server:

```sh
sudo apt install g++ make git libssl-dev zlib1g-dev
tools/ephsrv-fork.sh /opt/swisseph
make ephsrv SWE_HOME=/opt/swisseph    # ./astrolog-ephd and ./eph_wsclient
```

Or the container image, from a commit rather than the working tree:

```sh
tools/ephsrv-image.sh                 # tags astrolog-ephd:<commit>; proves it
```

## Ephemeris data

The server answers from whatever `--ephe` names and nothing else. Every
body a chart asks for must have its file there, or that body fails with
ERROR 5. Astrolog's bundled `ephem/` (19 MB, 1800-2400) is enough for the
common charts; a full `/swe`-style set covers everything.

## A plain host with systemd

```sh
useradd --system --no-create-home --shell /usr/sbin/nologin astrolog-ephd
install -m 0755 astrolog-ephd /usr/local/bin/
install -d /var/lib/astrolog-ephd
cp -r ephem /var/lib/astrolog-ephd/ephe            # or the full set
certbot certonly --standalone -d ephem.example.org  # before the server holds 443
install -m 0755 ephsrv/deploy/certbot-deploy-hook \
  /etc/letsencrypt/renewal-hooks/deploy/astrolog-ephd
RENEWED_LINEAGE=/etc/letsencrypt/live/ephem.example.org \
  /etc/letsencrypt/renewal-hooks/deploy/astrolog-ephd   # the first copy
install -m 0644 ephsrv/deploy/astrolog-ephd.service /etc/systemd/system/
systemctl daemon-reload && systemctl enable --now astrolog-ephd
```

Renewals then run the hook themselves. certbot's standalone renewal needs
port 80 free, which this server never uses.

Options beyond the unit's go in `/etc/default/astrolog-ephd`:

```sh
EPHD_ARGS=--cells-per-sec 20000 --max-conns-per-ip 32
```

## A container

```sh
docker run -d --name ephd -p 443:47190 \
  -v /srv/ephe:/ephe:ro -v /etc/astrolog-ephd/tls:/tls:ro \
  astrolog-ephd:<commit> --port 47190 --ephe /ephe \
  --tls-cert /tls/fullchain.pem --tls-key /tls/privkey.pem
docker kill -s HUP ephd      # after a renewal
docker stop -t 15 ephd       # drains, then exits 0
```

## Operating it

- **Health**: `GET /healthz` (the process answers), `GET /readyz` (send it
  traffic: 503 with the reason while draining or with no ephemeris).
- **Metrics**: `GET /metrics`, Prometheus text. Worth alerting on:
  `/readyz` not 200; `ephd_errors_total{code="4"}` or `{code="5"}` rising;
  `ephd_refused_connections_total` and `ephd_errors_total{code="6"}` rising
  (limits biting -- raise them or find the client); the certificate's expiry,
  which the server logs at every start and reload.
- **Upgrading**: start the new binary on the same port while the old one
  drains? No -- the server refuses a port another server is listening on.
  Stop (drains, clients reconnect on their own), replace, start.
- **Logs**: stdout, one logfmt line an event -- see "Logs" below.
- **Clients**: the protocol is **version 4** (EPHEMERIS_PLUGINS_PLAN.md §3;
  `ephsrv/ephproto.h` is the codec both ends compile). The server speaks
  only 4 -- `kProtoMin` and `kProtoVersion` are both 4. An Astrolog older
  than that is answered ERROR 8 in its own layout, told to update, and stops
  retrying; a server older than the client speaks gets the client's retry
  ladder. What WELCOME advertises is what the server will do: the object
  kinds, observers, planes, forms, frames, correction masks, zodiacs,
  sidereal planes, time scales, extra columns and hypothetical bodies it
  serves, its limits, its delta T model, and its **datasetId**, which
  changes whenever an answer could and which every client cache is keyed
  on. `GET /metrics` reports errors by the version 4 code (A.19).

## Pointing Astrolog at a server

A development server on this machine is enough to cast from:

```sh
make ephsrv
./astrolog-ephd --port 8765 --bind 127.0.0.1 --ephe /swe   # or any ephemeris directory
./astrolog-qt -bS -bW ws://127.0.0.1:8765
```

or, in Calculation Settings, choose "Ephemeris Server" and set Server
Address. Three things to know:

- **`=0n` blocks it, and only an edit lifts it.** Upstream's `astrolog.as`
  ships `=0n` (no internet features), and `-0n` is a one-way lock: no
  switch, `_0n` included, can clear it once it is set, and the
  `astrolog.as` beside the program is read before any `-i` file or the
  command line. With it set, `-bS` is refused, the server is missing from
  Calculation Settings' list, and a settings file that selects the server
  casts those bodies at 0 Aries with a warning. Change the line to `_0n` in that file, and in any
  `-i` file that also sets it. The refusal and the warning both say this.
- **The first cast after startup fails soft.** It runs before the
  connection is up; the chart casts again when the server's WELCOME
  arrives. With no local ephemeris at all, startup waits for the server in
  the "Connecting to cloud ephemeris" dialog instead.
- **`-bS` selects the server and turns ephemeris files on**; a second
  `-bS` turns the server off. `=bS` always selects it, and is what a saved
  settings file carries.

## Logs

One line an event on stdout, flushed as written, so journald or the
container runtime timestamps, stores and rotates it. Every line is
[logfmt](https://brandur.org/logfmt) and starts with the same three keys:

```
ts=2026-09-17T13:55:33.870Z level=info evt=req conn=1 loop=1 addr=127.0.0.1 req=1 objs=3 rows=24 cells=72 prec=64 chunks=1 cache=miss compute_ms=5.132 total_ms=5.310 bytes=3873 stalls=0 queued=0 cache_entries=1 cache_kib=4.2 cache_evictions=0
```

`ts` is UTC to the millisecond. A value with a space, `=`, `"` or a
control character is quoted, with `\"`, `\\`, `\n` and `\t` escapes. Loki,
Vector, Grafana and `hl` read it as it is; by hand, `grep ' evt=conn.close '`
and `grep ' conn=17 '` go a long way -- `conn` is unique for the process's
life, so one connection's lines are one grep.

**Levels** (`--log-level`, default `info`; `--verbose` is `debug`):

| level | what |
|---|---|
| `error` | the server could not do something: a fatal start, a failed certificate reload, an ERROR 4 |
| `warn` | a client refused or cut off, a degraded start, a drain that cut answers off |
| `info` | lifecycle, and every connection, HELLO and REQUEST |
| `debug` | the other loops' listen lines, each ephemeris directory, backpressure stalls, the HTTP routes, per-loop drain and reload |

**Events** and their own keys. `conn loop addr` means a connection's id,
its event loop and the client's address (IPv4 written as IPv4, even from an
IPv4-mapped socket):

| evt | level | keys |
|---|---|---|
| `start` | info | `version swisseph proto_min proto pid` |
| `config` | info | every limit and setting the server is running with |
| `log.contents` | warn | `msg` -- `--log-contents` is on |
| `tls.cert` | info | `cert key expires` |
| `tokens` | info | `count file required` -- tokens are counted, never written |
| `ephe` | info (warn when none) | `path dirs` |
| `ephe.dir` | debug | `dir explicit sentinels` |
| `ephe.dropped` | warn | `dirs msg` -- over Swiss's 20-directory, 242-byte path |
| `nofile` | info (warn when below `--max-conns`) | `soft hard` |
| `listen` | info (loop 0), debug | `port scheme bind loop` |
| `ready` | info | `loops` -- every loop is listening |
| `conn.refuse` | warn | `loop addr reason msg`; reason `conn_limit` or `addr_limit` (HTTP 503) |
| `conn.open` | info | `conn loop addr scheme open` (connections open on that loop) |
| `hello` | info (a repeat: debug) | `conn loop addr client_proto proto caps build client token after_ms`; `token` is `none`, `accepted` or `unknown` |
| `hello.refuse` | warn | `conn loop addr reason`; reason `version`, `token` or `request_first` |
| `hello.timeout` | warn | `conn loop addr hello_s` |
| `req` | info | `conn loop addr req objs rows cells prec chunks cache compute_ms total_ms bytes stalls queued cache_entries cache_kib cache_evictions`; written as the last chunk goes out, so `total_ms` runs from the REQUEST's arrival to its last chunk being sent |
| `error` | warn (ERROR 4: error) | `conn loop addr req code name msg` -- every ERROR sent; a REQUEST gets either a `req` line or an `error` line |
| `stall` | debug | `conn loop addr req row rows buffered` -- an answer waiting for its client to read |
| `conn.close` | info | `conn loop addr code reason dur_s proto reqs hits cells errors bytes_out unsent_rows client`; code 1006 is a client that went without a close frame |
| `http` | debug | `path status loop addr` |
| `signal` | info | `sig` |
| `drain.start` | info | `drain_s open` |
| `drain.loop` | debug | `loop closing` |
| `drain.cut` | warn | `loop drain_s open` -- answers still unsent at the deadline |
| `tls.reload` | info | `cert key expires` |
| `tls.reload.loop` | debug | `loop` |
| `tls.reload.refused`, `tls.reload.failed` | error | `cert` or `loop`, `msg` |
| `tls.reload.none` | warn | `msg` -- SIGHUP to a plain `ws://` server |
| `exit` | info (a second signal: warn) | `status msg` |
| `fatal` | error | `msg`, and the file or port it is about |
| `ctx.alloc` | error | `loop msg` -- a Swiss context could not be made |

**What the log never says.** A token, at any level. And by default nothing
a request asked: no instant, body, star, sidereal or topocentric setting,
and not Swiss's per-row error text, which names the instant (the production
plan's decision 0.3). `--log-contents` adds those to each `req` line --
`jd step_s tt iflag bodies` and, when the request uses them, `center`,
`sid_mode sid_t0 sid_ayan`, `topo_lon topo_lat topo_elv` and `jpl` -- for
debugging a client, and warns at startup that it is on. A public server
should leave it off. The client's address and its self-reported version
string (`client`) are logged at info; say so in the service's privacy note.

**Not logged, because the server cannot see it:** a failed TLS handshake.
uSockets has no hook for one; the connection closes before any upgrade.

## What is not here yet

The client's default address still points at `localhost`; making a public
server the default is Phase 6 of the production plan, after a load test.
The unit and the hook have been checked for syntax
(`systemd-analyze verify`, `sh -n`), not run on a real host.
