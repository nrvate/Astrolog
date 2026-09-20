// ephsrv/ephnodrate.h -- a MEAN node or apsis's rates, differenced.
//
// EPHEMERIS_ACCURACY_REGISTRY.md 1.5. swe_nod_aps() does not return rates for
// a MEAN point. Two faults, both measured against Swiss's own answers:
//
//   - THE LATITUDE-RATE SLOT HOLDS THE LATITUDE. Mars's mean perihelion at
//     J2000 comes back with lat -1.773509 and dlat -1.773507, where
//     differencing Swiss's own latitudes gives 2.83e-7 deg/day. Wrong by the
//     whole latitude -- 4.68 deg/day at worst, larger than the Sun's own
//     longitude rate.
//   - THE LONGITUDE RATE OMITS THE GEOCENTRIC RE-CENTRING: 0.387106 reported
//     against 0.401093 differenced for Mars, while HELIOCENTRICALLY the two
//     agree to 1.6e-7. So it is the re-centring's own rate that is missing,
//     not the mean elements -- registry 2.8's finding in a second place.
//
// 3.5a is normative -- a rate is "the time derivative of the coordinates
// answered in the other three columns" -- so replacing these is conformance,
// not preference.
//
// OSCULATING POINTS ARE LEFT AS SWISS GIVES THEM. Measured, they already
// agree with a difference of their own positions to about 1e-6 deg/day, so
// differencing them would move numbers for no gain and would difference a
// quantity that can jitter. Callers test for the mean method themselves;
// this header does not know what it was handed.
//
// WHY THIS IS A HEADER AND NOT TWO COPIES. It was two copies, for eighteen
// hours, and that is the whole reason this file exists. The fix landed in
// astrolog-ephd on 2026-09-19 (d1f1287) and touched ONLY the server, so the
// server answered a differenced rate while Astrolog's own Swiss path went on
// returning Swiss's raw one. The suite's "server cast bit-identical to the
// local one" leg caught it -- twenty failures, every one of them a North Node
// speed with the position beside it identical to the last bit -- which is the
// same shape as the plane-2 defect a day earlier, where the stars took
// Swiss's origin and the planets took ours. Arithmetic that two copies must
// agree on to the last digit belongs in one place, like ephsidplane.h and
// ephstarorb.h.
//
// THE STENCIL, and why these constants. Five-point central difference at
// h = 1/1024 day. Both are the other project's finding, and h being exactly
// representable in binary is load-bearing: at h = 0.001 the Julian day's own
// quantization costs 7e-7 deg/day, which is the size of the thing being
// measured. The centre point is not sampled -- the five-point formula does
// not use it -- so four extra calls buy all three columns.
//
// This does make tools/ephsrv-rates.sh tautological FOR THESE POINTS: a rate
// computed by differencing trivially matches a difference. That is inherent
// in 3.5a rather than a weakness here, and the independent check is the
// cross-test against an engine that does not difference.
//
// Requires <string.h> and Swiss's swephexp.h for SEFLG_RADIANS/SEFLG_XYZ.

#ifndef EPHNODRATE_H
#define EPHNODRATE_H

// How a caller hands back the point at an offset from the centre instant.
// It fills six doubles -- three coordinates and three rates, of which only
// the coordinates are read -- with the SAME point, the SAME flags and the
// SAME frame rotation the answered row was built with. Returning false for
// any offset abandons the whole differencing; see EphNodRateDiff().
typedef int (*PFNEPHNODPOINT)(void *pv, double dj, double *xx);

// Replace xx[3..5] with the time derivative of xx[0..2]. Returns non-zero if
// it did, zero if it left them exactly as they were.
//
// fRadians and fXyz come from the flags the row was computed with, and both
// only affect the wrap: a rectangular answer has no angle to wrap, and a
// radian one wraps at PI rather than 180.
//
// A STENCIL POINT THAT WILL NOT COMPUTE LEAVES SWISS'S RATES ALONE rather
// than inventing one from fewer points. Near an ephemeris file's edge some
// offsets answer and others do not, and a four-point formula evaluated on
// three points is not a worse rate, it is a wrong one that no longer looks
// wrong. The row is still answered, with its position intact.
static int EphNodRateDiff(PFNEPHNODPOINT pfn, void *pv, double *xx,
  int fRadians, int fXyz)
{
  const double h = 1.0 / 1024.0;
  double v[4][6];
  int k, kk;

  for (k = 0; k < 4; k++) {
    // -2h, -h, +h, +2h. The centre is skipped: the five-point central
    // difference does not use it.
    const double dj = (k < 2 ? -2.0 + (double)k : (double)k - 1.0) * h;
    if (!pfn(pv, dj, v[k]))
      return 0;
  }
  {
    const double half = fRadians ? 3.14159265358979323846 : 180.0;
    for (kk = 0; kk < 3; kk++) {
      double a = v[0][kk], b = v[1][kk], d = v[2][kk], e = v[3][kk];
      if (kk == 0 && !fXyz) {
        // The wrap, on the ONE column that has one. Walked forward along the
        // stencil rather than applied to each point against the centre,
        // because a point may sit either side of the seam from its neighbour
        // without any of them being near the centre's own value.
        while (b - a >  half) b -= 2.0 * half;
        while (b - a < -half) b += 2.0 * half;
        while (d - b >  half) d -= 2.0 * half;
        while (d - b < -half) d += 2.0 * half;
        while (e - d >  half) e -= 2.0 * half;
        while (e - d < -half) e += 2.0 * half;
      }
      xx[kk + 3] = (a - 8.0 * b + 8.0 * d - e) / (12.0 * h);
    }
  }
  return 1;
}

// The offsets the callback will be asked for, in order, so a caller that
// wants to pre-fetch or to bound a date range can see them without
// duplicating the stencil.
#define cEphNodRateStencil 4
static const double rgEphNodRateDj[cEphNodRateStencil] = {
  -2.0 / 1024.0, -1.0 / 1024.0, 1.0 / 1024.0, 2.0 / 1024.0
};

#endif // EPHNODRATE_H
