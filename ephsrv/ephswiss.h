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
// and their numbers differ. MapObject()'s nNative argument overrides the
// mapping when a LOCAL caller must reproduce an exact historical call
// (4.1): for bodies, hypotheticals and elements it is Swiss ipl + 1; for an
// orbit point, 1 forces swe_nod_aps over the named node/apogee bodies. It
// is a host-side hint, never a wire field -- ephproto.h is the protocol and
// nothing else -- so a server answering the wire passes 0.
//
// Known limit: Swiss derives Earth rotation for topocentric positions from
// its own delta T whatever deltaTSec says; the ΔT capability names "swiss"
// so a client can tell (3.5).

#ifndef EPHSWISS_H
#define EPHSWISS_H

#include "ephproto.h"
#include "swephexp.h"

#include <string>

namespace eph {
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
inline uint16_t MapObject(const Object &o, int32_t nNative, const Profile &pf,
                          uint8_t timeScale, double deltaTSec, EphFlag eph, SwissCall *c,
                          std::string *why) {
  *c = SwissCall();
  uint16_t err = ApplyProfile(pf, timeScale, deltaTSec, c, why);
  if (err) return err;
  c->iflag |= eph;
  int32_t extra = 0;
  switch (o.kind) {
    case kObjBody: {
      if (nNative > 0) {
        c->ipl = nNative - 1;
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
      // 3.5a: the Sun and the solar-system barycentre have no orbit about
      // their centre. (Swiss answers the Sun's as the Earth's, which is a
      // different point from the one asked for.)
      if (o.naif == 10 || o.naif == 0) {
        *why = "this body has no orbit about its centre";
        return kOErrUnsupported;
      }
      // Method 3 is pinned to the ephemeris's total solar-system GM (3.5a),
      // and what Swiss's SE_NODBIT_OSCU_BAR uses is not verified, so this
      // mapping does not offer it rather than answer a differently defined
      // point.
      if (o.method == kMethOscuBary) {
        *why = "barycentric osculating elements are not served";
        return kOErrUnsupported;
      }
      // The Moon's named node and apogee bodies (what Astrolog's built-in
      // objects have always used), unless the caller forces swe_nod_aps.
      if (o.naif == 301 && nNative != 1) {
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
      if (nNative > 0) c->ipl = nNative - 1;
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

// ---- The other direction: a Swiss call as a version 4 question ---------------
//
// What a client that decides in Swiss terms -- Astrolog's FSwissPlanetSpec()
// -- puts on the wire, so a server asked the question makes the call the
// client would have made locally. Exact for every body, observer and flag
// Astrolog computes with, with two documented exceptions that follow from
// Appendix B choosing the canonical body: an asteroid numbered 1-4, 2060,
// 5145 or 134340 asked by number (SE_AST_OFFSET+N) is answered from the
// named body's file, and the Moon's ascending or descending node or apogee
// asked through swe_nod_aps is answered by the named node and apogee
// bodies. Both name the same point in the sky; neither is the same bits.

// A plain Swiss body as a NAIF/SPK-ID. False for the named lunar points and
// hypotheticals (ObjectFromSwiss handles those) and for ids with no form.
inline bool NaifFromSwissBody(int32_t ipl, int32_t *pnaif) {
  if (ipl == SE_SUN) *pnaif = 10;
  else if (ipl == SE_MOON) *pnaif = 301;
  else if (ipl == SE_MERCURY) *pnaif = 199;
  else if (ipl == SE_VENUS) *pnaif = 299;
  else if (ipl >= SE_MARS && ipl <= SE_PLUTO) *pnaif = 4 + (ipl - SE_MARS);
  else if (ipl == SE_EARTH) *pnaif = 399;
  else if (ipl == SE_CHIRON) *pnaif = 20002060;
  else if (ipl == SE_PHOLUS) *pnaif = 20005145;
  else if (ipl >= SE_CERES && ipl <= SE_VESTA) *pnaif = 20000001 + (ipl - SE_CERES);
  else if (ipl > SE_AST_OFFSET && (int64_t)ipl <= (0x7FFFFFFF - 9099) / 100)
    *pnaif = 20000000 + (ipl - SE_AST_OFFSET);
  else if (ipl > SE_PLMOON_OFFSET + 400 && ipl < SE_PLMOON_OFFSET + 1000 && (ipl % 100) != 0)
    *pnaif = ipl - SE_PLMOON_OFFSET;
  else
    return false;
  return true;
}

// One Swiss object -- a body, or with point 0..3 (A.13) one of its
// swe_nod_aps answers by nodMethod -- as a wire object in profile 0.
inline bool ObjectFromSwiss(int32_t ipl, int point, int32_t nodMethod, Object *o) {
  *o = Object();
  if (point >= 0) {
    if (point > kOrbitPointMax || !NaifFromSwissBody(ipl, &o->naif)) return false;
    o->kind = kObjOrbitPoint;
    o->point = (uint8_t)point;
    if (o->naif == 10 || o->naif == 0) return false;   // no orbit (3.5a)
    if (nodMethod == SE_NODBIT_MEAN) o->method = kMethMean;
    else if (nodMethod == SE_NODBIT_OSCU) o->method = kMethOsculating;
    else if (nodMethod == (SE_NODBIT_OSCU | SE_NODBIT_FOPOINT) && point == kPtApo) o->method = kMethFocal;
    else return false;
    return true;
  }
  struct { int32_t ipl; uint8_t point, method; } const kNamed[] = {
    {SE_MEAN_NODE, kPtAscNode, kMethMean}, {SE_TRUE_NODE, kPtAscNode, kMethOsculating},
    {SE_MEAN_APOG, kPtApo, kMethMean}, {SE_OSCU_APOG, kPtApo, kMethOsculating},
    {SE_INTP_APOG, kPtApo, kMethInterpolated}, {SE_INTP_PERG, kPtPeri, kMethInterpolated},
  };
  for (const auto &n : kNamed)
    if (ipl == n.ipl) {
      o->kind = kObjOrbitPoint;
      o->naif = 301;
      o->point = n.point;
      o->method = n.method;
      return true;
    }
  if (ipl >= SE_FICT_OFFSET && ipl < SE_FICT_OFFSET + kHypotheticalTokenCount) {
    o->kind = kObjHypothetical;
    o->name = kHypotheticalTokens[ipl - SE_FICT_OFFSET];
    return true;
  }
  o->kind = kObjBody;
  return NaifFromSwissBody(ipl, &o->naif);
}

// SEFLG bits, a swe_calc_pctr() centre (-1 for none), a swe_set_sid_mode()
// mode (its t0 and ayan_t0 zero) and a swe_set_topo() site, as a profile.
// False for a combination version 4 has no form for.
inline bool ProfileFromSwiss(int32_t iflag, int32_t iplCenter, int32_t sidMode,
                             const double topo[3], Profile *pf) {
  const int32_t kKnown = SEFLG_JPLEPH | SEFLG_SWIEPH | SEFLG_MOSEPH | SEFLG_SPEED |
    SEFLG_SIDEREAL | SEFLG_NONUT | SEFLG_TRUEPOS | SEFLG_HELCTR | SEFLG_BARYCTR |
    SEFLG_TOPOCTR | SEFLG_NOABERR | SEFLG_NOGDEFL | SEFLG_J2000 | SEFLG_ICRS |
    SEFLG_EQUATORIAL | SEFLG_XYZ;
  *pf = Profile();
  if (iflag & ~kKnown) return false;
  bool fHelioFlag = (iflag & (SEFLG_HELCTR | SEFLG_BARYCTR)) != 0;
  if (iplCenter >= 0) {
    // swe_calc_pctr() drops the helio- and barycentric bits after they
    // have turned aberration and deflection off.
    if (iflag & SEFLG_TOPOCTR) return false;
    Object oc;
    if (!ObjectFromSwiss(iplCenter, -1, 0, &oc) || oc.kind != kObjBody) return false;
    pf->observer = kObsBody;
    pf->observerBody = oc.naif;
  } else if (iflag & SEFLG_TOPOCTR) {
    // Swiss turns the helio- and barycentric bits off under this one.
    if (!(std::fabs(topo[0]) <= 180.0 && std::fabs(topo[1]) <= 90.0 && std::isfinite(topo[2])))
      return false;
    pf->observer = kObsTopo;
    pf->siteLonEastDeg = topo[0];
    pf->siteLatDeg = topo[1];
    pf->siteHeightM = topo[2];
  } else if (iflag & SEFLG_BARYCTR) {
    pf->observer = kObsBary;
  } else if (iflag & SEFLG_HELCTR) {
    pf->observer = kObsHelio;
  }
  if (iflag & SEFLG_TRUEPOS) {
    pf->corrections = 0;
  } else {
    uint8_t m = kCorrMask;
    if (iflag & SEFLG_NOABERR) m &= (uint8_t)~kCorrAberration;
    if (iflag & SEFLG_NOGDEFL) m &= (uint8_t)~kCorrDeflection;
    // The mask is what the caller's flags SAY, not what Swiss will make of
    // them: swe_calc() and swe_calc_pctr() turn aberration and deflection
    // off for a heliocentric, barycentric or planet-centred call
    // themselves, but swe_nod_aps() honours the bits as given, so
    // narrowing the mask here would move every orbit point of a
    // heliocentric chart (measured at 5.8e-3 degrees for Jupiter's node).
    // One planet-centred case must still be narrowed: Astrolog reaches
    // swe_calc_pctr() with the heliocentric bit set, which Swiss trades for
    // exactly that pair before it computes anything.
    if (pf->observer == kObsBody && fHelioFlag) m = kCorrLightTime;
    pf->corrections = m;
  }
  pf->plane = (iflag & SEFLG_EQUATORIAL) ? kPlaneEquator : kPlaneEcliptic;
  pf->form = (iflag & SEFLG_XYZ) ? kFormRectangular : kFormSpherical;
  if (iflag & SEFLG_ICRS) {
    if (!(iflag & SEFLG_J2000)) return false;
    pf->frame = kFrameIcrf;
  } else if (iflag & SEFLG_J2000) {
    pf->frame = kFrameJ2000;
  } else if (iflag & SEFLG_NONUT) {
    pf->frame = kFrameMeanOfDate;
  }
  pf->speeds = (iflag & SEFLG_SPEED) ? 1 : 0;
  if (iflag & SEFLG_SIDEREAL) {
    const char *tok = ZodiacTokenForSwissMode(sidMode % SE_SIDBITS);
    int32_t bits = sidMode - sidMode % SE_SIDBITS;
    if (tok == nullptr || sidMode < 0 || sidMode % SE_SIDBITS == SE_SIDM_USER) return false;
    pf->zodiac = tok;
    if (bits == SE_SIDBIT_SSY_PLANE) pf->siderealPlane = kSidPlaneInvariable;
    else if (bits == SE_SIDBIT_ECL_T0) pf->siderealPlane = kSidPlaneAnchor;
    else if (bits != 0) return false;
  }
  return true;
}

// Two SEFLG sets that Swiss computes identically: what plaus_iflag() adds
// and drops, applied to both, and the ephemeris bits and the plane bit's
// dependants left alone. For the round trip checks of the above.
inline int32_t SwissEffectiveFlags(int32_t iflag, bool fPctr) {
  if (iflag & SEFLG_TOPOCTR) iflag &= ~(SEFLG_HELCTR | SEFLG_BARYCTR);
  if (iflag & SEFLG_BARYCTR) iflag &= ~SEFLG_HELCTR;
  if (iflag & (SEFLG_HELCTR | SEFLG_BARYCTR)) iflag |= SEFLG_NOABERR | SEFLG_NOGDEFL;
  if (iflag & SEFLG_J2000) iflag |= SEFLG_NONUT;
  if (iflag & SEFLG_SIDEREAL) iflag |= SEFLG_NONUT;
  if (iflag & SEFLG_TRUEPOS) iflag |= SEFLG_NOGDEFL | SEFLG_NOABERR;
  if (fPctr) iflag &= ~(SEFLG_HELCTR | SEFLG_BARYCTR);
  return iflag;
}

}  // namespace swiss
}  // namespace eph

#endif  // EPHSWISS_H
