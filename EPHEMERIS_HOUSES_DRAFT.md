# Houses in protocol v4 — draft for review (2026-09-30)

**Status: DRAFT, not yet part of EPHEMERIS_PLUGINS_PLAN.md §3.** Agreed with
the Prometheia project in the exchange of 2026-09-30; this text is what I will
merge into §3.4, §3.5, a new §3.5b and Appendix A once the other project has
read it. Nothing here is in `ephproto.h` or the fixtures yet.

The design in one paragraph: houses are a new **object kind** (6, "house
point") in REQUEST/DATA, not a new message. A house point is one of the twelve
cusps or four angles of one house system. It answers over the request's grid
or list of instants like any object, so a series is free; it takes rates from
`speeds`; a system that is undefined at a latitude is a per-object (and per-row)
error. The Appendix A additions are one object kind, two extra columns, one
per-object error code, a registry of house systems, and two WELCOME TLVs. No
message type and no capability bit is added.

## What changes in the registries (Appendix A)

| registry | addition |
|---|---|
| A.12 object kinds | **6 house point** |
| A.10 extra columns | **bit 4** ARMC, deg; **bit 5** true obliquity of date used, deg |
| A.17 per-object errors | **9 undefined here**: the point or system is not defined for this instant and site (§3.5b) |
| **A.22 house systems** (new) | 0 placidus, 1 koch, 2 porphyry, 3 regiomontanus, 4 campanus, 5 equal, 6 whole-sign, 7 alcabitius, 8 morinus, 9 meridian, 10 topocentric. Open-ended; Swiss's letters P K O R C A W B M X T are a comment, not the wire. |
| **A.23 house points** (new) | 1..12 cusp 1..12; 13 Ascendant; 14 Midheaven (MC); 15 Vertex; 16 equatorial Ascendant |
| A.3 WELCOME TLVs | **0x0015** house systems served: `u16 n, n × u8` (A.22 ids). **0x0016** sidereal time model: `str8`, the token `iau2006-2000a` for the reference model of §3.5b, otherwise a name the server chooses |
| A.3 0x0014 corrections by kind | a server serving kind 6 advertises `(topocentric, kind 6, mask 0)`; nothing else is honoured for it |
| A.18 META flags | unchanged (`noDistance` and `ratesApprox` are used as they are) |

## §3.4 — OBJECT kind 6 (addition to the payload table)

| kind | payload |
|---|---|
| 6 house point | `u8 system` (A.22), `u8 point` (A.23) |

A system that is not in A.22, and a point that is not in A.23, are ERROR 11
(registries grow). A system A.22 defines that this server does not serve
(0x0015) is per-object error 2.

## §3.5 — Semantics of kind 6

**The row.** The six base columns are the point's ecliptic longitude (deg,
[0, 360)), latitude 0, distance 0, then their rates. META's `noDistance` is set.
Longitude is in the ecliptic of date, true equinox, as §3.5a's frame of date
defines it. With a sidereal zodiac the longitude is the tropical value less the
ayanamsa of that row (the value the ayanamsa column reports, §3.5a), for every
point and every system; Whole Sign starts at the sign of the *sidereal*
Ascendant.

**The profile a kind 6 object references must be:**
- observer 1 (topocentric): it supplies the site. The site's longitude and
  latitude are the observer; **no parallax and no refraction is applied to a
  house point, and the site's height does not enter.** Height is carried, not
  pinned: two heights are two cache keys for one answer, which costs a cache
  entry and nothing else.
- plane ecliptic, form spherical, frame true of date, `siderealPlane` 0. Any
  other value is per-object error 2. (A cusp on a fixed sidereal plane is not
  defined by anything.)
- corrections mask 0. This needs no new rule: tag 0x0014 advertises
  `(topocentric, kind 6, mask 0)` and §3.5's existing sentence makes any other
  mask on a profile a kind 6 object references ERROR 11 for the whole request.
- `zodiac` tropical or any token the server serves (A.3 0x0007).

**Extra columns** follow the base six in A.10 bit order. For kind 6:
- bit 1 (ayanamsa), bit 3 (ΔT used), **bit 4 (ARMC)** and **bit 5 (true
  obliquity)** are answered.
- bit 0 (σ) and bit 2 (light time) are per-object error 2.
- Bits 4 and 5 asked of any *other* kind are per-object error 2. A profile
  that asks for them therefore has to be its own profile, and profiles are
  per object already.

**Rates.** When `speeds = 1` they are §3.5a's five-point stencil derivative of
the answered longitude, `h` = 1/1024 day, with the 360° wrap unwrapped before
differencing. Three cases have a jump, and the rule is one sentence:

> When any stencil point is refused (error 9), or two adjacent stencil values
> differ by more than 90° after unwrapping, the three rate columns of that row
> are 0 and META's `ratesApprox` is set for the object.

`ratesApprox` is per object, not per row, so a client cannot tell which rows;
that is the cost of the sentence and it is stated here rather than hidden. Two
consequences: the Ascendant of a site inside the polar circle flips by 180°
when the ecliptic lies in the horizon plane, which the 90° test catches; and
**Whole Sign rates are 0 always**, even when a sign change falls inside the
stencil, because a step has no derivative and 0 is the one answer that is not
an artefact of `h`. Whole Sign sets no flag for this.

**Failure.** A cusp or angle that is undefined for a row's instant and site is
that row failing: NaN in every column of the row (§3.5), `errCode` 9, and the
object is `partial` with `firstFailedRow` — a series can cross the boundary
partway, so the refusal is decided per row with that row's own obliquity. The
object fails altogether (`rowsOk` = 0) only if every row does.

## §3.5b — Physical definitions of houses (normative)

Inputs, per row: the instant (UT1 and TT via §3.5's ΔT), the site's east
longitude λ and latitude φ, and a frame in which the equator and ecliptic of
date are those of §3.5a. Two derived quantities are **reported** with every row
(A.10 bits 4 and 5) and every point is a function of them and φ:

- **ARMC** = GAST + λ, in degrees, where GAST is the Greenwich apparent
  sidereal time.
- **ε** = the true obliquity of date: the mean obliquity of the model §3.5a
  names, plus the IAU 2000A nutation in obliquity.

**Sidereal time.** The reference is IAU 2006 GMST (from the Earth rotation
angle) plus the IAU 2000A equation of the equinoxes (ERFA `gst06a`). A server
MUST agree with it to 0.01″ over 1850–2050. Outside that range it MAY
deviate, and says so through 0x0016 by naming its own model. (Swiss's
long-term model deviates by +1.22″ at 1600, +0.35″ at 1800, −1.79″ at 2100 and
−0.68″ at 2200, measured against `gst06a` with Swiss's own ΔT; the deviation is
not ΔT and agrees exactly over 1900–2026. Both projects reproduce these
numbers.) Cusps and angles are graded **at the row's own ARMC and ε**; the ARMC
itself is graded separately, at that tolerance.

**Notation.** `α(x)` is right ascension. `E(a)` is the ecliptic point of right
ascension `a` (on the hour circle): tan λ = tan a / cos ε. All angles are in
degrees.

**Angles.**
- **Ascendant** (13): the intersection of the ecliptic with the horizon that
  lies **east** of the meridian (negative hour angle). Inside the polar circle
  the closed form atan2(cos θ, −(sin θ cos ε + tan φ sin ε)) gives the western
  one; the side, not the formula, is the definition.
- **Midheaven** (14): `E(ARMC)`, the culminating point of the ecliptic, in every
  system. It can be below the horizon.
- **Vertex** (15): the intersection of the ecliptic with the prime vertical on
  the **west** side (positive hour angle).
- **Equatorial Ascendant** (16): `E(ARMC + 90°)`.

**Cusps.** Cusp `k` is a point of the ecliptic, and in every system below cusp
k + 6 is the opposite of cusp k, so each entry defines the cusps it needs and
the rest follow. Cusp 1 is the Ascendant and cusp 10 the MC except where an
entry says otherwise (Equal at cusp 10, Whole Sign, Morinus and Meridian, whose cusps are not
those points).
- **Equal** (5): Ascendant + 30° (k − 1). **Whole Sign** (6): 30° × floor(
  Ascendant / 30°) + 30° (k − 1) — in the sidereal zodiac the *sidereal*
  Ascendant.
- **Porphyry** (2): cusps 10 and 1 are the MC and the Ascendant; the arcs of
  ecliptic between them, and between the Ascendant and the IC, are trisected.
- **Meridian** (9): cusp k = `E(ARMC + 30° (k − 10))`. **Morinus** (8): the ecliptic
  point of the same *longitude* as the equator point of right ascension
  ARMC + 30° (k − 10): λ = atan2(sin a cos ε, cos a). Morinus cusp 10 is **not**
  the MC.
- **Regiomontanus** (3): cusp k is the ecliptic point on the great circle
  through the horizon's north and south points and the equator point of hour
  angle −30° (k − 10), on the half of that circle holding that point.
  For k = 10 that circle is the meridian and the point is the one above the
  horizon; when the MC culminates below it, that is the IC.
- **Campanus** (4): the same circles through the north and south points, through
  the prime-vertical points at angles 0, 30, 60, 90, 120, 150, 180° from the east
  point toward the zenith for cusps 1, 12, 11, 10, 9, 8, 7 and the negatives for
  2 … 6.
- **Alcabitius** (7): with D = α(Ascendant) − ARMC (mod 360°), the Ascendant's
  diurnal semi-arc, the equator points ARMC + D/3, ARMC + 2D/3 give cusps 11,
  12, and α(Ascendant) + (180° − D)/3, + 2 (180° − D)/3 give cusps 2, 3, each
  carried to the ecliptic by `E`.
- **Koch** (1): with SA = 90° + asin(tan φ tan δ), δ the declination of the MC
  point, cusp 11 is the Ascendant for ARMC − 2 SA / 3, cusp 12 for ARMC − SA / 3,
  cusp 2 for ARMC + SA / 3, cusp 3 for ARMC + 2 SA / 3.
- **Topocentric** (10, Polich–Page): with φ₁ = atan(tan φ / 3), φ₂ = atan(2 tan φ /
  3), cusp 11 is the Ascendant at latitude φ₁ for ARMC − 60°, cusp 12 at φ₂ for
  ARMC − 30°, cusp 2 at φ₂ for ARMC + 30°, cusp 3 at φ₁ for ARMC + 60°. Cusp 10 is
  the MC. **No reordering is applied inside the polar circle**; the definition
  gives no rule for one.
- **Placidus** (0): a point of the ecliptic whose hour angle is a fixed
  fraction of its own semi-arc — cusp 11 and 12 at 1/3 and 2/3 of the diurnal
  semi-arc east of the meridian, cusps 2 and 3 at 1/3 and 2/3 of the nocturnal
  semi-arc measured from the Ascendant.

**Where a system is undefined.** Placidus and Koch are undefined for
**|φ| > 90° − ε**, where ε is the value the row reports, at **every** instant:
inside the polar circle part of the ecliptic never rises or sets and Koch's
shifted Ascendants can be setting points. The row fails with error 9 and the
server MUST NOT substitute another system. **A server MAY answer or refuse
within ±1″ of 90° − ε**; the fixture rows sit at ±0.001° outside that band. The
reference implementation answers at exactly 90° − ε, and Prometheia refuses only
strictly beyond it; that is behaviour, not normative. Every other system, and
every angle, answers at every latitude.

## Conformance (additions to §3.10)

- Fixture bytes for a REQUEST carrying kind 6 objects (one grid, one list; a
  system unserved; a point out of range), for the two WELCOME TLVs, and for a
  DATA answer with the ARMC and obliquity columns.
- **Numeric rows** for the geometry, made by `tools/houses_ref.py` from the
  definitions above and nothing else: all eleven systems and four angles at
  (ARMC, φ, ε) = 280.457° with φ = 40°, 47.6°, 70° and ε = 23.4393°, among
  others; the polar boundary either side; the sidereal Whole Sign case; the
  Regiomontanus/Campanus/Topocentric rows inside the polar circle. Graded at
  the row's reported ARMC and ε.
- The reference implementation itself is checked against the fork's Swiss
  (`tools/houses_ref.py --compare`): all eleven systems and four angles agree to
  2e-6″ (Placidus 3e-4″, its iteration) over 56 points at |φ| ≤ 60°. At
  |φ| ≥ 70° (30 points) every system but Topocentric agrees, and Topocentric
  differs from Swiss by exactly 180° on some cusps at some hours (Swiss reorders;
  see above). Swiss's own MC for R, C, T is the cusp 10 there, and differs from
  the culminating point.

## What a Swiss-backed server does

`swe_houses_armc_ex2_r` gives the cusps and angles for a given ARMC, latitude
and ε, so `astrolog-ephd` serves kind 6 from it, with three exceptions where
the definitions and Swiss part: the **MC** is computed by formula (Swiss reports
the cusp-10 point for R, C and T inside the polar circle); **Topocentric inside
the polar circle** is computed from the definition; and the refusal for Placidus
and Koch is decided by |φ| against the row's ε *before* the call, since Swiss
otherwise substitutes Porphyry and returns −1. ARMC comes from Swiss's sidereal
time and is reported as it is, with 0x0016 naming Swiss's model.

## Open, for the other project

- Their `houses::compute` maps onto this directly (they have said so). The
  fixtures' numeric rows are theirs to reproduce; a disagreement beyond the
  stated tolerances is a finding for one of the definitions above.
- **TIME/TIME_RESULT** follows houses, narrow (no basis enum required).
