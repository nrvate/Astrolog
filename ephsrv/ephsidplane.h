// ephsrv/ephsidplane.h -- A.8's fixed sidereal planes, the arithmetic only.
//
// EPHEMERIS_ACCURACY_REGISTRY.md 4.1. A sidereal zodiac's zero point is the
// direction the zodiac NAMES -- longitude A0 on the mean ecliptic and equinox
// of t0 -- projected onto the plane, with longitude counted from that
// projection. Swiss instead carries the equinox of t0 onto the plane and walks
// the ayanamsa there, which gives plane 2 an origin that is not the zodiac's
// and that depends on WHICH ayanamsa was asked for. A plane does not know
// that, so the origin was tied to the equinox rather than to the sky.
//
// This header holds the MATH and nothing else. Where the anchor comes from is
// policy and stays with each caller: astrolog-ephd reads it from the protocol
// profile or the Swiss ayanamsa table, and Astrolog's own Swiss path has only
// the one sidereal base it has always had. The math is shared because the two
// must agree to the last digit -- the suite's "sidereal on the solar system
// plane" leg compares a server cast against a local one and would report a
// divergence, but a shared routine cannot diverge in the first place.
//
// TWO SWISS COPIES, ONE PIECE OF MATHS. The server links the thread-safe fork
// at /shares/swisseph, whose swi_precess() and swi_epsiln() take a context;
// Astrolog compiles the vendored Swiss sources into its own binary, where they
// do not. Define EPHSID_FORK before including this to get the context forms.
// Nothing else differs: swi_coortrf(), swi_polcart_sp() and swi_cartpol_sp()
// take no context in either.
//
// Requires <math.h>, <string.h> and Swiss's swephexp.h, swephlib.h and sweph.h
// to have been included already -- sweph.h for J2000, J2000_TO_J and the
// SSY_PLANE_* constants, which are internal to Swiss and not in swephexp.h.

#ifndef EPHSIDPLANE_H
#define EPHSIDPLANE_H

#ifdef EPHSID_FORK
#define EPHSID_CTXDECL swe_ctx *ctx,
#define EPHSID_CTXARG ctx,
#define EPHSID_PRECESS(x, j, d) swi_precess(ctx, (x), (j), 0, (d))
#define EPHSID_EPSILN(j) swi_epsiln(ctx, (j), 0)
#define EPHSID_BIAS(x, j, f) swi_bias(ctx, (x), (j), (f), FALSE)
#else
#define EPHSID_CTXDECL
#define EPHSID_CTXARG
#define EPHSID_PRECESS(x, j, d) swi_precess((x), (j), 0, (d))
#define EPHSID_EPSILN(j) swi_epsiln((j), 0)
#define EPHSID_BIAS(x, j, f) swi_bias((x), (j), (f), FALSE)
#endif

// What a caller works out once per object and then applies to every row.
// A0 is the MEAN ayanamsa at t0 -- the arc from the MEAN equinox. Handing it
// the true one instead moves the origin by the nutation in longitude at t0,
// which is zodiac-dependent (-3.311" Fagan/Bradley, +16.777" Lahiri) and so
// looks exactly like the defect this code exists to remove. See
// tools/sidplane-anchor.sh, which is the gate that holds it.
typedef struct _EPHSIDPLANE {
  int plane;          // 1 = mean ecliptic of t0, 2 = the invariable plane
  double t0Et;        // the anchor epoch, TT
  double A0;          // the MEAN ayanamsa there, degrees
  double lonOrigin;   // plane 2 only: the zero point's longitude in the plane
} EPHSIDPLANE;

// A unit vector from spherical degrees.
static void SidSph2Rect(double lon, double lat, double *x)
{
  const double l = lon * DEGTORAD, b = lat * DEGTORAD;
  x[0] = cos(b) * cos(l); x[1] = cos(b) * sin(l); x[2] = sin(b);
}

// Ecliptic of J2000 -> the invariable plane's frame. Swiss's own constants,
// and its node is stated ON THE ECLIPTIC OF 2000 (sweph.h), which is why
// every direction here is carried to that frame first.
static void SidToInvariable(double *x)
{
  const double om = SSY_PLANE_NODE_E2000, i = SSY_PLANE_INCL;
  const double c0 = cos(om), s0 = sin(om), ci = cos(i), si = sin(i);
  const double px = x[0] * c0 + x[1] * s0, py = -x[0] * s0 + x[1] * c0;
  x[0] = px; x[1] = py * ci + x[2] * si; x[2] = -py * si + x[2] * ci;
}

// Ecliptic of J2000 -> the mean ecliptic and equinox of t0.
static void SidToAnchor(EPHSID_CTXDECL double t0Et, double *x)
{
  swi_coortrf(x, x, -EPHSID_EPSILN(J2000));   // -> equatorial J2000
  EPHSID_PRECESS(x, t0Et, J2000_TO_J);        // -> equatorial of t0
  swi_coortrf(x, x, EPHSID_EPSILN(t0Et));     // -> ecliptic of t0
}

// The zero point's longitude in the invariable plane, from the anchor. Called
// once; ApplySidPlane() subtracts it from every body.
static double SidPlaneOrigin(EPHSID_CTXDECL double t0Et, double A0)
{
  double d[3];

  SidSph2Rect(A0, 0.0, d);                    // the zero point, ecliptic of t0
  swi_coortrf(d, d, -EPHSID_EPSILN(t0Et));
  EPHSID_PRECESS(d, t0Et, J_TO_J2000);
  swi_coortrf(d, d, EPHSID_EPSILN(J2000));    // -> ecliptic of J2000
  SidToInvariable(d);
  return atan2(d[1], d[0]) * RADTODEG;
}

// A TROPICAL position in the mean ecliptic of J2000 -> sidereal on the plane.
// Speeds ride the same rotation; the origin is a constant, so it moves
// longitude and not its rate.
static void ApplySidPlane(EPHSID_CTXDECL const EPHSIDPLANE *psp, int fRect,
  double *xx)
{
  double v[6], pol[6], origin;

  if (fRect)
    memcpy(v, xx, sizeof(v));
  else {
    // swi_polcart_sp() and swi_cartpol_sp() work in RADIANS; Swiss answers a
    // position in degrees. Feeding degrees to them silently produces a
    // direction somewhere else entirely -- the first build of this put the
    // 1800 Moon node 34 degrees from where it belongs.
    memcpy(pol, xx, sizeof(pol));
    pol[0] *= DEGTORAD; pol[1] *= DEGTORAD; pol[3] *= DEGTORAD;
    pol[4] *= DEGTORAD;
    swi_polcart_sp(pol, v);
  }
  if (psp->plane == 2) {
    SidToInvariable(v); SidToInvariable(v + 3);
    origin = psp->lonOrigin;
  } else {
    SidToAnchor(EPHSID_CTXARG psp->t0Et, v);
    SidToAnchor(EPHSID_CTXARG psp->t0Et, v + 3);
    origin = psp->A0;
  }
  swi_cartpol_sp(v, pol);
  pol[0] = pol[0] * RADTODEG - origin;
  pol[0] = fmod(pol[0], 360.0);
  if (pol[0] < 0.0)
    pol[0] += 360.0;
  pol[1] *= RADTODEG; pol[3] *= RADTODEG; pol[4] *= RADTODEG;
  if (fRect) {
    double p2[6];
    memcpy(p2, pol, sizeof(p2));
    p2[0] *= DEGTORAD; p2[1] *= DEGTORAD; p2[3] *= DEGTORAD; p2[4] *= DEGTORAD;
    swi_polcart_sp(p2, xx);
  } else
    memcpy(xx, pol, sizeof(pol));
}

// A node's frame, per 3.5a as the 2026-09-18 drop amended it: a node lies on
// the mean ecliptic of DATE, and the profile's frame gives the coordinates it
// is expressed in rather than which point it is.
//
// Swiss handed a fixed frame answers a THIRD point. The of-date node's
// longitude comes back precessed and correct, and the latitude is neither the
// rotation's nor zero: +10.013" at 1800 where rotating the of-date node gives
// +61.358", and at 2100 not even the same sign. So the caller asks for the
// node with OF-DATE flags and rotates it here.
//
// Shared because BOTH paths need it and for the same reason: anything that
// asks Swiss with SEFLG_J2000 gets the third point, and A.8's fixed sidereal
// planes ask exactly that way. Astrolog's own "-Ys" reintroduced this defect
// the moment it started asking in J2000, and the suite's server-versus-local
// leg caught it as a node whose LATITUDE had moved -- which an origin shift
// cannot do.
//
// swi_precess is the same precession swe_calc gives the BODIES, which is the
// property that matters: a chart has to be internally consistent, and a model
// reimplemented here would disagree with our own planets before it disagreed
// with anyone else's.
static void RotateNodeToFixedFrame(EPHSID_CTXDECL double jdEt, int32 iflag,
  double *xx)
{
  double v[6], pol[6];
  const int fRect = (iflag & SEFLG_XYZ) != 0;
  const int fEqu = (iflag & SEFLG_EQUATORIAL) != 0;
  const double toRad = (iflag & SEFLG_RADIANS) ? 1.0 : DEGTORAD;

  if (fRect)
    memcpy(v, xx, sizeof(v));
  else {
    memcpy(pol, xx, sizeof(pol));
    pol[0] *= toRad; pol[1] *= toRad; pol[3] *= toRad; pol[4] *= toRad;
    swi_polcart_sp(pol, v);
  }
  // Precession is defined on the equator, so ecliptic input goes there and
  // back. The obliquity out is J2000's, not the instant's.
  if (!fEqu) {
    swi_coortrf(v, v, -EPHSID_EPSILN(jdEt));
    swi_coortrf(v + 3, v + 3, -EPHSID_EPSILN(jdEt));
  }
  EPHSID_PRECESS(v, jdEt, J_TO_J2000);
#ifdef EPHSID_FORK
  swi_precess_speed(ctx, v, jdEt, 0, J_TO_J2000);
#else
  swi_precess_speed(v, jdEt, 0, J_TO_J2000);
#endif
  if (iflag & SEFLG_ICRS) EPHSID_BIAS(v, jdEt, iflag);
  if (!fEqu) {
    swi_coortrf(v, v, EPHSID_EPSILN(J2000));
    swi_coortrf(v + 3, v + 3, EPHSID_EPSILN(J2000));
  }
  if (fRect)
    memcpy(xx, v, sizeof(v));
  else {
    swi_cartpol_sp(v, pol);
    pol[0] /= toRad; pol[1] /= toRad; pol[3] /= toRad; pol[4] /= toRad;
    memcpy(xx, pol, sizeof(pol));
  }
}

#endif // EPHSIDPLANE_H
