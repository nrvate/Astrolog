# Fixed stars — the back burner

**Parked 2026-09-29 by the maintainer:** "we barely use Stars." Work on
fixed stars stops here and effort goes to solar-system bodies. Nothing below
is lost; each item says where its evidence lives and what finishing it would
take, so it can be picked up cold.

**What is NOT parked:** anything that serves every body and only happened to
be found on a star. The 0x0013 rate-check sentence (a five-point difference
at h = 1/1024 day; the distance bound on |error| / max(1 AU, r)) is agreed
with Prometheia for every object and stays in flight.

## Work to do

1. **DONE 2026-09-30: a star's rates are the derivative of its own
   positions.** `EphNodRateDiff()` (h = 1/1024) differences the row's own call
   at four offsets -- `swe_fixstar2` or `_ut` as the row used, the same flags,
   `EphStarOrbApply` for the four binary-orbit stars, all before the plane
   rotation -- in the server's `kCallFixstar` branch (`FSrvStarRatePoint`) and
   in `FSwissStar()` (`FStarRatePointLocal`), so the two are bit-identical
   and `ratesApprox` is no longer set on a star. Nets, each shown to fail
   without its half: the live-parity P1 leg (Sirius, an orbit star, and
   Aldebaran; tropical and sidereal; positions and all three rates, server
   against local), golden's star oracle (differenced, 162/162 bit-exact), and
   the rates gate (stars exact). CLIENT_SERVER_REVIEW.md C1i.
   **It found a second defect on the way:** `FSwissStar()` handed its UT day
   to `swe_fixstar2()`, which wants TT, so the application placed every star
   delta T early (57 s in 1990; 1.4e-4" on Sirius). The server converts and
   was right. Fixed with `RJdeSwiss()`, shared with `FSwissPlanet()`.
2. **DONE 2026-09-30: re-measured, and the star part of 0x0013 is met.** The
   §2.11a scattered cells are gone from the served column (topocentric
   angular rate worst 4.5e-6 deg/day against Prometheia's analytic rates,
   1.7e-5 before). The advertised bound stays 3e-5 deg/day and 1e-9 on
   purpose -- an f32 row's rounding is 8e-7 deg/day on the Moon (comment in
   `eph_srv.cpp`). The two-engine instrument now exists:
   `tools/star-rates-xengine.py` (by hand; `--selftest` runs in
   `ci-selftest.sh`).
3. **Registry §2.10**, a measurement trap: a star's distance rate is unstable
   within about five days of an ephemeris file's start. Nothing to fix; any
   future star sweep must avoid file edges.
4. **Registry §4.4, Vega's radial velocity:** ours is −20.6 km/s from
   `sefstars.txt`, Prometheia's −13.5 km/s from current SIMBAD. Editing the
   shipped `sefstars.txt` is the maintainer's call. Prometheia keeps −13.5 and
   records −20.6 as the 2018 value.
5. **Registry §2.4, α Cen's mass ratio:** 185 mas at 1900 on α Cen A between
   Akeson 2021's own masses (ours) and Pourbaix & Boffin 2016 (theirs).
   `tools/star-orbit-check.sh` prints it without grading it. Needs the two
   projects to agree on one paper.
6. **Registry §1.4, Toliman and Proxima,** rest on one catalogue (IAU WGSN
   names, SIMBAD astrometry). FK5 has α Cen A only, so no second reference
   exists here yet.
7. **Registry §2.5:** a star-anchored zodiac's zero point wobbles about 40″ a
   year, because the anchor is taken apparent. Its missing rate was fixed
   2026-09-18; the wobble itself is Swiss's convention and still followed.
8. **Instruments that do not exist:**
   - ~~`tools/swetest-oracle.sh` has no fixed-star legs~~ -- it has, since
     2026-09-30: eight stars x five dates against upstream `swetest` at the
     0.001 degree display precision (the four orbit stars are left out on
     purpose); sabotaged with `-true` on the swetest side it fails at 0.023
     degrees. **That resolution cannot see the delta-T slip the local star
     path had (1e-4 arcsec)**, which lived in exactly this gap; it sees a
     frame, epoch or zodiac handled wrongly. The slip is held by the P1 leg.
   - The local `./astrolog` star path is not refereed by FK5; only
     `astrolog-ephd` is.
   - ~~No two-engine RATES instrument for stars~~ -- built 2026-09-30,
     `tools/star-rates-xengine.py` (registry §4.4a).
9. **DONE 2026-09-30: the vacuous star check** (review ledger T1). The P1 leg
   asserts a separation through `RSepArcsecQt()` (haversine) at 1e-6" against
   the same engine; with the delta-T fix removed it fails at 1.4e-4", which
   the old check printed as `0.0000"` and passed. `SphDistance()`'s other
   users assert at 0.2" and up and are not blind.
10. **Performance, deferred:** a fixed star redoes its per-position work on
    every row; the side-call path's star brightness pass was never moved onto
    the registry.
11. **Alpha Centauri's parallax and four radial velocities differ between the
    engines** (registry §4.4b, 2026-09-30): Prometheia serves Rigil Kentaurus
    at 754.9 mas and Toliman at 797.1 mas (Gaia DR3 single-star values for a
    bright binary), ours 742.12 for both (Hipparcos 2007), and the orbit-derived
    system value is about 747.2. Regulus, Spica, Procyon and Toliman radial
    velocities differ by 1.3-7.3 km/s. Data, and the maintainer's call as for
    Vega (item 4); nothing was re-queried.
12. **Kind-4 Uranians are not stars,** but came up alongside them. Prometheia
    serves orbital elements (`prometheia_calc_elements`); routing the
    Uranians there with the user's own `seorbel.txt` elements is a follow-up
    decision for the maintainer. Not parked here, just noted.

## Where the evidence lives

- `EPHEMERIS_ACCURACY_REGISTRY.md`: §1.4, §2.4, §2.5, §2.10, §2.11a, §2.12,
  §4.4, §4.4a.
- `CLIENT_SERVER_REVIEW.md`: T1, and P3 (Prometheia's analytic star rate,
  confirmed from this side).
- `EPHEMERIS_PLUGINS_PLAN.md` work log items 30–36.
