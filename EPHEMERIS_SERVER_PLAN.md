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

## Status — how to pick this project back up

Everything below is landed and pushed; this section is the resume pointer.

- **Branches.** Everything through client increment 2 was squash-merged
  into `qt` as one commit, `3c7a1c9`, on 2026-09-16 at the maintainer's
  request; `qt` is the tree to work from now. The `ephserver` branch
  (worktree `/nvm/work/ephsrv`, content-identical at `4905f6c`) stays for
  review, per the standing convention, but its history is not in `qt`'s,
  so start increment 3 on a NEW branch off `qt` with its own worktree,
  and squash it into `qt` when asked -- do not rebase or re-squash
  `ephserver`. The commits that made it, in order: `5211e5e` (uWebSockets v20.80.0 + pinned uSockets vendored),
  `f8e7112` (round 1: server, client increment 1, gates, both plan
  docs), `831bd2b` (fork fix lands, workaround removed, static linking),
  `a8b5aa2` (Status sections), `816d401` (server increment 3: the result
  cache, its gate and the bench; work log item 7), `e95b5bc` (the
  FSwissPlanet() split, shared core), `6e29206` (TT instants, node/apsis
  records, the center bit, the pctr delta-t fix; work log item 8), then
  client increment 2 (EPHEMERIS_CLIENT_PLAN.md work log item 3).
- **The Swiss Ephemeris fork** (nrvate/swisseph, `/shares/swisseph`) is at
  **2.10.03-ts.14** (main d1779fe, 2026-09-17): its delta-t tidal term
  no longer follows which files a context has open (ts.11, work log items
  2 and 6), the full review's fork findings are fixed there
  (EPHEMERIS_REVIEW.md F1-F11, F9 last, in ts.13; that repo's
  notes/REVIEW.md), and S-fork is fixed in the library as well as bounded
  at the server's parse (ts.14, work log item 11). The server links
  `$(SWE_HOME)/libswe.a` by archive path; never `-lswe`.
- **Two environment traps, both pinned in the build and worth
  remembering elsewhere:** an installed stale `libswe.so` in
  `/usr/local/lib` wins the runtime search over `-L` every time (static
  archive path is the cure), and before ts.14 the fork's ROOT Makefile
  tracked header dependencies by a rotted hand list, so a header-only
  edit re-archived stale objects -- after pulling a fork older than
  ts.14, rebuild its libswe.a from clean (`rm -f *.o && make libswe.a`).
- **Green today (2026-09-17):** all five gates pass
  (`tools/ephsrv-{golden,robust,cache,bench,soak}.sh`; golden 88 columns
  bit-exact, with its heliocentric leg and topocentric legs at Greenwich
  and Sydney), and `make check` is all clear with the live group
  included (suite 5757/0).
- **Open work: none.** The client's four increments are all landed:
  increment 3 plus the full review's fixes as `11a672c`, protocol 2
  (review items S4, S9, S12, S-fork, T8; work log item 10) as `8d6d1d1`
  from branch `ephdefer`, and increment 4 (the required-server dialog)
  as `df5c63b` from branch `ephclient4`. Both branches stay for review.
  The two optional leftovers are done too (work log item 11): the
  bundled planet and Moon files are swept against Moshier by
  `tools/check-ephem.sh`, and the fork's root Makefile takes its header
  dependencies from the compiler.

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
  `2.10.03-ts.14` (delta-t order-independent since ts.11, work log item 6;
  the review's fixes in ts.12, F9 in ts.13, S-fork in ts.14): link `$(SWE_HOME)/libswe.a` by archive path.
  Never `-lswe` — an installed stale `libswe.so` in /usr/local/lib wins
  the runtime search over `-L` (see Status). NOT vendored into this repo —
  it is a sibling project with its own build, tests, and release cadence.

Proven compile command (from repo root):

    g++ -std=gnu++17 -O2 -I ephsrv/uWebSockets/src -I ephsrv/uSockets/src \
      ephsrv/eph_srv.cpp ephsrv/uSockets/uSockets.a \
      -L/shares/swisseph -lswe -lssl -lcrypto -lz -lpthread -o astrolog-ephd

uWS v20.80.0 API note: server-side WebSocket handlers are
`WebSocket<SSL, true, UserData>` — the second template parameter is
`isServer`, counterintuitively `true` on the server side.

## 4. Protocol v2

Shared header `ephsrv/ephproto.h`, compiled into both ends (server and,
later, the Qt client). All integers little-endian, structs packed, no
padding; every layout below is exhaustive.

Version 2 (2026-09-16, EPHEMERIS_REVIEW.md S4 and S9) differs from version
1 in two things, both carried by fields version 1 did not have: WELCOME
gains `maxCells`, the work bound per REQUEST, and a DATA object whose rows
fail only in part answers the rows that computed as real and the failed
rows as NaN. A version-1 peer is refused at the envelope, before any of
the new fields are read.

### 4.1 Envelope (16 bytes)

    offset size field
    0      u16  magic = 0x1EF0
    2      u8   protoVersion = 2
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
    u32 maxCells   (objects x rows per REQUEST; version 2. Default
                    kMaxCellsDefault = 100000, overridable with
                    --max-cells; a REQUEST over it is refused ERROR 2)
    sz  serverVersion string

### 4.4 REQUEST payload

    u32 nObj
    nObj object records:
      u8 kind: 0 = body by id, 1 = fixed star by name,
               2 = node or apsis of a body (added 2026-09-16, work log 8)
      kind 0: u32 id   (SWE id: planet, moon incl. SE_PLMOON_OFFSET,
                        asteroid incl. SE_AST_OFFSET, orbel fictitious body;
                        at most kObjIdMax, which keeps Swiss's own
                        center-of-body arithmetic and table indexes inside
                        int32 -- S-fork in EPHEMERIS_REVIEW.md)
      kind 1: sz name  (NUL-terminated, resolved from sefstars.txt)
      kind 2: u32 id, u8 point (1 north node, 2 south node, 3 perihelion,
              4 aphelion), u8 method (0 mean, 1 osculating): swe_nod_aps
    i32 center: 0 = use iflag center bits; else swe_calc_pctr body id
        (or, under kIflagCenter, a swe_calc_pctr body even when 0)
    u64 iflag: full SWE bitmask in the LOW 32 bits. Server ORs in
      SEFLG_SWIEPH. Center bits (SEFLG_HELCTR/BARYCTR/TOPOCTR),
      SEFLG_SIDEREAL, SEFLG_TRUEPOS, SEFLG_NONUT, SEFLG_SPEED,
      SEFLG_RSW_EPHEM etc. are all client-supplied; SEFLG_SPEED is added
      if absent. The HIGH 32 bits are the protocol's, stripped before SWE
      sees the flags (added 2026-09-16, work log 8):
        bit 32 kIflagTimeTT: jdStart and every row are TT, not UT, and the
               server calls the ET entry points (swe_calc_r,
               swe_calc_pctr_r, swe_nod_aps_r, swe_fixstar_r) with the
               instant exactly as sent -- how a client that makes its own
               delta-t gets answers bit-identical to calling SWE itself
        bit 33 kIflagCenter: the center field is a swe_calc_pctr body
               even when it is 0 (SE_SUN)
    i32 sidMode; f64 sidT0; f64 sidAyanOff
        (swe_set_sid_mode_r triple; applied only when SEFLG_SIDEREAL)
    f64 topoLon (east-positive degrees); f64 topoLat; f64 topoElv
        (meters, as swe_set_topo takes it; applied only when SEFLG_TOPOCTR)
    char jplFile[64] (swe_set_jpl_file_r; applied only when SEFLG_JPLEPH)
    f64 jdStart (UT, or TT under kIflagTimeTT)
    u32 stepSeconds
    u32 nTime (rows)
    u8 precision: 0 = f64, 1 = f32
    u32 chunkRows (client hint; server clamps to maxChunkRows)

Row i covers instant `jdStart + i * stepSeconds / 86400.0`, in the time
scale jdStart is in; the client evaluates the same expression to find its
rows, so it is exact, not approximate. A UT instant handed to an entry
point that takes ET (swe_calc_pctr_r, swe_nod_aps_r) is converted with
swe_deltat_ex_r first, the conversion swe_calc_ut_r makes itself.

### 4.5 DATA payload (one chunk)

    u32 chunkIndex
    u32 iTime (first row index in this chunk)
    u32 nTime (rows in this chunk)
    u8 precision (0 = f64, 1 = f32)
    u32 nObj
    nObj per-object metadata records:
      i32 retFlag (<0: NO row of this object computed; else flags SWE
                   actually used)
      i32 flagsUsed
      char serr[64]   (the first failed row's SWE text whenever any row
                       failed, so retFlag >= 0 with a non-empty serr is a
                       partial answer; zero-filled when nothing failed)
      char name[56]   (body name)
    then the data block, object-major, nObj * nTime * 6 values (f64 or f32):
      xx[0] lon (deg), xx[1] lat (deg), xx[2] dist (AU),
      xx[3] lonSpeed, xx[4] latSpeed (deg/day), xx[5] distSpeed (AU/day)

A row Swiss could not answer is NaN in all six of its columns, and the
rows that computed are real (version 2; version 1 failed the object for
the whole window, so a window reaching past an ephemeris file's edge lost
every frame before the edge too -- EPHEMERIS_REVIEW.md S9).

These six columns are exactly `swe_calc_ut_r`'s `xx[0..5]` with
`SEFLG_SPEED`, and exactly the six reals of GetJPLHorizons()' output
signature (io.cpp:4010) that ComputeEphem() (calc.cpp:1028) consumes.

### 4.6 ERROR payload

    u32 requestId; i32 code; sz text
    codes: 1 bad request/parse, 2 exceeds WELCOME limits, 3 unknown type,
           4 internal, 5 ephemeris data (carries SWE serr text)

### 4.7 Semantics

- REQUEST is answered by one or more DATA chunks for its requestId
  (chunkIndex ascending, row ranges contiguous), then done. A per-row
  failure is NaN in that row's six columns, with the first failed row's
  serr in the object's metadata, and is never fatal to the request. A
  whole-request failure (e.g. no ephemeris path) is ERROR code 5 with the
  SWE error text; a REQUEST past the work bound WELCOME advertises is
  ERROR code 2 before anything is computed.
- Requests are pure functions of their payload. There is no session state:
  a dropped connection re-sends the same requestId after reconnect.
- Heartbeat (as built): uWS protocol-level pings on a 30 s idle timeout,
  closing a silent connection; the app-level PING/PONG types remain for
  clients without automatic pong. (The sketch said app PINGs every 20 s
  with a 60 s deadline; nothing implements that.)
- The server computes with `swe_calc_ut_r` for UT body requests,
  `swe_calc_r` under kIflagTimeTT, `swe_fixstar(_ut)_r` for stars, and
  `swe_calc_pctr_r` / `swe_nod_aps_r` (which take ET) for centered and
  node/apsis requests, converting a UT instant with `swe_deltat_ex_r`
  (§4.4 is the detail).

## 5. Server architecture

- One uWS App per event-loop thread; kernel SO_REUSEPORT load-balances
  accepted connections across loops. Default threads = number of cores
  (`--threads` overrides).
- Each loop owns a private slice of the SWE context pool and a private
  result cache. No cross-thread sharing, no locks on the hot path.
- Context pool: `swe_ctx_new()` / `swe_calc_ut_r()` / `swe_ctx_free()`
  (swephexp.h:774-801 in the fork), contract: one thread at a time per
  context, contexts fully independent. ONE context per loop (as built,
  2026-09-16): a loop is one thread, so more parallelised nothing, opened
  every file again per context, and made an answer depend on which of a
  loop's contexts served it. The sketched 2x-cores pool left loops past
  it with none (`--threads` above 2x cores: ERROR 4). Each context is
  ~23KB; `swe_ctx_new()` inherits the process master config, so the ephemeris
  path set once at startup reaches every context.
- Per-request configuration uses ONLY the `_r` scoped setters on the serving
  context — `swe_set_sid_mode_r`, `swe_set_topo_r`, `swe_set_jpl_file_r` —
  never the process-global forms, so concurrent requests with different
  sidereal/topocentric settings cannot interfere. The JPL setter runs only
  when a request names a different file than the one the loop's context has
  open (S12): `swe_set_jpl_file_r` closes every file and rebuilds its
  delta-t and leap-second tables on each call, which per-JPL-request cost
  nothing correct and a long JPL animation paid per request. Workers never
  call `swe_close()` (it resets shared config); teardown is
  `swe_close_r(ctx)`.
- Request execution: hash the canonical REQUEST payload → cache lookup →
  on miss, for each time row, for each object, `swe_calc_ut_r` (or
  `swe_fixstar_ut_r` for kind-1 objects, `swe_calc_pctr` when center != 0),
  storing columns; then stream chunks, converting to f32 only at send if
  requested.
- Result cache (as built, work log item 7; `ephsrv/eph_cache.h`): per-loop
  LRU keyed on the canonical REQUEST, hashed FNV-1a, holding the computed
  f64 columns and the per-object metadata. The key is everything the
  answer depends on and nothing else: `precision` and `chunkRows` are
  delivery parameters and are left out, so a chart's f64 window and the
  animation's f32 window of the same rows are one computation; the
  sidereal, topocentric and JPL-file triplets are keyed only under the
  iflag bit that makes SWE read them; and the two forced flags are folded
  in. Per-object failures are cached with the rest. Memory-capped
  (`--cache-mb`, default 256, the TOTAL: each loop gets an equal share,
  since the kernel spreads connections across loops and one loop cannot
  answer from another's cache; 0 disables). An entry larger than the
  loop's whole share is computed and streamed but not stored. Streams
  hold their entry by `shared_ptr`, so eviction never pulls the columns
  out from under a slow client. No invalidation — requests are pure
  functions of static files, and a changed tree is picked up by restart.
- Backpressure (as built, work log item 9): DATA chunks go out until the
  connection has 4 MiB buffered unsent, then wait for drain; control
  replies (WELCOME, ERROR, PONG) always go out. uWS's own
  `maxBackpressure` is off, because past it uWS drops every send, the
  refusals included. A connection may hold 4 computed-and-unread answers;
  past that a REQUEST is refused ERROR 2 before anything is computed.
  NOT bounded: the CPU one request may take -- 64 bodies x 20000 rows is
  ~14 s of its loop, during which that loop's other connections wait
  (EPHEMERIS_REVIEW.md S4, deferred: a cells budget would change what
  WELCOME promises).

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
  hot-cache). As built: `tools/ephsrv-bench.sh`, five scenarios over that
  window, client-observed latency from `eph_wsclient --latency` and the
  server's own compute time from its `--verbose` log. Measured
  2026-09-16 on this machine (12 cores, 12 loops, the bundled `ephem/`):

  | scenario | p50 ms | p99 ms | n | note |
  |---|---|---|---|---|
  | cold, server compute | 647 | 716 | 5 | 30,000 `swe_calc_ut_r` per window |
  | cold, client round trip | 651 | 719 | 5 | one client, distinct windows |
  | hot f64, client | 2.5 | 6.4 | 50 | one client, 1.4 MiB per window |
  | hot f32, client | 1.2 | 1.7 | 50 | one client, 0.7 MiB per window |
  | hot f64 × 8 clients | 4.3 | 7.6 | 400 | 421 windows/s aggregate |
  | cold × 8 clients | 719 | 1713 | 40 | 6.5 windows/s aggregate |

  The hot target is met by an order of magnitude. The cold figure is the
  one client increment 2 has to design around: a window is computed in
  ~0.65 s, so the prefetch of the next window must be in flight well
  before the current one runs out, which the client plan's
  50%-consumed rule (§6 there) gives ~25 s of margin for at 25 ms
  frames. Cold throughput across loops is sub-linear (6.5 windows/s for
  8 clients against 1.5 for one) because SO_REUSEPORT hashes each new
  connection to a loop and two on one loop queue behind each other; the
  p99 of 1.7 s is that queue.
- **Cache gate**: `tools/ephsrv-cache.sh`, the result cache's own gate
  (work log item 7): a unit test compiled against `eph_cache.h` alone for
  the key canonicalization and the LRU mechanics, then a live server with
  a 1 MiB cache read through its `--verbose` log -- a repeated window is
  a hit and bit-identical to the miss that filled it, f32 hits the f64
  entry, an oversize window is answered and not stored, eviction is LRU
  rather than FIFO and stays under the cap, and `--cache-mb 0` never
  hits.
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
3. Result cache (LRU) + bench tool with recorded numbers. Landed, work
   log item 7: `ephsrv/eph_cache.h`, `tools/ephsrv-cache.sh` (its gate)
   and `tools/ephsrv-bench.sh`.
4. Gates: golden, soak, fd assertions; runbook; docs finalized.

Each increment lands green before the next starts.

## 11. Runbook

    astrolog-ephd [--port N] [--ephe path] [--threads N] [--cache-mb N]
                  [--max-cells N] [--verbose]

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

6. **The fork's tidal-term defect is fixed at source, and the server's
   workaround is gone.** The fork (nrvate/swisseph, 2.10.03-ts.11,
   c86c2b6) now publishes the DE numbers its setters' header pre-opens
   read (`sweph_denum_moon`, `jpldenum_cfg`, in `SWI_CFG_PATH`), so every
   context resolves delta-t's tidal term from the configuration instead of
   from whichever files it happens to have open — the fresh-context
   order dependence of work-log item 2 (Moon 272.41632607 vs
   272.41633228 at JD 2415020.5) is gone at the library level, held by
   G4's new ORDER property, and the server's per-object swe_close_r()
   workaround is deleted: the pool contexts stay warm and the golden
   gate still passes 70 columns bit-exact. Two environment traps fell
   out of the verification and are now pinned in the build: an installed
   libswe.so in /usr/local/lib (a month stale) wins the runtime search
   over -L every time, so Makefile.ephsrv and the golden gate's oracle
   link $(SWE_HOME)/libswe.a by archive path — a static archive cannot
   be shadowed; and the fork's root Makefile tracks no header
   dependencies, so a header-only edit re-archives stale objects — its
   tests/ learned this the hard way (69495ff) and the root build has
   not yet.

7. **Server increment 3: the result cache, its gate, and the bench.**
   `ephsrv/eph_cache.h` is the per-loop LRU §5 sketched: keyed on the
   canonical REQUEST -- the object list in order, center, the iflag with
   the two forced bits folded in, the sidereal/topo/JPL triplets only
   under the bit that makes SWE read them, jdStart, step and row count;
   NOT precision or chunkRows, which are delivery parameters, so a
   chart's f64 window and the animation's f32 window of the same rows are
   one computation. FNV-1a is the map's hash and equality is on the whole
   key. Entries hold the f64 columns and the metadata by `shared_ptr`, and
   a Stream holds the same pointer, so a slow client keeps its window
   alive through any number of evictions. `--cache-mb` is the total,
   split equally across loops; an entry over a loop's share is computed
   and streamed but not stored; 0 disables. The verbose log names every
   request "cache hit" or "cache miss N ms" with the cache's state, which
   is what the gate and the bench read. `tools/ephsrv-cache.sh` was
   falsified four ways before it was trusted (its header lists them), and
   its unit half found one bug in the gate itself before any in the code:
   a hit/miss count the author had mis-tallied. `tools/ephsrv-bench.sh`
   measured the numbers in §9; two of its own defects are worth the
   record. A bare `wait` waits for the background server too and the
   first run hung forever (the client PIDs are named now), and a new
   connection's first request can miss on a loop that has never seen the
   window even though another loop has -- the per-loop design's cost,
   right for a client that keeps one connection for its lifetime and
   dropped from the hot samples for the bench's purposes. `make ephsrv`
   now builds the wire client too; it used to build the server alone, so
   the gates' fallback `make -s ephsrv` never produced `eph_wsclient`.
   Golden 70/70 bit-exact and soak unchanged with the cache in the path.

8. **Three protocol additions and a server bug, for client increment 2.**
   `kIflagTimeTT` (bit 32 of iflag): the instants are TT and the server
   calls the ET entry points with them exactly as sent -- the only way to
   be bit-identical with a client that computes its own delta-t and lets
   the user override it. `kIflagCenter` (bit 33): the center field is a
   swe_calc_pctr body even when it is 0. Object record kind 2: a node or
   apsis of a body, what a custom object with an rgPntSwiss[] value is.
   The high half of iflag is the protocol's and never reaches SWE; the
   cache key keeps it. The bug: increment 1 handed swe_calc_pctr_r the UT
   instant, and that function takes ET -- a delta-t of error on every
   centered request; UT requests to it and to swe_nod_aps_r now add
   swe_deltat_ex_r first. The golden gate grew a leg for each (70 columns
   to 82) plus a check that a UT request for the Moon at 1900 does NOT
   match the TT oracle, so a server that ignored the bit fails. Found by
   the client's live group: the server logged "ephemeris path" before
   any loop bound the port, so a client connecting on that line could be
   refused, and a bind failure was silent -- a server with no listener
   answered nobody and looked alive. It logs "listening on port" from the
   listen callback now, exits with a message when the port cannot be
   bound, and the four gates and the suite wait for that line.

9. **The full review's server findings, fixed with a gate of their own
   (2026-09-16, EPHEMERIS_REVIEW.md S1-S13).** `tools/ephsrv-robust.sh`
   holds one assertion per finding, each seen failing with its fix
   removed:
   - A REQUEST with a NaN or infinite jdStart crashed every loop (the
     fork indexes a table with `(int)floor(NaN)` for the mean node and
     apogee). parseRequest refuses non-finite reals, |jdStart| past 1e8,
     and iflag bits the protocol does not define.
   - A chunk that met BACKPRESSURE was sent again on drain -- uWS had
     buffered it -- so a row-counting client finished early with zeros.
     Fixing that exposed a stall underneath: vendored uSockets'
     `us_socket_write2`, the path uWS takes for frames of 16 KB and up,
     never set `last_write_failed`, so a partial write made from inside
     the drain callback had its WRITABLE poll turned straight back off
     and the buffered tail was never sent. The old code had hidden it by
     re-sending whole chunks. One line in `ephsrv/uSockets/src/socket.c`,
     marked ASTROLOG PATCH; found by logging each send on the server and
     each chunk on the client until the two disagreed.
   - Past uWS's `maxBackpressure` every send was DROPPED, the ERROR
     refusing a queued request included, so that client waited forever.
     The ceiling is applied to DATA chunks alone now (§5).
   - A second `Conn` constructed over the one uWS had made leaked ~575
     bytes a connection (11 MiB per 20000).
   - `--threads` above the pool left loops with no context.
   - uSockets sets SO_REUSEPORT on every listener, so a second server on
     a port did not fail -- it shared the connections. The server connects
     to its port before binding and exits if anything answers. (A trial
     bind was the first form, and it refused ports that only had client
     connections in TIME_WAIT on them.)
   - Smaller: the planet-name buffer is AS_MAXCH; the cache charges the
     key and a per-entry allowance, not the columns alone, and its hash is
     seeded per cache; every HELLO is parsed and answered; the zstd flag
     and text frames are refused; a node/apsis retFlag is the flags used;
     an object's name survives its last row failing; Makefile.ephsrv
     relinks when the fork's libswe.a changes.
   - Every gate's port and the suite's moved below the kernel's ephemeral
     range (32768 up): after the robustness gate's 20000 connections,
     their TIME_WAIT sockets held ports scattered across it, and the
     golden gate's server was refused its port with nothing listening.
   - eph_wsclient checks every answer row by row (a row twice, a row
     missing, a chunk for another request) and gained `--sleep-ms`,
     `--burst`, `--cycles` and `--hello` for the gate.
   Deferred with reasons in the review ledger: a per-request CPU budget,
   per-row failure reporting (both change the protocol), and the JPL
   setter's cost.
10. **The review's deferred protocol items are done: protocol 2 (2026-09-16,
   EPHEMERIS_REVIEW.md S4, S9, S12, S-fork, T8; branch `ephdefer`).**
   - WELCOME carries `maxCells`, the work bound per REQUEST (§4.3):
     objects x rows, default 100000 (`kMaxCellsDefault`), startable with
     `--max-cells`. A REQUEST over it is refused ERROR 2 before anything
     is computed -- 64 bodies x 20000 rows measured 13.7 s on the loop's
     only thread, and every other connection on that loop waited. The Qt
     client clamps a window's rows to the bound in ClampEphSrvReqQt().
   - A failed row's six values are NaN, and the rows that computed are
     real; retFlag < 0 only when NO row computed, and serr carries the
     first failed row's text whenever any row failed (§4.5). Version 1
     failed the object for the whole window, so a window reaching past an
     ephemeris file's edge lost every frame before the edge too. The
     client asks exactly only the frames whose row failed
     (FWindowObjFailedQt takes the frame's instant) and FSrvPlanetQt
     fails a NaN row with the metadata's serr.
   - The JPL setter runs only when a request names a different file than
     the loop's context has open (S12): swe_set_jpl_file_r closes every
     file and rebuilds its delta-t and leap-second tables on each call,
     which a JPL request per row paid for nothing correct. An open file
     left across requests is read only by calls that name it, so no
     later answer moves.
   - REQUEST object ids are bounded at `kObjIdMax` ((INT32_MAX - 9099) /
     100): past it Swiss's center-of-body mapping (`ipl*100 + 9099`,
     sweph.c:432) overflows int32 and `ctx->nddat[ipl]` is reached out of
     bounds (sweph.c:4816) -- UBSan flagged both from the review's fuzz
     (S-fork). The bound also refuses negative int32 ids, of which
     SE 2.10 has none. The fork itself was not touched: this is
     protocol-level validation, and upstream keeps its own arithmetic.
   - The golden gate gained a heliocentric leg and two topocentric places
     (Greenwich, Sydney), two bodies each (T8): a pooled context keeping
     a stale `swe_set_topo` answers the second place with the first
     one's horizon, and only differing places catch it.
   - ephsrv-robust.sh: the heavy legs (64 x 10000) start their server
     with `--max-cells 700000`, and two legs are new -- S4b (a request
     one row over the default bound refused ERROR 2, one row under it
     answered) and S9 (a window crossing the bundled sepl_18's end:
     rows before it real, rows past it NaN, retFlag never negative).

11. **S-fork fixed in the library, which turned out to be two upstream
    defects and not one (2026-09-17, fork ts.14, main d1779fe, branch
    `s-fork`); and the two optional leftovers.**
   - Item 10 said the fork was left alone and that sweph.c:4816 was
     `ctx->nddat[ipl]` reached out of bounds by a wire id. The second half
     was wrong. A fuzz of the library itself under UBSan
     (bounds-strict, float-cast-overflow) placed that report in
     `get_new_segment()`: `(int32)((tjd - tfstart) / dseg)` with
     `dseg = 0`. The cause is an ordinary call sequence, no hostile id in
     it: `swe_calc(SE_ECL_NUT)` under a different ephemeris flag ran
     `free_planets()` outside its own `ipl != SE_ECL_NUT` exemption, so
     the files stayed open with their constants zeroed. Sun (Swiss),
     SE_ECL_NUT (Moshier), Mars (Swiss) returned ERR "sepl_18.se1 is
     damaged" -- in upstream too, at 3fd0f95 and 91339e5. The same fuzz
     found `swe_pheno(SE_ECL_NUT)` reading `pla_diam[-1]`/`mag_elem[-1]`
     and returning OK.
   - Fixed behind `SWE_UPSTREAM_COMPAT`: `free_planets()` only where the
     files close, the center-of-body test bounded at `ipl >= SE_ECL_NUT`,
     `swe_pheno()` refusing `ipl < SE_SUN`. Nets: two new G24
     `check-compat` cases (both directions) and a new gate, G25
     `check-hostile` -- every calculation entry point over edge ids and
     flag sets plus a fixed-seed random walk, UBSan with no recovery; it
     fails on the compat build, and 400000 random calls were clean on the
     fix. The fork's whole `make check` and G8 pass; the server's golden
     gate passes 88 columns bit-exact on ts.14. The fork's
     notes/UPSTREAM-BUGS.md gained entries 15-17. The server keeps its
     `kObjIdMax` bound: a protocol should refuse ids no body has anyway.
   - The fork's root Makefile had a hand-written dependency list that
     rebuilt 6 of 10 library objects after touching sweph.h; it is
     `-MMD -MP` now and rebuilds all 10.
   - `tools/check-ephem.sh` now sweeps the bundled planet and Moon files
     against Moshier (`tools/ephem-sweep.cpp`, compiled against the
     vendored Swiss): 952800 samples over 1800-2400, threshold 0.01 deg,
     worst good 0.0023. The sepl_18.se1 that shipped before 00a92da fails
     it (35 samples, Mercury 110 deg), and so does a directory with no
     files, since a Moshier fallback would otherwise agree with itself.
     About 15 s, in `make check-full`. Closes review item B3's open net.
