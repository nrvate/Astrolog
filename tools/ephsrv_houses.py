#!/usr/bin/env python3
"""The grading and the driver behind tools/ephsrv-houses.sh.

The server's house points (object kind 6) against tools/houses_ref.py, the
reference implementation of EPHEMERIS_PLUGINS_PLAN.md 3.5b. Every row is graded
AT ITS OWN REPORTED ARMC AND OBLIQUITY: the reference is handed the two
numbers the row carries, so a sidereal-time model that differs by an
arcsecond is not this gate's business.

    ephsrv_houses.py PORT      # the gate, against a running server
    ephsrv_houses.py --selftest
"""
import math
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import houses_ref as H

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
ANGLE = {13: "asc", 14: "mc", 15: "vertex", 16: "equasc"}
SIDEREAL_RATE = 360.98564736629      # degrees of ARMC per day, for the stencil


def sep(a, b):
    return abs(((a - b + 180.0) % 360.0) - 180.0) * 3600.0


def grade_row(system, point, lat, ayan, lon, armc, eps, refused):
    """Complaints about one row. `refused` is whether the server said error 9;
    lon/armc/eps are then meaningless. 3.5b: Placidus and Koch are undefined
    beyond 90 - eps (either answer within +-1 arcsecond of it); every point is
    undefined at a pole; nothing else is ever refused."""
    name = H.SYSTEMS[system]
    if abs(lat) >= 90.0:
        return [] if refused else ["%s point %d answered at a pole" % (name, point)]
    # only a CUSP of Placidus or Koch is undefined there: the four angles are
    # the same in every system and answer wherever they are defined
    pk = name in ("placidus", "koch") and point <= 12
    beyond = pk and abs(lat) > 90.0 - eps
    band = pk and abs(abs(lat) - (90.0 - eps)) * 3600.0 <= 1.0
    if refused:
        if beyond or band:
            return []
        return ["%s point %d refused at lat %.4f" % (name, point, lat)]
    if beyond and not band:
        return ["%s point %d answered beyond the polar boundary (lat %.4f, eps %.4f)"
                % (name, point, lat, eps)]
    if point <= 12:
        ref = H.cusps(name, armc, lat, eps, ayan)
        if ref is None:
            return []
        ref = ref[point - 1]
    else:
        ref = H.n360(H.angles(armc, lat, eps)[ANGLE[point]] - ayan)
    d = sep(lon, ref)
    if d > (1e-2 if name == "placidus" else 1e-4):
        return ["%s point %d: %.6f arcsec off at lat %.4f (server %.7f, reference %.7f)"
                % (name, point, d, lat, lon, ref)]
    return []


def reference_rate(system, point, lat, armc, eps):
    """3.5's rate for a house point: the five-point stencil of the answered
    longitude at h = 1/1024 day, 0 for Whole Sign, None across a jump."""
    name = H.SYSTEMS[system]
    if name == "whole-sign" and point <= 12:
        return 0.0                  # its CUSPS are steps; its angles are not
    hh = 1.0 / 1024.0

    def val(dj):
        a = armc + SIDEREAL_RATE * dj
        if point <= 12:
            c = H.cusps(name, a, lat, eps)
            return None if c is None else c[point - 1]
        return H.angles(a, lat, eps)[ANGLE[point]]
    v = [val(d) for d in (-2 * hh, -hh, hh, 2 * hh)]
    if any(x is None for x in v):
        return None
    for k in range(1, 4):
        while v[k] - v[k - 1] > 180.0:
            v[k] -= 360.0
        while v[k] - v[k - 1] < -180.0:
            v[k] += 360.0
        if abs(v[k] - v[k - 1]) > 90.0:
            return None
    return (v[0] - 8 * v[1] + 8 * v[2] - v[3]) / (12 * hh)


def selftest():
    bad = 0

    def case(label, want_bad, complaints):
        nonlocal bad
        ok = bool(complaints) == want_bad
        print("  %s: %s" % ("ok" if ok else "FAIL", label))
        bad += not ok
    k = H.cusps("koch", 280.457, 47.6, 23.4393)
    case("control: a correct Koch cusp passes", False,
         grade_row(1, 11, 47.6, 0.0, k[10], 280.457, 23.4393, False))
    case("a cusp 1 arcsec off is caught", True,
         grade_row(1, 11, 47.6, 0.0, k[10] + 1.0 / 3600.0, 280.457, 23.4393, False))
    case("a cusp graded at another ARMC is caught", True,
         grade_row(1, 11, 47.6, 0.0, k[10], 280.457 + 0.001, 23.4393, False))
    case("Koch answered beyond 90 - eps is caught", True,
         grade_row(1, 11, 70.0, 0.0, 10.0, 280.457, 23.4393, False))
    case("a Koch ANGLE answered beyond 90 - eps passes", False,
         grade_row(1, 13, 70.0, 0.0, H.angles(280.457, 70.0, 23.4393)["asc"], 280.457, 23.4393, False))
    case("a Koch angle refused beyond 90 - eps is caught", True,
         grade_row(1, 13, 70.0, 0.0, 0.0, 280.457, 23.4393, True))
    case("Koch refused inside the circle is caught", True,
         grade_row(1, 11, 47.6, 0.0, 0.0, 280.457, 23.4393, True))
    case("Koch refused beyond the circle passes", False,
         grade_row(1, 11, 70.0, 0.0, 0.0, 280.457, 23.4393, True))
    case("Koch either way inside the band passes", False,
         grade_row(1, 11, 66.5607, 0.0, 0.0, 280.457, 23.4393, True) +
         grade_row(1, 11, 66.5607, 0.0, H.cusps("koch", 280.457, 66.5607, 23.4393)[10],
                   280.457, 23.4393, False))
    case("Equal refused at 70 N is caught", True,
         grade_row(5, 1, 70.0, 0.0, 0.0, 280.457, 23.4393, True))
    case("an answer at a pole is caught", True,
         grade_row(5, 1, 90.0, 0.0, 10.0, 280.457, 23.4393, False))
    e24 = H.cusps("equal", 280.457, 47.6, 23.4393, 24.0)[2]
    case("a sidereal cusp is graded with its ayanamsa", False,
         grade_row(5, 3, 47.6, 24.0, e24, 280.457, 23.4393, False))
    case("the same cusp with the ayanamsa left off is caught", True,
         grade_row(5, 3, 47.6, 0.0, e24, 280.457, 23.4393, False))
    ok = reference_rate(6, 1, 47.6, 280.457, 23.4393) == 0.0
    print("  %s: Whole Sign cusp rates are 0" % ("ok" if ok else "FAIL"))
    bad += not ok
    r = reference_rate(6, 13, 47.6, 280.457, 23.4393)
    ok = r is not None and r > 100.0
    print("  %s: a Whole Sign system's ANGLE still moves (%.1f)" % ("ok" if ok else "FAIL", r or 0))
    bad += not ok
    r = reference_rate(1, 13, 47.6, 280.457, 23.4393)
    ok = r is not None and 100.0 < r < 1000.0
    print("  %s: an Ascendant moves hundreds of degrees a day (%.1f)" % ("ok" if ok else "FAIL", r or 0))
    bad += not ok
    return bad


def ask(port, out, jd, lat, zodiac, objs, speeds, cols, count=1):
    if os.path.exists(out):
        os.remove(out)
    prof = "plane=ecl,form=sph,speeds=%d,obs=topo,site=-122.3:%s:50,cols=%s" % (speeds, lat, cols)
    if zodiac:
        prof += ",zodiac=" + zodiac
    r = subprocess.run([os.path.join(ROOT, "eph_wsclient"), "--host", "127.0.0.1", "--port", port,
                        "--quiet", "--meta", "--jd", repr(jd), "--count", str(count), "--profile", prof,
                        "--house", objs, "--out", out], capture_output=True, text=True)
    meta, rows = {}, []
    for line in r.stdout.splitlines():           # --meta prints to stdout
        f = line.split()
        if f and f[0] == "META":
            meta[int(f[1])] = line
    if os.path.exists(out):
        for line in open(out):
            f = line.split()
            if f:
                rows.append(f)
    return meta, rows, r.stderr.strip()


def gate(port):
    lats = [-70.0, -40.0, 0.5, 17.3, 47.6, 60.0, 66.0, 70.0, 80.0, 90.0, 66.5597, 66.5617]
    jds = [2378496.5, 2451545.0, 2461300.5]
    out = os.path.join(tempfile.mkdtemp(), "o.txt")
    bad, nrows, nans, nrate = [], 0, 0, 0
    for zodiac in ("", "lahiri"):
        for jd in jds:
            for lat in lats:
                meta, rows, err = {}, [], ""
                for system in range(11):
                    objs = ",".join("%d:%d" % (system, p) for p in range(1, 17))
                    m1, r1, e1 = ask(port, out, jd, lat, zodiac, objs, 1, "0x3a")
                    for j, line in m1.items():
                        meta[system * 16 + j] = line
                    rows += r1
                    err = err or e1
                if len(rows) != 176:
                    bad.append("jd %.1f lat %.4f zodiac %r: %d rows (%s)" % (jd, lat, zodiac, len(rows), err[:80]))
                    continue
                for i, f in enumerate(rows):
                    system, point = i // 16, i % 16 + 1
                    nrows += 1
                    vals = [float.fromhex(x) if x != "nan" else float("nan") for x in f[5:]]
                    refused = math.isnan(vals[0])
                    if refused:
                        if "err=9" not in meta.get(i, ""):
                            bad.append("%s point %d lat %.4f: NaN row without error 9" % (H.SYSTEMS[system], point, lat))
                            continue
                        # the polar band is graded with the obliquity of date
                        bad += grade_row(system, point, lat, 0.0, 0.0, 0.0, 23.4393 - 0.0130 * (jd - 2451545.0) / 36525.0, True)
                        continue
                    # lon lat dist dlon dlat ddist ayanamsa dT armc eps
                    lon, ayan, armc, eps = vals[0], vals[6], vals[8], vals[9]
                    nans += 1
                    bad += grade_row(system, point, lat, ayan, lon, armc, eps, False)
                    if zodiac and ayan == 0.0:
                        bad.append("%s point %d: sidereal request, ayanamsa column 0" % (H.SYSTEMS[system], point))
                    if lat in (47.6, 0.5) and jd == 2451545.0 and not zodiac:
                        ref = reference_rate(system, point, lat, armc, eps)
                        nrate += 1
                        flagged = "flags=0x60" in meta.get(i, "") or "flags=0x40" in meta.get(i, "")
                        if ref is None:
                            if vals[3] != 0.0 or not flagged:
                                bad.append("%s point %d: jump across the stencil, server rate %g flagged=%s"
                                           % (H.SYSTEMS[system], point, vals[3], flagged))
                        elif abs(vals[3] - ref) > 2e-3:
                            bad.append("%s point %d lat %.4f: rate %.6f, reference stencil %.6f"
                                       % (H.SYSTEMS[system], point, lat, vals[3], ref))
    # the boundary, both hemispheres, Placidus and Koch cusp 11, at
    # +-0.001 degree of 90 - eps for the eps the server reports at that instant
    meta, rows, err = ask(port, out, 2451545.0, 0.5, "", "5:13", 0, "0x30")
    epsJ = float.fromhex(rows[0][-1]) if rows else 23.4377
    for sign in (1.0, -1.0):
        for delta, want in ((-0.001, True), (0.001, False)):
            lat = sign * (90.0 - epsJ + delta)
            meta, rows, err = ask(port, out, 2451545.0, lat, "", "0:11,1:11", 0, "0x0")
            got = [r[5] != "nan" for r in rows]
            if got != [want, want]:
                bad.append("Placidus/Koch at lat %.4f (90 - eps %+.3f) answered %s, expected %s"
                           % (lat, delta, got, want))
    # no extra columns asked: the row is the base six
    meta, rows, err = ask(port, out, 2451545.0, 47.6, "", "5:13", 0, "0x0")
    if not rows or len(rows[0]) != 11:
        bad.append("no extra columns asked, the row is not the base six: %s" % (rows[0] if rows else err))
    print("%d rows requested, %d answered and graded, %d rates" % (nrows, nans, nrate))
    for b in bad[:25]:
        print("  BAD:", b)
    if bad:
        print("HOUSES FAIL: %d complaints" % len(bad))
        return 1
    print("HOUSES PASS: every house point the server answered is the quantity 3.5b defines,")
    print("at its own ARMC and obliquity; refusals fall where 3.5b puts them.")
    return 0


if __name__ == "__main__":
    if len(sys.argv) == 2 and sys.argv[1] == "--selftest":
        rc = selftest()
        print("HOUSES GATE SELFTEST %s" % ("FAIL" if rc else "PASS"))
        sys.exit(1 if rc else 0)
    if len(sys.argv) == 2:
        sys.exit(gate(sys.argv[1]))
    sys.exit(__doc__)
