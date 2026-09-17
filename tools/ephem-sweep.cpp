// tools/ephem-sweep.cpp -- the bundled planet and Moon files agree with
// Moshier over their whole range.
//
//   ephem-sweep <ephemeris-dir>
//
// Built and run by tools/check-ephem.sh, against the vendored Swiss sources.
//
// Moshier is the independent reference: it is computed from analytical
// theories compiled into the library and reads no file at all. So a
// Swiss Ephemeris file that disagrees with it has bad data. Measured on
// 2026-09-16 (EPHEMERIS_REVIEW.md B3): the ephem/sepl_18.se1 then in the
// bundle put Mercury up to 104 degrees out over 1800-01-01..03-20, while
// the worst a good file reached against Moshier anywhere in 1800-2400 was
// 0.0024 degrees. The file passed check-ephem.sh's header test, and nothing
// compared its numbers with anything -- the suite, the oracle and every
// differential ask about dates where it was fine.
//
// The threshold sits between those two: 0.01 degrees, four times the worst
// honest disagreement and four orders of magnitude below the defect.
//
// Every sample has to come back from the file. A missing or unreadable file
// makes Swiss fall back to Moshier, which would then agree with itself, so
// that is a failure here rather than a pass.
//
// Not covered: the asteroid files. Moshier has no theory for them, so the
// reference this uses does not exist for them.

#include <cmath>
#include <cstdio>
#include <vector>
#include "swephexp.h"

int main(int argc, char **argv)
{
  static const int rgobj[] = { SE_SUN, SE_MOON, SE_MERCURY, SE_VENUS, SE_MARS,
    SE_JUPITER, SE_SATURN, SE_URANUS, SE_NEPTUNE, SE_PLUTO };
  const int cobj = sizeof(rgobj) / sizeof(*rgobj);
  // sepl_18/semo_18 cover 1800-01-01 to 2399-12-31; start a day in, since
  // the light-time step at the very first instant reaches before the file.
  const double jdLo = 2378497.5, jdHi = 2597640.5, dStep = 2.3;
  const double rMaxDeg = 0.01;
  char serr[AS_MAXCH];
  double xs[6], xm[6], rgworst[cobj] = { 0 }, rgjd[cobj] = { 0 };
  long cSample = 0, cBad = 0, cNoFile = 0;

  if (argc != 2) {
    fprintf(stderr, "usage: ephem-sweep <ephemeris-dir>\n");
    return 2;
  }
  swe_set_ephe_path(argv[1]);
  // Two passes, Swiss then Moshier, never interleaved: every change of
  // ephemeris flag makes swe_calc() close and reopen the files, which took
  // this from 8 seconds to 51.
  const long cjd = (long)((jdHi - jdLo) / dStep) + 1;
  std::vector<double> rglon((size_t)cjd * cobj);
  for (long k = 0; k < cjd; k++) {
    double jd = jdLo + k * dStep;
    for (int i = 0; i < cobj; i++) {
      int32 ret = swe_calc(jd, rgobj[i], SEFLG_SWIEPH, xs, serr);
      cSample++;
      if (ret < 0 || !(ret & SEFLG_SWIEPH)) {
        if (cNoFile++ < 5)
          printf("NOT FROM THE FILE: body %d at JD %.1f: %s\n", rgobj[i], jd,
            serr);
        rglon[k * cobj + i] = NAN;
      } else
        rglon[k * cobj + i] = xs[0];
    }
  }
  for (long k = 0; k < cjd; k++) {
    double jd = jdLo + k * dStep;
    for (int i = 0; i < cobj; i++) {
      double lon = rglon[k * cobj + i];
      if (std::isnan(lon))
        continue;
      if (swe_calc(jd, rgobj[i], SEFLG_MOSEPH, xm, serr) < 0) {
        printf("Moshier failed: body %d at JD %.1f: %s\n", rgobj[i], jd, serr);
        return 2;
      }
      double d = fabs(lon - xm[0]);
      if (d > 180.0)
        d = 360.0 - d;
      if (d > rgworst[i]) {
        rgworst[i] = d;
        rgjd[i] = jd;
      }
      if (d > rMaxDeg)
        cBad++;
    }
  }
  int fBad = cBad > 0 || cNoFile > 0;
  for (int i = 0; i < cobj; i++)
    if (fBad || rgworst[i] > rMaxDeg)
      printf("  %-8s worst %.4g deg at JD %.1f\n", swe_get_planet_name(rgobj[i],
        serr), rgworst[i], rgjd[i]);
  if (fBad) {
    printf("== %ld of %ld samples over %.2g deg from Moshier, %ld not from "
      "the file\n", cBad, cSample, rMaxDeg, cNoFile);
    return 1;
  }
  double rWorst = 0.0;
  for (int i = 0; i < cobj; i++)
    rWorst = rgworst[i] > rWorst ? rgworst[i] : rWorst;
  printf("%ld samples of %d bodies within %.2g deg of Moshier (worst %.2g)\n",
    cSample, cobj, rMaxDeg, rWorst);
  swe_close();
  return 0;
}
