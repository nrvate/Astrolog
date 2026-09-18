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

### 4.1 The sidereal zero point on the invariable plane — RESOLVED 2026-09-18

**Decided by measurement; implementation pending the §3.5a drop's approval, on
which it moves to §1.**

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

**A second defect found on the way.** Sweeping all 47 registry zodiac tokens on
planes 0, 1 and 2: **16 return the plane-0 answer bit-identically for both
`sidplane=1` and `sidplane=2`**, with no error — `b1950`, `j1900`, `j2000`,
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
all 47 tokens instead of 31.

### 4.2 Nodes in a J2000/ICRF frame — DECIDED 2026-09-18, and ours to fix

**§3.5a is amended and this server is wrong.** Not in the way this entry
used to say, which is the part worth reading.

The amended sentence, approved by both maintainers and already implemented
on the Prometheia side: *a node lies on the mean ecliptic of date; the
profile's frame gives the coordinates it is expressed in and does not change
which point it is.* Their maintainer's reasoning was astrological rather
than geometric — the node of date is where eclipses fall, which is what an
astrologer means by the node, and Uranian orbs are tight enough that a
frame-dependent node would move pictures.

**What this entry claimed, and why it was wrong.** It said this server
"computes the node against the ecliptic OF DATE and then rotates it", and
put that forward as the reading §3.5a should be amended to. The first half
is false. Measured by fitting the date→J2000 rotation from five ordinary
bodies at the same instant — which recovers it to **rms 0.00000″**, since
every direction takes the same rotation — and then asking whether the node
follows that same rotation:

| epoch | node of date | rotated into J2000 | what this server answers |
|---|---|---|---|
| 1800 | lon 33.246026, lat 0 | lon 36.038513, lat **+61.358″** | lon 36.038516, lat **+10.013″** |
| 1900 | lon 259.156463, lat 0 | lon 260.553025, lat **−46.882″** | lon 260.553025, lat **−0.884″** |
| 2100 | lon 350.910313, lat 0 | lon 349.513118, lat **+4.191″** | lon 349.513118, lat **−3.208″** |

The **longitudes agree to 0.003″** — so the longitude really is the of-date
node's, precessed. The **latitudes do not**, and at 2100 not even in sign.
So this server answers a **third point**: neither the node of date expressed
in J2000 (the amended sentence), nor the node against the J2000 ecliptic
(the old sentence, latitude exactly 0). The +10.013″ and −3.208″ this entry
used to quote were real numbers attached to a wrong explanation, and they
looked small enough to seem like the rotation rather than unlike it.

`eph_srv.cpp`'s `kCallNodAps` passes the profile's frame flags — `SEFLG_J2000`
among them — straight to `swe_nod_aps`, so what comes back is Swiss's own
choice and nothing here has ever asked what that choice is.

**The fix, and what it needs.** Compute the node with of-date flags and
rotate the result into the requested frame here, rather than delegating the
frame to Swiss. **Swiss exports no precession routine** — `swe_cotrans` is a
single-axis obliquity rotation and that is all — so the rotation has to come
from somewhere. Two routes, and it is the maintainer's call which:

- an **additive entry point in the Swiss fork** exposing the precession it
  already computes, the same shape as the elements entry point phase 2 added
  (a fork release, a sibling repo, its own gates);
- **deriving the rotation from Swiss itself**, measured viable below;
- ~~implementing the precession model here~~ — **ruled out.** Prometheia put
  the criterion better than the framing above did: what matters is that our
  node rotates exactly as *our own bodies* do in that frame. Any model written
  here, however correct to its own specification, will differ from Swiss in
  the last digits — and then the node disagrees with our own bodies before it
  disagrees with anyone else's. That is the failure mode to avoid.

**The derived route, measured 2026-09-18.** The rotation does not have to be
reproduced; it can be derived, because three independent directions in both
frames determine it and Swiss will give any body in both frames. The node
then rotates by literally the same rotation the bodies got — the criterion
satisfied by construction rather than by care.

The objection is conditioning: every body sits near the ecliptic, so does a
rotation fitted from them hold off it? Fitting from **bodies only** and then
predicting **stars** is the out-of-sample test:

| fitted from 5 bodies within ±1.7° of the ecliptic | predicted vs actual |
|---|---|
| Aldebaran, latitude −5.5° | 9.1e-11″ |
| Sirius, latitude −39.6° | 3.5e-11″ |
| Vega, latitude +61.7° | 8.7e-11″ |
| Polaris, latitude +66.1° | 4.9e-11″ |

That is the float64 noise floor, at three epochs. Near-coplanarity only had to
leave the fit non-degenerate, not well-spread: a rotation has three parameters
and five directions over-determine it. `tools/frame-rotation-fit.py`.

So this route needs no sibling-repo release, and the fit residual is its own
health guard — a degenerate instant announces itself rather than returning a
quiet wrong matrix. Against that, the fork entry point is conceptually
cleaner: no cache, no derived matrix, no guard. **The maintainer's call.**

**Net when it lands:** Prometheia's own shape, which is the right one — ask
for the node in a fixed frame, rotate the of-date answer with the frame
matrices, require equality. Found by their points leg after the check
flipped to test the amended rule.

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
