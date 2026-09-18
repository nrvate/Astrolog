#!/usr/bin/env python3
"""tools/star_identity_audit.py -- a star name carries ITS OWN star's position.

Every other check over sefstars.txt is structural: field counts, parse
success, that a name resolves.  All of those pass on a record that resolves
perfectly to the wrong star, which is what this file shipped.  Five names in
the Centaurus block -- Rigil Kentaurus, Rigel Kentaurus, Toliman, Bungula and
Proxima Centauri -- carried one identical set of coordinates, so asking for
Proxima Centauri returned alpha Centauri A, 2.18 degrees away, and asking for
Toliman returned A rather than B, 16.45 arcsec away.  Nothing complained,
because nothing here had ever compared a name against the sky.

So the reference comes from outside the repository, the same way the numeric
oracle's does: catalogued ICRS positions, cited per row.  A generous
tolerance is deliberate -- this is an IDENTITY check, not a precision one.
It asks "is this the right star", and the answer is wrong by arcminutes or
degrees when it is wrong at all.  Where the file knowingly carries something
other than the catalogue entry, the tolerance says so and why.

The second check is the one that actually caught both defects: names the IAU
assigns to DIFFERENT components of a multiple system must sit at different
places, separated by about what the system's geometry says.  An alias may
share a position; a distinct star may not.

  python3 tools/star_identity_audit.py [path/to/sefstars.txt]

Sources: SIMBAD (CDS) for astrometry; IAU Working Group on Star Names for the
name-to-component assignments (Rigil Kentaurus -> alpha Cen A, 2016-11-06;
Proxima Centauri -> alpha Cen C, 2016-08-21; Toliman -> alpha Cen B,
2018-08-10).
"""
import math, sys

PATH = sys.argv[1] if len(sys.argv) > 1 else "sefstars.txt"

# name -> (RA hours, RA min, RA sec, Dec sign*deg, min, sec, tolerance arcsec, note)
REFERENCE = [
  ("Rigil Kentaurus", 14, 39, 36.494, -60, 50,  2.374,  30,
   "SIMBAD alpha Cen A; the file carries the AB photocentre, 6.2\" away"),
  ("Toliman",         14, 39, 35.063, -60, 50, 15.099,   5,
   "SIMBAD alpha Cen B"),
  ("Proxima Centauri",14, 29, 42.946, -62, 40, 46.165,   5,
   "SIMBAD alpha Cen C = V645 Cen = GJ 551"),
  # Controls, so a broken parser cannot pass this by finding nothing.
  ("Sirius",          6, 45,  8.917, -16, 42, 58.02,    30, "SIMBAD alpha CMa"),
  ("Vega",           18, 36, 56.336, +38, 47,  1.28,    30, "SIMBAD alpha Lyr"),
  ("Spica",          13, 25, 11.579, -11,  9, 40.75,    30, "SIMBAD alpha Vir"),
  ("Betelgeuse",      5, 55, 10.305,  +7, 24, 25.43,    30, "SIMBAD alpha Ori"),
]

# Names the IAU assigns to different stars, with the separation their geometry
# requires.  (low, high) in arcsec.
DISTINCT = [
  ("Rigil Kentaurus", "Toliman",          2.0,    25.0,
   "alpha Cen A and B: an 80-year orbit, 2\"-22\" apparent separation"),
  ("Rigil Kentaurus", "Proxima Centauri", 7000.0, 8500.0,
   "alpha Cen A and C: about 2.18 degrees apart on the sky"),
  ("Toliman",         "Proxima Centauri", 7000.0, 8500.0,
   "alpha Cen B and C"),
]


def read(path):
  out = {}
  for ln in open(path, encoding="latin-1"):
    if ln.startswith("#") or "," not in ln:
      continue
    f = [x.strip() for x in ln.split(",")]
    if len(f) < 14:
      continue
    try:
      ra = (float(f[3]) + float(f[4]) / 60 + float(f[5]) / 3600) * 15
      d, m, s = float(f[6]), float(f[7]), float(f[8])
      dec = abs(d) + m / 60 + s / 3600
      if f[6].lstrip().startswith("-"):
        dec = -dec
    except ValueError:
      continue
    out.setdefault(f[0], (ra, dec))
  return out


def sep(a, b):
  """Angular separation in arcsec (Vincenty, stable at every separation)."""
  D = math.pi / 180
  l1, b1, l2, b2 = a[0] * D, a[1] * D, b[0] * D, b[1] * D
  dl = l2 - l1
  x = math.sqrt((math.cos(b2) * math.sin(dl)) ** 2 +
                (math.cos(b1) * math.sin(b2) -
                 math.sin(b1) * math.cos(b2) * math.cos(dl)) ** 2)
  y = math.sin(b1) * math.sin(b2) + math.cos(b1) * math.cos(b2) * math.cos(dl)
  return math.atan2(x, y) / D * 3600


stars = read(PATH)
bad = 0
print("%-18s %10s %10s  %s" % ("star", "offset", "allowed", "reference"))
for nm, rh, rm, rs, dd, dm, ds, tol, note in REFERENCE:
  if nm not in stars:
    print("%-18s %10s %10s  MISSING from %s" % (nm, "-", "-", PATH)); bad += 1; continue
  ref = ((rh + rm / 60 + rs / 3600) * 15,
         (abs(dd) + dm / 60 + ds / 3600) * (-1 if dd < 0 else 1))
  d = sep(stars[nm], ref)
  ok = d <= tol
  print("%-18s %9.2f\" %9.1f\"  %s%s" % (nm, d, tol, note, "" if ok else "   <-- FAIL"))
  bad += not ok
print()
print("%-18s %-18s %10s %16s  %s" % ("star", "star", "separation", "required", "why"))
for a, b, lo, hi, note in DISTINCT:
  if a not in stars or b not in stars:
    print("  missing %s or %s" % (a, b)); bad += 1; continue
  d = sep(stars[a], stars[b])
  ok = lo <= d <= hi
  print("%-18s %-18s %9.2f\" %7.0f\"-%-7.0f\"  %s%s" %
        (a, b, d, lo, hi, note, "" if ok else "   <-- FAIL"))
  bad += not ok
print("\n%s: %d problem(s)" % (PATH, bad))
sys.exit(1 if bad else 0)
