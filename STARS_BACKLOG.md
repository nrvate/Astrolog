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

1. **Our fixed-star rates are Swiss's, and §3.5a says they must be the
   derivative of our own positions.** Registry §2.11a (a topocentric star's
   distance rate misses by up to 8.2e-3 AU/day in scattered instant, ΔT and
   site cells), §2.12 (0.6–3.4% on every star, geocentric) and §4.4a are the
   same fault three ways. The reported column is Swiss's rate, bit for bit in
   `libswe.a` with no server.
   - **The fix, designed but not started:** difference a star's rates from
     its own answered positions with `EphNodRateDiff()` (`ephsrv/ephnodrate.h`,
     h = 1/1024), as mean nodes already are. The callback repeats the row's own
     call at an offset: `swe_fixstar2` or `_ut` as the row used, the same
     flags, then `EphStarOrbApply` for the four binary-orbit stars, all before
     the plane rotation.
   - **Both halves or neither:** in the server's `kCallFixstar` branch
     (`eph_srv.cpp`) and in `FSwissStar()` (`calc.cpp`), which
     `SwissComputeStars()` reaches through the registry. Otherwise the
     suite's server-versus-local legs go red, as they did for nodes.
   - **Then:** golden's star comparisons get the oracle the spec defines, as
     nodes did, and the star rows join the 0x0013 measurement.
   - Prometheia's view: h = 1/32 would also be safe for stars (their
     truncation estimate is 1.3e-8 AU/day radially), but 1/1024 reuses the
     helper unchanged, and under the relative bound the floor, about 2e-13 per
     AU, doesn't matter.
2. **Advertise the star part of 0x0013** once item 1 lands and the sentence is
   approved on both sides. Until then our advertised 4e-3 AU/day does not
   cover topocentric stars (§2.11a), which is the fact a client would need.
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
   - `tools/swetest-oracle.sh` has no fixed-star legs.
   - The local `./astrolog` star path is not refereed by FK5; only
     `astrolog-ephd` is.
   - There is no two-engine RATES instrument for stars (registry §4.4a).
9. **A vacuous check** (review ledger T1): the suite's host-path star check
   asserts a separation under 0.001″ using `SphDistance()`, whose acos floor
   is about 0.003″, so it cannot fail. Fix: an `atan2(|a×b|, a·b)` separation
   for sub-0.01″ assertions. `SphDistance()`'s other users assert at 0.2″ and
   up and are not blind.
10. **Performance, deferred:** a fixed star redoes its per-position work on
    every row; the side-call path's star brightness pass was never moved onto
    the registry.
11. **Kind-4 Uranians are not stars,** but came up alongside them. Prometheia
    serves orbital elements (`prometheia_calc_elements`); routing the
    Uranians there with the user's own `seorbel.txt` elements is a follow-up
    decision for the maintainer. Not parked here, just noted.

## Where the evidence lives

- `EPHEMERIS_ACCURACY_REGISTRY.md`: §1.4, §2.4, §2.5, §2.10, §2.11a, §2.12,
  §4.4, §4.4a.
- `CLIENT_SERVER_REVIEW.md`: T1, and P3 (Prometheia's analytic star rate,
  confirmed from this side).
- `EPHEMERIS_PLUGINS_PLAN.md` work log items 30–36.
