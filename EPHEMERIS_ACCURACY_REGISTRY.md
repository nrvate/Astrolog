# Where this fork's numbers differ from the Swiss Ephemeris, and why

The Swiss Ephemeris is this project's oracle: `tools/swetest-oracle.sh` asks
it the same questions Astrolog asks, `tools/ephsrv-golden.sh` requires
`astrolog-ephd` to match the fork bit for bit over 149 comparisons, and the
numeric oracle group in the suite compares against the library directly.
That arrangement is deliberate and it stays.

But **being bit-exact with Swiss is a means, not the goal.** A differential
can only say "unchanged"; it cannot say "correct". Where an outside
reference shows Swiss is wrong, or where Swiss answers one question two
ways and one of the answers is degenerate, **this fork takes the
astronomically correct number and records the divergence here.**

This file is that record, in four parts: where this fork is deliberately
more correct than Swiss; where Swiss is known wrong and this fork still
follows it; divergences declined on purpose; and differences neither side
can yet adjudicate. **The second part is the uncomfortable one and is
meant to be.**

---

## 1. Where this fork is deliberately MORE correct than Swiss

Each entry states the measurement, not the intention. Every one is gated:
`ephsrv-golden`'s oracle probe asks the way the server asks, so a
divergence that is not also written into the gate fails the next run.

### 1.1 The Moon's MEAN node: distance

**Swiss answers the same question twice and the two differ.**

| | distance at J2000 | latitude, lat rate, dist rate |
|---|---|---|
| named `SE_MEAN_NODE` | 384,400.0 km — the Moon's mean distance **constant** | all exactly zero |
| `swe_nod_aps`, `SE_NODBIT_MEAN` | 368,148.6 km — the radius the mean orbit has at the node | real |

Directions agree to **8.7e-13 degrees**, so nothing a chart shows as a
position moves. A constant is not a distance to anything, and the node
demonstrably moves, so the constant cannot be right.

**It matters because the distance is load-bearing.** `ComputeEphem()`
computes a node geocentrically whatever the chart's centre and then
re-centres it in space, so a heliocentric or planet-centred chart put the
node **4.18 arcsec** out.

**This fork uses `swe_nod_aps`**, in both the local path (`calc.cpp`,
`FSwissPlanetSpec`) and the server (`ephsrv/ephswiss.h`), so the two stay
in step. Commit `477ad49`.

*Found by:* the Ephemeris Prometheia cross-test, whose independent engine
computes the radius. **Both projects first misdiagnosed it as a protocol
gap** — two defensible conventions for an undefined column — and a spec
drop was drafted and withdrawn. The probe that settled it was asking Swiss
what `swe_nod_aps` returns, which neither side had done.

### 1.2 The Moon's TRUE (osculating) descending node: distance

Both nodes lie on the line where the orbital plane meets the ecliptic, and
that line passes through the geocentre — so the descending node's
**direction** is exactly the ascending one's plus 180°. Its **distance** is
not: the two ends of that line sit at different radii on the osculating
ellipse, `p/(1 ± e·cos ω)`.

The server reached the descending node by negating the named ascending
one's vector, which kept its radius:

| | J2000 | 2026 |
|---|---|---|
| what we answered | 365,837.6 km | 387,226.9 km |
| the osculating radius there | **396,065.1 km** | **377,477.8 km** |
| error | 30,227 km (8%) | 9,749 km |

**This fork keeps the named body's direction — which is exact — and takes
the descending radius from `swe_nod_aps`.** Note that `swe_nod_aps`'s own
*ascending* node differs from the named body by 0.06 arcsec; that is noise
between two Swiss computations of one point, and the named direction is
the one an independent engine agrees with to 0.003 arcsec.

*Found by:* the cross-test noticing our two true nodes reporting the
**same** distance.

### 1.3 The `swe_calc_pctr` ecliptic-of-date cache (inherited)

The thread-safe Swiss fork at `/shares/swisseph` fixed `swe_calc_pctr()`
transforming to the ecliptic of date through caches its own inner calls had
re-keyed. `astrolog-ephd` links that fork and is therefore on the right
side of it; the vendored in-tree Swiss (upstream 2.10.03) is not. Measured
at 5e-9 degrees for the Earth, and up to 1.2e-6 degrees a day on Pluto's
speed. `ephsrv-golden` carries it as its one knowing divergence between the
server and the vendored library, with the tolerance stated.

---

### 1.4 Five names for one star — Toliman and Proxima Centauri corrected

`sefstars.txt` shipped **Rigil Kentaurus, Rigel Kentaurus, Toliman, Bungula
and Proxima Centauri** as five names on ONE entry, at identical coordinates
and proper motion. Two of those five are not that star:

| name | IAU assigns it to | was | error |
|---|---|---|---|
| Toliman | α Cen **B** (WGSN 2018-08-10) | the shared position | **16.45″** |
| Proxima Centauri | α Cen **C** (WGSN 2016-08-21) | the shared position | **2.18° = 7,860″** |
| Rigil Kentaurus, Rigel Kentaurus, Bungula | α Cen **A** (WGSN 2016-11-06) | the shared position | **6.23″** |

A client asking for Proxima Centauri — the nearest star to the Sun, V = 11.13,
parallax 768 mas — got a position 2.18° away.

**And the shared position was not α Cen A either**, which took a second pass to
see. It sits **37.8% of the way from A toward B** — not A (0%), not the pair's
photocentre (22.5%), not its centre of mass (45.7%) — with a proper motion
blended the same way (−3608, +686, against A's −3679.25, +473.67 and B's
−3614.39, +802.98). That is what an **unresolved ground-based measurement of
the pair** looks like: a centroid whose place depends on the orbit's phase at
the epoch it was measured, corresponding to no defined modern point at all.

**This fork now carries SIMBAD astrometry for all five names**, and
`sefstars.txt` is no longer byte-identical to the Swiss sibling's.

**Why the first pass missed it, which is the part worth keeping.** Toliman and
Proxima were fixed and Rigil Kentaurus was *not*, on the reasoning that it was
"the AB photocentre" and so defensible. The audit written in the same commit
gave it a 30″ tolerance and said so in a note. Both halves were wrong: the
position is not the photocentre, and a 30″ tolerance around a 6.23″ error is an
exception carved precisely where the audit was supposed to look. Prometheia's
cross-test found it from outside — our α Cen A sitting 6.23″ from theirs,
toward B — which is what an independent implementation is for. All three A
names are held to 5″ now, like B and C.

**Why the earlier reasoning was wrong.** This entry sat in §2 — "Swiss is
wrong and we still follow it" — on the argument that a vendored data file is a
provenance decision rather than a code fix. That reasoning mistakes the kind
of thing at stake. Parity is worth having only where it is parity with the
truth; a program that answers a documented, two-degree falsehood because its
upstream does is not compatible, it is jointly wrong.

**What checked this, and what could NOT.** The fix rests on the IAU WGSN's
name-to-component assignments and SIMBAD's astrometry. It has **not** been
confirmed against an independent pre-Gaia catalogue, and that is not for want
of trying: the cross-test's FK5 leg (2026-09-18) covers 28 stars, and FK5 has
α Cen **A only** — no separate B entry, and Proxima is far too faint for a
ground-based catalogue of that era. So Toliman and Proxima are the two entries
in this file resting on one catalogue rather than two. Recorded here so nobody
later reads the FK5 leg's clean result as having covered them.

**Net:** `tools/star_identity_audit.py`, which checks catalogued positions by
name and requires the IAU's distinct components of a multiple system to sit at
different places. It reports **5 failures** against the unfixed file and none
against this one, and carries four control stars so a broken parser cannot
pass it by finding nothing. Found by the Prometheia cross-test's fixed-star
leg, which agrees with this server to 0.008″ on the other 29 stars it checks.

## 2. Where Swiss is known WRONG and this fork still follows it

Recorded so that nobody re-derives them, and so that the cost of each is
visible rather than assumed. **All three reach the desktop application**,
not only the server, because Astrolog casts from the same library.

The standing reason is a hard rule: `/shares/swisseph` is a sibling
repository this project does not patch. That is a maintainer's call to
revisit, not a law of nature — and §1.1 is a warning that "it's upstream's"
can be a comfortable way of not checking.

### 2.1 Heliocentric light time — 0.38″ Mercury, 0.28″ Venus, 0.11″ Mars

Against JPL Horizons' own Sun-centred astrometric rows, Prometheia's engine
is 1–6 µas and Swiss is up to 0.38″. Bisected to the retardation itself:
light time alone moves Venus 24.208″ on a correct treatment and 24.452″ on
Swiss's, a 1% difference in the retarded position. Geometric (mask 0)
positions agree to 0.3 mas, so it is the light-time solve and not the
ephemeris.

Checked for a second Swiss answer, as in §1.1: there is none.
`SEFLG_HELCTR` and differencing barycentric positions agree to 0.0000″, so
Swiss is self-consistent here and simply differs from JPL.

### 2.2 The topocentric observer is built about the MEAN pole — 0.165″ on the Moon

`swi_get_observer()` is able to nutate the site — it has the branch — but
**every internal call site passes `SEFLG_NONUT`** (`sweph.c:3195`, `:3367`,
`:4108`). So the site vector is built about the mean pole and used against
a true-of-date geocentric vector.

Pinned by Prometheia recovering the observer offset from two topocentric
vectors — 100–290 m, horizontal, turning epoch to epoch — and reproducing
it to 1–3 m at all fifteen rows with the nutation pole offset, the opposite
sign missing by 2×.

**This is the one worth a decision.** It is not a choice between two
defensible conventions, and **every topocentric chart the application draws
carries it.**

### 2.3 Deflection at a planet-centred observer — up to 0.544″, and it bends the Sun's own light

`swe_calc_pctr()` re-bases aberration on the centring body — it passes
`xxctr` to `swi_aberr_light()` — but deflection cannot be re-based:
`swi_deflect_light()` takes no observer argument and builds its geometry
from `pldat[SEI_EARTH]`. So the bending is computed from a vector relative
to the centring body using the **Earth's** Sun geometry: a mixture that
corresponds to no observer anywhere.

Refereed by Prometheia against USNO Circular 179's `grav_vec`, implemented
from the formula rather than from either engine, and applied to each
server's own mask-1 answer so only the bending is compared: 63 of 64 rows
fail, worst Mars in 2075 bent 0.544″ where the textbook gives 0.00066″.

**What this fork does about it:** it stops *claiming* the term. WELCOME
advertises deflection for the geocentric and topocentric observers only,
and `corrApplied` reports light time and aberration for a planet-centred
call. Advertised is promised, so a capability we cannot deliver correctly
is not offered. The arithmetic remains Swiss's to fix. Commit `050a5a4`.

---

### 2.4 Astrometric binaries — up to 2.33″, and not a defect

Both this server and Prometheia carry fixed stars as a position plus a
**linear** proper motion, which is what every star catalogue supplies. A star
whose photocentre is swinging around an unseen companion does not move in a
straight line, so propagating it over a century accumulates the orbit.

Measured 2026-09-18 against **FK5** (Fricke et al. 1988, CDS I/149A) — ground
based and pre-Hipparcos, so independent of the Hipparcos-derived numbers both
servers read. Barycentric ICRS, no corrections, separation in arcsec at
1900 / 2000 / 2100:

| star | this server | Prometheia |
|---|---|---|
| Sirius | 1.756 / 0.687 / 2.332 | 1.760 / 0.687 / 2.334 |
| Procyon | 1.356 / 0.174 / 1.539 | 1.354 / 0.174 / 1.546 |
| Achernar | 0.783 / 0.143 / 1.009 | 0.783 / 0.143 / 1.009 |
| Polaris | 0.548 / 0.118 / 0.780 | 0.548 / 0.118 / 0.779 |

The other 24 stars are within **0.56″** on both servers over the same two
centuries (worst: Antares at 1900), and within 0.31″ at 2000. The two servers
agree with each other to **0.007″** everywhere.

**Why this is not a defect and is recorded anyway.** The error is smallest at
2000 and grows both ways, which is the signature of a straight line fitted
through a curve at its catalogue epoch — not of an engine being wrong. Two
independent implementations share it because they share the *model*, not the
code. It is here so that a later reader measuring Sirius against a modern
catalogue and finding two arcsec does not go looking for a bug in either
server. Fixing it would mean carrying orbital elements for the companion,
which no star catalogue either of us reads supplies.

## 3. Divergences deliberately DECLINED

### 3.1 The Gaussian constant's truncation

Swiss carries `k` truncated to ten significant figures in degrees per day,
1.446e-12 relative. It enters only through the mean motion and reaches
0.3 mas in a case contrived to accumulate 173 orbits. **Left alone on
purpose:** the constant is upstream's, and changing it would make this the
only Swiss consumer that disagrees with the rest about where a Hamburg
point is. The durable half was published instead, as an amendment to the
kind-4 drop: derive `k` from the radian value at full double precision
rather than carrying a rounded decimal in your working units.

---

## 4. Open: differences neither side can yet adjudicate

Listed because they are real and measured, but with no outside reference
saying who is right. They are NOT §1 entries: this fork is not claiming to
be more correct, only different.

### 4.1 The sidereal zero point on the invariable plane — FIXED 2026-09-18

**Fixed.** The drop was approved on both sides and this server now computes
A.8's planes 1 and 2 itself rather than delegating them to a Swiss sidereal
mode. Prometheia confirmed their construction is the same: ICRF → the mean
ecliptic and equinox of t0 → rot3(A0), plane 1 that frame, plane 2 its
projection onto the invariable plane.

**The correction applied is 31.508″ for Fagan/Bradley**, against the 31.469″
this entry predicted and the −31.51″ the other engine measured independently
between the two servers. The remainder is the ~20 mas frame bias: our
construction lives in Swiss's mean equinox and ecliptic of J2000, theirs starts
from ICRF.

**It read 28.196″ first, and the 3.3″ gap was a defect rather than a
reference artefact.** `swe_get_ayanamsa_ex` at t0 answers the **true**
ayanamsa, the arc from the *true* equinox; §3.5a wants the zero point's
longitude on the **mean** ecliptic and equinox of t0, which is that value less
the nutation in longitude at t0. Putting the true value on a mean frame moves
the origin by Δψ(t0) — −3.311″ for Fagan/Bradley, +16.777″ for Lahiri,
+17.346″ for Raman — so it is zodiac-dependent and looks exactly like the
defect this entry exists to fix. `SEFLG_NONUT` on that one call is the whole
correction.

**What caught it was a regression, not the new behaviour.** Plane 1 had agreed
with the other engine to 0.003″ while Swiss computed it, and taking it in-house
moved it by precisely Δψ(t0). Their cross-test found that within an hour. The
check that now holds it needs neither engine: **this server's in-house plane 1
reproduces Swiss's own `SE_SIDBIT_ECL_T0` plane 1 to 0.0000″** at 1900, 2000
and 2026, which is the anchor convention asserting itself against the
implementation it replaced.

**Two defects were found by the fix's own nets rather than by inspection.**
A node under a sidereal fixed plane was still being handed `SEFLG_J2000`,
because the §4.2 guard was keyed on the request's flags and switched off
whenever the sidereal path was active — so Swiss's third point went into every
fixed-plane chart carrying a node. The suite leg caught it by asserting the
server/local divergence is *an origin shift and nothing else*: a node had moved
in **latitude**, which an origin shift cannot do. And the same leg's first two
drafts partitioned objects by index range, which was wrong twice — house cusps
are not the only points Astrolog computes for itself. It partitions by whether
the object moved instead, and asserts both halves.

Sidereal plane 2 (A.8's invariable plane). Both engines' planes coincide and
longitudes differ by a constant: **−31.51″ under Fagan/Bradley, −30.42″ under
Lahiri** as the cross-test measured it.

**What decides it**, and it needs nothing from the other engine. Two coordinate
systems on the sky differ by exactly one rotation, and a z-x-z rotation has
three angles. Fitting all three from this server's own plane-0 and plane-2
output over six stars spanning latitude −39.6° to +61.7° recovers an
inclination of **1.5787010°** — Swiss's own invariable-plane constant to seven
digits — and explains all twelve observables to **rms 0.00000″**. So the planes
are not in dispute and the entire disagreement is the third angle: where
longitude starts.

Evaluating that rotation at the one direction whose answer is fixed by
definition — the zodiac's own zero point, sidereal longitude 0 on the ecliptic
of t0 — gives what our plane 2 calls it:

| zodiac | ayanamsa at J2000 | our plane-2 longitude of the zodiac's zero point |
|---|---|---|
| Fagan/Bradley | 24.7365° | **+31.4685″** |
| Lahiri | 23.8563° | **+30.3904″** |
| Raman | ≈22.4° (derived from the fit) | **+27.5764″** |

Under any reading that carries the zero point as a **direction** that column is
0 by construction. Prometheia answers 0; these reproduce the independently
measured server-to-server offsets to 0.04″.

**The number depends on the zodiac, and that is the argument.** A plane does not
know which ayanamsa was requested. Ours puts a different sky direction at 0° for
each one and never the one the zodiac names, so the origin is not tied to the
sky at all — it is tied to the equinox, and where it lands is a by-product of
how far that ayanamsa happened to have moved. Consistent with Swiss walking the
arc **in the plane** from the projected equinox rather than on the ecliptic
before projecting (predicts 32.23″ and 31.06″ against 31.47″ and 30.39″; the
0.7″ residual is anchor-epoch handling and is not claimed to be explained).

**Why the zodiac's direction is the invariant.** A sidereal zodiac is defined by
a direction in the sky, and the registry's own token names say so: `true-citra`
is Spica at 180°, `aldebaran-15tau` is Aldebaran at 45°, `galcent-0sag` is the
galactic centre at 240°. Lahiri and Fagan/Bradley are numerical fits to
statements of the same kind. The arc from the equinox is the *derived* quantity
— recomputed for every instant as the equinox precesses, different tomorrow. A
reference plane is a choice of how to *measure* directions and may not change
*which* direction a zodiac starts at, any more than a frame change may answer a
different body (§4.2's principle, reached separately).

**This supersedes the earlier reasoning** in this entry, which declined §1 for
want of "an argument from the physics". The argument is above and the
measurement is reproducible: `tools/sidplane-origin.py`.

**STILL OPEN: Astrolog's own local Swiss path.** The server computes A.8's
planes itself; `calc.cpp` still delegates them to Swiss, so the *application*
carries the old plane-2 origin while the *server* has the new one — about
31.5″ apart on a `-Ys` chart. The suite's "sidereal on the solar system plane"
leg measures that gap rather than hiding it, and requires it to be an origin
shift and nothing else, so it cannot drift unnoticed. `ephsrv/ephsidplane.h`
exists so the fix is the same rotation rather than a second copy of it.

**What makes it more than a one-line change, found by reading before writing
it.** Astrolog's sidereal handling is not in one place, and the pieces
interact:

- `space[]` holds **tropical rectangular** coordinates; `planet[]` is the
  longitude with `is.rSid` added by `ProcessPlanet()`.
- `ProcessPlanet(ind, aber)` computes `ang - aber + is.rSid`, so passing
  `aber = is.rSid` **cancels** the addition. Several call sites use that idiom
  to mean "already applied, do not apply again", and a reader who does not
  notice will double-count or zero out the zodiac.
- The re-centring paths (`fMoonMove`, the star-magnitude distances) re-derive
  a longitude from `space[]` *after* the fact, so they apply the zodiac a
  second time through that same idiom.
- **Houses are Astrolog's own arithmetic** and take `is.rSid`, a flat ayanamsa
  subtraction. They are not served by any ephemeris source, which is why the
  suite leg finds the cusps bit-identical between server and local and only
  the bodies moved. Whether a fixed plane should move the cusps at all is a
  separate question this entry does not answer.
- `is.rSid` also carries the user offsets `us.rZodiacOffset` and
  `us.rZodiacOffsetAll`, which have nothing to do with the plane.
- The Matrix backend sets `is.rSid` of its own accord (`matrix.cpp`), so the
  non-Swiss path has to keep working unchanged.

None of that makes the change hard, but it makes it a change to five places
that must agree, in code every chart runs, for an option spelt `-Ys`. It is
recorded here rather than attempted at the end of a long session.

**A second defect, fixed by the same change.** Sweeping all 47 registry zodiac
tokens on planes 0, 1 and 2: **16 returned the plane-0 answer bit-identically
for both `sidplane=1` and `sidplane=2`**, with no error — `b1950`, `j1900`, `j2000`,
`true-citra`, `true-mula`, `true-pushya`, `true-revati`, `true-sheoran`,
`galcent-0sag`, `galcent-cochrane`, `galcent-mula-wilhelm`, `galcent-rgilbrand`,
`galequ-iau1958`, `galequ-mula`, `galequ-true`, `galalign-mardyks`.
`ephswiss.h:165` sets `SE_SIDBIT_SSY_PLANE` for every token and Swiss declines
to honour it for these sixteen. These are exactly the star- and frame-anchored
ayanamsas, the ones whose definitions are most explicitly directions, so a plane
change genuinely must move them. Answering a different plane than the one asked
for is a protocol violation either way: A.8 is a request field, and a field the
server cannot honour is `kOErrUnsupported`, not a different answer. Both defects
have one fix — if the zero point is a projected direction, the plane transform
is our own arithmetic rather than a Swiss sidereal mode, and it then works for
all 47 tokens instead of 31. A zodiac whose zero point cannot be constructed
— the star- and galactic-anchored ones, whose zero point is defined at the
*instant* rather than at an epoch and which §3.5a's sentence does not cover —
now answers `kOErrUnsupported` on planes 1 and 2 rather than a plane-0 row.
Both projects read the sentence as applying to epoch-anchored zodiacs only, and
a clause for the others is a text drop neither maintainer has been asked for
yet.

**Nets.** `tools/sidplane-origin.py` is the gate, and it needs no oracle:
planes 1 and 2 are both fixed frames counting from the same zero-point
direction, so the rotation between them must carry **no origin offset at all**.
Measured **0.0000″ over four zodiacs at rms 0.00000″**. Its earlier form
measured against plane 0 and reported a 13.94″ residual that looked exactly
like a bug; that was the nutation, and a reference that moves for reasons of
its own cannot measure anything. Three golden-gate legs were **removed**, with
the reason recorded there: they compared bit-exact against the Swiss the defect
came from, so they necessarily mismatch now, and a leg that must differ does
not belong in a gate whose contract is bit-exactness. Plane 0 is untouched and
still bit-exact, which is the leg that matters.

### 4.2 Nodes in a J2000/ICRF frame — FIXED 2026-09-18

**§3.5a as amended:** a node lies on the mean ecliptic of date; the profile's
frame gives the coordinates it is expressed in and does not change which point
it is. Approved by both maintainers; Prometheia implemented it in their
`18f84ac`, this server in the commit carrying this entry.

Their maintainer's reasoning was astrological rather than geometric, and is the
better one: **the node of date is where eclipses fall**, which is what an
astrologer means by the node, and Uranian orbs are tight enough that a
frame-dependent node would move pictures.

**What was wrong.** `eph_srv.cpp` passed the profile's frame flags straight to
`swe_nod_aps`, and Swiss handed a fixed frame answers a **third point** —
neither reading. Measured by fitting the date→J2000 rotation from five ordinary
bodies (every direction takes the same rotation, so it fits to rms 0.00000″)
and asking whether the node follows it:

| epoch | node of date | rotated into J2000 | was answered |
|---|---|---|---|
| 1800 | lon 33.246026 | lon 36.038513, lat **+61.358″** | lon 36.038516, lat **+10.013″** |
| 1900 | lon 259.156463 | lon 260.553025, lat **−46.882″** | lon 260.553025, lat **−0.884″** |
| 2100 | lon 350.910313 | lon 349.513118, lat **+4.191″** | lon 349.513118, lat **−3.208″** |

The longitudes agreed to 0.003″, so the longitude really was the of-date node's,
precessed; the latitudes did not, and at 2100 not even in sign.

**The fix.** The node is computed on the mean ecliptic of date and rotated here,
through `swi_precess` — *the same precession `swe_calc` gives the bodies*. That
property is the point, and it is Prometheia's criterion: a J2000 chart has to be
internally consistent, and a precession model reimplemented here would disagree
with our own planets before it disagreed with anyone else's. `swi_precess` is
linkable in both Swiss copies and this codebase's shared core has always called
that family (`swi_epsiln`, `swi_nutation` in `calc.cpp`), so it is an
established kind of call rather than a new dependency — **no fork release was
needed**, which is where this entry expected to end up.

**Frames 0 and 1 are untouched, and that is measured rather than assumed.**
Nutation rotates the *equator*, not the ecliptic, so a point on the mean
ecliptic of date lies on the true ecliptic of date too. Asking Swiss for the
node both ways and comparing gives the same numbers to every digit, so only
frames 2 and 3 were ever affected.

**Net:** `tools/node-frame-probe.sh` piped to `tools/node-frame-fit.py`, which
now exits nonzero. It is assumption-free: it derives the rotation from the
*bodies* and requires the node to follow it, to 0.001″. **0.00000″ after the
fix, 51.345″ before it.** The golden gate could never have caught this — it
compares bit-exact against the same Swiss the defect came from, and it passes
149 comparisons either way.

**One trap, recorded because it produced a plausible wrong answer.**
`swi_polcart_sp`/`swi_cartpol_sp` work in **radians**; `swe_nod_aps` answers in
degrees. The first build fed degrees to them and put the 1800 node 34° from
where it belongs — far enough to be obvious, but the same mistake on a smaller
angle would not have been.

### 4.3 Planetary mean nodes and apsides — 60″ to 3,300″

Swiss takes mean nodes and apsides for Mercury–Neptune from the planetary
theory **VSOP87** (published manual, "Mean positions"). Prometheia fits
theirs to **DE440**. The same page anticipates the disagreement: it says
NASA's DE200-fitted mean elements are not used — 250-year validity, linear
rates only, not based on a planetary theory — and states that "the
differences between the DE200 and the VSOP87 mean elements are
considerable."

Two published sources, not a defect on either side.

## How to add an entry

An entry earns its place by **measurement against something outside this
project** — an independent engine, JPL Horizons, a textbook formula
implemented from the text. "Swiss looks wrong to me" is not an entry.

State the numbers, name the probe, say where in the code the divergence
lives, and make sure a gate fails if the divergence is silently reverted.
If the entry is in §2, say plainly what it would take to move it to §1.
