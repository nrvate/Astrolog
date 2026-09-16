# Ephemeris Server Plan

Astrolog's ephemeris positions come from data files on local disk (Swiss
Ephemeris `-bs`/`-b`, JPL files `-bj`) or live web queries (JPL Horizons
`-bJ`, one body per HTTP fetch with a 64-entry reply cache). This plan adds a
fourth source: a WebSocket ephemeris service, so a client with no local
ephemeris files at all computes full charts, and animation (25ms frames on
10 minute steps) runs off a single prefetched window instead of per-frame
requests.

Part I is the server plan and is the authority for it. Part II is the client
specification: complete enough to implement on its own, scheduled after the
server. Both parts were designed together; the client section records every
decision made during that design so a future agent need not re-derive it.

Work log entries append at the end of this file, newest last. Nothing here
modifies QT_GUI_PLAN.md, which keeps its own complete work log.

---

# Part I — Server plan

## 1. Goals

- A long-lived WebSocket service serving Swiss Ephemeris positions: planets,
  planetary moons, main and numbered asteroids, fictitious (seorbel.txt)
  bodies, and fixed stars — everything the configured ephemeris tree
  actually contains.
- Batching by design: one request covers N objects × a time range, returned
  as packed columns the client interpolates from. One round trip fuels
  minutes of animation.
- Multi-core concurrent service of many clients, stable tail latency.
- Zero configuration beyond what Astrolog already has: if the user has an
  ephemeris path configured, the server finds it and serves it.
- Plays nice with a nearly-million-file asteroid tree: O(1) startup, lazy
  file loads, fd use bounded by the context pool.
- Lives in this repo; easy to build and run (`make ephsrv`).

## 2. Design decisions

| Decision | Choice | Why |
|---|---|---|
| Transport | WebSocket only, one long-lived connection for the app's lifetime | Door to future interactive services; no per-request connection churn. |
| Server stack | uWebSockets + uSockets, vendored | Epoll-based, WebSocket-native, top-tier throughput, one binary. |
| Payload | Packed binary, 16-byte envelope, fixed layouts | Dense float arrays; zero-copy decode; ~50-line client parser. |
| Protobuf | Rejected for the ephemeris channel | Codegen + libprotobuf in a hand-rolled codebase for rectangular float arrays it adds nothing to. The envelope's `type` field keeps the door open for protobuf or a JSON sidecar if a future service (e.g. AI) brings many cross-language message types. |
| zstd | Deferred; flag reserved in the envelope | Packed f64 ephemeris compresses ~10-20% at best; float32 mode is the bigger lever. Enable only if profiling dense batches shows a net win. |
| SWE engine | The thread-safe fork at /shares/swisseph | Real context API (see §5); the repo's vendored upstream 2.10.03 stays untouched for the swetest-oracle tooling. |
| Missing data | Hard error (fork default), no Moshier fallback | A cloud ephemeris client wants real data or a clean failure, never a silent formula downgrade. |
| Id space | Server speaks raw SWE ids; all Astrolog-object mapping stays client-side | The client already owns that machinery (FSwissFromObj, rgTypSwiss); the server stays engine-free. |
| Positions returned | SWE-native (tropical by default, sidereal when SEFLG_SIDEREAL) | Astrolog's own rZodiacOffset arithmetic layers on top client-side; the server must not duplicate it. |

## 3. Dependencies and vendoring

Vendored under `ephsrv/`, cgif-style (license files kept, sources pinned):

- `ephsrv/uWebSockets/` — uWebSockets **v20.80.0**, commit
  `3ffd6f44c9c3c92c96345d9f96bd01ba9c025ab5`, Apache-2.0 (`LICENSE`).
  Header-only; includes are flat in this release (`#include <App.h>`).
  No client implementation exists in uWS (ClientApp.h is a placeholder) —
  test/bench clients use their own minimal raw-socket WebSocket client.
- `ephsrv/uSockets/` — the uSockets snapshot pinned by v20.80.0, commit
  `86097c490263ab662d62e8e7b541390bdec7d149`, Apache-2.0 (`LICENSE`).
  Built with OpenSSL (`make WITH_OPENSSL=1`) as `ephsrv/uSockets/uSockets.a`.
- Swiss Ephemeris thread-safe fork at /shares/swisseph, version
  `2.10.03-ts.10`: link `libswe.a` (or `.so`). NOT vendored into this repo —
  it is a sibling project with its own build, tests, and release cadence.

Proven compile command (from repo root):

    g++ -std=gnu++17 -O2 -I ephsrv/uWebSockets/src -I ephsrv/uSockets/src \
      ephsrv/eph_srv.cpp ephsrv/uSockets/uSockets.a \
      -L/shares/swisseph -lswe -lssl -lcrypto -lz -lpthread -o astrolog-ephd

uWS v20.80.0 API note: server-side WebSocket handlers are
`WebSocket<SSL, true, UserData>` — the second template parameter is
`isServer`, counterintuitively `true` on the server side.

## 4. Protocol v1

Shared header `ephsrv/ephproto.h`, compiled into both ends (server and,
later, the Qt client). All integers little-endian, structs packed, no
padding; every layout below is exhaustive.

### 4.1 Envelope (16 bytes)

    offset size field
    0      u16  magic = 0x1EF0
    2      u8   protoVersion = 1
    3      u8   flags: bit0 zstd, bit1 float32, bit2-7 reserved (0)
    4      u16  type
    6      u16  reserved (0)
    8      u32  requestId
    12     u32  payloadLen (bytes)

Types 1-7 are ephemeris service messages and are never reused. Types 8+ are
reserved for future non-ephemeris services sharing the connection.

| Type | Name | Direction | Meaning |
|---|---|---|---|
| 1 | HELLO | C→S | First message after connect |
| 2 | WELCOME | S→C | Server identity and limits |
| 3 | REQUEST | C→S | Ephemeris batch request |
| 4 | DATA | S→C | One chunk of results |
| 5 | ERROR | S→C | Whole-request error |
| 6 | PING | both | Heartbeat |
| 7 | PONG | both | Heartbeat reply |

### 4.2 HELLO payload

    u32 protoVersion
    u32 caps (bit0 float32, bit1 zstd)
    u32 build (client build id)
    sz  version string (NUL-terminated)

### 4.3 WELCOME payload

    u32 protoVersion
    u32 caps (same bit meanings)
    u32 swissephVersion (packed major*10000+minor*100+patch)
    u32 maxObjs    (objects per request)
    u32 maxRows    (rows per request)
    u32 maxChunkRows (rows per DATA chunk)
    u32 maxPayload (bytes)
    sz  serverVersion string

### 4.4 REQUEST payload

    u32 nObj
    nObj object records:
      u8 kind: 0 = body by id, 1 = fixed star by name
      kind 0: u32 id   (SWE id: planet, moon incl. SE_PLMOON_OFFSET,
                        asteroid incl. SE_AST_OFFSET, orbel fictitious body)
      kind 1: sz name  (NUL-terminated, resolved from sefstars.txt)
    i32 center: 0 = use iflag center bits; else swe_calc_pctr body id
    u64 iflag: full SWE bitmask. Server ORs in SEFLG_SWIEPH.
      Center bits (SEFLG_HELCTR/BARYCTR/TOPOCTR), SEFLG_SIDEREAL,
      SEFLG_TRUEPOS, SEFLG_NONUT, SEFLG_SPEED, SEFLG_RSW_EPHEM etc.
      are all client-supplied; SEFLG_SPEED is added if absent.
    i32 sidMode; f64 sidT0; f64 sidAyanOff
        (swe_set_sid_mode_r triple; applied only when SEFLG_SIDEREAL)
    f64 topoLon (east-positive degrees); f64 topoLat; f64 topoElv (km)
        (swe_set_topo_r; applied only when SEFLG_TOPOCTR)
    char jplFile[64] (swe_set_jpl_file_r; applied only when SEFLG_JPLEPH)
    f64 jdStart (UT)
    u32 stepSeconds
    u32 nTime (rows)
    u8 precision: 0 = f64, 1 = f32
    u32 chunkRows (client hint; server clamps to maxChunkRows)

Row i covers instant `jdStart + i * stepSeconds / 86400.0` (UT).

### 4.5 DATA payload (one chunk)

    u32 chunkIndex
    u32 iTime (first row index in this chunk)
    u32 nTime (rows in this chunk)
    u8 precision (0 = f64, 1 = f32)
    u32 nObj
    nObj per-object metadata records:
      i32 retFlag (<0: this object failed; else flags SWE actually used)
      i32 flagsUsed
      char serr[64]   (SWE error text when retFlag < 0, else zero-filled)
      char name[56]   (body name)
    then the data block, object-major, nObj * nTime * 6 values (f64 or f32):
      xx[0] lon (deg), xx[1] lat (deg), xx[2] dist (AU),
      xx[3] lonSpeed, xx[4] latSpeed (deg/day), xx[5] distSpeed (AU/day)

These six columns are exactly `swe_calc_ut_r`'s `xx[0..5]` with
`SEFLG_SPEED`, and exactly the six reals of GetJPLHorizons()' output
signature (io.cpp:4010) that ComputeEphem() (calc.cpp:1028) consumes.

### 4.6 ERROR payload

    u32 requestId; i32 code; sz text
    codes: 1 bad request/parse, 2 exceeds WELCOME limits, 3 unknown type,
           4 internal, 5 ephemeris data (carries SWE serr text)

### 4.7 Semantics

- REQUEST is answered by one or more DATA chunks for its requestId
  (chunkIndex ascending, row ranges contiguous), then done. Per-object
  failures are recorded in that object's metadata rows (retFlag < 0), not
  fatal to the request. A whole-request failure (e.g. no ephemeris path)
  is ERROR code 5 with the SWE error text.
- Requests are pure functions of their payload. There is no session state:
  a dropped connection re-sends the same requestId after reconnect.
- Heartbeat: server PINGs every 20s, expects PONG within 60s; client may
  PING too. Either side closes on missed heartbeats.
- The server computes with `swe_calc_ut_r` (jd UT; delta-t handled inside
  SWE, same as the client's local swe_calc(JDE) path).

## 5. Server architecture

- One uWS App per event-loop thread; kernel SO_REUSEPORT load-balances
  accepted connections across loops. Default threads = number of cores
  (`--threads` overrides).
- Each loop owns a private slice of the SWE context pool and a private
  result cache. No cross-thread sharing, no locks on the hot path.
- Context pool: `swe_ctx_new()` / `swe_calc_ut_r()` / `swe_ctx_free()`
  (swephexp.h:774-801 in the fork), contract: one thread at a time per
  context, contexts fully independent. Pool sized 2x cores. Each context is
  ~23KB; `swe_ctx_new()` inherits the process master config, so the ephemeris
  path set once at startup reaches every context.
- Per-request configuration uses ONLY the `_r` scoped setters on the serving
  context — `swe_set_sid_mode_r`, `swe_set_topo_r`, `swe_set_jpl_file_r` —
  never the process-global forms, so concurrent requests with different
  sidereal/topocentric settings cannot interfere. Workers never call
  `swe_close()` (it resets shared config); teardown is `swe_close_r(ctx)`.
- Request execution: hash the canonical REQUEST payload → cache lookup →
  on miss, for each time row, for each object, `swe_calc_ut_r` (or
  `swe_fixstar_ut_r` for kind-1 objects, `swe_calc_pctr` when center != 0),
  storing columns; then stream chunks, converting to f32 only at send if
  requested.
- Result cache: per-loop LRU keyed on FNV-1a of the canonical payload,
  caching computed f64 columns. Memory-capped (`--cache-mb`, default 256).
  No invalidation — requests are pure functions of static files.
- Backpressure: uWS send buffering; chunks sized so a slow client cannot
  balloon server memory (`maxChunkRows` from WELCOME).

## 6. Ephemeris path discovery (zero config)

Mirrors the client's SwissEnsurePath() (calc.cpp:2989-3131) in ~100
standalone lines, linking no engine code. Resolution order:

1. `--ephe <path>` (may be a ';'-joined list) — highest priority.
2. Env vars `ASTR<version>` (e.g. ASTR80), then `ASTROLOG`, then `ASTR`
   (the client's ENVIRONVER/ENVIRONALL/ENVIRONVER, astrolog.h:279-282).
3. The `-Yi1 "<dir>"` line of the settings file (us.rgszPath[1],
   astrolog.h:2345; file format: `@` first line, `;` comments, quoted
   params — FProcessSwitchFile, io.cpp:367-475). The settings file itself is
   found via cwd, then the server executable's directory, then those env
   vars. Relative paths resolve against the executable's directory, matching
   the client (calc.cpp:3033-3041, astrolog.as:62-72).
4. Current working directory, then the server executable's directory.
5. Compile-time default (EPHE_DIR equivalent).

Each candidate directory is probed for any sentinel from the client's list
(rgszEphemFileQ, calc.cpp:2811-2818: sepl_18.se1, semo_18.se1, seas_18.se1,
sefstars.txt, seorbel.txt, seasnam.txt, sedeltat.txt, plus other-century
sepl_*.se1). Only directories containing sentinels are joined with ';' and
passed to `swe_set_ephe_path()` once at startup, respecting SWE's limits
(20 dirs / 242 bytes: cDirEphemMax, cchEphemPathMax, calc.cpp:2771-2772).
If nothing is found, the server still starts and serves ERROR code 5 for
file-dependent requests with explicit SWE error text.

The `--ephe` flag and the ephemeris-path setting are logged at startup, with
which sentinels each candidate hit.

## 7. Million-file tree rules

The production ephemeris tree includes ~48GB of numbered-asteroid `astN/`
directories (nearly a million files). Invariants, enforced by design and
asserted by the soak gate:

- Startup performs only sentinel `stat()` calls. No opendir, no tree walk,
  no indexing, ever. Startup is O(1) in tree size.
- Files open lazily inside SWE, per body, on demand. fd use is bounded by
  the context pool (a context holds a handful of handles), not by tree size.
  Startup asserts `RLIMIT_NOFILE` is at least 4096 and warns otherwise.
- No directory batch-stats. The filesystem may be a network mount; nothing
  may assume local-latency metadata operations.
- Fixed-star name resolution reads only sefstars.txt (one small file).
- Hot bodies stay warm via per-context segment caches plus the result cache.

## 8. Build integration

- `Makefile.srcs`: new source group `SRC_EPHSRV` (ephsrv.cpp + future server
  sources). "Add a source file here, once, in the group it belongs to."
- New `Makefile.ephsrv` modeled on Makefile.qt: `NAME=astrolog-ephd`,
  `OBJDIR=obj-ephsrv`, includes Makefile.srcs, links
  `ephsrv/uSockets/uSockets.a -lssl -lcrypto -lz` and the fork's libswe.
- Top-level Makefile gains a named target `ephsrv:` next to `wcli:`/`qt:`.

## 9. Testing and benchmarks

- **Golden**: request planets across sample JDs at f64 precision and compare
  bit-exact against the fork's own 13,000-row golden transcript
  (tests/golden.c) and/or `swetest` hex output — the server must be
  indistinguishable from local SWE.
- **Bench**: concurrent WebSocket clients issuing streaming REQUESTs;
  report throughput and p50/p99 latency under load (animation cadence
  target: one 30-body × 1000-row window in well under 25ms server-side,
  hot-cache).
- **Soak (million-file gate)**: build a synthetic astN farm (one small .se1
  symlinked across ~100k directories), then assert: startup time O(1),
  `getdents`/`opendir` count during startup is zero (strace), fd count
  stable across requests via /proc, and no growth over a long run.
- Targets wired so `make ephsrv` builds the server and the gates run
  standalone (scripts under tools/), keeping the suite group untouched until
  the client phase decides what belongs in `make check`.

## 10. Implementation increments

1. `ephproto.h` + `ephsrv.cpp` skeleton: single loop, context pool of one,
   planets via swe_calc_ut_r, all message types wired, ERROR paths,
   heartbeats. Proven against a raw-socket test client.
2. Multi-loop + full context pool + complete surface: fixed stars, pctr
   centers, sidereal/topo/jpl-file params, moons, asteroids, orbel bodies,
   f32 precision, chunk streaming, WELCOME limits.
3. Result cache (LRU) + bench tool with recorded numbers.
4. Gates: golden, soak, fd assertions; runbook; docs finalized.

Each increment lands green before the next starts.

## 11. Runbook

    astrolog-ephd [--port N] [--ephe path] [--threads N] [--cache-mb N]
                  [--verbose]

Default port: constant `EPH_DEFAULT_PORT` in ephproto.h (47190), shared with
the client so both ends agree with zero configuration. `--ephe` overrides
discovery (§6). The server is stateless across restarts; killing and
restarting it is always safe, and clients reconnect transparently (§4.7).

---

# Part II — Client specification (Ephemeris Server backend)

Unscheduled; implement after Part I. Written to stand alone.

## C1. Backend slot

- New backend in the fEphemFiles/nSwissEph table (astrolog.h:2233-2252) and
  szEphem[] (data.cpp:406-409): "Ephemeris Server", e.g. fEphemFiles=1,
  nSwissEph=5, alongside the existing Swiss/Moshier/JPL-file/Horizons rows.
- Settings writer gains the new backend line (io.cpp:1609-1970 pattern).
- Qt ephemeris combo (qtdialog.cpp) gains the entry.
- `-0n` (us.fNoNetwork) disables the backend like every other internet
  feature.

## C2. ComputeEphem integration

- New dispatch branch in ComputeEphem (calc.cpp:1028+) producing the same
  six-reals signature as GetJPLHorizons (io.cpp:4010): obj, objalt, dir,
  dist, diralt, dirlen — filled directly from the DATA columns; no
  three-point slope derivation needed (speeds come from SEFLG_SPEED).
- Astrolog-object → SWE-id mapping reuses FSwissFromObj (calc.cpp:3637-3653)
  and the custom-slot machinery (rgTypSwiss, calc.cpp:3129-3146): a new slot
  type "server SWE id" passes raw ids through (planets via the existing
  inline map, custom slots via user-entered SWE id, fixed stars by name).
- Settings sent per REQUEST: iflag from GetSwissFlags (calc.cpp:3656-3673)
  plus SEFLG_TRUEPOS when us.fTruePos; sidereal triple from us.fSidereal /
  us.fSidereal2 (SE_SIDM_FAGAN_BRADLEY / SE_SIDBIT_SSY_PLANE,
  calc.cpp:3679-3684); topo site from ciCore.lon/lat and us.elvDef when
  us.fTopoPos — note Astrolog longitude is west-positive, the protocol is
  east-positive, so negate at the client; center from us.objCenter / flags.
- The client's own zodiac-offset arithmetic (is.rSid, us.rZodiacOffset,
  us.rZodiacOffsetAll; calc.cpp:3850-3852, 3930-3933) applies AFTER the
  server columns land, exactly as it does for local Swiss results. The
  server never sees it.

## C3. Connection lifecycle

- On startup the client attempts the WebSocket connection in the background
  and retries every minute, entirely invisible to the user, as long as a
  local ephemeris path is configured.
- When NO local ephemeris path is configured, the backend is required: the
  client shows a "Connecting to cloud ephemeris ..." dialog with a waiting
  bar. Retry ladder: 1 second apart, 10 times; then 1 minute apart; give up
  after one hour total; then exit with a specific exit code (constant
  `EXIT_NO_EPHEMERIS`, proposed value 86, adjustable at implementation).
  The dialog displays the full text of every connection error encountered.
- Transport implementation: QWebSocket in qtdriver.cpp, near the existing
  network code (FGetUrlQt, qtdriver.cpp:6925), using the protocol in
  Part I. Default port EPH_DEFAULT_PORT (47190); server address from a
  new settings key, defaulting to localhost.
- Reliability: reconnect with exponential backoff + jitter on drop;
  heartbeats per §4.7; in-flight requestIds are simply re-sent on reconnect
  (requests are pure functions; no resume state exists).
- Client cache: small ring keyed on canonical request (the JPL web query's
  ring-cache pattern, io.cpp:3960), sized for animation reuse.

## C4. Animation prefetch

- While animating, the client requests a window of ~1000-2000 rows ahead of
  the animation point and issues the next window's request when half the
  current one is consumed. One request fuels minutes of animation at 25ms
  frames; per-request latency stops mattering.
- Chart casts (non-animation) request exactly the rows needed, f64.

## C5. Work log

Client-phase work appends to this document's work log (§ at end), one entry
per landed change, newest last — same convention as QT_GUI_PLAN.md.

---

# Work log

(Entries append here, newest last.)

## Work log

1. **Round 1 landed: the server, its wire client, and both gates.** The
   protocol of §4 became ephsrv/ephproto.h (bounds-checked Writer/Reader,
   static_asserts on every fixed size; the PING/PONG echo payload of the
   original sketch was dropped — heartbeats ride uWS's sendPingsAutomatically
   with idleTimeout 30s instead of the sketched 20s app-level ping, which a
   Qt client answers by itself anyway; the app-level PING/PONG types remain
   in the envelope). eph_srv.cpp implements §5-§7 and §11: one App per event
   loop on SO_REUSEPORT, a private slice of the swe_ctx pool per loop,
   heartbeats delegated to uWS, zero-config discovery with sentinel stats
   only. Verified end-to-end: WELCOME round trip, Sun+Moon rows, sidereal
   (Fagan-Bradley ayanamsha through the context path), per-object failure
   (retFlag -1), ERROR 2 on an oversized REQUEST, ERROR 5 with no ephemeris,
   f32, and 30 concurrent clients across two loops.
2. **The fork's shared-context answers are poisoned by body switches at
   historical dates.** Computing several different bodies on ONE swe_ctx at
   pre-1972 epochs (where the delta-t table is in force) diverges from a
   fresh context by ~0.05 arcsec, and a SECOND call for the same body
   diverges further — while at modern dates (polynomial delta-t) reuse is
   bit-stable. swe_close_r() between bodies restores fresh-context answers
   exactly, so ExecuteRequest() calls it after each object's row loop; the
   pool survives and only the file caches are dropped. This belongs in the
   fork's own ledger (notes/UPSTREAM-BUGS.md already catalogues the
   delta-t table's one-shot init); until it is fixed there, every
   multi-body consumer of the context API needs the same close_r between
   bodies.
3. **tools/ephsrv-golden.sh**: bit-exact (hexfloat string equality) against
   a probe compiled from the same libswe — the context API, not legacy
   swe_calc_ut, which the fork's one-shot delta-t init makes diverge at
   historical dates even in one process. 70 columns green: 6 instants ×
   Sun..Pluto + Vesta, plus sidereal Fagan-Bradley at two instants.
4. **tools/ephsrv-soak.sh**: the §7 invariants measured, not asserted —
   with a 100,000-file astN farm the server still starts in 0.11s (0.10s
   at 2,000 files: O(1), not merely small), the strace log shows zero
   getdents64 on the farm ever, the fd count did not move over a 200-request
   barrage (11 before, 11 after), and a missing asteroid is a clean
   per-object failure that leaves the connection up.
5. **The client's settings round trip forced one backend-slot correction**:
   the -bW writer emits the default address as "" and the reader originally
   refused an empty address as garbage — every written settings file failed
   to reload, which 73 suite failures traced back to. Empty now clears to
   the default, like FCloneSz(NULL) elsewhere; the suite stands at 5649/0
   with the client's new ephem-server group included.
