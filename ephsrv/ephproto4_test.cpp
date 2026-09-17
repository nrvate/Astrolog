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
