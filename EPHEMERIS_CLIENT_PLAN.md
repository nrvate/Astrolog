# Ephemeris Client Plan

The client half of the ephemeris server project. The server's design and
protocol are the authority in EPHEMERIS_SERVER_PLAN.md (its §4 is byte-law);
this document expands that plan's Part II into a standalone client
specification, incorporating what building the server taught us. It is
written to be implemented without reading this project's conversation.

Work log entries append at the end, newest last, same convention as
QT_GUI_PLAN.md.

## 1. Goals

- A new chart-time ephemeris backend, "Ephemeris Server", that gets every
  position over the long-lived WebSocket from the ephd server: planets,
  moons, asteroids, fictitious bodies, fixed stars — whatever the central
  tree serves — so a client machine carries no ephemeris files at all.
- Animation-grade responsiveness: 25ms frames on 10-minute steps run
  entirely off prefetched windows; network activity is one request per
  window, amortized to roughly one per 50 seconds of animation.
- A held connection that heals itself: background reconnect, resent
  requests, no user-visible state unless the server is actually required.
- Chart casts (including sidereal, topocentric, true-position, custom
  center bodies) compute exactly what the local Swiss Ephemeris path
  computes — same flags, same offsets, same results, bit-exact.

## 2. Lessons carried from the server build

These shaped the spec below; each is a design correction, not trivia.

1. **ComputeEphem is per-object, but the fetch must not be.**
   ComputeEphem(real t) (calc.cpp:1035) receives the Julian day and loops
   objects itself, calling one compute function per object. A per-object
   blocking fetch would mean one network round trip per object per cast —
   the exact pathology the Horizons ring cache exists to paper over
   (io.cpp:3960). The client therefore batches at the head of the loop and
   reads per object from a window cache (§5).
2. **The synchronous engine needs a synchronous facade.** Astrolog computes
   charts synchronously; there is no "render later" path. The facade is:
   cache hit → instant; miss → issue request, then a bounded, cancelable
   event-loop wait (the FGetUrlQt pattern, qtdriver.cpp:6925); still
   nothing → fail soft for this cast. Steady-state animation only ever
   sees hits; misses are amortized at window boundaries.
3. **Do not reuse the Horizons failure latch.** is.fNoEphFile permanently
   disables a web source after its first failure (io.cpp:4023) — right for
   a one-shot query, wrong for a held connection, where it would turn one
   dropped frame-window into a dead backend until restart. The server
   backend fails soft per cast and lets the background reconnect heal.
4. **Qt answers protocol pings by itself.** uWS heartbeats are WebSocket
   protocol-level ping frames; QWebSocket replies automatically. The
   client implements no heartbeat of its own; the app-level PING/PONG
   message types stay in the envelope for other clients.
5. **Requests are pure functions; WELCOME caps govern the session.**
   Reconnect-and-resend needs no state. The client reads maxObjs/maxRows/
   maxChunkRows/caps from WELCOME and clamps every request to them;
   float32 is chosen for animation windows only if the caps bit says the
   server supports it (f64 is always legal).
6. **The server speaks SWE ids; every Astrolog-specific conversion is
   client-side.** Object mapping (FSwissFromObj, calc.cpp:3637-3653),
   zodiac offsets (is.rSid arithmetic, calc.cpp:3850-3852), west-positive
   longitude (negate before sending topo coords), and speed derivations
   (none needed — SEFLG_SPEED columns arrive ready) all live here, exactly
   as they do for the local Swiss path.
7. **ephproto.h is compiled into the client.** The server's ephsrv/ephproto.h
   ephsrv/ephproto.h is the single protocol definition (static_asserts on
   every fixed size). The Qt build gains -I ephsrv and includes it; a
   mismatch is a compile error, not a runtime mystery.

## 3. Backend slot and settings

- New backend row in the fEphemFiles/nSwissEph table (astrolog.h:2233-2252):
  fEphemFiles=1, nSwissEph=5, "Ephemeris Server" appended to szEphem[]
  (data.cpp:406-409). -bs cycling and the command-line mapping pick it up
  like the other rows.
- Settings writer line in the -bs family (io.cpp:1609-1970 pattern) and the
  Qt ephemeris combo entry.
- New setting: the server address, a ws:// URL or host:port, default
  localhost:47190 (EPH_DEFAULT_PORT from ephproto.h — the two ends agree
  with zero configuration). Its switch and mnemonic are chosen at
  implementation time against switch.cpp's tables; the settings file line
  follows the quoted-parameter form the -Yi lines use.
- ComputeEphem's dispatch gains fSrvPla = us.nSwissEph == 5, parallel to
  fJPLPla = us.nSwissEph == 3 (calc.cpp:1049).
- -0n (us.fNoNetwork) disables the backend at selection time: the combo
  may show it, but ComputeEphem fails fast and says why, once.

## 4. Connection lifecycle

- On startup, if the backend is selected (or becomes selected), the client
  connects in the background: QWebSocket to the configured address, state
  machine Disconnected → Connecting → Welcomed. Retry every 60s. Entirely
  invisible: no dialogs, no status noise, while a local ephemeris path is
  configured.
- **Required-server mode**: when SwissEnsurePath() (calc.cpp:2989-3131)
  finds no ephemeris files at all AND the server backend is what would be
  used, the server is the only source. Startup then shows a modal
  "Connecting to cloud ephemeris ..." dialog with a waiting bar and the
  full text of every connection error encountered, appended per attempt.
  Retry ladder: 1 second apart, 10 times; then every 60 seconds; give up
  after one hour total; exit with EXIT_NO_EPHEMERIS (proposed value 86 —
  confirm no collision with existing exit codes at implementation).
- On drop (once Welcomed): reconnect with exponential backoff (1s doubling,
  capped at 60s, jittered). In-flight requestIds are re-sent verbatim after
  reconnect; requests are pure functions, so there is nothing to resume.
- The server address is read once per connect attempt; changing it in
  settings takes effect on the next (re)connect.
- Protocol-version or WELCOME mismatch (server too old): behave as
  connection refused — retry cycle continues, and required-server mode
  shows the server's version text in the dialog so the mismatch is
  diagnosable from the sofa.

## 5. ComputeEphem integration

- Prefetch hook at the head of ComputeEphem (before the object loop): when
  fSrvPla, scan the object set exactly as the loop's skip logic does
  (FSkipEphem, calc.cpp:999-1024), map each server-bound object to its
  protocol record, and issue ONE REQUEST covering all of them and the
  needed row window (§6). Objects on non-server sources (custom slots of
  other types, Horizons type-4 slots) are excluded exactly as the loop
  excludes them.
- The per-object branch inside the loop (the server analogue of the
  GetJPLHorizons call site, calc.cpp:1065-1072) reads six reals from the
  window cache — the same signature GetJPLHorizons fills (io.cpp:4010):
  obj, objalt, dir, dist, diralt, dirlen. No three-point slope derivation:
  the columns carry speeds from SEFLG_SPEED.
- Parameters per REQUEST, mirroring GetSwissFlags (calc.cpp:3656-3673) and
  FSwissPlanet's iflag assembly (calc.cpp:3741-3752):
  - iflag: SEFLG_SWIEPH | SEFLG_SPEED, plus SEFLG_SIDEREAL / SEFLG_NONUT /
    SEFLG_TRUEPOS / center bits as the settings dictate. The server ORs in
    nothing the client didn't ask for (except SWIEPH and SPEED).
  - Sidereal triple: SE_SIDM_FAGAN_BRADLEY, or SE_SIDBIT_SSY_PLANE when
    us.fSidereal2 — the same two modes GetSwissFlags sets; t0 0, offset 0.
    The client's own rZodiacOffset arithmetic applies AFTER the columns
    land (is.rSid, calc.cpp:3850-3852) — the server never sees it.
  - Topo: when us.fTopoPos, site coords from ciCore.lon (negated —
    Astrolog is west-positive, the protocol east-positive), ciCore.lat,
    us.elvDef/1000. SEFLG_TOPOCTR in iflag.
  - Center: from us.objCenter / us.fBarycenter as the local path does;
    custom central bodies pass the center id and use the protocol's
    swe_calc_pctr path.
- Object → record mapping (all client-side):
  - planets Sun..Pluto and Earth via the existing inline map
    (calc.cpp:3711-3733);
  - Chiron, Cer..Ves, nodes, Lilith variants via FSwissFromObj;
  - custom slots: orbel fictitious bodies (type 0), MPC asteroids
    (type 1, + SE_AST_OFFSET), object indices (type 2), planetary moons
    (type 3, + SE_PLMOON_OFFSET incl. COB 50199) — all become kind-0
    records with raw SWE ids;
  - fixed stars by name become kind-1 records;
  - type-4 (JPL Horizons) slots stay on the Horizons path, untouched.
- True position: SEFLG_TRUEPOS is in the REQUEST iflag, so the server
  computes it — the Horizons path's client-side fTruePos adjustment
  (io.cpp:4162) does not apply here.

## 6. Animation prefetch and the window cache

- The row grid is anchored at the cast: jdStart = the t ComputeEphem is
  running with, quantized so every animation frame lands exactly on
  grid(t0 + i*step). Animation advances the chart time in fixed units
  (PciAnimate / is.tim1, us.nAnim rate), so a window requested at frame
  zero serves every later frame by lookup; only the request at a window
  boundary touches the network.
- Window sizing: default 2000 rows clamped to WELCOME maxRows; at 10-minute
  steps that is ~14 days of chart time, ~50 seconds of 25ms animation.
  Next window requested when half consumed, issued in the background
  (never inside the render path).
- The window cache: a small LRU of windows (default 8) keyed on the
  canonical request (object list, iflag, sidereal triple, topo, step,
  jdStart quantized, precision). Bounded (~64MB worst case at maxObjs ×
  maxRows f64; the cap is checked at fill time, windows above it are
  refused, not evicted-to-death). This generalizes the JPL ring cache
  (io.cpp:3960) from single replies to rectangular windows; its design
  note applies verbatim: cache raw columns, apply settings after.
- Precision: chart casts request f64. Animation windows request f32 when
  WELCOME caps allow (25ms frames do not resolve the last bits of f64;
  the f64 path is always available and unchanged). The cache keys include
  precision, so a cast during animation never reads f32 rows.
- Grid shifts (anim rate changed, chart recast, step size changed) simply
  produce a new cache key; old windows age out of the LRU.

## 7. UI surface

- The ephemeris combo gains "Ephemeris Server"; selecting it shows the
  server address field (default localhost:47190) beside the existing
  backend controls.
- Connection state appears in the About/status area as one quiet line:
  address, state (Connecting/Online/Retry in Ns), server version from
  WELCOME. No modal anything in background mode (§4).
- The required-server dialog (§4) is the only blocking UI, and only at
  startup with no local ephemeris configured.

## 8. Failure semantics

- Any single cast: server-bound objects with no data render as failed
  objects the way the Horizons path renders a failed fetch (warning once
  per cast, no latch — lesson 3). The rest of the chart is unaffected.
- us.fNoNetwork (-0n): backend fails fast at ComputeEphem with one clear
  message; the background connector does not run.
- Server gone mid-animation: frames continue from the cache until the
  window runs out, then objects fail soft while the reconnect loop works;
  when the server returns, the next window boundary heals everything.
  No dialog, no toast, until the user looks for status.

## 9. Testing

- In-process protocol tests in a new qttest suite group: envelope/payload
  encode-decode round trips against ephproto.h's static_asserts, REQUEST
  clamping to WELCOME limits, cache key identity (same request bytes →
  same key; any field difference → different key), grid quantization.
- Live loopback tests, the run-qt-tests.sh process-level pattern: launch a
  scratch astrolog-ephd on a scratch port (binary built by `make ephsrv`),
  drive the client backend through a cast, assert positions equal the
  local Swiss path bit-exact for the same settings (the same oracle the
  server's golden gate uses), and assert the reconnect ladder by killing
  the server mid-run. Skipped with a printed reason when the binary or a
  usable port is unavailable, so the suite stays green in bare
  environments.
- Offline determinism: -0n test asserts fail-fast without a single socket
  call.

## 10. Implementation increments

1. Backend slot + settings + Qt connection layer (state machine, WELCOME
   handling, reconnect ladder) behind the existing ComputeEphem structure:
   server-backend objects fail soft while the facade is cache-miss-only.
2. Prefetch hook + window cache + per-object reads; chart casts land
   bit-exact vs the local Swiss path.
3. Animation path: grid anchoring, background window prefetch, f32
   windows; pinned by a loopback animation test.
4. Required-server dialog + exit ladder; -0n semantics; status line.

Each increment lands green (build both binaries, suite) before the next.

## 11. Work log

(Entries append here, newest last.)

## Work log

1. **Increment 1 landed: the backend slot, the settings, the connection
   layer, and the fail-soft facade.** cmEphSrv=6 in the calculation-method
   enum, "Ephemeris Server" in szEphem[], -bS selecting it (console build
   accepts the spelling and says, once, what actually happens), -bW storing
   the address as a standalone -b suffix that does NOT fall through to the
   fEphemFiles toggle, us.szEphSrv with NULL meaning localhost on the
   protocol port, the settings writer line, and the rgsetfield row. The
   QWebSocket connection (qtdriver.cpp) runs the Disconnected → Connecting →
   Welcomed state machine with the 60s background tick before a session and
   the jittered 1s-doubling-to-60s ladder after a drop, re-sends the
   in-flight request verbatim, times out a HELLO without WELCOME at 15s,
   and answers app-level PINGs; Qt answers protocol pings by itself
   (lesson 4), so no heartbeat of our own exists. ComputeEphem dispatches
   fSrvPla = nSwissEph == 5 into FSrvPlanetQt(), which for increment 1
   fails soft once per cast with NO is.fNoEphFile latch (lesson 3). The
   required-server dialog and exit ladder are still increment 4.
2. **One bug found on integration**: the reader refused the empty -bW
   address the writer emits for the default, so every written settings
   file failed to reload (73 suite failures; empty now clears to default).
   Suite: 5649 passed, 0 failed, the ephem-server group's 75 checks
   included.
