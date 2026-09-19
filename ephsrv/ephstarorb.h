// ephsrv/ephstarorb.h -- the four astrometric binaries, the arithmetic only.
//
// EPHEMERIS_ACCURACY_REGISTRY.md 2.4. Every star catalogue either engine reads
// supplies a position and a LINEAR proper motion, so a star whose photocentre
// swings around an unseen companion is propagated along a straight line and
// the orbit accumulates. Four stars in sefstars.txt are affected enough to
// matter: Sirius A, Procyon A, alpha Cen A and alpha Cen B.
//
// This header holds the MATH and nothing else, like ephsidplane.h. Which stars
// have orbits and what the catalogue line already contains is data; where the
// star came from is the caller's business. tools/star-orbit-check.py is the
// net: it reproduces ORB6's own published ephemeris from these same elements,
// and it reproduces the sefstars.txt alpha Cen A-to-B separation at the
// Hipparcos epoch in BOTH components, which is the only check either project
// has that can see DIRECTION rather than magnitude.
//
// TWO SWISS COPIES, ONE PIECE OF MATHS -- the same arrangement ephsidplane.h
// describes. Define EPHSID_FORK before including this for the context-taking
// forms. Include it after ephsidplane.h, whose macros it reuses.
//
// Requires <math.h>, <string.h> and Swiss's swephexp.h, swephlib.h and sweph.h.

#ifndef EPHSTARORB_H
#define EPHSTARORB_H

// The two Swiss entry points FEphStarOrbCall() needs, in each copy's spelling.
#ifdef EPHSID_FORK
#define EPHSTARORB_FIXSTAR(s, j, f, x, e) swe_fixstar2_r(ctx, (s), (j), (f), (x), (e))
#define EPHSTARORB_AYANAMSA(j, f, p, e) swe_get_ayanamsa_ex_r(ctx, (j), (f), (p), (e))
#else
#define EPHSTARORB_FIXSTAR(s, j, f, x, e) swe_fixstar2((s), (j), (f), (x), (e))
#define EPHSTARORB_AYANAMSA(j, f, p, e) swe_get_ayanamsa_ex((j), (f), (p), (e))
#endif

// The visual orbits, from the USNO Sixth Catalog of Orbits of Visual Binary
// Stars (orb6orbits.txt), masses from the papers the catalogue's reference
// column names. Fetched and read here rather than taken from the other
// project, which is the confirmation worth having: the same three references
// arrived at independently.
//
//   Sirius    WDS 06451-1643  BdH2017  Bond et al. 2017, ApJ 840, 70
//   Procyon   WDS 07393+0514  BdH2015  Bond et al. 2015, ApJ 813, 106
//   alpha Cen WDS 14396-6050  Ake2021  Akeson et al. 2021, AJ 162, 14
//
// The node Omega is at the equinox the catalogue record names -- J2000 for all
// three -- and it STAYS there. ORB6's published ephemeris gives the position
// angle at the equinox of DATE, and the check applies that precession to
// compare; the offset laid on a star must not, because ours is wanted in one
// fixed frame and re-precessing the node per epoch would put the orbit in a
// frame that moves. Registry 2.4, convention 1.
// The primary's ICRS place and proper motion ride along because the tangent
// plane the offset is laid in is the PRIMARY's, at the date -- for the
// secondary too, which is placed from the primary. Taking the basis from the
// catalogue rather than from the position Swiss just answered is what keeps
// this independent of the frame that position came back in: a sidereal
// longitude, a J2000 equatorial one and an apparent ecliptic one all give the
// same basis, because none of them is consulted for it.
typedef struct _EPHSTARORBIT {
  double P;      // period, years
  double T;      // epoch of periastron, year
  double e;      // eccentricity
  double a;      // semi-major axis of the RELATIVE orbit, arcsec
  double incl;   // inclination, degrees
  double Om;     // node, degrees, at the record's own equinox
  double om;     // argument of periastron, degrees
  double ra;     // the primary at ICRS J2000, degrees
  double dec;
  double pmRa;   // mu alpha * cos delta, and mu delta, arcsec/year
  double pmDec;
} EPHSTARORBIT;

static const EPHSTARORBIT rgStarOrbit[] = {
  // P          T          e        a        i         Om        om
  { 50.1284, 1994.5715, 0.59142,  7.4957, 136.336,  45.400, 149.161,
    101.2871553, -16.7161159, -0.54601, -1.22307 },              // Sirius
  { 40.840,  1968.076,  0.39785,  4.3075,  31.408, 100.683,  89.23,
    114.8254979,   5.2249876, -0.71459, -1.03680 },              // Procyon
  { 79.762,  1955.564,  0.51947, 17.4930,  79.2430, 205.073, 231.519,
    219.9020583, -60.8339927, -3.67925,  0.47367 },              // alpha Cen
};

// What the catalogue line already contains is NOT uniform across the four,
// and that is the whole of why this table has more in it than a mass ratio.
//
//   fracPos  multiplies the relative orbit to give this star's own excursion
//            from the system barycentre: -mB/(mA+mB) for a primary,
//            +mA/(mA+mB) for a secondary.
//   fracTan  multiplies the TANGENT removed at t0. Zero where the catalogue
//            line is already barycentric, so the whole excursion is added.
//   t0       the epoch that tangent is taken at -- the solution's own epoch,
//            because a component solution's straight line IS that tangent.
//   szLine   the star whose catalogue line to start from, when it is not this
//            star's own.
//
// Sirius A and Procyon A carry Hipparcos ORBITAL solutions, so their lines are
// barycentric and the whole excursion is added. alpha Cen A is a COMPONENT
// solution at 1991.25, so its line already carries the star's own motion there
// and only the curvature the straight line misses is added. alpha Cen B is
// placed from A plus the relative orbit, because B's own solution is poor
// (+/-20-26 mas/yr) -- so its own line is not used at all, and the tangent
// removed is still A's.
typedef struct _EPHSTARORB {
  const char *szNomen;   // sefstars.txt's nomenclature field, the key
  int iOrb;              // which orbit above
  double fracPos;
  double fracTan;
  double t0;
  const char *szLine;    // NULL = this star's own catalogue line
} EPHSTARORB;

static const EPHSTARORB rgStarOrb[] = {
  { "alCMa",  0, -1.018 / (2.063 + 1.018),   0.0,                          0.0,     NULL },
  { "alCMi",  1, -0.592 / (1.478 + 0.592),   0.0,                          0.0,     NULL },
  { "alCenA", 2, -0.9092 / (1.0788 + 0.9092), -0.9092 / (1.0788 + 0.9092), 1991.25, NULL },
  // The leading comma is Swiss's own "search the nomenclature field, not the
  // traditional name" marker. Without it the lookup is by traditional name and
  // "alCenA" resolves to nothing at all.
  { "alCenB", 2,  1.0788 / (1.0788 + 0.9092), -0.9092 / (1.0788 + 0.9092), 1991.25, ",alCenA" },
};
#define cStarOrb (int)(sizeof(rgStarOrb) / sizeof(rgStarOrb[0]))

// The SECONDARY relative to the primary at year t, as (east, north) in arcsec.
//
// Standard visual-binary reduction: mean anomaly, Kepler's equation, the true
// anomaly and radius in the orbit plane, then the projection onto the sky.
// Position angle is measured from north THROUGH EAST, so east = rho sin theta
// and north = rho cos theta -- the convention the sefstars.txt alpha Cen check
// pins, and the one a wrong sign here misses by 22 to 31 arcsec.
static void StarOrbRel(const EPHSTARORBIT *po, double t, double *pE, double *pN)
{
  double M, E, d, v, r, u, ci, cu, su, xw, yw, co, so;
  int i;

  M = 2.0 * PI * (t - po->T) / po->P;
  M = fmod(M, 2.0 * PI);
  E = M;
  // Newton-Raphson. The eccentricities here are at most 0.59, so this
  // converges in a handful of turns from E = M.
  for (i = 0; i < 60; i++) {
    d = (E - po->e * sin(E) - M) / (1.0 - po->e * cos(E));
    E -= d;
    if (fabs(d) < 1.0e-14)
      break;
  }
  v = 2.0 * atan2(sqrt(1.0 + po->e) * sin(E / 2.0),
                  sqrt(1.0 - po->e) * cos(E / 2.0));
  r = po->a * (1.0 - po->e * cos(E));
  u = v + po->om * DEGTORAD;          // argument of latitude
  ci = cos(po->incl * DEGTORAD);
  cu = cos(u); su = sin(u);
  co = cos(po->Om * DEGTORAD); so = sin(po->Om * DEGTORAD);
  // In the plane of the sky: xw toward the node, yw ninety degrees from it,
  // the second foreshortened by the inclination. Rotating by Omega puts them
  // on north and east. Written out rather than as theta and rho because the
  // arctangent form needs a quadrant fix and this does not; the Python check
  // computes theta and rho and the two agree.
  xw = r * cu; yw = r * su * ci;
  *pN = xw * co - yw * so;
  *pE = xw * so + yw * co;
}

// The offset this star's catalogue position is missing at year t, as
// (east, north) in arcsec. The tangent term is differentiated numerically at
// t0, which is exact enough: the excursion is smooth and the step is a day.
static void StarOrbOffset(const EPHSTARORB *ps, double t, double *pE, double *pN)
{
  const EPHSTARORBIT *po = &rgStarOrbit[ps->iOrb];
  double e0, n0, e1, n1, e2, n2;
  const double h = 1.0 / 365.25;

  StarOrbRel(po, t, &e0, &n0);
  *pE = ps->fracPos * e0;
  *pN = ps->fracPos * n0;
  if (ps->fracTan == 0.0)
    return;
  StarOrbRel(po, ps->t0, &e0, &n0);
  StarOrbRel(po, ps->t0 - h, &e1, &n1);
  StarOrbRel(po, ps->t0 + h, &e2, &n2);
  *pE -= ps->fracTan * (e0 + (e2 - e1) / (2.0 * h) * (t - ps->t0));
  *pN -= ps->fracTan * (n0 + (n2 - n1) / (2.0 * h) * (t - ps->t0));
}

// sefstars.txt's nomenclature field -> an index into rgStarOrb, or -1.
//
// Keyed on what SWISS HANDS BACK, not on what the client asked for. Swiss
// rewrites the caller's buffer to "traditional,nomenclature" on success, so
// every spelling of a star -- "Sirius", "alCMa", ",alCMa" -- arrives here as
// the one name the catalogue file gives it. A table keyed on the request would
// have to carry every alias, and would silently miss a new one.
static int IStarOrbFind(const char *szResolved)
{
  const char *pch;
  int i;

  if (szResolved == NULL)
    return -1;
  pch = strchr(szResolved, ',');
  pch = (pch != NULL) ? pch + 1 : szResolved;
  for (i = 0; i < cStarOrb; i++)
    if (strcmp(pch, rgStarOrb[i].szNomen) == 0)
      return i;
  return -1;
}

// The star whose catalogue line this one is placed from, or NULL for its own.
static const char *SzStarOrbLine(int iStar)
{
  return (iStar >= 0 && iStar < cStarOrb) ? rgStarOrb[iStar].szLine : NULL;
}

// Unit vector, and back, for a position in the frame iflag names.
static void StarOrbUnit(const double *xx, int32 iflag, double *u)
{
  double r;

  if (iflag & SEFLG_XYZ) {
    r = sqrt(xx[0]*xx[0] + xx[1]*xx[1] + xx[2]*xx[2]);
    if (r <= 0.0) r = 1.0;
    u[0] = xx[0]/r; u[1] = xx[1]/r; u[2] = xx[2]/r;
  } else {
    const double f = (iflag & SEFLG_RADIANS) ? RADTODEG : 1.0;
    SidSph2Rect(xx[0] * f, xx[1] * f, u);
  }
}

// Carry a DISPLACEMENT from ICRS equatorial into the frame iflag names.
//
// Only a displacement goes through here, never a position, which is what makes
// it short: every step is a rotation, and a rotation is linear, so the same
// arithmetic that carries the star carries the few arcsec it is being moved
// by.
//
// SIDEREAL SUBTRACTS THE AYANAMSA FROM WHICHEVER LONGITUDE WAS ASKED FOR, and
// the pole it turns about is therefore the output plane's own. That is worth
// stating because the first draft of this had it the other way -- carrying the
// equatorial case through the ecliptic on the reasoning that an ayanamsa is an
// ecliptic quantity -- and it put the offset 4.46 degrees around, which is 1.3
// arcsec of position on alpha Cen. Measured against Swiss for Sirius at J2000:
// going through the ecliptic predicts right ascension 81.454 where Swiss
// answers 76.547, while subtracting the ayanamsa from right ascension directly
// lands within 3 arcsec, and the 3 arcsec is a position error that turns the
// tangent basis by nothing worth counting.
//
// Nothing in the magnitude checks could see that: the offset stayed exactly
// the right size and pointed somewhere else. What found it was asking for an
// angle no change of frame can move -- the leg in tools/star-orbit-apply.cpp.
//
// Nutation and frame bias are deliberately NOT applied, and that is a measured
// decision rather than an omission: what these rotations act on is a
// displacement of at most 17 arcsec, so a 17 arcsec error in the frame turns
// it by 8e-5 radians, which is 1.4 milliarcsec on the largest offset here and
// 0.2 on Sirius. Precession is NOT in that class -- it reaches degrees over
// Astrolog's range -- and neither is the obliquity or the ayanamsa, so those
// three are applied.
static void StarOrbCarry(EPHSID_CTXDECL double jdEt, int32 iflag, double ayan,
  double *v)
{
  const int fJ2000 = (iflag & SEFLG_J2000) != 0;
  const int fEclOut = !(iflag & SEFLG_EQUATORIAL);
  double eps;

  if (!fJ2000)
    EPHSID_PRECESS(v, jdEt, J2000_TO_J);
  // Asked for only where a branch below needs it. Computing it up front made
  // an equatorial J2000 request -- where neither branch runs -- call
  // swi_epsiln() for a value it then threw away.
  if (ayan == 0.0 && !fEclOut)
    return;
  if (fEclOut) {
    eps = EPHSID_EPSILN(fJ2000 ? J2000 : jdEt);
    swi_coortrf(v, v, eps);
  }
  // About the output plane's own pole, which is where the longitude the
  // ayanamsa was subtracted from is measured.
  if (ayan != 0.0) {
    const double c = cos(-ayan * DEGTORAD), s = sin(-ayan * DEGTORAD);
    const double x = v[0];
    v[0] = x * c - v[1] * s; v[1] = x * s + v[1] * c;
  }
}

// The corrected DIRECTION, given the one Swiss answered, at Julian day jdEt.
//
// The offset is laid in the tangent plane at the star's position AT THE DATE
// -- registry 2.4, convention 2 -- and the basis for it comes from the
// CATALOGUE, propagated with the star's own proper motion, not from the
// position handed in. Two reasons, and the second is the one that bites: the
// position handed in may be sidereal, apparent, topocentric or J2000, and
// every one of those would give a different basis for the same physical point;
// and for the secondary the plane wanted is the PRIMARY's, which the
// secondary's position is not. Convention 3 says not to also turn the offset
// by the proper motion's rotation of local north -- using the date's own place
// here is that turn, and doing both double-counts.
//
// Arcsec of proper motion over a century are enough to matter: alpha Cen moves
// 0.065 degrees of local north by 2025, which is 10 mas on the present
// separation and more as it opens.
static void StarOrbDirection(EPHSID_CTXDECL const EPHSTARORB *ps, double jdEt,
  int32 iflag, double ayan, const double *u, double *w)
{
  const EPHSTARORBIT *po = &rgStarOrbit[ps->iOrb];
  double v[3], d[3], east[3], north[3], eOff, nOff, s;
  const double t = 2000.0 + (jdEt - J2000) / 365.25;
  const double dec = po->dec + po->pmDec * (t - 2000.0) / 3600.0;
  const double ra = po->ra +
    po->pmRa * (t - 2000.0) / 3600.0 / cos(dec * DEGTORAD);
  int i;

  SidSph2Rect(ra, dec, v);
  // East is the direction of increasing right ascension, north of increasing
  // declination: east = zhat x v, north = v x east, both normalized. At a
  // point on the equator toward the equinox this gives east = +y and
  // north = +z, which is the check that fixes the handedness.
  east[0] = -v[1]; east[1] = v[0]; east[2] = 0.0;
  s = sqrt(east[0]*east[0] + east[1]*east[1]);
  if (s < 1.0e-12) {                    // at a celestial pole there is no east
    for (i = 0; i < 3; i++) w[i] = u[i];
    return;
  }
  east[0] /= s; east[1] /= s;
  north[0] = v[1]*east[2] - v[2]*east[1];
  north[1] = v[2]*east[0] - v[0]*east[2];
  north[2] = v[0]*east[1] - v[1]*east[0];
  StarOrbOffset(ps, t, &eOff, &nOff);
  eOff *= DEGTORAD / 3600.0; nOff *= DEGTORAD / 3600.0;
  for (i = 0; i < 3; i++) d[i] = eOff * east[i] + nOff * north[i];
  StarOrbCarry(EPHSID_CTXARG jdEt, iflag, ayan, d);
  for (i = 0; i < 3; i++) w[i] = u[i] + d[i];
  s = sqrt(w[0]*w[0] + w[1]*w[1] + w[2]*w[2]);
  for (i = 0; i < 3; i++) w[i] /= s;
}

// Move a star position onto its orbit. xx is in the frame iflag names, as
// Swiss answered it; for the star placed from another's line it is that other
// star's position, so the whole excursion including the separation is applied
// here. The distance is untouched -- these are angular offsets of a companion
// at the same distance, and the components share a parallax in the catalogue.
//
// The SPEED gets the offset's own rate, differenced over a day. Leaving it out
// would make a client that differences two positions disagree with the speed
// column by up to 0.1 arcsec a year, which on alpha Cen is a tenth of the
// proper motion.
//
// ayan is the ayanamsa Swiss subtracted, in degrees, or zero when the answer
// is tropical. The caller reads it back from Swiss with the request's own
// flags rather than computing one, so a true ayanamsa stays true and a mean
// one stays mean.
static void EphStarOrbApply(EPHSID_CTXDECL int iStar, double jdEt, int32 iflag,
  double ayan, double *xx)
{
  const EPHSTARORB *ps;
  double u[3], w[3], wm[3], wp[3], r, f, lon, lat, dlon;
  const double h = 1.0;
  int i;

  if (iStar < 0 || iStar >= cStarOrb)
    return;
  ps = &rgStarOrb[iStar];
  StarOrbUnit(xx, iflag, u);
  StarOrbDirection(EPHSID_CTXARG ps, jdEt, iflag, ayan, u, w);
  StarOrbDirection(EPHSID_CTXARG ps, jdEt - h, iflag, ayan, u, wm);
  StarOrbDirection(EPHSID_CTXARG ps, jdEt + h, iflag, ayan, u, wp);
  if (iflag & SEFLG_XYZ) {
    r = sqrt(xx[0]*xx[0] + xx[1]*xx[1] + xx[2]*xx[2]);
    for (i = 0; i < 3; i++) {
      xx[i] = w[i] * r;
      xx[i + 3] += (wp[i] - wm[i]) / (2.0 * h) * r;
    }
    return;
  }
  f = (iflag & SEFLG_RADIANS) ? 1.0 : RADTODEG;
  lon = atan2(w[1], w[0]); lat = asin(w[2]);
  if (lon < 0.0) lon += 2.0 * PI;
  xx[0] = lon * f;
  xx[1] = lat * f;
  // The rate of the CORRECTION only: the star's own motion is already in the
  // speed columns, and the two endpoints here share one input direction. The
  // wrap matters -- a star a hair either side of zero longitude would
  // otherwise difference to a full turn per two days.
  dlon = atan2(wp[1], wp[0]) - atan2(wm[1], wm[0]);
  if (dlon > PI) dlon -= 2.0 * PI; else if (dlon < -PI) dlon += 2.0 * PI;
  xx[3] += dlon / (2.0 * h) * f;
  xx[4] += (asin(wp[2]) - asin(wm[2])) / (2.0 * h) * f;
}

// The whole sequence, for a caller that asked Swiss at a TT instant: resolve
// the star, re-ask for the line it is placed from if that is another star's,
// read back the ayanamsa Swiss used, and apply. FALSE only when a Swiss call
// this needed failed; a star with no orbit is TRUE and untouched.
//
// szResolved is the buffer swe_fixstar2() rewrote, and it is NOT written to
// here -- callers read it back as the star's display name.
//
// astrolog-ephd does this inline rather than calling this, because its star
// call chooses between the UT and TT entry points and carries the request's
// own delta T. The arithmetic is the same and lives above; only the two Swiss
// calls differ.
static int FEphStarOrbCall(EPHSID_CTXDECL const char *szResolved, double jdEt,
  int32 iflag, double *xx)
{
  char serr[AS_MAXCH], szLine[SE_MAX_STNAME * 2];
  double ayan = 0.0;
  const int iStar = IStarOrbFind(szResolved);
  const char *szFrom;

  if (iStar < 0)
    return 1;
  szFrom = SzStarOrbLine(iStar);
  if (szFrom != NULL) {
    snprintf(szLine, sizeof(szLine), "%s", szFrom);
    if (EPHSTARORB_FIXSTAR(szLine, jdEt, iflag, xx, serr) < 0)
      return 0;
  }
  // The ayanamsa Swiss ACTUALLY SUBTRACTED, asked for with the request's own
  // flags, so a true ayanamsa stays true and a mean one stays mean rather than
  // a value being reconstructed here.
  if ((iflag & SEFLG_SIDEREAL) &&
      EPHSTARORB_AYANAMSA(jdEt, iflag, &ayan, serr) < 0)
    return 0;
  EphStarOrbApply(EPHSID_CTXARG iStar, jdEt, iflag, ayan, xx);
  return 1;
}

#endif // EPHSTARORB_H
