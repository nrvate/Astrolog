#!/usr/bin/env python3
"""Fixed-star RATES, two engines: astrolog-ephd against Ephemeris Prometheia.

    tools/star-rates-xengine.py OURS_PORT THEIRS_PORT   # the comparison
    tools/star-rates-xengine.py --selftest              # the grading, no servers

This is the instrument registry 4.4a said did not exist: two engines compared
on star RATES. tools/ephsrv-rates.sh grades a server against its own
positions, which is tautological for a star since 2026-09-30 (STARS_BACKLOG.md
item 1: the rates ARE differences of the positions), so the independent check
is an engine that does not difference. Prometheia's star rate is analytic.

It asks both for the same twelve stars at five epochs in four profiles
(geocentric, topocentric Quito, J2000 equatorial, sidereal) through
eph_wsclient, and grades:

  * the angular rate, as the separation of the two (lon rate x cos lat, lat
    rate) in deg/day, against the advertised A.3 0x0013 bound, 3e-5. Measured
    2026-09-30: 2.4e-7 geocentric, 4.5e-6 topocentric (Polaris, 2100).
  * the distance rate, as |difference| / max(1 AU, r), against 1e-9 -- for
    every star whose two catalogues agree on its radial velocity. A distance
    rate is the radial velocity plus the observer's motion, so where the two
    engines' catalogue lines disagree the RATE disagrees by exactly that and
    it is data (registry 4.4). Those stars are REPORTED with the difference
    in km/s and never graded, and the list is below with the reason each is
    there: a star that leaves it because a catalogue was fixed is caught by
    the ratchet in the other direction (it must still differ, or the entry is
    stale).

By hand, like every gate that needs the other project's build.
"""
import math
import os
import subprocess
import sys

WS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "eph_wsclient")
STARS = ["Aldebaran", "Regulus", "Antares", "Vega", "Polaris", "Canopus",
         "Sirius", "Procyon", "Rigil Kentaurus", "Toliman", "Arcturus", "Spica"]
JDS = [2378496.5, 2415020.5, 2451545.0, 2461300.5, 2488069.5]
PROFILES = {
    "geo": "plane=ecl,form=sph,speeds=1",
    "topo": "plane=ecl,form=sph,speeds=1,obs=topo,site=-78.5:-0.2:2800",
    "equ-j2k": "plane=equ,frame=j2000,form=sph,speeds=1",
    "sid": "plane=ecl,form=sph,speeds=1,zodiac=lahiri",
}
BOUND_ANG = 3e-5     # deg/day, A.3 0x0013
BOUND_DIST = 1e-9    # AU/day per max(1 AU, r)
AU_PER_DAY_TO_KMS = 1.495978707e8 / 86400.0
# Stars whose radial velocity the two catalogues give differently (measured
# 2026-09-30 at J2000, geocentric; ours from sefstars.txt): the distance-rate
# difference in km/s is the catalogue difference.
CATALOGUE_RV = {
    "Vega": "-20.6 (sefstars, 2018 vintage) against -13.5 (SIMBAD): registry 4.4",
    "Regulus": "+5.9 against a value 5.2 km/s more negative on their side",
    "Spica": "+1 against a value 4.3 km/s more negative on their side",
    "Procyon": "-3.2 against a value 1.3 km/s more negative on their side",
    "Toliman": "alpha Cen B: ours is placed from A, so it shares A's distance and "
               "rate; theirs has its own parallax and RV (7.3 km/s apart)",
}


def ask(port, jd, profile, star, out):
    if os.path.exists(out):
        os.remove(out)
    subprocess.run([WS, "--host", "127.0.0.1", "--port", str(port), "--quiet",
                    "--jd", repr(jd), "--count", "1", "--profile", profile,
                    "--stars", star, "--out", out],
                   capture_output=True, text=True)
    if not os.path.exists(out):
        return None
    for line in open(out):
        f = line.split()
        if len(f) >= 11 and int(f[2]) == 0:
            return [float.fromhex(x) for x in f[5:11]]   # lon lat r dlon dlat dr
    return None


def grade(a, b):
    """(angular rate separation deg/day, distance-rate miss per max(1 AU, r),
    distance-rate difference km/s) for two rows lon lat r dlon dlat dr."""
    dl, db = b[3] - a[3], b[4] - a[4]
    ang = math.hypot(dl * math.cos(math.radians(a[1])), db)
    dr = abs(b[5] - a[5])
    return ang, dr / max(1.0, a[2]), (b[5] - a[5]) * AU_PER_DAY_TO_KMS


def run(theirs, ours):
    out = os.path.join(os.environ.get("TMPDIR", "/nvm/work"), "star-rates-%d.txt" % os.getpid())
    worst_ang, worst_dist, kms = {}, {}, {}
    n = 0
    for pn, prof in PROFILES.items():
        for jd in JDS:
            for st in STARS:
                a, b = ask(theirs, jd, prof, st, out), ask(ours, jd, prof, st, out)
                if a is None or b is None:
                    continue
                n += 1
                ang, dist, k = grade(a, b)
                if ang > worst_ang.get(pn, (0, ""))[0]:
                    worst_ang[pn] = (ang, "%s@%.1f" % (st, jd))
                if pn == "geo" and jd == 2451545.0:
                    kms[st] = k
                if st not in CATALOGUE_RV and dist > worst_dist.get(pn, (0, ""))[0]:
                    worst_dist[pn] = (dist, "%s@%.1f" % (st, jd))
    if os.path.exists(out):
        os.remove(out)
    return n, worst_ang, worst_dist, kms


def verdict(n, worst_ang, worst_dist, kms, quiet=False):
    bad = 0
    print = (lambda *a: None) if quiet else __builtins__.print
    print("%d star cells compared" % n)
    for pn in PROFILES:
        a = worst_ang.get(pn, (float("nan"), ""))
        d = worst_dist.get(pn, (float("nan"), ""))
        over = not (a[0] <= BOUND_ANG) or not (d[0] <= BOUND_DIST)
        bad += over
        print("  %-8s angular rate %.2e deg/d (%s); distance rate %.2e relative (%s)%s"
              % (pn, a[0], a[1], d[0], d[1], "   <-- OVER" if over else ""))
    print("distance rates that are catalogue radial velocities (reported, not graded):")
    for st in sorted(CATALOGUE_RV):
        k = kms.get(st)
        # The ratchet: an entry that no longer differs is stale.
        stale = k is not None and abs(k) < 0.05
        bad += stale
        print("  %-16s %+7.3f km/s   %s%s" % (st, k if k is not None else float("nan"),
              CATALOGUE_RV[st], "   <-- STALE ENTRY" if stale else ""))
    if n == 0:
        print("nothing was compared")
        bad += 1
    return bad


def selftest():
    """The grading only: a control, and one injection per leg."""
    row = [10.0, 5.0, 1e6, 1e-5, 2e-6, 1e-3]
    ang, dist, _ = grade(row, row)
    ok = ang == 0.0 and dist == 0.0
    print("  %s: identical rows grade 0" % ("ok" if ok else "FAIL"))
    bad = not ok
    inj = list(row); inj[3] += 1e-4                     # an angular rate 3x the bound
    ang, _, _ = grade(row, inj)
    ok = ang > BOUND_ANG
    print("  %s: a 1e-4 deg/d rate error exceeds the angular bound" % ("ok" if ok else "FAIL"))
    bad |= not ok
    inj = list(row); inj[5] += 1e-8 * row[2]            # 1e-8 relative
    _, dist, _ = grade(row, inj)
    ok = dist > BOUND_DIST
    print("  %s: a 1e-8 relative distance-rate error exceeds the bound" % ("ok" if ok else "FAIL"))
    bad |= not ok
    inj = list(row); inj[4] += 1e-4                     # latitude channel too
    ang, _, _ = grade(row, inj)
    ok = ang > BOUND_ANG
    print("  %s: the latitude rate is graded, not only the longitude rate" % ("ok" if ok else "FAIL"))
    bad |= not ok
    bad |= verdict(1, {p: (0.0, "") for p in PROFILES}, {p: (0.0, "") for p in PROFILES},
                   {s: 3.0 for s in CATALOGUE_RV}, True) != 0
    ok = verdict(1, {p: (0.0, "") for p in PROFILES}, {p: (0.0, "") for p in PROFILES},
                 {s: 0.0 for s in CATALOGUE_RV}, True) > 0
    print("  %s: a catalogue entry that no longer differs is flagged stale" % ("ok" if ok else "FAIL"))
    bad |= not ok
    return bad


if __name__ == "__main__":
    if len(sys.argv) == 2 and sys.argv[1] == "--selftest":
        rc = selftest()
        print("STAR-RATES SELFTEST %s" % ("FAIL" if rc else "PASS"))
        sys.exit(1 if rc else 0)
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    rc = verdict(*run(int(sys.argv[2]), int(sys.argv[1])))
    print("STAR-RATES %s" % ("FAIL" if rc else "PASS"))
    sys.exit(1 if rc else 0)
