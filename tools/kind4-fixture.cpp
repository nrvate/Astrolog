// The kind-4 elements fixture (EPHEMERIS_PLUGINS_PLAN.md 3.5a, the
// elements-rule drop). Built and run by tools/kind4-fixture.sh, which
// writes the element sets this reads.
//
// Check the kind-4 elements rule against the vendored Swiss Ephemeris.
// Geometric (TRUEPOS, NONUT), rectangular (XYZ), mean ecliptic and equinox
// of J2000, from the orbit's own origin -- heliocentric for the Sun-origin
// cases, geocentric for the Earth-origin one. That is the setup in which no
// ephemeris position enters and the answer is pure two-body mechanics.
#include <stdio.h>
#include <string.h>
#include "swephexp.h"

static void Row(const char *szName, int idx, double jd, int fGeo)
{
  double xx[6];
  char serr[AS_MAXCH];
  int32 iflag = SEFLG_SWIEPH | SEFLG_J2000 | SEFLG_XYZ |
    SEFLG_TRUEPOS | SEFLG_NONUT;

  if (!fGeo)
    iflag |= SEFLG_HELCTR;
  if (swe_calc(jd, SE_FICT_OFFSET_1 + idx, iflag, xx, serr) < 0) {
    printf("%-6s %.1f  ERROR %s\n", szName, jd, serr);
    return;
  }
  printf("%-6s %.1f  %.17g  %.17g  %.17g\n", szName, jd, xx[0], xx[1], xx[2]);
}

int main(int argc, char **argv)
{
  if (argc < 2) {
    fprintf(stderr, "usage: kind4-fixture <dir holding seorbel.txt>\n");
    return 2;
  }
  swe_set_ephe_path(argv[1]);
  Row("A", 1, 2415020.0, 0);
  Row("A", 1, 2451545.0, 0);
  Row("B", 2, 2415020.0, 0);
  Row("B", 2, 2451545.0, 0);
  Row("C", 3, 2415020.0, 0);
  Row("C", 3, 2451545.0, 0);
  Row("D", 4, 2451545.0, 1);
  Row("D", 4, 2488070.0, 1);
  swe_close();
  return 0;
}
