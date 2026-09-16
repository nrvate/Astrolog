// astrolog-ephd: the ephemeris WebSocket service.
//
// Implements EPHEMERIS_SERVER_PLAN.md Part I sections 2-8 and 11: a uWS App
// per event-loop thread (kernel SO_REUSEPORT balances accepts), a private
// slice of the Swiss Ephemeris thread-safe fork's context pool per loop,
// the packed binary protocol of ephproto.h, heartbeats, and zero-
// configuration ephemeris path discovery. One thread at a time per swe_ctx;
// per-request configuration uses ONLY the _r scoped setters; swe_close() is
// never called and no ephemeris fallback exists (the fork's strict default;
// swe_set_ephe_fallback is deliberately never called).
//
// Sections in this file:
//   1. startup options (plan 11)
//   2. ephemeris path discovery (plan 6, 7) -- standalone, no engine code
//   3. request execution (plan 5) -- one swe_ctx per request, pure function
//   4. loop wiring -- uWS App, per-loop state, message handlers, backpressure
//   5. heartbeats -- server PING every 20s, PONG deadline 60s (plan 4.7)
//   6. main

#include "ephproto.h"

#include <App.h>

#include <swephexp.h>

#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using uWS::WebSocket;

// ---------------------------------------------------------------------------
// 1. Startup options (plan 11)
// ---------------------------------------------------------------------------

struct Options {
  uint16_t port = eph::kDefaultPort;
  int threads = (int)std::thread::hardware_concurrency();
  uint32_t cacheMb = 256;      // accepted, reserved for the result cache
  std::string ephe;            // --ephe, may be ';'-joined
  bool verbose = false;
};

static Options gOpt;

static void Log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void Log(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stdout, fmt, ap);
  va_end(ap);
  fputc('\n', stdout);
  fflush(stdout);
}

// True when nothing was found and file-backed work must fail whole-request
// with ERROR 5 instead of per-object rows.
static bool gHaveEphemeris = false;

static bool parseU(const char *s, uint64_t *out) {
  if (!s || !*s) return false;
  uint64_t v = 0;
  for (const char *p = s; *p; p++) {
    if (*p < '0' || *p > '9') return false;
    if (v > (UINT64_MAX - (uint64_t)(*p - '0')) / 10) return false;
    v = v * 10 + (uint64_t)(*p - '0');
  }
  *out = v;
  return true;
}

static bool parseArgs(int argc, char **argv, Options *opt) {
  for (int i = 1; i < argc; i++) {
    const char *a = argv[i];
    uint64_t v = 0;
    auto needValue = [&](const char **val) -> bool {
      if (i + 1 >= argc) return false;
      *val = argv[++i];
      return true;
    };
    if (strcmp(a, "--port") == 0) {
      const char *val;
      if (!needValue(&val) || !parseU(val, &v) || v == 0 || v > 65535) return false;
      opt->port = (uint16_t)v;
    } else if (strcmp(a, "--threads") == 0) {
      const char *val;
      if (!needValue(&val) || !parseU(val, &v) || v == 0 || v > 1024) return false;
      opt->threads = (int)v;
    } else if (strcmp(a, "--cache-mb") == 0) {
      const char *val;
      if (!needValue(&val) || !parseU(val, &v) || v > 1024 * 1024) return false;
      opt->cacheMb = (uint32_t)v;
    } else if (strcmp(a, "--ephe") == 0) {
      const char *val;
      if (!needValue(&val)) return false;
      opt->ephe = val;
    } else if (strcmp(a, "--verbose") == 0) {
      opt->verbose = true;
    } else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
      printf("usage: astrolog-ephd [--port N] [--ephe path] [--threads N] "
             "[--cache-mb N] [--verbose]\n");
      exit(0);
    } else {
      return false;
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// 2. Ephemeris path discovery (plan 6, 7)
//
// Mirrors the client's SwissEnsurePath() without linking any engine code.
// Resolution order: --ephe (may be ';'-joined); then env ASTR<version>,
// ASTROLOG, ASTR; then the -Yi1 "<dir>" line of the settings file; then
// cwd; then the executable's directory; then a compile-time default. Each
// candidate is probed for sentinel files with stat() ONLY -- no opendir, no
// tree walk, ever (plan 7: the production tree holds nearly a million files
// and startup is O(1) in its size). Directories that answer are joined with
// ';' respecting SWE's 20-directory / 242-byte budget and handed to
// swe_set_ephe_path() exactly once at startup, before any context is
// created (contexts inherit process config at swe_ctx_new()).
// ---------------------------------------------------------------------------

// The files Swiss asks a search path for. The "_18" century block ships
// with every installation and answers nearly every probe; the other sepl_*
// blocks recognise installations covering only distant dates. Each costs
// one stat() on a directory that already failed the common cases.
static const char *const kSentinels[] = {
  "sepl_18.se1", "semo_18.se1", "seas_18.se1",
  "sefstars.txt", "seorbel.txt", "seasnam.txt", "sedeltat.txt",
  "sepl_00.se1", "sepl_06.se1", "sepl_12.se1", "sepl_24.se1", "sepl_30.se1",
  "seplm18.se1",
};
static const int cSentinels = (int)(sizeof(kSentinels) / sizeof(kSentinels[0]));

// SWE's own limits (the client's cDirEphemMax / cchEphemPathMax).
static const int kDirEphemMax = 20;
static const size_t kchEphemPathMax = 242;

// Compile-time default, EPHE_DIR's equivalent; empty by default like the
// client's DEFAULT_DIR.
#ifndef EPH_EPHE_DIR_DEFAULT
#define EPH_EPHE_DIR_DEFAULT ""
#endif

struct EphDir {
  std::string dir;
  std::vector<const char *> hits;   // which sentinels this directory holds
  bool explicitDir = false;         // named by --ephe, env, or the settings file
};

struct EphDiscovery {
  std::vector<EphDir> dirs;
  std::string resolved;             // ';'-joined directories that answered
  int cJoined = 0;
  int cDropped = 0;                 // hit but dropped for the path budget
};

// Executable's directory, from /proc/self/exe (argv[0]-independent).
static std::string ExeDir() {
  char buf[4096];
  ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (n <= 0 || (size_t)n >= sizeof(buf)) return std::string();
  buf[n] = '\0';
  char *slash = strrchr(buf, '/');
  if (!slash) return std::string();
  *slash = '\0';
  return std::string(buf);
}

// Split a ';'-joined list into non-empty pieces.
static void SplitDirs(const char *sz, std::vector<std::string> *out) {
  const char *p = sz;
  while (p && *p) {
    const char *semi = strchr(p, ';');
    size_t n = semi ? (size_t)(semi - p) : strlen(p);
    if (n > 0) out->push_back(std::string(p, n));
    if (!semi) break;
    p = semi + 1;
  }
}

// The ~30-line settings-file parser: first line starts '@', lines starting
// ';' are comments, params may be double-quoted. Answers the -Yi1 "<dir>"
// line -- the LAST one, because later lines override earlier ones in
// Astrolog's own switch processing. Relative values resolve against the
// executable's directory, not the cwd (the client's rule, calc.cpp:3033).
static bool ParseSettingsYi1(const char *szFile, const std::string &exeDir,
                             std::string *outDir) {
  FILE *file = fopen(szFile, "r");
  if (!file) return false;
  int ch = fgetc(file);
  if (ch != '@') { fclose(file); return false; }
  bool fFound = false;
  char line[4096];
  while (fgets(line, sizeof(line), file)) {
    // Tokenize: whitespace-separated, '"' quoted params.
    const char *tok[3] = {nullptr, nullptr, nullptr};
    size_t cch[3] = {0, 0, 0};
    int cTok = 0;
    for (char *p = line; *p && cTok < 3;) {
      while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
      if (!*p) break;
      const char *start;
      size_t n = 0;
      if (*p == '"') {
        start = ++p;
        while (*p && *p != '"') { p++; n++; }
        if (*p == '"') p++;
      } else {
        start = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') { p++; n++; }
      }
      tok[cTok] = start;
      cch[cTok] = n;
      cTok++;
    }
    if (cTok >= 2 && cch[0] == 4 && strncmp(tok[0], "-Yi1", 4) == 0) {
      outDir->assign(tok[1], cch[1]);
      fFound = true;
    }
  }
  fclose(file);
  if (fFound && !outDir->empty() && (*outDir)[0] != '/')
    *outDir = exeDir + "/" + *outDir;
  return fFound;
}

static void AddCandidate(EphDiscovery *disc, const std::string &dir, bool fExplicit) {
  if (dir.empty()) return;
  for (const EphDir &d : disc->dirs)
    if (d.dir == dir) return;   // dedup; the earliest occurrence has priority
  if (disc->dirs.size() >= 32) return;   // sanity cap on candidates
  EphDir d;
  d.dir = dir;
  d.explicitDir = fExplicit;
  disc->dirs.push_back(std::move(d));
}

static EphDiscovery DiscoverEphemDirs() {
  EphDiscovery disc;
  std::string exeDir = ExeDir();

  // 1. --ephe, possibly ';'-joined. Highest priority.
  if (!gOpt.ephe.empty()) {
    std::vector<std::string> v;
    SplitDirs(gOpt.ephe.c_str(), &v);
    for (const std::string &s : v) AddCandidate(&disc, s, true);
  }

  // 2. Environment: ASTR<version> ("ASTR" + the version with dots stripped,
  //    the client's ENVIRONVER algorithm, plus the plan's example ASTR80),
  //    then ASTROLOG, then ASTR.
  const char *rgszEnv[] = {"ASTR800", "ASTR80", "ASTROLOG", "ASTR"};
  for (const char *szEnv : rgszEnv) {
    const char *env = getenv(szEnv);
    if (env && *env) {
      std::vector<std::string> v;
      SplitDirs(env, &v);
      for (const std::string &s : v) AddCandidate(&disc, s, true);
    }
  }

  // 3. The -Yi1 "<dir>" line of the settings file. The file itself is
  //    looked up in cwd, then the executable's directory, then those env
  //    vars.
  {
    std::vector<std::string> setDirs = {"."};
    if (!exeDir.empty()) setDirs.push_back(exeDir);
    for (const char *szEnv : rgszEnv) {
      const char *env = getenv(szEnv);
      if (env && *env) SplitDirs(env, &setDirs);
    }
    for (const std::string &d : setDirs) {
      std::string file = (d == "." ? std::string(".") : d) + "/astrolog.as";
      struct stat st;
      if (stat(file.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
      std::string dir;
      if (ParseSettingsYi1(file.c_str(), exeDir, &dir)) {
        AddCandidate(&disc, dir, true);
        break;
      }
    }
  }

  // 4. Current working directory, then the executable's directory.
  AddCandidate(&disc, ".", false);
  if (!exeDir.empty()) AddCandidate(&disc, exeDir, false);

  // 5. Compile-time default.
  AddCandidate(&disc, EPH_EPHE_DIR_DEFAULT, false);

  // Probe each candidate for sentinels -- stat() only, never opendir.
  for (EphDir &d : disc.dirs) {
    std::string prefix = d.dir;
    if (prefix.back() != '/') prefix += '/';
    for (int i = 0; i < cSentinels; i++) {
      std::string file = prefix + kSentinels[i];
      struct stat st;
      if (stat(file.c_str(), &st) == 0 && S_ISREG(st.st_mode))
        d.hits.push_back(kSentinels[i]);
    }
  }

  // Join the directories that answered, respecting SWE's budget. Hits only:
  // hand Swiss a path that is known good rather than a list of places one
  // might be.
  size_t cch = 0;
  for (const EphDir &d : disc.dirs) {
    if (d.hits.empty()) continue;
    size_t need = d.dir.size() + (disc.resolved.empty() ? 0 : 1);
    if (disc.cJoined >= kDirEphemMax || cch + need > kchEphemPathMax) {
      disc.cDropped++;
      continue;
    }
    if (!disc.resolved.empty()) disc.resolved += ';';
    disc.resolved += d.dir;
    cch += need;
    disc.cJoined++;
  }
  return disc;
}

// ---------------------------------------------------------------------------
// 3. Request execution (plan 5)
//
// A request is a pure function of its payload: hash it, (cache lookup goes
// here in a later increment), then for each time row and object call the
// thread-safe fork on one private context. Per-object failures are recorded
// in that object's metadata (retFlag < 0 + serr) and never fail the
// request; a whole-request failure is ERROR 5 with the SWE serr text
// (plan 4.7). Per-request configuration uses only the _r scoped setters.
// ---------------------------------------------------------------------------

struct LoopCtx;   // 4.

// One completed request awaiting/chunk-streaming to its client.
struct Stream {
  uint32_t requestId = 0;
  uint8_t precision = eph::kPrecF64;
  uint32_t nObj = 0, nTimeRows = 0, chunkRows = 0;
  uint32_t nextRow = 0;         // next row to send
  uint32_t chunkIndex = 0;
  std::vector<double> cols;     // object-major nObj*nTimeRows*6 f64 values
  std::vector<uint8_t> meta;    // nObj * 128 metadata records
  std::vector<uint8_t> buf;     // chunk scratch, sized once, reused
};

struct Conn {
  bool greeted = false;
  std::deque<Stream> out;       // completed streams, FIFO
};

// Server version string for WELCOME.
static const char kServerVersion[] = "astrolog-ephd/1.0";

// Pack SWE version "2.10.03-ts.10" into major*10000+minor*100+patch.
static uint32_t PackSweVersion(const char *szVersion) {
  int a = 0, b = 0, c = 0;
  if (sscanf(szVersion, "%d.%d.%d", &a, &b, &c) != 3) return 0;
  return (uint32_t)(a * 10000 + b * 100 + c);
}

// Apply the per-request configuration triplets to ctx using ONLY the _r
// scoped setters (never the process-global forms; concurrent requests with
// different sidereal/topocentric settings must not interfere). Stale
// settings are harmless: SWE consults each only when the matching iflag bit
// is set on the call.
static void ApplyRequestConfig(swe_ctx *ctx, const eph::Request &req) {
  if (req.iflag & SEFLG_SIDEREAL)
    swe_set_sid_mode_r(ctx, (int32_t)req.sidMode, req.sidT0, req.sidAyanOff);
  if (req.iflag & SEFLG_TOPOCTR)
    swe_set_topo_r(ctx, req.topoLon, req.topoLat, req.topoElv);
  if (req.iflag & SEFLG_JPLEPH)
    swe_set_jpl_file_r(ctx, req.jplFile);
}

// Compute one (object, row) cell. Returns the SWE return flag; on failure
// serr is filled. kind 0 bodies go through swe_calc_ut_r, or swe_calc_pctr
// when the request names a center body; kind 1 fixed stars through
// swe_fixstar_ut_r (which also resolves the name in place).
static int32_t ComputeCell(swe_ctx *ctx, const eph::Request &req,
                           const eph::ObjSpec &obj, double jdUt,
                           uint32_t iflag, double xx[6], char *serr,
                           const char **pName, char *nameBuf, size_t nameCap) {
  serr[0] = '\0';
  *pName = nullptr;
  if (obj.kind == eph::kObjStar) {
    if (req.center != 0) {
      // No pctr form exists for fixed stars; say so rather than guess.
      snprintf(serr, 256, "center body not supported for fixed stars");
      return -1;
    }
    snprintf(nameBuf, nameCap, "%s", obj.name);
    int32_t ret = swe_fixstar_ut_r(ctx, nameBuf, jdUt, (int32_t)iflag, xx, serr);
    if (ret >= 0) *pName = nameBuf;   // resolved full star name
    return ret;
  }
  int32_t ret;
  if (req.center != 0)
    ret = swe_calc_pctr_r(ctx, jdUt, (int32_t)obj.id, req.center, (int32_t)iflag, xx, serr);
  else
    ret = swe_calc_ut_r(ctx, jdUt, (int32_t)obj.id, (int32_t)iflag, xx, serr);
  if (ret >= 0) {
    char nm[96];
    swe_get_planet_name_r(ctx, (int)obj.id, nm);
    snprintf(nameBuf, nameCap, "%s", nm);
    *pName = nameBuf;
  }
  return ret;
}

// Execute one REQUEST into a Stream. Returns false for a whole-request
// failure and fills errCode/errText (plan 4.6/4.7). The only whole-request
// failure is the missing-ephemeris-path case: with no ephemeris directory
// found at startup and SEFLG_SWIEPH forced, every object would fail with
// the same file-not-found serr, so the first probe's serr becomes the
// request-level ERROR 5 text and nothing is streamed.
static bool ExecuteRequest(LoopCtx *lc, const eph::Request &req,
                           Stream *stream, int32_t *errCode,
                           std::string *errText);

// ---------------------------------------------------------------------------
// 4. Loop wiring
//
// One uWS App per event-loop thread; SO_REUSEPORT load-balances accepted
// connections across loops. Each loop owns a private slice of the context
// pool (pool total 2x cores) and will own a private LRU result cache in a
// later increment -- LoopCtx is the structure that cache slots into.
// ---------------------------------------------------------------------------

struct LoopCtx {
  int index = 0;
  uWS::Loop *loop = nullptr;
  std::unique_ptr<uWS::App> app;
  std::vector<swe_ctx *> pool;   // private slice, never shared
  size_t nextCtx = 0;
  // The per-loop LRU result cache lands here (later increment), keyed on
  // FNV-1a of the canonical REQUEST payload, capped by --cache-mb.

  swe_ctx *TakeContext() {
    if (pool.empty()) return nullptr;
    swe_ctx *ctx = pool[nextCtx % pool.size()];
    nextCtx++;
    return ctx;
  }
};

// Heartbeats are uWS's own: sendPingsAutomatically sends WebSocket protocol
// pings on the idle timeout's cadence and closes silent connections (plan
// 4.7's intent; QWebSocket answers protocol pings by itself, so a Qt client
// never sees an app-level PING). The app-level kMsgPing/kMsgPong types stay
// in the protocol for clients without automatic pong.
static const int kIdleTimeoutSeconds = 30;

static WebSocket<false, true, Conn>::SendStatus
SendEnvelope(WebSocket<false, true, Conn> *ws, uint16_t type,
             uint32_t requestId, const void *payload,
             size_t payloadLen, uint8_t flags = 0) {
  std::vector<uint8_t> msg(eph::kEnvelopeSize + payloadLen);
  eph::writeEnvelope(msg.data(), type, requestId, flags, (uint32_t)payloadLen);
  if (payloadLen) memcpy(msg.data() + eph::kEnvelopeSize, payload, payloadLen);
  return ws->send(std::string_view((char *)msg.data(), msg.size()),
                  uWS::OpCode::BINARY);
}

static void SendError(WebSocket<false, true, Conn> *ws, uint32_t requestId,
                      int32_t code, const char *text) {
  uint8_t fixed[8];
  eph::putU32(fixed, requestId);
  eph::putU32(fixed + 4, (uint32_t)code);
  size_t n = text ? strlen(text) : 0;
  std::vector<uint8_t> payload(8 + n + 1);
  memcpy(payload.data(), fixed, 8);
  memcpy(payload.data() + 8, text ? text : "", n + 1);
  Log("ephd: ERROR %d to request %u: %.120s", code, requestId,
      text ? text : "");
  SendEnvelope(ws, eph::kMsgError, requestId, payload.data(), payload.size());
}

// Execute one REQUEST into a Stream. Returns false for a whole-request
// failure and fills errCode/errText (plan 4.6/4.7). The only whole-request
// failure is the missing-ephemeris-path case: with no ephemeris directory
// found at startup and SEFLG_SWIEPH forced, every object fails the same
// file-not-found way, so the request fails whole with ERROR 5. Per-object
// failures never fail the request: they zero that object's rows and set
// its metadata retFlag < 0 with the SWE serr text (plan 4.5).
static bool ExecuteRequest(LoopCtx *lc, const eph::Request &req,
                           Stream *stream, int32_t *errCode,
                           std::string *errText) {
  if (!gHaveEphemeris) {
    *errCode = eph::kErrEphemeris;
    *errText = "no ephemeris directory found at startup; start the server "
               "with --ephe or configure the client's -Yi1";
    return false;
  }
  swe_ctx *ctx = lc->TakeContext();
  if (ctx == nullptr) {
    *errCode = eph::kErrInternal;
    *errText = "no swe context available";
    return false;
  }

  ApplyRequestConfig(ctx, req);

  const uint32_t nObj = stream->nObj, nTime = stream->nTimeRows;
  stream->cols.assign((size_t)nObj * nTime * 6, 0.0);
  stream->meta.assign((size_t)nObj * eph::kDataMetaSize, 0);

  // SWE's iflag is int32; the protocol carries u64 for headroom. SEFLG_SWIEPH
  // is forced (plan 4.4) and SEFLG_SPEED added if absent, so speeds arrive
  // in the columns and clients never derive them.
  int32_t iflag = (int32_t)(uint32_t)(req.iflag & 0xFFFFFFFFu) | SEFLG_SWIEPH;
  iflag |= SEFLG_SPEED;

  char serr[256];
  for (uint32_t o = 0; o < nObj; o++) {
    const eph::ObjSpec &obj = req.objs[o];
    char nameBuf[eph::kObjNameMax];
    const char *name = nullptr;
    int32_t retRow0 = 0;
    bool fFailed = false, fSawSuccess = false;
    char failSerr[eph::kSerrMax] = {0};
    for (uint32_t r = 0; r < nTime; r++) {
      double jdUt = req.jdStart +
        (double)((uint64_t)r * (uint64_t)req.stepSeconds) / 86400.0;
      double xx[6];
      int32_t ret = ComputeCell(ctx, req, obj, jdUt, (uint32_t)iflag, xx, serr,
                                &name, nameBuf, sizeof(nameBuf));
      if (ret < 0) {
        fFailed = true;
        if (failSerr[0] == '\0')
          snprintf(failSerr, sizeof(failSerr), "%.63s", serr);
        continue;   // this object's cells for this row stay zeroed
      }
      if (!fSawSuccess) {
        fSawSuccess = true;
        retRow0 = ret;
      }
      double *dst = stream->cols.data() + ((size_t)o * nTime + r) * 6;
      for (int c = 0; c < 6; c++) dst[c] = xx[c];
    }
    eph::writeDataMeta(stream->meta.data() + (size_t)o * eph::kDataMetaSize,
                       fFailed ? -1 : retRow0, retRow0,
                       fFailed ? failSerr : nullptr, fSawSuccess ? name : nullptr);

    // Drop this object's file caches before the next body: the fork's
    // cached per-body segments poison cross-body answers on a shared
    // context at historical dates (pre-1972, where the delta-t table is
    // in force -- measured: same ctx, Moon then Mercury, differs from a
    // fresh context by ~0.05 arcsec; swe_close_r between bodies restores
    // fresh-context answers exactly). close_r releases files without
    // touching the published config, so the pool ctx stays usable.
    swe_close_r(ctx);
  }
  return true;
}

// Stream completed chunks to the client, honoring uWS backpressure: stop
// at the first BACKPRESSURE and resume from the drain callback. Chunks are
// chunkRows-sized pieces, ascending chunkIndex, contiguous row ranges. The
// columns are object-major (nObj blocks of nTime*6), so writeDataChunk
// gathers each object's own row range out of the full column array.
static void FlushStreams(WebSocket<false, true, Conn> *ws) {
  Conn *c = (Conn *)ws->getUserData();
  while (!c->out.empty()) {
    Stream &s = c->out.front();
    while (s.nextRow < s.nTimeRows) {
      uint32_t rows = s.nTimeRows - s.nextRow;
      if (rows > s.chunkRows) rows = s.chunkRows;
      size_t chunkLen = 0;
      eph::writeDataChunk(s.buf.data(), s.buf.size(), s.chunkIndex, s.nextRow,
                          rows, s.nTimeRows, s.nObj, s.precision, s.meta.data(),
                          s.cols.data(), &chunkLen);
      // The chunk buffer holds the DATA payload; SendEnvelope wraps it in
      // the 16-byte envelope the protocol requires.
      auto st = SendEnvelope(ws, eph::kMsgData, s.requestId, s.buf.data(),
                             chunkLen);
      if (st == WebSocket<false, true, Conn>::SendStatus::BACKPRESSURE ||
          st == WebSocket<false, true, Conn>::SendStatus::DROPPED)
        return;   // resume on drain; DROPPED means the peer is going away
      s.nextRow += rows;
      s.chunkIndex++;
    }
    c->out.pop_front();
  }
}

// Execute + enqueue one REQUEST (already decoded and limit-checked).
static void RunRequest(WebSocket<false, true, Conn> *ws, LoopCtx *lc,
                       const eph::Envelope &env, const eph::Request &req) {
  Conn *c = (Conn *)ws->getUserData();

  Stream s;
  s.requestId = env.requestId;
  s.precision = req.precision;
  s.nObj = (uint32_t)req.objs.size();
  s.nTimeRows = req.nTime;
  uint32_t hint = req.chunkRows ? req.chunkRows : eph::kMaxChunkRows;
  s.chunkRows = hint > eph::kMaxChunkRows ? eph::kMaxChunkRows : hint;
  if (s.chunkRows == 0) s.chunkRows = 1;

  int32_t errCode = 0;
  std::string errText;
  if (!ExecuteRequest(lc, req, &s, &errCode, &errText)) {
    SendError(ws, env.requestId, errCode, errText.c_str());
    return;
  }

  s.buf.resize(eph::dataPayloadSize(s.nObj, (size_t)s.chunkRows, s.precision));
  c->out.push_back(std::move(s));
  if (gOpt.verbose)
    Log("ephd: request %u -> %.1f KiB columns, chunks of %u rows",
        env.requestId,
        (double)(s.nObj * s.nTimeRows * 6 * 8) / 1024.0, s.chunkRows);
  FlushStreams(ws);
}

// Handle one decoded WebSocket message. The envelope is checked for size
// agreement first; a payload-length mismatch is a protocol error, not a
// truncation to recover from. HELLO is answered with WELCOME once; REQUESTs
// are answered whether or not HELLO came first -- the connection is
// stateless in every way that matters (plan 4.7).
static void HandleMessage(WebSocket<false, true, Conn> *ws,
                          std::string_view message, LoopCtx *lc) {
  if (message.size() < eph::kEnvelopeSize) {
    SendError(ws, 0, eph::kErrBad, "message shorter than the envelope");
    return;
  }
  eph::Envelope env;
  if (!eph::parseEnvelope((const uint8_t *)message.data(), &env)) {
    SendError(ws, 0, eph::kErrBad, "bad magic or protocol version");
    return;
  }
  if (env.flags & ~eph::kEnvFlagMask ||
      env.payloadLen > eph::kMaxPayload ||
      eph::kEnvelopeSize + (size_t)env.payloadLen != message.size()) {
    SendError(ws, env.requestId, eph::kErrBad, "payload length mismatch");
    return;
  }
  const uint8_t *pl = (const uint8_t *)message.data() + eph::kEnvelopeSize;
  Conn *c = (Conn *)ws->getUserData();

  switch (env.type) {
    case eph::kMsgHello: {
      if (!c->greeted) {
        c->greeted = true;
        static thread_local char szSweVersion[256];
        swe_version(szSweVersion);
        uint8_t wbuf[sizeof(eph::WelcomeWire) + 256];
        uint32_t wlen = 0;
        eph::buildWelcome(wbuf, eph::kCapFloat32,
                          PackSweVersion(szSweVersion), kServerVersion, &wlen);
        SendEnvelope(ws, eph::kMsgWelcome, env.requestId, wbuf, wlen);
      }
      break;
    }
    case eph::kMsgRequest: {
      eph::Request req;
      eph::ParseResult pr = eph::parseRequest(pl, env.payloadLen, &req);
      if (pr == eph::kParseBad) {
        SendError(ws, env.requestId, eph::kErrBad, "malformed REQUEST");
        return;
      }
      if (pr == eph::kParseLimits) {
        SendError(ws, env.requestId, eph::kErrLimits,
                  "REQUEST exceeds the limits WELCOME advertised");
        return;
      }
      RunRequest(ws, lc, env, req);
      break;
    }
    case eph::kMsgPing: {
      SendEnvelope(ws, eph::kMsgPong, env.requestId, nullptr, 0);
      break;
    }
    case eph::kMsgPong:
      break;   // the client's answer to our ping; liveness is uWS's job
    default:
      SendError(ws, env.requestId, eph::kErrUnknown, "unknown message type");
      break;
  }
}

static thread_local LoopCtx *tlc = nullptr;   // this thread's loop state

static void OnMessage(WebSocket<false, true, Conn> *ws, std::string_view message,
                      uWS::OpCode) {
  HandleMessage(ws, message, tlc);
}

// Per-loop wiring: the thread-local LoopCtx pointer, the App with the
// WebSocket behavior (per-socket Conn constructed in open, message handler,
// drain resuming the stream), and the listen socket (SO_REUSEPORT lets the
// kernel balance accepts across the per-thread listeners).
static void SetupLoop(LoopCtx *lc) {
  tlc = lc;
  lc->loop = uWS::Loop::get();
  lc->app = std::make_unique<uWS::App>();

  uWS::App::WebSocketBehavior<Conn> behavior;
  behavior.compression = uWS::DISABLED;
  behavior.maxPayloadLength = (unsigned)(eph::kMaxPayload + 65536);
  behavior.idleTimeout = kIdleTimeoutSeconds;
  behavior.maxBackpressure = 4 * 1024 * 1024;
  behavior.sendPingsAutomatically = true;
  behavior.open = [](WebSocket<false, true, Conn> *ws) {
    new ((Conn *)ws->getUserData()) Conn();
  };
  behavior.message = [](WebSocket<false, true, Conn> *ws,
                        std::string_view message, uWS::OpCode op) {
    OnMessage(ws, message, op);
  };
  behavior.drain = [](WebSocket<false, true, Conn> *ws) {
    FlushStreams(ws);
  };
  lc->app->ws<Conn>("/*", std::move(behavior));
  lc->app->listen((int)gOpt.port, [](us_listen_socket_t *) {});
}

int main(int argc, char **argv) {
  if (!parseArgs(argc, argv, &gOpt)) {
    fprintf(stderr, "usage: astrolog-ephd [--port N] [--ephe path] "
            "[--threads N] [--cache-mb N] [--verbose]\n");
    return 1;
  }

  EphDiscovery disc = DiscoverEphemDirs();
  char szVersion[256];
  swe_version(szVersion);
  Log("astrolog-ephd %s, Swiss Ephemeris %s", kServerVersion, szVersion);
  Log("%d event loop(s), context pool %d", gOpt.threads,
      2 * (int)std::thread::hardware_concurrency());
  Log("ephemeris path: %s",
      disc.resolved.empty() ? "<none found; file-backed requests fail>" :
      disc.resolved.c_str());
  if (gOpt.verbose) {
    for (const EphDir &d : disc.dirs) {
      Log("  candidate %s%s: %s", d.dir.c_str(),
          d.explicitDir ? " (explicit)" : "",
          d.hits.empty() ? "no sentinels" : d.hits.front());
      for (size_t i = 1; i < d.hits.size(); i++)
        Log("    %s", d.hits[i]);
    }
    if (disc.cDropped)
      Log("  %d directory(ies) dropped for SWE's 20-dir/242-byte budget",
          disc.cDropped);
  }

  // The one process-global SWE call, before any context exists: contexts
  // inherit config at swe_ctx_new(). An empty path is legal -- strict
  // no-fallback means file-backed requests fail with ERROR 5 and their
  // serr text, and the discovery log above said why.
  swe_set_ephe_path(disc.resolved.c_str());
  gHaveEphemeris = !disc.resolved.empty();

  int nLoops = gOpt.threads;
  int totalCtx = 2 * (int)std::thread::hardware_concurrency();
  if (totalCtx < 2) totalCtx = 2;
  std::vector<LoopCtx> loops(nLoops);
  for (int i = 0; i < nLoops; i++) {
    loops[i].index = i;
    int n = totalCtx / nLoops + (i < totalCtx % nLoops ? 1 : 0);
    for (int j = 0; j < n; j++) {
      swe_ctx *ctx = swe_ctx_new();
      if (ctx == nullptr) {
        Log("ephd: swe_ctx_new() failed; reducing the pool");
        break;
      }
      loops[i].pool.push_back(ctx);
    }
  }

  std::vector<std::thread> threads;
  for (int i = 1; i < nLoops; i++) {
    threads.emplace_back([&loops, i] {
      SetupLoop(&loops[i]);
      loops[i].app->run();
    });
  }
  SetupLoop(&loops[0]);
  loops[0].app->run();
  for (std::thread &t : threads) t.join();

  for (LoopCtx &lc : loops)
    for (swe_ctx *ctx : lc.pool) {
      swe_close_r(ctx);   // releases file handles; never swe_close(), which
      swe_ctx_free(ctx);  // resets the process-wide config (fork contract)
    }
  return 0;
}
