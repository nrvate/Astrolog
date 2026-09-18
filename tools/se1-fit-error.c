/*
** tools/se1-fit-error.c -- how far the compressed .se1 files sit from the
** JPL binary they were compressed FROM, measured with the same library.
**
**   cc -O2 -I/shares/swisseph -o se1-fit-error tools/se1-fit-error.c \
**      /shares/swisseph/libswe.a -lm
**   ./se1-fit-error [jpl-file-name] [ephe-path]
**
** Run by hand. It answers one question that came out of the cross-test with
** Ephemeris Prometheia and could not be answered by reading: their harness
** judged agreement against a flat 2 mas band, and Venus at its 2020
** inferior conjunction sat just outside it on our side while theirs was 2
** microarcseconds. Is that our data being wrong, or the band being the
** wrong shape?
**
** The Swiss Ephemeris documentation states the compression's fidelity as an
** ANGLE with no qualifier: "the compressed version agrees with the JPL
** Ephemeris to 1 milli-arcsecond (0.001")" (doc/swisseph.htm, 2.1.1). That
** cannot hold uniformly if the residual is a fixed DISTANCE, because the
** same kilometres subtend a bigger angle the closer a body comes -- and
** this measures which it is.
**
** Both sides come from Swiss itself: SEFLG_SWIEPH reads the .se1 files,
** SEFLG_JPLEPH reads the DE binary, one process, one set of corrections. So
** the difference is the compression and nothing else -- no second engine's
** conventions, no Horizons pipeline, no delta-T model in between.
**
** Note swe_set_jpl_file() takes a NAME LOOKED UP ON THE EPHEMERIS PATH, not
** a path of its own: an absolute path there fails with "JPL ephemeris is
** not available", which reads like a missing file rather than a misuse.
*/

#include <stdio.h>
#include <math.h>
#include "swephexp.h"

/* An angular separation, not a longitude difference: these bodies reach
** high latitudes and a longitude difference there is a projection. */
static double SepArcsec(const double *a, const double *b)
{
  double ca = cos(a[1] * M_PI / 180.0), cb = cos(b[1] * M_PI / 180.0);
  double x = (a[0] - b[0]) * M_PI / 180.0, y = (a[1] - b[1]) * M_PI / 180.0;
  x *= 0.5 * (ca + cb);
  return sqrt(x * x + y * y) * 180.0 / M_PI * 3600.0;
}

int main(int argc, char **argv)
{
  static const struct { const char *sz; int ipl; double jd; } rgcase[] = {
    { "Venus at its 2020 inferior conjunction", SE_VENUS,   2459004.0 },
    { "Venus at J2000",                         SE_VENUS,   2451545.0 },
    { "Venus at superior conjunction 2021",     SE_VENUS,   2459280.0 },
    { "Mercury at J2000",                       SE_MERCURY, 2451545.0 },
    { "Mercury in 2100",                        SE_MERCURY, 2488070.0 },
    { "Mars at the 2003 opposition",            SE_MARS,    2452880.0 },
    { "Mars at J2000",                          SE_MARS,    2451545.0 },
    { "Jupiter at J2000",                       SE_JUPITER, 2451545.0 },
    { "Saturn at J2000",                        SE_SATURN,  2451545.0 },
    { "the Moon at J2000",                      SE_MOON,    2451545.0 },
  };
  const char *szJpl = argc > 1 ? argv[1] : "linux_p1550p2650.440";
  const char *szPath = argc > 2 ? argv[2] :
    "./ephem:.:/shares/ephemeris-prometheia/ephe";
  char serr[AS_MAXCH];
  int i;

  swe_set_ephe_path((char *)szPath);
  swe_set_jpl_file((char *)szJpl);
  printf("  %-40s %10s %9s %9s\n", "case", "angle", "distance", "position");
  for (i = 0; i < (int)(sizeof(rgcase) / sizeof(rgcase[0])); i++) {
    double xs[6], xj[6], rSep, rKm;
    if (swe_calc(rgcase[i].jd, rgcase[i].ipl, SEFLG_SWIEPH | SEFLG_SPEED,
      xs, serr) < 0) {
      printf("  %-40s .se1: %s\n", rgcase[i].sz, serr);
      continue;
    }
    if (swe_calc(rgcase[i].jd, rgcase[i].ipl, SEFLG_JPLEPH | SEFLG_SPEED,
      xj, serr) < 0) {
      printf("  %-40s JPL: %s\n", rgcase[i].sz, serr);
      continue;
    }
    rSep = SepArcsec(xs, xj);
    /* The same residual as a distance, which is the number that tells you
    ** whether the angle is the right way to state it at all. */
    rKm = rSep / 206264.806 * xs[2] * 149597870.7;
    printf("  %-40s %7.4f mas %6.3f AU %7.3f km\n", rgcase[i].sz,
      rSep * 1000.0, xs[2], rKm);
  }
  swe_close();
  return 0;
}
