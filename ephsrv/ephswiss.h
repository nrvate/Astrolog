// ephswiss.h: protocol version 4 questions as Swiss Ephemeris calls.
//
// EPHEMERIS_PLUGINS_PLAN.md Appendix B, in code. One object in one profile
// becomes a SwissCall: which entry point, which body, which SEFLG bits,
// which sidereal mode, and what to tell the client about what was
// computed. Both ends of Astrolog use it -- astrolog-ephd answering the
// wire, and Astrolog's own swiss/moshier/jpl source plugins answering a
// local cast -- so the two can never disagree about what a body is.
//
// This header maps; it calls nothing. It needs only swephexp.h for the
// constants, so it compiles against upstream Swiss (the vendored copy in
// Astrolog's tree) and against the thread-safe fork alike.
//
// Canonical Swiss body first (Appendix B): a NAIF or SPK-ID that names one
// of Swiss's built-in bodies maps to that body, not to the generic
// asteroid or planetary-moon route, because the two read different files
// and their numbers differ. The host-only Object::nNative overrides the
// mapping when a local caller must reproduce an exact historical call
// (4.1): for bodies, hypotheticals and elements it is Swiss ipl + 1; for an
// orbit point, 1 forces swe_nod_aps over the named node/apogee bodies.
//
// Known limit: Swiss derives Earth rotation for topocentric positions from
// its own delta T whatever deltaTSec says; the ΔT capability names "swiss"
// so a client can tell (3.5).

#ifndef EPHSWISS_H
#define EPHSWISS_H

#include "ephproto4.h"
#include "swephexp.h"

#include <string>

namespace eph4 {
namespace swiss {

enum CallKind {
  kCallCalc = 0,     // swe_calc / swe_calc_ut
  kCallPctr = 1,     // swe_calc_pctr (planet-centred observer)
  kCallNodAps = 2,   // swe_nod_aps / swe_nod_aps_ut
  kCallFixstar = 3,  // swe_fixstar2 / swe_fixstar2_ut
  kCallElements = 4, // the fork's orbital-elements entry point
};

struct SwissCall {
  int kind = kCallCalc;
  int32_t ipl = 0;             // body (calc, pctr, nod_aps)
  int32_t iplCenter = -1;      // pctr observer body
  int32_t iflag = 0;           // SEFLG_* including the ephemeris bits
  int32_t nodMethod = 0;       // SE_NODBIT_*
  int point = 0;               // nod_aps: which of the four answers (0..3)
  bool fOpposite = false;      // named node body: add 180 deg (descending)
  bool fSidereal = false;
  int32_t sidMode = 0;         // swe_set_sid_mode triple
  double sidT0 = 0.0, sidAyanT0 = 0.0;
  bool fTopo = false;          // swe_set_topo(lon, lat, height)
  double topo[3] = {0.0, 0.0, 0.0};
  bool fUT = false;            // call the _ut entry point with UT1 instants
  bool fAddDeltaT = false;     // UT1 instants plus deltaTSec, then ET entry point
  std::string star;            // fixstar name
  int32_t resolvedNaif = kNaifNone;
  bool fApproximated = false;
};

// SEFLG_SWIEPH, SEFLG_MOSEPH or SEFLG_JPLEPH: the plugin's choice.
using EphFlag = int32_t;

// NAIF / SPK-ID -> Swiss body (Appendix B). Returns a per-object error code
// (A.17). *pflagExtra receives SEFLG_CENTER_BODY for body centres.
inline uint16_t BodyFromNaif(int32_t naif, int32_t *pipl, int32_t *pflagExtra,
                             int32_t *presolved, bool *pfApprox) {
  *pflagExtra = 0;
  *presolved = naif;
  *pfApprox = false;
  if (naif == 10) { *pipl = SE_SUN; return kOErrNone; }
  if (naif == 301) { *pipl = SE_MOON; return kOErrNone; }
  if (naif == 199 || naif == 1) { *pipl = SE_MERCURY; return kOErrNone; }
  if (naif == 299 || naif == 2) { *pipl = SE_VENUS; return kOErrNone; }
  if (naif == 399) { *pipl = SE_EARTH; return kOErrNone; }
  if (naif == 0 || naif == 3) return kOErrUnsupported;       // SSB, EMB
  if (naif >= 4 && naif <= 9) { *pipl = SE_MARS + (naif - 4); return kOErrNone; }
  if (naif >= 499 && naif <= 999 && naif % 100 == 99) {
    // Body centres, from the planetary-moon files (sepm*.se1).
    *pipl = SE_MARS + (naif / 100 - 4);
    *pflagExtra = SEFLG_CENTER_BODY;
    return kOErrNone;
  }
  if (naif >= 401 && naif <= 998 && naif % 100 != 0 && naif % 100 != 99) {
    *pipl = SE_PLMOON_OFFSET + naif;   // planetary moons
    return kOErrNone;
  }
  if (naif > 20000000 && naif < 2147483647) {
    int32_t n = naif - 20000000;
    if (n >= 1 && n <= 4) { *pipl = SE_CERES + (n - 1); return kOErrNone; }
    if (n == 2060) { *pipl = SE_CHIRON; return kOErrNone; }
    if (n == 5145) { *pipl = SE_PHOLUS; return kOErrNone; }
    if (n == 134340) {   // Pluto's asteroid number: Swiss answers the planet
      *pipl = SE_PLUTO;
      *presolved = 9;
      *pfApprox = true;
      return kOErrNone;
    }
    // Swiss computes ipl*100 + 9099 for a centre-of-body lookup and indexes
    // by ipl: past this bound that is undefined behaviour (ephproto.h
    // kObjIdMax, EPHEMERIS_REVIEW.md S-fork).
    if ((int64_t)SE_AST_OFFSET + n > (0x7FFFFFFF - 9099) / 100) return kOErrUnknownBody;
    *pipl = SE_AST_OFFSET + n;
    return kOErrNone;
  }
  return kOErrUnknownBody;
}

// The profile's options as SEFLG bits and sidereal/topocentric state.
inline uint16_t ApplyProfile(const Profile &pf, uint8_t timeScale, double deltaTSec,
                             SwissCall *c, std::string *why) {
  int32_t f = 0;
  if (pf.speeds) f |= SEFLG_SPEED;
  switch (pf.observer) {
    case kObsGeo: break;
    case kObsTopo:
      f |= SEFLG_TOPOCTR;
      c->fTopo = true;
      c->topo[0] = pf.siteLonEastDeg;
      c->topo[1] = pf.siteLatDeg;
      c->topo[2] = pf.siteHeightM;
      break;
    case kObsHelio: f |= SEFLG_HELCTR; break;
    case kObsBary: f |= SEFLG_BARYCTR; break;
    case kObsBody: break;   // the caller maps observerBody (pctr)
    default: *why = "observer"; return kOErrUnsupported;
  }
  if (pf.plane == kPlaneEquator) f |= SEFLG_EQUATORIAL;
  if (pf.form == kFormRectangular) f |= SEFLG_XYZ;
  switch (pf.frame) {
    case kFrameTrueOfDate: break;
    case kFrameMeanOfDate: f |= SEFLG_NONUT; break;
    case kFrameJ2000: f |= SEFLG_J2000 | SEFLG_NONUT; break;
    case kFrameIcrf: f |= SEFLG_ICRS | SEFLG_J2000 | SEFLG_NONUT; break;
    default: *why = "frame"; return kOErrUnsupported;
  }
  // The correction masks Swiss can honour (A.3 tag 0x0004).
  switch (pf.corrections) {
    case kCorrMask: break;
    case 0: f |= SEFLG_TRUEPOS; break;
    case kCorrLightTime | kCorrDeflection: f |= SEFLG_NOABERR; break;
    case kCorrLightTime | kCorrAberration: f |= SEFLG_NOGDEFL; break;
    case kCorrLightTime: f |= SEFLG_NOABERR | SEFLG_NOGDEFL; break;
    default: *why = "Swiss cannot switch light time off alone"; return kOErrUnsupported;
  }
  if (!pf.zodiac.empty()) {
    int idx = ZodiacIndex(pf.zodiac);
    if (idx < 0) { *why = "zodiac"; return kOErrUnsupported; }
    c->fSidereal = true;
    f |= SEFLG_SIDEREAL;
    if (ZodiacIsUser(pf.zodiac)) {
      c->sidMode = SE_SIDM_USER;
      c->sidT0 = pf.anchorEpoch.Sum();
      c->sidAyanT0 = pf.anchorAyanamsaDeg;
    } else {
      c->sidMode = idx;
    }
    if (pf.siderealPlane == kSidPlaneAnchor) c->sidMode |= SE_SIDBIT_ECL_T0;
    else if (pf.siderealPlane == kSidPlaneInvariable) c->sidMode |= SE_SIDBIT_SSY_PLANE;
    else if (pf.siderealPlane != kSidPlaneDate) { *why = "sidereal plane"; return kOErrUnsupported; }
  }
  switch (timeScale) {
    case kTimeTT: break;
    case kTimeUT1:
      if (IsCanonicalNaN(deltaTSec)) c->fUT = true;
      else c->fAddDeltaT = true;
      break;
    default: *why = "Swiss takes UT1 or TT, not TDB"; return kOErrUnsupported;
  }
  c->iflag = f;
  return kOErrNone;
}

// One object in its profile. eph is SEFLG_SWIEPH, SEFLG_MOSEPH or
// SEFLG_JPLEPH. Returns a per-object error code (A.17); *why says what,
// without quoting instants or places (3.8).
inline uint16_t MapObject(const Object &o, const Profile &pf, uint8_t timeScale,
                          double deltaTSec, EphFlag eph, SwissCall *c, std::string *why) {
  *c = SwissCall();
  uint16_t err = ApplyProfile(pf, timeScale, deltaTSec, c, why);
  if (err) return err;
  c->iflag |= eph;
  int32_t extra = 0;
  switch (o.kind) {
    case kObjBody: {
      if (o.nNative > 0) {
        c->ipl = o.nNative - 1;
        c->resolvedNaif = o.naif;
      } else {
        err = BodyFromNaif(o.naif, &c->ipl, &extra, &c->resolvedNaif, &c->fApproximated);
        if (err) { *why = "no Swiss body for this id"; return err; }
      }
      c->iflag |= extra;
      break;
    }
    case kObjOrbitPoint: {
      c->resolvedNaif = o.naif;
      if (o.point > kOrbitPointMax || o.method > kOrbitMethodMax) {
        *why = "orbit point";
        return kOErrUnsupported;
      }
      // The Moon's named node and apogee bodies (what Astrolog's built-in
      // objects have always used), unless the caller forces swe_nod_aps.
      if (o.naif == 301 && o.nNative != 1) {
        int32_t named = -1;
        if (o.point == kPtAscNode || o.point == kPtDescNode) {
          if (o.method == kMethMean) named = SE_MEAN_NODE;
          else if (o.method == kMethOsculating) named = SE_TRUE_NODE;
          c->fOpposite = o.point == kPtDescNode;
        } else if (o.point == kPtApo) {
          if (o.method == kMethMean) named = SE_MEAN_APOG;
          else if (o.method == kMethOsculating) named = SE_OSCU_APOG;
          else if (o.method == kMethInterpolated) named = SE_INTP_APOG;
        } else if (o.point == kPtPeri && o.method == kMethInterpolated) {
          named = SE_INTP_PERG;
        }
        if (named >= 0) {
          c->kind = kCallCalc;
          c->ipl = named;
          // Nodes and apsides of the Moon are geocentric points; Swiss
          // refuses heliocentric flags on them, as it always has.
          break;
        }
      }
      if (o.method == kMethInterpolated) {
        *why = "interpolated points exist only for the Moon's apsides";
        return kOErrUnsupported;
      }
      int32_t ipl, resolved;
      bool fApprox;
      err = BodyFromNaif(o.naif, &ipl, &extra, &resolved, &fApprox);
      if (err || extra) { *why = "no Swiss body for this orbit"; return err ? err : (uint16_t)kOErrUnsupported; }
      c->kind = kCallNodAps;
      c->ipl = ipl;
      c->point = o.point;
      switch (o.method) {
        case kMethMean: c->nodMethod = SE_NODBIT_MEAN; break;
        case kMethOsculating: c->nodMethod = SE_NODBIT_OSCU; break;
        case kMethOscuBary: c->nodMethod = SE_NODBIT_OSCU_BAR; break;
        case kMethFocal:
          if (o.point != kPtApo) { *why = "the focal point replaces the aphelion only"; return kOErrUnsupported; }
          c->nodMethod = SE_NODBIT_OSCU | SE_NODBIT_FOPOINT;
          break;
        default: *why = "orbit method"; return kOErrUnsupported;
      }
      break;
    }
    case kObjStar:
      c->kind = kCallFixstar;
      c->star = o.name;
      break;
    case kObjHypothetical: {
      int idx = -1;
      for (int i = 0; i < kHypotheticalTokenCount; i++)
        if (o.name == kHypotheticalTokens[i]) idx = i;
      if (o.nNative > 0) c->ipl = o.nNative - 1;
      else if (idx >= 0) c->ipl = SE_FICT_OFFSET + idx;
      else { *why = "unknown hypothetical body"; return kOErrUnknownBody; }
      break;
    }
    case kObjElements:
      c->kind = kCallElements;
      break;
    case kObjDesignation: {
      // Plain numbers are numbered asteroids; names need a lookup table the
      // caller owns (LOOKUP, 3.4).
      const std::string &s = o.name;
      bool fDigits = !s.empty() && s.size() <= 9;
      for (char ch : s) fDigits = fDigits && ch >= '0' && ch <= '9';
      if (!fDigits) { *why = "designation not found"; return kOErrUnknownBody; }
      int32_t n = std::stoi(s);
      if (n <= 0) { *why = "designation not found"; return kOErrUnknownBody; }
      err = BodyFromNaif(20000000 + n, &c->ipl, &extra, &c->resolvedNaif, &c->fApproximated);
      if (err) { *why = "designation not found"; return err; }
      break;
    }
    default:
      *why = "object kind";
      return kOErrUnsupported;
  }
  if (pf.observer == kObsBody) {
    int32_t iplC, resolvedC;
    bool fApproxC;
    err = BodyFromNaif(pf.observerBody, &iplC, &extra, &resolvedC, &fApproxC);
    if (err || extra || c->kind != kCallCalc) {
      *why = "no Swiss body for this observer";
      return err ? err : (uint16_t)kOErrUnsupported;
    }
    if (iplC == c->ipl) { *why = "the observer is the body"; return kOErrUnsupported; }
    c->kind = kCallPctr;
    c->iplCenter = iplC;
  }
  return kOErrNone;
}

// A.11: the Swiss sidereal mode token for SE_SIDM_* number n, or NULL.
inline const char *ZodiacTokenForSwissMode(int n) {
  if (n >= 0 && n < kZodiacTokenCount - 1) return kZodiacTokens[n];
  if (n == SE_SIDM_USER) return "user";
  return nullptr;
}

}  // namespace swiss
}  // namespace eph4

#endif  // EPHSWISS_H
