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

### 4.1 The sidereal zero point on the invariable plane — ~31.5″

Sidereal plane 2 (A.8's invariable plane). Both engines' planes coincide —
latitudes agree to 0.03″ — but longitudes differ by a constant: **−31.51″
under Fagan/Bradley, −30.42″ under Lahiri**.

**What Swiss does**, read from `swi_trop_ra2sid_lon_sosy()`
(`sweph.c:4016`): it takes the **equinox of t0** as a vector, rotates it
into the invariable-plane frame through exactly the chain the bodies take,
measures the body from that longitude **in the plane**, and only then
subtracts the ayanamsa as a flat scalar in the plane's own longitude. It
never carries the sidereal zero point itself onto the plane.

Prometheia instead carries the point at longitude A0 **on the ecliptic of
t0** onto the plane.

Projection between planes inclined by `i` is not longitude-preserving —
the distortion is second order, about `(i²/2)·sin(2(λ−Ω))`, and
`i = 1.578701°` gives 78″ of available distortion — so walking the
ayanamsa's arc before projecting differs from walking it after by tens of
arcseconds. **It predicts the zodiac dependence too:** the two treatments
diverge in proportion to the arc walked, and Fagan/Bradley and Lahiri
differ by ~0.9° of A0, about 3.5% of 24.7° — and 3.5% of 31.5″ is 1.1″,
which is the measured gap between them. That agreement is why this is
recorded as understood rather than merely observed.

A smaller term: Swiss's plane constants (`sweph.h:312`, `:316`) are node
107.582569°, inclination 1.578701°, against Prometheia's 107.582322° and
1.578700°. The node differs by 0.89″, so a little of the gap is that, but
it is not the mechanism.

**Why this is not a §1 entry:** the registry's rule is that a claim of
being more correct is earned by measurement against something outside this
project, and there is none here — only a reading of what each side does.
The ayanamsa is a convention about a direction and the invariable plane is
a dynamical object, so which frame the convention is applied in may
genuinely matter; absent an argument from the physics, picking on
aesthetics would be exactly the reasoning this file exists to prevent.

## How to add an entry

An entry earns its place by **measurement against something outside this
project** — an independent engine, JPL Horizons, a textbook formula
implemented from the text. "Swiss looks wrong to me" is not an entry.

State the numbers, name the probe, say where in the code the divergence
lives, and make sure a gate fails if the divergence is silently reverted.
If the entry is in §2, say plainly what it would take to move it to §1.
