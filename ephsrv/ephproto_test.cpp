// ephproto_test: the protocol version 4 codec against the conformance
// fixtures (EPHEMERIS_PLUGINS_PLAN.md 3.10), plus codec checks the fixtures
// cannot express: row instants, segment evaluation, the cache key's split,
// and a truncation sweep over every accepted fixture.
//
//   ./ephproto_test [ephsrv/conformance]
//
// Every fixture must produce its MANIFEST outcome; every "ok" fixture must
// re-encode to identical bytes. Exit 0 with "EPHPROTO PASS".

#include "ephproto.h"
#include "ephswiss.h"

#include <climits>
#include <cstdio>
#include <cstdlib>
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

static const char *OutcomeName(eph::Outcome o) {
  return o == eph::kOk ? "ok" : o == eph::kMalformed ? "malformed" : "unsupported";
}

// ---- ephsrv/registries.json (Appendix A, generated) --------------------------
//
// The protocol's numbers live in three places -- the specification's prose,
// this codec, and whatever a second implementation writes -- so the file
// generated from the prose is read here and the codec's own constants are
// required to agree with it. What is compared is the binding of a NAME to a
// NUMBER, which is what a registry is: an entry that moves or is renamed
// fails loudly instead of passing on its position.
//
// Not a JSON parser: tools/gen-registries.py writes one field a line, so the
// fields are read by scanning, and a file not in that shape yields no entries
// and fails the count checks below.

struct RegEntry {
  long num = LONG_MIN;      // value, bit, tag or first
  std::string name, token;
};

static std::string g_regJson;

static std::vector<RegEntry> Registry(const char *key) {
  std::vector<RegEntry> out;
  size_t at = g_regJson.find("\"" + std::string(key) + "\": {");
  if (at == std::string::npos) return out;
  size_t from = g_regJson.find("\"entries\":", at);
  if (from == std::string::npos) return out;
  size_t end = g_regJson.find("\"appendix\":", from);
  if (end == std::string::npos) end = g_regJson.size();
  std::stringstream ss(g_regJson.substr(from, end - from));
  std::string line;
  while (std::getline(ss, line)) {
    size_t b = line.find_first_not_of(" \t");
    if (b == std::string::npos) continue;
    std::string t = line.substr(b);
    if (t == "{") { out.push_back(RegEntry()); continue; }
    if (out.empty() || t.empty() || t[0] != '"') continue;
    size_t colon = t.find("\": ");
    if (colon == std::string::npos) continue;
    std::string field = t.substr(1, colon - 1), value = t.substr(colon + 3);
    while (!value.empty() && (value.back() == ',' || value.back() == ' ')) value.pop_back();
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
      value = value.substr(1, value.size() - 2);
    if (field == "value" || field == "bit" || field == "tag" || field == "first")
      out.back().num = strtol(value.c_str(), nullptr, 10);
    else if (field == "name") out.back().name = value;
    else if (field == "token") out.back().token = value;
  }
  return out;
}

// The number bound to the first entry whose name starts with szName: the
// appendix spells several names with a parenthetical ("busy (too many unread
// answers)"), so a check names the part that identifies the entry.
static long RegNum(const std::vector<RegEntry> &reg, const char *szName) {
  size_t n = strlen(szName);
  for (const RegEntry &e : reg)
    if (e.name.size() >= n && e.name.compare(0, n, szName) == 0) return e.num;
  return LONG_MIN;
}

// The largest number in a registry, for the codec's kXxxMax constants.
static long RegMax(const std::vector<RegEntry> &reg) {
  long hi = LONG_MIN;
  for (const RegEntry &e : reg) hi = e.num > hi ? e.num : hi;
  return hi;
}

// Every registry value is one of a set, and the set is exactly the entries:
// a tag added to the appendix and not to the codec fails here.
static bool RegTagsAre(const std::vector<RegEntry> &reg, const std::vector<long> &want) {
  std::vector<long> got = {};
  for (const RegEntry &e : reg) got.push_back(e.num);
  std::vector<long> w = want;
  std::sort(got.begin(), got.end());
  std::sort(w.begin(), w.end());
  return got == w;
}

int main(int argc, char **argv) {
  std::string dir = argc > 1 ? argv[1] : "ephsrv/conformance";
  std::ifstream man(dir + "/MANIFEST.tsv");
  if (!man) {
    printf("EPHPROTO FAIL: no %s/MANIFEST.tsv\n", dir.c_str());
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
    eph::Envelope env;
    std::string why;
    eph::Outcome o = eph::ParseFrame(msg.data(), msg.size(), &env, &why);
    Check(col[3] == OutcomeName(o),
          col[0] + ": expected " + col[3] + ", got " + OutcomeName(o) + (why.empty() ? "" : " (" + why + ")"));
    Check(std::to_string(env.type) == col[2] || o != eph::kOk, col[0] + ": message type");
    if (o == eph::kOk) {
      std::vector<uint8_t> again;
      Check(eph::ReencodeFrame(msg.data(), msg.size(), &again) && again == msg,
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
    for (size_t cut = eph::kEnvelopeSize; cut < msg.size(); cut++) {
      std::vector<uint8_t> t(msg.begin(), msg.begin() + cut);
      uint32_t len = (uint32_t)(cut - eph::kEnvelopeSize);
      for (int i = 0; i < 4; i++) t[12 + i] = (uint8_t)(len >> (8 * i));
      eph::Envelope env;
      std::string why;
      eph::Outcome o = eph::ParseFrame(t.data(), t.size(), &env, &why);
      cTrunc++;
      // A few payload-free messages legitimately parse at a shorter length
      // only when the full message is empty; any other acceptance is a bug.
      if (o == eph::kOk) cTruncOk++;
    }
  }
  Check(cTruncOk == 0, "every truncated frame refused (" + std::to_string(cTruncOk) + " of " +
                           std::to_string(cTrunc) + " accepted)");

  // Row instants (3.5): grid rows advance jd2 by an exact i64 ratio; list
  // rows are the list.
  {
    eph::Request q;
    q.timeMode = eph::kTimeGrid;
    q.start = {2451545.0, 0.25};
    q.stepNs = -eph::kNsPerDay / 4;
    q.nTime = 5;
    Check(q.RowTime(0) == eph::Time{2451545.0, 0.25}, "grid row 0 is start");
    Check(q.RowTime(4).jd1 == 2451545.0 && q.RowTime(4).jd2 == 0.25 - 1.0,
          "grid row 4 steps back a quarter day each");
  }

  // Cache key split (3.7): the same question with different delivery fields
  // has identical question-block bytes.
  {
    eph::Request a;
    a.start = {2451545.0, 0.0};
    a.profiles.push_back(eph::Profile());
    eph::Object o;
    o.naif = 301;
    a.objs.push_back(o);
    eph::Request b = a;
    b.precision = eph::kPrecF32;
    b.chunkRows = 7;
    b.priority = 1;
    b.deadlineMs = 250;   // advisory delivery, not part of the question
    std::vector<uint8_t> pa, pb;
    eph::EncodeRequest(&pa, a);
    eph::EncodeRequest(&pb, b);
    Check(pa.size() == pb.size() &&
              std::equal(pa.begin() + a.questionOffset, pa.end(), pb.begin() + b.questionOffset),
          "delivery fields stay outside the question block");
    Check(pa != pb, "and inside the delivery block");
    std::string why;
    eph::Request back;
    Check(eph::ParseRequest(pa.data(), pa.size(), &back, &why) == eph::kOk,
          "a default request parses (" + why + ")");
  }

  // Segment evaluation (3.4): a degree-2 segment reproduces the polynomial
  // it encodes, x = 1 + 2 tau + 3 T2(tau), and its derivative.
  {
    eph::Segment g;
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
    using namespace eph::swiss;
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
    Check(body(3, &ipl, &extra, &res, &fa) == eph::kOErrUnsupported, "3 (EMB) unsupported");
    Check(body(2000433, &ipl, &extra, &res, &fa) == eph::kOErrUnknownBody, "the old 2000000+N form is unknown");
    Check(body(20000000 + 21464746, &ipl, &extra, &res, &fa) == eph::kOErrUnknownBody,
          "asteroid numbers past Swiss's ipl bound refused");

    eph::Profile geo;
    std::string why;
    SwissCall c;
    eph::Object o;
    o.kind = eph::kObjOrbitPoint; o.naif = 301; o.point = eph::kPtAscNode; o.method = eph::kMethMean;
    Check(MapObject(o, 0, geo, eph::kTimeTT, eph::CanonicalNaN(), SEFLG_SWIEPH, &c, &why) == 0 &&
              c.kind == kCallCalc && c.ipl == SE_MEAN_NODE && !c.fOpposite,
          "Moon mean ascending node is SE_MEAN_NODE");
    o.point = eph::kPtDescNode; o.method = eph::kMethOsculating;
    Check(MapObject(o, 0, geo, eph::kTimeTT, eph::CanonicalNaN(), SEFLG_SWIEPH, &c, &why) == 0 &&
              c.ipl == SE_TRUE_NODE && c.fOpposite,
          "Moon osculating descending node is SE_TRUE_NODE plus 180");
    o.point = eph::kPtApo; o.method = eph::kMethInterpolated;
    Check(MapObject(o, 0, geo, eph::kTimeTT, eph::CanonicalNaN(), SEFLG_SWIEPH, &c, &why) == 0 &&
              c.ipl == SE_INTP_APOG, "interpolated apogee is SE_INTP_APOG");
    o.point = eph::kPtAscNode; o.method = eph::kMethOsculating;
    Check(MapObject(o, 1, geo, eph::kTimeTT, eph::CanonicalNaN(), SEFLG_SWIEPH, &c, &why) == 0 &&
              c.kind == kCallNodAps && c.ipl == SE_MOON && c.nodMethod == SE_NODBIT_OSCU && c.point == 0,
          "nNative 1 forces swe_nod_aps for the Moon (Astrolog's custom points)");
    o.naif = 5; o.point = eph::kPtPeri; o.method = eph::kMethMean;
    Check(MapObject(o, 0, geo, eph::kTimeTT, eph::CanonicalNaN(), SEFLG_SWIEPH, &c, &why) == 0 &&
              c.kind == kCallNodAps && c.ipl == SE_JUPITER && c.nodMethod == SE_NODBIT_MEAN && c.point == 2,
          "Jupiter mean perihelion through swe_nod_aps");
    o.naif = 301; o.point = eph::kPtAscNode; o.method = eph::kMethInterpolated;
    Check(MapObject(o, 0, geo, eph::kTimeTT, eph::CanonicalNaN(), SEFLG_SWIEPH, &c, &why) ==
              eph::kOErrUnsupported, "no interpolated lunar node");

    // Astrolog's topocentric Fagan-Bradley chart on the invariable plane.
    eph::Profile topo;
    topo.observer = eph::kObsTopo;
    topo.siteLonEastDeg = -122.3; topo.siteLatDeg = 47.6; topo.siteHeightM = 50.0;
    topo.zodiac = "fagan-bradley";
    topo.siderealPlane = eph::kSidPlaneInvariable;
    eph::Object sun;
    sun.naif = 10;
    Check(MapObject(sun, 0, topo, eph::kTimeTT, 69.2, SEFLG_SWIEPH, &c, &why) == 0 &&
              c.iflag == (SEFLG_SWIEPH | SEFLG_SPEED | SEFLG_TOPOCTR | SEFLG_SIDEREAL) &&
              c.sidMode == (SE_SIDM_FAGAN_BRADLEY | SE_SIDBIT_SSY_PLANE) && c.fTopo &&
              c.topo[0] == -122.3 && !c.fUT && !c.fAddDeltaT,
          "topocentric sidereal on the invariable plane is Astrolog's GetSwissFlags");
    eph::Profile p2;
    p2.corrections = eph::kCorrLightTime | eph::kCorrDeflection;
    p2.frame = eph::kFrameJ2000;
    p2.plane = eph::kPlaneEquator;
    p2.speeds = 0;
    Check(MapObject(sun, 0, p2, eph::kTimeUT1, eph::CanonicalNaN(), SEFLG_MOSEPH, &c, &why) == 0 &&
              c.iflag == (SEFLG_MOSEPH | SEFLG_NOABERR | SEFLG_J2000 | SEFLG_NONUT | SEFLG_EQUATORIAL) &&
              c.fUT, "no aberration, J2000 equator, no speeds, UT1 through _ut");
    p2.corrections = eph::kCorrDeflection;
    Check(MapObject(sun, 0, p2, eph::kTimeTT, 0.0, SEFLG_SWIEPH, &c, &why) == eph::kOErrUnsupported,
          "a mask Swiss cannot honour (deflection alone) is unsupported");
    eph::Profile jup;
    jup.observer = eph::kObsBody;
    jup.observerBody = 5;
    Check(MapObject(sun, 0, jup, eph::kTimeTT, 0.0, SEFLG_SWIEPH, &c, &why) == 0 && c.kind == kCallPctr &&
              c.ipl == SE_SUN && c.iplCenter == SE_JUPITER, "the Sun seen from Jupiter is swe_calc_pctr");
    eph::Object h;
    h.kind = eph::kObjHypothetical; h.name = "vulcan";
    Check(MapObject(h, 0, geo, eph::kTimeTT, 0.0, SEFLG_SWIEPH, &c, &why) == 0 && c.ipl == SE_VULCAN,
          "the vulcan token is SE_VULCAN");
    h.name = "white-moon";
    Check(MapObject(h, 0, geo, eph::kTimeTT, 0.0, SEFLG_SWIEPH, &c, &why) == 0 && c.ipl == SE_WHITE_MOON,
          "the white-moon token is SE_WHITE_MOON");
    h.name = "cupido";
    Check(MapObject(h, 0, geo, eph::kTimeTT, 0.0, SEFLG_SWIEPH, &c, &why) == 0 && c.ipl == SE_CUPIDO,
          "the cupido token is SE_CUPIDO");
    eph::Object d;
    d.kind = eph::kObjDesignation; d.name = "2060";
    Check(MapObject(d, 0, geo, eph::kTimeTT, 0.0, SEFLG_SWIEPH, &c, &why) == 0 && c.ipl == SE_CHIRON,
          "designation 2060 is Chiron");
    eph::Profile user;
    user.zodiac = "user";
    user.anchorEpoch = {2451545.0, 0.0};
    user.anchorAyanamsaDeg = 23.85;
    Check(MapObject(sun, 0, user, eph::kTimeTT, 0.0, SEFLG_SWIEPH, &c, &why) == 0 &&
              c.sidMode == SE_SIDM_USER && c.sidT0 == 2451545.0 && c.sidAyanT0 == 23.85,
          "user zodiac is SE_SIDM_USER with the anchor");
    Check(std::string(ZodiacTokenForSwissMode(SE_SIDM_LAHIRI)) == "lahiri" &&
              std::string(ZodiacTokenForSwissMode(SE_SIDM_LAHIRI_ICRC)) == "lahiri-icrc" &&
              std::string(ZodiacTokenForSwissMode(SE_SIDM_USER)) == "user",
          "zodiac tokens follow SE_SIDM_* numbering");
  }

  // The other direction (ephswiss.h ObjectFromSwiss, ProfileFromSwiss): every
  // Swiss call Astrolog makes goes on the wire as a question that maps back
  // to the same call -- the same entry point, body and point, and flags
  // Swiss treats identically -- except the two exceptions the header names.
  {
    using namespace eph::swiss;
    struct { int32_t ipl; int point; int32_t nod; bool fException; } const bodies[] = {
      {SE_SUN, -1, 0, false}, {SE_MOON, -1, 0, false}, {SE_MERCURY, -1, 0, false},
      {SE_VENUS, -1, 0, false}, {SE_MARS, -1, 0, false}, {SE_PLUTO, -1, 0, false},
      {SE_EARTH, -1, 0, false}, {SE_CHIRON, -1, 0, false}, {SE_PHOLUS, -1, 0, false},
      {SE_CERES, -1, 0, false}, {SE_VESTA, -1, 0, false}, {SE_MEAN_NODE, -1, 0, false},
      {SE_TRUE_NODE, -1, 0, false}, {SE_MEAN_APOG, -1, 0, false}, {SE_OSCU_APOG, -1, 0, false},
      {SE_INTP_APOG, -1, 0, false}, {SE_INTP_PERG, -1, 0, false}, {SE_CUPIDO, -1, 0, false},
      {SE_FICT_OFFSET + 18, -1, 0, false}, {SE_AST_OFFSET + 433, -1, 0, false},
      {SE_AST_OFFSET + 90377, -1, 0, false}, {SE_PLMOON_OFFSET + 501, -1, 0, false},
      {SE_PLMOON_OFFSET + 599, -1, 0, false},
      {SE_JUPITER, 2, SE_NODBIT_MEAN, false}, {SE_EARTH, 0, SE_NODBIT_OSCU, false},
      {SE_EARTH, 3, SE_NODBIT_MEAN, false}, {SE_MOON, 2, SE_NODBIT_MEAN, false},
      {SE_AST_OFFSET + 1, -1, 0, true}, {SE_AST_OFFSET + 2060, -1, 0, true},
      {SE_MOON, 0, SE_NODBIT_MEAN, true}, {SE_MOON, 3, SE_NODBIT_OSCU, true},
    };
    const int32_t rgflag[] = {
      SEFLG_SWIEPH | SEFLG_SPEED,
      SEFLG_SWIEPH | SEFLG_SPEED | SEFLG_SIDEREAL | SEFLG_NONUT,
      SEFLG_SWIEPH | SEFLG_SPEED | SEFLG_HELCTR | SEFLG_TRUEPOS,
      SEFLG_SWIEPH | SEFLG_BARYCTR,
      SEFLG_SWIEPH | SEFLG_SPEED | SEFLG_TOPOCTR | SEFLG_SIDEREAL,
    };
    const int32_t rgsid[] = {SE_SIDM_FAGAN_BRADLEY, SE_SIDBIT_SSY_PLANE, SE_SIDM_LAHIRI | SE_SIDBIT_ECL_T0};
    const double topo[3] = {-122.3, 47.6, 12.0};
    int cBad = 0, cTried = 0, cExcept = 0;
    std::string firstBad;
    for (const auto &b : bodies)
      for (int32_t fl : rgflag)
        for (int32_t sid : rgsid)
          for (int32_t ctr : {-1, (int32_t)SE_MARS}) {
            if (ctr >= 0 && (b.point >= 0 || fl & SEFLG_TOPOCTR || b.ipl == ctr || b.ipl == SE_MEAN_NODE ||
                             b.ipl == SE_TRUE_NODE || b.ipl == SE_MEAN_APOG || b.ipl == SE_OSCU_APOG ||
                             b.ipl == SE_INTP_APOG || b.ipl == SE_INTP_PERG))
              continue;
            int32_t flag = fl | (ctr >= 0 ? SEFLG_HELCTR : 0);
            eph::Object o;
            eph::Profile pf;
            SwissCall c;
            std::string why;
            cTried++;
            bool fOk = ObjectFromSwiss(b.ipl, b.point, b.nod, &o) &&
                       ProfileFromSwiss(flag, ctr, sid, topo, &pf) &&
                       MapObject(o, 0, pf, eph::kTimeTT, 0.0, SEFLG_SWIEPH, &c, &why) == 0;
            // What the server computes, against what the client would have.
            int wantKind = ctr >= 0 ? kCallPctr : b.point >= 0 ? kCallNodAps : kCallCalc;
            bool fSame = fOk && c.kind == wantKind && c.ipl == b.ipl &&
                         (b.point < 0 || (c.point == b.point && c.nodMethod == b.nod)) &&
                         c.iplCenter == (ctr >= 0 ? ctr : -1) &&
                         SwissEffectiveFlags(c.iflag, ctr >= 0) == SwissEffectiveFlags(flag, ctr >= 0) &&
                         (!(flag & SEFLG_SIDEREAL) || c.sidMode == sid) &&
                         (!(flag & SEFLG_TOPOCTR) || (c.fTopo && c.topo[0] == topo[0] &&
                                                      c.topo[1] == topo[1] && c.topo[2] == topo[2]));
            if (b.ipl == SE_PLMOON_OFFSET + 599 && fOk && !fSame)
              fSame = c.ipl == SE_JUPITER && (c.iflag & SEFLG_CENTER_BODY) &&
                      SwissEffectiveFlags(c.iflag & ~SEFLG_CENTER_BODY, ctr >= 0) ==
                        SwissEffectiveFlags(flag, ctr >= 0);
            if (b.fException) {
              cExcept += fOk && !fSame;
              continue;
            }
            if (!fSame && cBad++ == 0)
              firstBad = "ipl " + std::to_string(b.ipl) + " point " + std::to_string(b.point) +
                         " flags " + std::to_string(flag) + " sid " + std::to_string(sid) + " centre " +
                         std::to_string(ctr) + (fOk ? "" : " refused: " + why);
          }
    Check(cBad == 0, "Swiss calls round trip through version 4 (" + std::to_string(cBad) + " of " +
                         std::to_string(cTried) + " differ; first: " + firstBad + ")");
    Check(cExcept > 0, "the documented exceptions are answered, from the canonical body");
    eph::Profile pf;
    Check(!ProfileFromSwiss(SEFLG_SWIEPH | SEFLG_SIDEREAL, -1, SE_SIDM_FAGAN_BRADLEY | SE_SIDBIT_PREC_ORIG,
                            topo, &pf), "a sidereal bit version 4 has no field for is refused");
    Check(!ProfileFromSwiss(SEFLG_SWIEPH | SEFLG_TOPOCTR, SE_MARS, 0, topo, &pf),
          "a topocentric planet-centred call is refused");
    // 3.5a: the Sun and the solar-system barycentre have no orbit, and
    // barycentric osculating elements are not this mapping's to define.
    eph::Object o;
    eph::Profile geo2;
    SwissCall c2;
    std::string why2;
    o.kind = eph::kObjOrbitPoint; o.naif = 10; o.point = eph::kPtApo; o.method = eph::kMethMean;
    Check(MapObject(o, 0, geo2, eph::kTimeTT, 0.0, SEFLG_SWIEPH, &c2, &why2) == eph::kOErrUnsupported,
          "the Sun has no orbit point");
    o.naif = 0;
    Check(MapObject(o, 0, geo2, eph::kTimeTT, 0.0, SEFLG_SWIEPH, &c2, &why2) == eph::kOErrUnsupported,
          "nor has the solar-system barycentre");
    o.naif = 5; o.method = eph::kMethOscuBary;
    Check(MapObject(o, 0, geo2, eph::kTimeTT, 0.0, SEFLG_SWIEPH, &c2, &why2) == eph::kOErrUnsupported,
          "barycentric osculating elements are not served");
  }

  // 3.5 and A.4 0x8004: the delta T table, and a row's delta T from it.
  {
    std::vector<uint8_t> pay;
    eph::Request q, back;
    std::string why;
    q.timeScale = eph::kTimeUT1;
    q.start = {2451545.0, 0.0};
    q.stepNs = eph::kNsPerDay;
    q.nTime = 3;
    q.profiles.push_back(eph::Profile());
    q.objs.push_back(eph::Object());
    q.objs[0].naif = 10;
    eph::Tlv e;
    eph::EncodeDeltaTTable({{{2451545.0, 0.0}, 60.0}, {{2451547.0, 0.0}, 64.0}}, &e);
    q.ext.push_back(e);
    eph::EncodeRequest(&pay, q);
    Check(eph::ParseRequest(pay.data(), pay.size(), &back, &why) == eph::kOk &&
              back.deltaTTable.size() == 2, "a delta T table parses (" + why + ")");
    Check(back.DeltaTAt(0) == 60.0 && back.DeltaTAt(1) == 62.0 && back.DeltaTAt(2) == 64.0,
          "a row's delta T is interpolated at its own instant, no iteration");
    eph::Request past = back;
    past.start = {2400000.0, 0.0};
    Check(past.DeltaTAt(0) == 60.0, "before the table it is held constant");
    past.start = {2500000.0, 0.0};
    Check(past.DeltaTAt(0) == 64.0, "after the table it is held constant");
    // The table is in the question block, so two casts with different tables
    // are different questions (3.7).
    Check(back.questionOffset == 16, "the table rides in the question block");
    eph::Request one;
    one.deltaTSec = 69.2;
    Check(one.DeltaTAt(0) == 69.2, "without a table the one value is every row's");
    eph::Request none;
    Check(eph::IsCanonicalNaN(none.DeltaTAt(0)), "and without either it is the server's model");
  }

  // 3.4 batched LOOKUP: several queries, one budget, one answer whose lists
  // come back in the order they were asked.
  {
    eph::Lookup l;
    l.maxMatches = 16;
    l.queries = {"Ceres", "Chiron", "Aldebaran"};
    std::vector<uint8_t> pay;
    std::string why;
    eph::EncodeLookup(&pay, l);
    eph::Lookup back;
    Check(eph::ParseLookup(pay.data(), pay.size(), &back, &why) == eph::kOk &&
              back.queries == l.queries && back.maxMatches == 16,
          "a batched LOOKUP round trips (" + why + ")");
    pay.clear();
    l.queries.clear();
    eph::EncodeLookup(&pay, l);
    Check(eph::ParseLookup(pay.data(), pay.size(), &back, &why) == eph::kMalformed,
          "a LOOKUP with no query is malformed");
    eph::LookupResult lr, lrBack;
    lr.sources.push_back("Swiss Ephemeris files");
    eph::Match m;
    m.obj.naif = 20000001;
    m.canonicalName = "Ceres";
    lr.queries.push_back({m});
    lr.queries.push_back({});          // a query with no match keeps its place
    lr.queries.push_back({m, m});
    lr.flags = 1;                      // the message budget ran out
    std::vector<uint8_t> rp;
    eph::EncodeLookupResult(&rp, lr);
    Check(eph::ParseLookupResult(rp.data(), rp.size(), &lrBack, &why) == eph::kOk &&
              lrBack.queries.size() == 3 && lrBack.queries[0].size() == 1 &&
              lrBack.queries[1].empty() && lrBack.queries[2].size() == 2 &&
              (lrBack.flags & 1),
          "three answers keep their order, an empty one among them (" + why + ")");
  }

  // 3.1 in the answering direction: a client tolerates registry values a
  // newer server sends, and refuses only what it cannot read.
  {
    eph::LookupResult lr, back;
    eph::Match m;
    m.quality = 9;              // a quality this build does not define
    m.obj.naif = 10;
    m.canonicalName = "Sun";
    lr.sources.push_back("Swiss Ephemeris files");
    lr.queries.push_back({m});
    std::vector<uint8_t> pay;
    std::string why;
    eph::EncodeLookupResult(&pay, lr);
    Check(eph::ParseLookupResult(pay.data(), pay.size(), &back, &why) == eph::kOk &&
              back.queries.size() == 1 && back.queries[0].size() == 1 &&
              back.queries[0][0].quality == 9,
          "an unknown match quality is kept, not refused (" + why + ")");
    // An unknown KIND is skipped by its matchLen and the matches on either
    // side are kept -- and the message still re-encodes to the same bytes.
    // The middle match is built as it would arrive from a newer server: a
    // kind past the registry, its own length, and a payload this build
    // cannot read.
    lr.queries[0][0].quality = 0;
    eph::Match unknown;
    unknown.fUnknownKind = true;
    unknown.raw = std::string("\x07\x00\x00\x00", 4) + std::string("future", 6);
    eph::Match moon = m;
    moon.quality = 1;
    moon.obj.naif = 301;
    moon.canonicalName = "Moon";
    lr.queries[0].push_back(unknown);
    lr.queries[0].push_back(moon);
    pay.clear();   // the encoders append to their buffer
    eph::EncodeLookupResult(&pay, lr);
    Check(eph::ParseLookupResult(pay.data(), pay.size(), &back, &why) == eph::kOk &&
              back.queries[0].size() == 3 && back.queries[0][1].fUnknownKind &&
              back.queries[0][1].raw == unknown.raw && back.queries[0][2].obj.naif == 301,
          "an unknown match kind is skipped by its length, the rest kept (" + why + ")");
    std::vector<uint8_t> again;
    eph::EncodeLookupResult(&again, back);
    Check(again == pay, "and the message re-encodes to the same bytes");
    // A matchLen that disagrees with a match this reader CAN read is
    // malformed: one of the two ends has miscounted.
    // nQueries, flags, nSources, the one source's str8, this query's u16
    // count, then the match's quality and sourceIdx: matchLen follows.
    std::vector<uint8_t> bad = pay;
    bad[1 + 1 + 1 + 1 + (int)lr.sources[0].size() + 2 + 2] = 4;
    Check(eph::ParseLookupResult(bad.data(), bad.size(), &back, &why) == eph::kMalformed,
          "a matchLen that does not match what was read is malformed");

    eph::Meta meta;
    meta.flags = 0x80;          // a flag bit past the registry
    meta.name = "Sun";
    eph::DataChunk d;
    d.nObj = 1; d.nRows = 1; d.totalRows = 1; d.flags = eph::kChunkLast | eph::kChunkMeta;
    d.sources.push_back("Swiss Ephemeris files");
    d.meta.push_back(meta);
    d.values.assign(6, 1.0);
    std::vector<uint8_t> dp;
    eph::DataChunk dback;
    eph::EncodeData(&dp, d);
    Check(eph::ParseData(dp.data(), dp.size(), &dback, &why) == eph::kOk &&
              dback.meta[0].flags == 0x80, "an unknown META flag bit is tolerated");
    meta.corrApplied = 0x87;    // the three A.7 bits plus one past them
    d.meta[0] = meta;
    dp.clear();                 // Writer appends; encode fresh
    eph::EncodeData(&dp, d);
    Check(eph::ParseData(dp.data(), dp.size(), &dback, &why) == eph::kOk &&
              dback.meta[0].corrApplied == 0x87,
          "corrApplied round-trips, high bits included: reserved, never refused");
    d.columnsPresent = 0x10;    // a column bit is not: it changes the row width
    dp.clear();
    eph::EncodeData(&dp, d);
    Check(eph::ParseData(dp.data(), dp.size(), &dback, &why) != eph::kOk,
          "an unknown column bit is still refused");
    eph::Error e;
    e.code = 200;
    e.flags = eph::kErrFlagRetryable;
    std::vector<uint8_t> ep;
    eph::Error eback;
    eph::EncodeError(&ep, e);
    Check(eph::ParseError(ep.data(), ep.size(), &eback, &why) == eph::kOk && eback.code == 200,
          "an unregistered ERROR code is still an error");
  }

  // A.3 capabilities: both example servers decode, and an encode decodes back.
  {
    for (const char *name : {"welcome_swiss.hex", "welcome_prometheia.hex"}) {
      std::vector<uint8_t> msg;
      eph::Welcome w;
      eph::Capabilities c;
      std::string why;
      Check(ReadHex(dir + "/" + name, &msg) &&
                eph::ParseWelcome(msg.data() + eph::kEnvelopeSize, msg.size() - eph::kEnvelopeSize, &w,
                                  &why) == eph::kOk &&
                eph::ParseCapabilities(w.caps_, &c, &why) == eph::kOk &&
                c.CorrectionMask(eph::kObsGeo, 7) && c.Kind(eph::kObjBody) &&
                c.TimeScale(eph::kTimeTT) && c.lookupMax >= 32,
            std::string(name) + ": capabilities decode (" + why + ")");
    }
    eph::Capabilities c, back;
    c.kinds = 0x2F; c.observers = 0x1F; c.planes = 3; c.forms = 3; c.frames = 0xF;
    c.corrMasks = {{0x13, 7}, {0x1F, 0}, {0x13, 3}, {0x13, 5}, {0x1F, 1}};
    c.orbitPoints = 0xF; c.orbitMethods = 0x17; c.columns = 0xA;
    c.zodiacs = {"fagan-bradley", "user"}; c.siderealPlanes = 7; c.timeScales = 3;
    c.deltaTModel = "swiss"; c.fRate = true; c.cellsPerSec = 10; c.burst = 20; c.lookupMax = 32;
    c.hypotheticals = {"cupido"};
    c.fRatesBound = true; c.ratesDegPerDay = 5e-5f; c.ratesAuPerDay = 1e-4f;
    c.fSegments = true; c.segMaxDegree = 15; c.segMaxPerObject = 4096;
    c.segMinErrArcsec = 1e-4f; c.segKinds = 0x3;
    eph::TlvList tl;
    eph::EncodeCapabilities(c, &tl);
    std::string why;
    Check(eph::ParseCapabilities(tl, &back, &why) == eph::kOk && back.corrMasks == c.corrMasks &&
              back.zodiacs == c.zodiacs && back.hypotheticals == c.hypotheticals && back.burst == 20 &&
              back.deltaTModel == "swiss" && back.columns == 0xA && back.frames == 0xF &&
              back.fRatesBound && back.ratesAuPerDay == 1e-4f && back.fSegments &&
              back.segKinds == 0x3 && back.segMaxDegree == 15 &&
              !back.CorrectionMask(eph::kObsHelio, 7) && back.CorrectionMask(eph::kObsHelio, 1),
          "capabilities round trip (" + why + ")");
    tl.erase(tl.begin());
    Check(eph::ParseCapabilities(tl, &back, &why) == eph::kMalformed, "a missing required TLV is malformed");
  }

  // ephsrv/registries.json against this codec's constants (Appendix A).
  {
    std::ifstream in(dir + "/../registries.json", std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    g_regJson = ss.str();
    Check(!g_regJson.empty(), "registries.json read");

    auto msg = Registry("message_types");
    Check(msg.size() >= 18, "A.1 message types parsed (" + std::to_string(msg.size()) + ")");
    Check(RegNum(msg, "HELLO") == eph::kMsgHello && RegNum(msg, "WELCOME") == eph::kMsgWelcome &&
              RegNum(msg, "REQUEST") == eph::kMsgRequest && RegNum(msg, "DATA") == eph::kMsgData &&
              RegNum(msg, "ERROR") == eph::kMsgError && RegNum(msg, "PING") == eph::kMsgPing &&
              RegNum(msg, "PONG") == eph::kMsgPong && RegNum(msg, "CANCEL") == eph::kMsgCancel &&
              RegNum(msg, "LOOKUP_RESULT") == eph::kMsgLookupResult &&
              RegNum(msg, "SEGDATA") == eph::kMsgSegData,
          "A.1 message types agree with the codec");
    // "LOOKUP" prefixes "LOOKUP_RESULT", so it is asked for by its own value.
    Check(msg[eph::kMsgLookup - 1].num == eph::kMsgLookup &&
              msg[eph::kMsgLookup - 1].name == "LOOKUP",
          "A.1 LOOKUP is 9");

    auto caps = Registry("caps_bits");
    Check(caps.size() == 10, "A.2 caps bits parsed (" + std::to_string(caps.size()) + ")");
    Check(RegNum(caps, "f32") == 0 && RegNum(caps, "zstd") == 1 && RegNum(caps, "cancel") == 2 &&
              RegNum(caps, "lookup") == 3 && RegNum(caps, "instant lists") == 4 &&
              RegNum(caps, "priority") == 5 && RegNum(caps, "segments") == 6 &&
              RegNum(caps, "designations") == 7 && RegNum(caps, "deep sky") == 9,
          "A.2 caps bit names agree with the codec");
    Check((1u << RegNum(caps, "f32")) == eph::kCapF32 &&
              (1u << RegNum(caps, "zstd")) == eph::kCapZstd &&
              (1u << RegNum(caps, "cancel")) == eph::kCapCancel &&
              (1u << RegNum(caps, "lookup")) == eph::kCapLookup &&
              (1u << RegNum(caps, "instant lists")) == eph::kCapInstantLists &&
              (1u << RegNum(caps, "priority")) == eph::kCapPriority &&
              (1u << RegNum(caps, "segments")) == eph::kCapSegments &&
              (1u << RegNum(caps, "designations")) == eph::kCapDesignations &&
              (1u << RegNum(caps, "deep sky")) == eph::kCapDeepSky &&
              (1u << caps[8].num) == eph::kCapDeltaTTable,   // the delta T name is not ASCII
          "A.2 caps masks agree with the codec");

    Check(RegTagsAre(Registry("welcome_capability_tlvs"),
                     {eph::kCapTagKinds, eph::kCapTagObservers, eph::kCapTagPlanesFormsFrames,
                      eph::kCapTagCorrections, eph::kCapTagOrbit, eph::kCapTagColumns,
                      eph::kCapTagZodiacs, eph::kCapTagSiderealPlanes, eph::kCapTagTimeScales,
                      eph::kCapTagCoverage, eph::kCapTagCatalogs, eph::kCapTagDeltaT,
                      eph::kCapTagPrecession, eph::kCapTagRate, eph::kCapTagSegments,
                      eph::kCapTagLookup, eph::kCapTagHypotheticals, eph::kCapTagEquinoxes,
                      eph::kCapTagRatesBound}),
          "A.3 WELCOME capability tags are exactly the codec's");
    Check(RegTagsAre(Registry("request_tlvs"),
                     {eph::kReqTagPrecession, eph::kReqTagEphemerisPin, eph::kReqTagCatalogPin,
                      eph::kReqTagDatasetPin, eph::kReqTagDeltaTTable}),
          "A.4 REQUEST tags are exactly the codec's");

    auto obs = Registry("observers");
    Check(obs.size() == 5 && RegMax(obs) == eph::kObserverMax &&
              RegNum(obs, "geocentric") == eph::kObsGeo &&
              RegNum(obs, "topocentric") == eph::kObsTopo &&
              RegNum(obs, "heliocentric") == eph::kObsHelio &&
              RegNum(obs, "solar-system barycentre") == eph::kObsBary &&
              RegNum(obs, "body") == eph::kObsBody,
          "A.5 observers agree with the codec");
    auto planes = Registry("planes"), forms = Registry("forms"), frames = Registry("frames");
    Check(RegNum(planes, "ecliptic") == eph::kPlaneEcliptic &&
              RegNum(planes, "equator") == eph::kPlaneEquator &&
              RegNum(forms, "spherical") == eph::kFormSpherical &&
              RegNum(forms, "rectangular") == eph::kFormRectangular &&
              RegNum(frames, "true of date") == eph::kFrameTrueOfDate &&
              RegNum(frames, "mean of date") == eph::kFrameMeanOfDate &&
              RegNum(frames, "J2000") == eph::kFrameJ2000 &&
              RegNum(frames, "ICRF") == eph::kFrameIcrf && RegMax(frames) == eph::kFrameMax,
          "A.6 planes, forms and frames agree with the codec");
    auto corr = Registry("correction_bits");
    Check(corr.size() == 3 && RegNum(corr, "light time") == eph::kCorrLightTime &&
              RegNum(corr, "gravitational deflection") == eph::kCorrDeflection &&
              RegNum(corr, "aberration") == eph::kCorrAberration &&
              (uint8_t)(eph::kCorrLightTime | eph::kCorrDeflection | eph::kCorrAberration) ==
                  eph::kCorrMask,
          "A.7 correction bits agree with the codec");
    auto sid = Registry("sidereal_planes");
    Check(sid.size() == 3 && RegMax(sid) == eph::kSidPlaneMax &&
              RegNum(sid, "ecliptic of date") == eph::kSidPlaneDate &&
              RegNum(sid, "ecliptic of the anchor epoch") == eph::kSidPlaneAnchor &&
              RegNum(sid, "invariable plane") == eph::kSidPlaneInvariable,
          "A.8 sidereal planes agree with the codec");
    auto ts = Registry("time_scales");
    Check(ts.size() == 3 && RegMax(ts) == eph::kTimeScaleMax && RegNum(ts, "UT1") == eph::kTimeUT1 &&
              RegNum(ts, "TT") == eph::kTimeTT && RegNum(ts, "TDB") == eph::kTimeTDB,
          "A.9 time scales agree with the codec");
    auto cols = Registry("extra_column_bits");
    // The sigma column's name is Greek in the appendix, so the four are
    // checked by position -- which is what a bit registry is.
    Check(cols.size() == 4 && RegTagsAre(cols, {0, 1, 2, 3}) &&
              eph::kColSigma == 1u && eph::kColAyanamsa == 2u && eph::kColLightTime == 4u &&
              eph::kColDeltaT == 8u && eph::kColMask == 0xFu,
          "A.10 extra column bits agree with the codec");
    auto zod = Registry("zodiac_tokens");
    bool fZod = zod.size() == (size_t)eph::kZodiacTokenCount;
    for (size_t i = 0; fZod && i < zod.size(); i++)
      fZod = zod[i].token == eph::kZodiacTokens[i];
    Check(fZod, "A.11 zodiac tokens are the codec's, in order (" +
                    std::to_string(zod.size()) + ")");
    auto kinds = Registry("object_kinds");
    Check(kinds.size() == 6 && RegMax(kinds) == eph::kObjKindMax &&
              RegNum(kinds, "body") == eph::kObjBody &&
              RegNum(kinds, "orbit point") == eph::kObjOrbitPoint &&
              RegNum(kinds, "fixed star") == eph::kObjStar &&
              RegNum(kinds, "named hypothetical") == eph::kObjHypothetical &&
              RegNum(kinds, "elements") == eph::kObjElements &&
              RegNum(kinds, "designation") == eph::kObjDesignation,
          "A.12 object kinds agree with the codec");
    auto pts = Registry("orbit_points");
    Check(pts.size() == 4 && RegMax(pts) == eph::kOrbitPointMax &&
              RegNum(pts, "ascending node") == eph::kPtAscNode &&
              RegNum(pts, "descending node") == eph::kPtDescNode &&
              RegNum(pts, "perihelion") == eph::kPtPeri &&
              RegNum(pts, "aphelion") == eph::kPtApo,
          "A.13 orbit points agree with the codec");
    auto meth = Registry("orbit_methods");
    Check(meth.size() == 5 && RegMax(meth) == eph::kOrbitMethodMax &&
              RegNum(meth, "mean") == eph::kMethMean &&
              RegNum(meth, "osculating, barycentric") == eph::kMethOscuBary &&
              RegNum(meth, "interpolated") == eph::kMethInterpolated &&
              RegNum(meth, "focal point") == eph::kMethFocal,
          "A.14 orbit methods agree with the codec");
    auto hyp = Registry("hypothetical_tokens");
    bool fHyp = hyp.size() == (size_t)eph::kHypotheticalTokenCount;
    for (size_t i = 0; fHyp && i < hyp.size(); i++)
      fHyp = hyp[i].token == eph::kHypotheticalTokens[i];
    Check(fHyp, "A.15 hypothetical tokens are the codec's, in seorbel.txt order (" +
                    std::to_string(hyp.size()) + ")");
    auto eqx = Registry("element_equinoxes");
    Check(eqx.size() == 5 && RegMax(eqx) == eph::kEquinoxMax &&
              RegNum(eqx, "J2000") == eph::kEqJ2000 && RegNum(eqx, "B1950") == eph::kEqB1950 &&
              RegNum(eqx, "J1900") == eph::kEqJ1900 && RegNum(eqx, "of date") == eph::kEqOfDate &&
              RegNum(eqx, "explicit JD") == eph::kEqExplicit,
          "A.16 element equinoxes agree with the codec");
    Check(RegMax(Registry("element_centres")) == eph::kCentreMax,
          "A.21 element centres agree with the codec");
    auto oerr = Registry("object_error_codes");
    Check(oerr.size() == 9 && RegNum(oerr, "none") == eph::kOErrNone &&
              RegNum(oerr, "unknown body or name") == eph::kOErrUnknownBody &&
              RegNum(oerr, "unsupported by this source") == eph::kOErrUnsupported &&
              RegNum(oerr, "outside the data's time coverage") == eph::kOErrCoverage &&
              RegNum(oerr, "data unavailable") == eph::kOErrDataMissing &&
              RegNum(oerr, "point undefined") == eph::kOErrUndefinedPoint &&
              RegNum(oerr, "ambiguous name") == eph::kOErrAmbiguous &&
              RegNum(oerr, "numerical failure") == eph::kOErrNumerical &&
              RegNum(oerr, "internal") == eph::kOErrInternal,
          "A.17 per-object error codes agree with the codec");
    auto mf = Registry("meta_flags");
    Check(mf.size() == 7 && (1u << RegNum(mf, "approximated")) == eph::kMetaApproximated &&
              (1u << RegNum(mf, "extrapolated")) == eph::kMetaExtrapolated &&
              (1u << RegNum(mf, "hasSigma")) == eph::kMetaHasSigma &&
              (1u << RegNum(mf, "partial")) == eph::kMetaPartial &&
              (1u << RegNum(mf, "noSpeeds")) == eph::kMetaNoSpeeds &&
              (1u << RegNum(mf, "noDistance")) == eph::kMetaNoDistance &&
              (1u << RegNum(mf, "ratesApprox")) == eph::kMetaRatesApprox &&
              ((1u << mf.size()) - 1u) == eph::kMetaFlagMask,
          "A.18 META flags agree with the codec");
    auto err = Registry("error_codes");
    Check(err.size() == 12 && RegNum(err, "malformed or non-canonical") == eph::kErrMalformed &&
              RegNum(err, "over a WELCOME limit") == eph::kErrLimits &&
              RegNum(err, "unknown message type") == eph::kErrUnknownType &&
              RegNum(err, "internal") == eph::kErrInternal &&
              RegNum(err, "source, data or pin unavailable") == eph::kErrSource &&
              RegNum(err, "rate limited") == eph::kErrRateLimited &&
              RegNum(err, "token required or unknown") == eph::kErrToken &&
              RegNum(err, "version") == eph::kErrVersion && RegNum(err, "busy") == eph::kErrBusy &&
              RegNum(err, "cancelled") == eph::kErrCancelled &&
              RegNum(err, "unsupported") == eph::kErrUnsupported &&
              RegNum(err, "draining") == eph::kErrDraining,
          "A.19 ERROR codes agree with the codec");
    auto prec = Registry("precession_model_tokens");
    Check(prec.size() == 2 && prec[0].token == "iau2006" && prec[1].token == "vondrak2011",
          "A.20 precession model tokens");
  }

  // Zodiac registry: the Swiss mode numbers are the token indices.
  Check(eph::ZodiacIndex("fagan-bradley") == 0 && eph::ZodiacIndex("lahiri") == 1 &&
            eph::ZodiacIndex("lahiri-icrc") == 46 && eph::ZodiacIndex("user") == 47,
        "zodiac tokens in SE_SIDM order");

  if (g_fail) {
    printf("EPHPROTO FAIL: %d failed, %d passed\n", g_fail, g_pass);
    return 1;
  }
  printf("EPHPROTO PASS: %d checks, %d fixtures\n", g_pass, cFixtures);
  return 0;
}
