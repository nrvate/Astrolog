#!/usr/bin/env python3
"""The house systems of EPHEMERIS_PLUGINS_PLAN.md 3.5b, as code.

A reference implementation written from the DEFINITIONS in that section and
from nothing else: no table, no other engine's source. Given ARMC, latitude
and true obliquity (all degrees) it returns the twelve cusps and the angles
of one system, or None where the system is undefined. The conformance
fixtures' numbers are made by this file (tools/ephproto4-fixtures.py), and
`tools/houses_ref.py --compare` grades it against the fork's Swiss over a
grid, which is how the definitions were checked before they were written down.

    python3 tools/houses_ref.py --compare          # against the fork's Swiss
    python3 tools/houses_ref.py --selftest         # the arithmetic, no Swiss
    python3 tools/houses_ref.py --write-rows       # ephsrv/houses-rows.tsv
    python3 tools/houses_ref.py --check-rows       # it is what the code makes
    python3 tools/houses_ref.py ARMC LAT EPS       # every system, one line each

The numbering is registry A.22: placidus 0, koch 1, porphyry 2, regiomontanus
3, campanus 4, equal 5, whole-sign 6, alcabitius 7, morinus 8, meridian 9,
topocentric 10.
"""
import math
import os
import subprocess
import sys

D = math.pi / 180.0
SYSTEMS = ["placidus", "koch", "porphyry", "regiomontanus", "campanus", "equal",
           "whole-sign", "alcabitius", "morinus", "meridian", "topocentric"]
# Swiss's letters, for --compare only.
LETTER = {"placidus": "P", "koch": "K", "porphyry": "O", "regiomontanus": "R",
          "campanus": "C", "equal": "A", "whole-sign": "W", "alcabitius": "B",
          "morinus": "M", "meridian": "X", "topocentric": "T"}


def n360(x):
    return x % 360.0


# ---- the points -----------------------------------------------------------

def mc_point(armc, eps):
    """The ecliptic point of right ascension armc: on the hour circle."""
    a, e = armc * D, eps * D
    return n360(math.atan2(math.sin(a), math.cos(a) * math.cos(e)) / D)


def morinus_point(alpha, eps):
    """The ecliptic point of longitude equal to the equator point's."""
    a, e = alpha * D, eps * D
    return n360(math.atan2(math.sin(a) * math.cos(e), math.cos(a)) / D)


def asc_point(theta, phi, eps):
    """The intersection of the ecliptic with the horizon that lies EAST of the
    meridian (negative hour angle), for sidereal angle theta at latitude phi.
    The closed form atan2(cos t, -(sin t cos e + tan p sin e)) gives the same
    point everywhere outside the polar circle; inside it that expression
    returns the western intersection, which is why this is defined by the
    side and not by the formula."""
    zenith = (math.cos(phi * D), 0.0, math.sin(phi * D))
    pole = _vec(theta - 270.0, 90.0 - eps)
    d = _unit(_cross(zenith, pole))
    if _dot(d, (0.0, -1.0, 0.0)) < 0:
        d = (-d[0], -d[1], -d[2])
    return _to_ecliptic(d, theta, eps)


def equatorial(lam, eps):
    """(right ascension, declination) of the ecliptic point lam."""
    l, e = lam * D, eps * D
    return (n360(math.atan2(math.sin(l) * math.cos(e), math.cos(l)) / D),
            math.asin(math.sin(e) * math.sin(l)) / D)


def _vec(h, dec):
    h, dec = h * D, dec * D
    return (math.cos(dec) * math.cos(h), math.cos(dec) * math.sin(h), math.sin(dec))


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _unit(a):
    n = math.sqrt(_dot(a, a))
    return (a[0] / n, a[1] / n, a[2] / n)


def _to_ecliptic(d, theta, eps):
    """A direction in the local frame (x on the meridian at the equator, y
    toward hour angle +90, z the celestial pole) as an ecliptic longitude."""
    h = math.atan2(d[1], d[0]) / D
    dec = math.asin(d[2]) / D
    a, de, e = (theta - h) * D, dec * D, eps * D
    return n360(math.atan2(math.sin(a) * math.cos(e) + math.tan(de) * math.sin(e),
                           math.cos(a)) / D)


def _circle_point(through, theta, phi, eps):
    """The ecliptic point on the great circle through the horizon's north and
    south points and the direction `through`, on the half of that circle
    that holds `through`."""
    north = (-math.sin(phi * D), 0.0, math.cos(phi * D))
    plane = _unit(_cross(north, through))
    pole = _vec(theta - 270.0, 90.0 - eps)
    d = _unit(_cross(plane, pole))
    q = tuple(through[i] - _dot(through, north) * north[i] for i in range(3))
    if _dot(d, q) < 0:
        d = (-d[0], -d[1], -d[2])
    return _to_ecliptic(d, theta, eps)


def vertex_point(theta, phi, eps):
    """The intersection of the ecliptic with the prime vertical, on the west
    side: the one whose hour angle is positive."""
    north = (-math.sin(phi * D), 0.0, math.cos(phi * D))
    pole = _vec(theta - 270.0, 90.0 - eps)
    d = _unit(_cross(north, pole))
    if _dot(d, (0.0, -1.0, 0.0)) > 0:      # (0,-1,0) is the east point
        d = (-d[0], -d[1], -d[2])
    return _to_ecliptic(d, theta, eps)


def angles(armc, lat, eps):
    return {"asc": asc_point(armc, lat, eps), "mc": mc_point(armc, eps),
            "vertex": vertex_point(armc, lat, eps),
            "equasc": mc_point(armc + 90.0, eps)}


def defined(system, lat, eps):
    """Placidus and Koch are undefined where part of the ecliptic never rises
    or sets: |lat| > 90 - eps. At exactly 90 - eps this reference answers; a
    conforming server may do either within 1 arcsecond of it (3.5b)."""
    if system in ("placidus", "koch"):
        return abs(lat) <= 90.0 - eps
    return True


# ---- the systems ----------------------------------------------------------

def _hour_angle(lam, theta, eps):
    ra, _ = equatorial(lam, eps)
    return ((theta - ra + 180.0) % 360.0) - 180.0


def _semi_arc(lam, phi, eps):
    _, dec = equatorial(lam, eps)
    x = -math.tan(phi * D) * math.tan(dec * D)
    return math.acos(max(-1.0, min(1.0, x))) / D


def _placidus_cusp(theta, phi, eps, lo, hi, frac, nocturnal):
    def g(lam):
        sa = _semi_arc(lam % 360.0, phi, eps)
        target = -(sa + frac * (180.0 - sa)) if nocturnal else -frac * sa
        return ((_hour_angle(lam % 360.0, theta, eps) - target + 180.0) % 360.0) - 180.0
    a, b = lo, hi
    ga = g(a)
    for _ in range(200):
        m = (a + b) / 2.0
        gm = g(m)
        if (ga <= 0) == (gm <= 0):
            a, ga = m, gm
        else:
            b = m
    return ((a + b) / 2.0) % 360.0


def cusps(system, armc, lat, eps, ayanamsa=0.0):
    """The twelve cusps of one system as a list [cusp1 .. cusp12], or None
    where the system is undefined. In a sidereal zodiac every point is the
    tropical value less the ayanamsa, and Whole Sign starts at the sign of the
    sidereal Ascendant (3.5, 3.5b)."""
    if abs(lat) >= 90.0 or not defined(system, lat, eps):
        return None
    if ayanamsa != 0.0:
        if system == "whole-sign":
            sa = n360(asc_point(armc, lat, eps) - ayanamsa)
            return [n360(30.0 * math.floor(sa / 30.0) + 30.0 * k) for k in range(12)]
        return [n360(c - ayanamsa) for c in cusps(system, armc, lat, eps)]
    return _cusps_tropical(system, armc, lat, eps)


def _cusps_tropical(system, armc, lat, eps):
    asc = asc_point(armc, lat, eps)
    mc = mc_point(armc, eps)
    out = [None] * 12
    if system == "equal":
        return [n360(asc + 30.0 * k) for k in range(12)]
    if system == "whole-sign":
        return [n360(30.0 * math.floor(asc / 30.0) + 30.0 * k) for k in range(12)]
    if system == "meridian":
        return [mc_point(armc + 30.0 * (k - 10), eps) for k in range(1, 13)]
    if system == "morinus":
        return [morinus_point(armc + 30.0 * (k - 10), eps) for k in range(1, 13)]
    if system == "regiomontanus":
        return [_circle_point(_vec(-30.0 * (k - 10), 0.0), armc, lat, eps) for k in range(1, 13)]
    if system == "campanus":
        east, zenith = (0.0, -1.0, 0.0), (math.cos(lat * D), 0.0, math.sin(lat * D))
        beta = {1: 0, 12: 30, 11: 60, 10: 90, 9: 120, 8: 150, 7: 180,
                6: -150, 5: -120, 4: -90, 3: -60, 2: -30}
        res = []
        for k in range(1, 13):
            b = beta[k] * D
            p = tuple(math.cos(b) * east[i] + math.sin(b) * zenith[i] for i in range(3))
            res.append(_circle_point(p, armc, lat, eps))
        return res
    if system == "porphyry":
        arc1 = (asc - mc) % 360.0
        ic = n360(mc + 180.0)
        arc2 = (ic - asc) % 360.0
        out[0], out[1], out[2], out[3] = (asc, n360(asc + arc2 / 3.0),
                                          n360(asc + 2.0 * arc2 / 3.0), ic)
        out[9], out[10], out[11] = mc, n360(mc + arc1 / 3.0), n360(mc + 2.0 * arc1 / 3.0)
    elif system == "alcabitius":
        ra_asc, _ = equatorial(asc, eps)
        dd = (ra_asc - armc) % 360.0
        out[0], out[9] = asc, mc
        out[10] = mc_point(armc + dd / 3.0, eps)
        out[11] = mc_point(armc + 2.0 * dd / 3.0, eps)
        out[1] = mc_point(ra_asc + (180.0 - dd) / 3.0, eps)
        out[2] = mc_point(ra_asc + 2.0 * (180.0 - dd) / 3.0, eps)
        out[3] = n360(mc + 180.0)
    elif system == "koch":
        _, dec = equatorial(mc, eps)
        sa = 90.0 + math.asin(math.tan(lat * D) * math.tan(dec * D)) / D
        out[0], out[9], out[3] = asc, mc, n360(mc + 180.0)
        out[10] = asc_point(armc - 2.0 * sa / 3.0, lat, eps)
        out[11] = asc_point(armc - sa / 3.0, lat, eps)
        out[1] = asc_point(armc + sa / 3.0, lat, eps)
        out[2] = asc_point(armc + 2.0 * sa / 3.0, lat, eps)
    elif system == "topocentric":
        t = math.tan(lat * D)
        p1, p2 = math.atan(t / 3.0) / D, math.atan(2.0 * t / 3.0) / D
        out[0], out[9], out[3] = asc, mc, n360(mc + 180.0)
        out[10] = asc_point(armc - 60.0, p1, eps)
        out[11] = asc_point(armc - 30.0, p2, eps)
        out[1] = asc_point(armc + 30.0, p2, eps)
        out[2] = asc_point(armc + 60.0, p1, eps)
    elif system == "placidus":
        ic = n360(mc + 180.0)
        lo1, hi1 = mc, mc + ((asc - mc) % 360.0)
        lo2, hi2 = asc, asc + ((ic - asc) % 360.0)
        out[0], out[9], out[3] = asc, mc, ic
        out[10] = _placidus_cusp(armc, lat, eps, lo1, hi1, 1.0 / 3.0, False)
        out[11] = _placidus_cusp(armc, lat, eps, lo1, hi1, 2.0 / 3.0, False)
        out[1] = _placidus_cusp(armc, lat, eps, lo2, hi2, 1.0 / 3.0, True)
        out[2] = _placidus_cusp(armc, lat, eps, lo2, hi2, 2.0 / 3.0, True)
    else:
        raise ValueError(system)
    # Cusps 5..9 are the opposites of 11, 12, 1, 2, 3 (and 4, 7, 10 are set).
    for k in range(12):
        if out[k] is None:
            out[k] = n360(out[(k + 6) % 12] + 180.0)
    return out


# ---- checks ---------------------------------------------------------------

def sep(a, b):
    return abs(((a - b + 180.0) % 360.0) - 180.0) * 3600.0


def _swiss_dumper():
    here = os.path.dirname(os.path.abspath(__file__))
    src = os.path.join(here, "houses_swiss.c")
    exe = os.path.join(os.environ.get("TMPDIR", "/nvm/work"), "houses_swiss")
    swe = "/shares/swisseph"
    if not os.path.exists(src) or not os.path.exists(swe + "/libswe.a"):
        return None
    if not os.path.exists(exe) or os.path.getmtime(exe) < os.path.getmtime(src):
        subprocess.run(["gcc", "-O1", "-I" + swe, src, swe + "/libswe.a", "-lm", "-ldl",
                        "-lpthread", "-o", exe], check=True, capture_output=True)
    return exe


def compare():
    exe = _swiss_dumper()
    if exe is None:
        print("houses_ref: no fork checkout (/shares/swisseph); skipping")
        return 0
    eps = 23.4393
    worst = {}
    for armc in (0.0, 13.7, 77.1, 143.3, 200.0, 280.457, 333.3):
        for lat in (-60.0, -30.0, -5.0, 0.5, 17.3, 40.0, 47.6, 60.0):
            out = subprocess.run([exe, repr(armc), repr(lat), repr(eps)],
                                 capture_output=True, text=True).stdout.splitlines()
            sw = {}
            for line in out:
                left, right = line.split("|")
                f = left.split()
                sw[f[0]] = ([float(x) for x in f[2:]], [float(x) for x in right.split()])
            for name in SYSTEMS:
                mine = cusps(name, armc, lat, eps)
                theirs = sw[LETTER[name]][0]
                for k in range(12):
                    d = sep(mine[k], theirs[k])
                    if d > worst.get(name, (0.0,))[0]:
                        worst[name] = (d, (armc, lat, k + 1))
            ang = angles(armc, lat, eps)
            for j, name in enumerate(("asc", "mc", "vertex", "equasc")):
                a = sw["P"][1][(0, 1, 2, 3)[j]]
                d = sep(ang[name], a)
                if d > worst.get(name, (0.0,))[0]:
                    worst[name] = (d, (armc, lat))
    bad = 0
    for name, (d, ctx) in sorted(worst.items()):
        tol = 1e-3 if name == "placidus" else 1e-5
        flag = "" if d < tol else "   <-- OVER"
        bad += d >= tol
        print("  %-14s worst %.6f arcsec at %s%s" % (name, d, ctx, flag))
    print("HOUSES-REF %s" % ("FAIL" if bad else "PASS"))
    return 1 if bad else 0


ROWS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ephsrv", "houses-rows.tsv")
ROW_EPS = 23.4393
ROW_ARMC = (0.0, 77.1, 143.3, 200.0, 280.457, 333.3)
ROW_LAT = (-89.999, -80.0, -66.5617, -66.5597, -60.0, -30.0, 0.5, 17.3, 40.0, 47.6,
           60.0, 66.5597, 66.5607, 66.5617, 70.0, 89.999, 90.0)
ROW_AYAN = (0.0, 24.0)


def rows_text():
    """The numeric conformance rows: every system and angle over a grid, made
    from the definitions and nothing else. A cell is degrees to 7 places, or
    `refused` (error 9), or `either` for Placidus and Koch within 1 arcsecond
    of the polar boundary, where a server may do either (3.5b)."""
    out = ["# ephsrv/houses-rows.tsv -- GENERATED by tools/houses_ref.py --rows;",
           "# do not edit. Numeric conformance rows for 3.5b, made from the",
           "# definitions alone. Columns: system, armc, lat, eps, ayanamsa, then",
           "# cusp1..cusp12, Asc, MC, Vertex, EquAsc. Graded at the row's own",
           "# armc and eps (3.5b); Placidus to 0.01 arcsec, the rest to 0.0001.",
           "# A cell is degrees, `refused` (error 9), or `either` (within 1\"",
           "# of the polar boundary, either answer conforms)."]
    body = []
    for system in SYSTEMS:
        for armc in ROW_ARMC:
            for lat in ROW_LAT:
                for ayan in ROW_AYAN:
                    band = abs(abs(lat) - (90.0 - ROW_EPS)) * 3600.0 <= 1.0
                    if band and system in ("placidus", "koch"):
                        cells = ["either"] * 12
                    else:
                        c = cusps(system, armc, lat, ROW_EPS, ayan)
                        cells = ["refused"] * 12 if c is None else ["%.7f" % x for x in c]
                    if abs(lat) >= 90.0:
                        angs = ["refused"] * 4
                    else:
                        a = angles(armc, lat, ROW_EPS)
                        angs = ["%.7f" % n360(a[k] - ayan) for k in ("asc", "mc", "vertex", "equasc")]
                    body.append("\t".join([system, "%.3f" % armc, "%.4f" % lat, "%.4f" % ROW_EPS,
                                           "%.1f" % ayan] + cells + angs))
    import hashlib
    digest = hashlib.sha256(("\n".join(body) + "\n").encode()).hexdigest()
    out.append("# rows-sha256 " + digest)
    return "\n".join(out + body) + "\n"


def selftest():
    bad = 0
    # A control with a known answer: at the equator with eps = 0 every system
    # divides the circle evenly from the Ascendant.
    c = cusps("equal", 90.0, 0.0, 0.0)
    ok = all(sep(c[k], (c[0] + 30.0 * k)) < 1e-9 for k in range(12))
    print("  %s: equal houses are 30 degrees apart" % ("ok" if ok else "FAIL"))
    bad |= not ok
    c = cusps("meridian", 123.0, 40.0, 23.44)
    ok = sep(c[9], mc_point(123.0, 23.44)) < 1e-9
    print("  %s: meridian cusp 10 is the culminating point" % ("ok" if ok else "FAIL"))
    bad |= not ok
    ok = cusps("placidus", 10.0, 80.0, 23.44) is None and cusps("koch", 10.0, 80.0, 23.44) is None
    print("  %s: placidus and koch refuse inside the polar circle" % ("ok" if ok else "FAIL"))
    bad |= not ok
    ok = cusps("porphyry", 10.0, 80.0, 23.44) is not None and cusps("regiomontanus", 10.0, 80.0, 23.44) is not None
    print("  %s: the other systems answer there" % ("ok" if ok else "FAIL"))
    bad |= not ok
    # Inside the polar circle Regiomontanus cusp 10 is the point of the
    # meridian circle above the horizon, which is the IC when the MC is below.
    c = cusps("regiomontanus", 280.457, 70.0, 23.4393)
    ok = sep(c[9], 99.611) < 3.6
    print("  %s: Regiomontanus cusp 10 at 70N is the point above the horizon" % ("ok" if ok else "FAIL"))
    bad |= not ok
    a = angles(280.457, 70.0, 23.4393)
    ok = sep(a["mc"], 279.611) < 3.6
    print("  %s: the MC is the culminating point even when it is below the horizon" % ("ok" if ok else "FAIL"))
    bad |= not ok
    return bad


if __name__ == "__main__":
    if len(sys.argv) == 2 and sys.argv[1] == "--compare":
        sys.exit(compare())
    if len(sys.argv) == 2 and sys.argv[1] == "--rows":
        sys.stdout.write(rows_text())
        sys.exit(0)
    if len(sys.argv) == 2 and sys.argv[1] == "--write-rows":
        with open(ROWS, "w", newline="\n") as f:
            f.write(rows_text())
        sys.exit(0)
    if len(sys.argv) == 2 and sys.argv[1] == "--check-rows":
        with open(ROWS, newline="\n") as f:
            sys.exit(0 if f.read() == rows_text() else 1)
    if len(sys.argv) == 2 and sys.argv[1] == "--selftest":
        rc = selftest()
        print("HOUSES-REF SELFTEST %s" % ("FAIL" if rc else "PASS"))
        sys.exit(rc)
    if len(sys.argv) == 4:
        armc, lat, eps = (float(x) for x in sys.argv[1:4])
        for name in SYSTEMS:
            c = cusps(name, armc, lat, eps)
            print("%-14s" % name, "undefined" if c is None else " ".join("%.4f" % x for x in c))
        print("angles", {k: round(v, 4) for k, v in angles(armc, lat, eps).items()})
        sys.exit(0)
    sys.exit(__doc__)
