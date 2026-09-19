// tools/star-orbit-apply.cpp -- ephsrv/ephstarorb.h answers the offsets the
// OTHER ENGINE published, at the epochs it published them for.
//
//   star-orbit-apply <ephemeris-dir>
//
// Built and run by tools/star-orbit-check.sh.
//
// tools/star-orbit-check.py nets the ARITHMETIC against ORB6's own published
// ephemeris and against sefstars.txt's own alpha Cen separation. Neither of
// those touches the C, and the C is where the frame work lives -- the tangent
// basis, the rotation into the frame a request asked for, the speed. So this
// asks the header itself, through Swiss, and compares against numbers computed
// by an independent implementation from independent catalogue data.
//
// THE FIXTURE IS EPHEMERIS PROMETHEIA'S, at their commit 618861f: the offset
// each star's catalogue position is missing, as (east = d(alpha)cos(delta),
// north = d(delta)) in milliarcsec, ICRS, at TT epochs. Their engine is
// cleanroom -- ERFA, Hipparcos records, their own orbit code -- so an
// agreement here is two implementations of a published model meeting, not one
// checking itself. Their controls: Vega and 61 Cyg A give 0.000 mas through
// the same subtraction at every epoch, and Barnard's Star at most 1.4 mas over
// a century from its radial-velocity term.
//
// WHY THIS IS MEASURED AS A DIFFERENCE and not as a position. Their catalogue
// line is a Hipparcos record carried with ERFA's pmsafe; ours is sefstars.txt
// carried by Swiss. The lines are not the same data and the positions would
// differ by that, forever, with or without an orbit. The OFFSET is what the
// model specifies and the only thing both engines can be held to.
//
// ALPHA CEN A IS DELIBERATELY NOT GRADED, and that is the one open item.
// Their split is 1.13/0.97 solar masses -- Pourbaix & Boffin 2016 -- while the
// elements both engines use are Akeson et al. 2021, which publishes its own
// masses, 1.0788 +/- 0.0029 and 0.9092 +/- 0.0025, from the same fit. Mixing a
// mass ratio into another paper's elements is the thing to avoid, so this side
// uses Akeson's. It is worth 185 mas at 1900 and 142 at 2100, which is far too
// big to round away: the difference is PRINTED here at every epoch so the
// decision stays visible until one side moves. Everything else agrees to under
// a milliarcsec.
//
// The relative orbit is graded, because it carries no mass ratio at all and so
// is the part of alpha Cen both sides can be held to today.

#include <math.h>
#include <stdio.h>
#include <string.h>

// No extern "C" around these: the vendored Swiss sources compile as C++ in
// this tree, so their symbols have C++ linkage and calc.cpp includes them the
// same way. Wrapping them here declared swi_epsiln with C linkage and the link
// failed on it.
#include "swephexp.h"
#include "swephlib.h"
#include "sweph.h"
#include "ephsrv/ephsidplane.h"
#include "ephsrv/ephstarorb.h"

// Barycentric, geometric, ICRF, equatorial -- the frame their fixture names.
#define IFLAG (SEFLG_SWIEPH | SEFLG_SPEED | SEFLG_EQUATORIAL | SEFLG_ICRS | \
               SEFLG_J2000 | SEFLG_NONUT | SEFLG_BARYCTR | SEFLG_TRUEPOS | \
               SEFLG_NOABERR | SEFLG_NOGDEFL)

static const double rgYear[] = { 1900.0, 1991.25, 2000.0, 2050.0, 2100.0 };
#define cYear (int)(sizeof(rgYear) / sizeof(rgYear[0]))

typedef struct { const char *szStar; double rgE[cYear], rgN[cYear]; } FIXTURE;

// Ephemeris Prometheia at 618861f, milliarcsec.
static const FIXTURE rgFixture[] = {
  { "Sirius",  { -792.251,  228.353,  -709.761,  -667.991,  -625.878 },
               { 1279.376, -1174.852, 1291.491,  1296.610,  1301.066 } },
  { "Procyon", {  576.588, -575.144, -1197.643,   176.682,   958.300 },
               { -1292.354, -1341.729, -416.196,  609.617,  -869.242 } },
};
#define cFixture (int)(sizeof(rgFixture) / sizeof(rgFixture[0]))

// Their alpha Cen A, for printing the mass-ratio gap rather than grading it.
static const double rgAcenAE[cYear] = { -4152.059, 0.0, -337.302, 982.444, 155.267 };
static const double rgAcenAN[cYear] = { -18678.395, 0.0, -442.066, 13532.640, 14324.522 };

// Their B-minus-A in A's tangent plane, arcsec. No mass ratio enters it.
static const double rgRelE[cYear] = { -10.7258, -11.0951, -9.5383, -7.6724, -1.1585 };
static const double rgRelN[cYear] = { -18.8631, -15.5794, -10.4299, -16.7275,  5.5151 };

static double JdOf(double year) { return J2000 + (year - 2000.0) * 365.25; }

// The catalogue line and the corrected position, both in the frame IFLAG
// names, which is ICRS equatorial. Exactly the sequence the server runs.
static int FPlaceOfIn(const char *szStar, double year, int32 iflag,
  double *xxLine, double *xxOrb)
{
  char star[SE_MAX_STNAME * 2], serr[AS_MAXCH];
  double jd = JdOf(year), ayan = 0.0;
  int iStar;

  snprintf(star, sizeof(star), "%s", szStar);
  if (swe_fixstar2(star, jd, iflag, xxLine, serr) < 0) {
    printf("  %-10s %8.2f  FAILED: %s\n", szStar, year, serr);
    return 0;
  }
  iStar = IStarOrbFind(star);
  if (iStar < 0) {
    printf("  %-10s %8.2f  NOT IN THE TABLE (resolved as \"%s\")\n",
           szStar, year, star);
    return 0;
  }
  memcpy(xxOrb, xxLine, sizeof(double) * 6);
  // Through the SHARED helper, not a copy of its steps: re-asking for the line
  // a star is placed from, reading the ayanamsa back, and applying are exactly
  // what could differ between this check and the two engines, so the check
  // runs the same code Astrolog's star path runs.
  (void)ayan;
  if (!FEphStarOrbCall(star, jd, iflag, xxOrb)) {
    printf("  %-10s %8.2f  FAILED inside the orbit call\n", szStar, year);
    return 0;
  }
  return 1;
}

static int FPlaceOf(const char *szStar, double year, double *xxLine,
  double *xxOrb)
{
  return FPlaceOfIn(szStar, year, IFLAG, xxLine, xxOrb);
}

// A GNOMONIC tangent-plane separation in milliarcsec, second minus first, in
// the plane tangent at the first.
//
// Not d(alpha)cos(delta) and d(delta), which is what the first draft of this
// did. That form carries the first point's cos(delta) across the whole
// separation, so it is wrong by about tan(delta) times the separation in
// declination -- 1.6e-4 of the offset for alpha Cen at delta = -60.8, which is
// 2.4 mas on a 21 arcsec separation and graded BAD here against a fixture that
// says gnomonic. It was the check that was wrong and not the header, and the
// giveaway was that the residual scaled with the separation instead of being
// constant or random. Sirius and Procyon never showed it: their offsets are an
// arcsec and their declinations small, which puts the same error at 0.003 mas.
static void Sep(const double *xxA, const double *xxB, double *pE, double *pN)
{
  double a[3], b[3], east[3], north[3], s, d;
  int i;

  SidSph2Rect(xxA[0], xxA[1], a);
  SidSph2Rect(xxB[0], xxB[1], b);
  east[0] = -a[1]; east[1] = a[0]; east[2] = 0.0;
  s = sqrt(east[0]*east[0] + east[1]*east[1]);
  east[0] /= s; east[1] /= s;
  north[0] = a[1]*east[2] - a[2]*east[1];
  north[1] = a[2]*east[0] - a[0]*east[2];
  north[2] = a[0]*east[1] - a[1]*east[0];
  d = 0.0;
  for (i = 0; i < 3; i++) d += b[i] * a[i];
  *pE = *pN = 0.0;
  for (i = 0; i < 3; i++) {
    *pE += b[i] * east[i];
    *pN += b[i] * north[i];
  }
  *pE *= RADTODEG * 3600000.0 / d;
  *pN *= RADTODEG * 3600000.0 / d;
}

// The offset the header applied, as (east, north) in milliarcsec.
static int FOffsetOf(const char *szStar, double year, double *pE, double *pN)
{
  double xxLine[6], xxOrb[6];

  if (!FPlaceOf(szStar, year, xxLine, xxOrb))
    return 0;
  Sep(xxLine, xxOrb, pE, pN);
  return 1;
}

int main(int argc, char **argv)
{
  double e, n, worst = 0.0;
  int i, j, fail = 0;

  swe_set_ephe_path(argc > 1 ? argv[1] : (char *)"ephem");

  printf("The offsets ephsrv/ephstarorb.h applies, against Ephemeris "
         "Prometheia's\nfixture at 618861f (milliarcsec, ICRS, TT epochs).\n\n");
  printf("%-10s %9s %11s %11s %9s %9s\n",
         "star", "epoch", "east", "north", "dE", "dN");
  for (i = 0; i < cFixture; i++) {
    for (j = 0; j < cYear; j++) {
      if (!FOffsetOf(rgFixture[i].szStar, rgYear[j], &e, &n)) { fail++; continue; }
      double dE = e - rgFixture[i].rgE[j], dN = n - rgFixture[i].rgN[j];
      // A milliarcsec is the fixture's own printed resolution. Two engines
      // agreeing there is as close as the numbers can say anything.
      int bad = fabs(dE) > 1.0 || fabs(dN) > 1.0;
      if (fabs(dE) > worst) worst = fabs(dE);
      if (fabs(dN) > worst) worst = fabs(dN);
      fail += bad;
      printf("%-10s %9.2f %11.3f %11.3f %9.3f %9.3f%s\n",
             rgFixture[i].szStar, rgYear[j], e, n, dE, dN, bad ? "  <-- BAD" : "");
    }
  }

  // The relative orbit, which carries no mass ratio: B's placed position less
  // A's corrected one, both in A's tangent plane.
  printf("\nalpha Cen B minus A, arcsec -- graded, no mass ratio enters it:\n");
  printf("%-10s %9s %11s %11s %9s %9s\n",
         "", "epoch", "east", "north", "dE", "dN");
  for (j = 0; j < cYear; j++) {
    double xxLineA[6], xxA[6], xxLineB[6], xxB[6], eRel, nRel;
    if (!FPlaceOf("Rigil Kentaurus", rgYear[j], xxLineA, xxA) ||
        !FPlaceOf("Toliman", rgYear[j], xxLineB, xxB)) { fail++; continue; }
    // Both corrected positions, differenced in A's tangent plane. Neither
    // catalogue line survives into this, which is what makes it transferable:
    // it is the relative orbit and nothing else.
    Sep(xxA, xxB, &eRel, &nRel);
    double relE = eRel / 1000.0, relN = nRel / 1000.0;
    double dE = (relE - rgRelE[j]) * 1000.0, dN = (relN - rgRelN[j]) * 1000.0;
    int bad = fabs(dE) > 1.0 || fabs(dN) > 1.0;
    fail += bad;
    printf("%-10s %9.2f %11.4f %11.4f %9.3f %9.3f%s\n",
           "B - A", rgYear[j], relE, relN, dE, dN, bad ? "  <-- BAD" : "");
  }

  printf("\nalpha Cen A -- PRINTED, NOT GRADED: the open mass-ratio question.\n"
         "Akeson 2021 (1.0788/0.9092, the elements' own paper) here against\n"
         "Pourbaix & Boffin 2016 (1.13/0.97) there. Milliarcsec.\n");
  printf("%-10s %9s %11s %11s %9s %9s\n",
         "", "epoch", "east", "north", "theirs dE", "dN");
  for (j = 0; j < cYear; j++) {
    if (!FOffsetOf("Rigil Kentaurus", rgYear[j], &e, &n)) { fail++; continue; }
    printf("%-10s %9.2f %11.3f %11.3f %9.1f %9.1f\n", "a Cen A", rgYear[j],
           e, n, e - rgAcenAE[j], n - rgAcenAN[j]);
  }

  // -- the frame carrying, against an angle no frame can change ------------
  //
  // THE OFFSET IS CARRIED INTO WHATEVER FRAME THE REQUEST ASKED FOR, and that
  // is the part of this with no arithmetic oracle: ORB6 and the other engine
  // both speak ICRS, so both legs above exercise one frame out of the twelve
  // the protocol allows. A rotation error there leaves the offset's SIZE
  // untouched and turns its DIRECTION, which is exactly the failure registry
  // 2.4's fourth prerequisite says the magnitude gradings cannot see.
  //
  // So grade an angle instead of a coordinate. At alpha Cen A, the angle
  // between the direction to alpha Cen B and the direction to Vega is a
  // physical angle: every frame here differs from every other by a rotation,
  // and a rotation cannot change it. B's place comes from A plus the offset,
  // so a turn of the offset turns A-to-B by the same amount and shows up one
  // for one. Vega is the third point because it has no orbit -- nothing this
  // file computes moves it.
  //
  // The sidereal rows are the reason this exists. Swiss's sidereal output is a
  // rotation about the ECLIPTIC pole even when the plane asked for is the
  // equator, and getting that wrong turns the offset by degrees while every
  // magnitude check stays green.
  {
    static const struct { const char *sz; int32 iflag; } rgFrame[] = {
      { "ecliptic of date",   0 },
      { "ecliptic J2000",     SEFLG_J2000 | SEFLG_NONUT },
      { "ecliptic ICRF",      SEFLG_ICRS | SEFLG_J2000 | SEFLG_NONUT },
      { "equator of date",    SEFLG_EQUATORIAL },
      { "equator J2000",      SEFLG_EQUATORIAL | SEFLG_J2000 | SEFLG_NONUT },
      { "ecliptic sidereal",  SEFLG_SIDEREAL },
      { "equator sidereal",   SEFLG_EQUATORIAL | SEFLG_SIDEREAL },
    };
    const int cFrame = (int)(sizeof(rgFrame) / sizeof(rgFrame[0]));
    const int32 iflagBase = SEFLG_SWIEPH | SEFLG_SPEED | SEFLG_BARYCTR |
      SEFLG_TRUEPOS | SEFLG_NOABERR | SEFLG_NOGDEFL;
    double ang0 = 0.0;
    int k;

    swe_set_sid_mode(SE_SIDM_FAGAN_BRADLEY, 0.0, 0.0);
    printf("\nthe frame carrying: the angle at alpha Cen A between A-to-B and\n"
           "A-to-Vega, which no change of frame can move. Degrees, at 2050.\n");
    printf("%-20s %14s %12s\n", "frame", "angle", "B off(mas)");
    for (k = 0; k < cFrame; k++) {
      double xxLineA[6], xxA[6], xxLineB[6], xxB[6], xxV[6], a[3], b[3], v[3];
      char star[SE_MAX_STNAME * 2], serr[AS_MAXCH];
      const int32 iflag = iflagBase | rgFrame[k].iflag;
      double eB, nB, eV, nV, ang, d;

      if (!FPlaceOfIn("Rigil Kentaurus", 2050.0, iflag, xxLineA, xxA) ||
          !FPlaceOfIn("Toliman", 2050.0, iflag, xxLineB, xxB)) { fail++; continue; }
      snprintf(star, sizeof(star), "%s", "Vega");
      if (swe_fixstar2(star, JdOf(2050.0), iflag, xxV, serr) < 0) {
        printf("%-20s  Vega FAILED: %s\n", rgFrame[k].sz, serr); fail++; continue;
      }
      // Both directions in A's own tangent plane, then the angle between them.
      Sep(xxA, xxB, &eB, &nB);
      Sep(xxA, xxV, &eV, &nV);
      ang = atan2(eB, nB) - atan2(eV, nV);
      if (ang < 0.0) ang += 2.0 * PI;
      ang *= RADTODEG;
      if (k == 0) ang0 = ang;
      // Graded as POSITION, not as angle: turning A-to-B by an angle moves B
      // by that angle times the separation, and it is the position that the
      // wire carries. An angle of a degree here is 1.3 arcsec of alpha Cen B.
      d = (ang - ang0) * DEGTORAD * hypot(eB, nB);
      int bad = fabs(d) > 1.0;
      fail += bad;
      printf("%-20s %14.7f %12.3f%s\n", rgFrame[k].sz, ang, d,
             bad ? "  <-- BAD" : "");
      (void)a; (void)b; (void)v;
    }
  }

  printf("\n");
  if (fail == 0) {
    printf("STAR ORBIT APPLY PASS: every graded row within 1 mas of the other "
           "engine\n(worst %.3f mas), across two centuries and four stars.\n", worst);
    return 0;
  }
  printf("STAR ORBIT APPLY FAIL: %d rows\n", fail);
  return 1;
}
