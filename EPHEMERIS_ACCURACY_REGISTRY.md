# Where this fork's numbers differ from the Swiss Ephemeris, and why

The Swiss Ephemeris is this project's oracle: `tools/swetest-oracle.sh` asks
it the same questions Astrolog asks, `tools/ephsrv-golden.sh` requires
`astrolog-ephd` to match the fork bit for bit over 161 comparisons -- which
include fixed stars since 2026-09-19, and did not before -- and the numeric
oracle group in the suite compares against the library directly.
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

**ADOPTED AND APPLIED**, 2026-09-18. Approved by the maintainer; Ephemeris
Prometheia moves these four on their orbits and we follow. The elements and the
arithmetic landed at `7c05f7f`; the offsets are applied to star positions from
this commit, in `ephsrv/ephstarorb.h`, which both Swiss copies compile — the
server at its `swe_fixstar2` call and Astrolog at `FSwissStar()`, so the
application and `astrolog-ephd` cannot drift.

**The agreement with the other engine is sub-milliarcsecond.** They published
their offsets as (east, north) in mas at five TT epochs from 1900 to 2100
(their `618861f`), computed from Hipparcos records with ERFA — different
catalogue data, different code, no shared line. Ours reproduce them to **0.40
mas on Sirius and 0.17 on Procyon** at every epoch, and the α Cen relative
orbit to 0.27 mas. `tools/star-orbit-check.sh` is the gate and holds all of it.

**One number is still open, and it is a mass ratio, not a method.** They split
α Cen 1.13/0.97 M☉ — Pourbaix & Boffin 2016 — while the elements both engines
use are Akeson et al. 2021, which publishes **its own** masses from the same
fit, 1.0788 ± 0.0029 and 0.9092 ± 0.0025. Taking a mass ratio from one paper
and elements from another is the thing to avoid, so this side uses Akeson's. It
is worth **185 mas at 1900 and 142 at 2100** on α Cen A — too big to round away
— so the check prints the difference at every epoch rather than grading it, and
α Cen A is the one row of the four that is not yet held to the other engine.
Nothing else in the four differs by as much as a milliarcsec.

**The FK5 table above does not measure what this entry first claimed it did,
and the reasoning is corrected here rather than quietly dropped.** Re-derived
2026-09-18 (1.750 / 0.665 / 2.242 for Sirius against the 1.756 / 0.687 / 2.332
recorded — the same measurement), and then looked at as a *vector* instead of a
magnitude: the Sirius residual is E = (+0.001, −0.665, −1.330) and
N = (−1.750, +0.027, +1.804) at 1900 / 2000 / 2100 — **linear in time to three
figures, with no periodic part at all.** The reason is arithmetic. Sirius's
period is 50.1284 yr, so 1900→2100 is 3.99 periods and all three epochs land at
orbital phase ≈ 0.11: the table samples the same point of the orbit three
times. What those numbers measure is the ~19 mas/yr proper-motion difference
between FK5 and Hipparcos.

FK5 cannot be a direct oracle for the offsets at any epoch, because **FK5's own
model is a straight line too**, and two straight lines can differ only by a
constant and a slope. The growth away from 2000 is still real evidence of the
binary — two lines fitted to the same curved path over different windows have
different slopes — but "the signature of a straight line fitted through a curve
at its catalogue epoch" claims more than the measurement supports. Ephemeris
Prometheia's own notes had already read those residuals as the proper-motion
difference; the phase coincidence is the sharper reason and is new.

**What the nets are, now that FK5 is not one of them.** Three, and they fail
differently:

- **ORB6's own published ephemeris** — θ and ρ at 15 epochs from these same
  elements, to better than 0.01″. Nets the Kepler solve and the projection.
- **sefstars.txt's own α Cen A and B** — the catalogue carries the pair as two
  records, so it states their relative position itself. Carried back to the
  Hipparcos epoch with their own proper motions the separation is
  E = −11.0275″, N = −15.6070″ against the orbit's −11.0952 and −15.5796:
  **0.073″, in both components.** This is the leg that sees DIRECTION, and a
  sign flip in either component misses it by 22 to 31″. Prometheia ran it
  against their own Hipparcos records and got the same difference in the same
  direction (+73.0, −28.0 mas against our +68, −27) — two catalogues, two
  implementations, one answer. They have adopted it too.
- **An angle no change of frame can move** — at α Cen A, the angle between the
  direction to B and the direction to Vega, computed in seven frames. This one
  earned its place immediately: the first implementation carried the offset
  into Swiss's *equatorial sidereal* output through the ecliptic, on the
  reasoning that an ayanamsa is an ecliptic quantity. Swiss subtracts the
  ayanamsa from **right ascension** there. The offset kept exactly the right
  size and pointed **4.46° away**, which is 1.3″ of position on α Cen B, and
  every magnitude check stayed green. Measured, then fixed: the pole the
  sidereal step turns about is the output plane's own.

**The bit-exact golden gate WAS structurally blind to all of this, and is not
any more** (2026-09-19). It had no star legs at all, so it neither caught nor
could catch a star defect. It now has sixteen: six stars bit-exact against the
fork, Vega across two instants, both planes, three frames, both forms, two
zodiacs and a topocentric site — and, for the four here, the **inverse**
assertion, because for exactly these the gate's premise is turned around and
the server's bits are deliberately not the fork's. Each must differ by between
0.05″ and 30″, and a star with no orbit asked the same way must still be
bit-exact, so a change that moved every star fails rather than passing both
halves. Sabotage-verified: with the orbit lookup disabled it reports Sirius
differing by 0.0000″.

That leg grades a **magnitude**, and says so in the file: direction is owned by
`tools/star-orbit-check.sh`'s three legs. The division is deliberate — golden
asks the question that file cannot, which is whether the correction reaches the
wire at all.

**Three conventions that have to be right, all found by a check failing rather
than by reading.** Each cost an engine a real error:

1. **ORB6's published θ is at the equinox of DATE; the catalogue's node Ω is at
   the equinox its record names.** The difference is the precession of position
   angle, 0.0056°/yr × sin α × sec δ — 0.13–0.19° over 25 years, and it
   **changes sign** between the northern and southern systems, which is how it
   was identified, since rounding cannot do that. Our check applies it; **the
   offset applied to a star must not**, because ours is wanted in one fixed
   frame and re-precessing the node per epoch puts the orbit in a frame that
   moves.
2. **The offset is laid in the tangent plane at the star's position AT THE
   DATE, not at its catalogue place.** Proper motion turns the local north by
   Δα·sin δ — 0.065° for α Cen by 2025, about 10 mas at the present separation
   and more as it opens. Prometheia found this when they added θ to their own
   check after we flagged the first convention; their separations-only check
   could not see it, because ρ is exactly the coordinate neither convention
   touches.
3. **Do not do both.** Propagating a *shifted* J2000 place with ERFA's `pmsafe`
   already keeps the offset aligned with the date's north, because the shifted
   point moves at the same RA/Dec rates. Adding an explicit north-turn on top
   double-counts, whichever sign is chosen.

4. **The cross-test's own binary rows cannot see direction, today.** Prometheia
   disclosed this unprompted: their `expected-difference` grading for these
   four compares |ours − theirs| against `hypot(east, north)` of the bend — a
   **magnitude**, so a bend laid in the wrong *direction* passes it. Harmless
   while this side has no orbit at all. The moment we apply offsets those rows
   become direct position comparisons, which do see direction, and they should
   drop to the ordinary star band. **If one instead lands exactly at the old
   bend magnitude, the offset is going the wrong way rather than missing** —
   and that is the single most likely mistake, since east/north sign and
   θ-from-north conventions are where this kind of arithmetic goes wrong.

**And the per-star rule, which is not uniform**, because it depends on what the
catalogue line already contains:

- **Sirius A, Procyon A** — Hipparcos *orbital* solutions, so the line is
  barycentric: add the whole offset from the barycentre.
- **α Cen A** — a *component* solution at 1991.25, so the line already carries
  the star's own motion there: add only the curvature the straight line misses.
- **α Cen B** — placed from A plus the relative orbit, because B's own solution
  is poor (±20–26 mas/yr).

**Why this was not a defect and is recorded anyway.** The error is smallest at
2000 and grows both ways, which is the signature of a straight line fitted
through a curve at its catalogue epoch — not of an engine being wrong. Two
independent implementations share it because they share the *model*, not the
code. It is here so that a later reader measuring Sirius against a modern
catalogue and finding two arcsec does not go looking for a bug in either
server. Fixing it would mean carrying orbital elements for the companion,
which no star catalogue either of us reads supplies.

### 2.5 A star-anchored zodiac's zero point wobbles 40″ a year — the anchor is taken apparent

**Its RATE was missing until 2026-09-18, and that is worth recording beside
it.** The correction this entry describes was applied to the longitude and not
to the longitude's rate — but the correction is the anchor's own aberration in
longitude, and an entry about a term swinging 40.179″ over a year is an entry
about something with a rate. Every one of the eleven instant-defined zodiacs
reported a longitude that moved while its speed column said otherwise, by up to
**9.9e-5 °/day**, uniform across every body and a function of the date alone.

Found by Ephemeris Prometheia's cross-test `rates` leg, whose oracle is each
server's own positions — a five-point central difference at h = 1/1024 day,
that step chosen because at h = 0.001 the Julian day's own quantization costs
7e-7 °/day, the size of the thing being measured. Neither engine grades the
other, so there is nothing to agree about. `tools/ephsrv-rates.sh` asks the
same question on this side; with the fix removed it reports 9.875e-5 °/day at
J2000 and 4.8e-5 at 2461300.5, against the 9.8e-5 and 4.8e-5 they measured
independently on the wire.

It grades an instant-defined zodiac against an **epoch-anchored** one rather
than against an absolute band, and that is deliberate: Swiss's own speeds sit
at 1e-6 to 1e-5 °/day against a difference of its own positions, and a
topocentric Moon is worse. Those are Swiss's numbers and belong in §2, not
here. What is this server's is a rate error that appears only when the zero
point is the one thing that changed.

Eleven of A.11's tokens anchor their zero point to something in the sky rather
than to an epoch: `true-citra`, `true-revati`, `true-pushya`, `true-mula`, the
four `galcent-*` and the three `galequ-*`. Swiss computes their ayanamsa from
the anchor's **apparent** position. **Swiss's own published documentation says
the true position** — §2.8.12, items 4–5, "no aberration or deflection".

**Measured** against Prometheia's independently computed J2000 values. Three
galactic-centre modes are off by *identically* −20.4011″, which says the gap is
in the anchor and not the mode; everything is inside ±20.5″, the size of
aberration; and measuring the anchors closes it:

| anchor | aberration in longitude, J2000 | the mode's gap |
|---|---|---|
| Spica | −4.83620″ | `true-citra` −4.8284″ |
| Revati | +3.42085″ | `true-revati` +3.4067″ |

**Why this is a defect and not a convention.** Aberration is an artefact of the
observer's motion, not a property of the sky, so an apparent anchor makes the
zodiac's zero point swing through a full cycle every year. Spica's aberration
across 2000 runs +16.7″, +19.9″, +5.9″, −19.4″ — **40.179″ peak to peak**. A
sidereal zero point that oscillates by 40 arcsec a year is not a fixed
reference, and being one is the whole of what a sidereal zodiac is for.

**Why this fork still follows it.** The fix is a §3.5a sentence, drafted as the
anchors drop and awaiting the maintainer. It is the *ayanamsa* that is wrong,
so it moves **plane 0** — ordinary sidereal charts — for all eleven modes, and
by a different amount at every instant. Nothing in Astrolog itself moves: the
application's own sidereal path offers Fagan/Bradley only.

**`galequ-iau1958` is a different thing and is now explained.** It is off by
**−0.1754″**, too small for aberration and too large for float noise, while its
two siblings agree to 0.0007″ — so Swiss applies no aberration to those two.
Prometheia identified it as the **ICRS realisation of the IAU 1958 pole**,
which has no aberration to have, being fixed in ICRS: the 1958 pole is defined
in B1950/FK4 and its ICRS value depends on the transfer, the E-terms and the
FK4→FK5→ICRS chain. Theirs is Liu, Zhu & Zhang eq. 19, and the Hipparcos
transfer (ERFA's `g2icrs`) independently agrees with it to 0.012″. Two
published transfers within 12 mas of each other, against our 0.175″, so ours is
the outlier and the difference is in which transfer Swiss used. Recorded as a
transfer difference rather than a defect, because nothing here has established
which transfer Swiss intended.

**`true-sheoran`: Swiss's implementation contradicts its own documentation, and
the implementation is what we follow.** Its published definition is
epoch-anchored — −60° at the winter solstice of 4174 BCE — while Swiss's
ayanamsa table gives it **t0 = 0**, the marker for "no anchor epoch". The
documentation would have us hardcode a datum the library does not carry, so
this looked like it needed adjudicating.

It does not: **Swiss's implementation is measurably star-anchored.** Switching
aberration off moves its ayanamsa by **18.3463″** at J2000 — an epoch-anchored
ayanamsa is a fixed arc plus precession and cannot care about aberration at
all, which the Lahiri and Fagan/Bradley controls confirm at exactly 0.0000″. So
whatever the documentation calls it, the code computes it from an anchor in the
sky at the instant asked, and this server's treatment — anchorless, refused on
the fixed planes, true anchor on plane 0 — is right for the library we actually
link. Nothing is hardcoded and nothing is invented.

The same probe settles `galequ-iau1958` from the other direction: **0.0000″**,
no anchor in the sky at all, which is why the true-anchor change did not move
it and confirms its −0.1754″ is the ICRS transfer and nothing else.

### 2.6 A topocentric Moon's speed does not describe its own positions — 8e-4 °/day

Swiss's reported longitude rate for a **topocentric Moon** disagrees with a
five-point central difference of **Swiss's own topocentric longitudes** by
7.9e-4 °/day. That is Swiss contradicting itself, not two engines differing,
and it is 25 times the 3.5a rates bound this server advertises.

Measured 2026-09-18, h = 1/1024 day (exactly representable; a 0.001-day step
costs 7e-7 °/day in the Julian day's own quantization, and three points
truncate at about 4e-5 on a topocentric Moon — both findings are Ephemeris
Prometheia's), site Zürich 8.55E 47.37N 400 m:

| | J2000 | 2461300.5 |
|---|---|---|
| **Moon topocentric** | **7.917e-04** | **8.216e-04** |
| Moon topocentric, no light time | 9.646e-04 | 9.437e-04 |
| Moon geocentric | 7.208e-06 | 3.342e-05 |
| Sun topocentric | 8.739e-07 | 2.511e-06 |
| Mars topocentric | 5.223e-06 | 6.862e-06 |

**It is the Moon and the topocentric observer together.** Geocentrically the
Moon is a hundred times better; topocentrically the Sun and Mars are a hundred
times better. Turning light time off makes it slightly *worse*, so the
retardation is not the cause. What is left is the observer's own motion: the
diurnal parallax rate is about 1.7 °/day for the Moon and Swiss carries most
of it — the topocentric and geocentric rates differ by that much — so the
residue is a modelling gap in that term, not its omission.

**THIS SERVER ADDS NOTHING TO IT.** Prometheia's cross-test measured 7.9e-4
and 8.2e-4 on our wire; the numbers above come from calling the library
directly. They agree to three figures, so `astrolog-ephd` is passing Swiss's
answer through unaltered — and, incidentally, the vendored 2.10.03 and the
fork 2.10.03-ts.14 behave identically here, which the two measurements
together establish without a third run.

**Not fixed, and the reason is that the fix is not ours to pick.** Reporting a
differenced rate instead would diverge from the library on a column
`ephsrv-golden` holds bit-exact, for a body whose topocentric speed almost
nothing reads. Recorded so that a later reader measuring it does not go
looking for a defect in this server.

### 2.7 The distance and the distance RATE are different quantities — up to 4.7e-5 AU/day

Swiss reports the **light-time-retarded distance** in column 2 and the
**geometric** range rate in column 5. The two therefore do not describe the
same curve, and differencing the distances does not give the rate.

The proof is that turning light time off makes the disagreement vanish
entirely. Same method as §2.6:

| | apparent | geometric (`SEFLG_TRUEPOS`) |
|---|---|---|
| Mercury | 2.505e-06 | **1.427e-12** |
| Pluto | **4.748e-05** | 2.903e-11 |
| Moon | 2.016e-09 | 1.929e-14 |

Twelve orders of magnitude between the two columns is not a tolerance
question. With the retardation off, Swiss's rate column describes its distance
column to the last bits it can; with it on, the gap is ṙ·(dτ/dt), which is why
it scales with the light time — Pluto worst, the Moon negligible at a light
time of 1.3 seconds.

**The mechanism was Ephemeris Prometheia's**, offered as a measurement of
their own engine rather than a claim about ours: their range rate is the
derivative of the light-time range, and they predicted a geometric one would
miss by exactly ṙ·(dτ/dt), "of order 1e-6 AU/day for the planets and tiny for
the Moon". It is.

**A number that looked open and was a unit.** Their leg reported our Pluto at
1.5e-6 where the library gives 4.748e-05, and I recorded the thirty-fold gap
as a question rather than rounding it away. It was the right instinct and the
wrong guess: their distance column is **AU/day per AU of distance**, and Pluto
was 31 AU away at that instant — 1.53e-6 × 31 = 4.7e-5. Same measurement, and
the mask was not different. **4.748e-05 AU/day is the number.** They are adding
the unit to the leg's printed header.

**A candidate for §1 rather than §2, and not decided here.** Unlike §2.6 this
is not a modelling approximation, it is two columns of one row answering
different questions, and "parity with truth only" points at fixing it — the
honest rate for a retarded distance is the derivative of the retarded
distance. Against that: it diverges from the library on a column
`ephsrv-golden` holds bit-exact, and the protocol does not yet say which
quantity column 5 is. That last gap is the real finding, and it wants a
sentence in §3 before either engine changes a number.

### 2.8 Every rate is the derivative of the UNTRANSFORMED quantity — and §2.7 is one case of it

§2.7 found the distance rate describing a different curve from the distance.
It is not special. **Swiss's rate columns are the derivatives of the geometric,
mean-frame quantity, while its position columns are the transformed one** — so
every transformation applied to a position (light time, aberration, the
rotation into the true ecliptic of date) is missing its own time derivative
from the rate beside it.

Measured 2026-09-18 by turning each term off in turn and re-differencing, six
bodies, geocentric, h = 1/1024 day. The miss and the term it lives in:

| column | term whose derivative is missing | example |
|---|---|---|
| distance | light time | Pluto 4.748e-05 AU/day → **2.9e-11** geometric |
| latitude | **nutation in obliquity** | Mercury 5.765e-06 → **6.19e-08** with nutation off |
| longitude | light time, and aberration for the Sun | Sun 4.078e-07 → **4.04e-10** with aberration off; Mercury 2.669e-06 → **4.51e-09** geometric |

**The latitude case is the one that can be predicted in closed form, so it is
the proof.** Rotating into the *true* ecliptic of date tilts by the nutation in
obliquity, and a change dε moves ecliptic latitude by −dε·sin λ. If the
rotation's derivative is the missing term, the miss must be |dε/dt · sin λ| — a
different number for every body, because it depends on where the body is.
Against Swiss's own nutation (dε/dt = −6.063e-06 °/day at J2000):

| body | λ | observed | predicted | ratio |
|---|---|---|---|---|
| Sun | 280.4° | 5.913e-06 | 5.964e-06 | **0.99** |
| Mercury | 271.9° | 5.765e-06 | 6.060e-06 | **0.95** |
| Mars | 328.0° | 3.197e-06 | 3.216e-06 | **0.99** |
| Jupiter | 25.3° | 2.617e-06 | 2.587e-06 | **1.01** |
| Pluto | 251.5° | 5.736e-06 | 5.748e-06 | **1.00** |

Five bodies spanning 300° of longitude, ratios within 5% of unity, and the
prediction varies by a factor of 2.3 across them — so the agreement is not a
scale factor that any constant would satisfy. Repeated at 2461300.5 where
dε/dt is 1.6 times larger: Mars, Jupiter and Pluto all 1.00.

**The Moon does not fit and is not made to.** Ratio 1.28 at J2000 and 0.03 at
the second instant. −dε·sin λ is a small-latitude approximation and the Moon's
latitude reaches 5° and changes fast, so other terms are comparable there. A
formula that fitted the Moon too would be one fitted to the data.

**AND THE ADVERTISED BOUND WAS WRONG, WHICH IS OURS.** §3.5a requires a server
whose rates miss by more than 1e-5 °/day to state **its largest such
difference** (A.3 0x0013). This one advertised **3e-6 °/day**. That figure was
measured geocentrically, on five bodies, at J2000 — so it never asked the
observer that matters. Swept across four observers, thirteen bodies and four
instants from 1800 to 2400, the real worst is **4.05e-03 °/day**: the
topocentric lunar node at 1800, the osculating node's own jitter seen through
the diurnal parallax. A client trusting the advertisement was out by **1351**.
Corrected to 5e-3 °/day. The distance figure, 1e-4 AU/day, was already right
(worst measured 5.06e-05, geocentric Neptune).

**A measurement trap worth keeping**, because it cost a plausible-looking
5,000,000× result before it was caught. Near the edge of an ephemeris file
Swiss **silently falls back to Moshier for some points of a stencil and not
others**, returning success either way — only the returned FLAGS differ
(18180 against 18178). A five-point stencil straddling that boundary
differences two different theories and reports 16 °/day, which is four times
Mercury's own speed and entirely the measurement's. Every point of a stencil
must be required to come from the same ephemeris. Stability across h = 1/512,
1/1024 and 1/2048 is the other guard: the real misses above repeat to five
figures, the artefact does not.

**All of it is Swiss's**, on the same evidence as §2.6: these numbers come from
calling the library directly, and `astrolog-ephd` passes them through. The
`equator` of date and the `J2000` ecliptic are clean in the other project's
cross-test, which is what a missing *ecliptic-of-date rotation* derivative
predicts and a general speed defect would not.

**Ephemeris Prometheia's engine does not have this**: their rates are the
derivatives of the coordinates they report, retardation and frame included. So
the two engines differ on every rate column by construction, and §3.5's text
does not say which is meant — see §2.7. One sentence closes all four columns
at once, which is why it should be general and not about distance.

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

**CLOSED for the bodies, and then the STARS turned out to be a separate half,
closed 2026-09-18.** `FSwissPlanet()` took the new origin; `FSwissStar()` did
not. It handed `SEFLG_SIDEREAL` with sid mode `SE_SIDBIT_SSY_PLANE` straight to
Swiss, so in one `-Ys` chart **the stars used Swiss's plane-2 origin and the
planets used ours** — 31.47″ apart on Aldebaran at J2000, in longitude alone,
the latitude identical to the last digit. A chart has to be internally
consistent before it is accurate.

Nothing saw it because nothing compared a star against anything but Swiss asked
the same way: the numeric oracle asked Swiss exactly what `FSwissStar()` asked,
so it agreed; `tools/ephsrv-golden.sh` has no star legs; and the
server-versus-local leg casts bodies, with the stars restricted by default. **A
scenario added to that leg passed with the bug deliberately put back**, because
clearing the restriction did not make the scenario cast a star — so it was
thrown away rather than tuned, and the check moved to the oracle group where a
number can be held against something outside this repository.

The net is **Ephemeris Prometheia's own plane-2 longitudes** (`510e1ab`):
Aldebaran, Polaris and Spica at three instants, geocentric apparent, from a
cleanroom engine sharing no code, catalogue or ephemeris with this one. Three
declinations on purpose — +66°, −3.6° and −4.5° — because an origin shift is
the same arc everywhere and a wrong *plane* would not be. Our fixed path sits
within **0.04″** of theirs; with the fix removed the check reports **31.511″ in
longitude and 0.019″ in latitude**, identically for all three.

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
