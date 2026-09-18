#!/usr/bin/env python3
"""tools/sidplane-origin.py -- where does a sidereal plane start counting?

Run by hand against a live astrolog-ephd.  It answers the question §3.5a of
EPHEMERIS_PLUGINS_PLAN.md left open: a sidereal zodiac's zero point is
"carried onto" the invariable plane (A.8 plane 2), and the spec does not say
how.  Two readings are possible -- carry the DIRECTION the zodiac names, or
carry the ARC from the equinox -- and they differ by about half an arcminute.

The measurement needs no second engine and no external constant.  Two
coordinate systems on the sky differ by exactly one rotation, and a z-x-z
rotation has three angles: inclination, node, and where longitude starts.  So
sample the same stars on plane 1 and plane 2, fit all three, and look at the
third.  Both are FIXED frames counting from the same zero-point direction, so
a correct pair differs by a pure rotation with no origin offset at all, and
any nonzero third angle is the defect.

PLANE 0 IS DELIBERATELY NOT THE REFERENCE, and the reason cost an evening.
Measured against plane 0 this reported a residual of +13.9359" that looked
exactly like a bug in the fixed-plane code.  It is the nutation in longitude
at J2000, -13.9315" -- plane 0 carries the profile's frame, so with nutation
on, its sidereal origin sits a nutation away from the mean-equinox zero point
the fixed planes use.  That is pre-existing behaviour of the delegated plane-0
path, shared with the other engine, and nothing to do with this code.  A
reference that moves for reasons of its own cannot measure anything.

Six stars from -40 to +62 degrees of latitude are enough to over-determine
three parameters eightfold, which is the point: a rigid rotation that fits
twelve observables to rms 0 has nothing left to hide a modelling difference in.

  ./astrolog-ephd --bind 127.0.0.1 --port 47410 --ephe "$PWD/ephem;$PWD" &
  python3 tools/sidplane-origin.py 47410

Prints one row per zodiac.  A nonzero "origin" column is the defect.
"""
import math, subprocess, sys, collections

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 47410
CLIENT = "./eph_wsclient"
STARS = ["Spica", "Aldebaran", "Antares", "Regulus", "Sirius", "Vega"]
ZODIACS = sys.argv[2].split(",") if len(sys.argv) > 2 else ["fagan-bradley", "lahiri"]
JD = 2451545.0
D = math.pi / 180


def ask(zodiac, plane, star):
  """One star's (longitude, latitude) in degrees, or None if the server declined."""
  out = subprocess.run([CLIENT, "--host", "127.0.0.1", "--port", str(PORT),
    "--quiet", "--jd", str(JD), "--count", "1", "--out", "/dev/stdout",
    "--profile", "zodiac=%s,sidplane=%d,corr=0" % (zodiac, plane),
    "--stars", star], capture_output=True, text=True).stdout
  for ln in out.splitlines():
    if ln.startswith("META"):
      continue
    f = ln.split()
    if len(f) > 6 and f[5].lower().startswith(("0x", "-0x")):
      return float.fromhex(f[5]), float.fromhex(f[6])
  return None


def vec(l, b):
  l *= D; b *= D
  return [math.cos(b) * math.cos(l), math.cos(b) * math.sin(l), math.sin(b)]


def model(p, l0, b0):
  """Plane-0 (lon, lat) through the trial rotation (inclination, node, origin)."""
  i, om = p[0] * D, p[1] * D
  v = vec(l0, b0)
  x = v[0] * math.cos(om) + v[1] * math.sin(om)
  y = -v[0] * math.sin(om) + v[1] * math.cos(om)
  y2 = y * math.cos(i) + v[2] * math.sin(i)
  z2 = -y * math.sin(i) + v[2] * math.cos(i)
  return math.atan2(y2, x) / D + p[2], math.asin(max(-1, min(1, z2))) / D


def lstsq(A, b):
  n = len(A[0])
  M = [[sum(A[k][i] * A[k][j] for k in range(len(A))) for j in range(n)] +
       [sum(A[k][i] * b[k] for k in range(len(A)))] for i in range(n)]
  for c in range(n):
    piv = max(range(c, n), key=lambda r: abs(M[r][c]))
    M[c], M[piv] = M[piv], M[c]
    for r in range(n):
      if r != c and M[c][c]:
        f = M[r][c] / M[c][c]
        for k in range(c, n + 1):
          M[r][k] -= f * M[c][k]
  return [(M[i][n] / M[i][i] if abs(M[i][i]) > 1e-30 else 0.0) for i in range(n)]


def fit(rows):
  """Gauss-Newton on the three Euler angles; residuals in arcsec."""
  def resid(p):
    r = []
    for l0, b0, l2, b2 in rows:
      L, B = model(p, l0, b0)
      r += [((L - l2 + 180) % 360 - 180) * 3600, (B - b2) * 3600]
    return r
  p = [1.5787, 83.7, 0.0]
  for _ in range(80):
    r = resid(p)
    J = [[0.0] * 3 for _ in r]
    for j in range(3):
      q = list(p); q[j] += 1e-7
      rq = resid(q)
      for m in range(len(r)):
        J[m][j] = (rq[m] - r[m]) / 1e-7
    d = lstsq(J, [-x for x in r])
    for j in range(3):
      p[j] += d[j]
    if max(abs(x) for x in d) < 1e-13:
      break
  r = resid(p)
  return p, math.sqrt(sum(x * x for x in r) / len(r))


print("Planes 1 and 2 are both FIXED frames counting from the same zero-point")
print("direction, so the rotation between them carries NO origin offset. Any")
print("nonzero value in the origin column is a defect.\n")
print("%-22s %11s %11s %12s %9s" %
      ("zodiac", "incl(deg)", "node(deg)", 'origin(")', 'rms(")'))
fail = 0
for z in ZODIACS:
  rows = []
  for s in STARS:
    a, b = ask(z, 1, s), ask(z, 2, s)
    if a and b:
      rows.append((a[0], a[1], b[0], b[1]))
  if len(rows) < 3:
    print("%-22s  no usable rows" % z); continue
  if all(abs(r[0] - r[2]) < 1e-12 and abs(r[1] - r[3]) < 1e-12 for r in rows):
    print("%-22s  plane 2 answered BIT-IDENTICALLY to plane 1 -- plane ignored" % z)
    fail += 1
    continue
  p, rms = fit(rows)
  # The zodiac's own zero point, through the fitted rotation.  Zero if the
  # zero point was carried as a direction.
  origin = model(p, 0.0, 0.0)[0] * 3600
  print("%-22s %11.7f %11.6f %12.4f %9.5f" % (z, p[0], p[1], origin, rms))
  if abs(origin) > 0.001:
    fail += 1
sys.exit(1 if fail else 0)
