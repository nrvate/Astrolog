#!/usr/bin/env python3
"""tools/star-orbit-check.py -- the visual-binary orbit arithmetic, checked
against the catalogue that published the elements.

EPHEMERIS_ACCURACY_REGISTRY.md §2.4 records that both this project and
Ephemeris Prometheia carried fixed stars as a position plus a LINEAR proper
motion, because that is what every star catalogue supplies -- and that a star
whose photocentre swings around an unseen companion does not move in a
straight line.  Measured against FK5 over two centuries, that costs 2.33" on
Sirius and 28.6" on alpha Cen A.

This is the arithmetic for removing it.  Elements from the USNO Sixth Catalog
of Orbits of Visual Binary Stars (orb6orbits.txt), masses from the papers the
catalogue cites, and the check is ORB6's OWN published ephemeris
(orb6ephem.txt): theta and rho at five epochs, which the catalogue computed
from these same elements.  Reproducing it says the Kepler solve and the
projection are right, independently of either engine.

  python3 tools/star-orbit-check.py

Sources, one per system, all named in ORB6's reference column:
  Sirius    WDS 06451-1643  BdH2017  Bond et al. 2017, ApJ 840, 70
  Procyon   WDS 07393+0514  BdH2015  Bond et al. 2015, ApJ 813, 106
  alpha Cen WDS 14396-6050  Ake2021  Akeson et al. 2021, AJ 162, 14
"""
import math, sys

# P years, T Besselian-ish year of periastron, e, a arcsec, i deg, Omega deg,
# omega deg, then the component masses in solar masses.
ORBITS = {
  "Sirius": dict(P=50.1284, T=1994.5715, e=0.59142, a=7.4957,
                 i=136.336, Om=45.400, om=149.161, mA=2.063, mB=1.018,
                 ra=101.28715, dec=-16.71611, ref="BdH2017"),
  "Procyon": dict(P=40.840, T=1968.076, e=0.39785, a=4.3075,
                  i=31.408, Om=100.683, om=89.23, mA=1.478, mB=0.592,
                  ra=114.82550, dec=5.22500, ref="BdH2015"),
  "alpha Cen": dict(P=79.762, T=1955.564, e=0.51947, a=17.4930,
                    i=79.2430, Om=205.073, om=231.519, mA=1.0788, mB=0.9092,
                    ra=219.90206, dec=-60.83399, ref="Ake2021"),
}

# ORB6's own ephemeris for 2025.0 .. 2029.0 (orb6ephem.txt), theta then rho.
PUBLISHED = {
  "Sirius":    [(59.0, 11.256), (57.1, 11.163), (55.2, 11.032),
                (53.3, 10.861), (51.2, 10.648)],
  "Procyon":   [(348.7, 5.070), (353.7, 5.101), (358.6, 5.122),
                (3.4, 5.135), (8.3, 5.139)],
  "alpha Cen": [(9.2, 8.737), (11.9, 9.294), (14.3, 9.765),
                (16.5, 10.121), (18.6, 10.329)],
}
EPOCHS = [2025.0, 2026.0, 2027.0, 2028.0, 2029.0]


def relative(o, t):
  """The SECONDARY relative to the primary at Besselian year t: (theta, rho).

  Standard visual-binary reduction: mean anomaly, Kepler's equation, the
  true anomaly and radius in the orbit plane, then the Thiele-Innes
  projection onto the sky."""
  M = 2.0 * math.pi * ((t - o["T"]) / o["P"] % 1.0)
  e = o["e"]
  # Kepler, Newton-Raphson. The eccentricities here are at most 0.59, so this
  # converges in a handful of turns from E = M.
  E = M
  for _ in range(60):
    d = (E - e * math.sin(E) - M) / (1.0 - e * math.cos(E))
    E -= d
    if abs(d) < 1e-14:
      break
  # True anomaly and radius, in units of the semi-major axis.
  v = 2.0 * math.atan2(math.sqrt(1.0 + e) * math.sin(E / 2.0),
                       math.sqrt(1.0 - e) * math.cos(E / 2.0))
  r = (1.0 - e * math.cos(E))
  D = math.pi / 180.0
  i, Om, om = o["i"] * D, o["Om"] * D, o["om"] * D
  u = v + om                                   # argument of latitude
  # Position angle measured from north through east, and the separation
  # foreshortened by the inclination.
  th = Om + math.atan2(math.sin(u) * math.cos(i), math.cos(u))
  rho = o["a"] * r * math.cos(u) / math.cos(th - Om) if abs(math.cos(th - Om)) > 1e-9 \
        else o["a"] * r * math.sin(u) * math.cos(i) / math.sin(th - Om)
  return (math.degrees(th) % 360.0, abs(rho))


fail = 0
print("%-11s %-9s %8s %8s %9s | %8s %8s %9s" %
      ("system", "epoch", "theta", "ORB6", 'd(")', "rho", "ORB6", 'd(")'))
for name, o in ORBITS.items():
  for t, (th0, rho0) in zip(EPOCHS, PUBLISHED[name]):
    th, rho = relative(o, t)
    # ORB6's PUBLISHED ephemeris gives the position angle at the EQUINOX OF
    # DATE, while the catalogue's node is at the equinox its record names
    # (J2000 for these). The difference is the precession of position angle,
    #     dtheta = 0.0056 deg/yr * sin(alpha) * sec(delta),
    # which is 0.13-0.19 degrees over the 25-29 years to these epochs and
    # changes SIGN between the northern and southern systems -- which is how
    # it was identified, since a rounding artefact would not do that.
    #
    # The check applies it; the OFFSET WE APPLY TO A STAR DOES NOT. Ours is
    # wanted in one fixed frame, and re-precessing the node per epoch would
    # put the orbit in a frame that moves.
    th += 0.0056 * math.sin(math.radians(o["ra"])) / \
      math.cos(math.radians(o["dec"])) * (t - 2000.0)
    dth = (th - th0 + 180.0) % 360.0 - 180.0
    # A position-angle error matters as an arc, so weight it by the separation.
    dthArc = dth * math.pi / 180.0 * rho
    drho = rho - rho0
    bad = abs(dthArc) > 0.01 or abs(drho) > 0.01
    fail += bad
    print("%-11s %-9.1f %8.2f %8.2f %9.4f | %8.3f %8.3f %9.4f%s" %
          (name, t, th, th0, dthArc, rho, rho0, drho, "  <-- BAD" if bad else ""))
print()
if fail == 0:
  print("STAR ORBIT PASS: the Kepler solve reproduces ORB6's own ephemeris "
        "to better than 0.01\" in both coordinates")
  sys.exit(0)
print("STAR ORBIT FAIL: %d of %d epochs" % (fail, len(ORBITS) * len(EPOCHS)))
sys.exit(1)
