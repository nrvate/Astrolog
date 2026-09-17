// ephproto4_test: the protocol version 4 codec against the conformance
// fixtures (EPHEMERIS_PLUGINS_PLAN.md 3.9), plus codec checks the fixtures
// cannot express: row instants, segment evaluation, the cache key's split,
// and a truncation sweep over every accepted fixture.
//
//   ./ephproto4_test [ephsrv/conformance]
//
// Every fixture must produce its MANIFEST outcome; every "ok" fixture must
// re-encode to identical bytes. Exit 0 with "EPHPROTO4 PASS".

#include "ephproto4.h"
#include "ephswiss.h"

#include <cstdio>
#include <fstream>
#include <sstream>

static int g_fail = 0, g_pass = 0;

static void Check(bool f, const std::string &what) {
  if (f) {
    g_pass++;
  } else {
    g_fail++;
    printf("  FAIL  %s\n", what.c_str());
  }
}

static bool ReadHex(const std::string &path, std::vector<uint8_t> *out) {
  std::ifstream in(path);
  if (!in) return false;
  std::string hex, line;
  while (std::getline(in, line)) hex += line;
  if (hex.size() % 2) return false;
  out->clear();
  for (size_t i = 0; i < hex.size(); i += 2)
    out->push_back((uint8_t)std::stoul(hex.substr(i, 2), nullptr, 16));
  return true;
}

static const char *OutcomeName(eph4::Outcome o) {
  return o == eph4::kOk ? "ok" : o == eph4::kMalformed ? "malformed" : "unsupported";
}

int main(int argc, char **argv) {
  std::string dir = argc > 1 ? argv[1] : "ephsrv/conformance";
  std::ifstream man(dir + "/MANIFEST.tsv");
  if (!man) {
    printf("EPHPROTO4 FAIL: no %s/MANIFEST.tsv\n", dir.c_str());
    return 1;
  }
  std::string line;
  int cFixtures = 0;
  std::vector<std::vector<uint8_t>> accepted;
  while (std::getline(man, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::vector<std::string> col;
    std::stringstream ss(line);
    std::string f;
    while (std::getline(ss, f, '\t')) col.push_back(f);
    if (col.size() < 4) { Check(false, "manifest line: " + line); continue; }
    cFixtures++;
    std::vector<uint8_t> msg;
    if (!ReadHex(dir + "/" + col[0], &msg)) { Check(false, "read " + col[0]); continue; }
    eph4::Envelope env;
    std::string why;
    eph4::Outcome o = eph4::ParseFrame(msg.data(), msg.size(), &env, &why);
    Check(col[3] == OutcomeName(o),
          col[0] + ": expected " + col[3] + ", got " + OutcomeName(o) + (why.empty() ? "" : " (" + why + ")"));
    Check(std::to_string(env.type) == col[2] || o != eph4::kOk, col[0] + ": message type");
    if (o == eph4::kOk) {
      std::vector<uint8_t> again;
      Check(eph4::ReencodeFrame(msg.data(), msg.size(), &again) && again == msg,
            col[0] + ": re-encodes to the same bytes");
      accepted.push_back(msg);
    }
  }
  Check(cFixtures >= 60, "manifest lists the fixtures (" + std::to_string(cFixtures) + ")");

  // Truncation: every strict prefix of an accepted frame, with the envelope's
  // length patched to match, must be refused -- never read past its end
  // (run this binary under ASan/UBSan to make that last part a check).
  int cTrunc = 0, cTruncOk = 0;
  for (const auto &msg : accepted) {
    for (size_t cut = eph4::kEnvelopeSize; cut < msg.size(); cut++) {
      std::vector<uint8_t> t(msg.begin(), msg.begin() + cut);
      uint32_t len = (uint32_t)(cut - eph4::kEnvelopeSize);
      for (int i = 0; i < 4; i++) t[12 + i] = (uint8_t)(len >> (8 * i));
      eph4::Envelope env;
      std::string why;
      eph4::Outcome o = eph4::ParseFrame(t.data(), t.size(), &env, &why);
      cTrunc++;
      // A few payload-free messages legitimately parse at a shorter length
      // only when the full message is empty; any other acceptance is a bug.
      if (o == eph4::kOk) cTruncOk++;
    }
  }
  Check(cTruncOk == 0, "every truncated frame refused (" + std::to_string(cTruncOk) + " of " +
                           std::to_string(cTrunc) + " accepted)");

  // Row instants (3.5): grid rows advance jd2 by an exact i64 ratio; list
  // rows are the list.
  {
    eph4::Request q;
    q.timeMode = eph4::kTimeGrid;
    q.start = {2451545.0, 0.25};
    q.stepNs = -eph4::kNsPerDay / 4;
    q.nTime = 5;
    Check(q.RowTime(0) == eph4::Time{2451545.0, 0.25}, "grid row 0 is start");
    Check(q.RowTime(4).jd1 == 2451545.0 && q.RowTime(4).jd2 == 0.25 - 1.0,
          "grid row 4 steps back a quarter day each");
  }

  // Cache key split (3.7): the same question with different delivery fields
  // has identical question-block bytes.
  {
    eph4::Request a;
    a.start = {2451545.0, 0.0};
    a.profiles.push_back(eph4::Profile());
    eph4::Object o;
    o.naif = 301;
    a.objs.push_back(o);
    eph4::Request b = a;
    b.precision = eph4::kPrecF32;
    b.chunkRows = 7;
    b.priority = 1;
    std::vector<uint8_t> pa, pb;
    eph4::EncodeRequest(&pa, a);
    eph4::EncodeRequest(&pb, b);
    Check(pa.size() == pb.size() &&
              std::equal(pa.begin() + a.questionOffset, pa.end(), pb.begin() + b.questionOffset),
          "delivery fields stay outside the question block");
    Check(pa != pb, "and inside the delivery block");
    std::string why;
    eph4::Request back;
    Check(eph4::ParseRequest(pa.data(), pa.size(), &back, &why) == eph4::kOk,
          "a default request parses (" + why + ")");
  }

  // Segment evaluation (3.4): a degree-2 segment reproduces the polynomial
  // it encodes, x = 1 + 2 tau + 3 T2(tau), and its derivative.
  {
    eph4::Segment g;
    g.mid = {2451545.0, 0.0};
    g.halfSpanDays = 10.0;
    g.degree = 2;
    g.coef = {1.0, 2.0, 3.0, 0.0, 0.0, 0.0, 5.0, 0.0, 0.0};
    double pos[3], vel[3];
    g.Eval({2451545.0, 5.0}, pos, vel);   // tau = 0.5
    double tau = 0.5, T2 = 2 * tau * tau - 1, dT2 = 4 * tau;
    Check(std::fabs(pos[0] - (1 + 2 * tau + 3 * T2)) < 1e-15, "segment value");
    Check(std::fabs(vel[0] - (2 + 3 * dT2) / 10.0) < 1e-15, "segment velocity");
    Check(pos[1] == 0.0 && vel[1] == 0.0 && pos[2] == 5.0 && vel[2] == 0.0, "segment other axes");
  }

  // Appendix B: the Swiss mapping (ephswiss.h).
  {
    using namespace eph4::swiss;
    auto body = [](int32_t naif, int32_t *ipl, int32_t *extra, int32_t *res, bool *fApprox) {
      return BodyFromNaif(naif, ipl, extra, res, fApprox);
    };
    int32_t ipl, extra, res;
    bool fa;
    Check(body(10, &ipl, &extra, &res, &fa) == 0 && ipl == SE_SUN && extra == 0, "10 is the Sun");
    Check(body(4, &ipl, &extra, &res, &fa) == 0 && ipl == SE_MARS && !fa, "4 is Mars (system barycentre)");
    Check(body(9, &ipl, &extra, &res, &fa) == 0 && ipl == SE_PLUTO, "9 is Pluto");
    Check(body(599, &ipl, &extra, &res, &fa) == 0 && ipl == SE_JUPITER && extra == SEFLG_CENTER_BODY,
          "599 is Jupiter's body centre");
    Check(body(401, &ipl, &extra, &res, &fa) == 0 && ipl == SE_PLMOON_OFFSET + 401, "401 is Phobos");
    Check(body(20000001, &ipl, &extra, &res, &fa) == 0 && ipl == SE_CERES, "Ceres is SE_CERES, not SE_AST_OFFSET+1");
    Check(body(20002060, &ipl, &extra, &res, &fa) == 0 && ipl == SE_CHIRON, "Chiron is SE_CHIRON");
    Check(body(20005145, &ipl, &extra, &res, &fa) == 0 && ipl == SE_PHOLUS, "Pholus is SE_PHOLUS");
    Check(body(20000433, &ipl, &extra, &res, &fa) == 0 && ipl == SE_AST_OFFSET + 433, "433 Eros");
    Check(body(20134340, &ipl, &extra, &res, &fa) == 0 && ipl == SE_PLUTO && res == 9 && fa,
          "134340 answered as Pluto, approximated");
    Check(body(3, &ipl, &extra, &res, &fa) == eph4::kOErrUnsupported, "3 (EMB) unsupported");
    Check(body(2000433, &ipl, &extra, &res, &fa) == eph4::kOErrUnknownBody, "the old 2000000+N form is unknown");
    Check(body(20000000 + 21464746, &ipl, &extra, &res, &fa) == eph4::kOErrUnknownBody,
          "asteroid numbers past Swiss's ipl bound refused");

    eph4::Profile geo;
    std::string why;
    SwissCall c;
    eph4::Object o;
    o.kind = eph4::kObjOrbitPoint; o.naif = 301; o.point = eph4::kPtAscNode; o.method = eph4::kMethMean;
    Check(MapObject(o, geo, eph4::kTimeTT, eph4::CanonicalNaN(), SEFLG_SWIEPH, &c, &why) == 0 &&
              c.kind == kCallCalc && c.ipl == SE_MEAN_NODE && !c.fOpposite,
          "Moon mean ascending node is SE_MEAN_NODE");
    o.point = eph4::kPtDescNode; o.method = eph4::kMethOsculating;
    Check(MapObject(o, geo, eph4::kTimeTT, eph4::CanonicalNaN(), SEFLG_SWIEPH, &c, &why) == 0 &&
              c.ipl == SE_TRUE_NODE && c.fOpposite,
          "Moon osculating descending node is SE_TRUE_NODE plus 180");
    o.point = eph4::kPtApo; o.method = eph4::kMethInterpolated;
    Check(MapObject(o, geo, eph4::kTimeTT, eph4::CanonicalNaN(), SEFLG_SWIEPH, &c, &why) == 0 &&
              c.ipl == SE_INTP_APOG, "interpolated apogee is SE_INTP_APOG");
    o.point = eph4::kPtAscNode; o.method = eph4::kMethOsculating; o.nNative = 1;
    Check(MapObject(o, geo, eph4::kTimeTT, eph4::CanonicalNaN(), SEFLG_SWIEPH, &c, &why) == 0 &&
              c.kind == kCallNodAps && c.ipl == SE_MOON && c.nodMethod == SE_NODBIT_OSCU && c.point == 0,
          "nNative 1 forces swe_nod_aps for the Moon (Astrolog's custom points)");
    o.nNative = 0; o.naif = 5; o.point = eph4::kPtPeri; o.method = eph4::kMethMean;
    Check(MapObject(o, geo, eph4::kTimeTT, eph4::CanonicalNaN(), SEFLG_SWIEPH, &c, &why) == 0 &&
              c.kind == kCallNodAps && c.ipl == SE_JUPITER && c.nodMethod == SE_NODBIT_MEAN && c.point == 2,
          "Jupiter mean perihelion through swe_nod_aps");
    o.naif = 301; o.point = eph4::kPtAscNode; o.method = eph4::kMethInterpolated;
    Check(MapObject(o, geo, eph4::kTimeTT, eph4::CanonicalNaN(), SEFLG_SWIEPH, &c, &why) ==
              eph4::kOErrUnsupported, "no interpolated lunar node");

    // Astrolog's topocentric Fagan-Bradley chart on the invariable plane.
    eph4::Profile topo;
    topo.observer = eph4::kObsTopo;
    topo.siteLonEastDeg = -122.3; topo.siteLatDeg = 47.6; topo.siteHeightM = 50.0;
    topo.zodiac = "fagan-bradley";
    topo.siderealPlane = eph4::kSidPlaneInvariable;
    eph4::Object sun;
    sun.naif = 10;
    Check(MapObject(sun, topo, eph4::kTimeTT, 69.2, SEFLG_SWIEPH, &c, &why) == 0 &&
              c.iflag == (SEFLG_SWIEPH | SEFLG_SPEED | SEFLG_TOPOCTR | SEFLG_SIDEREAL) &&
              c.sidMode == (SE_SIDM_FAGAN_BRADLEY | SE_SIDBIT_SSY_PLANE) && c.fTopo &&
              c.topo[0] == -122.3 && !c.fUT && !c.fAddDeltaT,
          "topocentric sidereal on the invariable plane is Astrolog's GetSwissFlags");
    eph4::Profile p2;
    p2.corrections = eph4::kCorrLightTime | eph4::kCorrDeflection;
    p2.frame = eph4::kFrameJ2000;
    p2.plane = eph4::kPlaneEquator;
    p2.speeds = 0;
    Check(MapObject(sun, p2, eph4::kTimeUT1, eph4::CanonicalNaN(), SEFLG_MOSEPH, &c, &why) == 0 &&
              c.iflag == (SEFLG_MOSEPH | SEFLG_NOABERR | SEFLG_J2000 | SEFLG_NONUT | SEFLG_EQUATORIAL) &&
              c.fUT, "no aberration, J2000 equator, no speeds, UT1 through _ut");
    p2.corrections = eph4::kCorrDeflection;
    Check(MapObject(sun, p2, eph4::kTimeTT, 0.0, SEFLG_SWIEPH, &c, &why) == eph4::kOErrUnsupported,
          "a mask Swiss cannot honour (deflection alone) is unsupported");
    eph4::Profile jup;
    jup.observer = eph4::kObsBody;
    jup.observerBody = 5;
    Check(MapObject(sun, jup, eph4::kTimeTT, 0.0, SEFLG_SWIEPH, &c, &why) == 0 && c.kind == kCallPctr &&
              c.ipl == SE_SUN && c.iplCenter == SE_JUPITER, "the Sun seen from Jupiter is swe_calc_pctr");
    eph4::Object h;
    h.kind = eph4::kObjHypothetical; h.name = "vulcan";
    Check(MapObject(h, geo, eph4::kTimeTT, 0.0, SEFLG_SWIEPH, &c, &why) == 0 && c.ipl == SE_VULCAN,
          "the vulcan token is SE_VULCAN");
    h.name = "white-moon";
    Check(MapObject(h, geo, eph4::kTimeTT, 0.0, SEFLG_SWIEPH, &c, &why) == 0 && c.ipl == SE_WHITE_MOON,
          "the white-moon token is SE_WHITE_MOON");
    h.name = "cupido";
    Check(MapObject(h, geo, eph4::kTimeTT, 0.0, SEFLG_SWIEPH, &c, &why) == 0 && c.ipl == SE_CUPIDO,
          "the cupido token is SE_CUPIDO");
    eph4::Object d;
    d.kind = eph4::kObjDesignation; d.name = "2060";
    Check(MapObject(d, geo, eph4::kTimeTT, 0.0, SEFLG_SWIEPH, &c, &why) == 0 && c.ipl == SE_CHIRON,
          "designation 2060 is Chiron");
    eph4::Profile user;
    user.zodiac = "user";
    user.anchorEpoch = {2451545.0, 0.0};
    user.anchorAyanamsaDeg = 23.85;
    Check(MapObject(sun, user, eph4::kTimeTT, 0.0, SEFLG_SWIEPH, &c, &why) == 0 &&
              c.sidMode == SE_SIDM_USER && c.sidT0 == 2451545.0 && c.sidAyanT0 == 23.85,
          "user zodiac is SE_SIDM_USER with the anchor");
    Check(std::string(ZodiacTokenForSwissMode(SE_SIDM_LAHIRI)) == "lahiri" &&
              std::string(ZodiacTokenForSwissMode(SE_SIDM_LAHIRI_ICRC)) == "lahiri-icrc" &&
              std::string(ZodiacTokenForSwissMode(SE_SIDM_USER)) == "user",
          "zodiac tokens follow SE_SIDM_* numbering");
  }

  // Zodiac registry: the Swiss mode numbers are the token indices.
  Check(eph4::ZodiacIndex("fagan-bradley") == 0 && eph4::ZodiacIndex("lahiri") == 1 &&
            eph4::ZodiacIndex("lahiri-icrc") == 46 && eph4::ZodiacIndex("user") == 47,
        "zodiac tokens in SE_SIDM order");

  if (g_fail) {
    printf("EPHPROTO4 FAIL: %d failed, %d passed\n", g_fail, g_pass);
    return 1;
  }
  printf("EPHPROTO4 PASS: %d checks, %d fixtures\n", g_pass, cFixtures);
  return 0;
}
