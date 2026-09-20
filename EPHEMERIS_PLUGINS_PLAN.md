# Modular ephemeris: protocol version 4, source plugins, one way to choose a source

This document is the design record for three linked changes. First, a
**source-neutral wire protocol, version 4**, which both `astrolog-ephd` and
Ephemeris Prometheia's `prometheiad` speak natively. Second, **every ephemeris
source as a compiled-in plugin**, used through a fallback chain. Third, **one
way to select a source** from the command line, the settings file and an
Ephemeris Settings dialog in both GUI builds.

Section 3 is **normative**: it is the protocol specification. Once
implemented, `ephsrv/ephproto.h` is the byte-level authority, as it is for
version 3, and this section is the design authority behind it.

## Status — how to pick this back up

- **START HERE (2026-09-20, latest). Phase 6h steps 1, 2 and most of 3
  are LANDED. `ephhorizons.cpp` exists and is a registered source. ONE
  piece is left and it is named below: ComputeEphem() still FETCHES
  through its own inline branch rather than through the chain walk.**

  **What step 3 landed.** `ephhorizons.cpp` implements the full
  `EPHSRCDEF` — availability, capabilities (`fBody`, `fSpeeds`,
  `fGeoUncorrected`), state, submit, read, lookup — and sits in
  `rgephsrc[]` between `server` and `none`. `horizons` is gone from
  `rgszEphSrcFuture[]`, so `-bE horizons` now names a real source rather
  than a key the parser tolerates, and the transitional clause in
  `FEphGeoUncorrected()` is deleted: it is an ordinary capability read.

  **`NEphHorizonsId()` is now the ONE object→target mapping.**
  `ComputeEphem()` carried four near-copies of it, and they were not
  quite copies — **two of them also counted the Earth**, which a reader
  comparing them had to notice. They are one call now, with the
  difference passed as `fEarthToo` and explained where it is used.

  **`GetJPLHorizonsAt()` takes its instant and site as an argument**, and
  `GetJPLHorizons()` is a wrapper passing `ciCore`. `CiFromJulianEph()`
  converts the query's Julian day back into a `CI`, and the conversion
  has a half-day offset in it, because Astrolog counts a day from
  midnight where `MdyToJulian()` returns the noon-based integer.

  **THE PIECE THAT IS LEFT, and why it was not done in the same commit.**
  `ComputeEphem()`'s object loop still excludes this source's objects
  from the `EPHQUERY` and fetches them inline. Routing them through the
  chain is the last of step 3, and it is **not** additive:

  - It changes **when the program reaches the network.** Today a custom
    slot of Swiss type 4 fetches from JPL even when the chain says
    `swiss` — the selection does not mention the network and the program
    uses it anyway. Under the chain model the user would have to name
    `horizons`. That is arguably the better behaviour and it is
    certainly a **user-visible change**, so it is the maintainer's.
  - **No gate here can see it.** Not one of the four matrices reaches
    the network, and neither does the suite. The change would be made,
    gated green, and unverified.

  So the duplicate that remains is a routing decision, not a second copy
  of any arithmetic: the mapping, the instant, the frame and the
  re-centring all have exactly one implementation now.

  **How this was verified.** Four matrices byte-identical against
  `022b0a4` (chart 7568, switch 119691, influence 762, graphics 579),
  `make check` 6176/0. Three new offline checks in the suite's
  `horizons` group, over the recorded corpus and with no network: the
  query's instant reproduces the cast's own UT moment across four
  epochs, the id mapping serves what the inline branch served and
  refuses the Earth, and a barycentric Sun asks for target **0** — which
  is why the "not served" sentinel is not 0. The instant check is
  sabotage-proven: dropping the half-day offset moves every date by one
  day and every time by twelve hours.

  **The suite had been written to fail at this exact moment**, and did:
  *"horizons is not registered yet — when its plugin lands the registry
  supplies the key, so drop it from `rgszEphSrcFuture[]`"*. Three
  assertions needed updating and all three were about the registry
  changing shape, not about behaviour. `vcxproj_audit` caught the
  missing MSVC project entry in the same run.

- **Superseded (2026-09-20, later). Phase 6h steps 1 and 2 are LANDED
  (`df8a565`).**

  Step 2 was the whole of 6h's risk and it is spent. What landed:

  - **`EPHCAPS` has `fGeoUncorrected`**, and `FEphGeoUncorrected()`
    (`ephem.cpp`) reads it. `ComputeEphem()` used to ask "is the chain's
    head spelled `horizons`" five separate times, each phrased in the
    negative. It asks the capability once now.
  - **`EphEmulateGeoRows()` (`calc.cpp`) is the re-centring**, host-owned,
    over the whole row set after the loop. Inside the per-object loop it
    worked only because `i` ascends with `oEar < oSun`; that is now stated
    rather than relied on, which is what lets a plugin answer rows in any
    order — what `FRead()` over a remote source actually does.
  - **`EPHGEOROWS` moved to `ephem.h`** so the suite can drive the
    emulation with no cast and no network.

  **The transitional clause to delete in step 3** is the last line of
  `FEphGeoUncorrected()`: `return FEqSz(sz, "horizons");`. It exists only
  because the key resolves to no registered source yet. Register
  `ephsrcHorizons` with `fGeoUncorrected` set, drop the key from
  `rgszEphSrcFuture[]` in `ephem.cpp`, and that line goes — the suite's
  own check (`the horizons chain head declares rows that are geocentric
  and light-time-uncorrected`) keeps answering either way, which is why
  it tests the capability and not the text.

  **How step 2 was verified, because the obvious gate cannot see it.**
  All four matrices are byte-identical against a baseline at `21eaee2` —
  and they would have been byte-identical against a rewrite of the whole
  emulation too, because **not one of the four reaches the network.** The
  real net is `TestHorizonsEmulationQt()` in `qttest.cpp`: it drives the
  recorded 1990 corpus through `EphEmulateGeoRows()` and requires
  agreement with a heliocentric Swiss cast of the same instant. Measured
  Venus 0.273′, Mars 0.211′, band 2′; the raw geocentric rows sit ~52°
  away, which is what makes the band mean something.

  **And the sixth gate blind to its own subject was caught here**, by
  sabotage rather than by shipping. The first version of that net left
  the Swiss cast's correct heliocentric Earth in `space[oEar]`, so
  **deleting the Earth arm entirely still passed** — the state that arm
  produces was already present. In a real Horizons cast the Earth is
  skipped outright (JPL refuses it as degenerate; `earth-1990` in the
  corpus is that refusal), so the test poisons `space[oEar]` first. With
  the poison in place, dropping the Earth arm gives 3242′ and dropping
  the re-centring gives 3109′.

- **Superseded (2026-09-20, earlier): the accuracy work is at a clean
  stop and every open item but one is closed.**

  **Read `### PHASE 6h` below and work-log item 22, in that order.** Item
  22 is the specification and its `calc.cpp` line references were
  re-verified on 2026-09-19. Steps 1 and 2 of it are now done; step 3's
  text there still stands.

  **Before touching anything: take a baseline binary.** `git worktree add`
  at HEAD, build `./astrolog-base`, and require all four matrices
  byte-identical against it afterwards. 6h refactors `ComputeEphem()`,
  which every chart in the program goes through, and a re-centring change
  that is right geocentrically and wrong heliocentrically shows in the
  **chart** matrix and in nothing else.

  **What landed on 2026-09-19/20**, eight commits `e2caa6e`..`97a18db`,
  all gated and pushed:

  | | |
  |---|---|
  | `e2caa6e` | the mean-apsis rate fix had reached the server and **not** the application; shared header `ephsrv/ephnodrate.h` |
  | `1539da9` | and the server did not call the header it was named for |
  | `96aef21` | the push address is the GitHub **noreply** one now |
  | `36b2f95` | `ratesApprox` names the objects concerned, not all of them |
  | `b631333` | the Moon's named points ignored a topocentric site — registry **2.9**, fixed in both halves |
  | `f390e2a` | two outside referees run: stars pass FK5, deflection cannot grade us |
  | `16303ef` | the rates bound gets headroom; the sweep gets the site that found it |
  | `97a18db` | open item 3 closed, and its diagnosis had been wrong |

  **A daemon may still be listening on 127.0.0.1:47392** for the
  Prometheia cross-test (`tools/ephsrv-serve.sh 47392`). It is not needed
  for 6h. Stop it with the pid in `/nvm/work/ephd47392.pid`, never
  `pkill`.

  **Two things are owed outward and neither blocks 6h.** A reply may
  arrive from the Prometheia session about (a) whether a **geocentric**
  deflection leg is cheap for them — the largest claim we make that no
  textbook has ever refereed, see registry 2.3 — and (b) the AU-unit
  question, their bound being per-AU where §3.5a's is absolute.

  **The lesson this fortnight actually taught**, because it is the thing
  most likely to bite 6h too: **four separate defects were a fix landing
  in one of two places.** The plane-2 origin (stars vs planets), the
  mean-apsis rates (server vs application), the shared header that only
  one caller used, and the topocentric named points (both again). Whenever
  a change touches `ephsrv/` arithmetic, ask what `calc.cpp` does with the
  same question, and the reverse. Three shared headers now exist for
  exactly this reason — `ephsidplane.h`, `ephstarorb.h`, `ephnodrate.h` —
  and the rule is that arithmetic two copies must agree on lives in one
  file rather than in two disciplined ones.

- **FOR THE MAINTAINER: this branch is ready for your squash decision,
  with three things named rather than buried.**

  **What it does.** One way to choose an ephemeris source, from the
  command line (`-bE`/`-bP`), the settings file and an Ephemeris Settings
  dialog in both builds; every source a plugin behind one registry with
  an ordered fallback chain; protocol version 4, locked and verified from
  both ends. The seven legacy selection fields are gone, and so is the
  startup nag and its exit code.

  **What is NOT done, and why.** The console and WinHTTP transports are
  unwritten: work, not problems, and low value while Qt is the shipped
  interface on every platform. The `horizons` plugin is unwritten too,
  but it is **no longer blocked**: on your instruction it was tested
  against the live service, once, and the result is a committed corpus of
  20 recorded replies plus an offline replay harness, so the rewrite can
  now be developed and regression-tested without a network. Its remaining
  difficulty is honest and named -- the re-centring is cast-level vector
  arithmetic that five sites in `ComputeEphem()` are written in the
  negative to accommodate -- and work-log item 22 specifies the three
  steps that resolve it. **Getting there found four defects**, including
  a one-minute error in every position Horizons has ever returned to
  Astrolog.

  **What phase 8 found, because it bears on how much to trust the rest.**
  Three delegated reviews found THIRTEEN defects -- one a crashing
  out-of-bounds write, several silent wrong answers, including a Matrix
  selection that drew 0Ari00'00" for every body, a heliocentric chart
  that computed the lunar node heliocentrically, and fixed stars 24.7
  degrees out in every sidereal chart. All are fixed, each with a net
  proven to fail without its fix -- except P1's, which is named below
  because it is missing rather than because it is hard. Every one of the
  thirteen was in code that passed every gate at the time.

  **The third review's own lesson is the one to keep.** It was briefed to
  hunt what the EARLIER FIXES had broken, not to re-find what they fixed,
  and its worst finding was exactly that: D2's fix corrected what the
  server computes without correcting what the consumer does with the
  answer, and put every fixed star 24.7 degrees out in sidereal charts,
  silently, with `nErr` clear so the chain claimed the row. A review pass
  after a round of fixes is not ceremony.

  **And the gate itself was the tenth finding.** `make check` and the
  release workflow run the suite under `-Yi1 ephem`; `./run-qt-tests.sh`,
  the command this project documents, runs it under `-i nrvate.as`, which
  is what the hard rule names. Those are different configurations, only
  one is gated, and the ungated one was failing 20 assertions for most of
  this branch's life -- and is the one that reaches the crash. **That is
  worth a line in CLAUDE.md whatever you decide about this branch.**

  **That third review has now been done** (work-log item 21), every
  finding it raised is fixed, **and P1's missing net is closed**
  (work-log item 24): the suite's loopback daemon was started without a
  star catalogue because `sefstars.txt` is in the tree root rather than
  the bundled `ephem/`, and its `--ephe` is its whole search path. Nothing
  is now carried as outstanding from any of the three reviews.

  **Two gate-level findings are recorded here and deliberately NOT added
  to CLAUDE.md**, because that file is yours: (a) `make check` and the
  documented `./run-qt-tests.sh` run different configurations, and only
  one is gated; (b) the switch matrix could not see a switch whose effect
  depends on what came before it, which is now closed in the harness
  itself.

- **STATUS (2026-09-19, accuracy work; nothing structural changed).** The
  protocol, the plugins and the selection are where the entry below left
  them. What this day added was ACCURACY and the checks that hold it, in
  collaboration with Ephemeris Prometheia, whose cross-test found most of
  it. Ten commits, `13d3e5e`..`1b4e826`, all gated.

  **Landed.** Registry 2.4's binary-star orbits are applied
  (`ephsrv/ephstarorb.h`, shared by both Swiss copies like
  `ephsidplane.h`), agreeing with their independent engine to 0.40 mas on
  Sirius and 0.29 on alpha Cen A.

  **Five defects, each fixed with a net proven to fail without it.** The
  stars took Swiss's plane-2 origin while the planets took ours, 31.5"
  apart in one chart (`1ecb8b9`); the eleven instant-defined zodiacs
  reported a longitude that moved and a rate that did not (`46cf57d`);
  the Moon's orbit points were answered from another body's centre at
  impossible distances (`d3d8d76`); a retryable BUSY was treated as a
  dead window, so a large chart fell back to local Swiss and said so
  (`02bb758`); and the rates bound this server ADVERTISES was 1351 times
  too small (`429c764`).

  **Two gates could not see their own subject.** `ephsrv-golden.sh` had
  no fixed stars at all and now has sixteen legs plus the inverse
  assertion for the four with orbits (`3ff4cab`); and the configuration
  the documentation tells people to run was gated nowhere, which is where
  the BUSY defect had been hiding (`19f3cea`).

  **Three findings are Swiss's, not ours**, measured and recorded rather
  than fixed: registry 2.6, 2.7 and 2.8. The last generalises the other
  two -- Swiss's rate columns are the derivative of the geometric,
  mean-frame quantity while its positions are the transformed one -- and
  the nutation case is proved in closed form.

  **The mean-apsis rate columns are FIXED** (`d1f1287`, registry 1.5) --
  Swiss put the latitude in the latitude-rate slot and left the geocentric
  re-centring out of the longitude rate. The first response was to raise
  the advertised bound to 5 deg/day so the advertisement covered them, and
  to call the fix a maintainer decision because `ephsrv-golden.sh` pinned
  those columns. That was hiding behind a gate: a gate asserting the wrong
  thing is a second thing to fix, not a reason to serve a wrong number.
  **The maintainer's standing instruction, 2026-09-19, is to fix errors
  without asking.**

  ### OPEN ITEMS, so a fresh session does not have to find them again

  1. **CLOSED 2026-09-19, and it closed into a finding rather than a
     clean bill.** All four differential matrices were run against a
     baseline built at `e6c10d6` -- the commit before `calc.cpp`'s cast
     changed -- and all four came back **byte-identical**: chart 7,072
     lines, switch 119,691, influence 762, graphics 579.

     **That clean diff covered less than it looked like.** The change
     moves every `-Ys` star by 31.5" BY DESIGN, so a byte-identical
     result was the wrong answer to be happy about. It was byte-identical
     because **no leg of any of the four matrices listed a fixed star or
     cast `-Ys`** -- `grep -c ' -U'` and `Ys` over all four scripts
     returns zero outside two `-XU` graphics renders that are tropical.
     This is CLAUDE.md's own rule about an inert entry diffing to zero,
     and it is the third time in two days the same shape has turned up:
     `ephsrv-golden.sh` had no stars, `swetest-oracle.sh` still has none
     (item 2), and now the differentials had none either.

     **Closed by giving the chart matrix four legs it lacked**
     (`-b0 -U`, `-b0 -U -s`, `-b0 -U -s -Ys`, `-b0 -v -s -Ys`), and the
     legs are proven live by moving between the two binaries in exactly
     the designed quantity: 50 of 62 stars shift 31.00"-32.00" in the
     `-Ys` leg, and **no star moves in longitude tropically** -- the
     tropical change is the binary orbits in LATITUDE, 1" on Sirius and
     Procyon, with Vega and the other 59 untouched.

     **`-b0` is load-bearing on those legs, not decoration.** The
     binary-star offsets are sub-arcsecond to ~2", so at the arcminute
     resolution every other leg in the harness prints, Sirius and Procyon
     are byte-identical before and after their orbits were applied. The
     leg that catches registry 2.4 only catches it at seconds.

     The `-b0 -v -s -Ys` planet leg does **not** move, which is correct
     and is why it is kept: `1ecb8b9` made the stars agree with the
     planets, so the planets were already right, and the leg now guards
     that they stay that way.
  2. **Substantially answered 2026-09-19, by a better reference than the
     one this item asked for.** `tools/swetest-oracle.sh` still has no
     fixed-star legs, and an upstream `swetest` would not have helped much
     anyway: it is the same Swiss, so it could never see an error Swiss
     shares.

     Ephemeris Prometheia's `tools/check/stars_fk5.py` is the reference
     that can. It drives their wire client against any v4 server and
     compares against the **Basic FK5** (Fricke 1988, CDS I/149A) carried
     into the Hipparcos frame by ERFA's `fk52h` + `pmsafe` -- ground-based
     and pre-Hipparcos, so **genuinely not something Swiss produced**.

     Run against `astrolog-ephd` at `b631333`, 29 stars at 1900, 2000 and
     2100: **all within band, worst 0.559" (Antares)** against a 1.0"
     band. The band is an ESTIMATE, not a measurement -- the FK5's stated
     mean errors omit its system errors -- and that caveat is theirs and
     is repeated here so the result is not read as tighter than it is.

     Registry 2.4 shows up in it exactly as it should: Sirius, Procyon and
     Rigil Kentaurus are annotated "barycentre compared (X's orbit
     applied)", because we move the component and the FK5 lists the pair.
     Polaris and Achernar are named as binaries with no orbit applied.

     **CALLED, NOT PORTED**, per their maintainer's request, and it is not
     in `make check`: it needs their client, their venv
     (`pyerfa==2.0.1.5`) and their fetched FK5 tables. To re-run:

         cd /shares/ephemeris-prometheia && .venv-oracle/bin/python \
           tools/check/stars_fk5.py --client build/prometheia-wire-client \
           --server astrolog=<port>

     What remains genuinely open is the **local** path: this refereed
     `astrolog-ephd`, not `./astrolog`, and the two read the same
     corrected `sefstars.txt` but reach it by different code.
  3. **CLOSED 2026-09-19, and the diagnosis was wrong.** This said the
     topocentric lunar node's 4.05e-3 deg/day was Swiss's topocentric
     velocity model (registry 2.6) and that fixing it meant differencing
     every topocentric row -- four extra calls apiece, which an animation
     pays per frame. A cost decision.

     **It was neither.** It was registry 2.9: Swiss does not serve the
     Moon's NAMED points topocentrically and does not say so, returning
     the geocentric position beside a rate that had moved. Routing those
     to `swe_nod_aps` took it to **3.2e-7 deg/day**, four orders of
     magnitude, at no per-row cost at all.

     **What made the wrong diagnosis plausible is worth keeping.** The
     number really was the largest thing in the sweep, registry 2.6 really
     does describe a topocentric velocity defect of Swiss's, and the two
     fitted. Nothing in our own grid could separate "Swiss's topocentric
     rates are poor" from "this point is not topocentric at all" --
     because both produce a large rate miss, and a rate miss is all that
     grid measures. The separating question is whether the POSITION moves
     with the site, which nothing here asked until the other project
     asked it.

     The residual after the fix is the topocentric Moon *body* at
     8.2e-4 (now 1.7e-3 at Quito in 2100), and THAT one is genuinely
     registry 2.6.
  4. **CLOSED 2026-09-19.** `ratesApprox` was set on every object carrying
     speeds, where 3.5a says "the objects concerned" -- over-broad rather
     than wrong, and it cost the flag its meaning. Worse, it was being
     claimed about the one class of rate this server computes as a true
     derivative of the positions it answers: a **mean** node or apsis,
     differenced in `ephnodrate.h`. Measured over three observers at 1800,
     J2000 and 2026, every mean point misses by **0.000e+00** in all three
     columns while the bodies around them miss by 1e-6 to 5e-5 AU/day and a
     topocentric Moon by 8.2e-4 deg/day.

     The flag is now cleared for a mean point and put back per row by
     `ComputeObjectRows()` if the differencing bailed at an ephemeris edge,
     so it is true per object rather than per class. **Fixed stars keep it,
     and the measurement is why**: their angles are inside at 5.7e-6 deg/day
     but their DISTANCE rates miss by up to 1.5e-3 AU/day, a star's distance
     being parsecs and its rate a radial velocity.

     `tools/ephsrv-rates.sh` gained the gate, with two assertions that fail
     in opposite directions: **soundness** (nothing over the tolerance may be
     unflagged -- a wrong clear is a lie a client acts on) and
     **informativeness** (every mean point must be unflagged AND measure
     zero, or the flag rots back to always-on, which is sound and useless).
     Both were sabotage-proven; the second exists because always-on passes
     the first.
  5. **SENT 2026-09-19. Nothing is now owed to Ephemeris Prometheia.**
     Our leg inventory went back in two messages, split by what each
     leg's reference actually is, since that is the axis that decides
     whether a leg can catch a mistake both engines share:

     - *Outside this repository (3):* `swetest-oracle.sh` (upstream
       `swetest` at our own vendored version), `star-orbit-check.py`
       (ORB6's published ephemeris and `sefstars.txt`'s own α Cen
       separation), `crosstest-prom.sh` (them).
     - *Anchored on the same Swiss we compute with, so structurally
       blind to a Swiss-side error (2):* `ephsrv-golden.sh`, the
       `oracle` group.
     - *Self-consistent, no outside reference (1):* `ephsrv-rates.sh`.
     - *Protocol rather than numbers:* the 111 conformance fixtures.

     **And what we do NOT have, named rather than left as implied
     parity**: no Horizons leg; **no deflection leg at all**, though
     registry §2.3 measures it at up to 0.544"; no FK5 or
     ERFA-generated star fixtures; no JPL `testpo`; no per-observer
     orbit-point sweep, sidereal token sweep, refusal combinatorics or
     fuzzing.

     **The gap has one shape and it is worth stating plainly: almost
     every check we have is anchored on Swiss, so it grades our
     integration and not our physics.** Their engine is the only
     reference we hold that Swiss did not produce, which is why their
     cross-test found most of what was fixed this week. By the same
     argument the **deflection leg is the most valuable single thing
     either side could add** -- the largest measured error in the
     registry with nothing behind it.

     **The log join was NOT owed -- this entry was wrong when it was
     written.** `astrolog-ephd` has logged the request id since `943b74e`,
     at the default level, one line per request answered, and `evt=error`,
     `evt=cancel`, `evt=stall` and the LOOKUP line all carry `req=` too.
     Verified 2026-09-19 by running the daemon and reading the line back:

         ts=... level=info evt=req conn=1 loop=0 addr=127.0.0.1 req=1
         objs=2 rows=2 cells=4 profiles=1 ... cache=miss compute_ms=0.226

     `conn=` is there as well, which the join needs: a request id is the
     client's and is only unique within a connection.

     **Their side carries four more items as "waiting on Astrolog" that
     are all stale as of 2026-09-19** -- the binary-star offsets
     (`13d3e5e`), the 16 mean-perihelion `rates` rows (`d1f1287`), the 64
     `sidsweep` rows and the plane-2 offset (`1ecb8b9`, registry §4.1
     fixed 2026-09-18), and the 5 deg/day advertised speed error (now
     5e-3 deg/day and 1e-4 AU/day). Their note that the rates fix "is
     their maintainer's call because a golden test pins those columns" is
     the exact reasoning this project rejected: a gate asserting the
     wrong thing is a second thing to fix.
  5b. **CLOSED 2026-09-20 by measurement, and the diagnosis it had
     carried was wrong.** §3.5a now says the rate comparison is defined
     on **f64 positions**, which is one sentence and closes it for every
     object rather than for stars.

     **What it was recorded as:** "§3.5a's 1e-6 AU/day distance tolerance
     is below the f64 noise floor at a star's distance, so no
     implementation can meet it", with three candidate fixes -- a
     relative tolerance, exempting stars, or declaring a star's distance
     nominal. Both engines measured themselves failing it, stars only,
     which looked like confirmation.

     **What it actually is.** The floor argument was about an order of
     magnitude that is not reached. At f64 Polaris's five distances
     across the window are distinct and span **5071 ulp**, the central
     difference is clean, and the row is **under** the bound at 3.99e-4
     AU/day. The failures were all at **f32**, where the ULP of 2.7356e7
     AU is **3.26 AU** against a window change of 3.08e-5 AU: the column
     is bit-identical at all five instants, its central difference is
     exactly zero, and the "discrepancy" is the server's own rate with
     the sign removed. Prometheia's 7.4824e-3 AU/day falls inside the
     range the reported rate itself spans over those rows.

     **And it was never about stars.** At f32 the SUN's distance is
     constant over the same window, and Pluto's barycentre moves 8 ulp
     where at f64 it moves 4.5e9. Every object degrades two to three
     orders; a star is only where the scale ends.

     **`ratesAuPerDay` stays at 4e-3 for now.** The f64 star rows are
     genuinely ~1e-3 (Aldebaran 1.5154e-3) -- real, and separate from
     everything above. The solar-system-only figure is 9.0324e-5
     (Saturn's osculating aphelion, topocentric Quito, 2026), so a client
     that cares only about planets should read registry §2.8 rather than
     the advertisement. Whether a per-observer or per-kind bound is worth
     a capability field is a new question, not this one.

     **The method note, which is the part worth keeping.** The wrong
     explanation was committed and pushed (`7b2acba`) on a peer's number
     plus a plausible physical story -- a pole star seen from the equator
     sits on the horizon -- without once querying our own server. One
     `eph_wsclient` call at two precisions settled it in five minutes.
     **A plausible mechanism is not evidence, and the more of the
     observation it explains, the less likely anyone is to check it.**

  6. **Closed, recorded so it is not re-checked**: their dataset id's
     digest changed on 2026-09-19 (file hashing in 8 MiB pieces). Nothing
     of ours pins it -- `ephsrv/conformance/welcome_prometheia.hex` is a
     synthetic "prometheiad-like" WELCOME, not their real identity.
  7. **Phase 6h** is deferred with its own pickup checklist (work-log item
     22).

- **STATUS (2026-09-18, phase 6 substantially landed).** Phases 2, 3, 4,
  5 and 7 are on this branch and gated, and phase 6 is most of the way
  through: **the server is a registered source reached through the chain,
  and ComputeEphem no longer knows it exists.** What remains of 6 is
  platform reach and one more plugin, not the mechanism.

  | increment | state |
  |---|---|
  | 6a `ephserver.cpp`, the source and the transport interface | landed `176e333` |
  | 6b `ephreq.h`, the one EPHQUERY-to-REQUEST translation | landed `2bf30e5` |
  | 6c the Qt transport, bound at startup | landed `c1c221b` |
  | 6d ComputeEphem's two `#ifdef QT` server branches deleted | landed `6ed60d5` |
  | 6e the required-server dialog, its ladder and exit 86 deleted | landed `05a0c21` |
  | 6f the console transport (`eph_wsclient.cpp`'s framing, reusable) | **DECLINED 2026-09-18 by the maintainer.** Not an omission: Qt is the shipped interface on every platform, and the console build is the CLI and the matrices' oracle, where nobody has asked to reach a remote ephemeris. The extraction cost is real -- the framing is in a PROGRAM, not a library, and ten gate scripts drive that program |
  | 6g the WinHTTP transport (Win32) | **DECLINED 2026-09-18 by the maintainer**, same reasoning, and it additionally needs a Windows runner to test, which is the slowest loop in this project |
  | 6h the `horizons` plugin | **STEPS 1-3 LANDED 2026-09-20; one routing decision is left, and it is the maintainer's.** `ephhorizons.cpp` is a registered source with `fGeoUncorrected`; `NEphHorizonsId()` is the one object-to-target mapping where `ComputeEphem()` had four near-copies (two of which also counted the Earth); `GetJPLHorizonsAt()` takes its instant rather than reading `ciCore`. What is NOT done is routing the fetch through the chain walk, because that changes when the program reaches the network -- a custom slot of Swiss type 4 fetches from JPL today even under a `swiss` chain -- and no gate here can see a network path. See the Status block above. Previously: **STEPS 1-2 LANDED 2026-09-20 (`df8a565`); step 3 is what remains.** The capability (`EPHCAPS fGeoUncorrected`) and the host-owned re-centring (`EphEmulateGeoRows()`) are in, with an offline net over the recorded corpus and both arms sabotage-proven; the four matrices cannot see any of it, which is recorded in the Status block above. What is left is the plugin itself: one request per body, three instants each, `ephErrOutsideCover` per object from the recorded boundaries, Earth never asked for. Previously: **DEFERRED by the maintainer 2026-09-19 -- not declined, and not started.** The seam, the recorded corpus and the offline replay harness are landed (work-log item 22), and the rewrite is specified there in three steps, with a pickup checklist for a fresh session. It buys the chain, the fallback and provenance rather than function: `GetJPLHorizons()` already casts charts, with the one-minute error fixed. Its real content is a refactor of `ComputeEphem()`'s core, so it wants a baseline binary and the four matrices, not the end of a long session. Four defects were found getting here, one a one-minute error in every position Horizons has ever returned |

  Phase 8's three reviews are done and their thirteen findings fixed
  (work-log items 19-21). **Nothing else on this branch is implementable
  from here**: what is left is one large extraction, one low-value port
  and one rewrite that cannot be verified without a network.

  **The live parity group casts through the chain bit-identically to the
  local path**, which is the check that says the mechanism works rather
  than merely compiles.

  Phase 8 (branch review) and the maintainer's squash-to-qt decision
  follow. The branch is 80 commits ahead of `qt`; each phase kept its
  own dev branch, so it can be reviewed in pieces rather than as one
  diff.

- **Where 6f-6h stand, so nobody re-surveys.** `eph_wsclient.cpp` is a
  standalone PROGRAM with `main()`, not a library, and its socket and
  WebSocket framing are file-static -- so 6f is an extraction before it
  (both are DECLINED as of 2026-09-18; the survey below is kept because a
  decision needs its evidence, not because the work is queued)
  is a transport, and ten gate scripts drive that program and must not
  regress. 6g is low value while Qt is the shipped Windows interface and
  the Win32 build is only the oracle. 6h is the larger one: the Horizons
  special case is woven through FIVE sites in `ComputeEphem()`
  (`fJPL`/`FJPL`), including a post-processing step at the read that
  re-centres a geocentric answer -- so the plugin has to take that with
  it, which is what makes it a rewrite rather than a move.

- **corrapplied.py has been run against `astrolog-ephd`, and the result
  needs reading carefully.** It printed OK and exited 0 having checked
  **zero of 29 cases**. Not our defect and not theirs in the numbers: of
  the eight correction masks, Swiss cannot express the three that ask for
  a correction WITHOUT light time (there is no flag for it), so we
  advertise five and refuse three, the tool asks for ones we refuse, and
  every case skips. The per-case reporting is right; the verdict line on
  top is the bug, and it is the vacuous pass their own notes warn about.
  Raised with them. **Do not treat a green from that tool as coverage
  until it fails on nothing-checked.**

- **The 00:20 handoff block below is SUPERSEDED, and one of its central
  claims is DISPROVEN.** It attributed phase 4c's 14 red assertions to a
  sticky `tid_acc` leak in the Swiss fork and said the fix belonged in
  `/shares/swisseph`, gated by that fork's golden suite. That is wrong,
  and nothing in the fork was changed. **The fork is not linked into any
  Astrolog binary** -- the Qt, console and Windows builds all compile the
  VENDORED upstream Swiss 2.10.03 (`sweph.cpp`, `swephlib.cpp` and
  siblings in this tree); `/shares/swisseph` is built only into
  `astrolog-ephd`. So no change there could have caused or cured it.

  Kept rather than deleted, per the keep-full-record rule, and flagged
  here rather than only in the work log because a wrong-but-specific root
  cause in a handoff is worse than none: it is confident enough to stop
  the next reader looking. (Raised from the Prometheia side, who had the
  same class of error the same day and said so first.)

- **What the 14 assertions actually were.** The library reads its tidal
  acceleration from the moon file's DE number, and with no file open
  `swi_get_tid_acc()` falls through to `SE_TIDAL_DEFAULT` (DE431, -25.80)
  instead of the bundled `ephem/`'s own (DE441, -25.936). The two differ
  by 0.136 -- these are tidal accelerations in arcsec/cy^2, not seconds,
  and their Delta-T consequence at 1900 is 0.037 s, through a correction
  that goes as the square of the offset from 1955. Astrolog caches Delta-T
  in `is.rDeltaT` keyed on the DATE ALONE, so a value computed before the
  ephemeris path was set outlived the path change and the bodies reused
  it while `swe_calc_ut()` recomputed on DE441. Every body came out by
  its own motion over 0.037 s -- the Moon 0.018", the Sun 0.0015",
  Mercury 0.00003" -- which is why it read as one epoch and fourteen
  bodies rather than as an ephemeris fault. Fixed in `2fe2719` at two
  sites (`SwissEnsurePath()` drops the cached offset when it sets the
  path; `SwissHouse()` ensures the path before asking for Delta-T, being
  the one site of four that did not), each pinned by its own assertion in
  the numeric oracle's leg 1b and each proven by removing it and watching
  only its own assertion fail.

  **The general shape, worth keeping:** a value derived from the
  ephemeris must not outlive the ephemeris it was derived from. Anything
  else cached off a Swiss answer has the same hazard.

- **Where things stand.** Phase 4 is merged. The merge resolved the
  pre-analysed collision in the predicted direction: 4c deletes the five
  legacy selection fields, phase 5 deletes the Windows
  Calculation-Settings code that assigned them, and **neither branch
  compiled for Windows on its own** -- `backend_parity_audit` and the
  warning audit's Windows leg were both red on `eph4sel` for that one
  structural reason, and both go green on the merge. Two further
  conflicts resolved: the Calculation-Settings combo and its suite group
  took phase 5's side (the combo is gone; Ephemeris Settings is the
  shipped surface), and the registry pin combined phase 7's index
  arithmetic with 4c's chain representation.

- **Review findings.** F1, F3, F6, F7, F8, F9, F10 closed. F2 closed in
  `2fe2719`, and the finding was worth less than it looked: the key list
  reads `rgephsrc[]` directly now, so registry/key drift is impossible by
  construction, and only the three section 4.2 keys whose plugins are
  later phases' are hand-written. Those are pinned both ways, so leaving
  one in `rgszEphSrcFuture[]` after its plugin lands fails rather than
  being remembered. F4 FALSIFIED -- do not re-try; Swiss has no
  corrections-off form for planetary nodes reachable from an Astrolog
  setting, 10.4761" measured. **F11 is the one still open**: both builds'
  Ephemeris Settings dialog declares `rgiepEphemParam[4]` beside a
  generated `cEphParam` of 6. Two of the four it does reach
  (`epServerUrl`, `epServerToken`) are inert until phase 6 wires the
  transports, so it is not a live defect -- but the fixed `4` will not
  grow with the generated count, and nothing says so.

- **Phase 6, and the peer.** Prometheia's `tools/check/corrapplied.py`
  (their `4052e29`) is written and waiting for a v4 daemon on our side;
  it compares one server with ITSELF, so it presumes nothing about either
  engine. Their `docs/CROSS-TEST.md` (`71d4f78`) drafts the cross-test
  plan and asks two questions of us. Settled with them and recorded on
  their side: protocol kinds 3 and 4 stay unadvertised, because
  Astrolog's hypotheticals ARE `seorbel.txt` -- the user's own elements
  are the definition of the body, so a server substituting its own would
  silently discard them and the chart would move when a fallback changed
  source. Two warnings from them worth keeping: an angular separation
  through `acos(dot)` returns exactly 0 below ~0.01", which is a false
  negative precisely at the deflection scale and reads as a clean pass
  (use `atan2(|a x b|, a.b)`); and a subset check that reads only the
  mask-0 answer is vacuous against a server that echoes the request.

- **A DROP IS DRAFTED AND AWAITS THE MAINTAINER: two §3.5a sentences**
  (`/nvm/work/ephv4-drop-planes/DROP.md`, drafted at `781b602`). Moves no
  artifact bytes. **The two parts are independent and can be approved
  separately**, and they cost opposite things:

  **Part A, the invariable plane's zero point.** §3.5a says the zero point
  is "carried onto" the plane and does not say how; the two servers differ
  by a constant 31.5 arcsec (30.4 under Lahiri) with the planes themselves
  coinciding to 0.03 arcsec. Prometheia's argument is that a sidereal zero
  point is a DIRECTION among the stars, so carrying that direction keeps
  what 0 degrees points at whichever plane it is counted along. **Adopting
  it moves every body in an Astrolog solar-system-plane chart by 31.5
  arcsec** -- the first user-visible number in this effort changed without
  a defect behind it, since both readings are defensible and one is being
  chosen.

  **Part B, what a frame changes about a node.** The text says a node lies
  on the ecliptic of the profile's frame; this server computes it on the
  ecliptic of DATE and rotates, which is up to 10 arcsec of latitude off in
  a J2000 frame. The amendment says a frame changes the coordinates a point
  is given in, not which point it is. **This costs us nothing and excuses
  what we already do** -- Prometheia conform today and would change -- so
  it is put forward on the argument alone, and if the maintainer would
  rather conform to the text as written, that work is ours.

- **A DROP WAS DRAFTED AND WITHDRAWN: the distance of a MEAN orbit point.**
  Worth keeping as a record of a wrong diagnosis, because it was wrong in
  an expensive direction.

  The cross-test measured the Moon's mean node 4.18 arcsec out between the
  two servers in a planet-centred chart, and traced it to the DISTANCE:
  ours 384,400 km, theirs 368,130. I read that as a SPEC gap -- two
  defensible conventions for a column §3.5a leaves undefined -- drafted a
  drop to define it, and argued that `astrolog-ephd` could not conform
  because it is bit-exact with Swiss.

  **Every step of that was wrong.** Swiss's own `swe_nod_aps` answers
  368,148.6 km, agreeing with Prometheia to 18 km, which is the
  DE440-against-refit floor. Swiss simply answers the question TWICE --
  the named `SE_MEAN_NODE` body carries the mean distance constant with
  zero rates -- and this project had chosen the degenerate one. There was
  no convention to define, no upstream divergence to carry, and the fix
  was ours to make: commit `477ad49`.

  **The lesson is about which way the suspicion ran.** "Our server is
  bit-exact with Swiss, so a disagreement with another engine is the other
  engine's or the spec's" is a comfortable inference and it was false
  here: we were bit-exact with the WRONG ONE OF SWISS'S OWN TWO ANSWERS.
  Checking what Swiss actually returns took one probe and would have
  saved a drop, a wrong recommendation to the maintainer, and a message
  to the other project asserting a gap that did not exist.

- **A NAMED DROP IS CLOSED: correction masks per object KIND**
  (`/nvm/work/ephv4-drop-corrkind/DROP.md`, cut at `050a5a4`, implemented
  at `eed6429`, completed at `472b21a`). **Verdicted by Prometheia 99/99**,
  `set-sha256 d904e358…` independently computed, `JUDGEMENTS.tsv` 7/7
  through their own section-2 judge, and live on both servers: their
  refusal leg checks every (observer, kind, mask) for bodies AND orbit
  points, **160/160**, fault-injected both ways -- 48 findings if every
  mask is treated as listed, exactly six misjudged pairs if `0x0014` is
  ignored. The "permissive by decision" divergence is gone from the
  cross-test table, which is what the drop was for.

  **Three things this drop found that were not its subject.** The fixture
  harness had never validated a capability TLV's PAYLOAD -- WELCOME stores
  them raw -- so any tag could have carried any bytes and the set would
  have passed; closed for every tag. `JUDGEMENTS.tsv` exists because a
  standalone message fixture cannot render a verdict that depends on the
  server's capabilities, which is a hole the format had from the start.
  And **the cut was short one fixture**: the drop promised the
  `representation = 1` twin and the commit did not contain it, reported as
  present without counting, found by Prometheia counting the files against
  the nine cases.

- **The drop, as proposed and reviewed:**
  (`/nvm/work/ephv4-drop-corrkind/DROP.md`, cut 2026-09-18 at `050a5a4`),
  approved by the maintainer and **sent to Prometheia for review before
  anything is built** -- deliberately, because their implementer found the
  kind-4 `nTerms` flaw by reading the text before either side had a codec,
  which was far cheaper than finding it in a results table.

  **Unlike the kind-4 drop this one MOVES BYTES**: `ephproto.h` gains an
  enumerator, a `Capabilities` field and its codec, `registries.json` gains
  a row, and `conformance/`'s `set-sha256` changes. Nothing already on the
  wire changes meaning and every existing fixture stays valid.

  **Why.** A correction-mask entry is keyed on the OBSERVER (A.3 tag
  0x0004), but the behaviour depends on the object KIND, and the two
  disagree in both directions: at the Sun's centre a BODY answers masks 1,
  3, 5 and 7 identically, while an ORBIT POINT from the same observer
  genuinely honours them. One entry cannot say both -- and this project hit
  the consequence from both sides in one day, first advertising the wide
  reading (which made Prometheia's harness report a 3.2 arcsec
  disagreement that was really us over-promising) and then enforcing the
  narrow one (which made the heliocentric orbit point unaskable, caught by
  `ephsrv-golden` on the next run). The branch currently carries a
  deliberate divergence -- advertise narrow, accept wide -- which works and
  is true only until someone reads the spec instead of our source.

  **The proposal:** a new non-critical TLV `0x0014`
  `CORRECTIONS_BY_KIND`, `{u32 observerMask, u32 kindMask, u8
  correctionMask}`; `0x0004` keeps its encoding and becomes the
  INTERSECTION over kinds rather than the union, because it is what a
  client that has never heard of `0x0014` will trust, and advertised is
  promised.

  **Revisions 2 and 3 (2026-09-18) after two Prometheia reviews.** The
  second found an error in the FIRST review's own answer, which this
  project had then written into normative text without checking: revision 2
  said a REQUEST carries segment series that reference profiles, and it
  does not -- only `Object` carries a profile index, and `AyanSeries`
  exists solely in the SEGDATA reply. **A reviewer's question is a
  question, and turning one into a rule without opening the header is how
  a specification acquires something that cannot be implemented.**

  Revision 2's four gaps. The one that would have cost an implementation round:
  **where ERROR 11 is decided.** The mask sits on the PROFILE and the kind
  sits on the objects referencing it, so a profile can no longer be judged
  alone -- it is now checked against every (observer, kind) pair of the
  objects and series referencing it, refused whole, with an unreferenced
  profile checked against `0x0004`. Also: an unnamed pair falls back to
  `0x0004`, so a server lists only exceptions; "serves" then needs no
  definition; and the contradiction case they asked for a rule about was
  instead made **unrepresentable** -- `0x0014` can only ADD, so the two
  tags cannot disagree.

  **Neither implementer's existing WELCOME changes.** Prometheia's engine
  is uniform across kinds, so they send no `0x0014` at all; ours under the
  intersection reading is byte-identical to today, and only the
  heliocentric-orbit-point row needs the new tag. A capability extension
  that is free for the implementation which does not need it is the test
  of whether the design is right.

- **A NAMED DROP IS OPEN: the kind-4 elements rule**
  (`/nvm/work/ephv4-drop-elements/DROP.md`, cut 2026-09-18 at `176e333`).
  §3.5a's "Elements (kind 4)" gains four normative sentences, approved by
  the maintainer. It **moves no bytes** -- the three locked artifacts are
  byte-identical to the corrApplied set and to `cf83dc9`, and kind 4's
  encoding is unchanged -- so it is a drop rather than an edit because §3
  is locked, not because the wire moved. Prometheia is building against
  it. The numeric fixture is generated (`tools/kind4-fixture.sh`,
  sha256 `121ab3c6f9bed858b3f8aa949843f8e045850680054ca11aa184e289b58b3e63`)
  and **cross-checked**: Prometheia produced the same four cases from
  their engine and from a separate textbook Kepler implementation, and
  every case agrees. The one residual is accounted for -- Swiss carries
  the Gaussian constant truncated to ten significant figures in degrees
  per day, 1.446e-12 relative, which enters only through the mean motion
  and reaches 0.3 mas only in the case contrived to accumulate 173
  orbits. Left alone on purpose: the constant is upstream Swiss's, and
  changing it would make this the only Swiss consumer that disagrees
  with the rest about where a Hamburg point is. A **fifth sentence** carries the durable
  half -- derive k from the radian value at full double precision rather
  than carrying a rounded decimal in your working units -- released as an
  amendment on the maintainer's approval, 2026-09-18. Prometheia has
  verdicted the fixture: it conforms, every row, and is a permanent gate
  on their side. The drop is closed. **§3.5a itself is not edited in this
  document until the drop is verdicted**, which is the rule the approval
  condition exists to enforce.

  The substance, because it is the kind of thing that gets lost: kind 4's
  mean anomaly was ambiguous in a way that put two conforming servers
  DEGREES apart a century from epoch, with both reporting `corrApplied`
  truthfully and nothing on the wire catching it. And §3.5a's "mu = GM of
  the centre from the ephemeris's constants" was simply wrong -- it is
  the Gaussian constant, which is also the right answer, because a kind-4
  answer must be a function of the elements the client sent and not of
  which ephemeris the server happens to have open.

- **Scratch that must survive (do not delete):** `/nvm/work/ephv4*`
  (review.md, the mail file, the verdicts), `eph4sel`, `eph4base` (the
  baseline binary for matrices), `eph4verify`, `tidprobe*.c`, the
  `eph4-mx-*` artifacts.

- **`EPHEMERIS_ACCURACY_REGISTRY.md` is where the fork's numbers are
  judged**, new on 2026-09-18. Being bit-exact with Swiss is a MEANS, not
  the goal: a differential can only say "unchanged", never "correct". The
  registry records, with measurements, where this fork is deliberately
  more correct than Swiss (two lunar node distances so far), where Swiss
  is known wrong and this fork still follows it (three, all reaching the
  desktop), and one divergence declined on purpose. **Read it before
  "fixing" a number to agree with Swiss.**

- **House rules that bind whoever picks this up:** locked artifacts
  (`ephsrv/ephproto.h`, `registries.json`, `conformance/`) are
  byte-untouchable -- a change is a new named drop, never an edit.
  `/shares/swisseph` is additive-only and has its own gates.
  `/shares/ephemeris-prometheia` is read-only. `nrvate.as` is the
  maintainer's. Scratch in `/nvm/work`, never `/tmp`. `-j4` cap (NAS).
  `env -u DISPLAY` for binaries. No `pkill -f astrolog` -- stop by PID.
  No `Claude-Session:` lines in commits; `Co-Authored-By:` fine; new
  commits never amend; subjects one short sentence. Commits on ephv4
  push immediately; the branch squashes to qt only when the maintainer
  says so.

---
## Status — superseded handoff blocks (kept; see the Status above)

- **SUPERSEDED, and its root cause DISPROVEN** -- see the Status
  above. Kept whole rather than corrected in place, so the reasoning
  that was wrong stays legible beside what replaced it.

- **HANDOFF (2026-09-18 ~00:20, from the GLM-5.3 coordinator that is
  being stood down).** Read this whole block before touching anything.
  The honest state: the branch is sound below the neck and broken at
  one known joint; every break is localized, measured and written down
  below. Nothing on this branch is speculative — everything either has
  a green gate behind it or a written reason it is red.

- **Where things stand.** ephv4 head `c3c3d7c` (pushed; clean tree).
  On it, complete and gated: the locked 3 protocol; the v4 server and
  client (phase 2); the source registry (phase 3, a4c6b89); the
  Prometheia plugin (phase 7, ed6e4f2..dc05d07 incl. the re-pinned
  Jupiter-node courtesy legs, the PROMETHEIA stamp, work-log items
  renumbered 10-12); the review fix pass (8950fbd: lunar-named-point
  helio/bary refusal, pin wiring 0x8001/0x8003, plugin DeltaT -Yz0
  leg, raw star rows, client DeltaT cap, fixture-digest check, console
  Makefile stamp, Status sha corrections); and the Ephemeris Settings
  dialog (phase 5, c3c3d7c merge: both builds, suite 5944/0,
  Calculation lost the combo). Verified independently at each step —
  matrices byte-identical against baselines, locked artifacts
  untouched throughout. The blocks below this one (the 21:00 handoff,
  the phase-7 merge record, the phase-4 withdrawal) are SUPERSEDED by
  this block where they differ — kept per the keep-full-record rule.

- **THE ONE BREAKING THING — phase 4c, uncommitted, in worktree
  `/nvm/work/eph4sel` (branch eph4sel, based on 6f7eef1 = 4a+4b,
  which ARE committed and green).** Its code is complete and compiles
  warning-clean: the chain walk reads `us.szEphemSource`, `FCm*`
  predicates replaced by `FEphSpeeds()`/`FEphLegacyCast()`, old
  fields deleted with the US positional initializer migrated exactly,
  dialogs re-pointed, `-0b`/`-0n` inert, writer/astrolog.as migrated,
  and F1's save-reload net landed. Its 20-pass byte-identity battery
  (four matrices x five single-source selections, old-vs-new with each
  tree's own astrolog.as) is COMPLETE AND CLASSIFIED GREEN. What is
  red: the suite, 14 assertions, all in the numeric-oracle group's
  1900 epoch, and ONLY in group order registry-then-oracle. Each group
  alone is green; order matters.

- **The 14 failures are a Swiss-fork library bug, 90% diagnosed.
  Facts, all probe-verified, probes in `/nvm/work/tidprobe*.c`:**
  1. A `swe_calc_ut` under `SEFLG_MOSEPH` (any cast through the
     moshier source's borrowed bit) internally converts UT via a
     MOSEPH-flagged delta-t call, and `swi_set_tid_acc` SAVES the
     Moshier tidal acceleration into process-global `ctx->tid_acc`
     (fork: swephlib.c).
  2. The saved value is sticky. A later plain `swe_deltat()` at a
     FRESH jd — no flag transition in between — computes at the stale
     term (probe11: deltat(1601-jd) = 0.001045927 stale vs 0.001038866
     correct, tid stays -25.58).
  3. Suite shape: the ephem-registry group's moshier walk poisons
     tid_acc; chart-list's dialog render (ten seeded 1601-era charts,
     house cusps -> plain swe_deltat at fresh jds) CONSUMES the poison
     and the wrong Delta T values get cached into `is.rDeltaT`; the
     numeric-oracle group's 1900 assertions then inherit the skew
     (0.0372 s = the DE404-vs-DE441 tidal split at 1900).
  4. THE ONE UNRESOLVED NUMBER: the instrumented suite prints the
     poisoned tid as **-25.80** (= SE_TIDAL_DE431, the DEFAULT) where
     my standalone probes produce **-25.58** (Moshier/DE404). So in
     the real suite the poison may be a `swi_set_tid_acc` call that
     landed on the `default:` branch (denum unresolved ->
     SE_TIDAL_DEFAULT = DE431 = -25.80), not the Moshier value. The
     first thing to do: instrument `swi_set_tid_acc` in the FORK
     (print tjd, iflag, denum on every call) and run the two-group
     pair (command below). That names the exact call site and its
     denum in one 30-second run.
  5. The fix belongs in the FORK (`/shares/swisseph`, commit 8c4a23a,
     additive-only by house rule): `swe_deltat_ex_r` must not let a
     cross-flavor saved tid_acc be consumed by a plain SWIEPH-flag
     delta-t — re-derive when the saved flavor mismatches the request,
     or make the save non-sticky. The fork's own tests/golden.c holds
     the net (`cov:order_moseph_*` is the adjacent precedent; the
     2026-09-16 review's F1/F5 in notes/REVIEW.md are the paper
     trail). Run the fork's gate standalone (its check-samples has a
     known -j flake, Error 127, passes serial).
  6. DO NOT "fix" this in the suite by reordering groups or warming
     casts — that masks a state leak the plan's no-silent-fallbacks
     rule wants actually closed. The acceptance pin once fixed:
     ephem-registry group then numeric-oracle group, no reordering,
     green, with a comment naming the fork sha.

- **Fast reproduction loop (all from /nvm/work/eph4sel):**
  - The failing pair, instrumented (probes are already in the tree's
    qttest.cpp, gated by ASTROLOG_DBG=1; they print DBGDELTA per
    group + tid):
    `env -u DISPLAY QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME= ASTROLOG_DBG=1 ASTROLOG_QT_TESTS=ephem-registry,chart-list,swiss-enumerate ./astrolog-qt-test -Yi1 ephem 2>&1 >/dev/null | grep DBG`
  - The 14 failures: `ASTROLOG_QT_TESTS=oracle` alone is green; the
    full suite shows 14 failed at the 1900 epoch. Suite driver:
    `timeout 900 tools/ci-run-suite.sh 840 <log> -Yi1 ephem`. Group
    filter is the ASTROLOG_QT_TESTS env var; group slugs from
    `ASTROLOG_QT_TESTS=list`. Binaries ALWAYS with
    `env -u DISPLAY QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME=`
    (the Gtk "cannot open display" trap ate an hour tonight).
  - Traps recorded the hard way: `make astrolog-qt-test` is a no-op,
    the target is `make qt-test`; suite slugs are hyphenated
    (`ephem-registry`, not the group's display name); the .as matrix
    trap — the baseline binary aborts its settings load on the new
    -bE line, so matrix old-vs-new runs need each tree's OWN
    astrolog.as over the shared root.

- **What to do with eph4sel's uncommitted tree:** 17 modified files of
  complete, coherent 4c work plus temporary DBG probe lines in
  qttest.cpp (8 hits, `getenv("ASTROLOG_DBG")`-gated — strip or keep,
  inert without the env var; the probe additions at lines ~13290,
  ~19514 and ~20400 are the only instrumentation). Backup copies the
  agent kept: `/nvm/work/calc.cpp.4c4` and siblings. Verify by
  reading, then gate in THIS order: the fork fix FIRST (previous
  bullet), then full `make check -j4` on eph4sel (all 35 steps; with
  the fork fixed it should go green in one pass), the battery re-run
  on the final tree (artifacts: /nvm/work/eph4-mx-*), the warning
  gate, ASan suite (`tools/asan-suite.sh` bare, timeout 1500 — the
  US initializer was rewritten, this is not optional). Then ONE
  commit on eph4sel (fold 4d in; it is effectively already there),
  push, and merge to ephv4.

- **THE MERGE COLLISION, pre-analyzed:** eph4sel branched at 6f7eef1;
  ephv4 has since gained the phase-7 merge and phase 5 (c3c3d7c).
  Both 4c and 5 edited qtdialog.cpp's Calculation-Settings combo
  region: 4c minimally (deleted-field reads removed so it compiles),
  5 fully (the combo's real replacement, gated 5944/0). RESOLUTION:
  take phase 5's side of that region — its dialog is the shipped
  surface; 4c's edit there was scaffolding. Everything else merges
  clean (the two trees' suite-group additions sit in different
  regions). After the merge: full `make check` on ephv4 once more,
  push.

- **Work-log items for phase 4: 8 of this document, items start at
  13** (10-12 are phase 7). Keep the maintainer's keep-full-record
  rule: append after the last item, never renumber landed ones.

- **Open review items, from /nvm/work/ephv4-review.md (read it — every
  finding has a disposition):** F1 landed in 4c's tree (verify it
  survives the commit), F2/F3 relayed to phase 4 (check the 4c commit
  carries them), F4 FALSIFIED (do not re-try: Swiss has no
  corrections-off form for planetary nodes; 10.4761" measured), F5
  two-line band re-land pending (the mask-6/7 legs' ceilings become
  bands, in ephv4's qttest.cpp leg 8), F10/F11 open (phase-5 dialog
  test coverage — the phase-5 agent landed its own 44-check dialog
  group, which may cover F10; F11's two unreachable parameters are
  epServerUrl/epServerToken, unreachable pending phase 6's
  transports).

- **Remaining plan, in order:** 4c lands (above) -> phase 6 (remote
  adapter/transports, server+horizons plugins, required-server dialog
  and exit 86 removed — unblocks the peer's armed corrApplied
  live-traffic check) -> phase 8 (branch review; the review doc is
  most of it already) -> the maintainer's squash-to-qt decision.
  Also outstanding, small: the server-side kind-4 wiring (the fork's
  `swe_calc_orbel_r` at 8c4a23a exists and is verified; the server
  end is unwritten).

- **Scratch that must survive (do not delete):** /nvm/work/ephv4*
  (drop, verdicts, mails, review.md), eph4sel, eph4base (the baseline
  binary for matrices), eph4verify, tidprobe*.c (the diagnosis
  probes), calc.cpp.4c4, the eph4-mx-* artifacts. The peer channel:
  /nvm/work/ephv4-mail-to-astrolog.md (their migration is complete;
  nothing owed until phase 6's traffic check).

- **House rules that bind whoever picks this up:** locked artifacts
  (`ephsrv/ephproto.h`, `registries.json`, `conformance/`) are
  byte-untouchable — a change is a new named drop, never an edit.
  `/shares/swisseph` is additive-only and has its own gates.
  `/shares/ephemeris-prometheia` is read-only. `nrvate.as` is the
  maintainer's. Scratch in /nvm/work, never /tmp. `-j4` cap (NAS).
  `env -u DISPLAY` for binaries. No `pkill -f astrolog` — stop by
  PID. No `Claude-Session:` lines in commits; `Co-Authored-By:` fine;
  new commits never amend; subjects one short sentence. Commits on
  ephv4 push immediately; the branch squashes to qt only when the
  maintainer says so.


- **HANDOFF BLOCK (2026-09-17 ~21:00, for the next agent).** The live state,
  all of it:
  - **ephv4 head `5ac12d2`** (dates corrected; everything happened the 17th;
    this line moves with each Status commit).
    Phase 3 is DONE and merged. **Phase 7 is MERGED (2026-09-17 ~21:15):
    the peer answered in the mail file** — the orbit-point corrections are
    intended, settled jointly for v4, recorded in their engine commit
    `cf889eb` ("Nodes and apsides answer in the frame they are compared
    against") and docs/ORBIT-POINTS.md; their two smaller items they fixed
    themselves (`fa4e0b3`: the stale ENGINE.md sentence and the
    `prometheia.pc` prefix — the build-tree .pc now works bare, validated
    live by our rebuild; our scratch .pc is retired). The merge did the
    checklist: both Jupiter-node legs **re-pinned as §3.5a courtesy tier**
    (`1fe84a8`: measured 10.4750"/10.4834" masks 6/7 against their engine
    at `df0ae42`, tolerance 11", the two masks 0.008" apart); the
    **PROMETHEIA stamp** landed (`dc05d07`: each opting-in makefile stamps
    its object dir with the plugin flags; a flip removes
    ephprom/ephem/qttest objects before make considers them — falsified
    both directions); the **work-log items renumbered** (`ed6e4f2`:
    phase 7's are 10-12, phase 3 owns 6-9; the phase-7 block sits
    ascending inside the log's descending run -- a reader must rely on
    the numbers, not the position).
    Gates on the merged tree: default `make check` all clear (suite
    5834/0), plugin-on Prometheia group 143/0, ephsrv/ byte-identical
    throughout. Branch `eph7prom` was rebased onto ephv4 for the merge
    and force-pushed; its worktree can be removed when convenient.
    Nothing is owed the peer until phase 6's live traffic check.
  - **Phase 7 review findings (main agent):** the plugin had mapped Swiss
    TRUEPOS → `kCorrLightTime`; the protocol's mask **0** is TRUEPOS
    (`ephswiss.h`'s own mapping) — fixed at both conversion sites in
    `95fae2d`, netted (mask-0 binding check + spec-carries-TRUEPOS check).
    Everything else verified clean: the `is.rSid` subtract/re-add
    convention, topo west→east negation, per-object walk semantics, stable
    registry indexes. Default `make check` on eph7prom all clear; the
    plugin-on Prometheia group is 140/2 (the two held legs only).
  - **Phase 4: the background agent was NEVER dead — the "died" call was
    wrong and is withdrawn.** `10f8723` misread a 7-minute API gap
    (20:31-20:38) as death; the successor session had declared the agent
    gone on the same evidence. It is alive and working in
    `/nvm/work/eph4sel` (branch `eph4sel` off `a4c6b89`), mid-4b grown
    toward 4c: `c53172e` landed (increment 4a whole plus 4b's parse side
    and 4d's writer: `us.szEphemSource` + `us.rgszEphParam[]`, generated
    `ephparam.h`, `-bE`/`-bP` with the legacy shadow kept in step both
    ways, the writer emitting `-bE` + one `-bP` per non-default,
    astrolog.as; self-attested suite 5751/0 and the four matrices
    byte-identical against the a4c6b89 baseline — that binary is still
    built at `/nvm/work/eph4base/astrolog`), and the uncommitted set now
    runs to nine files (the 4b tail plus `ephem.cpp`/`ephem.h`/
    `extern.h`/`switch.cpp`). **No other agent works in
    /nvm/work/eph4sel — not editing, not building, not committing.** A
    foreign `make check` ran through the tree 20:35-20:39 while the
    owner was in its API gap; gate results from that window are
    untrusted, and the owner has been warned. The "finish 4b"
    instructions in `10f8723` are withdrawn — the owner lands its own
    increments and reports; THEN verify per the original recipe
    (re-read the diffs, re-run the four matrix byte-diffs per
    single-source selection against eph4base with each tree's own path
    normalized inside outputs, `make check` all clear) and ff-merge into
    ephv4. §8 work-log items for phase 4 start at 9 (eph4sel branched
    after phase 3's items 6-8 — no renumbering needed). The owner's
    full original instructions are recoverable from
    `/nvmraid/home/n/.zcode/cli/rollout/model-io-sess_subagent_agent_5825b6be-cf37-446f-9744-305a2c510a0a.jsonl`,
    line 1, message 3. **Phase 5 (the dialog, both builds) fans out once
    4c merges; phase 6 after 4; phase 8 (branch review) last.**
  - **The peer** (Ephemeris Prometheia, repo `/shares/ephemeris-prometheia`,
    session channel the mail file above): their v4 migration is COMPLETE
    including SEGDATA/CANCEL/priority; nothing is owed in either direction;
    their pre-agreed armed action is checking truthful `corrApplied`
    against live `astrolog-ephd` traffic when our phase 6 lands. Their
    parked items (Vondrák 2011, zstd, the deadlineMs strategy switch) wait
    on the maintainer. Two of their items ride my finding in the mail file
    (stale "orbit points are geometric" sentence in their docs/ENGINE.md;
    their `build/prometheia.pc` prefix is broken). Their progress letter is
    section 2 of the same file; my finding is the last section — the
    maintainer's relay of it is pending.
  - **The Swiss fork** (`/shares/swisseph`, commit `8c4a23a`): gained
    `swe_calc_orbel_r` (elements→positions, strictly additive, 608
    insertions; golden re-verified bit-exact 149/149). Kind-4 serving
    notes for the server side: μ is fixed to KGAUSS²/KGAUSS_EARTH²
    regardless of SEFLG_EPHMASK; wire centre 0/1 and A.16 equinox values
    map by identity; polynomial evaluation stays server-side; `ratesApprox`
    on kind-4 rows with speeds. The server's kind-4 wiring is still
    unwritten — a natural small increment when wanted.
  - Scratch (not in git, do not delete): `/nvm/work/ephv4-drop/`,
    `ephv4-drop-verdicts.md`, `ephv4-mail-to-astrolog.md`,
    `ephv4-mail-to-prometheia.md`, `ephv4-handoff.md` (narrative),
    `/nvm/work/eph7prom-scratch/` (the corrected `prometheia.pc`), the
    probe sources `corrprobe.c`, `orbelprobe*`.

- **Where.** Branch `ephv4`, branched from `qt` at d9642c0, in worktree
  `/nvm/work/ephv4`. Each pass lands as one commit on `ephv4`. The whole branch
  is squashed into `qt` when the maintainer says so; never before.
- **Approved plan.** `/nvmraid/home/n/.claude/plans/reactive-percolating-prism.md`
  (2026-09-17). This document supersedes it wherever they differ.
- **Phases.** See §7. The work log (§8) says which are done. **Phase 3 is
  done** (work log items 6-8: the registry, ComputeEphem's Swiss branch, the
  side calls, the fallback chain; merged to this branch, matrices
  byte-identical, suite 5834/0). Phase 2 is done,
  and work log item 3 cleared what it deferred except **segments** (no fitter;
  the capability is not advertised); **orbital elements** is unblocked — the
  fork gained `swe_calc_orbel_r` (8c4a23a), so kind 4 can be served when the
  server reaches it. `cancel` IS
  advertised now: requests are computed in blocks across loop turns.
- **Prometheia.** `/shares/ephemeris-prometheia` pins `ephproto.h`,
  `ephsrv/registries.json` and the conformance fixtures (§3.10). With version
  4 it deletes its wire map (`server/wire_map.*`). Their session is
  `ephemeris-prometheia-0b`. Their last reader run passed **85/85** on the
  previous fixture set with the checksum verified independently.

### §3 is locked. The drop's verdict was 91/91, zero disagreements.

Section 3 was settled with the Prometheia maintainers over many rounds
(work log items 0, 0b, 0c), encoded as one drop (work log item 4, commit
0fbc863), and **locked green on 2026-09-17 (the verdict letter was dated a day ahead; the commit timestamps are the authority)**: their independent reader ran
that set at 91/91 with zero disagreements, the set-sha256 verified by a
second implementation (`1c934c7da19965f21ded99a0a53e36eaa3c45434cfbfaeabdd
afaf154ea454ec`). The verdict letter is `/nvm/work/ephv4-drop-verdicts.md`;
both questions were confirmed as drafted (work log item 5), and the three
prose findings from their header review landed in the same commit that
records the lock. §3 changes now cost a version bump. What remains open is
the maintainer's decision on **phases 3-7** (§7); on the Prometheia side the
session migration onto the v4 header is queued on the maintainer's go.

1. **`u8 corrApplied` in DATA's META**, immediately after `metaFlags`.
   `resolvedNaif` and `firstFailedRow` shift by one. Its meaning is
   **structural availability** -- the corrections this engine *can* apply to
   this object, for this kind and this observer. A bit is clear when the
   engine cannot apply that term here at all, and stays set when the correction
   ran and contributed nothing. Three bits used (`kCorrLightTime`,
   `kCorrDeflection`, `kCorrAberration`, already in `ephproto.h`), five spare;
   unknown high bits are reserved and clients MUST ignore them. It is
   **diagnostic**: a conformance harness MUST NOT gate comparisons on it
   (work log 0c, the retraction). Normative wording in §3.4 (the META table).
2. **The §3.4 contradiction resolved as drafted** (work log item 2): the
   early-flush suggestion is withdrawn; a server MUST NOT flush each block as
   it completes, because META's `rowsOk`, `firstFailedRow` and `partial` are
   facts about the whole answer. Put to Prometheia as an explicit question in
   the drop; theirs to confirm or counter.
3. **Conformance fixtures regenerated** (`tools/ephproto4-fixtures.py`) with
   the new fixed part, and one new `# set-sha256` in
   `ephsrv/conformance/MANIFEST.tsv`
   (`1c934c7da19965f21ded99a0a53e36eaa3c45434cfbfaeabddafaf154ea454ec`,
   recomputed independently from the on-disk bytes).
4. **The set sent whole** to Prometheia (`ephemeris-prometheia-0b`):
   `ephsrv/ephproto.h`, `ephsrv/registries.json` and the fixtures with their
   digest, plus the §3.4 question and the corrApplied wording question. Their
   reader's verdicts on THAT set are the gate; their header
   leniency/strictness review rides in the same reply.

**The maintainer has approved phases 3-7 (2026-09-17, late).** They were never
approved as automatically next; §7 lists them and they were a separate
decision. Two conditions ride the approval: the **locked artifacts stay
byte-identical at cf83dc9** (`ephsrv/ephproto.h`, `ephsrv/registries.json`,
`ephsrv/conformance/`) -- any spec problem found while implementing becomes a
new named drop with a new digest and a fresh fixture gate on both sides, not
an in-place edit -- and the **shared scratch stays put**:
`/nvm/work/ephv4`, `/nvm/work/ephv4-drop/` and
`/nvm/work/ephv4-drop-verdicts.md` are read by Prometheia's fixture gate and
pinned-header test. Phases 3-7 are Astrolog-repo work and are built on this
tree; the Prometheia session's own queue (session migration, truthful
corrApplied, wire-map delete, SegmentCache wiring) is separate and runs on
the maintainer's go. The fitter is theirs: `prometheiad` answers SEGDATA;
`astrolog-ephd` serves samples and does not advertise the `segments` cap
(work log item 2).

- **Open for the maintainer, no action needed before the lock.** Whether to
  patch vendored Swiss's `lunar_osc_elem()` to retard in the barycentric
  frame. Work log 0c has the argument; the recommendation on this branch is
  **no**, and Prometheia withdrew the suggestion when given it.
- **How to run a round with them** is `EPHEMERIS_PROTOCOL_COLLABORATION.md`:
  what a drop contains, why it is sent whole, and the discipline that made the
  objections on both sides productive. Read it before sending anything.
- **A narrative handoff** with the round-by-round reasoning lives at
  `/nvm/work/ephv4-handoff.md`. It is scratch and not in git; everything
  load-bearing from it is in this document, which is the authority.

## 1. Why

These were found on 2026-09-17 while pointing the Qt GUI at a local server.
Every item below was verified on branch `qt` at d9642c0.

**Selection state and switches**

1. **One choice is spread over three fields.** `us.fEphemFiles`,
   `us.nSwissEph` and `us.fMatrixPla` (astrolog.h ~2230–2334) together hold the
   selected source. Many combinations mean nothing. "None" is not stored at all:
   it is simply files off with Matrix off.
2. **Two numberings for the same thing.** The `cm*` enum (astrolog.h:1295) and
   `nSwissEph` number sources differently: Horizons is 4 in one and 3 in the
   other, the server 6 and 5. This caused bug C15.
   - `FCmJPLWeb` is `nSwissEph >= 3` (extern.h:152), so it is also true for the
     server.
   - Win32's Calculation Settings shows a server selection as Horizons, and OK
     saves it as Horizons.
3. **The command-line toggles depend on prior state** (switch.cpp:2269–2383).
   - Every `-b` suffix also toggles `fEphemFiles`. From the defaults, `-bj`,
     `-bs`, `-bJ` or `-bU` therefore gives None.
   - `-bS` turns files on, but a second `-bS` turns them off.
   - `_bX` clears any backend, not only its own.
4. **The settings file only round-trips because of line order.** The writer
   emits lines in a fixed order that loading depends on (io.cpp:1795–1830).
5. **Upstream's one-way locks are shipped on.** `=0b` and `=0n` sit in
   `astrolog.as` and cannot be undone (`_0` does nothing, switch.cpp:3849).
   - Matrix, Horizons and the server cannot be chosen anywhere.
   - The dialogs hide them.
   - A saved file that selects one of them fails to load.

**Connection**

6. **A startup nag and slow retries.** A modal "Connecting to cloud ephemeris"
   dialog appears at startup, and closing it exits with code 86. Before any
   session, retries run on a 60 s tick.

**Hidden dependence on local Swiss files**

7. **Side calculations assume local Swiss files.** Progressed arcs, the eclipse
   Sun, fixed stars, planet phenomena and asteroid listings all do, whatever
   source is selected.

**Protocol**

8. **Version 3 is Swiss-numbered.** Bodies, flag bits and sidereal modes use the
   Swiss Ephemeris numbering, so any other engine needs a hand-written wire map.
   Prometheia's `server/wire_map.hpp` exists only for that reason.
9. **Version 3 cannot express one cast.**
   - The observer and flags vary per object within a single cast (fMoonMove,
     a custom object's nFlg, geocentric nodes in a heliocentric chart, the
     barycentric Sun), so the client splits a cast into several requests
     (qtdriver.cpp ~8530).
   - Sidereal mode "Fagan-Bradley on the invariable plane" is mode 0 plus a
     plane bit (calc.cpp:3714).
10. **Small items.**
    - The AstroExpression `_b` returns `us.fEphemeris` (the -E flag), not the
      ephemeris-files setting.
    - `is.fNoEphFile` is a single warning latch shared by unrelated failures.
    - There is no setting for the JPL file name.

## 2. Decisions (maintainer, 2026-09-17)

**Sources**
- **No switch disables network sources.** A selected network source is used
  if it is reachable, and a failure is reported when it happens.
- **Plugins are compiled in only.**
- **What a plugin supplies.** Body positions, with capabilities it declares.
  Houses, ΔT, the calendar and refraction are shared formulas; they need no data
  files.
- **Sources are not tied to Swiss.** The design assumes only the selected
  ephemeris is available: **every call tries the primary source, then
  successively less ideal ones**, and records which source answered.
- **Horizons is rewritten** as a proper plugin that takes instants.

**Builds**
- **Full Win32 parity**, including the Ephemeris Settings dialog.
- **Real server transports in every build.** Qt uses QWebSocket, Win32 uses
  WinHTTP's WebSocket client, and the console build uses a plain socket
  client.

**Protocol version 4**
- **Bodies are named by NAIF/SPK-IDs**, with typed extras for what NAIF does
  not cover.
- **Options are explicit typed fields.**
- **Clean break.** kProtoMin = 4. No released client speaks 2 or 3 (qt.24
  shipped version 1), and no public server exists.
- **Discovery and provenance.** Capabilities come in WELCOME, provenance per
  object in DATA, and there is a LOOKUP message.
- **Segments are normative but gated by a capability.**

**Settled in design review**
- **Local Swiss stays bit-exact** through a host-only native-id hint.
- **The Swiss fork gains an orbital-elements entry point.**

## 3. Protocol version 4 (normative)

The key words MUST, MUST NOT, SHOULD and MAY are used as in RFC 2119.

### 3.1 Transport and conventions

**Transport**
- **WebSocket, binary frames only.** Each frame carries exactly one message.
  A text frame is ERROR 1.
- **Default port 47190.** The server port also answers `GET /healthz`,
  `/readyz` and `/metrics`.
- **TLS.** `wss://`, with TLS 1.2 or later.

**Encoding**
- **Byte order.** Integers are little-endian. `f32` and `f64` are IEEE-754 bit
  patterns, little-endian.
- **`str8`.** A u8 length followed by that many bytes of UTF-8, with no
  terminator. The bytes MUST be valid UTF-8 with no C0 control characters.
- **`TIME`.** Two f64 values, `jd1` and `jd2`: a two-part Julian date whose
  instant is jd1 + jd2. A sender that has a single double sends it as jd1 with
  jd2 = 0. Every TIME MUST be finite, with |jd1| + |jd2| ≤ 1e8.
- **TLV area.** A u16 `totalLen` counts the entry bytes that follow it. Each
  entry is `{u16 tag, u16 len, len bytes}`.
  - Tags MUST be strictly ascending by their full u16 value, with no duplicates.
  - Bit 15 of a tag is **critical**. A receiver that does not know a critical
    tag MUST refuse the message: for REQUEST that is ERROR 11, closing = 0.
    Unknown non-critical tags MUST be ignored, and a server that ignored one sets
    `ignoredExt` in its answer.
  - Tags 0x0001–0x6FFF (with or without bit 15) are assigned in Appendix A.
    0x7000–0x7FFF (with or without bit 15) are experimental and never assigned.
  - Tag numbers are scoped to the message type whose TLV area they appear in.
- **Reserved fields** MUST be zero. So MUST fields a message's other choices
  make unused (§3.5).
- **Enumerated fields** carry registry values (Appendix A). A value the
  receiver does not implement makes REQUEST fail with ERROR 11, not ERROR 1:
  registries grow.
- **The answering direction tolerates growth.** Registries grow while released
  clients stay in use, so a client MUST accept a server message carrying a value
  it does not know, and MUST NOT treat it as malformed:
  - an unknown per-object `errCode` means that object failed, reason unknown;
  - unknown META flag bits are ignored;
  - an unknown ERROR code is a failure of unknown kind — `flags` still says
    whether the connection is closing and whether a retry may work;
  - an unknown match `quality`, orbit method or object kind inside a
    LOOKUP_RESULT means "not something this client can ask for": the client
    skips that match using its `matchLen` and reads the rest;
  - unknown bits in an answer's flag fields — ERROR `flags`, DATA and SEGDATA
    `chunkFlags`, LOOKUP_RESULT `flags` — are ignored, and a bit added to those
    fields later MUST NOT change how following bytes are read, precisely so
    that this stays possible.
  The exception is anything that does change how following bytes are read: an
  unadvertised bit in DATA's `columnsPresent` changes the row width, so it is
  **unsupported**, not malformed — and the same holds for an unadvertised
  `precision`: both are refused with ERROR 11, a registry-growth refusal this
  side may answer later, never a claim that the peer broke a rule.
- **Floats** MUST be finite. The single exception is the canonical quiet NaN,
  `0x7FF8000000000000`, which is allowed only where a field says so. **At
  `precision` = f32 the canonical quiet NaN is `0x7FC00000`** — the f64 pattern
  narrowed, which widens back to it exactly — and a receiver MUST reject any
  other f32 NaN payload. In DATA the only field that says so is §3.5's
  row-failure rule: a failed row is the canonical NaN in **every** column, so a
  NaN in some columns of a row and not others has no field permitting it and is
  malformed, of the same class as an infinity. If a registry column ever wants
  NaN to mean *absent*, that column's A.10 entry must say so and the rule
  becomes "the whole row, or a column whose entry allows it"; none does today.
- **Canonical input.** A receiver MUST reject non-canonical input (ERROR 1)
  rather than normalise it. This makes byte equality mean question equality,
  and the result cache depends on it (§3.7).

### 3.2 Envelope

Every message is a 16-byte envelope followed by `payloadLen` bytes of payload.

| offset | type | field |
|---|---|---|
| 0 | u16 | magic `0x1EF0` |
| 2 | u8 | version the message is written in |
| 3 | u8 | flags: bit 0 `zstd` (payload compressed with zstd; only when both ends advertised it); bits 1–7 zero |
| 4 | u16 | type (Appendix A.1) |
| 6 | u16 | reserved, zero (held for channel multiplexing) |
| 8 | u32 | requestId |
| 12 | u32 | payloadLen |

- **payloadLen** MUST NOT exceed WELCOME's `maxPayload` after the session is
  established. Before that the limit is 64 KiB.
- **requestId.**
  - The client chooses a request's id. It MUST be nonzero and MUST NOT be reused
    while that request's answer is outstanding.
  - Answers carry the id of the question they answer.
  - It MUST be 0 on HELLO, WELCOME, PING, PONG and a connection-level ERROR, and
    MUST be nonzero on REQUEST, CANCEL and LOOKUP and on the DATA, SEGDATA,
    LOOKUP_RESULT and ERROR messages answering them.
- **Unknown message type.** ERROR 3, not closing.

### 3.3 Negotiation and frozen layouts

The layouts of **HELLO, WELCOME's first 28 bytes, ERROR, PING, PONG and
CANCEL** are frozen for every version ≥ 4. Later versions extend them only
through their TLV areas. This is what lets two ends of any versions talk long
enough to agree or refuse.

1. **HELLO.** The client sends HELLO first. Its envelope version is the
   client's highest version.
2. **Session version.** A server MUST accept a HELLO in any envelope version
   ≥ 4. It computes `session = min(client.protoMax, server.max)`.
3. **No overlap.** If `session < max(client.protoMin, server.min)`, the server
   sends ERROR 8 (closing) in version `session` if that is ≥ 4, otherwise in
   version 4, and closes.
4. **Otherwise** the server answers WELCOME in `session`. Every later message
   in either direction is written in `session`.
5. **Older clients.** An envelope with version < 4 comes from an older client.
   The server answers ERROR 8 in **that client's own layout** (versions 2–3:
   `u32 requestId, i32 code, NUL-terminated text`) in that version, and closes.
6. **Message before HELLO.** Any message other than HELLO and PING before HELLO
   gets ERROR 1 (closing).
7. **Repeated HELLO.** A second HELLO is answered with WELCOME in the
   established session, which is not renegotiated.
8. **Missing HELLO.** A server MAY close a connection that sends no HELLO within
   its deadline.

### 3.4 Messages

Payload layouts follow; `…` marks a variable part.

#### HELLO (1, client → server)
| type | field |
|---|---|
| u32 | protoMax — the client's highest version |
| u32 | protoMin — the client's lowest version (4) |
| u32 | build — the client's build number, or 0 |
| u32 | clientCaps — Appendix A.2 bits the client can use |
| str8 | clientName — e.g. `Astrolog 8.00-qt.25` |
| str8 | token — empty for none; at most 128 bytes; never logged by servers |
| TLV | HELLO extensions (none assigned) |

#### WELCOME (2, server → client)
| type | field |
|---|---|
| u32 | protoSession |
| u32 | caps — Appendix A.2 |
| u32 | maxObjs — objects per REQUEST |
| u32 | maxRows — rows (instants) per REQUEST |
| u32 | maxChunkRows — rows per DATA chunk |
| u32 | maxPayload — bytes per message |
| u32 | maxCells — objects × rows per REQUEST |
| u8 | maxProfiles |
| u8, u16 | reserved |
| str8 | serverName — e.g. `astrolog-ephd/2.0`, `prometheiad/0.2` |
| str8 | engine — human-readable, e.g. `Swiss Ephemeris 2.10.03 files`, `Prometheia 0.2, JPL DE440 + SBDB 2026-09-16` |
| str8 | datasetId — `<engine>/<ephemeris>/<catalogs>#<8 hex>`, the digest over every data file's checksum and the engine version. It MUST change whenever any answer the server gives could change. Clients key caches on it, and may pin it (A.4 0x8003). |
| TLV | capabilities (Appendix A.3). Tags 0x0001–0x0006, 0x0008 and 0x0009 MUST be present. |

A client MUST NOT send an option value, object kind, column or message that
the server did not advertise. A server MUST refuse one that it does not
support, with ERROR 11 or a per-object error as §3.5 says.

#### REQUEST (3, client → server)
The payload is a **delivery block**, then a **question block**. Only the
question block goes into the cache key.

Delivery block (16 bytes):
| type | field |
|---|---|
| u8 | precision — 0 f64, 1 f32 (DATA values only) |
| u8 | priority — 0 interactive, 1 prefetch |
| u8 | representation — 0 samples (answered with DATA), 1 segments (answered with SEGDATA; requires the `segments` cap) |
| u8 | maxDegreeHint — segments only: the largest Chebyshev degree the client wants to buffer; 0 lets the server choose. The server also caps by its own capability (A.3 0x000F). 0 for samples |
| u32 | chunkRows — a hint; the server clamps it to `[1, maxChunkRows]`, and 0 means `maxChunkRows` |
| f32 | segTargetErrArcsec — 0 for samples; for segments a finite value > 0, the angular error the client asks for |
| u32 | deadlineMs — how soon the client wants the answer, 0 for "no deadline stated". Advisory: a server MAY choose a cheaper strategy to meet it (samples now rather than a fit), and MUST NOT fail a request for missing it. `priority` says which order to work in; this says how long the work may take, which is a different question and the one that decides between two strategies |

Question block:
| type | field |
|---|---|
| u8 | timeScale — Appendix A.9 (0 UT1, 1 TT, 2 TDB) |
| u8 | timeMode — 0 grid, 1 list |
| u16 | reserved |
| … | grid: `TIME start`, `i64 stepNs`, `u32 nTime` · list: `u32 nTime`, `nTime × TIME` |
| f64 | deltaTSec — TT − UT1 in seconds, or the canonical NaN for "the server's model" |
| u8 | nProfiles (1..maxProfiles) |
| … | nProfiles × PROFILE |
| u16 | nObj (1..maxObjs) |
| … | nObj × OBJECT |
| TLV | REQUEST extensions (Appendix A.4) |

PROFILE:
| type | field |
|---|---|
| u8 | observer — A.5 (0 geocentric, 1 topocentric, 2 heliocentric, 3 solar-system barycentre, 4 body) |
| u8 | plane — A.6 (0 ecliptic, 1 equator) |
| u8 | form — A.6 (0 spherical, 1 rectangular) |
| u8 | frame — A.6 (0 true of date, 1 mean of date, 2 J2000, 3 ICRF) |
| u8 | corrections — A.7 bits (light time 1, deflection 2, aberration 4) |
| u8 | speeds — 0 or 1 |
| u8 | siderealPlane — A.8 (0 ecliptic of date, 1 ecliptic of the anchor epoch, 2 invariable plane of the solar system) |
| u8 | reserved |
| i32 | observerBody — NAIF id when observer = 4, else 0 |
| f64 ×3 | siteLonEastDeg, siteLatDeg, siteHeightM — when observer = 1, else 0 |
| TIME | anchorEpoch — for zodiac `user`, else zero |
| f64 | anchorAyanamsaDeg — for zodiac `user`, else 0 |
| u32 | columns — A.10 extra columns wanted |
| str8 | zodiac — "" tropical, otherwise a token from A.11 |
| TLV | PROFILE extensions (none assigned) |

OBJECT (a 4-byte head, then a payload that depends on the kind; Appendix A.12):
| type | field |
|---|---|
| u8 | kind |
| u8 | profile — an index < nProfiles |
| u16 | reserved |

| kind | payload |
|---|---|
| 0 body | `i32 naif` |
| 1 orbit point | `i32 naif`, `u8 point` (A.13), `u8 method` (A.14), `u16 reserved` |
| 2 fixed star | `str8 name` — an IAU proper name, Bayer, Flamsteed, HR, HD or HIP designation (§3.5a) |
| 3 named hypothetical | `str8 name` — a token from A.15; elements server-defined (§3.5a) |
| 4 elements | `TIME epoch`, `u8 equinox` (A.16), `u8 centre` (0 Sun, 1 Earth), `u8 nTerms` (1..5), `u8 reserved`, `f64 equinoxJd` (A.16 value 4 only, else 0), then `6 × nTerms f64`: the polynomial coefficients c0..c(nTerms−1) of, in order, mean anomaly M (deg), semi-major axis a (AU), eccentricity e, argument of perihelion ω (deg), ascending node Ω (deg), inclination i (deg); then `str8 name` |
| 5 designation | `str8 designation` — resolved as an exact LOOKUP; ambiguity is a per-object error (§3.5) |

#### DATA (4, server → client)
Answers a samples REQUEST in one or more chunks. Header (24 bytes):
| type | field |
|---|---|
| u32 | chunkIndex — 0, 1, … |
| u32 | iTime — index of this chunk's first row |
| u32 | nRows — rows in this chunk |
| u32 | totalRows — the request's nTime |
| u8 | precision — as requested |
| u8 | chunkFlags — bit 0 last chunk, bit 1 ignoredExt, bit 2 meta present |
| u16 | nObj |
| u32 | columnsPresent — the extra columns in this answer (§3.5) |

**Metadata.** If the meta-present flag is set, the source table and the object
metadata follow the header:
- **Source table.** `u8 nSources`, then `nSources × str8`: the sources that
  answered, e.g. `JPL DE440`, `Swiss Ephemeris files (sepl_18)`, `SBDB 2026-09-16`.
- **Object metadata.** `nObj × META`.

The meta-present flag MUST be set on chunk 0. A server MAY repeat the metadata
on later chunks, and clients use chunk 0's.

META:
| type | field |
|---|---|
| i32 | rowsOk — rows of this object that computed |
| u16 | errCode — A.17; the first failure's code, else 0 |
| u8 | sourceIdx — index into the source table, 0xFF if none |
| u8 | metaFlags — A.18 |
| u8 | corrApplied — A.7 bits; see below |
| i32 | resolvedNaif — the NAIF id actually computed, or INT32_MIN when not applicable |
| u32 | firstFailedRow — 0xFFFFFFFF if none |
| str8 | name — display name, e.g. `Ceres`, `Moon mean apogee` |
| str8 | errText — the first failure's text, else empty |

`corrApplied` says which correction terms are live for this object here --
**structural availability**, not the request's mask. A bit is clear when the
engine cannot apply that term to this object, for this kind and this
observer, at all; it stays set when the term's model ran and contributed
nothing (a deflection that returns zero far from the Sun has still been
applied). The field does not depend on what the request asked: a request for
true positions still reports the terms the engine can apply -- what it asked
stays the client's own knowledge, and the two conforming servers' bytes will
differ where their engines differ. Three bits from A.7 (`kCorrLightTime`,
`kCorrDeflection`,
`kCorrAberration`); the five low spares are zero, and unknown high bits are
reserved: clients MUST ignore them and servers MUST NOT refuse them. The
field is **diagnostic only**: it explains a difference; it does not predict
one, and equality of it is neither necessary nor sufficient for two answers
to agree. A conformance harness MUST NOT gate comparisons on it.

**Values.** Objects in request order. For each object come the chunk's rows in
order, and each row holds `nCols = 6 + popcount(columnsPresent)` values (f64 or
f32 as the precision says). The column order is in §3.5.

**Chunks.** Chunks are contiguous and ascending, covering every row exactly
once.

#### ERROR (5, server → client)
| type | field |
|---|---|
| u16 | code — A.19 |
| u16 | flags — bit 0 closing (the server closes after sending it), bit 1 retryable |
| u32 | retryAfterMs — 0 if unknown |
| str8 | text — human-readable; MUST NOT contain request contents that identify a person's chart (instants, places) |
| TLV | ERROR extensions (none assigned) |

The envelope's requestId names the failed request, or 0 for the connection.

#### PING (6) / PONG (7)
The payload is empty. Either end MAY send PING at any time, and the other end
answers PONG with the same requestId.

#### CANCEL (8, client → server)
The payload is empty, and the envelope's requestId names the request.
- **Still being answered.** The server stops computing what is left, discards
  the unsent chunks and sends ERROR 10 (not closing, not retryable).
- **A cancelled request caches nothing.** A partial answer MUST NOT become a
  cache entry, or a later identical question hits half an answer.
- **To make CANCEL mean anything**, a server computes a request in blocks of
  rows across loop turns rather than in one callback: otherwise the work is
  finished by the time the CANCEL is read and only bytes remain to drop, and a
  large request blocks that loop for everyone (64 objects × 20,000 rows measured
  13.7 s, EPHEMERIS_REVIEW.md S4). A server MUST NOT flush each block as it
  completes: DATA carries the metadata on chunk 0, and META's `rowsOk`,
  `firstFailedRow` and `partial` are facts about the whole answer, which an
  early flush would have to write before they are known -- and a streaming
  client starts allocating per-object state from chunk 0, so revisable META
  breaks exactly the client the early flush was meant to serve. A server that
  computes in blocks therefore holds the finished ones and streams when the
  answer is whole.
- **Already answered completely, or unknown.** The server sends nothing.
- **Chunks already in flight.** The client MUST ignore chunks that arrive for a
  cancelled id.

#### LOOKUP (9, client → server)
Requires the `lookup` cap.
| type | field |
|---|---|
| u16 | maxMatches — the budget for the WHOLE message, 1..the lookup TLV's limit |
| u8 | flags — bit 0 prefix match, bit 1 include hypotheticals, bit 2 include stars |
| u8 | nQueries — 1..255 |
| … | nQueries × `str8 query` — a name, designation or number, case-insensitive |
| TLV | LOOKUP extensions (none assigned) |

`maxMatches` bounds the answer as a whole, not each query: the server fills it
in query order and sets `truncated` when it runs out. A per-query budget would
make one small message ask for 255 × 65,535 matches, and a prefix query like
"a" matches thousands of stars in a real catalogue. Batching is what makes
LOOKUP usable at all for a body picker: Astrolog's Object Selections offers 78
bodies, which is 78 round trips one query at a time.

#### LOOKUP_RESULT (10, server → client)
| type | field |
|---|---|
| u8 | nQueries — the same count the LOOKUP carried, in the same order |
| u8 | flags — bit 0 truncated (the message budget ran out) |
| u8 | nSources, then nSources × str8 — shared by every query |
| … | per query: `u16 n`, then n × MATCH |

MATCH:
| type | field |
|---|---|
| u8 | quality — 0 exact canonical name, 1 exact alias or designation, 2 prefix |
| u8 | sourceIdx |
| u16 | matchLen — bytes from the end of this field to the end of this MATCH, so a client can skip a match whose object kind it does not know (§3.1). A value that disagrees with a match the client CAN read is malformed |
| … | OBJECT (kind + profile 0 + reserved + payload) — what to put in a REQUEST |
| str8 | canonicalName |
| str8 | designation — e.g. `2060`, `1P/Halley`, empty if none |
| TIME, TIME | validMin, validMax — the coverage for this object; both zero when unknown |

Matches are ordered by quality, then by the server's relevance. A query can
legitimately match several kinds: "Lilith" is asteroid 1181, the Moon's mean
apogee, and a hypothetical body.

#### SEGDATA (15, server → client)
Answers a segments REQUEST (`representation = 1`). Header (16 bytes):
| type | field |
|---|---|
| u32 | chunkIndex |
| u8 | chunkFlags — bit 0 last, bit 1 ignoredExt, bit 2 meta present |
| u8 | reserved |
| u16 | nObj — in the whole answer |
| u16 | iObj — first object in this chunk |
| u16 | nObjChunk |
| u32 | reserved |

Then, when meta is present (always on chunk 0), the source table and
`nObj × META` exactly as in DATA. Here `rowsOk` counts the segments served, and
`firstFailedRow` is unused (0xFFFFFFFF). Chunk 0 then carries the **ayanamsa
series**: `u8 nAyan`, and for each, `u8 profile`, `u32 nSeg`, `nSeg × AYANSEG`
— one entry per profile whose zodiac is not tropical, none otherwise. After
that, for each object from iObj to iObj + nObjChunk − 1: `u32 nSeg` and
`nSeg × SEGMENT`.

AYANSEG: `TIME mid`, `f64 halfSpanDays`, `u8 degree`, `u8 ×3 reserved`,
`f32 errArcsec`, `(degree+1) f64` — the ayanamsa in degrees, evaluated as a
SEGMENT's axis is, covering the same span. A mean ayanamsa needs about degree
3 over a century; a true one needs more.

SEGMENT:
| type | field |
|---|---|
| TIME | mid — the segment's centre |
| f64 | halfSpanDays — h > 0 |
| u8 | degree — d, 0..31 |
| u8 ×3 | reserved |
| f32 | errArcsec — the largest residual the server MEASURED on the direction |
| f32 | errRelDist — the largest measured relative distance residual |
| f32 | errRateArcsecPerDay — the largest measured residual of the analytic derivative against the server's own rates |
| f64 × 3(d+1) | Chebyshev coefficients: x0..xd, y0..yd, z0..zd |

Segment rules:
- **Required form.** A segments REQUEST's profiles MUST have `form = 1`
  (rectangular) and `columns = 0`, or the REQUEST is ERROR 11.
- **Grid.** Grid mode is required; the list mode is ERROR 11. The requested
  **span** runs from the grid's first instant to its last.
- **Coverage.** Segments for an object are ordered by `mid`, and together they
  cover the span contiguously. The end of one, mid + h, equals the start of the
  next, mid′ − h′, to within 1e-9 day.
- **Evaluation.** For an instant t:
  - τ = ((t.jd1 − mid.jd1) + (t.jd2 − mid.jd2)) / h, with |τ| ≤ 1.
  - The position (AU) is x = Σₖ xₖ Tₖ(τ), and likewise y and z, where Tₖ is the
    Chebyshev polynomial of the first kind.
  - The velocity (AU/day) is Σₖ xₖ T′ₖ(τ) / h.
  - The coordinates are rectangular in the profile's observer, plane, frame and
    corrections. For a sidereal zodiac, the rotation about the plane's pole by
    the ayanamsa at each instant is included in the fit.
- **Residuals, measured not claimed.** `errArcsec`, `errRelDist` and
  `errRateArcsecPerDay` are the largest residuals the server MEASURED against
  its own answers over the segment, on a check set of at least 4(d+1) instants
  that **MUST include both endpoints of the segment's interval** (τ = ±1) as
  well as points between the fit nodes. The endpoints are not optional: a
  Chebyshev interpolant's error peaks near the interval ends, and its
  DERIVATIVE's error peaks there much harder. Measured: the Moon's
  `errRateArcsecPerDay` declared from interior points alone was 1.48″/day where
  an independent sample found 3.6″/day; with the endpoints in the check set the
  same fit declares 4.65″/day. A server obeying the weaker reading
  under-reports by a factor of two to three, always in its own favour. The server SHOULD make
  `errArcsec` ≤ `segTargetErrArcsec`; where it cannot, it reports what it
  measured. (A rigorous bound costs more than the fit; a dense measured
  residual is cheap and honest.)
- **The zodiac is not in the fit.** Coefficients are always tropical in the
  profile's frame. When a profile carries a zodiac, the answer also carries
  that profile's **ayanamsa series** (below), and the client subtracts it. One
  fit then serves every zodiac, and a true-of-date ayanamsa -- which carries
  nutation in longitude, 17″ with an 18.6-year period -- is fitted where it
  belongs instead of roughening every body's fit.
- **Fit the lattice, not the ask.** A server SHOULD fit fixed lattice cells
  that cover the requested span -- 32 days, aligned from J2000 -- rather than
  the span it was asked for, so that two clients whose spans overlap share the
  fits. The lattice is the SERVER's: a client MUST NOT be able to name a
  boundary, or the sharing is gone. Measured cost of doing so (one 384-day span
  against twelve 32-day cells, same body, same target): the worst case is +14%
  sampler calls (the Moon at 1″), and in half the cases the cells are CHEAPER,
  because fitting a year as one interval wastes probes before it splits. The
  client is unaffected: it gets contiguous coverage of what it asked for, and a
  little either side.
- **Quantise the ask, downward only.** A server MAY round
  `segTargetErrArcsec` onto its own ladder (0.001 / 0.01 / 0.1 / 1″), so that
  two clients asking 0.1 and 0.12 share one fit, but it MUST round toward a
  FINER fit, never a coarser one. The residual it reports is the truth about
  what was served, but a client asked for a number because something downstream
  depends on it, and "you got worse than you asked, look at the metadata" is a
  defect that surfaces as a wrong ingress time three layers away. Rounding
  finer costs only the server, which is the right party to bear it.
- **Which objects.** The segments capability (A.3 0x000F) carries an A.12
  kinds bitmask; an object of another kind is per-object error 2. Some objects
  fit badly on purpose: the Moon's osculating perigee moves degrees a day and
  its longitude rate changes sign.
- **Failure.** An object that could not be computed has `nSeg = 0` and its
  error in META.

#### Reserved message types
- **11 SUBSCRIBE, 12 UNSUBSCRIBE.** Server-pushed windows that follow an
  anchor instant.
- **13 LIST, 14 LIST_RESULT.** Catalog paging with an opaque cursor.
- **16–31.** Event searches: stations, ingresses, aspects, eclipses, rise and
  set, heliacal phenomena.

These numbers are reserved and have no layout yet.

### 3.5 Semantics

**Instants**
- **Grid.** The instant of row r is the TIME
  `(start.jd1, start.jd2 + off)`, where `off = (double)(r × stepNs) /
  86400000000000.0`.
  - The product `r × stepNs` is computed in i64. A REQUEST where
    `(nTime−1) × |stepNs|` overflows i64 is ERROR 1.
  - An engine that takes one double evaluates `start.jd1 + (start.jd2 + off)`.
  - `stepNs` is signed. It MUST be 0 when nTime = 1 and nonzero otherwise.
- **List.** Instants in any order; duplicates are allowed.
- **Time scale.** Instants are in `timeScale`. ΔT (TT − UT1) converts UT1
  instants and drives Earth rotation for topocentric work. Its value at each
  row comes from, in order:
  1. the **ΔT table** REQUEST TLV (0x8004, A.4), when present: piecewise-linear
     interpolation in the table's instants, held constant beyond its ends;
     `deltaTSec` MUST then be the canonical NaN. **The table's instants are
     TT**, whatever the request's time scale, and a row's ΔT is interpolated at
     that row's own numeric instant with no iteration — deterministic on both
     ends, and for a UT1 row the ΔT so found differs from the TT-argument value
     by under 1e-5 s;
  2. a finite `deltaTSec`: that one value for every row, which is the
     client's responsibility to keep valid over the span it asks (ΔT drifts
     about a second a year today, and far faster historically);
  3. otherwise the server's own ΔT model, named in the ΔT-model capability.

  A client that owns ΔT, as Astrolog does, sends TT instants and a ΔT table
  (or, for short spans, one `deltaTSec`).

**Observers and options**
- **Observer.**
  - Correction bits are **honoured as sent, for every observer** (§3.5a). A
    server advertises, per observer, the masks it can honour (A.3 tag 0x0004);
    any other combination is ERROR 11.
  - A body observer (4) with `observerBody` equal to the object is a per-object
    error 2.
- **Sidereal zodiacs apply to the ecliptic plane only.** A PROFILE with a
  nonempty zodiac and `plane = 1` (equator) is ERROR 1.
- **Unused fields.** `observerBody`, the site, `anchorEpoch` and
  `anchorAyanamsaDeg` MUST be zero unless the observer or zodiac uses them.
  - Site ranges: longitude in [−180, 180], latitude in [−90, 90].
  - Zodiac `""` (tropical) requires `siderealPlane = 0`.
  - Zodiac `user` requires a nonzero anchor epoch.
- **Speeds.** When `speeds = 0`, the three rate columns are 0 and META's
  `noSpeeds` flag is set. When `speeds = 1`, rates are as §3.5a defines.

**Columns**, per row:
- The base six:
  - ecliptic spherical: longitude and latitude (deg), distance (AU), then their
    rates (deg/day, AU/day);
  - equatorial spherical: right ascension and declination (deg), distance, then
    their rates;
  - rectangular: x, y, z (AU), vx, vy, vz (AU/day).
- Longitudes and right ascensions are in [0, 360).
- For a sidereal zodiac, longitudes are reduced as §3.5a defines; with
  rectangular form the vector is rotated about the ecliptic pole by the same
  ayanamsa, so the two forms stay consistent.
- **Distance unknown** (a star without a parallax): the distance column and its
  rate are 0, and META's `noDistance` flag is set.
- Extra columns follow in A.10 bit order, for the bits in `columnsPresent`,
  which is the requested columns intersected with the server's advertised ones.
  - σ is valid only where META's `hasSigma` is set, and is 0 elsewhere.
  - Ayanamsa is the value subtracted for that row, in degrees (§3.5a); 0 for
    tropical.
  - Light time is the τ applied, in days; 0 when light time is off.
  - ΔT is the TT − UT1 used for that row, in seconds.

**Failure**
- **Row failure.** A row that failed has NaN in every column of that row. An
  object keeps its computed rows (`partial`).
- **Object failure.** An object whose rows all failed has `rowsOk = 0` and its
  errCode and errText set. **One object's failure never fails the request.**
- **Whole-request failures** are only those in A.19.

**Limits**
- `nObj ≤ maxObjs`, `nTime ≤ maxRows`, `nObj × nTime ≤ maxCells` and
  `nProfiles ≤ maxProfiles`; otherwise ERROR 2.
- A connection holding a server-chosen number of answers the client has not yet
  read (4 in `astrolog-ephd`) gets ERROR 9 (retryable) for the next one.
- A server enforcing a compute budget answers ERROR 6 (retryable) with
  `retryAfterMs`.

**Ordering and delivery**
- A server MAY interleave chunks of different requests, and MAY answer
  priority 0 before priority 1. Within one request, chunks stay in order.

**Pins** (REQUEST TLVs 0x8001 and 0x8002, both critical)
- The request is answered only from the named ephemeris or catalog snapshot, or
  refused with ERROR 5.
- This gives reproducibility: a chart can record the `datasetId` or pins it was
  cast with.

**Designations** (kind 5) resolve exactly as LOOKUP with quality 0 or 1.
- More than one match is per-object error 6.
- No match is per-object error 1.

### 3.5a Physical definitions (normative)

Two conforming servers given the same question MUST return the same
quantity. These definitions say which quantity; the numbers then differ only
by the ephemerides and models each server names in WELCOME (engine,
datasetId, coverage, precession models, ΔT model) and per object (the source
table).

**Frames and planes** (default precession model IAU 2006; REQUEST TLV 0x0003
may select another from A.20):
- **True of date (0).** Equator: the true equator and true equinox of date
  (IAU 2006 precession, IAU 2000A nutation or the server's advertised
  equivalent). Ecliptic: the mean ecliptic of date, longitudes counted from the
  true equinox of date (nutation in longitude applied, latitude unchanged by it).
- **Mean of date (1).** The mean equator, mean ecliptic and mean equinox of
  date; no nutation.
- **J2000 (2).** The mean equator and equinox of J2000.0, **including frame
  bias** (the IAU 2006 J2000 mean dynamical frame). Its ecliptic is that equator
  rotated by the IAU 2006 J2000 mean obliquity, 84381.406″.
- **ICRF (3).** ICRS axes, no bias. Its ecliptic is the ICRS equator rotated by
  84381.406″ about the ICRS x axis.
- **Topocentric site.** WGS84 ellipsoid; east longitude and geodetic latitude
  in degrees; height above the ellipsoid in metres.

**Corrections**, honoured as sent for every observer:
- **Light time (1):** the body's position at the retarded instant t − τ, τ the
  light time from body to observer.
- **Deflection (2):** gravitational light deflection by the Sun, applied for
  every observer that is not the Sun itself — the barycentre included, which
  sits about 0.005 AU from the Sun's centre. For an observer at the Sun's centre
  the bit has no effect. A direction inside the solar disc, as seen from the
  observer, skips deflection rather than evaluating a formula whose denominator
  vanishes there.
- **Aberration (4):** relativistic aberration from the observer's velocity
  relative to the solar-system barycentre, whatever the observer — the Earth's
  (geocentric), the site's (topocentric, including diurnal motion), the Sun's
  (heliocentric), zero at the barycentre, the observing body's (observer 4).
- A server that cannot honour a mask for an observer does not advertise that
  pair (A.3 0x0004); the client then asks for a mask it can.

**Rates** (`speeds = 1`):
- The rate columns are **the time derivatives, per day of the request's time
  scale, of the coordinates answered in the other three columns** — including
  every change of the pipeline with time (light time, aberration, precession,
  nutation, the ayanamsa for sidereal zodiacs). Rectangular velocities are the
  derivatives of the answered x, y, z likewise.
- **The comparison is defined on f64 positions.** `precision` (§3.4) is a
  delivery choice, and at f32 the distance column is quantised far above the
  change the test is trying to see: over ±0.001 day the Sun's distance and a
  fixed star's are both **bit-identical at f32**, so their central difference
  is exactly zero and the "difference" a client computes is the server's whole
  rate with the sign taken off. Measured 2026-09-20 -- Polaris at 2.7356e7 AU
  has an f32 ULP of **3.26 AU** against a window change of 3.08e-5 AU, and
  Pluto's barycentre moves 8 f32 ULP where it moves 4.5e9 f64 ULP. A client
  checking a server's rates asks for f64; a server measuring itself uses f64.
  Neither tolerance below is meetable at f32 by any implementation, which is
  how this was found: both engines measured each other failing it.
- A server whose rates may differ from the central difference of its own
  positions over ±0.001 day by more than **1e-5 °/day** (angles) or
  **1e-6 AU/day** (distance) sets META's `ratesApprox` flag on the objects
  concerned, and states its largest such difference in the rates-bound
  capability (A.3 0x0013). The distance figure is loose on purpose: a distance
  rate that omits the light-time term differs by the observer's acceleration
  times the light time — measured at 3e-5 AU/day for Uranus in the Swiss
  Ephemeris — which is a definitional difference, not an error, and a server
  whose distance rates omit it says so with the flag.

**Sidereal zodiacs** (see §3.5):
- A zodiac has a **zero point**, and it is a **DIRECTION IN THE SKY**, not an
  arc from the equinox. Every ayanamsa is defined that way — `true-citra` is
  Spica at 180°, `aldebaran-15tau` is Aldebaran at 45°, `galcent-0sag` is the
  galactic centre at 240°, and Lahiri and Fagan/Bradley are numerical fits to
  statements of the same kind. The arc from the equinox is the *derived*
  quantity: it is recomputed for every instant as the equinox precesses, and it
  is different tomorrow. A reference plane is a choice of how to **measure**
  directions and MUST NOT change **which** direction a zodiac starts at.
- Zodiacs come in two kinds, and **the kind is decided by whether the zodiac has
  an anchor epoch, never by an enumeration.** A list would rot; the question is
  answerable from the zodiac's own definition.
  - **Epoch-anchored.** A mean ayanamsa A₀ at an anchor epoch t₀ (TT). Its zero
    point is the direction at longitude A₀ on the **mean ecliptic and equinox of
    t₀** — fixed in space thereafter.
    - `user`: t₀ = `anchorEpoch` (TT), A₀ = `anchorAyanamsaDeg`, a **mean**
      ayanamsa (no nutation in it).
    - Named tokens (A.11): the published definition of that mode's zero point.
    - **A₀ is the MEAN ayanamsa at t₀**, the arc from the *mean* equinox. Placing
      the *true* value on a mean frame moves the origin by the nutation in
      longitude at t₀, which is zodiac-dependent — −3.311″ for Fagan/Bradley,
      +16.777″ for Lahiri, +17.346″ for Raman — and so looks exactly like the
      defect this rule exists to prevent.
  - **Instant-defined.** The zero point is defined by where something *is*,
    evaluated at the instant asked: a star on the ecliptic, the galactic centre,
    the galactic node. Such a zodiac has **no A₀ and no t₀**, and the
    epoch-anchored rules above simply do not apply to it.
- **The anchor is taken at its TRUE position: no aberration, no deflection.**
  Aberration is an artefact of the observer's motion, not a property of the sky,
  so an apparent anchor makes the zero point swing through a full cycle every
  year — measured at **40.179″ peak to peak** for Spica across 2000. A sidereal
  zero point that oscillates annually is not a fixed reference, and being one is
  the whole of what a sidereal zodiac is for.
- **A polar projection goes through the MEAN pole of date.** Where a zodiac's
  definition projects its anchor along an hour circle (`galcent-mula-wilhelm`),
  the pole is the mean pole of date and the equinox is then slid by Δψ. Through
  the *true* pole the pole's own nutation enters the zero point — about 0.6″
  over 18.6 years — **even in the mean ayanamsa**, and a mean quantity that
  carries nutation is a contradiction. The other ten instant-defined modes
  cannot see the difference, because the ecliptic does not nutate.
- A server implements the tokens whose definitions it implements, and advertises
  exactly those.
- **siderealPlane 0 (ecliptic of date).** The ayanamsa subtracted at t is
  A(t) = A₀ + p(t₀, t), p the general precession in longitude from t₀ to t, plus
  the nutation in longitude at t **for frame 0 only** (true ayanamsa); frame 1
  uses the mean ayanamsa; frames 2 and 3 use the constant A(J2000.0) mean, a zero
  point fixed on the J2000 ecliptic.
- **siderealPlane 1 (ecliptic of the anchor epoch).** Positions are referred to
  the mean ecliptic and equinox of t₀; longitude is counted from the zero point
  there (A₀ subtracted, no precession term). **An instant-defined zodiac has no
  plane 1**, because plane 1 *is* the ecliptic of the anchor epoch and such a
  zodiac has none; any epoch a server chose would be the server's and not the
  zodiac's. A request for one is `kOErrUnsupported` (A.17 code 2).
- **siderealPlane 2 (invariable plane).** Positions are projected onto the
  invariable plane of the solar system (the orientation the server names in its
  engine description); longitude is counted along that plane from the zero point
  projected onto it. Servers that implement it advertise it (A.3 0x0008).
  - **For an epoch-anchored zodiac the zero point is the zodiac's own
    zero-point DIRECTION — the direction at longitude A₀ on the mean ecliptic of
    t₀ — projected onto the plane.** Not the equinox of t₀ carried onto the
    plane with the ayanamsa then walked along it: projection between planes
    inclined by 1.578701° is not longitude-preserving, so walking the arc before
    projecting differs from walking it after, by **31.5″ under Fagan/Bradley and
    30.4″ under Lahiri**. It depends on *which* zodiac was asked for, and a
    plane does not know that — which is the tell that such an origin is tied to
    the equinox rather than to the sky.
  - **For an instant-defined zodiac the zero point is the direction at sidereal
    longitude 0 on the ecliptic of the REQUEST INSTANT, projected onto the
    plane.** This is the same construction that is *wrong* for an
    epoch-anchored zodiac, and the difference is not a special case: there it
    uses the zero point's projection onto the ecliptic of date instead of the
    zero point itself (30.75″ out for Lahiri), while here the zero point
    genuinely *is* defined on that ecliptic. The origin therefore moves with the
    instant and is rebuilt per row.
- The ayanamsa column reports the value subtracted for the row: A(t) for plane
  0, A₀ for planes 1 and 2 — and, for an instant-defined zodiac on plane 2, that
  instant's own value.

**Orbit points** (kind 1):
- **Which orbit.** The body's orbit about the Sun (heliocentric) — or about the
  solar-system barycentre for method 3 — and, for the Moon (301), its orbit
  about the Earth.
- **Nodes** lie on the ecliptic of the profile's frame (the mean ecliptic of
  date for frames 0 and 1; the J2000 ecliptic for frames 2 and 3). The node is
  the point on the orbit at that plane crossing, at the orbit's radius there.
- **Apsides** are the points of the orbit at pericentre, a(1−e), and apocentre,
  a(1+e); method 4 answers the empty focus, 2ae from the centre, in place of
  the apocentre.
- **Corrections apply as sent**, as they do to a body, and the three terms are
  defined independently (see **Corrections** above). The answered coordinates
  are the point as seen from the profile's observer, in its frame and plane,
  with the zodiac applied.
  - *Deflection* and *aberration* are as for a body, applied to the direction
    of the point.
  - *Light time* retards the point to the instant its light would have left it.
    An orbit point emits no light, so this is a **convention**, not an
    observable, and two engines may mean different operations by it. Measured
    2026-09-17 against Prometheia: Swiss's light-time term on the Moon's true
    node is 0.0031", theirs is 19.10" -- four orders of magnitude apart in the
    intermediate -- while the two apparent answers agree to 0.0002". Neither
    is wrong; they are different conventions for a point that has none.
- **Corrections on an orbit point are not independently meaningful.** The point
  is a construction rather than an emitter, so a proper SUBSET of the three
  terms is well defined only within one implementation. Two answers are
  interoperable when all three terms were applied, or when none were. A server
  MAY answer a proper subset, and MAY report which terms it applied in META's
  `corrApplied`; a client
  MUST NOT compare such an answer across servers. This is the reason the
  astrometric lunar nodes of two conforming servers may differ by 19", while
  their apparent ones agree to 0.0002".
- **Osculating (1, 3)** elements come from the body's state vector with
  μ = G(M_centre + M_body) from the ephemeris's own constants (M_body 0 where
  unknown).
- **Mean (0)** elements are the server's mean-element model, **named in the
  object's source string** (source strings are
  `<engine> | <ephemeris> | <model>`, the first two fields stable and the third
  free text, e.g. `prometheia 0.1.0 | JPL DE440 | mean elements: DE440 secular fit`) (e.g. "mean elements: DE440 secular fit",
  "Moon: Simon et al. 1994"). Mean points legitimately differ between models.
- **Interpolated (2)** is the Moon's "natural" apogee and perigee, a
  server-defined smoothing of the osculating points, also named in the source.
- **Which bodies.** Any body with an orbit about its centre. The Sun (10) and
  the solar-system barycentre (0) have none: per-object error 2.
- **Method 3 (osculating, barycentric)** takes the elements from the body's
  barycentric state with μ = the ephemeris's total solar-system GM. A server
  whose barycentric elements use another mass does not advertise method 3
  (A.3 0x0005), rather than answer a differently defined point.
- A point undefined for the orbit (a node of an orbit in the reference plane,
  an apsis of a circular orbit) is per-object error 5.

**Fixed stars** (kind 2):
- **Names accepted**, case-insensitive, single spaces:
  - an IAU (WGSN) proper name, e.g. `Aldebaran`;
  - a Bayer designation: a Greek letter as its three-letter IAU abbreviation
    (`alf`, `bet`, …), its English name (`Alpha`) or the Unicode letter (`α`),
    optionally with a component number (`bet1`, `Beta1`, `β¹`), then the IAU
    three-letter constellation abbreviation or its Latin genitive
    (`bet Sco`, `Beta Scorpii`);
  - a Flamsteed number and constellation, `8 Sco`;
  - `HR n`, `HD n`, `HIP n`.
  Servers MAY accept further traditional aliases.
- **Components.** A name that matches more than one star (`Beta Sco` for β¹
  and β²) is per-object error 6, exactly as for designations; a client
  chooses with LOOKUP.
- **Deep-sky objects.** The `deep sky` cap (A.2 bit 9) means only that kind 2
  **also resolves deep-sky designations**; WHICH catalogues is machine-readable
  in the catalogs TLV (A.3 0x000B), one entry each, e.g. `messier`, `ngc`,
  `ic`. A designation from a catalogue the server did not advertise is
  per-object error 1. (Praesepe, M 44, and the Pleiades, M 45, are used in
  astrology and live in the star catalogues; a server with only the Messier
  catalogue advertises the bit and that one entry.) Two notes for clients: a
  Messier number is not necessarily an extended object -- M 40 is a double star
  and M 73 an asterism -- and these objects have no parallax, so they answer
  with `noDistance`.
- **Answered as** the star's position from the catalogue with proper motion,
  parallax and radial velocity propagated to the instant, then the corrections.
  Distance from the parallax in AU; without a parallax, `noDistance` (§3.5).
  `resolvedNaif` is INT32_MIN.

**Named hypotheticals** (kind 3): the token names a body; its orbital
elements are **server-defined** and the object's source string names the set
used. A client that needs a hypothetical body computed from particular
elements, identically on every server, sends it as kind 4 with those elements.

**Elements** (kind 4):
- **Motion is pure two-body Keplerian**: at each instant the polynomial
  elements are evaluated at T = (t_TT − epoch)/36525 and the position is the
  Kepler solution with those elements; no perturbations.
- μ = GM of the centre (Sun or Earth) from the ephemeris's constants, the body
  massless.
- The elements refer to the mean ecliptic and equinox named by `equinox`.
- Light time, deflection and aberration apply as to a body (light time through
  the same two-body motion); rates are as for any body.

**Error text** (META errText, ERROR text) is covered by §3.8: it never quotes
instants, places or request contents. Servers rewrite engine messages that
would.

### 3.6 Extension rules

1. **Fixed layouts do not change within version 4.** New fields and new
   behaviour arrive as TLVs, together with a caps bit or capability TLV when the
   client must know about them.
2. **The version is bumped only when a fixed layout must break.** The layouts
   frozen by §3.3 never break.
3. **Registries (Appendix A) grow without a version bump.** Values are appended
   and never reused.
4. **Neither end sends what the other did not advertise.**
5. **Experimental tags, types 0x7000–0x7FFF and tags 0x7000–0x7FFF** are for
   trials between consenting implementations and MUST NOT ship enabled by default.

### 3.7 Caching

- **The key** is `datasetId` followed by the question block's bytes, exactly as
  received. Canonical encoding (§3.1) makes equal questions equal bytes.
- **Delivery fields are outside the key.** A question asked for f32 in small
  chunks is a cache hit on the f64 answer.
- **Client caches** MUST be keyed on `datasetId` as well, and MUST be dropped
  when the server address changes.

### 3.8 Security and privacy

- **Logs.** Servers MUST NOT log request contents (instants, places, bodies)
  unless the operator explicitly enables it, as `astrolog-ephd --log-contents`
  does. Tokens are never logged.
- **Error text** — ERROR's text and every META errText — never contains
  instants, sites or other request contents; servers rewrite engine messages
  that would (e.g. "outside the ephemeris's time coverage", not "jd 2461300
  outside coverage").
- **Hardening.** Every parser is bounds-checked on truncated input and never
  allocates in proportion to an unvalidated count before checking it against a
  limit.

### 3.9 Performance and accuracy

Both are requirements, and the protocol is built so that neither is bought
with the other. **An engine never trades accuracy for speed silently.**

- **Every approximation is on the wire.** `approximated` (the body answered is
  not the body asked), `extrapolated` (outside the data's fitted range),
  `ratesApprox` with its bound (A.3 0x0013), `noDistance`, a segment's
  `errArcsec` and `errRelDist`, and σ where the engine has it. A server that
  cannot answer a question to its normal accuracy says so per object rather
  than quietly answering something else.
- **f32 is a delivery choice, never a computation one.** Servers compute in
  f64 and round at the last step; the cache holds the f64 answer (§3.7).
- **The batch is the unit of work.** One REQUEST carries a whole cast --
  every object, every instant, every profile -- because a request per object
  costs a round trip each and defeats the server's cache. A client that needs
  a second window before the first is drawn sends it with `priority = 1`, and
  the server answers interactive work first.
- **Segments are the answer to animation** (§3.4): one fit covers a span the
  client then evaluates locally at any instant, with the error it asked for
  stated on the wire.
- **CANCEL exists so that speed is not wasted:** an animation that moves on
  drops the window it no longer needs, and the server stops computing it
  (§3.4: in blocks, and caching nothing from a cancelled request).
- **Share the per-instant work across a cast.** Computing time-major -- every
  object at one instant, then the next -- lets the observer, Sun, frame and
  nutation work be memoised across the objects of a cast. Prometheia measured
  56 → 2.9 µs per object-row from this family of changes: interpolating the
  nutation series from half-day nodes (0.004 µas against the full series),
  caching those nodes, Newton's method for light time, memoising the observer
  and Sun states, and then time-major ordering for a further 17%. The ordering
  is free to try -- the answer cannot depend on it -- but it only pays where
  the per-instant work is memoised rather than recomputed inside the engine.
- **The cache key is canonical** (§3.7), so the same question asked twice --
  in either precision, in any chunking -- is computed once.
- **A request's cost is dominated by the time WINDOW it touches**, not by the
  objects in it, and that cost is shared by everything else touching the same
  window. Measured on Prometheia: the frame work for a year is about 730
  nutation nodes, roughly 15 ms, paid once and then free for every body, every
  instant and every client in that window; per-object costs sit within a factor
  of six of each other (an ephemeris body 14.2 µs, a catalogue body 5.3, a star
  2.4, an orbit point 3.5), most of the spread being one light-time solve. So
  the honest unit for a limit is SPAN, which is what `maxSegSpanDays` measures,
  and the lattice (§3.4) shares not only fitted segments but the frame work
  beneath them.
- **Ask a server for the same instant twice, and ask it in a different
  ORDER.** If the second identical instant costs what the first did, something
  is being recomputed that should have been reused; and if one object costs
  several times another of its kind, find out whether that is the object or
  merely the one asked FIRST -- a per-window cost paid by whoever arrives first
  reads exactly like an expensive body. Prometheia's engine had both: a
  catalogue record decoded per position, and then a "28× more expensive"
  integrated body that turned out to be a planet paying for the window's
  nutation nodes. Each was hidden under a speedup already banked --
  measured on Prometheia's engine, where a catalogue record was decoded per
  position outside the memo everything else amortised into, and looked like a
  28× cost for integrated bodies until it was fixed (1,013 → 5.9 µs, which is
  now cheaper than a DE read). It is a one-line experiment and it finds what
  profiling a realistic workload hides, so each implementation should run it on
  its own engine rather than assume.
- **Nothing is per connection that need not be.** The only state the protocol
  requires a server to hold for a connection is the negotiated version, the
  token's budget and the answers in flight. Everything else is
  content-addressed -- the cache key is `datasetId` plus the question block --
  so any loop, thread or process may serve any request, a reconnecting client
  loses nothing but what was in flight, and a server scales by adding loops
  rather than by remembering clients. A future implementation MUST NOT invent a
  session that answers differ by.
- **Budgets and limits are per connection, not per answer:** a server states
  its bound in WELCOME (`maxCells`) and a client keeps a request under it
  rather than discovering the limit by being refused.

**The three workloads a client brings**, because they want different things:
- **A cast.** 30-80 objects at one instant, in front of a user: latency-bound.
  Round trips hurt; this is where one request per cast matters.
- **Animation.** A window of instants at a fixed step, about 1000 rows, with
  the next window prefetched at priority 1 while the current one draws.
- **Scanning**, the heaviest and the one that shapes segments. Astrolog searches
  for events -- aspects forming, ingresses, stations, voids, eclipses,
  progressed hits -- by sampling uniformly and interpolating between adjacent
  samples: `-d` is 48 divisions a day by default, so a month is about 1500 full
  casts and a year about 4400 (charts3.cpp:270 and the interpolation at :354).
  The event time then comes from LINEAR interpolation between two bracketing
  samples, so its accuracy is limited by the sampling step, not by the
  ephemeris.

For scanning, **segments are an accuracy change, not a bandwidth one**: the
client evaluates any instant locally, so an event time comes from root-finding
on the polynomial instead of interpolating between half-hour samples, and the
answer stops depending on `-d`. Rates from the analytic derivative make a
station -- a sign change of the longitude rate -- exact the same way, which is
why a segment carries a measured rate residual. A fitter should therefore be
judged on whether the polynomial's ROOTS land where the sampled function's
roots would, over spans of a month to a year, rather than on looking smooth;
and where a body needs an unreasonable degree over such a span, more segments
at a sane degree beat a refusal. The Moon decides it: 13° a day, and a
year-long scan of lunar aspects is the commonest heavy search in the program.

This is also why the reserved event-search message types (16-31) may stay
reserved: with segments a client can search correctly for itself, which is a
better place for that complexity than the protocol.

Measured on the reference implementations (`tools/ephsrv-bench.sh`, and
Prometheia's own bench): a cold 30-body 1000-row window is about a
core-second of computation, and a cached one is delivered in milliseconds;
the numbers that matter to a GUI are in EPHEMERIS_SERVER_PRODUCTION_PLAN.md.

### 3.9a How the implementations work together

Two implementations wrote this specification at once, and the rules below are
how they stayed one protocol. They bind any third implementation as well.

These are rules about the **protocol**. How the two *sessions* negotiated it --
the shape of a round, the discipline that turned disagreements into findings,
who decides what, and the standing rule that a peer agent cannot grant
permission -- is `EPHEMERIS_PROTOCOL_COLLABORATION.md`.

1. **Never branch on who the peer is.** No code reads `serverName`, `engine`
   or `clientName` and behaves differently. Anything one end needs to know
   about the other is a capability bit or a TLV, registered in Appendix A and
   advertised.
2. **Advertise only what is implemented and tested.** A capability advertised
   because it mostly works is worse than one absent: the other end will use it.
   A bit whose behaviour is incomplete, or which no test exercises, stays dark.
3. **No silent fallbacks.** An engine that cannot answer the question as asked
   says so per object, with a code; it never quietly answers a nearby question.
   A substitution is visible on the wire (`resolvedNaif`, `approximated`) or it
   does not happen.
4. **Errors by meaning, not convenience.** A failure maps to the A.17 code
   that is true. If none fits, the registry gains one, with a fixture.
5. **No sniffing, no speculative parsing.** The envelope's version and the
   advertised capabilities are the whole truth; nothing is inferred from
   content. A place where guessing is tempting is a gap in this document.
6. **Every rule ships with a fixture or a gate**, in the same change that
   agrees it. A rule that exists only in prose gets implemented two ways.
7. **No leniency for one's own convenience.** Neither end accepts a message
   this document makes malformed because refusing it is inconvenient today; a
   lenient parser hides the other end's bug until a third implementation
   appears. An end that is strict where this document is silent fixes the
   document, not the parser.
8. **The engine does not know about the wire.** The protocol stops at each
   side's boundary and is translated there. A protocol field that reaches into
   engine semantics makes that engine a function of this document's version,
   and every other consumer of that engine inherits it.
9. **An objection is a finding.** A disagreement between two readings is the
   most valuable output either side produces, and it is resolved by changing
   this document until only one reading survives -- never by one side
   accommodating the other's code. What earned its place here came from
   objections: a rule clients could not obey (an unknown match could not be
   skipped without its length), a rule that named values but not the flag
   fields beside them, a checksum whose input had two readings, and a
   capability that would have been advertised for behaviour that could not
   honour it.
10. **A rule that needs a comment to implement is not finished.** If either
    side writes a comment explaining what this document meant in order to make
    code work, that sentence comes back here as a change.

### 3.10 Conformance fixtures

`ephsrv/conformance/` holds complete messages (envelope included) as hex, with
`MANIFEST.tsv` listing:

| column | contents |
|---|---|
| file | fixture name |
| direction | `c2s` or `s2c` |
| type | message type |
| expected outcome | `ok`, `malformed` (ERROR 1) or `unsupported` (ERROR 11) |
| note | one line |

**The set is atomic and verifiable.** A regeneration writes every `.hex`
first and `MANIFEST.tsv` last, and the manifest's header carries

```
# set-sha256 <64 hex digits>
```

the SHA-256 over **the bytes of each fixture file exactly as committed**
(hex digits, line breaks and all, including the trailing newline),
concatenated in the order the manifest's data rows list them. The manifest
itself is not part of the input. The verbatim reading is deliberate: it makes
the line a checksum of the directory as committed rather than of an
interpretation of it, so a file rewritten with different formatting — a
semantic no-op — still shows up, and neither side has to decode anything to
check a set. A reader that computes a different digest has an inconsistent or
half-written set and MUST refuse to report verdicts rather than report wrong
ones.

**Who generates and runs them**
- `tools/ephproto4-fixtures.py` is an **independent reference encoder**, written
  from this section and not from the C++ code. It generates the fixtures.
- Astrolog's codec tests and Prometheia's `server_ephproto_matches_astrolog`
  test both parse every fixture and check the expected outcome.
- An `ok` fixture must re-encode to the identical bytes.
- **What fixtures cannot prove.** A fixture is one message, so the rules about
  sequences — chunks contiguous, ascending and covering every row exactly once;
  segments contiguous to 1e-9 day; an answer following its own request — are
  server-side tests on each side, not fixtures.

## 4. Source plugins

### 4.1 Model

A **source** is anything that answers position questions: a local library
over data files, formulas, or a remote service. Sources live in a
compiled-in table in the shared core (`ephem.h`, `ephem.cpp`; the core
group of `Makefile.srcs`; no `#ifdef QT`), in the house style (`CONST`,
`P(())`, flags as `flag`).

```c
typedef struct _EphSrcDef {
  CONST char *szKey;          // "swiss" -- CLI and settings key
  CONST char *szName;         // "Swiss Ephemeris files" -- dialogs
  CONST char *szDesc;         // one line for the dialog
  CONST EPHPARAM *rgParam;    // declared settings (4.3)
  int cParam;
  flag (*FAvailable)(char *szWhy, int cch);  // compiled, files, transport
  void (*GetCaps)(EPHCAPS *pcaps);           // the WELCOME capability model
  int  (*State)(char *sz, int cch);          // esReady/esConnecting/esFailed + text
  void (*Start)(void);                       // local: open; remote: background connect
  void (*Stop)(void);
  flag (*FSubmit)(EPHQUERY *pq);             // non-blocking; local ones compute here
  flag (*FRead)(CONST EPHQUERY *pq, int iObj, int iRow, EPHROW *prow);
  void (*Hint)(CONST EPHQUERY *pq);          // the next window, for prefetch
  int  (*NLookup)(CONST char *sz, EPHMATCH *rgm, int cMax);
} EPHSRCDEF;
```

**Query and row types**
- `EPHQUERY` is the host-side form of a version 4 question block (§3.4),
  declared in `ephproto.h`, so the wire and the plugins cannot drift apart.
  (As implemented it lives in `ephem.h` — `ephproto.h` is the locked
  artifact; work log item 6 has the reason and the shape.)
  - Each OBJECT also carries a host-only `nNative`, the source's own id for the
    body, which never goes on the wire. The local Swiss plugin uses it to make
    exactly today's calls.
  - For example: the Moon's nodes through the named `SE_TRUE_NODE` and
    `SE_MEAN_NODE` bodies versus `swe_nod_aps` for custom points; `-Ye b 1` as
    `SE_AST_OFFSET+1`; a `seorbel.txt` index.
- `EPHROW` holds the columns, σ, ayanamsa, errCode, resolvedNaif and the key of
  the source that answered.

**Casting**
- **One pass per cast.** The host builds one query per cast (all objects, their
  profiles and the side calls), submits it to each source it needs, calls
  `EphCollect(msDeadline)` once, then reads.
- **Fallback chain.** The selection is an ordered list (§5.1). For each object
  the host asks the first source in the list that is available, advertises the
  capability, and has not already failed that object in this cast. When a
  source fails an object, the next source is asked for the same rows.
  - One quiet notice per cast says that a fallback served something.
  - The provenance is shown in the Ephemeris Settings status and is available
    to text charts and AstroExpressions.
- **Side calls use the same path**, as instant lists: `RProgArc`, the topocentric
  Sun in eclipses, fixed stars, planet phenomena and asteroid listings.
- **Emulation.** Where today's code emulates a capability, the host keeps doing
  it for any source:
  - geocentric → heliocentric with light time (today's Horizons path,
    calc.cpp:1131);
  - the South Node as the opposite point;
  - placeholder speeds.
- **`FCm*` predicates are replaced** by `FEphSpeeds()` (rates available) and
  `FEphLegacyCast()` (Matrix and None).
- **Matrix and None** keep their **legacy-cast hook**: `ComputePlanets` and
  `ComputeLunar` inside CastChart, plus Matrix dates and houses. They therefore
  stay byte-identical to today.

### 4.2 Built-in sources

| key | module | kind | parameters | notes |
|---|---|---|---|---|
| `swiss` | ephswiss.cpp | local | (uses `-Yi` paths) | Swiss Ephemeris files; bit-exact with today |
| `moshier` | ephswiss.cpp | local | — | analytic; major planets and Moon; always available |
| `jpl` | ephswiss.cpp | local | `file` | Swiss over a JPL DE file (a new parameter; today there is no setting and the default is de431.eph) |
| `prometheia` | ephprom.cpp | local | `ephemeris`, `catalog`, `perturbers` | `#ifdef PROMETHEIA`, detected with `pkg-config prometheia`; C API `prometheia_calc*`, `calc_orbit_point*`, `engine_lookup`; ΔT hook bound to Astrolog's |
| `server` | ephserver.cpp + transport | remote | `url`, `token` | protocol v4; astrolog-ephd or prometheiad |
| `horizons` | ephhorizons.cpp | remote | — | rewritten to take instants and batch per body over the Horizons API |
| `matrix` | matrix.cpp | local | — | Sun–Pluto, Moon, mean node; legacy cast |
| `none` | ephem.cpp | local | — | no bodies; legacy cast |

**Default chain.** When the user names only a primary, sources are appended in
quality order after it: `swiss`, `jpl`, `prometheia`, `moshier`, `matrix`. The
same source is never listed twice, and unavailable sources are skipped at cast
time, not at selection time.

### 4.3 Parameters

```c
typedef struct _EphParam {
  CONST char *szKey;     // "url"
  CONST char *szLabel;   // "Server Address"
  int nKind;             // epkText, epkPath, epkFile, epkUrl, epkToken
  CONST char *szDefault; // "" means the source's own default
} EPHPARAM;
```

All parameters of all sources share one generated index space: `cEphParam`,
with enum names like `epServerUrl`. Values live in
`us.rgszEphParam[cEphParam]`. The command-line key is `source.param`, so
`-bP server.url wss://host`. The dialog's generic rows are built from the
table, and a `epkToken` parameter is masked there.

### 4.4 Remote adapter and transports

**The adapter** is generalised from qtdriver.cpp's server client and lives in
core. It provides:
- the window cache, keyed on the datasetId plus the question;
- the animation prefetch through `Hint`;
- a bounded wait;
- a background connect with a fast ladder: 1 s doubling to 60 s, before and
  after a session;
- a recast when a source becomes ready;
- the once-per-cast soft warning;
- terminal refusals (ERROR 7 and 8) that stop the ladder until the settings
  change.

**Transports** implement `Send`, `Pump(msMax)` and `State`:
- **Qt:** QWebSocket.
- **Win32:** WinHTTP WebSocket (Windows 8 and later, native TLS).
- **Console:** a socket client grown from `ephsrv/eph_wsclient.cpp`, with
  optional OpenSSL.

**What does not exist any more:** the required-server dialog, exit code 86, and
the `-0n` fast-fail path. Nothing pops up while a connection is being made.

## 5. Selection, settings, dialogs

### 5.1 State

`us.szEphemSource` holds the chain (e.g. `server,swiss,moshier`), plus
`us.rgszEphParam[]`. These replace **in place**, in astrolog.h and in data.cpp's
positional initializer, the fields `fEphemFiles`, `nSwissEph`, `fMatrixPla`,
`szEphSrv`, `szEphSrvToken`, `fNoOldCalc` and `fNoNetwork`. The default is
`"swiss"` under `EPHEM`, else `"matrix"`. `settingsfields.h` is regenerated,
and the settings sweeps validate source keys.

Changing the source resets `is.fSwissPathSet`, the warning latch and the
adapter caches.

### 5.2 Command line

| spelling | meaning |
|---|---|
| `-bE <source[,source…]>` | select the chain; idempotent |
| `-bP <source.param> <value>` | set a parameter; `""` restores the default |

**Legacy spellings** keep loading exactly as before. They update a parse-time
shadow {files, n, matrix} the way `NSwb` does today, and the chain is
recomputed from the shadow after each one:
- `-b`, `-bs`, `-bj`, `-bJ`, `-bS`, `-bm`, with their `=`, `_` and `:` forms;
- `-bW` and `-bT` set `server.url` and `server.token`;
- `-0b` and `-0n` are accepted and do nothing (registered as inert in
  `tools/inert_option_audit.py`).

-H documents only the new forms, plus one line naming the older spellings.

### 5.3 Settings writer

The writer emits one `-bE` line plus one `-bP` line per non-default parameter.
There is no order dependence, and `=0b`/`=0n` are not written.
`astrolog.as` is updated. `nrvate.as` is the maintainer's file and is left
alone: its old lines keep loading.

### 5.4 Ephemeris Settings dialog (both builds)

**Where it is defined and wired**
- **Resource.** `dlgEphem` in astrolog.rc (resource.h: dialog 226, command
  40366, controls from 1735).
- **Menu.** Setting → "E&phemeris Settings..." after Calculation Settings.
- **Win32.** `DlgEphem` in wdialog.cpp, a case in wdriver.cpp, a declaration in
  extern.h.
- **Qt.** `ShowEphemDialogQt` in qtdialog.cpp; the menu item in qtdriver.cpp
  `BuildSettingMenu`.

**Controls**
- a primary-source list, where unavailable sources are shown with their reason;
- the fallback order;
- a status line (state and provenance), refreshed by a Win32 timer or a Qt
  signal;
- Connect/Test;
- four generic parameter rows: a label, an edit field, and Browse for paths.

**What leaves Calculation Settings.** The method combo, and the Qt-only
server-address and token rows. `QT_ONLY_ROWS` for dlgCalc is removed from
`tools/rc2qt.py`.

## 6. Appendix A — registries

These registries are also published as **`ephsrv/registries.json`**,
generated from this appendix by `tools/gen-registries.py` and regenerated and
diffed by `make check`; `ephsrv/ephproto_test.cpp` requires the codec's own
constants to agree with it, by name. An implementation may vendor that file
rather than transcribe the lists below. Values are appended and never reused
(§3.6), so a pinned copy stays valid; a copy that is missing values is merely
older than the server it is talking to.

**A.1 Message types:**
- 1 HELLO
- 2 WELCOME
- 3 REQUEST
- 4 DATA
- 5 ERROR
- 6 PING
- 7 PONG
- 8 CANCEL
- 9 LOOKUP
- 10 LOOKUP_RESULT
- 11 SUBSCRIBE*
- 12 UNSUBSCRIBE*
- 13 LIST*
- 14 LIST_RESULT*
- 15 SEGDATA
- 16–31 event searches*
- 32–0x6FFF unassigned
- 0x7000–0x7FFF experimental

(* = reserved, no layout yet.)

**A.2 caps bits** (in HELLO clientCaps and WELCOME caps):
- 0 f32
- 1 zstd
- 2 cancel
- 3 lookup
- 4 instant lists
- 5 priority
- 6 segments
- 7 designations (kind 5)
- 8 ΔT tables (REQUEST TLV 0x8004)
- 9 deep sky (kind 2 also resolves `M n`, `NGC n`, `IC n`)

**A.3 WELCOME capability TLVs:**
| tag | payload |
|---|---|
| 0x0001 | object kinds, u32 bitmask of A.12 |
| 0x0002 | observers, u32 bitmask of A.5 |
| 0x0003 | planes u32, forms u32, frames u32 (bitmasks of A.6) |
| 0x0004 | correction masks per observer: u8 n, n × {u32 observers (A.5 bitmask), u8 mask (A.7)} — each pair says that mask is honoured for those observers |
| 0x0005 | orbit points u32 (A.13), orbit methods u32 (A.14) |
| 0x0006 | extra columns u32 (A.10) |
| 0x0007 | zodiacs: u16 n, n × str8 (A.11 tokens) |
| 0x0008 | sidereal planes u32 (A.8) |
| 0x0009 | time scales u32 (A.9) |
| 0x000A | coverage: u16 n, n × {str8 id, TIME min, TIME max} |
| 0x000B | catalogs: u16 n, n × {str8 id, str8 snapshot} |
| 0x000C | ΔT model: str8 |
| 0x000D | precession models: u16 n, n × str8 |
| 0x000E | rate: u32 cellsPerSec, u32 burst |
| 0x000F | segments: u8 maxDegree, u8 ×3 reserved, u32 maxSegmentsPerObject, f32 minErrArcsec, u32 kinds (A.12 bitmask of what it will fit), u32 maxSegSpanDays (the widest span it will fit in one request; 0 = no stated bound) |
| 0x0010 | lookup: u16 maxMatches |
| 0x0011 | hypotheticals: u16 n, n × str8 (A.15 tokens served) |
| 0x0012 | equinoxes for elements: u32 bitmask of A.16 |
| 0x0013 | rates bound: f32 degPerDay, f32 auPerDay — the largest difference of the server's rates from central differences of its positions (§3.5a); absent means rates meet the 1e-5 °/day, 1e-9 AU/day tolerance |
| 0x0014 | corrections by kind: u8 n, n × {u32 observers (A.5), u32 kinds (A.4), u8 mask (A.7)} — ADDS to tag 0x0004 per (observer, kind). 0x0004 is the intersection over kinds; a pair no entry names falls back to it, so a kind-uniform server sends nothing and the two cannot contradict. A profile's mask is checked against every (observer, kind) of the objects referencing it, ERROR 11 for the whole request; an unreferenced profile against 0x0004 alone. Mask bits above 0x07 are reserved and MUST be zero (§3.1: reject, do not normalise); unknown observer and kind bits are ignored |

**A.4 REQUEST TLVs:**
- 0x0003 precession model, str8 (non-critical; an unknown model falls back and
  sets ignoredExt)
- 0x8001 ephemeris pin, str8 (critical)
- 0x8002 catalog pin, str8 (critical)
- 0x8003 datasetId pin, str8 (critical) — the whole identity in one string;
  ERROR 5 if the server's datasetId differs
- 0x8004 ΔT table (critical; requires the `ΔT tables` cap): u32 n (2..65535
  entries allowed by the TLV length), n × {TIME t, f64 deltaTSec}, **instants in
  TT** whatever the request's time scale (§3.5), strictly ascending, all finite.
  When present the REQUEST's `deltaTSec` MUST be the canonical NaN.

**A.5 Observers:**
- 0 geocentric
- 1 topocentric
- 2 heliocentric
- 3 solar-system barycentre
- 4 body

**A.6 Planes, forms and frames:**
- Planes: 0 ecliptic, 1 equator.
- Forms: 0 spherical, 1 rectangular.
- Frames: 0 true of date, 1 mean of date, 2 J2000 (mean), 3 ICRF.

**A.7 Correction bits:** 1 light time, 2 gravitational deflection, 4 aberration.

**A.8 Sidereal planes:** 0 ecliptic of date, 1 ecliptic of the anchor epoch,
2 invariable plane of the solar system.

**A.9 Time scales:** 0 UT1, 1 TT, 2 TDB.

**A.10 Extra column bits:**
- bit 0: σ, arcsec
- bit 1: ayanamsa applied, deg
- bit 2: light time, days
- bit 3: ΔT used, s

**A.11 Zodiac tokens.** The Swiss Ephemeris 2.10.03 sidereal modes are listed
in `SE_SIDM_*` order. Other engines implement whichever subset they choose and
advertise it (A.3 0x0007).

- `fagan-bradley`, `lahiri`, `deluce`, `raman`, `usha-shashi`, `krishnamurti`,
  `djwhal-khul`, `yukteshwar`, `jn-bhasin`
- `babyl-kugler1`, `babyl-kugler2`, `babyl-kugler3`, `babyl-huber`,
  `babyl-etpsc`, `aldebaran-15tau`, `hipparchos`, `sassanian`, `galcent-0sag`
- `j2000`, `j1900`, `b1950`
- `suryasiddhanta`, `suryasiddhanta-msun`, `aryabhata`, `aryabhata-msun`,
  `ss-revati`, `ss-citra`, `true-citra`, `true-revati`, `true-pushya`
- `galcent-rgilbrand`, `galequ-iau1958`, `galequ-true`, `galequ-mula`,
  `galalign-mardyks`, `true-mula`, `galcent-mula-wilhelm`, `aryabhata-522`
- `babyl-britton`, `true-sheoran`, `galcent-cochrane`, `galequ-fiorenza`,
  `valens-moon`, `lahiri-1940`, `lahiri-vp285`, `krishnamurti-vp291`,
  `lahiri-icrc`
- `user` (anchored by the PROFILE's anchor fields)

**A.12 Object kinds:**
- 0 body
- 1 orbit point
- 2 fixed star
- 3 named hypothetical
- 4 elements
- 5 designation

**A.13 Orbit points:** 0 ascending node, 1 descending node, 2 perihelion
(perigee), 3 aphelion (apogee).

**A.14 Orbit methods:**
- 0 mean
- 1 osculating
- 2 interpolated ("natural"; the Moon only in Swiss)
- 3 osculating, barycentric
- 4 focal point

**A.15 Named hypotheticals.** Tokens name bodies; their elements are
server-defined (§3.5a), and the source string names the set. The tokens are
the conventional names of these bodies (listed in the order of Swiss's
`seorbel.txt`, for reference only):

| token | seorbel.txt entry |
|---|---|
| `cupido`, `hades`, `zeus`, `kronos`, `apollon`, `admetos`, `vulcanus`, `poseidon` | Uranians, 1–8 |
| `isis-transpluto` | 9 |
| `nibiru` | 10 |
| `harrington` | 11 |
| `neptune-leverrier` | 12 |
| `neptune-adams` | 13 |
| `pluto-lowell` | 14 |
| `pluto-pickering` | 15 |
| `vulcan` | 16 |
| `white-moon` | 17 (Selena, the T-term form) |
| `proserpina` | 18 |
| `waldemath` | 19 |

A server MAY serve more; each token names one body.

**A.16 Element equinoxes:** 0 J2000, 1 B1950, 2 J1900, 3 of date, 4 explicit
JD (`equinoxJd`). The element epoch is a TIME in TT. Polynomial terms are in
T = (t_TT − epoch) / 36525 Julian centuries, as in `seorbel.txt`.

**A.17 Per-object error codes:**
- 0 none
- 1 unknown body or name
- 2 unsupported by this source (kind, observer, option or body)
- 3 outside the data's time coverage
- 4 data unavailable (for example a missing file)
- 5 point undefined (for example the node of a zero-inclination orbit)
- 6 ambiguous name
- 7 numerical failure
- 8 internal

**A.18 META flags:**
- bit 0 approximated (resolvedNaif differs from the request, e.g. 499 answered
  as 4)
- bit 1 extrapolated
- bit 2 hasSigma
- bit 3 partial
- bit 4 noSpeeds
- bit 5 noDistance (a star without a parallax)
- bit 6 ratesApprox (rates may exceed the §3.5a tolerance; see A.3 0x0013)

**A.19 ERROR codes:**
- 1 malformed or non-canonical
- 2 over a WELCOME limit
- 3 unknown message type
- 4 internal
- 5 source, data or pin unavailable
- 6 rate limited
- 7 token required or unknown
- 8 version
- 9 busy (too many unread answers)
- 10 cancelled
- 11 unsupported (a value, capability or critical extension not advertised)
- 12 draining (retry elsewhere)

**A.21 Element centres** (kind 4 `centre`): 0 Sun, 1 Earth.

**A.20 Precession model tokens** (REQUEST TLV 0x0003, WELCOME TLV 0x000D):
- `iau2006` — Capitaine et al. 2003, IAU 2006 (the default)
- `vondrak2011` — Vondrák, Capitaine & Wallace 2011, long-term

## 6B. Appendix B — mapping to the Swiss Ephemeris

A single shared header, `ephsrv/ephswiss.h`, holds the mapping. Both
astrolog-ephd and Astrolog's `swiss`/`moshier`/`jpl` plugins use it.

**Bodies**
- **Canonical Swiss body first.**
  - 10 → `SE_SUN`
  - 301 → `SE_MOON`
  - 199/299 → `SE_MERCURY`/`SE_VENUS`
  - 399 → `SE_EARTH`
  - 4–9 → `SE_MARS`..`SE_PLUTO` (system barycentres, Swiss's default)
  - 20000001–4 → `SE_CERES`..`SE_VESTA`
  - 20002060 → `SE_CHIRON`
  - 20005145 → `SE_PHOLUS`
  - 20134340 → `SE_PLUTO`, flagged approximated
- **Other numbered asteroids.** 20000000+N → `SE_AST_OFFSET + N`.
- **Body centres and moons.** x99 and moon ids → `SE_PLMOON_OFFSET + naif`.
  199–499 are answered as the barycentre and flagged approximated. 0 and 3 are
  per-object error 2.

**Orbit points**
- On 301, (point 0, method 0) → `SE_MEAN_NODE`, (0, 1) → `SE_TRUE_NODE`,
  (3, 0) → `SE_MEAN_APOG`, (3, 1) → `SE_OSCU_APOG`, (3, 2) → `SE_INTP_APOG`
  and (2, 2) → `SE_INTP_PERG`.
- Everything else → `swe_nod_aps` with `SE_NODBIT_MEAN`/`OSCU`/`OSCU_BAR`/`FOPOINT`.
- The descending node is the ascending node's opposite for the named bodies.
- A host-only `nNative` keeps Astrolog's custom Moon points on `swe_nod_aps`.

**Stars and hypotheticals**
- **Stars:** `swe_fixstar2`.
- **Hypotheticals:** `SE_FICT_OFFSET` + the A.15 index.
- **Elements:** the fork's new orbital-elements entry point.

**Options**
- **Observer** → `SEFLG_TOPOCTR` plus `swe_set_topo`, `SEFLG_HELCTR`,
  `SEFLG_BARYCTR`, or `swe_calc_pctr`.
- **Plane** → `SEFLG_EQUATORIAL`. **Form** → `SEFLG_XYZ`.
- **Frame:**
  - true of date: none
  - mean of date: `SEFLG_NONUT`
  - J2000: `SEFLG_J2000 | SEFLG_NONUT`
  - ICRF: `SEFLG_ICRS | SEFLG_J2000 | SEFLG_NONUT`
- **Corrections.** Swiss can honour the masks 7, 0 (`SEFLG_TRUEPOS`),
  3 (`SEFLG_NOABERR`), 5 (`SEFLG_NOGDEFL`) and 1 (`NOABERR|NOGDEFL`).
- **Speeds** → `SEFLG_SPEED`.
- **Zodiac** → `SEFLG_SIDEREAL` with `swe_set_sid_mode`: the mode from A.11,
  `SE_SIDM_USER` with the anchor, plus `SE_SIDBIT_ECL_T0` or
  `SE_SIDBIT_SSY_PLANE` for sidereal planes 1 and 2.

  **Known defect, 2026-09-18.** Swiss declines both plane bits for 16 of the
  47 A.11 tokens — `b1950`, `j1900`, `j2000`, the five `true-*` and the eight
  `gal*` — and this server returns the plane-0 answer for them **bit
  identically**, with no error. A.8 is a request field, so a plane the server
  cannot honour is `kOErrUnsupported`, not a different answer. Measured with
  `tools/sidplane-origin.py`; recorded in `EPHEMERIS_ACCURACY_REGISTRY.md`
  §4.1, and fixed by the same change as the zero point, since that makes the
  plane transform this server's own arithmetic rather than a Swiss sidereal
  mode.
- **Time scale.** UT1 calls the `_ut` entry points (or TT = UT1 + ΔT when
  `deltaTSec` is given). TT is used directly. TDB is error 2 unless it converts
  exactly.

## 6C. Appendix C — mapping to Ephemeris Prometheia

These are Prometheia's C ABI names; the C++ engine is the same.

**Bodies**
- **Kind 0:** NAIF ids and SBDB SPK-IDs, directly.
- **Kind 1:** `prometheia_calc_orbit_point` with methods 0 and 1.
- **Kind 4:** its Kepler engine, when implemented.
- **Kind 5 and LOOKUP:** `prometheia_engine_lookup`.

**Options**
- **Observer:** 0–3 → `PROMETHEIA_CENTER_{GEO,TOPO,HELIO,BARY}CENTRIC`, and
  4 → `PROMETHEIA_CENTER_BODY` + `center_body`.
- **Plane** → `coords`. **Frame** → `frame` (ICRF, J2000, MEAN_OF_DATE,
  TRUE_OF_DATE).
- **Corrections** → `light_time`, `deflection`, `aberration`.
- **Speeds** → `speed`.
- **Zodiac:** `fagan-bradley`, `lahiri`, `user` → `sidereal` with the anchor.
  All three of A.8's sidereal planes → `sidereal_plane`
  (`PROMETHEIA_SIDEREAL_PLANE_DATE`/`_ANCHOR`/`_INVARIABLE`), since **C ABI
  6**, which appended the field. An ABI-5 build binds no longer — the binding
  check is equality — and before it only plane 0 was reachable. A sidereal
  zodiac on the equatorial plane is still refused, by §3.5.
- **Extra columns:** σ → `sigma`, `HAS_SIGMA`; ayanamsa → `ayanamsa_deg`;
  light time → `light_time_days`.
- **Time:** TT → `prometheia_calc`, UT1 → `_ut` with the ΔT hook or
  `deltaTSec`.
- **Datasets:** `datasetId` derives from the DE file, catalogs and perturbers
  loaded; the source strings come from `result.source`.

## 7. Phases

Each phase is one or more commits on `ephv4`, each passing `make check` and
the gates the phase touches.

1. **This document and the conformance fixtures.** Covers §3.10:
   `tools/ephproto4-fixtures.py` and `ephsrv/conformance/`.
2. **Protocol v4 in code.**
   - `ephproto.h`: codecs for every message, TLV, SEGDATA, LOOKUP and CANCEL,
     plus the conformance test.
   - `ephsrv/ephswiss.h`.
   - The Swiss fork's elements entry point.
   - `eph_srv.cpp`: WELCOME capabilities, profiles, LOOKUP, CANCEL, and segments
     fitted from samples.
   - `eph_wsclient.cpp`.
   - The Qt client: one request per cast.
   - Every `tools/ephsrv-*.sh` gate moves to v4, with golden bit-exact against
     the fork's swetest.
3. **Source registry.**
   - `ephem.h`/`ephem.cpp`; the `swiss`, `moshier` and `jpl` plugins;
     `matrix`/`none` as legacy cast.
   - ComputeEphem and the side calls go through the host API with the fallback
     chain.
   - Chart, switch, graphics and influence matrices byte-identical against a
     baseline.
4. **Selection re-plumb:** state, `-bE`/`-bP`, the legacy shadow, the writer,
   the locks, astrolog.as, and the settings sweeps and audits.
5. **Ephemeris Settings dialog** in both builds; Calculation Settings loses the
   combo.
6. **Remote adapter and transports** (Qt, WinHTTP, console socket).
   - `server` and `horizons` plugins.
   - The required-server dialog, exit 86 and `is.fNoEphFound` go away.
7. **Prometheia plugin** (optional dependency, makefile detection, oracle
   against Swiss on DE440).
8. **Review** of the whole branch, and a summary for the maintainer.

## 7B. Proposed, not started: an MCP render tool

**Documented at the maintainer's instruction 2026-09-18, and deliberately not
built.** Recorded here so the reasoning is not re-derived.

**Scope correction first, because it bounds everything below.** Astrolog stays
a **desktop application**. "AI-forward" is about the **data layer** — the
ephemeris. Ephemeris Prometheia is the AI-facing ephemeris (a high-speed C++
library first, with a C API, a binary protocol, a CLI, and JSON/MCP as
convenience surfaces beside them). Astrolog is **not** exposing charts as data
and is **not** growing an agent-facing chart API. A design for one was started
and scrapped on that instruction.

**What is proposed instead**, and it is a different and better thing: a channel
for an agent to ask Astrolog to *show* something.

**The use case, in the maintainer's words:** the AI session reaches chart data
through one MCP — Prometheia's — and calls Astrolog through another to
**display**. An astrologer talks to the AI conversationally, through whatever
client they like, and asks it to put up the patterns it is describing.

That framing settles what this is and what it is not. **Astrolog is the display
endpoint, not a data endpoint.** It is not asked what the Moon's longitude is;
it is asked to draw the configuration the AI has already found, in a particular
view, with particular objects and aspects showing. The agent does the finding —
it has the ephemeris — and Astrolog does what it is actually good at, which is
drawing a chart thirty years of options deep.

It also makes the shared vocabulary load-bearing rather than tidy: the AI will
be naming bodies and aspects it got from the ephemeris when it asks Astrolog to
draw them. If the two surfaces spell the same sky differently, every such call
needs a translation layer that can silently get it wrong.

**Why it would be cheap, which is the part worth keeping.** Astrolog's API
already exists and is the **switch registry**: 529 invocations in the switch
matrix, `tools/registry_audit.py` asserting that every spelling the help text
or the settings writer names resolves to a row, and it is the single way the
program is configured. A tool that takes switches is not a new API; it is an
audited one with a new transport. Measured: one headless command, no display,
switches in and a rendered chart out.

**Two shapes, in the order they should be taken:**

- **(b) Stateless render, first.** A request (time, place, objects, aspects,
  chart type) in; the rendered image and the computed positions out. No
  changes inside the shipped GUI, nothing to regress, no new attack surface,
  and it proves the vocabulary before anything ships.
- **(a) Driving the live application, after.** A listener inside the Qt build
  so an agent can put a chart in the window the user is already looking at.
  This is what was actually asked for, and it is the one with a security
  question — a local socket that can drive the user's application — so it
  wants to be opt-in and loopback-only.

**One constraint either way:** anything astronomical — bodies, zodiacs, frames,
orbit points — uses the **same A.11/A.15 tokens Prometheia's tools use**. Only
the presentational words are Astrolog's own, because those are the part an
ephemeris has no opinion about. An agent that sees two vocabularies for one sky
is worse off than one that sees none.

**The case against, recorded because it is real:** it is a convenience, and an
agent could print a command line for a human to run. Against that: emitting
instructions for a human to copy is the thing this direction exists to stop.

## 8. Work log

0. **Spec amendments after Prometheia's review (2026-09-17).** The Prometheia
   maintainers reviewed §3 and Appendices A and C (not B: they are a
   cleanroom) and found eight places where two conforming servers could
   legitimately return different numbers. All are now normative in **§3.5a**:
   rates as derivatives of the answered coordinates with a tolerance and a
   `ratesApprox` flag plus a rates-bound capability; a critical ΔT-table TLV
   (0x8004) because one deltaTSec cannot cover a century-long grid; sidereal
   zodiacs (anchor in TT and mean, which ayanamsa each frame gets, ecliptic
   plane only -- equatorial sidereal is now malformed, the ayanamsa column
   defined); orbit points (which orbit, which plane, corrections as sent,
   mean models named in the source); fixed stars (an accepted name grammar, components
   ambiguous, no deep-sky objects, distance and resolvedNaif); corrections
   honoured as sent for every observer, with per-observer masks advertised
   (A.3 0x0004 gains the observer bitmask); frames defined exactly, with
   J2000 including frame bias and the site on WGS84; privacy covering META
   errText. Also: hypothetical elements are server-defined, and a client
   needing identical numbers everywhere sends kind 4; elements are pure
   two-body with the centre's GM; precession tokens registered (A.20);
   segments note that velocities come from the fit.

0b. **Second Prometheia pass, and performance (2026-09-17).** Their second
   reading found five smaller ambiguities, all now closed: the ΔT table's
   instants are TT and are interpolated at a row's own numeric value with no
   iteration (deterministic on both ends); a sidereal rectangular answer is
   rotated by the same ayanamsa as the spherical one; deep-sky designations
   (`M n`, `NGC n`, `IC n`) are served under kind 2 behind a new `deep sky`
   cap, because Praesepe and the Pleiades are used in astrology and live in the
   star catalogues; orbit points of the Sun and the barycentre are error 2, and
   method 3's mass is pinned (a server using another does not advertise it);
   deflection applies at the barycentre, with a solar-disc guard. They also
   accepted two of ours as improvements on their proposals -- the ratesApprox
   flag over redefining Swiss's speeds, and an ambiguous star being error 6
   rather than the brighter component.
   - New **§3.9 Performance and accuracy**, at the maintainer's instruction
     that both are keys: every approximation is declared on the wire
     (approximated, extrapolated, ratesApprox with its bound, noDistance,
     segment error bounds, σ), f32 is delivery-only and the cache holds f64,
     one REQUEST carries a whole cast, prefetch has its own priority, segments
     answer animation, CANCEL stops wasted computation, and the cache key is
     canonical so one question is computed once.

0c. **Orbit-point corrections, settled with Prometheia (2026-09-17).** The
   agreement in §3.5a above, and the correction of item 2's physics.
   - **The term Swiss carries on a planetary orbit point is ABERRATION, not
     light time.** Prometheia objected to item 2's measurement; re-measured
     rather than defended, and they were right. `SEFLG_NOABERR` alone moves
     Jupiter's ascending node **-20.8370"** (their independent figure
     20.8378"). Item 2 had measured the bits on the lunar *named* bodies,
     where they genuinely move nothing, and carried the sentence to the
     planetary case without re-running it.
   - **Swiss applies zero light time to planetary orbit points.** TRUEPOS and
     NOABERR|NOGDEFL agree to **0.000000"** on Jupiter, Saturn, Mars and
     Pluto, by both methods. Deflection is honoured (0.0002-0.008"),
     aberration is honoured (1-21"), light time is inert. So on kind 1
     planetary this server structurally cannot honour the light-time bit.
   - **The lunar points are the other way round**, settled two independent
     ways. By code: `lunar_osc_elem()` (sweph.c:5692, reached from :976 for
     SE_TRUE_NODE and :1000 for SE_OSCU_APOG) consults exactly one flag,
     SEFLG_TRUEPOS, and `grep -c "swi_aberr_light\|swi_deflect_light"` over
     it returns **0**. By behaviour: SE_MOON itself moves +11.361744" under
     NOABERR, so the flag is live on the lunar path and only the node
     function never asks. Lunar points therefore carry light time, and
     neither aberration nor deflection.
   - **A hypothesis neither side had named** was killed in passing: that
     NOABERR might be dead on the whole lunar path, which would have given
     the same 0.0000" while aberration *was* being applied. The SE_MOON
     measurement above rules it out.
   - **The 19.1" astrometric gap is a spec carve-out, not a bug.** Our
     astrometric lunar nodes equal our apparent ones to the bit; theirs are
     19.105" away; the apparent answers agree to 0.0002". Both servers are
     truthful. §3.5a now says corrections on an orbit point are interoperable
     in full or not at all.
   - **Declined: patching vendored Swiss's `lunar_osc_elem()`** to retard in
     the barycentric frame, which would close the 19.1" at a cost of 0.0002"
     on Astrolog's published apparent nodes. Three reasons, all accepted by
     Prometheia, who withdrew the suggestion: vendored Swiss must reproduce
     Swiss, and the golden gate compares against the fork's own swetest, so
     patching would make that gate measure us against ourselves; "light time
     to a node" is a convention, so adopting their route into a reference
     implementation is a convention change dressed as a bug fix; and it is
     the maintainer's call, not one to take on a peer's suggestion.
   - **A retraction of our own.** We had written that the golden leg could use
     the applied-corrections report to decide which objects are comparable.
     Prometheia showed it cannot: the two lunar answers agree to 0.0002" with
     DIFFERENT corrections applied, and the mean nodes will differ with
     IDENTICAL ones. It explains a difference; it does not predict one, and
     equality of it is neither necessary nor sufficient for two answers to
     agree. A conformance harness MUST NOT gate comparisons on it.
   - **Standing fact for the golden leg:** mean orbit points need a looser
     tolerance than osculating ones (their mean rows differ by 0.006-0.025"
     against 0.0003-0.012" osculating), because mean-element fits differ
     between engines. Unrelated to corrections; the reason belongs written at
     the tolerance.
   - **Instrumentation finding, and it is general:** compare angular
     SEPARATIONS, never ecliptic-longitude differences. A 0.063" "gap" read
     as a real disagreement and was entirely the projection -- the Moon's
     apparent-vs-astrometric displacement is 11.424861", its longitude
     difference 11.361744", and the Moon sits at 5.17 degrees latitude.

     **Corrected 2026-09-18, and the correction matters more than the
     original:** do NOT take that separation as
     `acos(sin b1 sin b2 + cos b1 cos b2 cos(l1-l2))`. The cosine of a
     small angle rounds to 1.0 in f64, so `acos` of it returns EXACTLY
     0 for anything under roughly 0.01" -- which is a false negative
     precisely at the deflection and barycentric-aberration scale, and
     it does not read as a wrong number, it reads as a clean pass. Use
     `atan2(|a x b|, a.b)` on the unit vectors, which stays accurate as
     the angle goes to zero. Raised by the Prometheia side, who put the
     bug into their own `corrapplied.py` and caught it by fault
     injection rather than by trusting the green. The symptom to grep
     for in any existing leg is a column of suspiciously exact zeros.

22. **Phase 6h unblocked: a seam, a recorded corpus, and four defects
   (2026-09-18).** The maintainer's decision -- "they should test it
   against jpl's live service" -- turned 6h from blocked into work. What
   it actually took was making the code testable WITHOUT the service, and
   that paid for itself four times before a single fixture existed.

   `GetJPLHorizons()` was welded to `ciCore`, a fixed temp file and
   `GetURL()`, so nothing about it could be exercised offline. It is now
   `SzUrlJPLHorizons()` (the question, taking its instant and site as
   arguments), `FParseJPLHorizons()` (the reply, taking an open stream)
   and the same wrapper as before. **The fetcher composes no URLs of its
   own** -- it asks the client binary what it would send -- because a
   fixture that answers a question the client never asks proves nothing,
   and a second implementation in Python would drift from the first the
   day anyone touched either.

   **What the visible URL showed immediately:**

   - **Every Horizons position was computed one minute early.** The
     minute came from `RFract(tim)*60` TRUNCATED while the second came
     from `RFract(tim)*3600 + rSmall` -- two different roundings of one
     quantity. 12:00 minus five minutes is 11.916666666666666, whose
     `RFract()*60` is 54.99999999999997, so the query went out as
     11:54:00 meaning 11:55:00. Both ends shifted together, so the window
     kept its 11-minute span and its three rows -- and the MIDDLE row,
     which is the reported position, landed a minute before the chart's
     own instant. About 33 arcseconds for the Moon. The rates were
     unharmed, because they difference the outer two rows and that span
     is right either way, **which is exactly why nothing downstream ever
     looked wrong.**

   - **A raw space went out in the query string**, from
     `"COORD_TYPE= 'GEODETIC'&"`. The hand encoder below it expands only
     `'` and `;`, so the space was never encoded and the value Horizons
     read began with one. Topocentric queries only.

   **And three things JPL itself contradicted**, which is the whole
   argument for recording real answers instead of writing plausible ones:

   - **Earth asked for from Earth's geocentre is refused as degenerate.**
     It was in the corpus expecting a position, to cross-check the
     re-centring. It is now an error fixture -- and the evidence for why
     `ComputeEphem()` synthesises Earth from the Sun rather than fetching
     it, which the code did without ever saying so.
   - **"No ephemeris for target Mars prior to A.D. 1600-JAN-01."** The
     observer tables do not reach 1500.
   - **Pluto's boundary is 1800, not Mars's 1600.** The limits are **per
     body**, so a plugin cannot carry one coverage range for the source;
     it must answer `ephErrOutsideCover` per object.

   20 requests total, sequential, 5s apart, identifying User-Agent, no
   retry needed. The pacing policy is enforced in
   `tools/horizons-fetch.py` rather than remembered, and follows what the
   Prometheia project measured fetching its own corpus on 2026-09-17.
   **Nothing in any gate reaches the network.**

   ### DEFERRED BY THE MAINTAINER, 2026-09-19

   **Not declined like 6f and 6g, and not started: deferred, to be picked
   up on a fresh session.** The reasoning to carry forward is that the
   user-visible feature already works -- `GetJPLHorizons()` casts charts
   today, with the one-minute error fixed and a recorded corpus behind it
   -- so 6h buys **architectural consistency** (the chain, the fallback,
   per-object provenance) rather than function. Against that, its real
   content is a refactor of `ComputeEphem()`'s core, which every chart
   goes through.

   **What a fresh session needs, so it does not have to re-derive this.**

   - The three steps below are still the specification, and the
     `calc.cpp` line references in them were **re-verified on 2026-09-19**
     and are accurate.
   - Step 2 is the whole of the risk. Steps 1 and 3 are additive; step 2
     moves cross-object arithmetic out of a per-object loop, and that
     arithmetic works today only because `i` ascends with
     `oEar < oSun`. That ordering dependence is undocumented in the code
     and is the thing most likely to be broken silently.
   - **Take a baseline binary first.** `git worktree add` at the commit
     being changed, build `./astrolog-base`, and require
     `tools/chart-matrix.sh`, `tools/switch-matrix.sh`,
     `tools/influence-matrix.sh` and `tools/graphics-matrix.sh` to be
     byte-identical against it. A re-centring change that is correct for
     the geocentric case and wrong for a heliocentric or planet-centred
     one will show in the chart matrix and in nothing else.
   - **The offline check that made this verifiable** is in step 2's own
     note: cast the same chart through Swiss heliocentrically, and
     through a recorded geocentric reply plus the emulation, and require
     agreement. The corpus and `horizons_audit` are committed and in
     `make check`; nothing reaches the network.
   - Do not start it at the end of a long session. It is the one piece
     left in this plan whose blast radius is every chart the program
     draws.

   ### What the rewrite still needs, specified

   The remaining blocker is **semantic, not plumbing**. Horizons returns a
   geocentric, light-time-uncorrected row, and **five** sites in
   `ComputeEphem()` are written in the negative to accommodate that --
   "don't shift, Horizons already did": `calc.cpp:1019-1021` (Earth
   skipped), `:1052` (selection), `:1142-1181` (the re-centring),
   `:1195-1196` (the Sun zeroed), `:1221-1226` and `:1247-1251` (the
   skip-the-shift and skip-the-relocate rules). The re-centring itself
   reads `space[oEar]`, `space[oSun]` and Earth's finished rates while
   computing object `i`, so it is cross-object arithmetic inside a
   per-object step, and it works only because `i` ascends with
   `oEar < oSun`. `FRead()` hands back one row with no access to siblings.

   So the rewrite is **not** "move `GetJPLHorizons()` behind `FSubmit`".
   It is:

   1. Give `EPHCAPS` a bit saying **"my rows are geocentric and
      light-time-uncorrected"**. It has no way to say that today, which is
      why the convention had to live in the caller.
   2. Move the re-centring out of `ComputeEphem()`'s object loop into a
      **host-owned emulation step that runs after the chain walk**, over
      the whole row set at once, for any source declaring that bit. The
      host already emulates missing capabilities (Part B); this is one
      more, and it is the one that deletes all five negative rules.
   3. Then `ephhorizons.cpp` is an ordinary plugin: one request per body,
      three instants each, `ephErrOutsideCover` per object from the
      recorded boundaries, Earth never asked for, and `NLookup` from
      today's `SzObjSelName()`.

   **Step 2 is verifiable offline with what is now committed**: cast the
   same chart through Swiss heliocentrically, and through a recorded
   geocentric reply plus the emulation, and require agreement. That check
   did not exist before this corpus and is the reason 6h was called
   unverifiable.

25. **The cross-test with Ephemeris Prometheia (2026-09-18).** Two
   independent engines, two independent servers, one protocol, asked the
   same questions and compared. It found **five defects in
   `astrolog-ephd` that nothing in this tree could have found**, because
   everything we own compares Swiss against Swiss.

   | finding | what | outcome |
   |---|---|---|
   | error code | A.17 4 where 3 was meant at the first instant of the .se1 span | fixed, plus a second instance they had not hit |
   | delta-T | the request's value never reached Swiss's own UT<->TT, so a topocentric observer sat where Swiss's model put them | fixed, 0.000 -> 16.72 arcsec |
   | corrections | WELCOME advertised masks at the Sun's centre and the barycentre whose effect a body never sees | fixed, and broader than reported |
   | helio light time | ~1% against Horizons | **upstream Swiss**, see below |
   | Beta Sco | our plugin re-derived a decision their library already makes | fixed (item 23) |

   **The three lessons worth keeping:**

   - **A report can be right about the symptom and wrong about the
     cause, and still be worth everything.** Their delta-T finding said
     "the request's delta T is ignored". It is not: a UT1 request moves
     the Moon 75.46 arcsec between deltaTSec 0 and 100. Their probe sent
     TT, where the conversion has nothing to do -- and that is exactly
     the configuration where the OTHER half of 3.5's promise, Earth
     rotation, is the only half there is. They found a real defect by a
     route that made it look like a different one.

   - **"Advertised is promised" has a second edge.** Narrowing WELCOME to
     the truth broke the server on the next gate run, because acceptance
     was validated against the advertisement -- so an honest WELCOME
     began refusing heliocentric mask 7, which is what Astrolog's own
     casts send. The two are now deliberately different questions:
     WELCOME says what the server will DO, acceptance declines only what
     it cannot parse.

   - **What the gates structurally cannot see.** `ephsrv-golden` compares
     this server against the fork's Swiss with hand-written flags and
     passes 149 comparisons bit-exact, including `obs=helio,corr=1`. So
     the heliocentric light time IS Swiss's, exactly, and the 1% against
     Horizons lives in Swiss's model rather than our wiring of it. **A
     differential against yourself cannot find a shared premise**, which
     is the whole argument for this exercise and for the Horizons corpus
     in item 22.

   `tools/crosstest-prom.sh` is our half of leg 2, run by hand like every
   eph gate; nothing in `make check` starts a foreign daemon. **Its first
   run reported five FALSE mismatches**, including the one case the two
   projects had just settled, because it began probing when the daemon
   logged "listening" rather than when it could answer. A readiness check
   that is not the thing being waited for is how a harness invents
   findings, and a table of error codes is the worst place for it: a
   wrong answer there is indistinguishable from a real disagreement.

26. **Three upstream Swiss findings the cross-test surfaced, and one
   harness lesson (2026-09-18).** Legs 2, 3, 4, 5 and 10 are complete from
   this side (`tools/crosstest-prom.sh`). Leg 10 needed a SEGDATA decoder
   in `eph_wsclient` -- the codec had been in `ephproto.h` since phase 2
   and only the consuming half was missing -- and it now evaluates the fit
   through `Segment::Eval` and compares it with sampled rows: **0.000636
   arcsec against the 0.001 asked for.**

   **THREE FINDINGS ARE UPSTREAM SWISS, NOT THIS SERVER'S WIRING.**
   `ephsrv-golden` compares astrolog-ephd against the fork with
   hand-written flags and is bit-exact over 149 comparisons, so where the
   server and Swiss agree and both differ from JPL, the difference is
   Swiss's. Recorded here because the fork is a sibling repository this
   project does not patch, and because **all three reach the DESKTOP**:
   Astrolog casts from the same library.

   | what | size | where |
   |---|---|---|
   | heliocentric light time | 0.38" Mercury, 0.28" Venus, 0.11" Mars vs Horizons | Swiss's retardation model |
   | topocentric observer built about the MEAN pole | 0.165" on the Moon | `swi_get_observer` is able to nutate, but every call site passes `SEFLG_NONUT` (sweph.c:3195, 3367, 4108) |
   | deflection at a planet-centred observer | 7 mas | `swi_deflect_light` builds its geometry from `pldat[SEI_EARTH]`, so Swiss deflects as seen from EARTH and then re-centres |

   The nutation one is the most worth a decision: it is not a modelling
   choice between two defensible conventions, and **every topocentric
   chart the application draws carries it.** The Prometheia project pinned
   it by recovering the observer offset from two topocentric vectors --
   100-290 m, horizontal, turning epoch to epoch -- and reproducing it to
   1-3 m at all fifteen rows with the nutation pole offset, the opposite
   sign missing by 2x.

   **And the harness lesson, which is the one to keep.**
   `tools/crosstest-prom.sh` reported **eleven confident mismatches across
   two runs**, including cases the two projects had already settled. The
   diagnosis was a startup race, and that was fixed -- wrongly. The cause
   was that the script hardcoded a port, and the OTHER project's rerun had
   started **our** `astrolog-ephd` on it. Our daemon could not bind, the
   readiness probe was answered by that other server, and every probe
   compared astrolog-ephd's answers against Prometheia's contract. The
   readiness check passed because the port was genuinely serving -- the
   wrong thing, correctly.

   It now takes port 0 and reads back what it got, and then **asks who
   answered**: WELCOME names the engine, and a run against anything else
   aborts. A free port makes the collision unlikely; the identity check
   makes it impossible to go unnoticed. **Two harnesses on one machine is
   a failure mode neither project designed for**, and "is this the server
   I think it is" turns out to be the first question a cross-project gate
   should ask.

24. **P1's missing net, closed (2026-09-18).** The one thing the three
   reviews left open, and the reason it stayed open for a day: a fixed
   star through the Ephemeris Server could not be tested because the
   suite's own loopback daemon could not serve one.

   **Why.** `sefstars.txt` is in the TREE ROOT, not in the bundled
   `ephem/`. The live group hands the daemon an explicit `--ephe` built
   from every `-Yi` directory, and an explicit `--ephe` is the server's
   whole search path -- so under `-Yi1 ephem` the daemon had no star
   catalogue and answered every star `ephErrDataUnavailable`. The local
   cast finds the file regardless, because Swiss falls back to the
   working directory. That asymmetry is the whole of it, and it is worth
   remembering generally: **a server told where to look is stricter than
   a library allowed to guess.**

   The leg now appends the tree root, asks the chain for one named star
   directly rather than through `SwissComputeStars()` (whose query is
   empty unless the stars are unrestricted -- the trap that made the
   first attempt vacuous), and asserts the row's provenance is `server`
   so the swiss fallback behind it cannot answer for it. Measured
   agreement: **0.0000 arcsec tropical, 0.0031 sidereal.** Sabotaged by
   putting back the body convention for stars, the sidereal star moves
   **68036 arcsec** -- a 24.7 degree longitude shift seen at Sirius's
   -39.6 degree latitude -- and the tropical one does not move at all,
   which is exactly the signature P1 was reported with.

23. **The Prometheia project's review of `ephprom.cpp` (2026-09-18).**
   A review from the OTHER side of the plugin boundary, run under the
   cleanroom rule -- their agent read this plugin and their own headers,
   and nothing Swiss. Eight findings plus a correction to advice they had
   given earlier. All are resolved; one was refuted.

   **The headline finding was wrong, and proving that was worth more than
   fixing it would have been.** They reported that sidereal stars come
   back about 24.7 degrees out, because star profiles are given the
   `fagan-bradley` zodiac while the read deliberately does NOT subtract
   `is.rSid` -- justified in a comment claiming the library's zodiac
   option is "a label, not a transform". They had measured on their own
   engine that it IS a transform. That is the exact shape of the server
   source's P1, so it was settled by measurement rather than by argument:
   built against the real library, Sirius at 1990-06-15 reads
   **104.449916 tropical and 79.334059 under Fagan/Bradley from BOTH this
   source and the swiss source, identical to 0.0000 arcsec.** The chart
   is right.

   **But the comment's REASON was wrong**, and a wrong reason sitting over
   working code is worse than none: it invited exactly the "fix" that
   breaks it. The real reason is the CONSUMER -- `ComputeEphem()` re-adds
   `is.rSid` to a body row and the star callers do not, so each row is
   pre-adjusted for whoever reads it. Corrected, with the measurement in
   the comment.

   **The seven real findings, all fixed:**

   | # | what | reach |
   |---|---|---|
   | 1 | `FillRow()` wrote row 0 for EVERY row; the failure path already had the offset | latent -- casts ask one row |
   | 3 | orbit points always took the TT entry point, so a UT1 cast computed nodes and apsides delta-T late -- **5.2278 arcsec** on the osculating lunar apogee | live |
   | 4 | a row failing AFTER a row succeeded was not recorded at all; `iRowFailed` was never assigned; where several failed first, the LAST won | latent |
   | 5 | an unrecognised `ARGUMENT` refusal DEFAULTED to "undefined point", classified from their message prose | live |
   | 6 | every `star_find` refusal was "unknown body", never ambiguity | live |
   | 7 | the delta-T COLUMN dropped the user's `-Yz0` override that the hook applies | live |
   | 0 | **no ABI check at all**, and their own correction: it must be `==`, not `>=`, because 0.x APPENDS to `prometheia_options` and a newer library reads past the end of the older struct | latent under static linking |

   **Five nets, each proven by sabotage.** Two are deliberately not netted
   and say so: the ABI check needs a mismatched library, and finding 6's
   branch is reached only when `star_lookup` finds nothing, which no name
   available here does.

   **Two of the nets were wrong before they were right, both silently** --
   the same lesson as P1's withdrawn test, twice more. The straddling-rows
   leg asked for a 400000-day step; `stepNs` is an int64 of NANOSECONDS,
   it overflowed, and the leg "passed" having computed two rows well
   inside coverage. Even at 100000 days the i64 product of row and step
   overflows past about 292 years, and this ephemeris ends 660 years out,
   so it uses the LIST form. And the delta-T leg first read a column the
   profile had never requested, comparing 0.0 with 0.0. **A net that
   cannot fail is the default outcome, not the unlucky one.**

   And the star-parity net itself took two tries for the same reason: it
   first ran inside the scope of an earlier leg that deliberately points
   the ephemeris at a missing file, then passed with the bad subtraction
   reinstated because it never re-cast inside the sidereal borrow, so
   `is.rSid` was zero and it was blind to the one subtraction it exists
   to watch.

   **Their Q2 and Q3 are now answered by the compiler.** Three mappings
   are straight copies of a protocol enumeration into a Prometheia one,
   justified by a comment saying the lists agree. They do -- and three
   `static_assert`s now say so, because a comment cannot notice a
   reordering.

   **One thing this turned up that is not theirs:** `FEphSubmitSide()`
   (`ephem.cpp:650`) walks the user's chain and only APPENDS swiss, so
   side calls -- the fixed stars among them -- are not the "unconditional
   Swiss calls" the comment above it still claims. The plugin star paths
   are live code.

21. **Phase 8's third review: the fixes' own damage (2026-09-18).** A
   pass over phase 6's transport AS IT NOW STANDS, briefed to hunt what
   the earlier fixes broke rather than to re-find what they fixed. It
   paid, and the first finding is the argument for the whole exercise.

   - **P1: D2's fix broke fixed stars in sidereal charts.** D2 made the
     server compute a star with the chart's real settings; the CONSUMER
     half still applied `FSwissPlanet()`'s body convention to the answer,
     and a star row carries no `is.rSid` because the star callers apply
     the zodiac themselves. Every fixed star landed about 24.7 degrees
     out, `nErr` clear, so the chain claimed the row. Tropical charts
     cancel it exactly, which is why nothing saw it. Both other sources
     already kept the contract, `ephprom.cpp` with a comment naming this
     exact failure.

     **It has a net since 2026-09-18** (work-log item 24). One was
     written and withdrawn first: its version set `us.fStar` alone and
     PASSED with the fix sabotaged, because `SwissComputeStars()` only
     asks for stars whose `ignore[]` is clear -- an empty query,
     exercising nothing. Unrestricting them then showed the suite's
     loopback daemon has no star catalogue on its own path, and that is
     what took the rest of the session to close: **`sefstars.txt` lives
     in the TREE ROOT, not in the bundled `ephem/`**, and an explicit
     `--ephe` is the server's whole search path. The local cast finds the
     file anyway, because Swiss falls back to the working directory; the
     server, with its path stated, does not. The live group now appends
     the tree root, asks the chain for one named star directly rather
     than going through `SwissComputeStars()`, and checks provenance so
     the swiss fallback cannot answer for the server. Sabotaged, the
     sidereal star moves 68036 arcsec and tropical does not move at all.

   - **P2: a lossy round trip made a far-dated cast pay for an answer and
     then reject it.** The instant went out as centuries and came back,
     and the plan's instant is compared exactly. Measured over the
     expression `RProgArc()` builds: 0.00% at 1990 and 1500, 4.93% at
     year 0, 52.94% at year -3000. When it missed, the request was built,
     sent, waited for and cached, then every object was refused with a
     modal saying no request had been made.

   - **M2: the transport ignored `rgisrc`**, contrary to ephem.h's stated
     contract, so a `swiss,server` chain re-sent objects swiss had
     already answered and one already-answered out-of-range object could
     refuse the whole query.

   - **M1: a per-object failure message named the wrong body.** It used
     `szObjName[obj]` for a star whose object slot holds its CATALOGUE
     NUMBER, so `-XU` reported "could not compute Moon" for star #1.
     Positions were never affected -- the same index writes and reads --
     but a user hunting a star that would not draw was sent to a planet.
     `SzSrvObjNameQt()` reads the name off the request's own object,
     which is right for either kind, and the net asserts both halves:
     the star names the star, an ordinary body still names szObjName[].

   - **And the sanitizer found one more, in a NET.** AddressSanitizer
     over the changed groups caught a stack overflow in the E1 test
     itself: it borrowed a five-entry argv array for a sequence needing
     six. A regression test can be the regression. UBSan over the same
     ground is clean.

20. **Phase 8's second review, and the gate that could not see it
   (2026-09-18).** A second independent pass over phases 2-5 -- ground
   the first review was told to skip -- found four more defects. With the
   six from the first, ten in all, and the branch had been green through
   every gate the whole time.

   - **E1: a Matrix selection became "none" and drew 0Ari00'00" for every
     body**, with no warning. The legacy spellings keep a
     {files, nSwiss, matrix} shadow; `EphSourceSetShadow()` bumps the
     selection's generation and the parser never claimed it, so the next
     `-b` spelling rebuilt the shadow from the chain TEXT -- and that
     round trip cannot carry the Matrix bit. `_bs =bm _bU _b` is the
     order the PRE-BRANCH writer emitted for a Matrix selection, so it is
     what an old settings file replays. The branch's claim that any old
     file order loads as before did not hold for that one selection.

   - **E2: the Prometheia source computed every object under object 0's
     profile.** `rgobj[i].profile` was never assigned and defaults to 0,
     and the bounds check passes because 0 is always in range -- so a
     heliocentric chart computed the lunar node heliocentrically. Its
     second loop also overwrote refusals the first had set, so a refused
     object could return marked answered and the walk would claim it; the
     sharpest case is naif 0, the solar-system barycentre, which the
     engine serves.

   - **E3: picking a source in Ephemeris Settings dropped the whole
     fallback tail.** The composer stood ON the separator, so its token
     scan could not advance. Both builds.

   - **E4: the chain was validated in full and stored truncated to 254**,
     producing a settings file that would not load -- and a refused line
     aborts the load, so everything after it was dropped.

   - **THE GATE COULD NOT SEE ANY OF IT, AND THAT IS THE FINDING.**
     `make check` and the release workflow run the suite with
     `-Yi1 ephem`. `./run-qt-tests.sh` -- the command CLAUDE.md
     documents -- defaults to `-i nrvate.as`, which is what the hard rule
     "always test with -i nrvate.as" names, and which resolves far more
     bodies from `/swe`. **That configuration was failing 20 assertions
     for most of this branch's life while `make check` reported green.**
     The cause was the server source warning about objects it had never
     been asked for; the deeper point is that the two commands run
     different configurations and only one of them is gated.

     It is also the configuration that reaches D1's out-of-bounds write:
     with the refusal removed, `-i nrvate.as` CRASHES the suite outright
     while `-Yi1 ephem` passes.

   - **Why E1 in particular survived every gate, which is structural.**
     The switch matrix is the check that would be expected to catch a
     selection bug, and it cannot catch this one: **every one of its 543
     runs invokes a SINGLE switch.** The `-b` family's runs are `=b`,
     `_b`, `=bE swiss`, `-bE bogus` and so on, one per invocation. E1
     needs TWO spellings in sequence -- `=bm` then `_b` -- because the
     defect is in what the first leaves behind for the second. A harness
     built one-switch-per-run is blind to the whole class, and after
     E1 was fixed the matrix was byte-identical, which is the proof: it
     never exercised the path.

     The suite carries the sequence net now. The matrix does not, and
     adding a handful of two-switch runs to it would close the class
     rather than the instance. Same shape as `-XE 1 20` rendering
     identically to no `-XE` at all: an entry that exercises nothing
     still diffs to zero, and reads exactly like coverage.

   - **What to take from it.** Ten defects, one crashing, several silent
     wrong answers, all in code that every gate was green over. Both
     reviews were given a brief naming the CLASSES to hunt rather than
     asked to look for problems, and both paid. If there is budget for
     one more thing before the squash, it is a third pass over phase 6's
     own transport code, which only the first review saw and which has
     changed a great deal since.

19. **Phase 8, the branch review (2026-09-18).** Four independent
   passes, because they see different things.

   - **The lock condition holds.** `ephsrv/ephproto.h`,
     `registries.json` and `ephsrv/conformance/` are byte-identical to
     `cf83dc9` across every commit on this branch. That is the condition
     the maintainer's phase 3-7 approval rides on.

   - **The differential is clean.** Chart, influence and graphics
     matrices byte-identical from the phase 3 baseline to the phase 6
     head: phases 4, 5 and 6 moved no local cast output at all. The
     switch matrix differs, and only in the two places it should -- the
     writer's `-b` family, changed by design in phase 4, and four
     deliberate negative tests of `-bE`/`-bP` that abort as any bad
     switch does.

     **Worth keeping: the raw diff of that matrix LIES.** It showed whole
     `-YJ`, `-Y7C` and `-M0` sections apparently vanishing. They had not.
     On a concatenated multi-run file a two-line change near one run's
     `-b` section makes diff re-attribute neighbouring identical lines
     across run boundaries. Comparing run-by-run, aligned on the `== `
     markers, gives 543 runs both sides and four differing. Any future
     reading of that file must align first.

   - **An external check of the server.** Prometheia's `corrapplied.py`
     run against `astrolog-ephd`: 27 of 29 cases, all three sub-checks on
     all 27, green. The first verification of our `corrApplied` by an
     implementation that is not ours. Its first run printed OK having
     checked ZERO cases -- reported to them and fixed -- which is why the
     coverage line matters more than the verdict.

   - **The server's own gates, re-run.** `make check` does not run them,
     and this branch changed `eph_srv.cpp` (the no-ephemeris startup line
     went from warn to error, because `--log-level error` filtered out
     the one message explaining why a running server answers nothing).
     golden 149 comparisons bit-exact against the fork, robust, limits
     and ops all pass. **ops is the one that mattered here**, since it
     reads the daemon's log.

   - **A delegated code review found six defects, all now fixed**, and
     the first of them is the reason phase 8 exists rather than being a
     formality:

     **D1 was a crashing out-of-bounds write, introduced in 6c, that
     every gate was green over.** The adapter's plan is addressed by
     Astrolog object index and is `objMax` long; taking the host's QUERY
     as the object list let side-call indexes reach it -- an asteroid is
     `SE_AST_OFFSET + n`, over 10000. AddressSanitizer: SEGV at the
     write, some 160 KB past the array, reachable from any chart drawing
     stars or asteroids with the server selected. `FEphQueryAdd()`
     validates the COUNT against `objMax` and never the INDEX, and the
     READ side had been range-checked all along while the write had not
     -- it had not needed to be, while the only producer was a bounded
     cast loop.

     D2: a star was asked of the server with a DEFAULT profile, so a
     sidereal or heliocentric chart got tropical geocentric star
     positions, marked `ephErrNone` so the chain claimed them and the
     fallback never ran. D4: the bounded wait never covered star objects,
     so every star request was abandoned and reported as a server
     timeout. D5: the dialog's parameter rows never recomputed, so with
     the shipped default there was no way to enter a server address at
     all, and opening on `jpl` wrote a typed URL into `epJplFile`. D6: a
     truncated path comparison reopened the Prometheia engine on every
     call. D3 needed no fix of its own -- D1's refusal already turns away
     the catalogue-numbered star side call -- **verified rather than
     assumed**.

   - **The lesson worth carrying past this branch:** every one of D1-D6
     was in code the suite was green over, and D1 was a crash. A suite
     that grew to 5989 assertions alongside the work it checks tends to
     assert what the author was already thinking about. The review that
     found these was given a brief naming the CLASSES to hunt --
     lifetimes, bounds, uninitialised reads, error paths -- rather than
     asked to look for problems.

17. **Phase 6b-6d, the server through the chain (2026-09-18).**
   `ephreq.h` is the one EPHQUERY-to-REQUEST translation every transport
   shares; the Qt build binds a transport (`c1c221b`); and
   `ComputeEphem()` lost both `#ifdef QT` server branches (`6ed60d5`), so
   a remote cast and a local one no longer take different code to the
   same question. The live parity group casts through the chain
   bit-identically to the local path.

   - **A remote source is the first one for which "FSubmit filled the
     rows" is not automatic.** Removing the legacy branch turned the
     parity group red at once, every object showing the Earth's
     180-degree placeholder: the cast reached the server and came back
     empty. `FEphSubmitChain()` states the contract -- returning true
     means a row is filled for every open object -- and a local source
     satisfies it by computing inside FSubmit, while a remote one's
     answer arrives later. The source now submits the whole query and
     reads each object back into its row.

   - **An unanswered object must carry an ERROR, not a zero row.** The
     walk claims any row whose `nErr` is `ephErrNone`, so a silent zero
     is claimed as an ANSWER and the fallback is never reached -- a chart
     of zeros from a source that failed, with no notice. Written into the
     code rather than left to be discovered.

   - **The parameter notification of 16 was wrong and is gone.** Binding
     a transport exposed it: `Stop()` on a remote source tore down a live
     connection and its in-flight request because the user edited the
     address, discarding work the adapter carries across a reconnect.
     Nine assertions failed and were right to. **A notification is also
     missed by any path that writes the value another way.** So the rule
     is inverted: a source compares what it OPENED with what the
     parameter SAYS, at the point it uses it. Cannot be missed, cannot
     fire too hard.

   - **`SzSet()` is not a truth test.** It hands back `""` for a null,
     and `""` is a true pointer -- so `if (SzSet(x))` is always taken.
     The status line's localhost default was unreachable and the raw null
     went to `"%s"`: every user who had not set a server address was
     shown "Ephemeris Server (null)". `FSzSet()` tests; `SzSet()`
     substitutes.

18. **Phase 6e, the required-server nag deleted (2026-09-18).** The modal
   startup dialog, its hour-long ladder and `EXIT_NO_EPHEMERIS` existed
   for a world where a selection was ONE backend. The chain ended it: a
   source that cannot answer is offered past, and the failure is reported
   where it happens. **The reporting that replaces it was verified before
   the deletion, not assumed** -- a body no source could compute already
   warns once per cast, and a run with no ephemeris at all is documented
   as supported and quiet because Moshier covers the planets. The suite
   pins the promise (startup returns at once, is not welcomed, still has
   a state to show, leaves a local source casting) rather than the
   behaviour that went.

16. **Phase 6a, and three things it found (2026-09-18).** `ephserver.cpp`
   carries an ordinary `EPHSRCDEF` for the `server` key, over a TRANSPORT
   a backend registers (`EPHTRANS`) rather than an `#ifdef` in the
   plugin -- so the file compiles identically everywhere and a build with
   no transport has the source listed and unavailable, which is the shape
   an uncompiled plugin already has. No transport is bound yet; 6b-6c.

   - **The Prometheia parameters were unreachable.** `-bP`, the settings
     file and phase 5's dialog all wrote `us.rgszEphParam[]`; the plugin
     read a private array nothing outside itself ever wrote. Every
     configured path was dropped and the engine always opened on its
     default search, with no catalog and no perturbers. Live since phase
     5 shipped a dialog control that did nothing.

   - **`EPHSRCDEF.rgParam`/`cParam` were read by nothing**, which is why
     the copy the Prometheia source handed them had drifted in both label
     and kind -- and the suite pinned the drifted copy, so a test held the
     wrong value in place. **A field nothing reads cannot be kept
     honest.** Deleted rather than audited; a source's parameters are
     asked for by key now.

   - **F11 was load-bearing after all.** Both builds named four parameter
     indexes by hand beside a generated count of six, two from one source
     and two from another, so the dialog showed a mixture no selection
     could use and two parameters had no way in from any dialog. The rows
     are the chain head's own now. Four rows were never the bug.

   - Found on the way: the PROMETHEIA build had not compiled since the
     phase 4 merge, invisible because `pkg-config` does not resolve the
     library here so every run skips that group; and
     `EphSrvFinalizeQt()` reset everything except the missed-cast flag,
     so a cast that could not reach one server was replayed at the next
     one to say WELCOME. The second was found by instrumenting a byte
     comparison after three wrong guesses -- the measurement named it in
     one run.

13. **Phase 4c and the merge: the chain IS the selection (2026-09-18).**
   `us.szEphemSource` and `us.rgszEphParam[]` are the only representation
   now. `fEphemFiles`, `nSwissEph`, `fMatrixPla`, `szEphSrv`,
   `szEphSrvToken`, `fNoOldCalc` and `fNoNetwork` are deleted from `US`
   and from `data.cpp`'s positional initializer; `settingsfields.h`
   regenerated. Legacy spellings load by toggling a parse-time shadow and
   re-deriving the chain, so any old file order still means what it
   meant. `-0b`/`-0n` are genuinely inert. `FCm*` is replaced by
   `FEphSpeeds()` and `FEphLegacyCast()`. Committed `2fe2719`, merged
   `b296210`.

   - **The 14 red assertions were NOT a Swiss-fork bug**, and the fork
     was never touched. The 00:20 handoff said they were and said the fix
     belonged in `/shares/swisseph`, gated by that fork's golden suite.
     **The fork is not linked into any Astrolog binary** -- Qt, console
     and Windows all compile the VENDORED upstream Swiss 2.10.03, and
     `/shares/swisseph` is built only into `astrolog-ephd`. An hour went
     into a fork that could not have been the cause. The lesson is not
     about Delta-T: **check what is actually linked before diagnosing a
     library.**

   - **What it actually was.** The library reads its tidal acceleration
     from the moon file's DE number; with no file open `swi_get_tid_acc()`
     falls through to `SE_TIDAL_DEFAULT` (DE431, -25.80) instead of the
     bundled `ephem/`'s own (DE441, -25.936). Those are tidal
     accelerations in arcsec/cy^2, not seconds; they differ by 0.136,
     which is 0.037 s of Delta-T at 1900. Astrolog caches Delta-T in
     `is.rDeltaT` **keyed on the date alone**, so a value computed before
     the ephemeris path was set outlived the path change and the bodies
     reused it while `swe_calc_ut()` recomputed on DE441. Every body came
     out by its own motion over 0.037 s: the Moon 0.018", the Sun
     0.0015", Mercury 0.00003". That ratio is what identified it -- the
     errors scaled with apparent motion, which is a TIME error, not an
     ephemeris error.

     Two fixes, each independently correct: `SwissEnsurePath()` drops the
     cached offset when it sets the path, and `SwissHouse()` ensures the
     path before asking for Delta-T, being the one site of four that did
     not. **The general rule: a value derived from the ephemeris must not
     outlive the ephemeris it was derived from.**

   - **The first net was worthless and was thrown away.** It set the path
     flag false and called `SwissEnsurePath()`, which is exactly what the
     product does, so it passed with the fix removed. What it lacked was
     `swe_close()` -- without it the moon file is still open from the
     previous leg and the DE number is right for the wrong reason -- and
     a cache reset, without which the previous leg's correct value is
     reused. Each fix is now pinned by its OWN assertion, because a net
     covering both passes with either one present, and each was proven by
     removing it and watching only its own assertion fail.

14. **The merge, and two regressions it exposed (2026-09-18).** eph4sel
   branched before phase 5, and neither branch compiled for Windows on
   its own: 4c deletes the five legacy selection fields, phase 5 deletes
   the `wdialog.cpp` code that assigned them. `backend_parity_audit` and
   the warning audit's Windows leg were red on eph4sel for that one
   structural reason and are green on the merge. **`make check` does not
   build Windows**, so only the full warning audit could see it -- worth
   remembering before trusting a green `make check` on a branch that
   touches `US`.

   - **A pin set before the thing that invalidates it is not a pin.** The
     required-server test set `is.fSwissPathSet` and then selected the
     source, and 4c makes changing the source invalidate that latch by
     design. The pin was cleared by the very call it was meant to
     survive. Product right, test wrong.

   - **A silently disabled test reads exactly like a passing one.** 4c's
     mechanical rewrite of the selection calls dropped
     `EphSrvStartupQt()` from the TLS leg and replaced `us.nSwissEph = 5`
     with `EphSourceSet("swiss")` -- selecting the LOCAL source in the
     leg whose purpose is to watch the server refuse an untrusted
     certificate -- and pasted a duplicate of the local baseline cast
     where the startup had been. The leg ran, and could never observe a
     refusal. It was findable only because the pre-merge run was green
     (85/0) and the post-merge run was not; without that baseline it
     would have been written off as a flaky live group. Every
     `EphSrvStartupQt()` call site was then audited for the same
     signature, and the harness call counts compared against the
     pre-merge file.

15. **Review findings closed with phase 4 (2026-09-18).** F3: `-0b`/`-0n`
   are inert, which is what `tools/inert_option_audit.py` had been
   claiming since 4b. F2: closed, and the finding was worth less than it
   looked -- the key list reads `rgephsrc[]` directly now, so
   registry/key drift is impossible by construction, and only the three
   §4.2 keys whose plugins are later phases' are hand-written. Those are
   pinned both ways, so leaving one in `rgszEphSrcFuture[]` after its
   plugin lands fails rather than being remembered. **F11 remains open**:
   both builds' Ephemeris Settings dialog declares `rgiepEphemParam[4]`
   beside a generated `cEphParam` of 6.

10. **Phase 7, the Prometheia plugin, first increment (2026-09-17).**
   `ephprom.cpp` and `ephprom.h` in the CORE group of `Makefile.srcs`,
   entirely inside `#ifdef PROMETHEIA`; the makefiles detect the
   dependency with `pkg-config prometheia` and build without it silently
   -- the detection lives in `Makefile.srcs` once, each Linux makefile
   appends two lines after its own flags, and the Windows toolchains get
   no detection at all because the package is today a Linux static
   library. Phase 3's `EPHSRCDEF` table is still being built in parallel
   on branch `eph3` (nothing landed when this was written), so the
   module carries the parts of the 4.1 model it can own alone and the
   shapes the sketch names: the three `EPHPARAM` rows with the
   `prometheia.*` keys, `FAvailable`/`Start`/`Stop`/`State`, the
   question block of 3.4 as one `FEphPromCompute` over the locked
   header's own `eph::Profile` and `eph::Object` (so the plugin and the
   wire cannot drift), LOOKUP under 3.5a's star grammar, and registration
   into the shared table left as glue. What the binding pins:
   - **The frame lists run in opposite orders.** The protocol's frames
     are 0 true of date, 1 mean of date, 2 J2000, 3 ICRF; Prometheia's
     are 0 ICRF, 1 J2000, 2 mean of date, 3 true of date. A straight
     copy of the field binds the wrong frame four ways; the suite pins
     all four mappings in a check that needs no engine and no data file.
   - **The corrections reach the orbit points.** Prometheia honours the
     three bits as sent on kind 1 (measured: at the check's instant
     the light-time bit alone moves the Moon's ascending node -18.4030",
     the aberration bit alone +18.4053", deflection alone 0.0000", and
     all three together +0.0015" -- the two large terms nearly cancel), so
     `corrApplied` is 7 there, and the group asserts each bit passes
     through. Note that `docs/ENGINE.md` in the Prometheia tree still
     says an orbit point "is geometric" and none of the terms apply --
     the engine's behaviour and the locked 3.5a agree with each other
     and not with that sentence; it reads as a stale doc, reported here
     rather than assumed.
   - **The star grammar and the ambiguity rule are the library's
     namespace, enforced by the plugin.** `star_lookup` returns
     "Beta Sco" as beta1 and beta2 Sco of equal best quality, and "8 Sco"
     and "61 Cyg" the same way (a Flamsteed number two stars carry);
     those are error 6 under 3.5a, and the plugin enforces it from the
     lookup lists, because `star_find` answers the brighter component
     and never says there was a choice. `star_lookup`'s exact matching
     does not know every form of the grammar `star_find` does (a
     Flamsteed number among them), so the resolver falls back to it when
     the lookup answers nothing.
   - **Distance unknown is a sentinel in the library and a flag in the
     protocol:** a star without a parallax comes back with a huge
     distance, which becomes column 0 and META's `noDistance`.
   - **The delta T hook is Astrolog's:** a finite per-cast value when
     the question carries one, else the -Yz override, else
     `swe_deltat()` iterated once, so `calc_ut` answers with the same
     TT-UT1 the local Swiss path would use.
   A qttest group (`prometheia`, after `ephem-server-live`) runs in both
   configurations: without PROMETHEIA it prints why it skips and passes,
   with it the pure mapping checks and the star grammar run with no data
   file at all, the engine checks run when an ephemeris is found on the
   -Yi paths (the catalog and perturbers are set when their files are
   found, and the small-body legs say what they skipped otherwise), and
   the oracle against the local Swiss path is the next increment.
   - **One bug the group caught in the plugin the same day:** the file
     search returned fTrue for an absolute path without writing it to
     the out path, so the engine opened with an empty catalog while
     `FAvailable` said fine. The designation and Chiron checks failed
     with the star namespace's error text, which named the path at once.
   - **The scratch that runs the oracle** is `/nvm/work/eph7prom-scratch`:
     the `.pc` inside the Prometheia tree's `build/` resolves its prefix
     to `/shares` (it assumes a `lib/pkgconfig` layout that does not
     exist there), so the session wrote its own with the real paths and
     runs the PROMETHEIA build with `PKG_CONFIG_PATH` pointed at it.
     Nothing in the repo reads that file.
   - **Gates:** make check all clear, suite 5689 passed, 0 failed; the
     full suite with PROMETHEIA compiled in, 5720 passed, 0 failed; the
     prometheia group itself 54 passed, 0 failed with the engine open
     against DE440 and the catalog, and a clean skip in both
     configurations (no PROMETHEIA; PROMETHEIA with no ephemeris on the
     -Yi paths). The locked artifacts are untouched; `ephsrv/ephproto.h`
     is read, not written.

11. **Phase 7, the oracle against the local Swiss path, run for real
   (2026-09-17).** The second increment of phase 7: the group's oracle
   legs, angular separations only (`SphDistance()`, work log 0c's acos
   formula), the plugin's answers against `FSwissPlanet()` over
   1990-06-15 12h UT, 2000-01-01 12h UT and 2026-09-17 0h UT. The
   engines met on **DE440**: the Swiss side pinned to the -bj backend
   (`us.nSwissEph = 2`) with the DE440 binary under the `de431.eph`
   name that backend looks for on the -Yi paths, the plugin on the same
   binary plus the SBDB catalog and the sb441-n16-de440span perturbers.
   Both sides answered at UT instants -- the plugin's delta T hook is
   bound to the chart's own swe_deltat() -- so the time scales agree by
   construction. Figures, in arcseconds of angular separation:
   - **Bodies, geocentric apparent, tropical:** the Sun, Moon, Mercury
     and Venus 0.0000" at all three instants; Mars 0.0000-0.0031";
     Jupiter 0.0043-0.0111", Saturn 0.0000-0.0031", Uranus
     0.0043-0.0092", Neptune 0.0000-0.0147", Pluto 0.0102-0.0376" --
     the outer planets carry the two engines' different small-body
     perturbation models (Swiss's files vs sb441) and nothing else.
     Gate: within 0.2".
   - **Topocentric Sun and Moon** (the same site both sides,
     Astrolog's west-positive longitude negated into the plugin's
     east-positive one): 0.0000" and 0.0384". Gate: 0.2".
   - **Heliocentric** (light time only, the mask Swiss's own
     heliocentric calls structurally carry): Moon 0.0000", Mercury
     0.2904", Venus 0.0439", Mars 0.0565", Jupiter 0.0097", Saturn
     0.0000", Uranus 0.0061", Neptune 0.0144", Pluto 0.0383". Gate: 1".
   - **The sidereal binding:** the plugin's fagan-bradley Sun against
     the Swiss side's sidereal longitude (FSwissPlanet's answer
     un-subtracted of `is.rSid`) 0.0000". And the semantics came out of
     the measurement: both engines subtract the **true** ayanamsa (mean
     plus nutation in longitude), exactly 3.5a's frame-0 rule; the
     plugin's ayanamsa column sits 8.7788" from Swiss's MEAN ayanamsa
     (swe_get_ayanamsa) and that difference is the nutation; the
     plugin's sidereal longitude plus its own column equals its
     tropical longitude to 0.001". Gate: sidereal 0.05", nutation
     within (0.5", 20"), internal identity 0.001".
   - **The Moon's true node, osculating:** Swiss's own convention
     (light time in the Earth's frame, a 0.003" term) against the
     plugin's uncorrected node 0.0881"; against the plugin's
     light-timed node 18.3150" and its fully corrected node 0.0896" --
     the two engines' light-time conventions on a node differ by 19" by
     design (work log 0c measured 19.10"; this instant 18.3"). Gate:
     tight tier 1", the conventions 25".
   - **The Moon's mean node:** 0.2045", gate 0.5" (work log 0c's
     standing fact: mean-element fits differ between engines; 0c
     measured 0.006-0.025" and this instant 0.2").
   - **The Moon's osculating apogee** (oLil, kind 1 point 3): Swiss vs
     the plugin's uncorrected 5.1601" and light-timed 3.3232" -- a
     convention pair like the nodes, gate 25"; **mean apogee** 0.2680",
     gate 0.5".
   - **Jupiter's ascending node** through a customized object (type 2,
     point 1): Swiss (aberration and deflection, no light time) against
     the plugin's mask 6, 0.5349", and mask 7, 0.5264" -- the light
     time the plugin adds moves it under 0.01", inside the two engines'
     0.5" Jupiter-model gap. Gate: 1" and 2".
   - **Chiron:** Swiss's file vs the SBDB record, 0.0350" -- the two
     realizations agree far better than the 60" gate allowed for.
   - **Aldebaran:** 0.0000" (Swiss star 198, found by sweeping
     SwissComputeStar's enumeration; gate 1").
   - **Frames and time scales, Prometheia against itself:** J2000 vs
     ICRF 0.0516" (the frame bias, gate 0.1"); true of date vs J2000
     20.0' (26.7 years of precession, gate 15-30'); true vs mean of
     date 3.4" (nutation, gate 20"); UT1 vs TT at the same instant
     0.0000"; the delta T column 0.00000 s from swe_deltat().
   - **The oracle caught one real bug in the plugin before it could
     ship:** the delta T hook returned `swe_deltat()`'s value as
     SECONDS when swe_deltat() answers in DAYS, so `prometheia_calc_ut`
     ran with an effective delta T of 0.0008 s and every UT1 answer sat
     one delta T (~69 s) late -- the Moon 34.47", the Sun 2.80", the
     inner planets on the same signature, the outer planets almost
     still. The pattern (gap proportional to apparent motion, the Sun
     at the Sun's rate) named a time offset and the probe pinned it:
     the same TT instant, plugin against Swiss, agreed to 0.0000"
     while the UT1 leg carried the whole 34.5". Fixed (the hook and the
     delta T column both now scale by 86400), and every UT1 leg since
     agrees to the fourth decimal or better.
   - **Two vendored-Swiss facts the star leg paid for, recorded because
     the next person will hit both:** `swe_fixstar2()` writes the
     star's canonical name back into its first argument -- pass a
     string literal and it segfaults inside sprintf ("Aldebaran" as a
     .rodata target); and its NUMBER form does not count the file's
     records in order ("1" answers 109 Virginis; Aldebaran is 198 in
     the enumeration the local path's SwissComputeStar() uses, which is
     how the leg finds it).
   - The Mars legs answer NAIF 4, the system barycentre Prometheia's
     own convention names, not 499; the barycentre is within metres of
     the body's centre and the Mars legs measured 0.0000-0.0031".
   - **Gates:** the prometheia group 112 passed, 0 failed with the
     engine open (DE440 both sides); the full suite with PROMETHEIA
     compiled in, 5720 passed, 0 failed (no data paths -- the group
     prints its reason and skips the engine legs); make check in the
     default build, all clear, suite 5689 passed, 0 failed. The locked
     artifacts untouched.

12. **Phase 7, reconciled onto the landed registry (2026-09-17).** Phase
   3 landed while increments 1 and 2 were in flight, so the branch was
   rebased onto ephv4 a4c6b89 -- never merged, per the coordination
   rule -- and the module was rewritten against the interface as it
   landed, which deviates from 4.1's sketch in exactly the ways the
   deviation note records: EPHQUERY is ONE instant with objects by
   Astrolog index and a host-only native hint, the EPHROW carries the
   six columns in the protocol's order with A.17's own error values,
   and the types live in ephem.h, not in the locked ephproto.h. What
   the reconciliation changed:
   - **The source is registered.** `EPHSRCDEF ephsrcPrometheia` sits in
     `rgephsrc[]` before none -- index 4 with the plugin compiled in,
     so the five indexes phase 3 pinned never move and phase 3's own
     key check became conditional on `cEphSrcPrometheia` (its one-line
     amendment, reason recorded here). `CEphSrc()` is now
     `5 + cEphSrcPrometheia`: five without the plugin, six with.
   - **The delegation philosophy, kept.** FSubmitProm() derives what
     every object IS from `FSwissPlanetSpec()` -- the same function the
     local Swiss path has always used -- and converts the SWISSSPEC to
     Prometheia's options instead of re-deriving: TRUEPOS becomes the
     light-time-only mask, NONUT becomes the mean-of-date frame, the
     solar-system-plane sidereal mode becomes per-object error 2 (the
     engine serves no such plane), the Moon's named node bodies become
     kind 1 orbit points (osculating or mean as the setting chose),
     SE_INTP_APOG/PERG become error 2 (no interpolated points here),
     swe_calc_pctr's centres become CENTER_BODY, and the Swiss body ids
     map to NAIF/SPK-IDs with the barycentres-from-Mars-on convention
     the C API names -- Mars 4, not 499; asteroids 20000000+N.
   - **The row arithmetic is FSwissPlanet()'s own.** The answer's
     longitude carries is.rSid subtracted, which the host re-adds --
     the same convention the program's two halves have always met at,
     and the reason the sidereal rows below compare like with like.
   - **The parameters took the registry's shape.** The table is the
     shared EPHPARAM (keys bare: "ephemeris", "catalog", "perturbers",
     like jpl's "file" -- the source prefix is the CLI's business),
     with the values held beside it until phase 4 moves them into
     us.rgszEphParam[]; NLookup answers EPHMATCH, nNative carrying the
     SPK-ID or the star index.
   - **The host path is measured.** A query built the way
     ComputeEphem() builds its, submitted down a chain holding the
     prometheia source alone, every row against the direct call it
     replaces: geocentric Sun, Moon and Mars 0.0000", Jupiter 0.0043",
     the Moon's true node 0.0896", the sidereal Sun 0.0000", the
     topocentric Moon 0.0384", a customized Jupiter ascending node
     0.5264" (the two engines' Jupiter-model gap, the same figure the
     internal layer's mask-6 leg measures), and a star row identical to
     the internal answer. And the walk: with the ephemeris parameter
     pointed at a missing file the source's submit refuses whole, every
     object stays open, the swiss source behind it serves, the row's
     provenance names swiss, and the fallback notice rises.
   - **One trap the rebase itself left, recorded because it reads like
     a link error and is nothing of the kind:** switching between the
     PROMETHEIA and no-PROMETHEIA configurations recompiles nothing
     unless the objects are removed -- -DPROMETHEIA lives in CPPFLAGS,
     which the .d files do not record, and a stale ephem.o compiled
     with the define failed to link against the empty plugin. Delete
     ephem.o (all three object directories) when switching.
   - **Gates:** the prometheia group 141 passed, 0 failed with the
     engine open (DE440 both sides); the full suite with PROMETHEIA
     compiled in, 5782 passed, 0 failed (no data paths -- the group
     skips its engine legs with the reason printed); make check in the
     default build, all clear, suite 5751 passed, 0 failed; the
     registry group 64 passed, 0 failed under the plugin. The locked
     artifacts untouched.

9. **Phase 3d, the chain and the notice (2026-09-17).** The fallback
   walk of §4.1 is live in every submit, and each Swiss source now
   carries its OWN ephemeris bit: the three shared-implementation
   sources borrow their SEFLG value (swiss 0, moshier 1, jpl 2) around
   the delegated calls, so §4.2's "differing only in the SEFLG
   ephemeris bit" is enforced rather than inherited -- a chain of two
   sources asks two genuinely different engines. Under every production
   chain this phase the bit equals the setting it was derived from, so
   the borrow is a no-op and the cast's bytes cannot move; the
   multi-source chain is still phase 6's, and FEphSubmit()'s derived
   chain stays one source long.
   - **The notice** stays per walk (one walk is one submit): a flag the
     walk sets when a source after the first served something, read
     back by FEphFallbackNotice(). Showing it is the Ephemeris Settings
     status line, phase 5; per-cast once across the cast's several
     submits is a phase 6 refinement, unreachable while the chains are
     one source long.
   - **The net** exercises the walk three ways. With an explicit empty
     -Yi directory (NULL paths are not enough -- SwissEnsurePath()
     falls back to exe-relative defaults and finds the bundled files;
     measured), Chiron fails through the swiss source to the moshier
     source and NEITHER answers -- the library's own silent Moshier
     fallback covers the planets, so a planet was the wrong object for
     this leg, measured and written down. A [none, swiss] chain shows
     the fallback serving, with provenance naming the second source and
     the notice raised. And the moshier source answering under a swiss
     selection returns the Moshier call's bytes, measurably NOT the
     swiss call's -- the bit is the source's own. The selection mapping
     is pinned over its whole domain (nSwissEph 0-5, files off with and
     without the Matrix), and the side-call accessor keeps the Swiss
     files source under a matrix selection.
   - **Gates.** make check all clear, suite 5834 passed / 0 failed
     (the registry net is 62 assertions in-suite); chart, switch,
     influence and graphics matrices byte-identical against the 6cf3ac3
     baseline; the registry net under AddressSanitizer, 64 passed /
     0 failed.

8. **Phase 3c, the side calls through the host (2026-09-17).** The
   direct FSwissPlanet()/swe_fixstar2() call sites of §4.1's "side
   calls use the same path" go through the registry: RProgArc()'s two
   instants (one query each -- one instant per query is this phase's
   shape, and the arc is its own instant list), the progressed/arc
   object of ComputeChartProgressions(), the eclipse's topocentric Sun
   (NCheckEclipseSolarLoc), the asteroid listing (SwissComputeAsteroid),
   and the star paths: SwissComputeStars() builds one query of every
   star it will read, each with its resolved name, and SwissComputeStar()
   asks its position per star. New FSwissStar() is the stars'
   FSwissPlanet() analogue -- the caller hands the resolved Swiss name,
   the flags are GetSwissFlags()'s plus the centre bits -- and a star
   row carries the entry point's OWN six, in the protocol's column
   order, because the star callers' post-processing is its own and
   FSwissPlanet()'s convention re-associates the sidereal offset, which
   can move a bit; swe_fixstar2()'s rewrite of the name in place is
   preserved (it IS the display name of a star enumerated by number).
   - **The side-call chain** is its own accessor, IEphSrcSideCall():
     nSwissEph mapped the way IEphSrcPrimary() maps it, IGNORING the
     files-off branch. Today's side calls are unconditional Swiss calls
     with GetSwissFlags()'s bits from nSwissEph, whatever the cast's
     selection is (§1 item 7); this reproduces that exactly in every
     selectable mode, and phase 4's chain re-plumb walks the user's own
     order instead.
   - **Deferred, with reasons.** FSwissPlanetData() (planet phenomena):
     its answer is phase, diameter and magnitude, not six position
     columns -- it needs its own row semantics, not a transposition.
     The brightness passes of the stars (SwissComputeStars()'s
     fInitBright, the fixed-instant distance calibration of
     SwissComputeStar(), SwissTestStar()): fixed calibration constants,
     not cast questions; their flags do not come from the selection.
   - **Sabotage-proven.** The chart matrix does NOT render a progressed
     arc (RProgArc's instant sabotaged: no matrix moved), but the
     suite's progressions group does -- 24 assertions fail loudly
     against its pinned references. The star path's watcher is the
     graphics matrix: one degree off every star moves 32 diff lines;
     reverted, all four matrices are byte-identical again.
   - **Gates.** make check all clear, suite 5817 passed / 0 failed;
     chart, switch, influence and graphics matrices byte-identical
     against the 6cf3ac3 baseline.

7. **Phase 3b, ComputeEphem's Swiss branch through the host (2026-09-17).**
   The per-object FSwissPlanet() call of calc.cpp's Swiss branch -- and
   its slot-5 custom skip -- goes through the registry: one query built
   before the loop, one submit down the derived chain, one read per
   object. The query holds exactly the objects the loop's Swiss branch
   will read (not a Horizons object's fJPL, not a slot-5 custom), each
   with the objOrbit the inline call used to compute, in loop order, so
   FSubmitSwissLocal()'s delegation makes the same FSwissPlanet() calls
   in the same order and the cast's bytes cannot move. The JPL Horizons
   branch and the Qt server branch (SrvPrefetchQt/FSrvPlanetQt) are
   exactly where they were; they are phase 6 plugins.
   - **Gates.** make check all clear, suite 5817 passed / 0 failed
     (the live-parity group among them, so the host path is also
     bit-identical to the server's answers); chart, switch, influence
     and graphics matrices byte-identical against the 6cf3ac3 baseline;
     the whole suite under AddressSanitizer, 5817 passed / 0 failed.

6. **Phase 3a, the source registry skeleton (2026-09-17).** `ephem.h`,
   `ephem.cpp` and `ephswiss.cpp` join the core group of
   `Makefile.srcs` (no `#ifdef QT`): the EPHSRCDEF shape of §4.1,
   EPHPARAM, EPHCAPS, EPHMATCH, the accessors, the fallback walk, and
   the five built-in sources -- `swiss`, `moshier` and `jpl` as one
   shared implementation, `matrix` and `none` as legacy-cast stubs.
   - **One deviation from §4.1, with its reason.** The question type is
     not declared in `ephproto.h`, because that header is locked
     byte-identical for phases 3-7; EPHQUERY lives in `ephem.h`
     instead, shaped as §3.4's question block (one instant, the object
     list with their Astrolog indices and centres, per-object results,
     the host-only `nNative`), with the per-object error codes carried
     as A.17's values. The phase 6 remote adapter translates it to the
     wire, and the suite pins the codes to the codec's constants.
   - **Selection does not change.** The chain derives from today's
     fields (`fEphemFiles`, `nSwissEph`, `fMatrixPla`) and holds one
     live source; `IEphSrcPrimary()` maps nSwissEph 0 and 5 to `swiss`
     (the server backend still casts locally through the Swiss files),
     1 to `moshier`, 2-4 to `jpl`, and files-off to `matrix` or `none`.
   - **The delegation.** The Swiss sources' FSubmit calls
     `FSwissPlanet()` per object with the same three arguments
     ComputeEphem's Swiss branch hands it (index, instant, objOrbit);
     the row's six reals are the answer columns in the protocol's
     order, and the one transposition from FSwissPlanet's argument
     order lives in the submit and in FEphRead(), named in both.
   - **The net.** The suite's `ephem-registry` group: the six reals as
     BYTES, host against direct FSwissPlanet calls, over the
     live-parity scenario list x 18 objects x 4 instants, plus the
     walk with a refusing head source and the notice. Sabotage-proven:
     one changed argument in the delegation fails every
     scenario-instant check.
   - **Measured while writing the net, and pre-existing.**
     FSwissPlanet() is history-dependent on a first call: a
     FSwissPlanet(oLil) at a planet-centred centre shifts the NEXT
     pctr call by ~1e-8 degrees, and that call's own second invocation
     is stable again. Casts make one call per object, so cast bytes
     are unaffected; the net warms each object so host and oracle
     compare under equal history.
   - **Gates.** make check all clear, suite 5817 passed / 0 failed
     (the live-parity group's 83 assertions against the real
     astrolog-ephd among them); chart, switch, influence and graphics
     matrices byte-identical against the 6cf3ac3 baseline.

4. **The corrApplied drop, encoded and sent (2026-09-17).** Item 1 of the
   drop list above, plus the §3.4 withdrawal (item 2), the regenerated
   fixtures (item 3) and the set sent to Prometheia (item 4). One commit.
   - **Every encoding fact was re-measured live before it was encoded** --
     a C probe against the fork's own libswe.a (`/nvm/work/corrprobe.c`),
     the same `_r` entry points the server calls, at one instant under
     explicit iflags. The per-call classification that `CorrectionsLive()`
     encodes, each row measured: `swe_calc_pctr` honours all three terms
     as asked (aberration from Jupiter's centre 8.77"); `swe_nod_aps`
     never applies light time (its underlying positions always carry
     SEFLG_TRUEPOS; only aberration and deflection are applied afterwards),
     forces deflection off for the Moon and aberration off for a
     non-heliocentric one; the Moon's named osculating and interpolated
     points (`lunar_osc_elem`) carry light time only (TRUEPOS vs default
     differs on the true node, the osculating apogee and both interpolated
     points, not under NOABERR or NOGDEFL); the mean lunar elements are
     analytic and carry nothing; the fixstar path applies deflection and
     aberration and no light time at all; and `plaus_iflag()` narrows a
     heliocentric or barycentric body to light time only. Two of the 0c
     sentences survived re-measurement; one swetest letter trap did not
     survive the writing of it -- swetest's `-p` letters J..Z are the
     FICTITIOUS bodies (Jupiter is the digit 5), and a first battery
     measured Cupido and Vulkanus where it named Jupiter.
   - **The field** shifts `resolvedNaif` and `firstFailedRow` by one in
     DATA's META fixed part, in `ephsrv/ephproto.h` (both directions),
     populated in `eph_srv.cpp` as the profile's mask intersected with
     `CorrectionsLive()` (ephswiss.h). Diagnostic only; unknown high bits
     reserved, tolerated and never refused (a fixture carries 0x87 for
     exactly that).
   - **§3.4 resolved as drafted:** the early-flush sentence is withdrawn;
     a server MUST NOT flush each block as it completes, because META's
     `rowsOk`, `firstFailedRow` and `partial` are facts about the whole
     answer. This server already holds blocks and streams when the answer
     is whole. Put to Prometheia as an explicit question with the drop.
   - **Fixtures regenerated** (92 files, every DATA-carrying one moved);
     new set-sha256
     `1c934c7da19965f21ded99a0a53e36eaa3c45434cfbfaeabddafaf154ea454ec`,
     recomputed independently from the on-disk bytes and equal.
   - **Sabotage-proven:** flipping one corrApplied bit in `ReadMeta` fails
     the codec test 7 ways (every fixture re-encode plus the dedicated
     round-trip check); restored, 311 checks and 91 fixtures pass.
   - **Gates:** golden 149 comparisons bit-exact; ROBUST PASS; make check
     all clear, suite 5772 passed, 0 failed. The suite's first run failed
     the live group with 58 connection failures -- its own freshness check
     had the reason: "astrolog-ephd is newer than its sources". A stale
     server still speaking the old META layout reads exactly like a
     protocol bug; `make ephsrv` and everything passed.

5. **The verdict is in: §3 is locked (2026-09-17).** Prometheia's reader ran
   the drop set once after implementing three of the second-round agreements
   it had missed (`deadlineMs` in the delivery block, the batched LOOKUP,
   `u8 nQueries` leading LOOKUP_RESULT) plus the new byte: first run 67/91,
   every disagreement its own staleness, not our bytes; 91/91 with zero
   disagreements after. The set-sha256 was verified by a second independent
   implementation; all three computations agree. Verdict letter:
   `/nvm/work/ephv4-drop-verdicts.md`; their reader work is committed as
   e25d74b on their origin/initial, their vendoring of this header and
   `registries.json` with checksums as 9b7ba97. Both questions confirmed:
   the §3.4 early-flush withdrawal stands (they added the client-side
   reason, now in the paragraph: a streaming client allocates per-object
   state from chunk 0, so revisable META breaks exactly the client the
   early flush was meant to serve), and **corrApplied is the capability
   set, not empty on a TRUEPOS request** -- the byte is structural, what
   the request asked stays the client's own knowledge, and the two
   servers' differing helio/bary bytes are the diagnostic working. That
   answer changed one thing on this side: the server had intersected the
   capability with the request's mask; it now sends `CorrectionsLive()`
   whole, with the capability table pinned by six unit checks in
   `ephproto_test` (which had two of its own objects mislabeled while being
   written -- the behavior was right both times).
   - **Their header review found no byte-level problems; three prose
     findings, all landed here:** `ParseWelcome`'s zero-limits rule now
     covers `maxPayload` (one word; a WELCOME advertising no payload budget
     is refused, checked in `ephproto_test`, no fixture set moved); §3.1
     now pins the verdict category for unknown `precision` and
     `columnsPresent` bits -- **unsupported**, not malformed, per this
     codec's actual behavior and the registry-growth rule; and the
     two-part-JD note is recorded as no action (identical arithmetic both
     ends; a client that puts the whole date in `jd2` loses only its own
     precision, and if §3 ever wants a guard it is a prose sentence).
   - **Gates after the follow-ups:** EPHPROTO PASS 319 checks, 91
     fixtures; GOLDEN PASS 149 bit-exact; ROBUST PASS; make check all
     clear, suite 5772 passed, 0 failed. The locked set's digest is
     unchanged (`1c934c7d…`); nothing in this commit moves bytes.
   - **Phases 3-7 are approved as next (2026-09-17), under the two
     conditions in the Status section.** On the Prometheia side, vendoring
     is done and the session migration (prometheiad onto the v4 header) is
     queued on the maintainer's go.

3. **The five debts of the protocol pass, cleared (2026-09-17).** What phase 2
   deferred, and the five §3 rules written after its spec freeze.
   - **`ephsrv/registries.json`**, generated from Appendix A by
     `tools/gen-registries.py` and diffed by `make check` like every other
     generated table. 23 registries -- message types, caps bits, both TLV
     tag spaces, observers, planes, forms, frames, correction bits, sidereal
     planes, time scales, extra columns, object kinds, orbit points and
     methods, element equinoxes and centres, per-object error codes, META
     flags, ERROR codes, the zodiac, hypothetical and precession tokens.
     `ephproto_test` then requires the CODEC's constants to agree with the
     file, by name and not by position, so prose, file and code cannot
     drift; sabotaging one message type's value and one zodiac token was
     each seen to fail it. The file is the other implementation's to vendor,
     so it carries nothing of Astrolog's.
   - **Batched LOOKUP.** LOOKUP carries `u8 nQueries` and that many `str8`
     queries, LOOKUP_RESULT `u8 nQueries` and one match list each, and
     `maxMatches` is the budget for the WHOLE message, filled in query order
     with `truncated` when it runs out. A per-query budget would let a
     260-byte message ask for 255 x 65,535 matches, and a body picker asking
     one name at a time is a round trip a name -- Object Selections offers
     78. Codec, server, wire client (`--lookup`, repeatable) and fixtures;
     the robust gate's L1 asks four names in one message, checks a query
     with no match keeps its (empty) place, and checks that three queries
     under a budget of two come back truncated.
   - **`u32 deadlineMs`** closes the delivery block, which is 16 bytes now.
     Advisory and outside the cache key: this server records and logs it and
     never fails a request for it, because it has one strategy -- samples,
     computed now -- and so no cheaper one to choose.
   - **`u32 maxSegSpanDays`** appended to the segments capability (0x000F),
     codec and fixtures only: nothing here fits segments, and the capability
     is not advertised.
   - **The two fitter-side rules** -- the check set including both endpoints
     (τ = ±1), and the 32-day J2000 lattice with downward-only quantisation
     of `segTargetErrArcsec` -- have nothing to implement in a server that
     does not fit, and are written where the fitter will go (`UnservedOf()`
     in `eph_srv.cpp`), with the measurement that makes the endpoints
     non-negotiable. Nothing in this tree claims to fit anything.
   - **Block-wise computation, and the `cancel` capability.** A REQUEST is
     computed in blocks of rows across turns of its loop, so a CANCEL stops
     the work rather than only the bytes, a cancelled request caches
     nothing, and one large request no longer holds its loop. Measured, 8
     bodies x 20,000 rows: 1420 ms of server CPU answered against 200 ms
     cancelled 200 ms in, and a one-row request on a second connection
     answered in 29 ms where it had waited 1132 ms. `tools/ephsrv-robust.sh`
     C2 gates the three, each seen failing under sabotage.
   - **Measured against §3.9, and it does not hold here.** "Computing
     time-major -- every object at one instant, then the next" is 46% SLOWER
     on this engine: a 30-body 1000-row cold window costs 463 ms that way
     against 317 ms object-major, because Swiss's caches are per body and
     want consecutive instants. So a block is a range of rows of ONE object.
   - **The §3.9 canary, run on this engine.** *The same instant asked 1000
     times against 1000 distinct instants*, one object, server compute time:
     Sun 0.07 ms against 8.4 ms, Moon 0.05 against 9.2, Mars 0.07 against
     9.3, Chiron 0.10 against 12.0, the Moon's true node 0.06 against 11.7.
     A repeated instant costs about 1% of a fresh one -- Swiss's per-body
     cache answers it -- so nothing is being recomputed there. **The
     exception is fixed stars**: Aldebaran costs 3.37 ms for the same
     instant 1000 times and 3.33 ms for 1000 distinct ones, the same number.
     Per-position work that does not depend on the instant is redone for
     every row, which is exactly the shape §3.9 warns about, though the
     absolute cost is small (3.4 µs a row, against 8.2 for a planet).
     Recorded as the next pass's item, not restructured in this one.
     *One object asked first against later*: costs are additive and order
     does not matter. Mars alone 8.8-10.6 ms, Chiron alone 11.2-12.5, the
     two together 19.4-22.0 either way round; eight bodies together 68.5-69.8
     against 69.5 for the sum of the same eight asked alone. No per-window
     cost is paid by whoever arrives first here, and no body is expensive
     merely for being asked first -- the one that is dearer (Jupiter) is
     dearer alone too. That is the other side of the time-major measurement
     above: there is nothing shared across a cast to reuse, because Swiss
     recomputes what it needs per body.
   - **A conflict in §3.4 worth fixing.** The CANCEL paragraph says flushing
     each block as it completes "starts the client's first rows sooner", but
     DATA's rules say chunk 0 MUST carry the metadata and that clients use
     chunk 0's -- and META's `rowsOk`, `firstFailedRow` and `partial` are
     facts about the whole answer. A server cannot stream a block before the
     last row is computed without writing those before they are known. This
     server computes in blocks and streams when the answer is whole, which is
     what the normative rules require; the suggestion needs either a
     "metadata may be revised on the last chunk" rule or withdrawal.

2. **Phase 2, protocol version 4 in code (2026-09-17).** `astrolog-ephd`,
   `eph_wsclient` and the Qt client speak version 4 and nothing else, in one
   commit, because the break is clean and the suite's live group casts
   through both ends at once.
   - **One header.** `ephsrv/ephproto4.h` became `ephsrv/ephproto.h`
     (namespace `eph`), and the version 3 header of that name is gone. What
     remains of version 3 is the refusal an older client can read:
     `EncodeLegacyError` writes ERROR 8 in that client's own layout and
     envelope version, which both the limits gate and the robust gate ask
     for by name. `eph::Object::nNative` moved OUT of the protocol header
     into `MapObject`'s parameters: the header is a pinned interface the
     other implementation vendors, and an Astrolog-only hint does not belong
     in it. `tools/ci-assert-vendorable.sh`, in `make check`, compiles the
     header alone under C++20 `-Wall -Wextra -Werror` from a directory where
     no other header of this tree exists.
   - **The server.** WELCOME carries the capability TLVs that describe what
     it does; REQUESTs are parsed canonically by the shared codec, then
     checked against what it serves (ERROR 11), its limits (2), its budget
     (6), its queue (9) and its drain (12). Each object is mapped through
     `ephswiss.h` and computed on the fork's `_r` entry points, per-object
     failures carrying an A.17 code. LOOKUP resolves the bodies Swiss serves
     by name, the Moon's named points, the A.15 hypotheticals, numbered
     asteroids and fixed stars. CANCEL drops the unsent chunks and answers
     ERROR 10. The operational half is untouched.
   - **Advertised is promised.** The caps bits are f32, lookup, instant
     lists, priority, designations and delta T tables -- each exercised by a
     gate. NOT advertised: `cancel` (the message works, but 3.4 also means
     the server stops computing, and this one computes a whole request in
     one loop callback), segments, elements (kind 4 answers per-object error
     2), deep sky, zstd, and orbit method 3.
   - **The Qt client** builds ONE REQUEST per cast, with a profile per
     distinct Swiss call `FSwissPlanetSpec()` decides on and an object per
     body -- `ephswiss.h`'s `ProfileFromSwiss` and `ObjectFromSwiss`, whose
     round trip through `MapObject` the codec test checks over 681
     combinations. It splits into more requests only when WELCOME's maxObjs
     or maxProfiles says to. Window cache keys are the server's own (3.7):
     the datasetId and the question block's bytes. Animation prefetch
     windows go out with `priority = 1`; an evicted in-flight window is
     CANCELled. The suite's live group casts every scenario through the real
     server and compares the arrays as bytes: all of them are bit-identical
     to the local Swiss path, as they were under version 3.
   - **Two measurements the specification asked for.**
     - *Orbit points are not geometric in Swiss.* Measured at J2000 with
       the bundled ephemeris: switching SEFLG_TRUEPOS on moves
       `swe_nod_aps`'s ascending node of Jupiter by 5.8e-3 degrees (mean and
       osculating alike), its perihelion by 7.5e-4, and the named lunar
       bodies by up to 2.5e-5 (SE_OSCU_APOG). So honouring §3.5a's rule as
       it then stood would mean answering a different point from the one
       Astrolog's local cast computes. The server therefore honours the bits
       as sent, and this was reported rather than kludged: the spec follows
       the engines. **The conclusion held; the physics cited for it did
       not** -- see item 0c. This entry said "SEFLG_NOABERR|NOGDEFL alone
       moves nothing" and inferred that the term Swiss carries is light
       time. Both halves are wrong for planetary points, and the sentence is
       kept here only so the correction has something to point at.
     - *Rates against central differences.* Swiss's speeds, against central
       differences of its own positions over +-0.001 day (and +-0.01 and
       +-0.0001, to tell real disagreement from differencing noise):
       longitude rates agree to 2.4e-6 deg/day (the Moon; the Sun 2.4e-7),
       inside 3.5a's 1e-5. Distance rates do not: Mars 2.2e-6 AU/day, Chiron
       1.7e-5, Pluto 4.7e-5, stable across all three step sizes. So the
       rates-bound capability (A.3 0x0013) is advertised at 3e-6 deg/day and
       1e-4 AU/day, and every object answered with speeds carries
       `ratesApprox`.
   - **What Swiss does with the correction masks, per observer.**
     `plaus_iflag()` turns aberration and deflection off inside every
     heliocentric, barycentric and planet-centred call to `swe_calc`, so for
     a BODY the masks 7, 3, 5 and 1 all answer as 1 there; `swe_nod_aps`
     reads the bits itself and honours them. One per-observer pair cannot
     say both, and A.3 0x0004 is per observer rather than per kind, so every
     mask is advertised for every observer and the narrowing is recorded
     here and in Appendix B.
   - **The gates** all moved to version 4 and pass: golden (149 columns
     bit-exact against the fork, over instants, time scales, a delta T
     table, an instant list, every zodiac plane, every observer, frames,
     forms, correction masks, orbit points and methods including the focal
     point, fifteen hypotheticals, a designation, and the ayanamsa and delta
     T columns), robust (the version refusals, every c2s conformance
     fixture answered as its manifest says, CANCEL, priority ordering,
     LOOKUP, and the version 3 findings), cache (the key's split, now on
     datasetId + question bytes), limits, ops (including that a per-object
     error text carries no instant), soak, tls and bench. `eph_wsclient` was
     rewritten around profiles, kinds and the new options the gates need.
   - **Found while doing it.** `Makefile.ephsrv` never included
     `eph_wsclient`'s dependency file, so a change to `ephproto.h` rebuilt
     the server and left the client linked against the old layout -- which
     reads exactly like a protocol bug in whichever end you did not suspect.
     Fixed with the client's own `.d` in the include list.
   - **Deferred, with reasons.** Segments (SEGDATA is in the codec and the
     fixtures; no server implementation, and the capability is not
     advertised); orbital elements (kind 4 answers per-object error 2 until
     the fork has an entry point); block-wise computation across loop turns,
     which is what makes CANCEL worth advertising and what stops one large
     request blocking its loop (EPHEMERIS_REVIEW.md S4 measured 13.7 s for
     64 x 20000 cells); a `registries.json` generated from Appendix A; and
     the server's object-major loop, where Prometheia measured 56 us to
     2.9 us an object-row by computing time-major and sharing the observer,
     Sun, frame and nutation work across a cast.

1. **Phase 1, the document (2026-09-17).** Written from the approved plan, three
   code surveys (state and command line, GUI, connection) and a design review
   against both codebases.
   - The review caught four errors in the first draft:
     - numbered asteroid SPK-IDs are 20000000+N, not 2000000+N;
     - the sidereal model needed a separate plane field;
     - a cast needs per-object profiles;
     - the coords field had to split into plane and form.
   - `seorbel.txt` confirmed element polynomials in T = Julian centuries from
     the epoch, up to T⁴, with J1900/B1950/J2000/JDATE equinoxes.
